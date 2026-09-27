// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Event/DreamEventSystem.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#endif

UDreamWidget::UDreamWidget()
{
	bFlattenHierarchyIndexDirty = true;
	bNeedSortUIChildren = true;
	bIsCanvasWidget = false;
	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;

	bClipDirty = true;
	bNeedRecreateClip = true;
}

void UDreamWidget::BeginPlay()
{
	// Every caller checks first. One that did not is a bug to report, not one to take the process down for.
	if (!ensureMsgf(!bHasBegunPlay, TEXT("%s: BeginPlay on a widget that has already begun play."), *GetPathName()))
	{
		return;
	}
	bHasBegunPlay = true;

	// Iterate a snapshot: a component's BeginPlay can add or remove components on this same widget (a layout
	// container attaching its companion behaviour, for one), which would invalidate a ranged-for over Components.
	// OnRegister and OnUnregister already guard this way.
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToBeginPlay = Components;
	for (auto Component : ComponentsToBeginPlay)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->BeginPlay();
		}
	}

	if (IsValid(LayoutContainer))
	{
		LayoutContainer->BeginPlay();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->BeginPlay();
	}
	if (IsValid(PanelSlot))
	{
		PanelSlot->BeginPlay();
	}
	if (IsValid(Visual))
	{
		Visual->BeginPlay();
	}
}

void UDreamWidget::EndPlay()
{
	bHasBegunPlay = false;

	// Iterate a snapshot, exactly as BeginPlay/OnRegister/OnUnregister do. A component's EndPlay runs
	// its OnDisable and OnDestroy, and behaviour code there is free to DestroyComponent() -- which
	// goes RemoveComponent -> Components.RemoveAt and invalidates a live ranged-for.
	// UUIScrollView::OnDestroy releases its range helper exactly that way.
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToEndPlay = Components;
	for (auto Component : ComponentsToEndPlay)
	{
		if (IsValid(Component))
		{
			Component->EndPlay();
		}
	}
	
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->EndPlay();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->EndPlay();
	}
	if (IsValid(PanelSlot))
	{
		PanelSlot->EndPlay();
	}
	if (IsValid(Visual))
	{
		Visual->EndPlay();
	}
}

void UDreamWidget::Call_InteractableChanged()
{
	OnInteractableChangedEvent.Broadcast(this->GetInteractableInHierarchy());
}
void UDreamWidget::Call_TransformChanged()
{
	OnTransformChangedEvent.Broadcast();
}

void UDreamWidget::Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);

	if (Parent.IsValid())
	{
		Parent->Call_ChildDimensionsChanged(this, InPivotChanged, InWidthChanged, InHeightChanged);
	}
}

void UDreamWidget::Call_ChildDimensionsChanged(UDreamWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnChildDimensionChangedEvent.Broadcast(Child, InPivotChanged, InWidthChanged, InHeightChanged);
}

void UDreamWidget::Call_AttachmentChanged()
{
	OnAttachmentChangedEvent.Broadcast();
}

void UDreamWidget::Call_SiblingIndexChanged()
{
	OnSiblingIndexChangedEvent.Broadcast();
}

void UDreamWidget::Call_WidgetActiveChanged()
{
	OnWidgetActiveChangedEvent.Broadcast(this->GetWidgetActiveInHierarchy());
}
void UDreamWidget::Call_RaycastableChanged()
{
	OnRaycastableChangedEvent.Broadcast(this->GetRaycastableInHierarchy());
}

void UDreamWidget::MarkAllDirtyRecursive()
{
	MarkAllDirty();
	
	for (auto& uiChild : Children)
	{
		if (IsValid(uiChild))
		{
			uiChild->MarkAllDirtyRecursive();
		}
	}
}

void UDreamWidget::MarkAllDirty()
{
	bFlattenHierarchyIndexDirty = true;
	bClipDirty = true;
	MarkWorldRectBoundsDirty();

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;
	
	if (IsValid(Visual))
	{
		Visual->MarkAllDirty();
	}
}

void UDreamWidget::MarkRenderModeChangeRecursive(UDreamCanvas* Canvas)
{
	// The old and new render modes used to be carried through here as parameters and were never read by
	// anything: the whole subtree is dirtied wholesale either way, because UE and DreamGUI mesh data are
	// not compatible in either direction. Dropped rather than left as a promise the signature cannot keep.
	if (this->RenderCanvas == Canvas)
	{
		MarkAllDirty();
		for (auto& uiChild : Children)
		{
			if (IsValid(uiChild))
			{
				uiChild->MarkRenderModeChangeRecursive(Canvas);
			}
		}
	}
}


void UDreamWidget::PostLoad()
{
	Super::PostLoad();
	// RelativeRotationEuler is transient, so seed it from the serialized rotation. Loading writes
	// RelativeRotation through reflection rather than the setter, which would leave the mirror at
	// zero and make Sequencer restore an animated widget to no rotation at all.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	// Every asset authored before ids existed has none. Backfilling here rather than in a migration
	// commandlet keeps a widget that never gets resaved working for the session it is open in: the
	// preview is instanced from this object, so it copies whatever id this object is holding.
	EnsureWidgetGuid();
}

void UDreamWidget::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::PIE)
	{
		DreamUI::ReportCopiedIntoPlaySession(*this);
	}
}

void UDreamWidget::BeginDestroy()
{
	if (bHasBegunPlay || bIsRegistered)
	{
		UDreamWidget* TeardownRoot = RootWidget.GetEvenIfUnreachable();
		if (TeardownRoot == nullptr || TeardownRoot->HasAnyFlags(RF_FinishDestroyed))
		{
			TeardownRoot = this;
		}

		auto World = TeardownRoot->GetWorld();
		auto WorldName = World ? World->GetName() : TEXT("null");
		auto Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
		auto ManagerName = Manager ? Manager->GetName() : TEXT("null");

		// AN ERROR ONLY WHERE IT CAN MEAN SOMETHING, which is where there is a world.
		//
		// This says "an owner should have destroyed this tree", and the harm it names is the manager
		// still holding pointers into a hierarchy nobody tore down -- ticks, raycasts and a draw list
		// pointing at widgets on their way out. A tree with NO world has no manager to have leaked
		// into: nothing outside its own object graph refers to it, and the collector reaching it is
		// simply the end of its life rather than a symptom of one. That is the state of every tree in
		// a headless test and of every Blueprint authoring tree.
		//
		// Reporting it as an Error there was worse than useless, because BeginDestroy runs at GC
		// TIME and the automation framework attributes whatever is logged to whichever test happens
		// to be running: one leak inside the widget-blueprint tests reliably failed an unrelated one,
		// and it passed when run alone. A diagnostic that fails an innocent test teaches people to
		// ignore the suite. As a Warning it still names the tree in the log for anyone auditing test
		// hygiene -- TDreamTestControl exists for exactly that -- without accusing a bystander.
		if (World != nullptr)
		{
			UE_LOG(DreamGUI, Error, TEXT("UDreamWidget tree %s was not destroyed by its owner. World:%s, WorldType:%d, Manager:%s. Auto cleanup in BeginDestroy."),
				*TeardownRoot->GetPathDisplayName(), *WorldName, World->WorldType, *ManagerName);

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red
					, FString::Printf(TEXT("UDreamWidget tree %s was not destroyed by its owner; auto cleaned up. World:%s, Manager:%s")
						, *TeardownRoot->GetPathDisplayName(), *WorldName, *ManagerName));
			}
		}
		else
		{
			UE_LOG(DreamGUI, Warning, TEXT("UDreamWidget tree %s reached the collector still registered, with no world to have leaked into. Auto cleanup in BeginDestroy."),
				*TeardownRoot->GetPathDisplayName());
		}

		// Elevating fallback cleanup to the hierarchy root prevents one diagnostic per child.
		TeardownRoot->DestroyWidget();
	}
	Super::BeginDestroy();
}

void UDreamWidget::DestroyWidget()
{
	struct LOCAL
	{
		static void AppendSubtree(
			UDreamWidget* Widget,
			TArray<TObjectPtr<UDreamWidget>>& TeardownWidgets,
			TSet<const UDreamWidget*>& ScheduledWidgets)
		{
			// BeginDestroy can run after GC has marked the object unreachable, at which point
			// IsValid() is already false even though teardown on the live memory is still required.
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			TArray<TObjectPtr<UDreamWidget>> PendingWidgets;
			PendingWidgets.Add(Widget);
			while (!PendingWidgets.IsEmpty())
			{
				UDreamWidget* CurrentWidget = PendingWidgets.Pop(EAllowShrinking::No).Get();
				if (CurrentWidget == nullptr
					|| CurrentWidget->HasAnyFlags(RF_FinishDestroyed)
					|| ScheduledWidgets.Contains(CurrentWidget))
				{
					continue;
				}

				ScheduledWidgets.Add(CurrentWidget);
				TeardownWidgets.Add(CurrentWidget);
				const TArray<UDreamWidget*> ChildrenSnapshot = CurrentWidget->GetChildren();
				for (int32 ChildIndex = ChildrenSnapshot.Num() - 1; ChildIndex >= 0; --ChildIndex)
				{
					PendingWidgets.Add(ChildrenSnapshot[ChildIndex]);
				}
			}
		}

		static void AppendCurrentChildren(
			UDreamWidget* Widget,
			TArray<TObjectPtr<UDreamWidget>>& TeardownWidgets,
			TSet<const UDreamWidget*>& ScheduledWidgets)
		{
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			const TArray<UDreamWidget*> ChildrenSnapshot = Widget->GetChildren();
			for (UDreamWidget* Child : ChildrenSnapshot)
			{
				AppendSubtree(Child, TeardownWidgets, ScheduledWidgets);
			}
		}

		/**
		 * What InOuter owns and goes down with it: behaviours, the visual, the layout container, the
		 * panel slot, and what those made in turn -- a canvas's material instances, render target, data.
		 * Never a widget, though a widget may be outered to another (a child made with its parent as
		 * outer is), because an outer does not change when a widget is moved: whether a child goes down
		 * is the teardown list's call, which knows what was moved out while the subtree came down. Nor a
		 * widget tree, which holds widgets. Nor a component still registered with its world, which is its
		 * owner's to unregister first.
		 */
		static void AppendOwnedParts(UObject* InOuter, TArray<UObject*>& OutParts)
		{
			TArray<UObject*> Inner;
			GetObjectsWithOuter(InOuter, Inner, /*bIncludeNestedObjects*/ false);
			for (UObject* Object : Inner)
			{
				if (Object->IsA<UDreamWidget>() || Object->IsA<UDreamWidgetTree>()
					|| !IsValid(Object) || Object->IsRooted()
					|| Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed | RF_ClassDefaultObject | RF_ArchetypeObject))
				{
					continue;
				}
				if (const UActorComponent* Component = Cast<UActorComponent>(Object); Component != nullptr && Component->IsRegistered())
				{
					continue;
				}
				OutParts.Add(Object);
				AppendOwnedParts(Object, OutParts);
			}
		}
	};

	TArray<TObjectPtr<UDreamWidget>> TeardownWidgets;
	TSet<const UDreamWidget*> ScheduledWidgets;
	LOCAL::AppendSubtree(this, TeardownWidgets, ScheduledWidgets);

	int32 UnregisterIndex = 0;
	auto UnregisterPendingWidgets = [&]()
	{
		while (UnregisterIndex < TeardownWidgets.Num())
		{
			UDreamWidget* Widget = TeardownWidgets[UnregisterIndex++].Get();
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				continue;
			}
			if (Widget->bIsRegistered)
			{
				Widget->OnUnregister();
			}
			LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
		}
	};

	UnregisterPendingWidgets();
	this->SetParent(nullptr);
	const int32 WidgetCountAfterDetach = TeardownWidgets.Num();
	for (int32 WidgetIndex = 0; WidgetIndex < WidgetCountAfterDetach; ++WidgetIndex)
	{
		UDreamWidget* Widget = TeardownWidgets[WidgetIndex].Get();
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}
	UnregisterPendingWidgets();

	int32 EndPlayIndex = 0;
	while (EndPlayIndex < TeardownWidgets.Num())
	{
		UnregisterPendingWidgets();
		UDreamWidget* Widget = TeardownWidgets[EndPlayIndex++].Get();
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		if (Widget->bHasBegunPlay)
		{
			Widget->EndPlay();
		}
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}

	/**
	 * And now it is actually destroyed.
	 *
	 * Until this loop existed the word meant "unregistered and detached": IsValid() still answered true
	 * for a widget DestroyChild had just taken apart, so the several hundred IsValid checks in this
	 * plugin -- every one of them written to mean "is this still there" -- answered yes about a corpse
	 * for as long as anything happened to hold a reference. The header had to explain that away rather
	 * than the code being right.
	 *
	 * Two gates, and they are the whole reason this was not done sooner.
	 *
	 * GC. BeginDestroy reaches this function from inside a collection, and marking there would be
	 * writing into the object array the collector is walking -- for an object already being destroyed,
	 * so there is nothing the mark could still accomplish. IsGarbageCollecting() is exactly that
	 * question. IsRooted is skipped for the same kind of reason: UObjectBaseUtility::MarkAsGarbage
	 * check()s against it.
	 *
	 * Undo. The transaction buffer DOES carry a garbage transition across an undo -- FObjectRecord
	 * diffs the state Modify() captured against the state at the end of the transaction, records
	 * AliveToDead, and calls ClearGarbage() when the undo runs (UnrealEd's FTransaction::Apply). What
	 * it cannot do is carry one for an object it was never told about, and the designer's delete
	 * Modify()s only the widget the user picked, not the subtree under it. Modify() here, per widget,
	 * is what puts every one of them in the record -- so undoing a delete brings back live widgets
	 * rather than a tree of tombstones.
	 *
	 * And a third thing this has to get right, which is not a gate but a question of WHICH widgets:
	 * teardown is re-entrant by design. OnUnregister and EndPlay run with the hierarchy still writable,
	 * and a behaviour is allowed to move a widget out of the doomed subtree there (UUIScrollView re-homes
	 * its content; the lifecycle suite has a behaviour that does exactly this). Such a widget has a LIVE
	 * parent by the time the teardown finishes, so marking it would leave a garbage widget sitting in a
	 * hierarchy still in use, which the next collection would silently empty out of that parent's
	 * Children. It was still unregistered and ended -- the caller asked for that and got it -- it is
	 * simply no longer this subtree's to destroy. A widget moved the other way, INTO the subtree while it
	 * came down, is the mirror image and IS destroyed with it: that is already what the unregister loop
	 * does with it, and AppendCurrentChildren exists to catch it.
	 */
	if (!IsGarbageCollecting())
	{
		// Decide first, mark afterwards. The test below walks parent chains, and a TWeakObjectPtr stops
		// handing back an object the instant it is garbage -- marking as we went would make a widget's
		// own ancestors vanish from under the widgets after it in the list, and every one of those would
		// then look like it had been moved out.
		TArray<UDreamWidget*> WidgetsToMark;
		WidgetsToMark.Reserve(TeardownWidgets.Num());
		for (const TObjectPtr<UDreamWidget>& TornDown : TeardownWidgets)
		{
			UDreamWidget* Widget = TornDown.Get();
			if (Widget == nullptr || !IsValid(Widget) || Widget->IsRooted()
				|| Widget->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			{
				continue;
			}
			if (Widget != this && !Widget->IsChildOf(this))
			{
				continue;//moved out from under us while we were coming down; see above
			}
			WidgetsToMark.Add(Widget);
		}
		// And each widget's parts with it, or a TWeakObjectPtr to a destroyed widget's canvas, visual or
		// behaviour -- a delegate's target, a pending tween, the draw thread's view of a canvas -- would go
		// on answering valid for as long as the collector left it. Collected before any mark, for the same
		// reason as the widgets.
		TArray<UObject*> PartsToMark;
		for (UDreamWidget* Widget : WidgetsToMark)
		{
			LOCAL::AppendOwnedParts(Widget, PartsToMark);
		}
		// Recorded only where undo keeps the widget -- an authored one in the designer. A tree the level
		// editor or a preview built is transient: recording it is how undoing an unrelated edit brought
		// back a tree its host had destroyed, and marked the map dirty on the way.
		for (UDreamWidget* Widget : WidgetsToMark)
		{
			DreamUI::ModifyIfKeptByUndo(*Widget);
			Widget->MarkAsGarbage();
		}
		// The same for the parts: an undo of the delete has to bring them back to life too, and the
		// transaction only restores what it was told about.
		for (UObject* Part : PartsToMark)
		{
			DreamUI::ModifyIfKeptByUndo(*Part);
			Part->MarkAsGarbage();
		}
	}
}

UWorld* UDreamWidget::GetWorld() const
{
	auto OuterWorld = GetTypedOuter<UWorld>();
	return OuterWorld;
}


void UDreamWidget::OnRegister()
{
	// Idempotent: UDreamUIManagerWorldSubsystem::AddWidget treats a duplicate as a bug worth an error
	// and a stack dump, and every step below is either a set-to-true or an AddUnique, so a second
	// call can only do redundant work. Registering a widget you did not create is now safe to ask for.
	if (bIsRegistered)
	{
		return;
	}
	bIsRegistered = true;
	// The prefab loader writes properties straight into memory -- no setter, no
	// PostEditChangeProperty -- and restores the hierarchy through SetParentBeforeRegister, which
	// fires no attach events. So registration is the first moment the transient bits derived from
	// serialized properties can be recomputed. Without this, a saved render transform (or a saved
	// perspective declared anywhere but the root) works in the session that authored it and
	// silently does nothing after a load -- which reads as "only works in the designer".
	RefreshRenderTransformFlag();
	RefreshPerspectiveInHierarchy();
	RefreshShearInHierarchy();
	const bool bPanelSlotRegisteredByEnsure = Parent.IsValid()
		&& EnsurePanelSlotForChild(Parent.Get(), this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->AddWidget(this);
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
	}
	CheckRootWidget();

	if (this->IsRootWidgetInHierarchy())
	{
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
		CalculateRaycastable_Recursive();
		CalculateInteractable_Recursive();
	}

	if (IsValid(LayoutContainer))
	{
		LayoutContainer->Call_OnRegister();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->Call_OnRegister();
	}
	if (IsValid(PanelSlot) && !bPanelSlotRegisteredByEnsure)
	{
		PanelSlot->Call_OnRegister();
	}
	if (IsValid(Visual))
	{
		Visual->Call_OnRegister();
		if (RenderCanvas.IsValid())
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}

	Components.Remove(nullptr);//clear null component
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToRegister = Components;
	for (UDreamUIBehaviour* Component : ComponentsToRegister)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->OnRegister();
		}
	}
}
void UDreamWidget::OnUnregister()
{
	// Idempotent, like OnRegister: what follows undoes what registering did -- a canvas leaves the
	// manager, a visual leaves its canvas -- and undoing it for a widget that was never registered, or
	// twice, reaches into registries it is not in.
	if (!bIsRegistered)
	{
		return;
	}
	bIsRegistered = false;

	/**
	 * Live memory, not IsValid. Unregistering undoes what registering did -- a canvas leaves the
	 * manager and destroys the mesh it created, a visual leaves its canvas -- and that is owed by every
	 * part that registered, whether or not somebody has since marked it garbage. Somebody does: a
	 * Blueprint recompile marks the instance it replaces, and everything outered to it, as garbage
	 * while all of it is still registered. IsValid skipped those parts, so a world-space canvas never
	 * destroyed its mesh -- which belongs to the host actor and so outlived the collection -- and the
	 * mesh went on drawing with materials the collection had freed. The same rule DestroyWidget walks
	 * by; only an object whose destruction has begun, or that the collector is purging, is left alone.
	 */
	const auto IsLiveForTeardown = [](const UObject* InObject)
	{
		return InObject != nullptr
			&& !InObject->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& !InObject->IsUnreachable();
	};

	// Component teardown may remove helper behaviours from this same widget.
	// Iterate a snapshot so those callbacks cannot invalidate the active iterator.
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToUnregister = Components;
	for (UDreamUIBehaviour* Component : ComponentsToUnregister)
	{
		if (IsLiveForTeardown(Component) && Components.Contains(Component))
		{
			Component->OnUnregister();
		}
	}

	if (IsLiveForTeardown(LayoutContainer))
	{
		LayoutContainer->Call_OnUnregister();
	}
	if (IsLiveForTeardown(LayoutSelf))
	{
		LayoutSelf->Call_OnUnregister();
	}
	if (IsLiveForTeardown(PanelSlot))
	{
		PanelSlot->Call_OnUnregister();
	}
	if (IsLiveForTeardown(Visual))
	{
		Visual->Call_OnUnregister();
		UDreamCanvas* VisualCanvas = RenderCanvas.Get(/*bEvenIfPendingKill*/ true);
		if (IsLiveForTeardown(VisualCanvas))
		{
			VisualCanvas->MarkVisualWillChange(Visual);
			VisualCanvas->UnregisterVisual(Visual);
		}
	}
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->RemoveWidget(this);
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
	}
}

void UDreamWidget::RegisterRenderCanvas(UDreamCanvas* InRenderCanvas)
{
	bIsCanvasWidget = true;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		// Including the parent itself: GetComponentInParent starts ABOVE the widget it is asked on unless
		// told otherwise, and a canvas on the parent is the nearest one there is.
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>(/*bIncludeSelf*/ true);//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas != InRenderCanvas)
	{
		SetRenderCanvas(InRenderCanvas);
	}
	InRenderCanvas->SetParentCanvas(ParentCanvas);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InRenderCanvas);
		}
	}
}
void UDreamWidget::RefreshRenderCanvasFromParentChain()
{
	RenewRenderCanvasRecursive(GetComponentInParent<UDreamCanvas>(true));
}

void UDreamWidget::RefreshInheritedStateFromParentChain()
{
	// The same four walks OnRegister runs for a hierarchy root, plus the canvas. Each one reads the
	// parent's cached value and pushes the result down, and each only fires its changed-event when a
	// value actually moves -- so running this on a subtree that was already correct costs the walk
	// and nothing else.
	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	RefreshRenderCanvasFromParentChain();

	// And the raycast's own ordering, which is the sixth thing a subtree inherits from the chain it
	// was just attached to and the one this function was missing.
	//
	// FlattenHierarchyIndex is born -1 and only recalculated when the HIERARCHY ROOT is marked
	// dirty; the mark rides the attachment event, which SetParentBeforeRegister deliberately does
	// not raise. So a subtree assembled that way keeps -1 on every node -- and unlike the render
	// canvas (patched just above, for this same hole), nothing downstream self-heals it.
	//
	// It still DRAWS, which is what makes the symptom so misleading. What it loses is the hit sort:
	// UDreamBaseRaycaster orders hits within a canvas by flatten index, descending, so -1 loses to
	// every widget that has one. Measured on a modal dialog whose two buttons could not be clicked
	// at all -- scrim, panel and both buttons all read -1 while the page behind them ran 0..215, so
	// every click was awarded to the page. Re-attaching the layer by hand in a live session moved
	// them to 216..228 and the dialog answered the pointer again.
	MarkFlattenHierarchyIndexDirty();

	// And where the subtree IS: the seventh thing it inherits, and the one every other attach road
	// already re-derives. TrySetParentInternal recomputes the world transform on each attach and
	// detach, and the editor's full refresh (EnsureDataForRebuild, which the designer runs after every
	// preview rebuild) ends with this same call -- which is why no designer preview ever showed this.
	//
	// ObjectToWorldTransform is a cache of GetRenderLocalTransform() composed with the parent's, and
	// SetParentBeforeRegister, being the cheap attach, does not touch it. A subtree hung that way keeps
	// whatever it was last composed against: for anything CreateDreamWidget builds, its own user
	// widget before that had a parent -- the world origin. Nothing afterwards is bound to correct it,
	// because the setters and the layout write-back only recompute when the value they write differs
	// from the one held. A control left at its default position never moves, so it is never
	// recomputed: SetAnchoredPosition((0, 0)) on a new control returns early, and so does the
	// CalculateTransformFromAnchor a layout pass runs, which derives the relative location the widget
	// already has. The early returns are right; the state they protect was never established.
	//
	// A screen-space root sits at the origin, so there the stale value and the right one are the same
	// transform and nothing showed. Under a world-space panel or a render-target canvas they are not:
	// a button added at its default place on a panel standing in the level stayed at the world origin
	// -- drawn there, traced there, projected there -- while the same button moved sideways appeared
	// on the panel, because the move is what finally recomputed it.
	//
	// Last, for two reasons. The render canvas was re-derived above, so the canvas update the
	// recompute asks for lands on the canvas this subtree now draws into. And RegisterDreamWidgetHierarchy
	// calls this after every OnRegister in the subtree, which is where the render-transform, perspective
	// and shear bits the composition reads were refreshed from serialized data; composed any earlier, a
	// saved render transform would be left out of it.
	//
	// Unlike the four walks above, this announces itself whether or not the value moved -- the transform
	// cascade always ends in MarkTransformChanged, so every behaviour bound in the subtree hears one
	// transform change (queued until its Awake in a game world). An attach through TrySetParent opens
	// with this same recompute and says the same thing, so a behaviour now hears it whichever of the two
	// doors its widget came in by.
	CalculateObjectToWorldTransform(true);
}

void UDreamWidget::RenewRenderCanvasRecursive(UDreamCanvas* InParentRenderCanvas)
{
	// The self-canvas branch below used to be unreachable: a bare `ThisRenderCanvas = nullptr` sat
	// between the lookup and the test. It is what is left of an `&& !ThisRenderCanvas->IsRegistered()`
	// guard whose condition was dropped during the port -- UDreamUIBehaviour is a plain UObject and has
	// no such state to test, so the guard has no meaning here and the assignment is simply wrong.
	// With it in place a nested canvas kept none of its subtree: RegisterRenderCanvas,
	// UnregisterRenderCanvas and RefreshRenderCanvasFromParentChain all repointed the whole thing at
	// the ancestor canvas, losing the child canvas's own sorting, batching and clipping.
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		if (InParentRenderCanvas != ThisRenderCanvas)
		{
			ThisRenderCanvas->SetParentCanvas(InParentRenderCanvas);//set parent Canvas for this actor's Canvas
		}
		return;
	}

	if (RenderCanvas != InParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(InParentRenderCanvas);
	}

	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InParentRenderCanvas);
		}
	}
}

void UDreamWidget::UnregisterRenderCanvas()
{
	bIsCanvasWidget = false;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		// Including the parent itself: GetComponentInParent starts ABOVE the widget it is asked on unless
		// told otherwise, and a canvas on the parent is the nearest one there is.
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>(/*bIncludeSelf*/ true);//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->SetParentCanvas(nullptr);
	}
	if (RenderCanvas != ParentCanvas)
	{
		SetRenderCanvas(ParentCanvas);
	}
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(ParentCanvas);
		}
	}
}

void UDreamWidget::UpdateClip(UDreamUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FDreamUIClipData>>& ClipDataList)
{
	if (!bClipDirty)return;
	bClipDirty = false;
	
	if (bNeedRecreateClip && ClipData.IsValid())
	{
		if (ClipData.Pin()->GetWidget() == this)//remove old clip-data
		{
			ClipDataList.Remove(ClipData.Pin());
		}
		else
		{
			ClipData = nullptr;//will create new
		}
	}
	bNeedRecreateClip = false;
	
	TSharedPtr<FDreamUIClipData> ParentClip = nullptr;
	if (Parent.IsValid())
	{
		ParentClip = Parent->ClipData.Pin();
	}
	switch (GetClipping())
	{
	case EDreamWidgetClipping::Inherit:
		this->ClipData = ParentClip;
		break;
	case EDreamWidgetClipping::ClipToBounds:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(ParentClip, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(nullptr, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::Disabled:
		this->ClipData = nullptr;
		break;
	}
	if (Visual)
	{
		Visual->CheckClipDataStartPosition();
	}
}

void UDreamWidget::UpdateVisual() const
{
	if (IsValid(Visual))
	{
		Visual->UpdateGeometry();
	}
}

void UDreamWidget::SetRenderCanvas(UDreamCanvas* InNewCanvas)
{
	auto OldRenderCanvas = RenderCanvas;
	RenderCanvas = InNewCanvas;
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)//delete old clip-data
	{
		if (OldRenderCanvas.IsValid())
		{
			OldRenderCanvas->RemoveClipData(ClipData.Pin());//remove it from old canvas
		}
	}
	if (OldRenderCanvas.IsValid())
	{
		OldRenderCanvas->RemoveDreamWidget(this);
		if (IsValid(Visual))
		{
			OldRenderCanvas->MarkVisualWillChange(Visual);
			OldRenderCanvas->UnregisterVisual(Visual);
		}
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->AddDreamWidget(this);
		bClipDirty = true;//mark it dirty so it will be added to new canvas
		if (IsValid(Visual))
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}
	// Last, with the widget already in the new canvas: the visual's clip and property data live at
	// positions in its canvas's data textures, and a visual registered with another canvas has none
	// there until it writes them again.
	OnRenderCanvasChanged(OldRenderCanvas.Get(), RenderCanvas.Get());
}

void UDreamWidget::OnHierarchyAttachmentChanged(UDreamCanvas* ParentRenderCanvas, UDreamWidget* ParentRoot)
{
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		ParentRenderCanvas = ThisRenderCanvas;
	}

	if (RenderCanvas != ParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(ParentRenderCanvas);
	}

	CheckRootWidget(ParentRoot);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->OnHierarchyAttachmentChanged(ParentRenderCanvas, ParentRoot);
		}
	}

	//flatten hierarchy index
	MarkFlattenHierarchyIndexDirty();

	{
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		
		MarkAnchorDataChanged_Recursive(false, true, true, false, false);
		MarkLayoutForRebuild(this);
	}

	Call_AttachmentChanged();
}

void UDreamWidget::OnRenderCanvasChanged(UDreamCanvas* OldCanvas, UDreamCanvas* NewCanvas)
{
	// SetRenderCanvas has already moved the widget and its visual between the canvases' lists.
	if (IsValid(Visual))
	{
		Visual->OnRenderCanvasChanged(OldCanvas, NewCanvas);
	}
}

void UDreamWidget::CalculateWidgetActive_Recursive()
{
	// MarkRebuildAllLayoutTree empties the WHOLE MapWidgetToLayoutTree, so it says nothing about which
	// widget raised it and one call covers every change this walk can make. It used to be raised from
	// inside the recursion, once per descendant whose cached value moved: deactivating a page of a few
	// hundred widgets wiped the map a few hundred times over, each wipe costing the rebuild of every
	// cached layout tree in the world. Hoisted here it is one wipe for the whole subtree. The
	// MarkLayoutForRebuild below stays per widget on purpose -- unlike the wipe it is about a specific
	// widget, dirtying the containers on its own ancestor chain, and collapsing it to the root would
	// leave nested containers inside the subtree never re-arranging.
	bool bAnyActiveChanged = false;
	struct LOCAL
	{
		static void CalculateWidgetActive(UDreamWidget* Widget, bool& bOutAnyChanged)
		{
			bool bResultActive = true;
			if (!Widget->bWidgetActive || Widget->bParked)
				bResultActive = false;
			else if (Widget->Parent.IsValid())
				bResultActive = Widget->Parent->GetWidgetActiveInHierarchy();
			else
				bResultActive = true;

			if (Widget->bCacheWidgetActiveInHierarchy != bResultActive)
			{
				Widget->bCacheWidgetActiveInHierarchy = bResultActive;
				bOutAnyChanged = true;
				//callback
				Widget->Call_WidgetActiveChanged();
				//canvas update
				Widget->MarkCanvasUpdate(true);
				//tell layout
				MarkLayoutForRebuild(Widget);
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Children can hold nulls -- garbage collection clears a reference to a widget it
				// took while this one lived, and this walk runs from OnDetachedFromParent, which is
				// exactly when a hierarchy is coming apart. Every other walk in this file guards;
				// this one did not, and dereferenced the null on its first line.
				if (IsValid(Child))
				{
					CalculateWidgetActive(Child, bOutAnyChanged);
				}
			}
		}
	};
	LOCAL::CalculateWidgetActive(this, bAnyActiveChanged);
	if (bAnyActiveChanged)
	{
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}

void UDreamWidget::CalculateVisibility_Recursive()
{
	/** The same hoist as in CalculateWidgetActive_Recursive, and for the same reason. */
	bool bAnyLayoutVisibilityChanged = false;
	struct FVisibilityCalculator
	{
		static void Calculate(UDreamWidget* Widget, bool& bOutAnyLayoutChanged)
		{
			const bool bParentLayoutVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheLayoutVisibleInHierarchy;
			const bool bParentRenderVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheRenderVisibleInHierarchy;
			const bool bParentAllowsHitTest = !Widget->Parent.IsValid() || Widget->Parent->bCacheChildrenHitTestVisibleInHierarchy;
			const bool bActive = Widget->bCacheWidgetActiveInHierarchy;

			bool bDesignerVisible = true;
#if WITH_EDITOR
			if (Widget->bHiddenInDesigner && (!Widget->GetWorld() || !Widget->GetWorld()->IsGameWorld()))
			{
				bDesignerVisible = false;
			}
#endif

			const bool bCollapsed = Widget->Visibility == EDreamWidgetVisibility::Collapsed || Widget->bLayoutVisibilitySuppressed;
			const bool bPaints = Widget->Visibility != EDreamWidgetVisibility::Hidden && !bCollapsed;
			const bool bBlocksChildren = Widget->Visibility == EDreamWidgetVisibility::HitTestInvisible;
			const bool bSelfAcceptsHit = Widget->Visibility == EDreamWidgetVisibility::Visible;

			const bool bNewLayoutVisible = bActive && bParentLayoutVisible && !bCollapsed;
			const bool bNewRenderVisible = bActive && bDesignerVisible && bParentRenderVisible && bPaints;
			const bool bNewChildrenHitTestVisible = bNewRenderVisible && bParentAllowsHitTest && !bBlocksChildren;
			const bool bNewSelfHitTestVisible = bNewChildrenHitTestVisible && bSelfAcceptsHit;

			const bool bLayoutChanged = Widget->bCacheLayoutVisibleInHierarchy != bNewLayoutVisible;
			const bool bRenderChanged = Widget->bCacheRenderVisibleInHierarchy != bNewRenderVisible;
			const bool bHitTestChanged = Widget->bCacheSelfHitTestVisibleInHierarchy != bNewSelfHitTestVisible
				|| Widget->bCacheChildrenHitTestVisibleInHierarchy != bNewChildrenHitTestVisible;

			Widget->bCacheLayoutVisibleInHierarchy = bNewLayoutVisible;
			Widget->bCacheRenderVisibleInHierarchy = bNewRenderVisible;
			Widget->bCacheSelfHitTestVisibleInHierarchy = bNewSelfHitTestVisible;
			Widget->bCacheChildrenHitTestVisibleInHierarchy = bNewChildrenHitTestVisible;

			if (bLayoutChanged)
			{
				// Cached layout trees keep collapsed subtrees and filter at update time, so the wipe this
				// raises is belt-and-braces (it is also swallowed while a layout pass is executing); the
				// layout pass itself no longer depends on it to see a revealed subtree. It is raised once
				// for the whole walk, outside the recursion, rather than once per changed descendant.
				bOutAnyLayoutChanged = true;
				UDreamWidget::MarkLayoutForRebuild(Widget->Parent.IsValid() ? Widget->Parent.Get() : Widget);
			}
			if (bRenderChanged || bHitTestChanged)
			{
				Widget->MarkCanvasUpdate(true);
			}

			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (IsValid(Child))
				{
					Calculate(Child, bOutAnyLayoutChanged);
				}
			}
		}
	};
	FVisibilityCalculator::Calculate(this, bAnyLayoutVisibilityChanged);
	if (bAnyLayoutVisibilityChanged)
	{
		if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}
void UDreamWidget::CalculateInteractable_Recursive()
{
	struct LOCAL
	{
		static void CalculateInteractable(UDreamWidget* Widget)
		{
			// The enabled switch cascades on its own terms and is folded in below. It is computed in
			// this same walk rather than in one of its own because the two answers are always read
			// together, and a second recursion over the subtree would only make it possible for them
			// to disagree for a frame.
			const bool bParentEnabled = !Widget->Parent.IsValid() || Widget->Parent->bCacheEnabledInHierarchy;
			Widget->bCacheEnabledInHierarchy = Widget->bIsEnabled && bParentEnabled;

			bool bResultInteractable = true;
			switch (Widget->Interactable)
			{
			case EDreamWidgetInteractableType::Enabled:
				bResultInteractable = true;
				break;
			case EDreamWidgetInteractableType::Disabled:
				bResultInteractable = false;
				break;
			case EDreamWidgetInteractableType::Inherit:
				if (Widget->Parent.IsValid())
					bResultInteractable = Widget->Parent->GetInteractableInHierarchy();
				else
					bResultInteractable = true;
				break;
			}

			// A disabled ancestor beats a descendant that explicitly asked to be interactable. That
			// asymmetry is the point of having both: "disable this dialog" has to reach the one button
			// inside it that was pinned Enabled, or it does not mean anything.
			bResultInteractable = bResultInteractable && Widget->bCacheEnabledInHierarchy;

			if (Widget->bCacheInteractableInHierarchy != bResultInteractable)
			{
				Widget->bCacheInteractableInHierarchy = bResultInteractable;
				Widget->Call_InteractableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateInteractable(Child);
				}
			}
		}
	};
	LOCAL::CalculateInteractable(this);
}
void UDreamWidget::CalculateRaycastable_Recursive()
{
	struct LOCAL
	{
		static void CalculateRaycastable(UDreamWidget* Widget)
		{
			bool bResult = true;
			switch (Widget->Raycastable)
			{
			case EDreamWidgetRaycastableType::Disabled:
				bResult = false;
				break;
			case EDreamWidgetRaycastableType::Enabled:
				bResult = true;
				break;
			case EDreamWidgetRaycastableType::Inherit:
				if (Widget->Parent.IsValid())
					bResult = Widget->Parent->GetRaycastableInHierarchy();
				else
					bResult = true;
				break;
			}

			if (Widget->bCacheRaycastableInHierarchy != bResult)
			{
				Widget->bCacheRaycastableInHierarchy = bResult;
				Widget->Call_RaycastableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateRaycastable(Child);
				}
			}
		}
	};
	LOCAL::CalculateRaycastable(this);
}

UDreamCanvas* UDreamWidget::GetRootCanvas()const
{
	if (RenderCanvas.IsValid())
	{
		return RenderCanvas->GetRootCanvas();
	}
	return nullptr;
}

USceneComponent* UDreamWidget::GetAttachedRootSceneComponent() const
{
	if (auto RootCanvas = GetRootCanvas())
	{
		return RootCanvas->GetAttachedRootSceneComponent();
	}
	return nullptr;
}

void UDreamWidget::MarkCanvasUpdate(bool bRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bRebuildDrawCall);
	}
}

UDreamCanvas* UDreamWidget::GetRenderCanvas()const
{
	return RenderCanvas.Get();
}

bool UDreamWidget::IsScreenSpaceOverlayUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToScreenSpace();
}
bool UDreamWidget::IsRenderTargetUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToRenderTarget();
}
bool UDreamWidget::IsWorldSpaceUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToWorldSpace();
}

void UDreamWidget::MarkClipDirty(bool InClipTypeChanged) const
{
	bClipDirty = true;
	if (InClipTypeChanged)bNeedRecreateClip = true;
	// Waking the canvas is required, not optional. UDreamCanvas::UpdateCanvasDrawCall gates everything that
	// reconciles clipping behind bCanTickUpdate: UpdateClip (creates/destroys the FDreamUIClipData),
	// UDreamVisual::CheckClipDataStartPosition (refreshes the slot index baked into vertex data) and
	// MarkFinishUpdateCanvasDrawCall (uploads the clip blocks). Marking bClipDirty without waking the canvas
	// leaves a widget sitting on Clipping == ClipToBounds with no ClipData, while its visual still points at a
	// recycled clip slot — the shader then clips against a stale rectangle and the subtree renders nothing.
	MarkCanvasUpdate(true);
	struct LOCAL
	{
		static void MarkDirty(const UDreamWidget* Widget, bool InClipTypeChanged, FDreamVisitedWidgetSet& VisitedWidgets)
		{
			if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
			{
				return;
			}
			VisitedWidgets.Add(Widget);
			switch (Widget->GetClipping())
			{
			case EDreamWidgetClipping::Inherit:
			case EDreamWidgetClipping::ClipToBounds:
				Widget->bClipDirty = true;
				if (InClipTypeChanged)Widget->bNeedRecreateClip = true;
				// Descendants may render through a different (child) canvas, so wake each one individually.
				Widget->MarkCanvasUpdate(true);
				break;
			case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
			case EDreamWidgetClipping::Disabled:
				return;
			}

			for (const UDreamWidget* Child : Widget->GetChildren())
			{
				MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
			}
		}
	};
	FDreamVisitedWidgetSet VisitedWidgets;
	VisitedWidgets.Add(this);
	for (const UDreamWidget* Child : this->GetChildren())
	{
		LOCAL::MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
	}
}
bool UDreamWidget::IsPointVisibleOnClip(const FVector& Value) const
{
	if (ClipData.IsValid())
	{
		return ClipData.Pin()->IsPointVisible(Value);
	}
	return true;
}
void UDreamWidget::SetClipping(EDreamWidgetClipping Value)
{
	if (Clipping != Value)
	{
		const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
		Clipping = Value;
		if (PreviousEffectiveClipping != GetClipping())
		{
			MarkClipDirty(true);
		}
	}
}

void UDreamWidget::SetLayoutClippingOverride(EDreamWidgetClipping Value)
{
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = true;
	LayoutClippingOverride = Value;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}

void UDreamWidget::ClearLayoutClippingOverride()
{
	if (!bHasLayoutClippingOverride)
	{
		return;
	}
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = false;
	LayoutClippingOverride = EDreamWidgetClipping::Inherit;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}
void UDreamWidget::SetClippingCornerRadius(FVector4f Value)
{
	if (ClippingCornerRadius != Value)
	{
		ClippingCornerRadius = Value;
		MarkClipDirty(false);
	}
}

void UDreamWidget::SetClippingMargin(FMargin Value)
{
	if (ClippingMargin != Value)
	{
		ClippingMargin = Value;
		MarkClipDirty(false);
	}
}

float UDreamWidget::GetFinalRenderOpacity()const
{
	if (Parent.IsValid())
	{
		return this->RenderOpacity * Parent->GetFinalRenderOpacity();
	}
	return this->RenderOpacity;
}
void UDreamWidget::SetRenderOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (RenderOpacity != Value)
	{
		RenderOpacity = Value;
		struct LOCAL
		{
			static void MarkDirty(const UDreamWidget* Widget)
			{
				//Children can hold nulls between a teardown and the next tidy-up: a tween still running
				//on the parent walks straight into one on the frame after a child is deleted.
				if (!IsValid(Widget))
				{
					return;
				}
				if (Widget->Visual)
				{
					Widget->Visual->MarkColorDirty();
				}
				for (auto& Child : Widget->Children)
				{
					MarkDirty(Child);
				}
			}
		};
		LOCAL::MarkDirty(this);
	}
}

bool UDreamWidget::GetPixelSnappingInHierarchy() const
{
	switch (this->PixelSnapping)
	{
	case EWidgetPixelSnapping::SnapToPixel:
		return true;
	case EWidgetPixelSnapping::Disabled:
		return false;
	case EWidgetPixelSnapping::Inherit:
		if (Parent.IsValid())
		{
			return Parent->GetPixelSnappingInHierarchy();
		}
		return false;
	}
	return false;
}

void UDreamWidget::SetPixelSnapping(EWidgetPixelSnapping Value)
{
	if (PixelSnapping != Value)
	{
		PixelSnapping = Value;
		struct LOCAL
		{
			static void MarkChanged(const UDreamWidget* Widget)
			{
				//Children can hold nulls between a teardown and the next tidy-up.
				if (!IsValid(Widget))
				{
					return;
				}
				if (Widget->Visual)
				{
					Widget->Visual->OnPixelSnappingChanged();
				}
				for (auto& Child : Widget->GetChildren())
				{
					MarkChanged(Child);
				}
			}
		};
		LOCAL::MarkChanged(this);
	}
}

bool UDreamWidget::GetWidgetActiveInHierarchy() const
{
	return bCacheWidgetActiveInHierarchy;
}

#if WITH_EDITOR
void UDreamWidget::SetHiddenInDesigner(bool bHidden)
{
	if (bHiddenInDesigner != bHidden)
	{
		bHiddenInDesigner = bHidden;
		CalculateVisibility_Recursive();
	}
}
#endif

void UDreamWidget::SetParked(bool Value)
{
	if (bParked != Value)
	{
		bParked = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetWidgetActive(bool Value)
{
	if (bWidgetActive != Value)
	{
		bWidgetActive = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetVisibility(EDreamWidgetVisibility Value)
{
	if (Visibility != Value)
	{
		Visibility = Value;
		CalculateVisibility_Recursive();
		OnVisibilityChanged.Broadcast(Visibility);
	}
}

void UDreamWidget::SetIsFocusable(bool Value)
{
	if (bIsFocusable == Value)
	{
		return;
	}
	bIsFocusable = Value;
	if (!bIsFocusable)
	{
		// A widget that can no longer take focus must not keep the focus it already has. ClearFocus
		// is a no-op unless this widget IS the selected one, and answers nothing at all outside a
		// live game world, so this is safe on every path a property setter can arrive from.
		ClearFocus();
	}
}

bool UDreamWidget::SetFocus(int32 UserIndex, int32 PointerId)
{
	if (!bIsFocusable || !GetRenderVisibleInHierarchy() || !GetInteractableInHierarchy())
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, true);
		EventSystem->SetSelectWidget(this, EventData);
		// The navigation cursor has to move with focus, or the next directional press starts from
		// wherever focus USED to be and appears to teleport. UDreamUINavigationStack::FocusSelectable
		// already does both halves for the same reason; this entry point only ever did the first.
		EventSystem->SetHighlightedComponentForNavigation(this, PointerId);
		return true;
	}
	return false;
}

bool UDreamWidget::HasFocus(int32 UserIndex, int32 PointerId) const
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), UserIndex))
	{
		return EventSystem->GetCurrentSelectedComponent(PointerId) == this;
	}
	return false;
}

void UDreamWidget::ClearFocus(int32 UserIndex, int32 PointerId)
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, false);
		if (EventData && EventData->SelectedComponent == this)
		{
			EventSystem->SetSelectWidget(nullptr, EventData);
		}
	}
}

void UDreamWidget::NotifyFocusReceived(int32 UserIndex, int32 PointerId)
{
	OnFocusReceived.Broadcast(UserIndex, PointerId);
	if (AccessibleBehavior != EDreamAccessibleBehavior::NotAccessible)
	{
		AnnounceAccessibleText();
	}
}

void UDreamWidget::NotifyFocusLost(int32 UserIndex, int32 PointerId)
{
	OnFocusLost.Broadcast(UserIndex, PointerId);
}

void UDreamWidget::AnnounceAccessibleText(const FText& Announcement)
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FText TextToAnnounce = Announcement;
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = AccessibleBehavior == EDreamAccessibleBehavior::Summary ? AccessibleSummaryText : AccessibleText;
	}
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = FText::FromString(DisplayName);
	}
	if (!TextToAnnounce.IsEmpty())
	{
		FSlateApplication::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(TextToAnnounce.ToString());
	}
#endif
}

int32 UDreamWidget::GetLocalPlayerIndexOf(const APlayerController* InPlayerController)
{
	// The same index UDreamEventSystem::GetPlayerController reads back, so "this widget's player" and
	// "this event system's player" cannot drift apart: the position of the local player in the game
	// instance's list, which is what UserIndex has always meant here.
	if (!IsValid(InPlayerController))
	{
		return 0;
	}
	const ULocalPlayer* LocalPlayer = InPlayerController->GetLocalPlayer();
	if (LocalPlayer == nullptr)
	{
		return 0;
	}
	const UGameInstance* GameInstance = LocalPlayer->GetGameInstance();
	if (GameInstance == nullptr)
	{
		return 0;
	}
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer);
	return Index != INDEX_NONE ? Index : 0;
}

APlayerController* UDreamWidget::GetOwningPlayer() const
{
	// Whoever hosts this widget owns it. The walk starts at the PARENT because a user widget answers
	// this for itself (its override checks an explicitly set controller first and then calls this),
	// and an ancestor user widget with no explicit owner does the same thing again from its own
	// position -- so the nearest EXPLICIT owner anywhere up the chain is the one that wins.
	for (const UDreamWidget* Ancestor = GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (const UDreamUserWidget* HostUserWidget = Cast<const UDreamUserWidget>(Ancestor))
		{
			return HostUserWidget->GetOwningPlayer();
		}
	}
	// The whole answer in a single-player game, and what UMG's CreateWidget defaults to.
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetFirstPlayerController() : nullptr;
}

ULocalPlayer* UDreamWidget::GetOwningLocalPlayer() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return IsValid(PlayerController) ? PlayerController->GetLocalPlayer() : nullptr;
}

int32 UDreamWidget::GetOwningPlayerIndex() const
{
	return GetLocalPlayerIndexOf(GetOwningPlayer());
}

UGameInstance* UDreamWidget::GetGameInstance() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetGameInstance() : nullptr;
}

bool UDreamWidget::SetKeyboardFocus()
{
	return SetFocus(GetOwningPlayerIndex());
}

bool UDreamWidget::HasKeyboardFocus() const
{
	return HasFocus(GetOwningPlayerIndex());
}

bool UDreamWidget::SetUserFocus(APlayerController* InPlayerController)
{
	return SetFocus(GetLocalPlayerIndexOf(InPlayerController));
}

bool UDreamWidget::HasUserFocus(APlayerController* InPlayerController) const
{
	return HasFocus(GetLocalPlayerIndexOf(InPlayerController));
}

void UDreamWidget::ClearKeyboardFocus()
{
	ClearFocus(GetOwningPlayerIndex());
}

int32 UDreamWidget::GetLocalPlayerCountForQueries() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	// At least one: outside a game instance -- a test world, the designer preview -- user index 0 is
	// still a meaningful thing to ask about, and answering "no players" would make every one of the
	// any-user queries below silently false.
	return GameInstance != nullptr ? FMath::Max(GameInstance->GetNumLocalPlayers(), 1) : 1;
}

bool UDreamWidget::HasAnyUserFocus() const
{
	const int32 PlayerCount = GetLocalPlayerCountForQueries();
	for (int32 UserIndex = 0; UserIndex < PlayerCount; ++UserIndex)
	{
		if (HasFocus(UserIndex))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasFocusedDescendantForUser(int32 InUserIndex) const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		UDreamWidget* Focused = EventSystem->GetCurrentSelectedComponent(Entry.Key);
		// Descendants, not "this or its descendants" -- UMG draws the same line, and a widget asking
		// whether something INSIDE it has focus already knows whether it has focus itself.
		if (IsValid(Focused) && Focused != this && Focused->IsChildOf(this))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasFocusedDescendants() const
{
	const int32 PlayerCount = GetLocalPlayerCountForQueries();
	for (int32 UserIndex = 0; UserIndex < PlayerCount; ++UserIndex)
	{
		if (HasFocusedDescendantForUser(UserIndex))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasUserFocusedDescendants(APlayerController* InPlayerController) const
{
	return HasFocusedDescendantForUser(GetLocalPlayerIndexOf(InPlayerController));
}

bool UDreamWidget::IsHovered() const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), GetOwningPlayerIndex());
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		if (PointerEvent->EnterWidget.Get() == this)
		{
			return true;
		}
		// The enter STACK as well, so a button still reads as hovered while the pointer is over its
		// own label. Slate gets that for free because hover propagates to parents; here the stack is
		// where that fact lives.
		for (const TObjectPtr<UDreamWidget>& Entered : PointerEvent->EnterWidgetStack)
		{
			if (Entered.Get() == this)
			{
				return true;
			}
		}
	}
	return false;
}

bool UDreamWidget::HasMouseCapture() const
{
	return HasMouseCaptureByUser(GetOwningPlayerIndex(), INDEX_NONE);
}

bool UDreamWidget::HasMouseCaptureByUser(int32 InUserIndex, int32 InPointerIndex) const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		if (InPointerIndex >= 0 && Entry.Key != InPointerIndex)
		{
			continue;
		}
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		// Held down AND pressed on this widget: that pointer's drag and its release go here whatever
		// it travels over in between, which is the whole of what capture buys a caller.
		if (PointerEvent->bNowIsTriggerPressed && PointerEvent->PressWidget.Get() == this)
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::IsRendered() const
{
	return bCacheRenderVisibleInHierarchy && GetFinalRenderOpacity() > UE_KINDA_SMALL_NUMBER;
}

bool UDreamWidget::IsInViewport() const
{
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	if (ScreenUI == nullptr)
	{
		return false;
	}
	// Walk up: the subsystem tracks PAGES, and the thing a caller means by "am I on screen" is
	// usually a widget several levels inside one.
	for (const UDreamWidget* Ancestor = this; Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (ScreenUI->IsInViewport(const_cast<UDreamWidget*>(Ancestor)))
		{
			return true;
		}
	}
	return false;
}

void UDreamWidget::SetRaycastable(EDreamWidgetRaycastableType Value)
{
	if (Raycastable != Value)
	{
		Raycastable = Value;
		CalculateRaycastable_Recursive();
	}
}

void UDreamWidget::SetInteractable(EDreamWidgetInteractableType Value)
{
	if (Interactable != Value)
	{
		Interactable = Value;
		CalculateInteractable_Recursive();
	}
}

void UDreamWidget::SetIsEnabled(bool bInIsEnabled)
{
	if (bIsEnabled == bInIsEnabled)
	{
		return;
	}
	bIsEnabled = bInIsEnabled;
	// One walk does both halves: it recomputes the enabled cascade and the interactable answer that
	// input, navigation and every control's disabled look are already reading.
	CalculateInteractable_Recursive();
}

UDreamWidgetNavigation* UDreamWidget::GetNavigation() const
{
	return GetComponent<UDreamWidgetNavigation>();
}

UDreamWidgetNavigation* UDreamWidget::GetOrCreateNavigation()
{
	if (UDreamWidgetNavigation* Existing = GetComponent<UDreamWidgetNavigation>())
	{
		return Existing;
	}
	return Cast<UDreamWidgetNavigation>(AddComponent(UDreamWidgetNavigation::StaticClass()));
}

const UDreamWidget* UDreamWidget::GetRestrictNavigationAreaWidget() const
{
	if (bRestrictNavigationArea)
	{
		return this;
	}
	if (Parent.IsValid())
	{
		return Parent->GetRestrictNavigationAreaWidget();
	}
	return nullptr;
}

void UDreamWidget::SetRestrictNavigationArea(bool Value)
{
	bRestrictNavigationArea = Value;
}

void UDreamWidget::SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule Value)
{
	NavigationBoundaryRule = Value;
}
