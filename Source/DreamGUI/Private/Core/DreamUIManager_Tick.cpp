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
#include "Core/DreamUIRenderLayerTable.h"
#include "Event/DreamBaseRaycaster.h"
#include "RenderingThread.h"
#include "Async/ParallelFor.h"
#include <atomic>
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRenderStats.h"
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

TStatId UDreamUIManagerWorldSubsystem::GetStatId() const
{
	//return GetStatID();
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamGUIManagerWorldSubsystem, STATGROUP_Tickables);
}
bool UDreamUIManagerWorldSubsystem::IsTickableWhenPaused() const
{
	return true;
}

DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Tick"), STAT_DreamUIBehaviourTick, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Start"), STAT_DreamUIBehaviourStart, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("UpdateLayout"), STAT_UpdateLayout, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("PropertyBindings Poll"), STAT_DreamUIPropertyBindingsPoll, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("RefreshAllClipData"), STAT_DreamUIRefreshClipData, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("UpdateRootCanvas"), STAT_DreamUIUpdateRootCanvas, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("RenderPrioritySort"), STAT_DreamUIRenderPrioritySort, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("SubmitCanvasDrawCall"), STAT_DreamUISubmitCanvasDrawCall, STATGROUP_DreamGUI);

static TAutoConsoleVariable<int32> CVarDreamUIParallelVertexRefreshMinCanvases(
	TEXT("r.DreamUI.ParallelVertexRefreshMinCanvases"),
	32,
	TEXT("When at least this many canvases have vertices to refresh in a frame, the refreshes run on the task graph's workers ")
	TEXT("as well as the game thread. 0: always on the game thread."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDreamUIParallelLayerPlacementMinCanvases(
	TEXT("r.DreamUI.ParallelLayerPlacementMinCanvases"),
	32,
	TEXT("When at least this many canvases have render layers to place in a frame, the placing -- each layer's matrix and ")
	TEXT("section boxes -- runs on the task graph's workers as well as the game thread. 0: always on the game thread."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDreamUIDeferTransformNotifications(
	TEXT("r.DreamUI.DeferTransformNotifications"),
	1,
	TEXT("1: a widget's move is announced -- its canvas and visual told, its OnTransformChanged listeners called -- once, at ")
	TEXT("the UI manager's next flush (after the layout pass, and at the end of the frame). 0: at every write, for the moved ")
	TEXT("widget and its whole subtree. World transforms are composed when they are read either way."),
	ECVF_Default);

void UDreamUIManagerWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld() && EditorTick.IsBound())
	{
		EditorTick.Broadcast(DeltaTime);
	}
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
	DREAMUI_STAGE_SCOPE(ManagerTick);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_SweepParkedWidgets);
		SweepExpiredParkedWidgets();
	}
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
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_PropertyBindingsPoll);
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
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_BehaviourStart);
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
					if (item.IsValid() && item->bIsEnableCalled && item->bCanExecuteTick && item->HasTickWork())
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
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_BehaviourTick);
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
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_LayoutPass);
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

	// Every move since the last flush -- animations, tweens, input, the behaviours and the layout pass
	// above -- announced once, before the clips and the canvases below read what it tells them.
	FlushTransformChanges();

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
	const int32 ScreenSpaceOverlayCanvasCount = [this]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_OverlayCanvasCheck);
		return CountCompetingScreenSpaceOverlayCanvases();
	}();
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
	// A root canvas's clips are refreshed where the root is updated, just before, in the pass over the root canvases below:
	// only a root has clips to refresh (RefreshAllClipData), and a pass of their own over every canvas was two looks at each
	// of a world of panels a frame for none.

	//update draw-call
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIUpdateRootCanvas);
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_UpdateRootCanvases);
		// One stage for every root canvas's update: timed per canvas, a wall of thousands of world panels read the clock
		// thousands of times a frame for it.
		DREAMUI_STAGE_SCOPE(CanvasUpdate);
		bCanvasesUpdatedSinceSubmit = true;
		// Recorded and sent as one: see SubmitCanvasDrawCall.
		FRenderCommandList::FRecordScope RecordScope(FRenderCommandList::Create(ERenderCommandListFlags::CloseOnSubmit), FRenderCommandList::EStopRecordingAction::Submit);
		const UWorld* World = GetWorld();
		ForEachRootCanvasInRenderModeOrder(true, [World](UDreamCanvas* Canvas)
		{
			Canvas->RefreshAllClipData();
			Canvas->UpdateRootCanvas(World);
		});
	}
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();

	// Consume render-priority sort requests at their owner. A request raised outside the owner's own
	// draw-call rebuild (runtime SetSortOrder, a child canvas rebuilding alone) used to sit in the flag
	// until the owner happened to rebuild for some other reason; this sweep executes it the same frame.
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamUIRenderPrioritySort);
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_RenderPrioritySort);
		// Only the canvases that asked (AddRenderPrioritySortRequest), each once: every registered canvas was looked at every
		// frame for the few that had.
		if (RenderPrioritySortRequests.Num() > 0)
		{
			TArray<TWeakObjectPtr<UDreamCanvas>> Requests;
			Swap(Requests, RenderPrioritySortRequests);
			TArray<UDreamCanvas*> StillAsking;
			for (const TWeakObjectPtr<UDreamCanvas>& WeakCanvas : Requests)
			{
				UDreamCanvas* const Canvas = WeakCanvas.Get();
				// Gone, let go of, or listed twice and taken already.
				if (Canvas == nullptr || Canvas->RegisteredWithManager != this || !Canvas->bRenderPrioritySortListed)
				{
					continue;
				}
				Canvas->bRenderPrioritySortListed = false;
				Canvas->ConsumePendingRenderPrioritySort();
				// One that does not own its sorting keeps its request until a rebuild of its own takes it, as it did when every
				// canvas was looked at every frame: it sorts once it owns its sorting.
				if (Canvas->bNeedToSortRenderPriority)
				{
					StillAsking.Add(Canvas);
				}
			}
			for (UDreamCanvas* Canvas : StillAsking)
			{
				// Asked again meanwhile, and listed then.
				if (!Canvas->bRenderPrioritySortListed)
				{
					Canvas->bRenderPrioritySortListed = true;
					RenderPrioritySortRequests.Add(Canvas);
				}
			}
		}
	}
}

namespace DreamUIManagerTickLocal
{
	/** Moved on by InvalidateRootCanvasOrder; a manager whose sort is older sorts again. */
	std::atomic<uint64> RootCanvasOrderGeneration = 1;
	/** The render modes of UDreamUIManagerWorldSubsystem::RootCanvasesByPass, in the order the passes take them. */
	constexpr EDreamRenderMode PassOrder[] = { EDreamRenderMode::ScreenSpaceOverlay, EDreamRenderMode::WorldSpace, EDreamRenderMode::WorldSpace_DreamUI, EDreamRenderMode::RenderTarget };
}

void UDreamUIManagerWorldSubsystem::SortRootCanvasesIfStale()
{
	using namespace DreamUIManagerTickLocal;
	static_assert(UE_ARRAY_COUNT(PassOrder) == UE_ARRAY_COUNT(RootCanvasesByPass), "One sorted list per pass");
	const uint64 Generation = DreamUIManagerTickLocal::RootCanvasOrderGeneration.load(std::memory_order_relaxed);
	if (RootCanvasOrderGeneration == Generation)
	{
		return;
	}
	RootCanvasOrderGeneration = Generation;
	for (TArray<TWeakObjectPtr<UDreamCanvas>>& Pass : RootCanvasesByPass)
	{
		Pass.Reset();
	}
	for (const TWeakObjectPtr<UDreamCanvas>& Canvas : AllCanvasArray)
	{
		const UDreamCanvas* Resolved = Canvas.Get();
		if (Resolved == nullptr || !Resolved->IsRootCanvas())continue;
		const EDreamRenderMode Mode = Resolved->GetRenderMode();
		for (int32 Pass = 0; Pass < UE_ARRAY_COUNT(PassOrder); ++Pass)
		{
			if (PassOrder[Pass] == Mode)
			{
				RootCanvasesByPass[Pass].Add(Canvas);
				break;
			}
		}
	}
}

void UDreamUIManagerWorldSubsystem::InvalidateRootCanvasOrder()
{
	DreamUIManagerTickLocal::RootCanvasOrderGeneration.fetch_add(1, std::memory_order_relaxed);
}

void UDreamUIManagerWorldSubsystem::ForEachRootCanvasInRenderModeOrder(bool bInActualRenderMode, TFunctionRef<void(UDreamCanvas*)> InFunction)
{
	/**
	 * Screen space first, then world space, then render targets, as four passes over the registry used to take them --
	 * sorted in one walk instead, and that walk only when a canvas came or went, found another root or changed its mode.
	 * A root's actual render mode is the one it is set to, so one sort serves both kinds of pass. Each canvas is looked at
	 * again when its turn comes: a call may make a render target and broadcast it, and a listener may unregister a canvas
	 * or change its mode. One registered meanwhile waits for the next frame.
	 */
	auto ModeOf = [bInActualRenderMode](const UDreamCanvas* Canvas)
	{
		return bInActualRenderMode ? Canvas->GetActualRenderMode() : Canvas->GetRenderMode();
	};
	SortRootCanvasesIfStale();
	for (int32 Pass = 0; Pass < UE_ARRAY_COUNT(DreamUIManagerTickLocal::PassOrder); ++Pass)
	{
		// A copy: a call that sorts the canvases again, through a pass of its own, would otherwise change the list under
		// this one.
		const TArray<TWeakObjectPtr<UDreamCanvas>> Canvases = RootCanvasesByPass[Pass];
		for (const TWeakObjectPtr<UDreamCanvas>& WeakCanvas : Canvases)
		{
			// Looked up once for the checks and the call: IsCanvasStillRegistered and a weak look-up for each use after it were
			// four for every canvas of every pass.
			UDreamCanvas* const Canvas = WeakCanvas.Get();
			if (Canvas == nullptr || Canvas->RegisteredWithManager != this)continue;
			if (!Canvas->IsRootCanvas())continue;
			if (ModeOf(Canvas) != DreamUIManagerTickLocal::PassOrder[Pass])continue;
			InFunction(Canvas);
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates(UWorld* InWorld)
{
	if (InWorld == this->GetWorld())
	{
		// What moved after the tick -- a later tick group, a script, the editor -- is announced before the
		// frame ends, and its canvases take it at their next update.
		FlushTransformChanges();
#if WITH_EDITOR
		this->DrawHelperGizmo();
#endif
		/**
		 * A world sends its end-of-frame updates when its tick ends and again when a viewport draws it. Only the canvases'
		 * own update makes anything to submit -- vertices to refresh, a batch to wait for -- so once they have been
		 * submitted this frame, a second pass with no update in between would look at a thousand world panels to find
		 * nothing. A test or tool that calls SubmitCanvasDrawCall itself is not held to this.
		 */
		if (LastEndOfFrameSubmitFrame == GFrameCounter && !bCanvasesUpdatedSinceSubmit)
		{
			return;
		}
		LastEndOfFrameSubmitFrame = GFrameCounter;
		bCanvasesUpdatedSinceSubmit = false;
		this->SubmitCanvasDrawCall();
	}
}

bool UDreamUIManagerWorldSubsystem::DefersTransformChanges()const
{
	if (bWorldTornDown || CVarDreamUIDeferTransformNotifications.GetValueOnGameThread() == 0)
	{
		return false;
	}
#if WITH_EDITOR
	// A manager that does not tick -- a preview nobody shows -- would hold a change until something drew its
	// world, if anything ever did. Its widgets are told on the spot, as they always were.
	return bShouldTickInEditor;
#else
	return true;
#endif
}

void UDreamUIManagerWorldSubsystem::AddTransformChangeRoot(UDreamWidget* InWidget)
{
	TransformChangeRoots.Add(InWidget);
}

void UDreamUIManagerWorldSubsystem::FlushTransformChanges()
{
	// Asked for again from inside one, by something a listener did: its moves land in the running flush's
	// next pass instead.
	if (bIsFlushingTransformChanges || TransformChangeRoots.Num() == 0)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_FlushTransformChanges);
	TGuardValue<bool> FlushingGuard(bIsFlushingTransformChanges, true);
	// The layout loop's limit, for the same reason: every pass after the first is caused by the one before
	// it, and a listener that answers each move with another would otherwise never let the frame end.
	constexpr int32 MaxTransformFlushPasses = 32;
	int32 PassCount = 0;
	TArray<UDreamWidget*> NestedRoots;
	while (TransformChangeRoots.Num() > 0 && PassCount < MaxTransformFlushPasses)
	{
		++PassCount;
		// Taken whole, into an array kept for it so that neither gives up its memory from frame to frame:
		// what the listeners move from here is the next pass's.
		Swap(TransformChangeRootsBeingFlushed, TransformChangeRoots);
		NestedRoots.Reset();
		for (const TWeakObjectPtr<UDreamWidget>& WeakRoot : TransformChangeRootsBeingFlushed)
		{
			UDreamWidget* Root = WeakRoot.Get();
			// Gone, or already reached from a root flushed before it, or listed twice.
			if (!IsValid(Root) || !Root->IsTransformChangePending())
			{
				continue;
			}
			// Under a widget whose own move is pending too, which is flushed first so that the subtree is
			// announced parents first; the walk from it reaches this one on the way.
			const UDreamWidget* Parent = Root->GetParent();
			if (Parent != nullptr && Parent->IsTransformChangePending())
			{
				NestedRoots.Add(Root);
				continue;
			}
			Root->FlushTransformChanges();
		}
		// Whatever of those is still pending hangs under a pending widget no root of this pass leads to --
		// one moved out from under it, say -- and is flushed from where it is.
		for (UDreamWidget* Root : NestedRoots)
		{
			if (IsValid(Root) && Root->IsTransformChangePending())
			{
				Root->FlushTransformChanges();
			}
		}
		TransformChangeRootsBeingFlushed.Reset();
	}
	if (TransformChangeRoots.Num() > 0)
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("Widget transform changes did not settle after %d passes in World %s: an OnTransformChanged listener keeps moving widgets. %d move(s) wait for the next flush."),
			MaxTransformFlushPasses, *GetNameSafe(GetWorld()), TransformChangeRoots.Num());
	}
}


void UDreamUIManagerWorldSubsystem::SubmitCanvasDrawCall()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUISubmitCanvasDrawCall);
	DREAMUI_STAGE_SCOPE(DrawCallSubmit);
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();
	//update draw-call
	{
		/**
		 * The render commands the canvases send -- each of a thousand world panels patching its vertices and moving its
		 * render root -- recorded on this thread and handed to the render thread together when the pass ends. Enqueued one
		 * by one, each was a task of its own and, whenever the render thread had gone idle, a wake-up of it. The order is
		 * kept, and anything that waits for the render thread meanwhile (FlushRenderingCommands, a fence) sends what was
		 * recorded first.
		 */
		FRenderCommandList* const RenderCommands = FRenderCommandList::Create(ERenderCommandListFlags::CloseOnSubmit);
		FRenderCommandList::FRecordScope RecordScope(RenderCommands, FRenderCommandList::EStopRecordingAction::Submit);
		// Each canvas's UpdateDrawCallBatchData, in its three parts: what has to happen on the game thread for every canvas
		// first, then the vertex refreshes, which touch nothing another canvas does, on as many threads as there are, then
		// the game thread's again.
		TArray<UDreamCanvas*> ToRefresh;
		TArray<UDreamCanvas*> ToFinish;
		ForEachRootCanvasInRenderModeOrder(false, [&ToRefresh, &ToFinish](UDreamCanvas* Canvas) { Canvas->TakeDrawCallBatchData(ToRefresh, ToFinish); });
		const int32 MinCanvases = CVarDreamUIParallelVertexRefreshMinCanvases.GetValueOnGameThread();
		if (MinCanvases > 0 && ToRefresh.Num() >= MinCanvases && FApp::ShouldUseThreadingForPerformance())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_ParallelVertexRefresh);
			// A refresh sends no render command, but should one ever come to, each task records its own and they join this
			// frame's in task order when the context goes out of scope.
			constexpr int32 MinBatchSize = 8;
			constexpr EParallelForFlags Flags = EParallelForFlags::None;
			FRenderCommandList::FParallelForContext ParallelForContext(RenderCommands, ToRefresh.Num(), MinBatchSize, Flags);
			ParallelForWithExistingTaskContext(TEXT("DreamUI_RefreshDrawCallVertices"), ParallelForContext.GetCommandLists(), ToRefresh.Num(), MinBatchSize,
				[&ToRefresh](FRenderCommandList* TaskCommands, int32 Index)
				{
					FRenderCommandList::FRecordScope TaskRecordScope(TaskCommands);
					ToRefresh[Index]->RefreshDrawCallVertices();
				}, Flags);
		}
		else
		{
			for (UDreamCanvas* Canvas : ToRefresh)
			{
				Canvas->RefreshDrawCallVertices();
			}
		}
		// Their render layers placed where the layers now are: tended on the game thread, then placed -- a matrix and a box
		// for each section, the canvas's own -- on as many threads as there are, and the rest of each finish after.
		TArray<UDreamCanvas*> ToPlace;
		for (UDreamCanvas* Canvas : ToFinish)
		{
			// Still registered here, asked of the flags the tending reads next rather than of the object array; one let go of
			// meanwhile is asked about as before.
			if ((Canvas->RegisteredWithManager == this || IsValid(Canvas)) && Canvas->TendRenderLayersBeforeFinish())
			{
				ToPlace.Add(Canvas);
			}
		}
		const int32 MinPlacingCanvases = CVarDreamUIParallelLayerPlacementMinCanvases.GetValueOnGameThread();
		if (MinPlacingCanvases > 0 && ToPlace.Num() >= MinPlacingCanvases && FApp::ShouldUseThreadingForPerformance())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_ParallelLayerPlacement);
			constexpr int32 MinBatchSize = 16;
			ParallelFor(TEXT("DreamUI_PlaceRenderLayers"), ToPlace.Num(), MinBatchSize, [&ToPlace](int32 Index)
			{
				ToPlace[Index]->PlaceRenderLayers();
			});
		}
		else
		{
			for (UDreamCanvas* Canvas : ToPlace)
			{
				Canvas->PlaceRenderLayers();
			}
		}
		// Every row the canvases wrote this frame -- a layer made, a layer placed -- sent up together, a run at a time.
		if (RenderLayerTable != nullptr)
		{
			RenderLayerTable->Flush();
		}
		for (UDreamCanvas* Canvas : ToFinish)
		{
			if (Canvas->RegisteredWithManager == this || IsValid(Canvas))
			{
				Canvas->FinishDrawCallBatchData();
			}
		}
	}
	// The render-target canvases that asked to be drawn (AddRenderTargetDrawRequest), now that every canvas has sent this
	// frame's sections: every registered canvas was looked at every frame for the few that had.
	if (RenderTargetDrawRequests.Num() > 0)
	{
		TArray<TWeakObjectPtr<UDreamCanvas>> Requests;
		Swap(Requests, RenderTargetDrawRequests);
		for (const TWeakObjectPtr<UDreamCanvas>& WeakCanvas : Requests)
		{
			UDreamCanvas* const Canvas = WeakCanvas.Get();
			if (Canvas == nullptr || Canvas->RegisteredWithManager != this)
			{
				continue;
			}
			if (Canvas->IsRootCanvas())
			{
				Canvas->DrawRenderTargetIfRequested();
			}
			// Not a root just now, and so not drawn: asked again at the next submit, as when every canvas was looked at.
			if (Canvas->bRenderTargetDrawRequested && !RenderTargetDrawRequests.Contains(WeakCanvas))
			{
				RenderTargetDrawRequests.Add(WeakCanvas);
			}
		}
	}
}

bool UDreamUIManagerWorldSubsystem::IsBehaviourOnTickVisit(const UDreamUIBehaviour* InBehaviour) const
{
	return InBehaviour != nullptr && DreamUIBehavioursForTick.ContainsByPredicate([InBehaviour](const TWeakObjectPtr<UDreamUIBehaviour>& InItem)
	{
		return InItem.Get() == InBehaviour;
	});
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

#undef LOCTEXT_NAMESPACE
