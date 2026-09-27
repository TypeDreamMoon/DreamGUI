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


void UDreamWidget::CalculateAnchorFromTransform()
{
	auto TempRelativeLocation = this->GetRelativeLocation();
	FVector2D CalculatedAnchoredPosition;
	if (Parent.IsValid())
	{
		//just a reverse operation from CalculateTransformFromAnchor
		float LocalLeftPoint =
			Parent->GetLocalSpaceLeft()
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);

		float LocalBottomPoint =
			Parent->GetLocalSpaceBottom()
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);

		CalculatedAnchoredPosition.X = TempRelativeLocation.Y
			- LocalLeftPoint
			- +(Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X)) * this->AnchorData.Pivot.X;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z
			- LocalBottomPoint
			- (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y)) * this->AnchorData.Pivot.Y;
	}
	else
	{
		CalculatedAnchoredPosition.X = TempRelativeLocation.Y;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z;
	}

	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	if (AnchorData.AnchoredPosition != CalculatedAnchoredPosition)
	{
		AnchorData.AnchoredPosition = CalculatedAnchoredPosition;
	}
}
void UDreamWidget::CalculateTransformFromAnchor()
{
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);
}
void UDreamWidget::CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged)
{
	bCanSetAnchorFromTransform = false;
	FVector ResultLocation = this->GetRelativeLocation();
	if (Parent.IsValid())
	{
		float LocalLeftPoint = //this left point anchor position in parent's space
			Parent->GetLocalSpaceLeft()//parent's left position
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);//add anchor offset
		float LocalLeftPivotPoint = //to pivot point, with anchor offset
			LocalLeftPoint
			+ (Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.X
			+ this->AnchorData.AnchoredPosition.X;

		float LocalBottomPoint = //this bottom point anchor position in parent's space
			Parent->GetLocalSpaceBottom()//parent's bottom position
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);//add anchor offset
		float LocalBottomPivotPoint = //to pivot point, with anchor offset
			LocalBottomPoint
			+ (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.Y
			+ this->AnchorData.AnchoredPosition.Y;

		ResultLocation.Y = LocalLeftPivotPoint;
		ResultLocation.Z = LocalBottomPivotPoint;
	}
	else
	{
		ResultLocation.Y = this->AnchorData.AnchoredPosition.X;
		ResultLocation.Z = this->AnchorData.AnchoredPosition.Y;
	}

	auto OriginRelativeLocation = this->GetRelativeLocation();
	double Tolerance = 0.0f;
	if (FMath::Abs(OriginRelativeLocation.Y - ResultLocation.Y) > Tolerance)
	{
		OutHorizontalPositionChanged = true;
	}
	if (FMath::Abs(OriginRelativeLocation.Z - ResultLocation.Z) > Tolerance)
	{
		OutVerticalPositionChanged = true;
	}
	if (OutHorizontalPositionChanged || OutVerticalPositionChanged)
	{
		this->SetRelativeLocation(ResultLocation);
	}
	bCanSetAnchorFromTransform = true;
}

#pragma region AnchorData

void UDreamWidget::SyncAnimatableGeometryMirrors() const
{
	auto* MutableThis = const_cast<UDreamWidget*>(this);
	MutableThis->AnimatableWidth = CacheWidth;
	MutableThis->AnimatableHeight = CacheHeight;
	MutableThis->AnimatableAnchorLeft = CacheAnchorOffsetLeft;
	MutableThis->AnimatableAnchorRight = CacheAnchorOffsetRight;
	MutableThis->AnimatableAnchorTop = CacheAnchorOffsetTop;
	MutableThis->AnimatableAnchorBottom = CacheAnchorOffsetBottom;
}

float UDreamWidget::GetWidth() const
{
	if (bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		// The backstop for the world-rect cache. MarkDimensionChanged covers the announced paths, but
		// several do not announce -- MarkAllDirty simply dirties this flag, and a stretched child
		// resolves against its parent lazily, here, long after the parent moved. Whatever reached this
		// branch is a width nobody had, so the sphere built from the old one is gone too.
		MarkWorldRectBoundsDirty();
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				CacheWidth = AnchorData.SizeDelta.X + Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
			}
			else
			{
				CacheWidth = AnchorData.SizeDelta.X;
			}
		}
		else
		{
			CacheWidth = AnchorData.SizeDelta.X;
		}
	}
	// A resolved size is never negative. Offsets that cross -- a stretched child whose left and right
	// padding together exceed the parent's span, or a plainly authored negative -- describe an EMPTY
	// rect, not an inverted one, and every consumer here multiplies the width straight through:
	// GetLocalSpaceLeft is -W*Pivot and GetLocalSpaceRight is W*(1-Pivot), so a negative W swaps the two
	// edges and turns the hit rect inside out, while GetWorldRectBoundingSphere's W*W + H*H keeps
	// reporting a healthy positive radius over it. Panels already floor their own output
	// (DreamPanelLayoutLocal::CleanSize); this is that floor for every other path into the tree.
	// AnchorData keeps the true, possibly negative, SizeDelta -- only the resolved answer is floored, so
	// anchor offsets still round-trip exactly.
	return FMath::Max(0.0f, CacheWidth);
}
float UDreamWidget::GetHeight() const
{
	if (bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		// The vertical twin of the backstop in GetWidth.
		MarkWorldRectBoundsDirty();
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				CacheHeight = AnchorData.SizeDelta.Y + Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);
			}
			else
			{
				CacheHeight = AnchorData.SizeDelta.Y;
			}
		}
		else
		{
			CacheHeight = AnchorData.SizeDelta.Y;
		}
	}
	/** The vertical twin of the floor in GetWidth. */
	return FMath::Max(0.0f, CacheHeight);
}

void UDreamWidget::SetAnchorData(const FDreamUIAnchorData& Value)
{
	AnchorData.Pivot = Value.Pivot;
	AnchorData.AnchorMin = Value.AnchorMin;
	AnchorData.AnchorMax = Value.AnchorMax;
	AnchorData.AnchoredPosition = Value.AnchoredPosition;
	AnchorData.SizeDelta = Value.SizeDelta;

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	MarkAnchorDataChanged_Recursive(true, true, true, false);
	MarkLayoutForRebuild(this);
}

void UDreamWidget::SetPivot(FVector2D Value) 
{
	if (!AnchorData.Pivot.Equals(Value, 0.0f))
	{
		AnchorData.Pivot = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(true, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetAnchorMin(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(Value, 0.0f))
		{
			AnchorData.AnchorMin = Value;
			// Moving an anchor line moves the reference the rect is expressed against; it does not move
			// the rect relative to that reference. Both anchor offsets are functions of AnchoredPosition,
			// SizeDelta and Pivot alone (GetAnchorOffsetLeft/Right never read an anchor), so keeping them
			// costs nothing and the arithmetic that used to stand here was an identity: -Right - Left IS
			// SizeDelta.X, and CurrentLeft + SizeDelta.X * Pivot.X IS AnchoredPosition.X. Its one net
			// effect was to publish the size DELTA as the RESOLVED width and leave bCacheWidthDirty
			// clear, so a point-anchored widget turned stretched by this setter answered GetWidth() with
			// the delta until some unrelated path happened to dirty the cache.
			//
			// What genuinely changes is the resolved size: a stretched axis resolves to the delta PLUS
			// the parent's span across the anchors, and that span just moved. Hence InDiscardCache=true,
			// which is exactly how the 2026-09-01 fixes to SetWidth/SetHeight/SetAnchoredPositionAndSizeDelta
			// spell the same thing; these two setters were the ones that pass missed.
			MarkAnchorDataChanged_Recursive(false, true, true, true);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetAnchorMax(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMax.Equals(Value, 0.0f))
		{
			AnchorData.AnchorMax = Value;
			// The AnchorMax twin of SetAnchorMin, defect included and removed the same way.
			MarkAnchorDataChanged_Recursive(false, true, true, true);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

/**
 * The four-in-one form of SetAnchorOffsetLeft/Right/Top/Bottom, and now written the same way they are.
 *
 * Three separate defects lived in the old shape. CacheWidth was assigned `-Right - Left`, which is the
 * size DELTA and not the resolved width (they differ by the parent's span on a stretched axis), and no
 * branch in the function touched bCacheWidthDirty -- so DreamTextInput's `SetHorizontalAndVerticalAnchorMinMax
 * (stretch) + SetAnchorOffset(Padding)` pair left its clip and placeholder nodes answering GetWidth()
 * with the negated padding sum. The change test compared possibly-stale caches without first asking
 * whether they were dirty. And the early-exit tail cleared bCacheAnchorOffsetLeftDirty alone, leaving
 * Left holding a freshly written value while Right/Top/Bottom were still owed a resolve against
 * AnchorData -- a set the next getter would resolve into something inconsistent with it.
 *
 * Here the offsets are the authored intent, the resolved size follows from them plus the parent's span,
 * and the delta is what is left of that size once the span is subtracted (stretched axes only). Every
 * cache written is a value this function actually computed, so InDiscardCache stays false, exactly as
 * in the four single-axis setters.
 */
void UDreamWidget::SetAnchorOffset(FMargin Value)
{
	if (this->Parent.IsValid())
	{
		const bool bWidthChange = bCacheAnchorOffsetLeftDirty || bCacheAnchorOffsetRightDirty
			|| CacheAnchorOffsetLeft != Value.Left || CacheAnchorOffsetRight != Value.Right;
		const bool bHeightChange = bCacheAnchorOffsetBottomDirty || bCacheAnchorOffsetTopDirty
			|| CacheAnchorOffsetBottom != Value.Bottom || CacheAnchorOffsetTop != Value.Top;
		if (!bWidthChange && !bHeightChange)
		{
			return;//every offset already says what was asked for, and none of them is owed a resolve
		}

		const float ParentSpanX = this->Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
		const float ParentSpanY = this->Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);

		CacheAnchorOffsetLeft = Value.Left;
		CacheAnchorOffsetRight = Value.Right;
		CacheAnchorOffsetBottom = Value.Bottom;
		CacheAnchorOffsetTop = Value.Top;
		bCacheAnchorOffsetLeftDirty = false;
		bCacheAnchorOffsetRightDirty = false;
		bCacheAnchorOffsetBottomDirty = false;
		bCacheAnchorOffsetTopDirty = false;

		CacheWidth = ParentSpanX - Value.Right - Value.Left;
		bCacheWidthDirty = false;
		AnchorData.SizeDelta.X = AnchorData.IsHorizontalStretched() ? CacheWidth - ParentSpanX : CacheWidth;
		AnchorData.AnchoredPosition.X = FMath::Lerp(Value.Left, -Value.Right, AnchorData.Pivot.X);

		CacheHeight = ParentSpanY - Value.Top - Value.Bottom;
		bCacheHeightDirty = false;
		AnchorData.SizeDelta.Y = AnchorData.IsVerticalStretched() ? CacheHeight - ParentSpanY : CacheHeight;
		AnchorData.AnchoredPosition.Y = FMath::Lerp(Value.Bottom, -Value.Top, AnchorData.Pivot.Y);

		SyncAnimatableGeometryMirrors();
		MarkAnchorDataChanged_Recursive(false, bWidthChange, bHeightChange, false);
		MarkLayoutForRebuild(this);
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void UDreamWidget::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(MinValue, 0.0f) || !AnchorData.AnchorMax.Equals(MaxValue, 0.0f))
		{
			auto PrevRelativeLocation = this->GetRelativeLocation();
			auto PrevWidth = this->GetWidth();
			auto PrevHeight = this->GetHeight();
			this->SetAnchorMin(MinValue);
			this->SetAnchorMax(MaxValue);
			if (bKeepSize)
			{
				this->SetWidth(PrevWidth);
				this->SetHeight(PrevHeight);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.X != Value.X || AnchorData.AnchorMax.X != Value.Y)
		{
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			auto CurrentRight = this->GetAnchorOffsetRight();

			if (bKeepSize)
			{
				CacheWidth = this->GetWidth();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.X = Value.X;
			AnchorData.AnchorMax.X = Value.Y;

			//SetAnchorLeft & SetAnchorRight
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				}
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.Y != Value.X || AnchorData.AnchorMax.Y != Value.Y)
		{
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			auto CurrentTop = this->GetAnchorOffsetTop();

			if (bKeepSize)
			{
				CacheHeight = this->GetHeight();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.Y = Value.X;
			AnchorData.AnchorMax.Y = Value.Y;

			//SetAnchorBottom && SetAnchorTop
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				}
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetAnchoredPosition(FVector2D Value)
{
	if (!AnchorData.AnchoredPosition.Equals(Value, 0.0f))
	{
		AnchorData.AnchoredPosition = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}
void UDreamWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetSizeDelta(FVector2D Value)
{
	if (!AnchorData.SizeDelta.Equals(Value, 0.0f))
	{
		AnchorData.SizeDelta = Value;
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, true, true, false);
		MarkLayoutForRebuild(this);
	}
}

void UDreamWidget::SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size)
{
	bool bPosChange = false, bSizeChange = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		bPosChange = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		bSizeChange = true;
		AnchorData.SizeDelta = Size;
		// DIRTIED, never assigned. A size DELTA is only the resolved size on a point-anchored axis;
		// on a stretched one the size is the delta PLUS the parent's span across the anchors, and
		// writing the delta into the resolved cache published a number no arithmetic in this class
		// agrees with. That is where the list's "-0 wide viewport inside a 342-wide face" came from:
		// RefreshScrollFurniture states the gutter as a delta (0, or -Thickness) on a stretched
		// viewport, and this line then answered every GetWidth() with it. SetSizeDelta next door has
		// always dirtied instead; the two are the same operation and now say the same thing.
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
	}
	if (bPosChange || bSizeChange)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, bSizeChange, bSizeChange, false);
		MarkLayoutForRebuild(this);
	}
}

float UDreamWidget::GetAnchorOffsetLeft()const
{
	if (bCacheAnchorOffsetLeftDirty)
	{
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetLeft = this->AnchorData.AnchoredPosition.X - this->AnchorData.SizeDelta.X * this->AnchorData.Pivot.X;
		}
		else
		{
			CacheAnchorOffsetLeft = this->GetLocalSpaceLeft();//local space left
		}
	}
	return CacheAnchorOffsetLeft;
}
float UDreamWidget::GetAnchorOffsetTop()const
{
	if (bCacheAnchorOffsetTopDirty)
	{
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetTop = -(this->AnchorData.AnchoredPosition.Y + this->AnchorData.SizeDelta.Y * (1.0f - this->AnchorData.Pivot.Y));
		}
		else
		{
			CacheAnchorOffsetTop = this->GetLocalSpaceTop();
		}
	}
	return CacheAnchorOffsetTop;
}
float UDreamWidget::GetAnchorOffsetRight()const
{
	if (bCacheAnchorOffsetRightDirty)
	{
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetRight = -(this->AnchorData.AnchoredPosition.X + this->AnchorData.SizeDelta.X * (1.0f - this->AnchorData.Pivot.X));
		}
		else
		{
			CacheAnchorOffsetRight = this->GetLocalSpaceRight();
		}
	}
	return CacheAnchorOffsetRight;
}
float UDreamWidget::GetAnchorOffsetBottom()const
{
	if (bCacheAnchorOffsetBottomDirty)
	{
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetBottom = this->AnchorData.AnchoredPosition.Y - this->AnchorData.SizeDelta.Y * this->AnchorData.Pivot.Y;
		}
		else
		{
			CacheAnchorOffsetBottom = this->GetLocalSpaceBottom();
		}
	}
	return CacheAnchorOffsetBottom;
}

FMargin UDreamWidget::GetAnchorOffset() const
{
	return FMargin(
		this->GetAnchorOffsetLeft(),
		this->GetAnchorOffsetTop(),
		this->GetAnchorOffsetRight(),
		this->GetAnchorOffsetBottom()
	);
}

void UDreamWidget::SetAnchorOffsetLeft(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetLeft != Value || bCacheAnchorOffsetLeftDirty)
		{
			bCacheAnchorOffsetLeftDirty = false;
			CacheAnchorOffsetLeft = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentRight = this->GetAnchorOffsetRight();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - Value;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(Value, -CurrentRight, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetTop(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetTop != Value || bCacheAnchorOffsetTopDirty)
		{
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetTop = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - Value - CurrentBottom;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -Value, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetRight(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetRight != Value || bCacheAnchorOffsetRightDirty)
		{
			bCacheAnchorOffsetRightDirty = false;
			CacheAnchorOffsetRight = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - Value - CurrentLeft;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -Value, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetBottom(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetBottom != Value || bCacheAnchorOffsetBottomDirty)
		{
			bCacheAnchorOffsetBottomDirty = false;
			CacheAnchorOffsetBottom = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentTop = this->GetAnchorOffsetTop();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - Value;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(Value, -CurrentTop, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

/**
 * The gate is the RESOLVED width, and so is the invalidation. Both used to be the size DELTA, and
 * that is one defect rather than two spellings of the same one.
 *
 * A stretched node's width is its delta PLUS the parent's span across its anchors, so the delta can
 * sit still while the width moves -- which is the ordinary case, not an exotic one: a node authored
 * as "exactly the parent's span" keeps a delta of 0 from the moment it is built to the moment the
 * window is resized. The old shape assigned the new width into this node's own cache and then
 * declined to tell anybody, because the delta had not changed; every stretched descendant went on
 * answering with the number it resolved when the parent was still zero-sized. Measured as a list
 * whose viewport read back -0 inside a 342-wide face, worked around at the control layer by
 * re-publishing anchors from a per-frame watch, and the watch then oscillated in the designer.
 *
 * GetWidth() first, rather than comparing against a possibly-stale cache: it resolves the stretch
 * and clears the dirty flag, so "did the width change" is asked of the width and not of a leftover.
 * When the answer is no this returns having done nothing, which is the same contract as before.
 */
void UDreamWidget::SetWidth(float Value)
{
	// The argument is a RESOLVED width, and a resolved width has no negative branch -- see the floor in
	// GetWidth(). Flooring the authored intent as well keeps the stored SizeDelta and the answer the
	// getter gives consistent instead of silently diverging by the negative part.
	Value = FMath::Max(0.0f, Value);
	if (GetWidth() == Value)
	{
		return;
	}
	CacheWidth = Value;
	bCacheWidthDirty = false;
	SyncAnimatableGeometryMirrors();
	AnchorData.SizeDelta.X = (Parent.IsValid() && AnchorData.IsHorizontalStretched())
		? Value - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X))
		: Value;
	MarkAnchorDataChanged_Recursive(false, true, false, false);
	MarkLayoutForRebuild(this);
}
/** The vertical twin of SetWidth, defect included and fixed the same way. */
void UDreamWidget::SetHeight(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (GetHeight() == Value)
	{
		return;
	}
	CacheHeight = Value;
	bCacheHeightDirty = false;
	SyncAnimatableGeometryMirrors();
	AnchorData.SizeDelta.Y = (Parent.IsValid() && AnchorData.IsVerticalStretched())
		? Value - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y))
		: Value;
	MarkAnchorDataChanged_Recursive(false, false, true, false);
	MarkLayoutForRebuild(this);
}

#pragma endregion

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

void UDreamWidget::UpdateLayout()
{
	// Everything written from here down is layout output, not something anybody asked for; see
	// IsLayoutWriting.
	++LayoutPassDepth;
	ON_SCOPE_EXIT{ --LayoutPassDepth; };
	// Both halves are gated on the dirty flag now. The container half always was (BeginLayoutPass inside
	// CalculateLayout); the LayoutSelf half read nothing and re-solved on every pass for every widget
	// carrying one. See UDreamLayoutSelf::BeginLayoutPass.
	if (IsValid(LayoutSelf) && LayoutSelf->BeginLayoutPass())
	{
		LayoutSelf->CalculateSize();
	}
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->CalculateLayout();
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

void UDreamWidget::ForceUpdateLayout()
{
	MarkWidgetLayoutDirty();
	UpdateLayout();
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

FVector2D UDreamWidget::GetLocalSpaceLeftBottomPoint()const
{
	FVector2D leftBottomPoint;
	leftBottomPoint.X = GetWidth() * -AnchorData.Pivot.X;
	leftBottomPoint.Y = GetHeight() * -AnchorData.Pivot.Y;
	return leftBottomPoint;
}
FVector2D UDreamWidget::GetLocalSpaceRightTopPoint()const
{
	FVector2D rightTopPoint;
	rightTopPoint.X = GetWidth() * (1.0f - AnchorData.Pivot.X);
	rightTopPoint.Y = GetHeight() * (1.0f - AnchorData.Pivot.Y);
	return rightTopPoint;
}
FVector2D UDreamWidget::GetLocalSpaceCenter()const
{
	return FVector2D(this->GetWidth() * (0.5f - AnchorData.Pivot.X), this->GetHeight() * (0.5f - AnchorData.Pivot.Y));
}

float UDreamWidget::GetLocalSpaceLeft()const
{
	return this->GetWidth() * -AnchorData.Pivot.X;
}
float UDreamWidget::GetLocalSpaceRight()const
{
	return this->GetWidth() * (1.0f - AnchorData.Pivot.X);
}
float UDreamWidget::GetLocalSpaceBottom()const
{
	return this->GetHeight() * -AnchorData.Pivot.Y;
}
float UDreamWidget::GetLocalSpaceTop()const
{
	return this->GetHeight() * (1.0f - AnchorData.Pivot.Y);
}

void UDreamWidget::MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	// Unconditional, and pointedly not gated on the three flags: they say what the CALLER believes
	// changed, and several callers pass a size change through as false because the value they diffed
	// was an anchor offset. A cache that trusted them would go stale on exactly those paths.
	MarkWorldRectBoundsDirty();
	// A render transform turns about a pivot POINT, and that point is resolved from the current size:
	// GetLocalSpaceLeft() + GetWidth() * RenderTransformPivot.X. So a resize moves the point, which moves
	// the bracketed transform, which moves ObjectToWorldTransform -- and nothing else here recomputes it.
	// CalculateTransformFromAnchor only writes RelativeLocation, and for a point-anchored widget a pure
	// resize leaves that untouched, so SetRelativeLocation early-outs and the transform cascade never
	// runs: the widget goes on being drawn (and hit-tested, GetWorldRectBoundingSphere reads the same
	// matrix) about the pivot point of its OLD size, off by (I - ScaleAndRotate) * (P_new - P_old).
	// A card that hover-scales while its text changes width, a spinner resized by a SizeBox mid-rotation.
	// Centre pivots are immune (the point is the origin) and so are pure translations, which is why this
	// survived; the bit test keeps every widget without a render transform on the old path.
	if (bHasRenderTransform && (InPivotChanged || InWidthChanged || InHeightChanged))
	{
		// Propagating is required rather than tidy: descendants compose their world transform from this
		// one, and a descendant whose own relative location did not change would otherwise keep a world
		// transform built from the pre-resize parent.
		CalculateObjectToWorldTransform(true);
	}
	// No clip invalidation here: clip rectangles are recomputed and diffed every tick from the owner's world
	// transform (see FDreamUIClipData::UpdateData), so there is nothing to mark.
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (GetLayoutSelf())
	{
		LayoutSelf->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (IsValid(Visual))
	{
		Visual->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(InPivotChanged || InWidthChanged || InHeightChanged);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_DimensionsChanged(InPivotChanged, InWidthChanged, InHeightChanged);
}

void UDreamWidget::MarkTransformChanged()
{
	// UpdateObjectToWorldTransform is the ONLY writer of ObjectToWorldTransform and it ends here, so
	// this one line covers every move -- including the cascade CalculateObjectToWorldTransform sends
	// down, which reaches each descendant through its own UpdateObjectToWorldTransform. That is what
	// makes the cached sphere never staler than the transform it is derived from.
	MarkWorldRectBoundsDirty();
	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(true);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			//This is mainly to mark DreamGUICanvas's bIsViewProjectionMatrixDirty to true.
			//For the condition DreamGUI_Tutorials/Tutorials/UIRenderTarget, when move DreamGUIRenderTarget at runtime, the DreamGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_TransformChanged();
}

void UDreamWidget::MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	// A size change that did not come out of a layout pass is a new authored intent, so the panel slot's
	// measurement snapshot has to follow it. Without this the snapshot froze at whatever the widget
	// measured when its slot was first registered - nothing outside the editor and prefab paths ever
	// re-captured it - so the next pass measured the child from that stale value and wrote the old size
	// straight back. A runtime SetWidth, or UDreamSpriteBase::SetSprite swapping in art of a different size,
	// visibly flashed and snapped back. This funnel only ever runs on the widget the setter was called on:
	// the recursion below hands children to the by-layout-container twin instead.
	if ((InWidthChanged || InHeightChanged) && !IsLayoutWriting() && IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredDesiredSizeFromWidget();
	}

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChanged_Recursive(false, ChildWidthChange, ChildHeightChange);

		//check if child need layout rebuild, the widget self is already marked outside of this function
		if (ChildWidthChange || ChildHeightChange//parent size change may cause child layout change
			|| ((InWidthChanged || InHeightChanged) && Child->GetLayoutSelf())//parent size changed and parent can affect child layout, need calculate child layout
			)
		{
			MarkLayoutForRebuild(Child);
		}
	}
}

void UDreamWidget::MarkCanvasUpdate(bool bRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bRebuildDrawCall);
	}
}

void UDreamWidget::SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size)
{
	bool AnyChanged = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnyChanged = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		AnyChanged = true;
		// No CacheWidth/CacheHeight assignment: the argument is a size DELTA, the caches hold RESOLVED
		// sizes, and the two are the same number only on a point-anchored axis. The assignment that used
		// to stand here was dead rather than wrong -- the InDiscardCache=true below dirties both flags a
		// few lines later -- but it is the exact shape of the defect fixed in SetWidth and
		// SetAnchoredPositionAndSizeDelta, and it was there to be copied.
		AnchorData.SizeDelta = Size;
	}
	if (AnyChanged)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetPositionForLayoutAnimation(FVector2D Position)
{
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnchorData.AnchoredPosition = Position;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetSizeForLayoutAnimation(FVector2D Size)
{
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		/** Same as in SetPositionAndSizeForLayoutAnimation: a delta is not a resolved size. */
		AnchorData.SizeDelta = Size;

		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged,
                                                                  bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChangedByLayoutContainer_Recursive(false, ChildWidthChange, ChildHeightChange);
	}
}

float UDreamWidget::GetLayoutProperty(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
                                    TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
                                    TFunctionRef<float(UDreamVisual*)> GetVisualProperty,
                                    float DefaultValue)const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Value;
		}
	}
	return DefaultValue;
}
UObject* UDreamWidget::GetLayoutSource(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
	TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
	TFunctionRef<float(UDreamVisual*)> GetVisualProperty) const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return LayoutSelf;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return LayoutContainer;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Visual;
		}
	}
	return nullptr;
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

TArray<UDreamWidget*> UDreamWidget::LayoutWriterStack;
int32 UDreamWidget::LayoutPassDepth = 0;

UDreamWidget::FLayoutWriteScope::FLayoutWriteScope(UDreamWidget* InLayoutWidget)
{
	if (IsValid(InLayoutWidget))
	{
		LayoutWriterStack.Push(InLayoutWidget);
		bPushed = true;
	}
}

UDreamWidget::FLayoutWriteScope::~FLayoutWriteScope()
{
	if (bPushed)
	{
		LayoutWriterStack.Pop(EAllowShrinking::No);
	}
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget)
{
	MarkLayoutForRebuild(InWidget, EDreamLayoutInvalidation::Measure);
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget, EDreamLayoutInvalidation Reason)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	static IConsoleVariable* LayoutTraceCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("dreamgui.LayoutTrace"));
	if (LayoutTraceCVar && LayoutTraceCVar->GetInt() != 0)
	{
		UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace] invalidate %s reason=%s registered=%d"),
			*GetNameSafe(InWidget),
			Reason == EDreamLayoutInvalidation::Arrange ? TEXT("Arrange") : TEXT("Measure"),
			InWidget->HasRegistered() ? 1 : 0);
	}

	if (Reason == EDreamLayoutInvalidation::Arrange)
	{
		// Nothing above the parent can come out differently: a panel measures its children by desired
		// size and never by where they sit, so a widget that only moved leaves every preferred size on
		// the chain exactly as it was. The parent still re-arranges, which is what keeps a panel-managed
		// child from staying where it was put - the visible behaviour is unchanged, only the reach is.
		UDreamWidget* ParentWidget = InWidget->GetParent();
		if (!IsValid(ParentWidget))
		{
			return;
		}
		UDreamLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer();
		if (!IsValid(ParentLayout))
		{
			return;
		}
		// A writer applying its own arrangement must not be re-dirtied by it, same as the walk below.
		if (LayoutWriterStack.Contains(ParentWidget))
		{
			return;
		}
		ParentLayout->MarkLayoutDirty();
		if (UDreamLayoutSelf* LayoutSelf = InWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
		}
		ParentWidget->MarkWidgetLayoutDirty();
		return;
	}

	UDreamWidget* TargetWidget = InWidget;
	UDreamWidget* RebuildRoot = nullptr;
	bool bStoppedAtLayoutWriter = false;
	FDreamVisitedWidgetSet VisitedWidgets;
	// Desired-size dependencies may cross plain wrapper widgets, so dirty every layout on the ancestor chain.
	while (IsValid(TargetWidget) && !VisitedWidgets.Contains(TargetWidget))
	{
		// A layout that is applying its own results must not be re-dirtied by them. Stop the walk at the
		// writer: everything below it still gets dirtied, because a nested container does have to react to
		// the size it was just handed, but the writer and its ancestors keep the dirty state they already
		// consumed. Widgets outside the writer's subtree never reach this branch and behave as before.
		if (LayoutWriterStack.Contains(TargetWidget))
		{
			bStoppedAtLayoutWriter = true;
			break;
		}
		VisitedWidgets.Add(TargetWidget);
		if (UDreamLayoutContainer* LayoutContainer = TargetWidget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			LayoutContainer->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (UDreamLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (TargetWidget->GetIgnoreLayout())
		{
			break;
		}

		// No relayout boundary here, and it is not an oversight. Blink stops this walk at a box whose
		// size cannot be affected from inside it, and the obvious translation - stop where the parent
		// panel sizes the child by Fill - was written, measured and reverted.
		//
		// It does not hold. A panel's MeasureLayout sums its children's desired sizes even for children
		// it sizes by Fill, so the panel's preferred size still moves when their content does. The
		// tempting rescue is that GetLayoutPreferredSize recomputes on every call and caches nothing, so
		// there is no stale value anywhere - true, and irrelevant: the ancestor still has to RUN to
		// consume the new one, and stopping the walk is precisely what stops it running. Three text
		// invalidation tests went to zero reflows and the convergence test went from one pass to two.
		//
		// A sound version has to establish that no ancestor's size derives from this subtree at all,
		// which is a walk to the root - the walk being avoided. Cacheable per widget, invalidated on
		// slot and hierarchy changes; that is a design, not a condition.
		TargetWidget = TargetWidget->GetParent();
	}
	if (bStoppedAtLayoutWriter)
	{
		// The pass that is running collected the writer's whole subtree and visits it in pre-order, so any
		// descendant dirtied above is still ahead of the cursor. Enqueuing it would only buy a second pass.
		return;
	}
	if (!IsValid(RebuildRoot))
	{
		RebuildRoot = InWidget;
	}
	if (IsValid(RebuildRoot))
	{
		RebuildRoot->MarkWidgetLayoutDirty();
	}
}

void UDreamWidget::RebuildLayoutImmediately(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWidget->GetWorld()))
	{
		DreamUIManager->RebuildLayoutImmediately(InWidget);
	}
}

void UDreamWidget::MarkWidgetLayoutDirty()
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->AddLayoutDirtyWidget(this);
	}
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

void UDreamWidget::SetLayoutVisibilitySuppressed(bool bSuppressed)
{
	if (bLayoutVisibilitySuppressed != bSuppressed)
	{
		bLayoutVisibilitySuppressed = bSuppressed;
		// SizeBox, ScaleBox and WidgetSwitcher all flip this from inside their own arrange. It changes
		// which widgets a measurement is allowed to include, so every memoised desired size in the pass
		// may now be wrong - not just this widget's. Outside a pass the memo is empty and this is free.
		UDreamPanelLayoutBase::ForgetAllDesiredSizes();
		CalculateVisibility_Recursive();
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

void UDreamWidget::ForceLayoutPrepass()
{
	if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->RebuildLayoutImmediately(this);
	}
}

void UDreamWidget::InvalidateLayoutAndVolatility()
{
	MarkLayoutForRebuild(this);
}

FVector2D UDreamWidget::GetDesiredSize() const
{
	if (UDreamWidget* ParentWidget = Parent.Get(); IsValid(ParentWidget))
	{
		if (const UDreamPanelLayoutBase* ParentPanel = Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer()))
		{
			return ParentPanel->GetDesiredSize(const_cast<UDreamWidget*>(this));
		}
	}
	// Nothing is measuring this widget, so the size it has IS the size it wants.
	return GetSize();
}

void UDreamWidget::SetFlowDirectionPreference(EDreamFlowDirectionPreference Value)
{
	if (FlowDirectionPreference == Value)
	{
		return;
	}
	FlowDirectionPreference = Value;
	MarkFlowDirectionChangedRecursive();
}

void UDreamWidget::MarkFlowDirectionChangedRecursive()
{
	// Arrange and not Measure: mirroring changes where a container PUTS its children, never how big
	// any of them wants to be, so no preferred size on the ancestor chain can come out differently.
	// The container to dirty is this widget's OWN -- it is the one that arranges along the flow --
	// which is why this does not go through MarkLayoutForRebuild, whose Arrange branch dirties the
	// parent's container instead.
	if (UDreamLayoutContainer* Container = GetLayoutContainer(); IsValid(Container))
	{
		Container->MarkLayoutDirty();
		MarkWidgetLayoutDirty();
	}
	for (UDreamWidget* Child : GetChildren())
	{
		// A child that states its own preference already resolved to that answer and still does, and
		// so does everything below it. Stopping there keeps a per-screen mirror from re-arranging the
		// one panel that was deliberately pinned left-to-right.
		if (IsValid(Child) && Child->FlowDirectionPreference == EDreamFlowDirectionPreference::Inherit)
		{
			Child->MarkFlowDirectionChangedRecursive();
		}
	}
}

EDreamFlowDirection UDreamWidget::GetResolvedFlowDirection() const
{
	for (const UDreamWidget* Widget = this; Widget != nullptr; Widget = Widget->Parent.Get())
	{
		switch (Widget->FlowDirectionPreference)
		{
		case EDreamFlowDirectionPreference::LeftToRight:
			return EDreamFlowDirection::LeftToRight;
		case EDreamFlowDirectionPreference::RightToLeft:
			return EDreamFlowDirection::RightToLeft;
		case EDreamFlowDirectionPreference::Culture:
			return FLayoutLocalization::GetLocalizedLayoutDirection() == EFlowDirection::RightToLeft
				? EDreamFlowDirection::RightToLeft
				: EDreamFlowDirection::LeftToRight;
		case EDreamFlowDirectionPreference::Inherit:
			break;
		}
	}
	return EDreamFlowDirection::LeftToRight;
}

void UDreamWidget::RefreshCultureFlowDirection()
{
	if (FlowDirectionPreference == EDreamFlowDirectionPreference::Culture)
	{
		// This widget and everything below it that inherits just changed answer; nothing below a
		// descendant that states its own preference did, and the recursion already stops there.
		MarkFlowDirectionChangedRecursive();
		return;
	}
	for (UDreamWidget* Child : GetChildren())
	{
		if (IsValid(Child))
		{
			Child->RefreshCultureFlowDirection();
		}
	}
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

void UDreamWidget::SetIgnoreLayout(bool Value)
{
	if (bIgnoreLayout != Value)
	{
		bIgnoreLayout = Value;
		// Mark from the parent, not from here. MarkLayoutForRebuild breaks its ancestor walk on the first
		// widget with IgnoreLayout set - and we just set it - so starting at `this` stopped immediately and
		// the container that has to close the gap never heard about it. Turning the flag off happened to
		// work, because by then the flag reads false, which made this look like a one-way switch.
		MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
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
