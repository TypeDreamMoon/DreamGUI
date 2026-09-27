// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0
#if WITH_EDITOR

void UDreamUIManagerWorldSubsystem::OnEnginePreExit()
{
	// for (TObjectIterator<UDreamUIPrefab> Itr; Itr; ++Itr)
	// {
	// 	auto Prefab = *Itr;
	// 	Prefab->ClearDesignerScene();
	// }
}
#endif

bool UDreamUIManagerWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_EDITOR
	InstanceArray.Add(this);
	if (this->GetWorld()->WorldType == EWorldType::EditorPreview//EditorPreview world don't tick, so manually tick it
		|| this->GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorTickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("DreamUIManagerWorldSubsystemEditorTick"), 0, [WeakThis = MakeWeakObjectPtr(this)](float DeltaTime) {
			if (WeakThis.IsValid())
			{
				WeakThis->Tick(DeltaTime);
				return true;
			}
			return false;
			});
	}
	if (this->GetWorld()->IsGameWorld() || this->GetWorld()->WorldType == EWorldType::Editor)//game world or editor world, skip editor preview world
	{
		bShouldTickInEditor = true;
	}
	else
	{
		bShouldTickInEditor = false;
	}
	FCoreDelegates::OnEndFrame.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEndOfFrame);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEnginePreExit);
	UDreamUIManagerObject::GetInstance(true);//make sure it is created
#endif
	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDreamUIManagerWorldSubsystem::OnCultureChanged);
}
void UDreamUIManagerWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates);
}
void UDreamUIManagerWorldSubsystem::Deinitialize()
{
#if WITH_EDITOR
	InstanceArray.Remove(this);
	if (EditorTickDelegateHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickDelegateHandle);
		EditorTickDelegateHandle.Reset();
	}
	// Both of these are subscribed in Initialize, so they have to come off here: a subsystem is
	// created and destroyed on every PIE start/stop, and AddUObject being a weak binding only means
	// the stale entries do not crash -- they still accumulate in two global multicast arrays for the
	// length of an editor session, and this object still receives one more OnEndOfFrame between
	// Deinitialize and its own collection.
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
	OnDeinitialize.Broadcast();
#endif
	// The interaction objects this subsystem spawned are its to take away again. They are transient,
	// so a level change would not carry them anyway; destroying them here is what keeps a PIE session
	// that starts and stops repeatedly from leaving a host actor behind on every run.
	for (TPair<int32, TObjectPtr<AActor>>& HostPair : InteractionHosts)
	{
		if (IsValid(HostPair.Value))
		{
			HostPair.Value->Destroy();
		}
	}
	InteractionHosts.Reset();
	if (IsValid(CreatedEventSystemActor))
	{
		CreatedEventSystemActor->Destroy();
		CreatedEventSystemActor = nullptr;
	}
	DestroyRegisteredWidgetTrees();
	if (MainViewportViewExtension.IsValid())
	{
		MainViewportViewExtension.Reset();
	}
	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
	Super::Deinitialize();
}

void UDreamUIManagerWorldSubsystem::BeginDestroy()
{
	check(!IsInitialized());
	DestroyRegisteredWidgetTrees();
	Super::BeginDestroy();
}

TStatId UDreamUIManagerWorldSubsystem::GetStatId() const
{
	//return GetStatID();
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamGUIManagerWorldSubsystem, STATGROUP_Tickables);
}
bool UDreamUIManagerWorldSubsystem::IsTickableWhenPaused() const
{
	return true;
}

void UDreamUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

UDreamUIManagerWorldSubsystem* UDreamUIManagerWorldSubsystem::GetInstance(UWorld* InWorld)
{
	return IsValid(InWorld) ? InWorld->GetSubsystem<UDreamUIManagerWorldSubsystem>() : nullptr;
}

#if WITH_EDITOR
UDreamUISelection* UDreamUIManagerWorldSubsystem::GetSelection() const
{
	if (!IsValid(Selection))
	{
		Selection = NewObject<UDreamUISelection>();
		Selection->SetFlags(RF_Transactional);
	}
	return Selection;
}

void UDreamUIManagerWorldSubsystem::MarkDreamUIWidgetOutlinerChanged()
{
	bDreamUIWidgetOutlinerChanged = true;
}
#endif

void UDreamUIManagerWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	//normally BeginPlay is already called when LoadPrefab, but if World has not BeginPlay then this will work
	//
	// Snapshot first, and re-check each entry, the way UDreamWidget::BeginPlay/EndPlay/OnRegister do:
	// an Awake is free to create or destroy widgets, and RemoveWidget uses RemoveSingle, which shifts
	// everything after it down -- walking the live array by index skipped the widget that moved into
	// the slot just visited. A pending-kill widget can also still be in the array, so IsValid is not
	// optional here either. HasRegistered answers the other half: a widget in the snapshot that has
	// since been unregistered was torn down while this loop was running, and BeginPlay on it would
	// restart a widget that is on its way out.
	const TArray<TObjectPtr<UDreamWidget>> WidgetsToBeginPlay = AllWidgetArray;
	for (UDreamWidget* Widget : WidgetsToBeginPlay)
	{
		if (IsValid(Widget) && Widget->HasRegistered() && !Widget->HasBegunPlay())
		{
			Widget->BeginPlay();
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
#if WITH_EDITOR
	OnEndPlay.Broadcast();
	if (this->GetWorld()->IsGameWorld())//game mode should deinit when EndPlay
#endif
	{
		DestroyRegisteredWidgetTrees();
	}
	Super::OnWorldEndPlay(InWorld);
}

#if WITH_EDITOR
TArray<UDreamUIManagerWorldSubsystem*> UDreamUIManagerWorldSubsystem::InstanceArray;
#endif

DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Tick"), STAT_DreamUIBehaviourTick, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Start"), STAT_DreamUIBehaviourStart, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("UpdateLayout"), STAT_UpdateLayout, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("PropertyBindings Poll"), STAT_DreamUIPropertyBindingsPoll, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("RefreshAllClipData"), STAT_DreamUIRefreshClipData, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("UpdateRootCanvas"), STAT_DreamUIUpdateRootCanvas, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("RenderPrioritySort"), STAT_DreamUIRenderPrioritySort, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("SubmitCanvasDrawCall"), STAT_DreamUISubmitCanvasDrawCall, STATGROUP_DreamGUI);

void UDreamUIManagerWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if WITH_EDITOR
	if (bShouldTickInEditor)
#endif
	{
		this->TickDreamUI(DeltaTime);
	}
}

void UDreamUIManagerWorldSubsystem::AddPropertyBindingUser(UDreamUserWidget* InUserWidget)
{
	if (IsValid(InUserWidget))
	{
		PropertyBindingUsers.AddUnique(InUserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::RemovePropertyBindingUser(UDreamUserWidget* InUserWidget)
{
	PropertyBindingUsers.RemoveSingleSwap(InUserWidget);
}

void UDreamUIManagerWorldSubsystem::TickDreamUI(float DeltaTime)
{
	SweepExpiredParkedWidgets();
	//Update culture
	{
		if (bShouldUpdateOnCultureChanged)
		{
			bShouldUpdateOnCultureChanged = false;
			// Sweep first, then walk a snapshot. The entries are weak and a registrant can have been
			// destroyed since it registered -- the generated Execute_OnCultureChanged thunk checks its
			// target against null -- and a handler is free to register or unregister listeners as it runs.
			AllCultureChangedArray.RemoveAll([](const TWeakObjectPtr<UObject>& Item) { return !Item.IsValid(); });
			const TArray<TWeakObjectPtr<UObject>> CultureChangedListeners = AllCultureChangedArray;
			for (auto& Culture : CultureChangedListeners)
			{
				if (UObject* CultureObject = Culture.Get(); IsValid(CultureObject))
				{
					IDreamUICultureChangedInterface::Execute_OnCultureChanged(CultureObject);
				}
			}
		}
	}

	// Property bindings, BEFORE the behaviours: a behaviour that reads a bound property this frame
	// should see this frame's value, not the one from before the function was called.
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIPropertyBindingsPoll);
		for (int32 Index = PropertyBindingUsers.Num() - 1; Index >= 0; --Index)
		{
			UDreamUserWidget* UserWidget = PropertyBindingUsers[Index].Get();
			if (!IsValid(UserWidget))
			{
				PropertyBindingUsers.RemoveAtSwap(Index);
				continue;
			}
			// Only the polled remainder: subscribed bindings re-evaluate from their field's
			// broadcast, and visiting them here would just do the work twice.
			UserWidget->EvaluatePolledPropertyBindings();
		}
	}

	//DreamUIBehaviour start
	{
		if (DreamUIBehavioursForStart.Num() > 0)
		{
			bIsExecutingStart = true;
			SCOPE_CYCLE_COUNTER(STAT_DreamUIBehaviourStart);
			for (int i = 0; i < DreamUIBehavioursForStart.Num(); i++)
			{
				auto item = DreamUIBehavioursForStart[i];
				if (item.IsValid())
				{
					item->Call_Start();
					// Re-checked after Start, because Start is allowed to switch its own widget off or
					// destroy it. bIsStartCalled is set BEFORE Start() runs, so the Call_OnDisable that
					// follows takes the "already started" branch and calls RemoveDreamUIBehavioursFromTick
					// on a list this behaviour is not in yet (it logs "Not exist" and does nothing).
					// Adding it unconditionally here then ticked a disabled behaviour every frame, and
					// the next enable reported "Already exist".
					if (item.IsValid() && item->bIsEnableCalled && item->bCanExecuteTick)
					{
						DreamUIBehavioursForTick.AddUnique(item);
					}
				}
			}
			DreamUIBehavioursForStart.Reset();
			bIsExecutingStart = false;
		}
	}

	//DreamUIBehaviour tick
	{
		bIsExecutingTick = true;
		auto bIsGamePaused = GetWorld()->IsPaused();
		auto Settings = GetDefault<UDreamUISettings>();
		SCOPE_CYCLE_COUNTER(STAT_DreamUIBehaviourTick);
		for (int i = 0; i < DreamUIBehavioursForTick.Num(); i++)
		{
			CurrentExecutingTickIndex = i;
			UDreamUIBehaviour* Behaviour = DreamUIBehavioursForTick[i].Get();
			if (!IsValid(Behaviour))
			{
				// Destroyed since the list was built. Not removed here: the index is what
				// RemoveDreamUIBehavioursFromTick compares against CurrentExecutingTickIndex to decide
				// whether a removal is safe, so renumbering mid-walk would make it drop the wrong entry.
				// The sweep below the loop drops it instead.
				continue;
			}
			if (auto Widget = Behaviour->GetWidget())
			{
				bool bAffectByGamePause;
				if (Widget->IsScreenSpaceOverlayUI())
				{
					bAffectByGamePause = Settings->bScreenSpaceUIAffectByGamePause;
				}
				else
				{
					bAffectByGamePause = Settings->bWorldSpaceUIAffectByGamePause;
				}
				if (!bIsGamePaused || (bIsGamePaused && !bAffectByGamePause))
				{
					Behaviour->Tick(DeltaTime);
				}
			}
			else
			{
				if (!bIsGamePaused || (bIsGamePaused && Behaviour->bTickEvenWhenPaused))
				{
					Behaviour->Tick(DeltaTime);
				}
			}
		}
		bIsExecutingTick = false;
		CurrentExecutingTickIndex = -1;
		//remove these padding things
		if (DreamUIBehavioursNeedToRemoveFromTick.Num() > 0)
		{
			for (auto& item : DreamUIBehavioursNeedToRemoveFromTick)
			{
				DreamUIBehavioursForTick.Remove(item);
			}
			DreamUIBehavioursNeedToRemoveFromTick.Reset();
		}
		//and the entries whose behaviour was destroyed while the list was being walked
		DreamUIBehavioursForTick.RemoveAll([](const TWeakObjectPtr<UDreamUIBehaviour>& Item) { return !Item.IsValid(); });
	}

	//update layout
	if (LayoutDirtyWidgetArray.Num() > 0)
	{
		bIsExecutingLayout = true;
		constexpr int32 MaxLayoutPassesPerFrame = 32;
		int32 LayoutPassCount = 0;
		LastLayoutPassCount = 0;
#if WITH_EDITOR && ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME
		auto Time = FDateTime::Now();
		UE_LOG(DreamGUI, Log, TEXT("---Begin layout frame:%d, World:%s---"), GFrameNumber, *GetWorld()->GetPathName());
#endif
		LayoutContainerArrayWhichHasSnapshot.Reset();
		while (LayoutDirtyWidgetArray.Num() > 0 && LayoutPassCount < MaxLayoutPassesPerFrame)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLayout);
			++LayoutPassCount;
			LastLayoutPassCount = LayoutPassCount;

			TArray<TWeakObjectPtr<UDreamWidget>> CopiedLayoutDirtyWidgetArray;
			Swap(CopiedLayoutDirtyWidgetArray, LayoutDirtyWidgetArray);

			// Collect the live roots up front so ancestry can be tested against the whole batch.
			TSet<UDreamWidget*> BatchRoots;
			TArray<UDreamWidget*> OrderedRoots;
			BatchRoots.Reserve(CopiedLayoutDirtyWidgetArray.Num());
			OrderedRoots.Reserve(CopiedLayoutDirtyWidgetArray.Num());
			for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : CopiedLayoutDirtyWidgetArray)
			{
				if (UDreamWidget* Widget = WeakWidget.Get(); IsValid(Widget))
				{
					bool bAlreadyPresent = false;
					BatchRoots.Add(Widget, &bAlreadyPresent);
					if (!bAlreadyPresent)
					{
						OrderedRoots.Add(Widget);
					}
				}
			}

			// CalculateLayoutTree walks an entire subtree, so a root sitting under another root in the same
			// batch is redundant - the ancestor's walk already covers it. It was worse than redundant: this
			// batch used to be iterated back-to-front, so the descendant usually ran FIRST, laying its
			// subtree out against the ancestor's stale size and then being laid out a second time when the
			// ancestor's walk reached it. Both roots are easy to enqueue at once, because
			// UDreamWidget::MarkLayoutForRebuild falls back to the widget itself when no layout exists yet on
			// its ancestor chain - sizing a widget before parenting it is enough.
			// The survivors are pairwise unrelated, so their relative order no longer matters; keep enqueue
			// order for determinism.
			constexpr int32 MaxHierarchyDepthGuard = 1024;
			for (UDreamWidget* Widget : OrderedRoots)
			{
				bool bCoveredByAncestor = false;
				int32 DepthGuard = 0;
				for (UDreamWidget* Ancestor = Widget->GetParent();
					IsValid(Ancestor) && DepthGuard < MaxHierarchyDepthGuard;
					Ancestor = Ancestor->GetParent(), ++DepthGuard)
				{
					if (BatchRoots.Contains(Ancestor))
					{
						bCoveredByAncestor = true;
						break;
					}
				}
				if (!bCoveredByAncestor)
				{
					CalculateLayoutTree(Widget);
				}
			}
		}
		if (LayoutDirtyWidgetArray.Num() > 0)
		{
			UE_LOG(DreamGUI, Error,
				TEXT("Layout did not converge after %d passes in World %s. Deferring %d pending widgets to the next frame."),
				MaxLayoutPassesPerFrame, *GetNameSafe(GetWorld()), LayoutDirtyWidgetArray.Num());
		}
		for (auto& SnapshotLayout : LayoutContainerArrayWhichHasSnapshot)
		{
			if (UDreamLayoutContainer* Layout = SnapshotLayout.Get(); IsValid(Layout))
			{
				Layout->ApplyLayoutResult();
			}
		}
#if WITH_EDITOR && ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME
		for (auto& CalcCountKeyValue : LayoutCalculationCounterMap)
		{
			if (CalcCountKeyValue.Value >= 2)
			{
				UE_LOG(DreamGUI, Warning, TEXT("Widget %s has been calculated layout %d times in a frame"), *CalcCountKeyValue.Key, CalcCountKeyValue.Value);
			}
		}
		LayoutCalculationCounterMap.Reset();
		auto TimeSpan = (FDateTime::Now() - Time).GetTotalMilliseconds();
		UE_LOG(DreamGUI, Log, TEXT("---end layout frame:%d, count:%d, time:%f"), GFrameNumber, LayoutPassCount, TimeSpan);
#endif
		bIsExecutingLayout = false;
		// Anything that restructured the tree while the pass was running asked for a rebuild and was told
		// to wait; this is the wait ending. See MarkRebuildLayoutTree.
		FlushPendingLayoutTreeRebuild();
	}

	// One ScreenSpaceOverlay root canvas PER LOCAL PLAYER, not one per world.
	//
	// It used to be one per world, full stop, and that is what made split screen impossible: the
	// second player's screen is a second overlay canvas by definition. What is still wrong -- and
	// still shows up in a packaged build as one of the two UIs randomly not being there -- is having
	// MORE overlay canvases than there are local players to own them, because past that point two of
	// them are competing for the same screen with an undefined order between them.
	//
	// Not editor-only, for the same reason it was made not-editor-only before: the rule is a runtime
	// one. Shipping is the only build that stays silent.
#if !UE_BUILD_SHIPPING
	const int32 ScreenSpaceOverlayCanvasCount = CountCompetingScreenSpaceOverlayCanvases();
	const UGameInstance* GameInstanceForScreens = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const int32 AllowedOverlayCanvasCount = FMath::Max(1,
		GameInstanceForScreens != nullptr ? GameInstanceForScreens->GetNumLocalPlayers() : 1);
	if (ScreenSpaceOverlayCanvasCount > AllowedOverlayCanvasCount)
	{
		if (PrevScreenSpaceOverlayCanvasCount != ScreenSpaceOverlayCanvasCount)//only show message when change
		{
			PrevScreenSpaceOverlayCanvasCount = ScreenSpaceOverlayCanvasCount;
			auto errMsg = FText::Format(LOCTEXT("MultipleDreamUICanvasRenderScreenSpaceOverlay", "[{0}].{1} Detect {2} DreamCanvas rendered with ScreenSpaceOverlay mode for {3} local player(s). There may be at most one ScreenSpace UI per local player; the extra ones compete for the same screen.\
\n	World: {4}, type: {5}")
			, FText::FromString(ANSI_TO_TCHAR(__FUNCTION__)), __LINE__, ScreenSpaceOverlayCanvasCount, AllowedOverlayCanvasCount
			, FText::FromString(this->GetWorld()->GetPathName()), (int)(this->GetWorld()->WorldType));
			UE_LOG(DreamGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
			FDreamUIUtils::EditorNotification(errMsg, false, 10.0f);
#endif
		}
	}
	else
	{
		PrevScreenSpaceOverlayCanvasCount = 0;
	}
#endif
#if WITH_EDITOR
	if (bDreamUIWidgetOutlinerChanged)
	{
		bDreamUIWidgetOutlinerChanged = false;
		OnDreamUIWidgetOutlinerChanged.Broadcast();
	}
#endif

	// Refresh clip rectangles after layout, before draw-calls.
	//
	// This is the equivalent of UGUI's ClipperRegistry.Cull(): a clip rectangle is derived from widget world
	// transforms, which the layout pass above has just changed, so it is recomputed here every tick instead of
	// being driven by dirty flags. Flag-driven invalidation was the wrong shape for this — a clip depends on the
	// transform of every ancestor, and whatever moves an ancestor has no idea a descendant owns a clip, so every
	// missed mark left the shader clipping against a stale rectangle and silently culled a whole subtree.
	// FDreamUIClipData::UpdateData diffs against the last uploaded block, so an unchanged clip costs one matrix
	// build and a memcmp, with no GPU write.
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIRefreshClipData);
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : SnapshotCanvases())
		{
			if (IsCanvasStillRegistered(Canvas))
			{
				Canvas->RefreshAllClipData();
			}
		}
	}

	//update draw-call
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIUpdateRootCanvas);
		auto UpdateCanvas = [this](EDreamRenderMode RenderMode) {
			// A snapshot per pass: UpdateRootCanvas may make a render target and broadcast it, and a
			// listener may register or unregister a canvas.
			for (const TWeakObjectPtr<UDreamCanvas>& Canvas : SnapshotCanvases())
			{
				if (!IsCanvasStillRegistered(Canvas))continue;
				if (!Canvas->IsRootCanvas())continue;
				if (Canvas->GetActualRenderMode() != RenderMode)continue;
				Canvas->UpdateRootCanvas();
			}
		};
		UpdateCanvas(EDreamRenderMode::ScreenSpaceOverlay);
		UpdateCanvas(EDreamRenderMode::WorldSpace);
		UpdateCanvas(EDreamRenderMode::WorldSpace_DreamUI);
		UpdateCanvas(EDreamRenderMode::RenderTarget);
	}
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();

	// Consume render-priority sort requests at their owner. A request raised outside the owner's own
	// draw-call rebuild (runtime SetSortOrder, a child canvas rebuilding alone) used to sit in the flag
	// until the owner happened to rebuild for some other reason; this sweep executes it the same frame.
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIRenderPrioritySort);
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : SnapshotCanvases())
		{
			if (IsCanvasStillRegistered(Canvas))
			{
				Canvas->ConsumePendingRenderPrioritySort();
			}
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates(UWorld* InWorld)
{
	if (InWorld == this->GetWorld())
	{
#if WITH_EDITOR
		this->DrawHelperGizmo();
#endif
		this->SubmitCanvasDrawCall();
	}
}


void UDreamUIManagerWorldSubsystem::SubmitCanvasDrawCall()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUISubmitCanvasDrawCall);
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();
	//update draw-call
	{
		auto UpdateCanvas = [this](EDreamRenderMode RenderMode) {
			for (const TWeakObjectPtr<UDreamCanvas>& Canvas : SnapshotCanvases())
			{
				if (!IsCanvasStillRegistered(Canvas))continue;
				if (!Canvas->IsRootCanvas())continue;
				if (Canvas->GetRenderMode() != RenderMode)continue;
				Canvas->UpdateDrawCallBatchData();
			}
		};
		UpdateCanvas(EDreamRenderMode::ScreenSpaceOverlay);
		UpdateCanvas(EDreamRenderMode::WorldSpace);
		UpdateCanvas(EDreamRenderMode::WorldSpace_DreamUI);
		UpdateCanvas(EDreamRenderMode::RenderTarget);
	}
}

void UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForTick(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->DreamUIBehavioursForTick.Find(InComp, index))
			{
				Instance->DreamUIBehavioursForTick.Add(InComp);
				return;
			}
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Already exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
		}
	}
}
void UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromTick(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& TickArray = Instance->DreamUIBehavioursForTick;
			int32 Index = INDEX_NONE;
			if (TickArray.Find(InComp, Index))
			{
				if (Instance->bIsExecutingTick)
				{
					if (Index > Instance->CurrentExecutingTickIndex)//not execute it yet, safe to remove
					{
						TickArray.RemoveAt(Index);
					}
					else//already execute or current execute it, not safe to remove. should remove it after execute process complete
					{
						Instance->DreamUIBehavioursNeedToRemoveFromTick.Add(InComp);
					}
				}
				else//not executing tick, safe to remove
				{
					TickArray.RemoveAt(Index);
				}
			}
			else
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Not exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
			}

			//cleanup array
			int InvalidCount = 0;
			for (int i = TickArray.Num() - 1; i >= 0; i--)
			{
				if (!TickArray[i].IsValid())
				{
					TickArray.RemoveAt(i);
					InvalidCount++;
				}
			}
			if (InvalidCount > 0)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Cleanup %d invalid DreamUIBehaviour"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InvalidCount);
			}
		}
	}
}
void UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForStart(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->DreamUIBehavioursForStart.Find(InComp, index))
			{
				Instance->DreamUIBehavioursForStart.Add(InComp);
				return;
			}
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Already exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
		}
	}
}
void UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromStart(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& startArray = Instance->DreamUIBehavioursForStart;
			int32 index = INDEX_NONE;
			if (startArray.Find(InComp, index))
			{
				if (Instance->bIsExecutingStart)
				{
					if (!InComp->bIsStartCalled)//if already called start then nothing to do, because start array will be cleared after execute start
					{
						startArray.RemoveAt(index);//not execute start yet, safe to remove
					}
				}
				else
				{
					startArray.RemoveAt(index);//not executing start, safe to remove
				}
			}
			else
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Not exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
			}

			//cleanup array
			int inValidCount = 0;
			for (int i = startArray.Num() - 1; i >= 0; i--)
			{
				if (!startArray[i].IsValid())
				{
					startArray.RemoveAt(i);
					inValidCount++;
				}
			}
			if (inValidCount > 0)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Cleanup %d invalid DreamUIBehaviour"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, inValidCount);
			}
		}
	}
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

#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
{
	for (auto InstanceItem : InstanceArray)
	{
		if (InstanceItem != nullptr)
		{
			if (InWorld != nullptr && InstanceItem->GetWorld() != InWorld)
			{
				continue;
			}
		}
		auto Instance = InstanceItem;
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Instance->SnapshotCanvases())
		{
			if (!Instance->IsCanvasStillRegistered(Canvas))continue;
			if (!Canvas->IsRootCanvas())continue;
			if (auto Widget = Canvas->GetWidget())
			{
				Widget->EnsureDataForRebuild();
				Widget->MarkCanvasUpdate(true);
			}
		}
	}
}

#endif

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
}

int32 UDreamUIManagerWorldSubsystem::CountCompetingScreenSpaceOverlayCanvases()const
{
	int32 Count = 0;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (!Canvas->IsRootCanvas())continue;
		if (Canvas->GetRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)continue;
		// A canvas on an inactive widget is not on screen and is not fighting anyone for it. This
		// is the ordinary state of a widget that has been created but not yet added, so counting it
		// would fire the "only one ScreenSpace UI" error on a page prefab merely being prepared.
		const UDreamWidget* CanvasWidget = Canvas->GetWidget();
		if (CanvasWidget != nullptr && !CanvasWidget->GetWidgetActiveInHierarchy())continue;
		Count++;
	}
	return Count;
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

void UDreamUIManagerWorldSubsystem::AddWidget(UDreamWidget* InWidget)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.AddUnique(InWidget);
}

void UDreamUIManagerWorldSubsystem::RemoveWidget(UDreamWidget* InWidget)
{
	ParkedWidgets.RemoveAll(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == nullptr || Entry.Widget == InWidget; });
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.RemoveSingle(InWidget);
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(InWidget))
	{
		// A widget only reaches here from OnUnregister, which is teardown -- and a torn-down user
		// widget must stop being polled for its property bindings. IsValid() is no answer to that
		// question: DestroyWidget unregisters, ends play and detaches without ever marking the
		// object garbage, so the poll loop's own sweep went on calling binding source functions on
		// a widget that had already run EndPlay, until the next full GC.
		RemovePropertyBindingUser(UserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::DestroyRegisteredWidgetTrees()
{
	if (AllWidgetArray.IsEmpty())
	{
		return;
	}

	const TArray<TObjectPtr<UDreamWidget>> RegisteredWidgets = AllWidgetArray;
	TSet<UDreamWidget*> Roots;
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		UDreamWidget* Root = Widget->GetRootWidgetInHierarchyEvenIfUnreachable();
		Roots.Add(Root ? Root : Widget);
	}

	for (UDreamWidget* Root : Roots)
	{
		if (Root != nullptr && !Root->HasAnyFlags(RF_FinishDestroyed))
		{
			Root->DestroyWidget();
		}
	}

	// Corrupt or partially collected hierarchies may not have a usable cached root.
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget != nullptr && !Widget->HasAnyFlags(RF_FinishDestroyed)
			&& (Widget->HasRegistered() || Widget->HasBegunPlay()))
		{
			Widget->DestroyWidget();
		}
	}
}

void UDreamUIManagerWorldSubsystem::AddLayoutDirtyWidget(UDreamWidget* InWidget)
{
	if (IsValid(InWidget))
	{
		LayoutDirtyWidgetArray.AddUnique(InWidget);
	}
}

/**
 * Both entry points defer rather than drop while a pass is running.
 *
 * The cached tree cannot be rebuilt mid-pass -- CalculateLayoutTree is iterating a copy of it, and
 * emptying the map underneath would strand the walk. But a structural change made from inside a pass is
 * real: a behaviour that adds a child from OnDimensionChanged, the sibling renumbering panels do while
 * arranging, a subtree revealed by SetLayoutVisibilitySuppressed. The old shape simply did nothing, and
 * because CalculateLayoutTree only re-collects when the cached array is EMPTY, the stale tree then
 * survived indefinitely -- until some later attach or detach outside a pass happened to wipe it. The
 * new widget still laid itself out (it enqueues itself as its own dirty root), but its ancestors' cached
 * pre-order no longer contained it, so the ancestor walk skipped it.
 *
 * Remembering a single "rebuild everything" bit rather than the specific widgets is deliberate: the
 * targeted form only ever removes one entry, so upgrading it to the full wipe is conservative, and it
 * only costs anything on the frames where something really did restructure mid-pass.
 */
void UDreamUIManagerWorldSubsystem::MarkRebuildLayoutTree(UDreamWidget* InWidget)
{
	if (bIsExecutingLayout)
	{
		bPendingLayoutTreeRebuild = true;
		return;
	}
	MapWidgetToLayoutTree.Remove(InWidget);
}

void UDreamUIManagerWorldSubsystem::MarkRebuildAllLayoutTree()
{
	if (bIsExecutingLayout)
	{
		bPendingLayoutTreeRebuild = true;
		return;
	}
	MapWidgetToLayoutTree.Empty();
}

void UDreamUIManagerWorldSubsystem::FlushPendingLayoutTreeRebuild()
{
	if (bPendingLayoutTreeRebuild)
	{
		bPendingLayoutTreeRebuild = false;
		MapWidgetToLayoutTree.Empty();
	}
}

void UDreamUIManagerWorldSubsystem::CalculateLayoutTree(UDreamWidget* RootLayoutWidget)
{
	if (!IsValid(RootLayoutWidget))
	{
		return;
	}

	struct LOCAL
	{
		static void CollectLayoutTree(UDreamWidget* Widget, TArray<TWeakObjectPtr<UDreamWidget>>& LayoutTreeArray,
			TSet<const UDreamWidget*>& VisitedWidgets)
		{
			if (!IsValid(Widget))return;
			if (VisitedWidgets.Contains(Widget))return;
			VisitedWidgets.Add(Widget);
			//Collect the full subtree, including layout-invisible and not-yet-registered widgets. Both flags flip
			//without a usable chance to invalidate this cache: a collapsed subtree that becomes visible from
			//inside a layout pass hits the bIsExecutingLayout guard in MarkRebuildAllLayoutTree, and OnRegister
			//never invalidates the cache at all. Pruning here would bake such a subtree out of the cached tree
			//permanently, so it would only lay out again after something re-dirties the whole tree top-down
			//(a viewport resize). Filter per-widget at update time instead.
			LayoutTreeArray.Add(Widget);
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				CollectLayoutTree(Child, LayoutTreeArray, VisitedWidgets);
			}
		}
	};
	auto& LayoutTree = MapWidgetToLayoutTree.FindOrAdd(RootLayoutWidget);
	if (LayoutTree.WidgetArray.IsEmpty())
	{
		TSet<const UDreamWidget*> VisitedWidgets;
		LOCAL::CollectLayoutTree(RootLayoutWidget, LayoutTree.WidgetArray, VisitedWidgets);
	}
	//Iterate a copy: UpdateLayout can re-enter CalculateLayoutTree through RebuildLayoutImmediately, and the
	//FindOrAdd there may rehash the map out from under a reference into it.
	const TArray<TWeakObjectPtr<UDreamWidget>> LayoutTreeArray = LayoutTree.WidgetArray;
	for (int i = 0; i < LayoutTreeArray.Num(); i++)
	{
		UDreamWidget* Widget = LayoutTreeArray[i].Get();
		if (!IsValid(Widget))
		{
			continue;
		}
		if (!Widget->GetLayoutVisibleInHierarchy())
		{
			continue;//collapsed for layout, but stays in the tree so it lays out as soon as it becomes visible
		}
		if (!Widget->HasRegistered())
		{
			continue;//if not registered, means it could about to remove
		}
		if (auto LayoutContainer = Widget->GetLayoutContainer())
		{
			if (!LayoutContainerArrayWhichHasSnapshot.Contains(LayoutContainer))
			{
				LayoutContainer->SnapshotLayout();
				LayoutContainerArrayWhichHasSnapshot.Add(LayoutContainer);
			}
		}
		Widget->UpdateLayout();
	}
}

void UDreamUIManagerWorldSubsystem::RebuildLayoutImmediately(UDreamWidget* InWidget)
{
	auto RootLayoutWidget = InWidget;
	//move up, find if parent widget affect by layout then mark dirty
	while (RootLayoutWidget)
	{
		if (auto ParentWidget = RootLayoutWidget->GetParent())
		{
			if (ParentWidget->GetLayoutContainer())//parent contains LayoutContainer, need calculate layout
			{
				RootLayoutWidget = ParentWidget;
				continue;
			}
		}
		break;
	}

	bool bCanCalculateLayoutTree = true;
	if (RootLayoutWidget == InWidget)//no valid layout parent
	{
		if (InWidget->GetLayoutContainer())//self contains layout container
		{
			bCanCalculateLayoutTree = true;
		}
		else
		{
			bCanCalculateLayoutTree = false;
		}
	}
	if (bCanCalculateLayoutTree)
	{
		CalculateLayoutTree(RootLayoutWidget);
	}
}

#if WITH_EDITOR
int UDreamUIManagerWorldSubsystem::IncreateLayoutCalculationCounter(const FString& InPathName)
{
	if (auto CounterPtr = LayoutCalculationCounterMap.Find(InPathName))
	{
		(*CounterPtr)++;
		if (*CounterPtr >= 2)
		{
			// UE_LOG(DreamGUI, Warning, TEXT("Widget %s has been calculated layout %d times in a frame"), *InPathName, *CounterPtr);
		}
		return *CounterPtr;
	}
	else
	{
		LayoutCalculationCounterMap.Add(InPathName, 1);
		return 1;
	}
}
#endif

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

void UDreamUIManagerWorldSubsystem::AddRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		auto& AllRaycasterArray = Instance->AllRaycasterArray;
		if (AllRaycasterArray.Contains(InRaycaster))return;
		AllRaycasterArray.Add(InRaycaster);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		int32 index;
		if (Instance->AllRaycasterArray.Find(InRaycaster, index))
		{
			Instance->AllRaycasterArray.RemoveAt(index);
		}
	}
}

void UDreamUIManagerWorldSubsystem::AddSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		auto& AllSelectableArray = Instance->AllSelectableArray;
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
		if (AllSelectableArray.Contains(InSelectable))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		AllSelectableArray.AddUnique(InSelectable);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		int32 index;
		if (Instance->AllSelectableArray.Find(InSelectable, index))
		{
			Instance->AllSelectableArray.RemoveAt(index);
		}
	}
}

UDreamEventSystem* UDreamUIManagerWorldSubsystem::GetEventSystemByUserIndex(int UserIndex)
{
	if (auto ResultPtr = MapUserIndexToEventSystem.Find(UserIndex))
	{
		return ResultPtr->Get();
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddEventSystem(UDreamEventSystem* InEventSystem)
{
	if (!IsValid(InEventSystem))return;

	// The entry is a weak pointer, so "a key exists" and "an event system is registered" are different
	// questions. A level reload destroys the old component and leaves its stale entry behind: asking
	// that entry for an owner to name was a null dereference, and reporting it was a duplicate error
	// about a component that no longer exists -- after which the new level's UI was never registered
	// and stopped responding entirely.
	auto InstancePtr = MapUserIndexToEventSystem.Find(InEventSystem->GetUserIndex());
	UDreamEventSystem* Instance = InstancePtr != nullptr ? InstancePtr->Get() : nullptr;
	if (IsValid(Instance) && Instance != InEventSystem)
	{
		const AActor* InstanceOwner = Instance->GetOwner();
		FString ActorName = InstanceOwner == nullptr ? TEXT("(no owner)") :
#if WITH_EDITOR
			InstanceOwner->GetActorLabel();
#else
			InstanceOwner->GetName();
#endif
		FString ErrorMsg = FString::Printf(TEXT("[%s].%d DreamEventSystem component is already exist in actor:%s, pathName:%s, world:%s, multiple DreamEventSystem with same UserIndex in same world is not allowed!")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ActorName, *Instance->GetPathName(), *GetWorld()->GetPathName());
		UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrorMsg);
		GEngine->AddOnScreenDebugMessage(-1, -1, FColor::Red, ErrorMsg);
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(FText::FromString(ErrorMsg), false, 10);
#endif
	}
	else
	{
		MapUserIndexToEventSystem.Add(InEventSystem->GetUserIndex(), InEventSystem);
	}
}

void UDreamUIManagerWorldSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;

	const int UserIndex = InEventSystem->GetUserIndex();
	auto InstancePtr = MapUserIndexToEventSystem.Find(UserIndex);
	if (InstancePtr == nullptr)return;
	// Removed by identity, not by user index. An unregister arriving late -- the previous level's event
	// system being destroyed after the new one has already claimed the same index -- used to evict the
	// live registration and leave that player's UI deaf with nothing in the log.
	UDreamEventSystem* Instance = InstancePtr->Get();
	if (Instance == InEventSystem || Instance == nullptr)
	{
		MapUserIndexToEventSystem.Remove(UserIndex);
	}
}

namespace DreamInteractionLocal
{
	/**
	 * The index of the first local player -- the one a null owning player resolves to everywhere else
	 * in the plugin. Usually 0, but it is read rather than assumed so that "the first player" keeps
	 * meaning the same thing here as it does to a widget asking who owns it.
	 */
	int32 FirstLocalPlayerIndex(const UWorld* InWorld)
	{
		return InWorld != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(InWorld->GetFirstPlayerController()) : 0;
	}

	/** Does this player already have a raycaster of this kind, wherever it was placed? */
	bool HasRaycasterOfKind(const UDreamBaseRaycaster* InRaycaster, EDreamInteractionKind InKind)
	{
		return InKind == EDreamInteractionKind::Screen
			? InRaycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass())
			: InRaycaster->IsA(UDreamWorldSpaceRaycaster::StaticClass());
	}
}

void UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind)
{
	UWorld* World = GetWorld();
	if (World == nullptr)return;

	// The event system for THIS player, not "the one at index 0". A second local player with no event
	// system of their own gets nothing rather than borrowing the first player's cursor.
	//
	// The registry is only half the answer: a placed event system enrols itself when it begins play,
	// so during level startup the component can exist while the map does not know about it yet.
	bool bHasEventSystem = GetEventSystemByUserIndex(InUserIndex) != nullptr;
	if (!bHasEventSystem)
	{
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if (const UDreamEventSystem* PlacedEventSystem = ActorIt->FindComponentByClass<UDreamEventSystem>();
				PlacedEventSystem != nullptr && PlacedEventSystem->GetUserIndex() == InUserIndex)
			{
				bHasEventSystem = true;
				break;
			}
		}
	}
	// Only the first player gets one spawned for them. A second local player's event system has to be
	// placed deliberately -- with its UserIndex set -- because spawning a copy of the default actor
	// would give both players the same index and make each read the other's input.
	if (!bHasEventSystem && InUserIndex != DreamInteractionLocal::FirstLocalPlayerIndex(World))
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("Local player %d has DreamUI to point at but no event system with that UserIndex, so it takes no input. ")
			TEXT("Place a DreamEventSystem with UserIndex %d for that player."), InUserIndex, InUserIndex);
	}
	else if (!bHasEventSystem && !IsValid(CreatedEventSystemActor))
	{
		if (UClass* EventSystemClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->EventSystemActorClass, TEXT("EventSystemActorClass")))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(World, EventSystemClass, TEXT("DreamEventSystem"));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreatedEventSystemActor = World->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, SpawnParameters);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("Cannot create DreamUI input: Project Settings > Plugins > Dream GUI > ")
				TEXT("EventSystemActorClass is not set or failed to load."));
		}
	}

	// An authored raycaster wins. Somebody who placed a world-space raycaster on their pawn, or a
	// screen raycaster with a hand-tuned drag threshold, said what they wanted; adding a default one
	// beside it would give that player two rays into the same UI.
	for (const TWeakObjectPtr<UDreamBaseRaycaster>& RaycasterPtr : AllRaycasterArray)
	{
		const UDreamBaseRaycaster* Raycaster = RaycasterPtr.Get();
		if (IsValid(Raycaster) && Raycaster->GetUserIndex() == InUserIndex
			&& DreamInteractionLocal::HasRaycasterOfKind(Raycaster, InKind))
		{
			return;
		}
	}

	TObjectPtr<AActor>& HostSlot = InteractionHosts.FindOrAdd(InUserIndex);
	if (!IsValid(HostSlot))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(),
			*FString::Printf(TEXT("DreamInteractionHost_P%d"), InUserIndex));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HostSlot = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (IsValid(HostSlot))
		{
			HostSlot->SetActorEnableCollision(false);
		}
	}
	AActor* Host = HostSlot.Get();
	if (!IsValid(Host))return;
	// Asked again on the host itself, because a raycaster only enrols in AllRaycasterArray when it
	// activates, and a world that has not begun play never activates one. Without this the second
	// call would add a second raycaster to the same host and the function would not be idempotent
	// in exactly the case -- an inactive or headless world -- where nothing else would notice.
	for (UActorComponent* Component : Host->GetComponents())
	{
		const UDreamBaseRaycaster* Existing = Cast<UDreamBaseRaycaster>(Component);
		if (Existing != nullptr && DreamInteractionLocal::HasRaycasterOfKind(Existing, InKind))
		{
			return;
		}
	}

	UDreamBaseRaycaster* NewRaycaster = InKind == EDreamInteractionKind::Screen
		? static_cast<UDreamBaseRaycaster*>(NewObject<UDreamScreenSpaceRaycaster>(Host, NAME_None, RF_Transient))
		: static_cast<UDreamBaseRaycaster*>(NewObject<UDreamWorldSpaceRaycaster>(Host, NAME_None, RF_Transient));
	NewRaycaster->SetUserIndex(InUserIndex);
	Host->AddInstanceComponent(NewRaycaster);
	NewRaycaster->RegisterComponent();
}

AActor* UDreamUIManagerWorldSubsystem::GetInteractionHost(int32 InUserIndex)const
{
	const TObjectPtr<AActor>* Found = InteractionHosts.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

#undef LOCTEXT_NAMESPACE
