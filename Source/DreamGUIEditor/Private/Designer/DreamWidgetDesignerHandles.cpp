// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetDesignerViewportClient.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Designer/DreamDesignerHandles.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/Components/DreamWidget.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "GlobalRenderResources.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

/*
 * The viewport's half of the handles (Designer/DreamDesignerHandles.h): collecting the providers' handles for the selection,
 * drawing them, grabbing one, and turning a drag into one undo step and one write of the .dui. A drag moves the PREVIEW's
 * objects as the pointer goes, so what it changes follows the hand without a rebuild, and writes the properties the provider
 * named to the template through the preview host -- the details panel's own route -- once when it starts (for undo) and once
 * when it ends.
 */
namespace DreamDesignerHandlesViewportLocal
{
	/** How near the pointer has to be to a handle's centre to grab it, in viewport pixels, at the least. */
	constexpr double GrabRadius = 7.0;

	bool WorldToPixel(const FSceneView& InView, const FVector& InWorldPoint, FVector2D& OutPixel)
	{
		const FVector4 Screen = InView.WorldToScreen(InWorldPoint);
		if (Screen.W <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		return InView.ScreenToPixel(Screen, OutPixel);
	}

	FVector LocalToWorld(const UDreamWidget* InWidget, const FVector2D& InLocal)
	{
		return InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
	}

	/** A preview object's value of one property, held until a cancelled drag puts it back. */
	struct FValueSnapshot
	{
		TWeakObjectPtr<UObject> Object;
		FProperty* Property = nullptr;
		void* Value = nullptr;

		FValueSnapshot(UObject* InObject, FProperty* InProperty)
			: Object(InObject), Property(InProperty)
		{
			Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Value);
			Property->CopyCompleteValue(Value, Property->ContainerPtrToValuePtr<void>(InObject));
		}
		~FValueSnapshot()
		{
			Property->DestroyValue(Value);
			FMemory::Free(Value);
		}
		FValueSnapshot(const FValueSnapshot&) = delete;
		FValueSnapshot& operator=(const FValueSnapshot&) = delete;

		void PutBack() const
		{
			if (UObject* Live = Object.Get())
			{
				Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Live), Value);
			}
		}
	};

	FProperty* ResolveProperty(const FDreamDesignerEditedProperty& InEdited)
	{
		const UObject* Object = InEdited.Object.Get();
		return Object != nullptr ? Object->GetClass()->FindPropertyByName(InEdited.Property) : nullptr;
	}

	/** The property written to the template, or with bInIsModify only snapshotted there for undo. */
	bool Migrate(FDreamWidgetBlueprintEditor* InDesigner, const FDreamDesignerEditedProperty& InEdited, bool bInIsModify)
	{
		FProperty* Property = ResolveProperty(InEdited);
		const TSharedPtr<FDreamWidgetPreviewHost> Host = InDesigner != nullptr ? InDesigner->GetPreviewHost() : nullptr;
		if (Property == nullptr || !Host.IsValid())
		{
			return false;
		}
		FEditPropertyChain Chain;
		Chain.AddHead(Property);
		Chain.SetActivePropertyNode(Property);
		Chain.SetActiveMemberPropertyNode(Property);
		return Host->MigratePropertyToTemplate(InEdited.Object.Get(), Chain, bInIsModify);
	}
}

struct FDreamWidgetDesignerViewportClient::FDesignerHandleGesture
{
	TSharedPtr<IDreamDesignerHandleProvider> Provider;
	TWeakObjectPtr<UDreamWidget> HandleWidget;
	FDreamDesignerHandle Handle;
	FDreamDesignerHandleDrag Drag;
	TArray<FDreamDesignerEditedProperty> Properties;
	TArray<TUniquePtr<DreamDesignerHandlesViewportLocal::FValueSnapshot>> Before;
	TUniquePtr<FScopedTransaction> Transaction;
	bool bChanged = false;
};

UDreamWidget* FDreamWidgetDesignerViewportClient::GetHandleWidget() const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = DesignerPtr.Pin();
	if (!Designer.IsValid() || Designer->GetSelectedWidgets().Num() != 1)
	{
		return nullptr;
	}
	UDreamWidget* Selected = Designer->GetSelectedWidgets()[0].Get();
	if (!IsValid(Selected) || Designer->IsWidgetLockedForInteraction(Selected) || !IsAuthoredPreviewWidget(Selected))
	{
		return nullptr;
	}
	return Selected;
}

void FDreamWidgetDesignerViewportClient::CollectDesignerHandles(UDreamWidget* InWidget, const FSceneView& InView, TArray<FDesignerHandleSet>& OutSets) const
{
	OutSets.Reset();
	if (InWidget == nullptr)
	{
		return;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(InView, InWidget);
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : FDreamDesignerHandles::GetProvidersFor(InWidget))
	{
		FDesignerHandleSet& Set = OutSets.AddDefaulted_GetRef();
		Set.Provider = Provider;
		Set.Set.PixelsPerUnit = PixelsPerUnit;
		Provider->CollectHandles(InWidget, Set.Set);
		if (Set.Set.Handles.IsEmpty() && Set.Set.Lines.IsEmpty())
		{
			OutSets.Pop(EAllowShrinking::No);
		}
	}
}

bool FDreamWidgetDesignerViewportClient::PixelToWidgetLocal(const FVector2D& InPixel, const UDreamWidget* InWidget, FVector2D& OutLocal) const
{
	FVector World;
	if (InWidget == nullptr || !IntersectDesignerPlane(InPixel, InWidget->GetWorldTransform(), World))
	{
		return false;
	}
	const FVector Local = InWidget->GetWorldTransform().InverseTransformPosition(World);
	OutLocal = FVector2D(Local.Y, Local.Z);
	return true;
}

double FDreamWidgetDesignerViewportClient::MeasurePixelsPerUnit(const FSceneView& InView, const UDreamWidget* InWidget) const
{
	using namespace DreamDesignerHandlesViewportLocal;
	FVector2D Origin;
	FVector2D Along;
	if (InWidget == nullptr || !WorldToPixel(InView, LocalToWorld(InWidget, FVector2D::ZeroVector), Origin)
		|| !WorldToPixel(InView, LocalToWorld(InWidget, FVector2D(100.0, 0.0)), Along))
	{
		return 1.0;
	}
	return FMath::Max(FVector2D::Distance(Origin, Along) / 100.0, UE_KINDA_SMALL_NUMBER);
}

bool FDreamWidgetDesignerViewportClient::HitTestDesignerHandles(const FVector2D& InPixel, FDesignerHandleHit& OutHit) const
{
	using namespace DreamDesignerHandlesViewportLocal;
	UDreamWidget* HandleWidget = GetHandleWidget();
	if (HandleWidget == nullptr || !Viewport)
	{
		return false;
	}
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = const_cast<FDreamWidgetDesignerViewportClient*>(this)->CalcSceneView(&ViewFamily);
	if (View == nullptr)
	{
		return false;
	}
	TArray<FDesignerHandleSet> Sets;
	CollectDesignerHandles(HandleWidget, *View, Sets);
	if (Sets.IsEmpty())
	{
		return false;
	}
	bool bFound = false;
	double Nearest = 0.0;
	int32 BestPriority = MIN_int32;
	for (const FDesignerHandleSet& Set : Sets)
	{
		for (const FDreamDesignerHandle& Handle : Set.Set.Handles)
		{
			FVector2D Pixel;
			if (!WorldToPixel(*View, LocalToWorld(HandleWidget, Handle.Local), Pixel))
			{
				continue;
			}
			const double Distance = FVector2D::Distance(Pixel, InPixel);
			if (Distance > FMath::Max(GrabRadius, Handle.Radius + 3.0))
			{
				continue;
			}
			if (!bFound || Handle.Priority > BestPriority || (Handle.Priority == BestPriority && Distance < Nearest))
			{
				bFound = true;
				Nearest = Distance;
				BestPriority = Handle.Priority;
				OutHit.Provider = Set.Provider;
				OutHit.Handle = Handle;
			}
		}
	}
	return bFound;
}

bool FDreamWidgetDesignerViewportClient::BeginDesignerHandleDrag(const FDesignerHandleHit& InHit, const FVector2D& InPixel)
{
	using namespace DreamDesignerHandlesViewportLocal;
	UDreamWidget* HandleWidget = GetHandleWidget();
	if (HandleWidget == nullptr || !InHit.Provider.IsValid() || !Viewport)
	{
		return false;
	}
	TSharedPtr<FDesignerHandleGesture> Gesture = MakeShared<FDesignerHandleGesture>();
	Gesture->Provider = InHit.Provider;
	Gesture->HandleWidget = HandleWidget;
	Gesture->Handle = InHit.Handle;
	InHit.Provider->GetEditedProperties(HandleWidget, InHit.Handle.Id, Gesture->Properties);
	Gesture->Properties.RemoveAll([](const FDreamDesignerEditedProperty& InEdited) { return ResolveProperty(InEdited) == nullptr; });
	if (Gesture->Properties.IsEmpty())
	{
		return false;
	}
	Gesture->Drag.HandleId = InHit.Handle.Id;
	Gesture->Drag.HandleLocal = InHit.Handle.Local;
	if (!PixelToWidgetLocal(InPixel, HandleWidget, Gesture->Drag.GrabLocal))
	{
		Gesture->Drag.GrabLocal = InHit.Handle.Local;
	}
	Gesture->Drag.PointerLocal = Gesture->Drag.GrabLocal;
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
		if (const FSceneView* View = CalcSceneView(&ViewFamily))
		{
			Gesture->Drag.PixelsPerUnit = MeasurePixelsPerUnit(*View, HandleWidget);
		}
	}
	Gesture->Transaction = MakeUnique<FScopedTransaction>(InHit.Provider->GetTransactionName(InHit.Handle.Id));
	for (const FDreamDesignerEditedProperty& Edited : Gesture->Properties)
	{
		Gesture->Before.Add(MakeUnique<FValueSnapshot>(Edited.Object.Get(), ResolveProperty(Edited)));
		// The template's value as it is now goes into the undo entry before anything changes it.
		Migrate(DesignerPtr.Pin().Get(), Edited, /*bInIsModify*/ true);
	}
	HandleGesture = Gesture;
	return true;
}

void FDreamWidgetDesignerViewportClient::UpdateDesignerHandleDrag()
{
	FDesignerHandleGesture* Gesture = HandleGesture.Get();
	UDreamWidget* HandleWidget = Gesture != nullptr ? Gesture->HandleWidget.Get() : nullptr;
	if (Gesture == nullptr || HandleWidget == nullptr || !Viewport)
	{
		FinishDesignerHandleDrag(true);
		return;
	}
	FVector2D Pointer;
	if (!PixelToWidgetLocal(FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY()), HandleWidget, Pointer))
	{
		return;
	}
	Gesture->Drag.PointerLocal = Pointer;
	Gesture->Drag.bShift = IsShiftPressed();
	Gesture->Drag.bCtrl = IsCtrlPressed();
	Gesture->Drag.bAlt = IsAltPressed();
	if (Gesture->Provider->UpdateDrag(HandleWidget, Gesture->Drag))
	{
		Gesture->bChanged = true;
		Invalidate(false, false);
	}
}

void FDreamWidgetDesignerViewportClient::FinishDesignerHandleDrag(bool bCancel)
{
	using namespace DreamDesignerHandlesViewportLocal;
	const TSharedPtr<FDesignerHandleGesture> Gesture = HandleGesture;
	HandleGesture.Reset();
	if (!Gesture.IsValid())
	{
		return;
	}
	UDreamWidget* HandleWidget = Gesture->HandleWidget.Get();
	bool bWrite = false;
	if (bCancel || HandleWidget == nullptr)
	{
		if (Gesture->bChanged && HandleWidget != nullptr)
		{
			for (const TUniquePtr<FValueSnapshot>& Snapshot : Gesture->Before)
			{
				Snapshot->PutBack();
			}
			Gesture->Provider->OnRestored(HandleWidget, Gesture->Properties);
		}
		if (HandleWidget != nullptr)
		{
			Gesture->Provider->EndDrag(HandleWidget, Gesture->Drag, /*bInCancelled*/ true);
		}
	}
	else
	{
		bWrite = Gesture->Provider->EndDrag(HandleWidget, Gesture->Drag, /*bInCancelled*/ false) || Gesture->bChanged;
	}
	if (bWrite)
	{
		for (const FDreamDesignerEditedProperty& Edited : Gesture->Properties)
		{
			Migrate(DesignerPtr.Pin().Get(), Edited, /*bInIsModify*/ false);
		}
		FlushTemplateChangesNow();
	}
	else if (Gesture->Transaction.IsValid())
	{
		Gesture->Transaction->Cancel();
	}
	Gesture->Transaction.Reset();
	Invalidate(false, false);
}

bool FDreamWidgetDesignerViewportClient::HandleDesignerDoubleClick(const FVector2D& InPixel)
{
	using namespace DreamDesignerHandlesViewportLocal;
	UDreamWidget* HandleWidget = GetHandleWidget();
	FVector2D Local;
	if (HandleWidget == nullptr || !Viewport || !PixelToWidgetLocal(InPixel, HandleWidget, Local))
	{
		return false;
	}
	double PixelsPerUnit = 1.0;
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
		if (const FSceneView* View = CalcSceneView(&ViewFamily))
		{
			PixelsPerUnit = MeasurePixelsPerUnit(*View, HandleWidget);
		}
	}
	FDesignerHandleHit Hit;
	TArray<TSharedRef<IDreamDesignerHandleProvider>> Candidates;
	int32 HandleId = INDEX_NONE;
	if (HitTestDesignerHandles(InPixel, Hit))
	{
		Candidates.Add(Hit.Provider.ToSharedRef());
		HandleId = Hit.Handle.Id;
	}
	else
	{
		Candidates = FDreamDesignerHandles::GetProvidersFor(HandleWidget);
	}
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : Candidates)
	{
		TArray<FDreamDesignerEditedProperty> Properties;
		Provider->GetEditedProperties(HandleWidget, HandleId, Properties);
		Properties.RemoveAll([](const FDreamDesignerEditedProperty& InEdited) { return ResolveProperty(InEdited) == nullptr; });
		if (Properties.IsEmpty())
		{
			continue;
		}
		FScopedTransaction Transaction(Provider->GetTransactionName(HandleId));
		for (const FDreamDesignerEditedProperty& Edited : Properties)
		{
			Migrate(DesignerPtr.Pin().Get(), Edited, /*bInIsModify*/ true);
		}
		if (!Provider->DoubleClick(HandleWidget, Local, HandleId, PixelsPerUnit))
		{
			Transaction.Cancel();
			continue;
		}
		for (const FDreamDesignerEditedProperty& Edited : Properties)
		{
			Migrate(DesignerPtr.Pin().Get(), Edited, /*bInIsModify*/ false);
		}
		FlushTemplateChangesNow();
		Invalidate(false, false);
		return true;
	}
	return false;
}

void FDreamWidgetDesignerViewportClient::DrawDesignerHandles(FSceneView& View, FCanvas& Canvas) const
{
	using namespace DreamDesignerHandlesViewportLocal;
	UDreamWidget* HandleWidget = GetHandleWidget();
	TArray<FDesignerHandleSet> Sets;
	CollectDesignerHandles(HandleWidget, View, Sets);
	if (Sets.IsEmpty())
	{
		return;
	}
	const float DpiScale = Canvas.GetDPIScale();
	auto ToCanvas = [&View, HandleWidget, DpiScale](const FVector2D& InLocal, FVector2D& OutPixel)
	{
		if (!WorldToPixel(View, LocalToWorld(HandleWidget, InLocal), OutPixel))
		{
			return false;
		}
		OutPixel /= DpiScale;
		return true;
	};
	// The handle the pointer is over, or the one held: drawn larger, in the active colour when held.
	FDesignerHandleHit Hovered;
	const bool bHovering = !HandleGesture.IsValid() && bCursorInViewport
		&& HitTestDesignerHandles(FVector2D(HoverPixel.X, HoverPixel.Y), Hovered);
	for (const FDesignerHandleSet& Set : Sets)
	{
		for (const FDreamDesignerHandleLine& Line : Set.Set.Lines)
		{
			FVector2D From;
			FVector2D To;
			if (ToCanvas(Line.From, From) && ToCanvas(Line.To, To))
			{
				FCanvasLineItem Item(From, To);
				Item.SetColor(Line.Color);
				Item.LineThickness = Line.Thickness;
				Canvas.DrawItem(Item);
			}
		}
	}
	for (const FDesignerHandleSet& Set : Sets)
	{
		for (const FDreamDesignerHandle& Handle : Set.Set.Handles)
		{
			FVector2D Pixel;
			if (!ToCanvas(Handle.Local, Pixel))
			{
				continue;
			}
			const bool bHeld = HandleGesture.IsValid() && HandleGesture->Provider == Set.Provider && HandleGesture->Handle.Id == Handle.Id;
			const bool bHover = bHovering && Hovered.Provider == Set.Provider && Hovered.Handle.Id == Handle.Id;
			const double Radius = Handle.Radius + (bHeld || bHover ? 1.5 : 0.0);
			const FLinearColor Edge = bHeld ? FDreamDesignerHandles::GetActiveColor() : Handle.Outline;
			const FLinearColor Inside = Handle.Fill.A > 0.0f ? Handle.Fill : Edge;
			auto Tile = [&Canvas](const FVector2D& InCentre, double InHalf, const FLinearColor& InColour)
			{
				FCanvasTileItem Item(InCentre - FVector2D(InHalf, InHalf), FVector2D(InHalf, InHalf) * 2.0, InColour);
				Item.BlendMode = SE_BLEND_Translucent;
				Canvas.DrawItem(Item);
			};
			switch (Handle.Shape)
			{
			case EDreamDesignerHandleShape::Square:
				Tile(Pixel, Radius, Edge);
				Tile(Pixel, FMath::Max(Radius - 1.5, 1.0), Inside);
				break;
			case EDreamDesignerHandleShape::Circle:
			case EDreamDesignerHandleShape::Diamond:
			{
				// A fan of triangles: the outline's colour, then the inside a pixel and a half smaller.
				const int32 Sides = Handle.Shape == EDreamDesignerHandleShape::Circle ? 16 : 4;
				const double Turn = Handle.Shape == EDreamDesignerHandleShape::Circle ? 0.0 : UE_DOUBLE_HALF_PI;
				auto Fan = [&Canvas, &Pixel, Sides, Turn](double InRadius, const FLinearColor& InColour)
				{
					TArray<FCanvasUVTri> Triangles;
					for (int32 Side = 0; Side < Sides; ++Side)
					{
						const double A0 = Turn + UE_DOUBLE_TWO_PI * Side / Sides;
						const double A1 = Turn + UE_DOUBLE_TWO_PI * (Side + 1) / Sides;
						FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
						Triangle.V0_Pos = Pixel;
						Triangle.V1_Pos = Pixel + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * InRadius;
						Triangle.V2_Pos = Pixel + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * InRadius;
						Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = InColour;
					}
					FCanvasTriangleItem Item(Triangles, GWhiteTexture);
					Item.BlendMode = SE_BLEND_Translucent;
					Canvas.DrawItem(Item);
				};
				const double Outer = Handle.Shape == EDreamDesignerHandleShape::Diamond ? Radius * 1.3 : Radius;
				Fan(Outer, Edge);
				Fan(FMath::Max(Outer - 1.5, 1.0), Inside);
				break;
			}
			}
		}
	}
}
