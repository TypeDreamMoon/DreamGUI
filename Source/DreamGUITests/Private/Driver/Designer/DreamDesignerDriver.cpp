// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamDesignerDriver.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Designer/DreamWidgetEditorHierarchyViewItem.h"//FHierarchyDreamWidgetDragDropOp, what a hierarchy drag carries
#include "Designer/SDreamWidgetDesignerViewport.h"
#include "Designer/SDreamWidgetPalette.h"//FDreamUIPaletteDragDropOp, what a palette drag carries
#include "DreamUIControlRegistry.h"

#include "Core/Components/DreamImage.h"//the one visual a palette row asks for a default sprite
#include "Core/Components/DreamWidget.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Blueprint.h"
#include "GameTime.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "SceneView.h"
#include "Slate/SceneViewport.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDesignerDriver, Log, All);

namespace DreamTests
{
	namespace DreamDesignerDriverLocal
	{
		/**
		 * The palette row that places InClass, asked of the registry the palette itself lists from.
		 *
		 * Three kinds of row can name a class and they are asked in the order a caller means them:
		 * a control class ("Button" is UDreamButton), then a panel by its layout container
		 * ("Overlay" is UDreamLayoutContainerOverlay), then a visual. Looking the row up rather
		 * than building a descriptor by hand is what keeps a drop here identical to a drop an
		 * author performs -- the recipe, the validation and the placement rule are the registry's.
		 */
		FName FindPaletteRowForClass(const UClass* InClass)
		{
			if (InClass == nullptr)
			{
				return NAME_None;
			}
			const TArray<FDreamUIControlDescriptor>& Descriptors = FDreamUIControlRegistry::Get().GetDescriptors();
			for (const FDreamUIControlDescriptor& Descriptor : Descriptors)
			{
				if (Descriptor.ControlClass.Get() == InClass)
				{
					return Descriptor.Name;
				}
			}
			for (const FDreamUIControlDescriptor& Descriptor : Descriptors)
			{
				if (Descriptor.LayoutContainerClass.Get() == InClass)
				{
					return Descriptor.Name;
				}
			}
			for (const FDreamUIControlDescriptor& Descriptor : Descriptors)
			{
				if (Descriptor.VisualClass.Get() == InClass)
				{
					return Descriptor.Name;
				}
			}
			return NAME_None;
		}

		/**
		 * Whether a palette row asks the new element for the default sprite.
		 *
		 * The palette's own answer, stated once so the two operation builders below cannot drift
		 * apart from it or from each other: CollectBasics hands bDefaultSprite=true to exactly one
		 * row, the one whose visual is a UDreamImage (SDreamWidgetPalette.cpp:228-231), and every
		 * other row -- basic or registry -- leaves the flag at its false default. A visual nobody
		 * can put a sprite on has nothing to be given one.
		 */
		bool ShouldSetDefaultSprite(const UClass* InVisualClass)
		{
			return InVisualClass != nullptr && InVisualClass->IsChildOf(UDreamImage::StaticClass());
		}

		/**
		 * The object a palette drag carries for a registry row, filled exactly as the palette's own
		 * drag-detected handler fills it.
		 *
		 * FDragDropOperation::Construct is deliberately NOT called. All it does is put a cursor
		 * decorator window on screen, and a synthesised drag has no cursor to decorate; everything
		 * the drop path reads is the plain data below. The tooltip pair is set anyway, because the
		 * hierarchy validator both writes the hover text and resets it to a default, and a default
		 * that was never recorded clears the decorator instead of restoring it.
		 */
		TSharedPtr<FDreamUIPaletteDragDropOp> MakeRegistryOp(FName InRowName)
		{
			const FDreamUIControlDescriptor* Found = FDreamUIControlRegistry::Get().GetDescriptors().FindByPredicate(
				[InRowName](const FDreamUIControlDescriptor& Item) { return Item.Name == InRowName; });
			if (Found == nullptr)
			{
				return nullptr;
			}
			TSharedPtr<FDreamUIPaletteDragDropOp> Operation = MakeShared<FDreamUIPaletteDragDropOp>();
			Operation->bIsBasicWidget = false;
			Operation->VisualClass = Found->VisualClass;
			// Carried for the same reason the palette carries it on every row: only the basic branch
			// of CreateElement reads it, so on a registry row it is inert -- and a field left unset
			// on one of two nearly identical builders is how the two stop being identical.
			Operation->bSetDefaultSprite = ShouldSetDefaultSprite(Found->VisualClass.Get());
			Operation->NativeDescriptor = MakeShared<FDreamUIControlDescriptor>(*Found);
			Operation->WidgetClassPath = Found->WidgetClassPath;
			Operation->DisplayName = Found->DisplayName.ToString();
			Operation->SetToolTip(Found->DisplayName, nullptr);
			Operation->SetupDefaults();
			return Operation;
		}

		/**
		 * A row of the Basic group: a UDreamWidget plus a visual, or no visual at all for the plain
		 * Widget row. The default-sprite flag is the one thing that differs between those rows, and
		 * it is computed rather than passed in so a caller cannot get it wrong.
		 */
		TSharedPtr<FDreamUIPaletteDragDropOp> MakeBasicOp(UClass* InVisualClass)
		{
			TSharedPtr<FDreamUIPaletteDragDropOp> Operation = MakeShared<FDreamUIPaletteDragDropOp>();
			Operation->bIsBasicWidget = true;
			Operation->VisualClass = InVisualClass;
			Operation->bSetDefaultSprite = ShouldSetDefaultSprite(InVisualClass);
			Operation->DisplayName = InVisualClass != nullptr ? InVisualClass->GetName() : TEXT("Widget");
			Operation->SetToolTip(FText::FromString(Operation->DisplayName), nullptr);
			Operation->SetupDefaults();
			return Operation;
		}

		/** A geometry can be asked for a position only while it has an extent to measure one against. */
		bool HasUsableExtent(const FGeometry& InGeometry)
		{
			const FVector2D LocalSize = InGeometry.GetLocalSize();
			return LocalSize.X > 0.0 && LocalSize.Y > 0.0;
		}
	}

	TSharedPtr<FDreamDesignerDriver> FDreamDesignerDriver::Open(UBlueprint* InBlueprint)
	{
		if (!::IsValid(InBlueprint))
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("No Blueprint to open a designer for."));
			return nullptr;
		}
		if (GEditor == nullptr)
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("There is no editor engine, so there is no designer to open."));
			return nullptr;
		}
		UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditors == nullptr)
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("The asset editor subsystem is not up."));
			return nullptr;
		}

		AssetEditors->OpenEditorForAsset(InBlueprint);
		IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(InBlueprint, /*bFocusIfOpen*/false);
		if (Instance == nullptr)
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("No toolkit opened for %s."), *InBlueprint->GetName());
			return nullptr;
		}
		// The same cast every designer test already makes: the asset type only ever opens in this
		// toolkit, and the subsystem's interface has no way to say so.
		FDreamWidgetBlueprintEditor* Designer = static_cast<FDreamWidgetBlueprintEditor*>(Instance);

		TSharedPtr<SDreamWidgetDesignerViewport> Shell = Designer->GetViewportWidget();
		if (!Shell.IsValid())
		{
			UE_LOG(LogDreamDesignerDriver, Warning,
				TEXT("The toolkit for %s has no designer viewport; its Designer mode never built one."), *InBlueprint->GetName());
			return nullptr;
		}
		TSharedPtr<FSceneViewport> Viewport = Shell->GetSceneViewport();
		if (!Viewport.IsValid())
		{
			UE_LOG(LogDreamDesignerDriver, Warning,
				TEXT("The designer viewport shell for %s holds no scene viewport."), *InBlueprint->GetName());
			return nullptr;
		}

		TSharedPtr<FDreamDesignerDriver> Driver = MakeShareable(new FDreamDesignerDriver());
		Driver->WeakBlueprint = InBlueprint;
		Driver->ToolkitPtr = Designer;
		Driver->WeakViewportShell = Shell;
		Driver->WeakSceneViewport = Viewport;
		return Driver;
	}

	FDreamDesignerDriver::~FDreamDesignerDriver()
	{
		Close();
	}

	void FDreamDesignerDriver::Close()
	{
		// Deliberately not ticking Slate here, though the close is deferred and the toolkit lives on
		// until it is pumped: this driver is run from latent commands, and the next engine frame is
		// the pump. Calling into FSlateApplication::Tick from inside a frame it is already running
		// would re-enter it.
		if (GEditor != nullptr)
		{
			if (UBlueprint* Blueprint = WeakBlueprint.Get())
			{
				if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					AssetEditors->CloseAllEditorsForAsset(Blueprint);
				}
			}
		}
		ToolkitPtr = nullptr;
		WeakViewportShell.Reset();
		WeakSceneViewport.Reset();
		PressedButtons.Reset();
		WeakBlueprint.Reset();
	}

	bool FDreamDesignerDriver::IsUsable() const
	{
		return ToolkitPtr != nullptr && WeakViewportShell.IsValid() && WeakSceneViewport.IsValid();
	}

	bool FDreamDesignerDriver::IsToolkitOpen() const
	{
		if (ToolkitPtr == nullptr || GEditor == nullptr)
		{
			return false;
		}
		UBlueprint* Blueprint = WeakBlueprint.Get();
		UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!::IsValid(Blueprint) || AssetEditors == nullptr)
		{
			return false;
		}
		return AssetEditors->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/false) == static_cast<IAssetEditorInstance*>(ToolkitPtr);
	}

	FEditorViewportClient* FDreamDesignerDriver::ViewportClient() const
	{
		TSharedPtr<SDreamWidgetDesignerViewport> Shell = WeakViewportShell.Pin();
		return Shell.IsValid() ? Shell->GetViewportClient().Get() : nullptr;
	}

	bool FDreamDesignerDriver::EnsureHeadlessSize(FIntPoint InSize)
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		if (!Viewport.IsValid() || InSize.X <= 0 || InSize.Y <= 0)
		{
			return false;
		}
		const FIntPoint Measured = Viewport->GetSizeXY();
		if (Measured.X > 0 && Measured.Y > 0)
		{
			// Something already sized it -- a real window, or an earlier call. Leave it alone: the
			// point is to fill a hole, not to overrule whatever laid the designer out.
			return true;
		}
		Viewport->SetFixedViewportSize(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y));
		// A root geometry of the same extent, for the frames in which Slate has cached none: the
		// pixel-to-screen conversion divides by a local size, and an empty one makes every position
		// the origin. Identity layout transform, so a screen position here IS a viewport pixel --
		// the one place this driver is allowed to be synthetic, and it says so in its name.
		SyntheticGeometry = FGeometry::MakeRoot(FVector2D(InSize), FSlateLayoutTransform());
		const FIntPoint Result = Viewport->GetSizeXY();
		return Result.X > 0 && Result.Y > 0;
	}

	FIntPoint FDreamDesignerDriver::ViewportPixelSize() const
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		return Viewport.IsValid() ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	}

	FIntPoint FDreamDesignerDriver::ViewportCentrePixel() const
	{
		const FIntPoint Size = ViewportPixelSize();
		return FIntPoint(Size.X / 2, Size.Y / 2);
	}

	bool FDreamDesignerDriver::GetShellGeometry(FGeometry& OutGeometry) const
	{
		TSharedPtr<SDreamWidgetDesignerViewport> Shell = WeakViewportShell.Pin();
		if (Shell.IsValid())
		{
			OutGeometry = Shell->GetCachedGeometry();
			if (DreamDesignerDriverLocal::HasUsableExtent(OutGeometry))
			{
				return true;
			}
		}
		// Slate's answer first, always; the arranged one only where Slate has none to give.
		if (SyntheticGeometry.IsSet())
		{
			OutGeometry = SyntheticGeometry.GetValue();
			return DreamDesignerDriverLocal::HasUsableExtent(OutGeometry);
		}
		return false;
	}

	bool FDreamDesignerDriver::GetViewportGeometry(FGeometry& OutGeometry) const
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		if (Viewport.IsValid())
		{
			OutGeometry = Viewport->GetCachedGeometry();
			if (DreamDesignerDriverLocal::HasUsableExtent(OutGeometry))
			{
				return true;
			}
		}
		// The viewport caches its geometry when Slate paints or moves over it. Before either has
		// happened there is nothing to measure in, and the shell's frame is the nearest honest
		// answer -- it differs by the toolbar, which is a bounded error rather than a zero.
		return GetShellGeometry(OutGeometry);
	}

	FVector2D FDreamDesignerDriver::PixelToScreenIn(const FGeometry& InGeometry, FIntPoint InPixel) const
	{
		const FIntPoint ViewportSize = ViewportPixelSize();
		const FVector2D LocalSize = InGeometry.GetLocalSize();
		const FVector2D LocalPosition(
			ViewportSize.X > 0 ? InPixel.X * LocalSize.X / ViewportSize.X : 0.0,
			ViewportSize.Y > 0 ? InPixel.Y * LocalSize.Y / ViewportSize.Y : 0.0);
		return InGeometry.LocalToAbsolute(LocalPosition);
	}

	FVector2D FDreamDesignerDriver::PixelToScreen(FIntPoint InPixel) const
	{
		FGeometry Geometry;
		if (!GetShellGeometry(Geometry))
		{
			return FVector2D::ZeroVector;
		}
		return PixelToScreenIn(Geometry, InPixel);
	}

	FIntPoint FDreamDesignerDriver::ScreenToPixel(const FVector2D& InScreenPosition) const
	{
		FGeometry Geometry;
		if (!GetShellGeometry(Geometry))
		{
			return FIntPoint::ZeroValue;
		}
		const FIntPoint ViewportSize = ViewportPixelSize();
		const FVector2D LocalPosition = Geometry.AbsoluteToLocal(InScreenPosition);
		const FVector2D LocalSize = Geometry.GetLocalSize();
		// RoundToInt32 rather than RoundToInt: the same arithmetic the shell's ToViewportPixel does,
		// with the return type an FIntPoint actually holds instead of a silent narrowing from int64.
		return FIntPoint(
			FMath::RoundToInt32(LocalSize.X > 0.0 ? LocalPosition.X * ViewportSize.X / LocalSize.X : 0.0),
			FMath::RoundToInt32(LocalSize.Y > 0.0 ? LocalPosition.Y * ViewportSize.Y / LocalSize.Y : 0.0));
	}

	TOptional<FIntPoint> FDreamDesignerDriver::WidgetPixel(const UDreamWidget* InPreviewWidget) const
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		FEditorViewportClient* Client = ViewportClient();
		if (!::IsValid(InPreviewWidget) || !Viewport.IsValid() || Client == nullptr)
		{
			return TOptional<FIntPoint>();
		}
		if (Viewport->GetSizeXY().X <= 0 || Viewport->GetSizeXY().Y <= 0)
		{
			// A scene view built over an unsized viewport has no pixel rect to land in.
			return TOptional<FIntPoint>();
		}
		// Realtime and time set the way FEditorViewportClient::Draw sets them. SetTime is not
		// decoration: it raises the family's bTimesSet, and a scene view built from a family that
		// never had one asserts on the time fields rather than quietly reading zero.
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Viewport.Get(), Client->GetScene(), Client->EngineShowFlags)
			.SetRealtimeUpdate(Client->IsRealtime())
			.SetTime(FGameTime::GetTimeSinceAppStart()));
		FSceneView* View = Client->CalcSceneView(&ViewFamily);
		if (View == nullptr)
		{
			return TOptional<FIntPoint>();
		}
		const FVector WorldCentre = FDreamWidgetBlueprintEditor::GetWidgetWorldBox(InPreviewWidget).GetCenter();
		const FVector4 ScreenPoint = View->WorldToScreen(WorldCentre);
		// Refusing a point at or behind the eye rather than letting ScreenToPixel mirror it, which
		// is the same guard the designer's own overlay drawing makes for the same reason.
		if (ScreenPoint.W <= 0.0f)
		{
			return TOptional<FIntPoint>();
		}
		FVector2D Pixel = FVector2D::ZeroVector;
		if (!View->ScreenToPixel(ScreenPoint, Pixel))
		{
			return TOptional<FIntPoint>();
		}
		return FIntPoint(FMath::RoundToInt32(Pixel.X), FMath::RoundToInt32(Pixel.Y));
	}

	FPointerEvent FDreamDesignerDriver::MakePointerEvent(const FVector2D& InScreenPosition, const FKey& InEffectingButton) const
	{
		return FPointerEvent(
			FSlateApplicationBase::CursorPointerIndex,
			InScreenPosition,
			LastScreenPosition,
			PressedButtons,
			InEffectingButton,
			/*WheelDelta*/0.0f,
			FModifierKeysState());
	}

	bool FDreamDesignerDriver::DeliverDragDrop(const TSharedPtr<FDragDropOperation>& InOperation, const FPointerEvent& InEvent)
	{
		TSharedPtr<SDreamWidgetDesignerViewport> Shell = WeakViewportShell.Pin();
		if (!Shell.IsValid() || !InOperation.IsValid())
		{
			return false;
		}
		// The resolved frame, not Shell->GetCachedGeometry() directly: the screen position in InEvent
		// was built in whatever GetShellGeometry answered, and handing OnDrop a different frame to
		// measure it in would put the drop somewhere nobody asked for -- or, with an empty cached
		// geometry, at the origin.
		FGeometry Geometry;
		if (!GetShellGeometry(Geometry))
		{
			return false;
		}
		const FDragDropEvent DragDropEvent(InEvent, InOperation);
		// Over first. The design surface resolves the drop target and shows it on the way in, and a
		// drag that never hovered is one the user could not have performed.
		Shell->OnDragOver(Geometry, DragDropEvent);
		return Shell->OnDrop(Geometry, DragDropEvent).IsEventHandled();
	}

	bool FDreamDesignerDriver::DropFromPalette(UClass* InWidgetClass, FIntPoint InPixel)
	{
		if (InWidgetClass == nullptr)
		{
			FGeometry Geometry;
			if (!GetShellGeometry(Geometry))
			{
				return false;
			}
			return DeliverDragDrop(DreamDesignerDriverLocal::MakeBasicOp(nullptr),
				MakePointerEvent(PixelToScreenIn(Geometry, InPixel), FKey()));
		}
		const FName RowName = DreamDesignerDriverLocal::FindPaletteRowForClass(InWidgetClass);
		if (RowName.IsNone())
		{
			UE_LOG(LogDreamDesignerDriver, Warning,
				TEXT("No palette row places %s, so no drag can carry it."), *InWidgetClass->GetName());
			return false;
		}
		return DropFromPalette(RowName, InPixel);
	}

	bool FDreamDesignerDriver::DropFromPalette(FName InPaletteRowName, FIntPoint InPixel)
	{
		TSharedPtr<FDreamUIPaletteDragDropOp> Operation = DreamDesignerDriverLocal::MakeRegistryOp(InPaletteRowName);
		if (!Operation.IsValid())
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("The palette has no row named %s."), *InPaletteRowName.ToString());
			return false;
		}
		FGeometry Geometry;
		if (!GetShellGeometry(Geometry))
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("The designer viewport has no geometry to drop into."));
			return false;
		}
		return DeliverDragDrop(Operation, MakePointerEvent(PixelToScreenIn(Geometry, InPixel), FKey()));
	}

	bool FDreamDesignerDriver::DropFromHierarchy(UDreamWidget* InSource, FIntPoint InPixel)
	{
		if (!::IsValid(InSource))
		{
			return false;
		}
		FGeometry Geometry;
		if (!GetShellGeometry(Geometry))
		{
			UE_LOG(LogDreamDesignerDriver, Warning, TEXT("The designer viewport has no geometry to drop into."));
			return false;
		}
		TArray<UDreamWidget*> Dragged;
		Dragged.Add(InSource);
		const TSharedRef<FHierarchyDreamWidgetDragDropOp> Operation = FHierarchyDreamWidgetDragDropOp::New(Dragged);
		const FPointerEvent PointerEvent = MakePointerEvent(PixelToScreenIn(Geometry, InPixel), FKey());
		const bool bHandled = DeliverDragDrop(Operation, PointerEvent);
		// Slate ends every drag by telling the operation how it went, and this one is listening:
		// it holds an open transaction that an unhandled drop has to cancel. Skipping this leaves
		// the editor's transaction stack open across the rest of the test.
		Operation->OnDrop(bHandled, PointerEvent);
		return bHandled;
	}

	bool FDreamDesignerDriver::MoveTo(FIntPoint InPixel)
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		FGeometry Geometry;
		if (!Viewport.IsValid() || !GetViewportGeometry(Geometry))
		{
			return false;
		}
		const FVector2D ScreenPosition = PixelToScreenIn(Geometry, InPixel);
		const FPointerEvent PointerEvent = MakePointerEvent(ScreenPosition, FKey());
		const FReply Reply = Viewport->OnMouseMove(Geometry, PointerEvent);
		LastScreenPosition = ScreenPosition;
		return Reply.IsEventHandled();
	}

	bool FDreamDesignerDriver::Press(const FKey& InKey)
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		FGeometry Geometry;
		if (!Viewport.IsValid() || !GetViewportGeometry(Geometry))
		{
			return false;
		}
		// The pressed set carries the button on the way down, the way Slate's own does, because the
		// viewport client reads it to tell a drag from a hover.
		PressedButtons.Add(InKey);
		const FPointerEvent PointerEvent = MakePointerEvent(LastScreenPosition, InKey);
		return Viewport->OnMouseButtonDown(Geometry, PointerEvent).IsEventHandled();
	}

	bool FDreamDesignerDriver::Release(const FKey& InKey)
	{
		TSharedPtr<FSceneViewport> Viewport = WeakSceneViewport.Pin();
		FGeometry Geometry;
		if (!Viewport.IsValid() || !GetViewportGeometry(Geometry))
		{
			return false;
		}
		// ... and no longer carries it on the way up, which is the other half of the same rule.
		PressedButtons.Remove(InKey);
		const FPointerEvent PointerEvent = MakePointerEvent(LastScreenPosition, InKey);
		return Viewport->OnMouseButtonUp(Geometry, PointerEvent).IsEventHandled();
	}

	void FDreamDesignerDriver::Compile()
	{
		if (ToolkitPtr != nullptr)
		{
			// Through the toolkit, not FKismetEditorUtilities: the toolkit is what re-instances the
			// preview and re-points every panel afterwards, and that aftermath is the interesting part.
			ToolkitPtr->Compile();
		}
	}

	void FDreamDesignerDriver::Undo()
	{
		if (GEditor != nullptr)
		{
			GEditor->UndoTransaction();
		}
	}

	void FDreamDesignerDriver::Redo()
	{
		if (GEditor != nullptr)
		{
			GEditor->RedoTransaction();
		}
	}

	UDreamWidget* FDreamDesignerDriver::BlueprintRoot() const
	{
		return ToolkitPtr != nullptr ? ToolkitPtr->GetPreviewRootWidget() : nullptr;
	}

	UDreamWidget* FDreamDesignerDriver::PreviewRoot() const
	{
		return ToolkitPtr != nullptr ? ToolkitPtr->GetRootAgentWidget() : nullptr;
	}

	int32 FDreamDesignerDriver::ChildCountUnder(const UDreamWidget* InParent) const
	{
		if (!::IsValid(InParent))
		{
			return 0;
		}
		int32 Count = 0;
		for (const UDreamWidget* Child : InParent->GetChildren())
		{
			if (::IsValid(Child))
			{
				++Count;
			}
		}
		return Count;
	}
}
