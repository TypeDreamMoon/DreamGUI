// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIRenderLayerTable.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamGUISettings.h"
#include "Core/Text/DreamTextPaint.h"
#include "Core/Text/DreamTextPainter.h"
#include "Hash/CityHash.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "UObject/Package.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "DreamUIRender/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0

void UDreamUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

void UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	// Blueprint-callable, and an interface pin left empty arrives here as a null object. This is the
	// only way to subscribe to a culture change, so it is a pin every localised widget touches.
	UObject* Item = InItem.GetObject();
	if (!IsValid(Item))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Register culture changed event was given no object; nothing to register."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (auto Instance = GetInstance(Item->GetWorld()))
	{
		Instance->AllCultureChangedArray.AddUnique(Item);
	}
}
void UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	UObject* Item = InItem.GetObject();
	if (!IsValid(Item))
	{
		// Nothing to take off the list, and the list drops dead weak entries on its own.
		return;
	}
	if (auto Instance = GetInstance(Item->GetWorld()))
	{
		Instance->AllCultureChangedArray.RemoveSingle(Item);
	}
}

TArray<UDreamCanvas*> UDreamUIManagerWorldSubsystem::GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode) const
{
	TArray<UDreamCanvas*> CanvasArray;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (Canvas->GetActualRenderMode() == RenderMode)
		{
			CanvasArray.Add(Canvas.Get());
		}
	}
	return CanvasArray;
}


bool UDreamUIManagerWorldSubsystem::IsCanvasStillRegistered(const TWeakObjectPtr<UDreamCanvas>& InCanvas)const
{
	// The canvas says whom it is registered with: every pass over a thousand world panels asks this once for each, and
	// asking the array made each pass quadratic -- about 45 ms a frame -- and a set of keys a hash for each.
	const UDreamCanvas* Canvas = InCanvas.Get();
	return Canvas != nullptr && Canvas->RegisteredWithManager == this;
}

void UDreamUIManagerWorldSubsystem::AddCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.AddUnique(InCanvas);
	InCanvas->RegisteredWithManager = this;
	// What it asked of a manager before it was here: every canvas starts owing a sort.
	InCanvas->bRenderPrioritySortListed = false;
	if (InCanvas->bNeedToSortRenderPriority)
	{
		InCanvas->bRenderPrioritySortListed = true;
		RenderPrioritySortRequests.Add(InCanvas);
	}
	if (InCanvas->bRenderTargetDrawRequested)
	{
		RenderTargetDrawRequests.Add(InCanvas);
	}
	InvalidateRootCanvasOrder();
	BumpHitTestGeneration();
}

void UDreamUIManagerWorldSubsystem::RemoveCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.RemoveSingle(InCanvas);
	if (InCanvas != nullptr && InCanvas->RegisteredWithManager == this)
	{
		InCanvas->RegisteredWithManager = nullptr;
		// Its requests here go with it: the lists pass over a canvas registered elsewhere or nowhere.
		InCanvas->bRenderPrioritySortListed = false;
	}
	InvalidateRootCanvasOrder();
	BumpHitTestGeneration();
}

void UDreamUIManagerWorldSubsystem::AddRenderTargetDrawRequest(UDreamCanvas* InCanvas)
{
	if (InCanvas != nullptr)
	{
		RenderTargetDrawRequests.Add(InCanvas);
	}
}

void UDreamUIManagerWorldSubsystem::AddRenderPrioritySortRequest(UDreamCanvas* InCanvas)
{
	// Listed once: the canvas says whether it is (UDreamCanvas::RequestRenderPrioritySort).
	if (InCanvas != nullptr)
	{
		RenderPrioritySortRequests.Add(InCanvas);
	}
}

int32 UDreamUIManagerWorldSubsystem::CountCompetingScreenSpaceOverlayCanvases()const
{
	// Asked every frame: the root canvases set to ScreenSpaceOverlay are one of the lists sorted for the passes over them,
	// rather than a walk of every registered canvas -- a thousand world panels among them.
	const_cast<UDreamUIManagerWorldSubsystem*>(this)->SortRootCanvasesIfStale();
	TMap<int32, int32, TInlineSetAllocator<4>> CountsByLayer;
	int32 MaxCount = 0;
	for (auto& Canvas : RootCanvasesByPass[0])
	{
		if (!IsCanvasStillRegistered(Canvas))continue;
		if (!Canvas->IsRootCanvas())continue;
		if (Canvas->GetRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)continue;
		// A canvas on an inactive widget is not on screen and is not fighting anyone for it. This
		// is the ordinary state of a widget that has been created but not yet added, so counting it
		// would fire the "only one ScreenSpace UI" error on a page prefab merely being prepared.
		const UDreamWidget* CanvasWidget = Canvas->GetWidget();
		if (CanvasWidget != nullptr && !CanvasWidget->GetWidgetActiveInHierarchy())continue;
		// A shared root and one root for each player are different layers. Only roots with the same
		// authored player (INDEX_NONE for shared) compete for a projection.
		int32& LayerCount = CountsByLayer.FindOrAdd(Canvas->GetViewportPlayerIndex());
		MaxCount = FMath::Max(MaxCount, ++LayerCount);
	}
	return MaxCount;
}

void UDreamUIManagerWorldSubsystem::ParkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget) || IsWidgetParked(InWidget))
	{
		return;
	}
	FDreamParkedWidgetEntry& Entry = ParkedWidgets.AddDefaulted_GetRef();
	Entry.Widget = InWidget;
	Entry.ParkedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	InWidget->SetParked(true);
}

bool UDreamUIManagerWorldSubsystem::UnparkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	const int32 Index = ParkedWidgets.IndexOfByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	ParkedWidgets.RemoveAt(Index);
	InWidget->SetParked(false);
	return true;
}

namespace DreamParkedWidgetConsole
{
	/**
	 * The other half of "held, not lost". Holding created widgets in a named array is what stops
	 * them being collected; being able to list them is what stops that becoming a place things
	 * quietly accumulate.
	 */
	static FAutoConsoleCommandWithWorldAndArgs ListParkedWidgetsCommand(
		TEXT("dreamgui.ListPendingWidgets"),
		TEXT("List widgets created but not yet added to anything, with how long they have been waiting."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World);
				if (DreamUIManager == nullptr)
				{
					UE_LOG(DreamGUI, Log, TEXT("No DreamUI manager for this world."));
					return;
				}
				const TArray<FDreamParkedWidgetEntry>& Parked = DreamUIManager->GetParkedWidgets();
				if (Parked.IsEmpty())
				{
					UE_LOG(DreamGUI, Log, TEXT("No pending widgets."));
					return;
				}
				const double Now = World ? World->GetTimeSeconds() : 0.0;
				UE_LOG(DreamGUI, Log, TEXT("%d pending widget(s):"), Parked.Num());
				for (const FDreamParkedWidgetEntry& Entry : Parked)
				{
					UE_LOG(DreamGUI, Log, TEXT("  %s   waiting %.1fs")
						, Entry.Widget != nullptr ? *Entry.Widget->GetPathDisplayName() : TEXT("<stale>")
						, Now - Entry.ParkedAtSeconds);
				}
			}));
}

int32 UDreamUIManagerWorldSubsystem::SweepExpiredParkedWidgets()
{
	const float LifetimeSeconds = UDreamUISettings::GetParkedWidgetLifetimeSeconds();
	if (LifetimeSeconds <= 0.0f || ParkedWidgets.IsEmpty())
	{
		return 0;//off by default: a slow-but-legitimate caller must not have its widget taken away
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}
	const double Now = World->GetTimeSeconds();

	TArray<TObjectPtr<UDreamWidget>> Expired;
	for (const FDreamParkedWidgetEntry& Entry : ParkedWidgets)
	{
		if (Entry.Widget != nullptr && (Now - Entry.ParkedAtSeconds) >= (double)LifetimeSeconds)
		{
			Expired.Add(Entry.Widget);
		}
	}
	for (const TObjectPtr<UDreamWidget>& Widget : Expired)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		UE_LOG(DreamGUI, Warning, TEXT("Widget %s was created %.1fs ago and never added to anything; destroying it. Add it with AddChild or AddToViewport, or destroy it yourself. (DreamUI setting: Parked Widget Lifetime Seconds)")
			, *Widget->GetPathDisplayName(), LifetimeSeconds);
		// DestroyWidget rather than letting go: it unregisters and ends play in the right order, so
		// the widget never reaches BeginDestroy still registered, which is the state that logs an
		// error and an on-screen banner from a stack that says nothing about where it came from.
		Widget->DestroyWidget();
	}
	return Expired.Num();
}

bool UDreamUIManagerWorldSubsystem::IsWidgetParked(const UDreamWidget* InWidget)const
{
	return ParkedWidgets.ContainsByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
}

TArray<UDreamWidget*> UDreamUIManagerWorldSubsystem::GetRegisteredWidgets()const
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(RegisteredWidgets.Num());
	for (const TWeakObjectPtr<UDreamWidget>& Registered : RegisteredWidgets)
	{
		if (UDreamWidget* Widget = Registered.Get())
		{
			Widgets.Add(Widget);
		}
	}
	return Widgets;
}

bool UDreamUIManagerWorldSubsystem::IsWidgetRegistered(const UDreamWidget* InWidget)const
{
	return InWidget != nullptr && RegisteredWidgets.ContainsByPredicate(
		[InWidget](const TWeakObjectPtr<UDreamWidget>& Registered) { return Registered.Get() == InWidget; });
}

bool UDreamUIManagerWorldSubsystem::IsHeldByHost(const UDreamWidget* InRoot)const
{
	if (InRoot == nullptr)
	{
		return false;
	}
	const UObject* Owner = InRoot->GetOuter();
	if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Owner))
	{
		// Only the root a tree names is held through it; any other widget of the tree hangs off a parent,
		// or off nothing.
		if (Tree->RootWidget != InRoot)
		{
			return false;
		}
		Owner = Tree->GetOuter();
	}
	return Owner != nullptr && Owner != this && !Owner->IsA<UWorld>() && !Owner->IsA<UDreamWidget>() && !Owner->IsA<UPackage>();
}

void UDreamUIManagerWorldSubsystem::AdoptIfFreeRoot(UDreamWidget* InRoot)
{
	if (IsValid(InRoot) && InRoot->GetParent() == nullptr && InRoot->HasRegistered() && !IsHeldByHost(InRoot))
	{
		FreeRoots.AddUnique(InRoot);
	}
}

bool UDreamUIManagerWorldSubsystem::IsFreeRoot(const UDreamWidget* InWidget)const
{
	return InWidget != nullptr && FreeRoots.ContainsByPredicate([InWidget](const TObjectPtr<UDreamWidget>& Root) { return Root.Get() == InWidget; });
}

void UDreamUIManagerWorldSubsystem::ForgetFreeRoot(const UDreamWidget* InWidget)
{
	FreeRoots.RemoveSingle(const_cast<UDreamWidget*>(InWidget));
}

void UDreamUIManagerWorldSubsystem::RegisterTreeHost(UObject* InHost)
{
	if (!ensureMsgf(IsValid(InHost) && InHost->Implements<UDreamWidgetTreeHost>(), TEXT("%s: %s enrolled as a tree host without being one."),
		*GetPathName(), *GetPathNameSafe(InHost)))
	{
		return;
	}
	TreeHosts.RemoveAll([](const TWeakObjectPtr<UObject>& Host) { return !Host.IsValid(); });
	TreeHosts.AddUnique(InHost);
}

void UDreamUIManagerWorldSubsystem::UnregisterTreeHost(const UObject* InHost)
{
	TreeHosts.RemoveAll([InHost](const TWeakObjectPtr<UObject>& Host) { return !Host.IsValid() || Host.Get() == InHost; });
}

bool UDreamUIManagerWorldSubsystem::IsTreeHostRegistered(const UObject* InHost)const
{
	return InHost != nullptr && TreeHosts.ContainsByPredicate([InHost](const TWeakObjectPtr<UObject>& Host) { return Host.Get() == InHost; });
}

void UDreamUIManagerWorldSubsystem::ReleaseHostTrees(EDreamTreeReleaseReason InReason, const ULevel* InOnlyLevel)
{
	// A snapshot: letting a tree go is free to unregister the host, or to register another.
	const TArray<TWeakObjectPtr<UObject>> Hosts = TreeHosts;
	for (const TWeakObjectPtr<UObject>& WeakHost : Hosts)
	{
		UObject* HostObject = WeakHost.Get();
		IDreamWidgetTreeHost* Host = Cast<IDreamWidgetTreeHost>(HostObject);
		if (Host == nullptr)
		{
			continue;
		}
		if (InOnlyLevel != nullptr)
		{
			const AActor* HostActor = Cast<AActor>(HostObject);
			if (HostActor == nullptr)
			{
				HostActor = HostObject->GetTypedOuter<AActor>();
			}
			if (HostActor == nullptr || HostActor->GetLevel() != InOnlyLevel)
			{
				continue;
			}
		}
		Host->ReleaseTree(InReason);
	}
}

void UDreamUIManagerWorldSubsystem::HandleLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// Hiding or unloading a sublevel unregisters its actors' components and stops there: no EndPlay in
	// an editor world, no destruction. The trees of the hosts in it go now, rather than drawing on for a
	// level that is no longer in the world; showing it again registers the hosts, which build afresh.
	// A null level is the whole world going, which TeardownWorld sees to.
	if (InLevel != nullptr && InWorld != nullptr && InWorld == GetWorld())
	{
		ReleaseHostTrees(EDreamTreeReleaseReason::LevelRemoved, InLevel);
	}
}

void UDreamUIManagerWorldSubsystem::AddWidget(UDreamWidget* InWidget)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (IsWidgetRegistered(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	RegisteredWidgets.RemoveAll([](const TWeakObjectPtr<UDreamWidget>& Registered) { return !Registered.IsValid(); });
	RegisteredWidgets.AddUnique(InWidget);
	// A root no host holds has nobody else to keep it alive now that registering is not owning.
	AdoptIfFreeRoot(InWidget);
}

void UDreamUIManagerWorldSubsystem::RemoveWidget(UDreamWidget* InWidget)
{
	ParkedWidgets.RemoveAll(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == nullptr || Entry.Widget == InWidget; });
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!IsWidgetRegistered(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	RegisteredWidgets.RemoveAll(
		[InWidget](const TWeakObjectPtr<UDreamWidget>& Registered) { return !Registered.IsValid() || Registered.Get() == InWidget; });
	// Unregistered is no longer the manager's to keep, whatever becomes of it: a widget moved out of a
	// tree that came down is free to be registered where it went, and holds its own place there.
	ForgetFreeRoot(InWidget);
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(InWidget))
	{
		// A widget only reaches here from OnUnregister, which is teardown -- and a torn-down user
		// widget must stop being polled for its property bindings. The poll loop's own IsValid sweep
		// answers that only once DestroyWidget has marked the widget garbage, which is the last thing
		// it does; a widget unregistered without being destroyed is never marked at all.
		RemovePropertyBindingUser(UserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::DestroyRegisteredWidgetTrees(bool bInReportTreesOutlivingHosts)
{
	const TArray<UDreamWidget*> Registered = GetRegisteredWidgets();
	TArray<UDreamWidget*> Roots;
	for (UDreamWidget* Widget : Registered)
	{
		UDreamWidget* Root = Widget->GetRootWidgetInHierarchy();
		Roots.AddUnique(Root != nullptr ? Root : Widget);
	}

	for (UDreamWidget* Root : Roots)
	{
		if (!IsValid(Root))
		{
			continue;
		}
		// The pool's own are the manager's to take down; a tree its host still holds is a host that was
		// asked to let go and did not.
		if (bInReportTreesOutlivingHosts && !IsFreeRoot(Root) && IsHeldByHost(Root))
		{
			const UObject* Host = Root->GetOuter();
			if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Host))
			{
				Host = Tree->GetOuter();
			}
			ensureMsgf(false, TEXT("%s: the tree %s outlived its host %s, which never let it go."),
				*GetPathName(), *Root->GetPathDisplayName(), *GetPathNameSafe(Host));
		}
		Root->DestroyWidget();
	}

	// A hierarchy whose root could not be reached from its widgets -- corrupt, or half taken apart.
	for (UDreamWidget* Widget : GetRegisteredWidgets())
	{
		if (IsValid(Widget) && Widget->HasRegistered())
		{
			Widget->DestroyWidget();
		}
	}
	FreeRoots.Reset();
}

TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> UDreamUIManagerWorldSubsystem::GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist)
{
	if (auto Instance = GetInstance(InWorld))
	{
		if (!Instance->MainViewportViewExtension.IsValid())
		{
			if (InCreateIfNotExist)
			{
				Instance->MainViewportViewExtension = FSceneViewExtensions::NewExtension<FDreamUIRenderer>(InWorld, EDreamUIRendererType::ScreenSpace_and_WorldSpace);
			}
		}
		return Instance->MainViewportViewExtension;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

UDreamUIDataAsTexture* UDreamUIManagerWorldSubsystem::GetRectBlockDataRows(UDreamRectBlockData* InData, int32 InBlockSizeInBytes, EDreamUIDataAsTexturePixelFormat InPixelFormat)
{
	if (InData == nullptr)
	{
		return nullptr;
	}
	if (const TObjectPtr<UDreamUIDataAsTexture>* Found = RectBlockDataRows.Find(InData); Found != nullptr && IsValid(Found->Get()))
	{
		return Found->Get();
	}
	// The manager's, and never saved, duplicated or copied: a play session's copy of this world makes its own.
	UDreamUIDataAsTexture* Rows = NewObject<UDreamUIDataAsTexture>(this, NAME_None, DreamUI::RuntimeObjectFlags);
	Rows->Init(InBlockSizeInBytes, InPixelFormat, 32);
	RectBlockDataRows.Add(InData, Rows);
	return Rows;
}

UTexture* UDreamUIManagerWorldSubsystem::GetBuiltInRectBlockRowsTexture() const
{
	if (RectBlockDataRows.Num() == 0)
	{
		return nullptr;
	}
	UDreamRectBlockData* const Default = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockData, TEXT("DefaultRectBlockData"));
	const TObjectPtr<UDreamUIDataAsTexture>* Found = Default != nullptr ? RectBlockDataRows.Find(Default) : nullptr;
	return Found != nullptr && IsValid(Found->Get()) ? Found->Get()->GetDataTexture() : nullptr;
}

UDreamUIRenderLayerTable* UDreamUIManagerWorldSubsystem::GetRenderLayerTable()
{
	if (RenderLayerTable == nullptr)
	{
		// The manager's, and never saved, duplicated or copied, as the rect block rows above.
		RenderLayerTable = NewObject<UDreamUIRenderLayerTable>(this, NAME_None, DreamUI::RuntimeObjectFlags);
	}
	return RenderLayerTable;
}

/*
 * PAINT ROWS (DreamUIManager.h has the contract, DreamPaintRows the layout). One allocator, the data texture's, hands out
 * text tables and gradient rows alike; this keeps which row is which. A gradient row is found by its packed pixels -- the
 * hash first, then the pixels themselves -- and never written again while anyone holds it: changing a gradient is taking
 * another row. Writes wait in the data texture's batch until FlushPaintRows sends the frame's in one go.
 */
struct FDreamUIPaintRowsState
{
	/** A gradient row: the pixels it holds, their hash, and how many acquires it is answering. */
	struct FGradientRow
	{
		TArray<FVector4f> Pixels;
		uint64 Hash = 0;
		int32 Holders = 0;
	};

	/** Text tables taken, by row. */
	TSet<int32> TextRows;
	/** Gradient rows taken, by row; and the same rows by the hash of their pixels, since different pixels may share one. */
	TMap<int32, FGradientRow> GradientRows;
	TMap<uint64, TArray<int32>> GradientRowsByHash;
	/** Each said once a world: the texture could not grow any further, a text table's row is past what a record can link. */
	bool bLoggedFull = false;
	bool bLoggedLinkLimit = false;

	bool IsTaken(int32 InRow) const
	{
		return TextRows.Contains(InRow) || GradientRows.Contains(InRow);
	}
};

namespace DreamUIPaintRowsLocal
{
	/** Rows the texture starts with; it doubles in place as they run out. Sixteen kilobytes. */
	constexpr int32 InitialPaintRows = 32;

	TArray<uint8> PaintPixelBytes(TConstArrayView<FVector4f> InPixels)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(InPixels.Num() * DreamPaintRows::BytesPerPixel);
		FMemory::Memcpy(Bytes.GetData(), InPixels.GetData(), Bytes.Num());
		return Bytes;
	}

	/** What FDreamGradient::GetRowHash answers for the gradient these pixels were packed from. */
	uint64 HashPaintPixels(const TArray<FVector4f>& InPixels)
	{
		return CityHash64(reinterpret_cast<const char*>(InPixels.GetData()), static_cast<uint32>(InPixels.Num() * sizeof(FVector4f)));
	}

	bool SamePaintPixels(const TArray<FVector4f>& InA, const TArray<FVector4f>& InB)
	{
		return InA.Num() == InB.Num() && FMemory::Memcmp(InA.GetData(), InB.GetData(), InA.Num() * sizeof(FVector4f)) == 0;
	}

	/**
	 * A text table with nothing in it: every slot's layers NoRow, so a record still linking to it paints nothing; each
	 * slot's animation and transform neutral -- no phase, aspect 1, no turn or shift, scale 1 -- so a slot the text starts
	 * using reads as unanimated until the text writes its own; the header and the rest 0.
	 */
	TArray<FVector4f> MakeEmptyTextTable()
	{
		TArray<FVector4f> Pixels;
		Pixels.SetNumZeroed(DreamPaintRows::RowWidth);
		for (int32 Slot = DreamTextQuadCode::TextSlot; Slot <= DreamTextQuadCode::MaxSlot; ++Slot)
		{
			Pixels[DreamPaintRows::GetSlotRowsPixel(Slot)] = FVector4f(DreamPaintRows::NoRow, DreamPaintRows::NoRow, DreamPaintRows::NoRow, 0.0f);
			Pixels[DreamPaintRows::GetSlotAnimationPixel(Slot)] = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
			Pixels[DreamPaintRows::GetSlotTransformPixel(Slot)] = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
		}
		return Pixels;
	}

	/**
	 * A row of InRows for a new table or gradient, or INDEX_NONE when the texture is at the largest size the platform
	 * allows: the data texture then hands its last row out again, which is somebody's already.
	 */
	int32 TakePaintRow(UDreamUIDataAsTexture& InRows, FDreamUIPaintRowsState& InOutState, const UObject& InOwner)
	{
		const int32 Row = InRows.RegisterBuffer();
		if (Row < 0 || Row >= InRows.GetTextureHeight() || InOutState.IsTaken(Row))
		{
			if (!InOutState.bLoggedFull)
			{
				InOutState.bLoggedFull = true;
				UE_LOG(DreamGUI, Warning, TEXT("%s: the paint rows are full at %d rows; painted text that needs another row draws in its solid colours."),
					*InOwner.GetPathName(), InRows.GetTextureHeight());
			}
			return INDEX_NONE;
		}
		return Row;
	}
}

void UDreamUIManagerWorldSubsystem::CreatePaintRows()
{
	if (IsValid(PaintRows) || HasTornDownWorld())
	{
		return;
	}
	// The manager's, and never saved, duplicated or copied, as the rect block rows: a play session's copy of this world
	// makes its own when its manager starts.
	UDreamUIDataAsTexture* Rows = NewObject<UDreamUIDataAsTexture>(this, NAME_None, DreamUI::RuntimeObjectFlags);
	Rows->Init(DreamPaintRows::RowBytes, EDreamUIDataAsTexturePixelFormat::R32G32B32A32, DreamUIPaintRowsLocal::InitialPaintRows);
	Rows->PrepareForBatchUpdate();
	PaintRows = Rows;
	PaintRowsState = MakeShared<FDreamUIPaintRowsState>();
}

UTexture* UDreamUIManagerWorldSubsystem::GetPaintRowsTexture() const
{
	return IsValid(PaintRows) ? PaintRows->GetDataTexture() : nullptr;
}

int32 UDreamUIManagerWorldSubsystem::AcquirePaintTextRow()
{
	// After the teardown nothing is handed out: what the world's texts give back on their way out is ignored too.
	if (!IsValid(PaintRows) || !PaintRowsState.IsValid() || HasTornDownWorld())
	{
		return INDEX_NONE;
	}
	FDreamUIPaintRowsState& State = *PaintRowsState;
	const int32 Row = DreamUIPaintRowsLocal::TakePaintRow(*PaintRows, State, *this);
	if (Row == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// A record links to its table through 16 bits, as the row + 1: a row past that cannot be linked to. It goes back to
	// the allocator, where a gradient row may still use it.
	if (static_cast<uint32>(Row) + 1u > DreamPaintRows::RecordRowLinkMask)
	{
		PaintRows->UnregisterBuffer(Row);
		if (!State.bLoggedLinkLimit)
		{
			State.bLoggedLinkLimit = true;
			UE_LOG(DreamGUI, Warning, TEXT("%s: %d painted texts is as many as a world's records can link to; the next draws in its solid colours."),
				*GetPathName(), DreamPaintRows::MaxTextTableRows);
		}
		return INDEX_NONE;
	}
	State.TextRows.Add(Row);
	PaintRows->UpdateBlock(0, Row, DreamUIPaintRowsLocal::PaintPixelBytes(DreamUIPaintRowsLocal::MakeEmptyTextTable()), DreamPaintRows::RowWidth);
	return Row;
}

void UDreamUIManagerWorldSubsystem::ReleasePaintTextRow(int32 InRow)
{
	if (!IsValid(PaintRows) || !PaintRowsState.IsValid() || HasTornDownWorld() || InRow == INDEX_NONE)
	{
		return;
	}
	if (PaintRowsState->TextRows.Remove(InRow) == 0)
	{
		return;
	}
	// Emptied on the way back, so that a record still linking to it until its widget's data is next written paints
	// nothing rather than whatever the row is taken for next. A row taken again in the same frame is written after this.
	PaintRows->UpdateBlock(0, InRow, DreamUIPaintRowsLocal::PaintPixelBytes(DreamUIPaintRowsLocal::MakeEmptyTextTable()), DreamPaintRows::RowWidth);
	PaintRows->UnregisterBuffer(InRow);
}

int32 UDreamUIManagerWorldSubsystem::AcquirePaintGradientRow(const FDreamGradient& InGradient)
{
	if (!IsValid(PaintRows) || !PaintRowsState.IsValid() || HasTornDownWorld())
	{
		return INDEX_NONE;
	}
	using namespace DreamUIPaintRowsLocal;
	FDreamUIPaintRowsState& State = *PaintRowsState;
	TArray<FVector4f> Pixels;
	InGradient.PackRow(Pixels);
	const uint64 Hash = HashPaintPixels(Pixels);
	if (const TArray<int32>* SameHash = State.GradientRowsByHash.Find(Hash))
	{
		for (const int32 Candidate : *SameHash)
		{
			FDreamUIPaintRowsState::FGradientRow& Shared = State.GradientRows.FindChecked(Candidate);
			if (SamePaintPixels(Shared.Pixels, Pixels))
			{
				++Shared.Holders;
				return Candidate;
			}
		}
	}
	const int32 Row = TakePaintRow(*PaintRows, State, *this);
	if (Row == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	PaintRows->UpdateBlock(0, Row, PaintPixelBytes(Pixels), DreamPaintRows::RowWidth);
	FDreamUIPaintRowsState::FGradientRow& Taken = State.GradientRows.Add(Row);
	Taken.Pixels = MoveTemp(Pixels);
	Taken.Hash = Hash;
	Taken.Holders = 1;
	State.GradientRowsByHash.FindOrAdd(Hash).Add(Row);
	return Row;
}

void UDreamUIManagerWorldSubsystem::ReleasePaintGradientRow(int32 InRow)
{
	if (!IsValid(PaintRows) || !PaintRowsState.IsValid() || HasTornDownWorld() || InRow == INDEX_NONE)
	{
		return;
	}
	FDreamUIPaintRowsState& State = *PaintRowsState;
	FDreamUIPaintRowsState::FGradientRow* Held = State.GradientRows.Find(InRow);
	if (Held == nullptr)
	{
		return;
	}
	if (--Held->Holders > 0)
	{
		return;
	}
	// The last holder: the row goes back. Its pixels stay until it is taken again -- nothing reads a row nobody holds.
	const uint64 Hash = Held->Hash;
	if (TArray<int32>* SameHash = State.GradientRowsByHash.Find(Hash))
	{
		SameHash->RemoveSingleSwap(InRow);
		if (SameHash->IsEmpty())
		{
			State.GradientRowsByHash.Remove(Hash);
		}
	}
	State.GradientRows.Remove(InRow);
	PaintRows->UnregisterBuffer(InRow);
}

void UDreamUIManagerWorldSubsystem::WritePaintRowPixels(int32 InRow, int32 InFirstPixel, TConstArrayView<FVector4f> InPixels)
{
	if (!IsValid(PaintRows) || !PaintRowsState.IsValid() || HasTornDownWorld())
	{
		return;
	}
	// Into a text table only, and inside its row: a gradient row is written by acquiring it, a row given back by nobody.
	if (!PaintRowsState->TextRows.Contains(InRow) || InFirstPixel < 0 || InFirstPixel >= DreamPaintRows::RowWidth || InPixels.Num() <= 0)
	{
		return;
	}
	const int32 Count = FMath::Min(InPixels.Num(), DreamPaintRows::RowWidth - InFirstPixel);
	PaintRows->UpdateBlock(InFirstPixel, InRow, DreamUIPaintRowsLocal::PaintPixelBytes(InPixels.Left(Count)), Count);
}

void UDreamUIManagerWorldSubsystem::FlushPaintRows()
{
	if (!IsValid(PaintRows))
	{
		return;
	}
	if (PaintRows->GetIsBatchUpdateMode())
	{
		PaintRows->Flush();
	}
	PaintRows->PrepareForBatchUpdate();
}

void UDreamUIManagerWorldSubsystem::GetPaintRowsMemoryInfo(int32& OutTextureRows, int32& OutTextRows, int32& OutGradientRows, int64& OutTextureBytes) const
{
	OutTextureRows = 0;
	OutTextRows = 0;
	OutGradientRows = 0;
	OutTextureBytes = 0;
	if (!IsValid(PaintRows))
	{
		return;
	}
	OutTextureRows = PaintRows->GetTextureHeight();
	OutTextureBytes = static_cast<int64>(OutTextureRows) * DreamPaintRows::RowBytes;
	if (PaintRowsState.IsValid())
	{
		OutTextRows = PaintRowsState->TextRows.Num();
		OutGradientRows = PaintRowsState->GradientRows.Num();
	}
}
