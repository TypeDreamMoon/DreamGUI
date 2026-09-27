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
