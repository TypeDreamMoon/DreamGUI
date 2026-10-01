// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGoneCount.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
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
#include "Engine/Level.h"
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
	// Registered -> BegunPlay, and nothing in any other state (EDreamWidgetLifecycle): a second call is
	// harmless, and so is one for a widget already destroyed. Beginning play before registering is the
	// one worth reporting -- the manager ticks and raycasts only widgets it knows about, so a widget in
	// play it was never told of plays alone.
	if (Lifecycle != EDreamWidgetLifecycle::Registered)
	{
		ensureMsgf(Lifecycle != EDreamWidgetLifecycle::Constructed, TEXT("%s: BeginPlay on a widget that is not registered."), *GetPathName());
		return;
	}
	Lifecycle = EDreamWidgetLifecycle::BegunPlay;

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
	// BegunPlay -> Registered, and nothing in any other state: ending play for a widget that never began
	// used to run every part's EndPlay anyway, and a behaviour's OnDisable/OnDestroy with it.
	if (Lifecycle != EDreamWidgetLifecycle::BegunPlay)
	{
		return;
	}
	Lifecycle = EDreamWidgetLifecycle::Registered;

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
	// commandlet keeps a widget that never gets resaved working: the preview is instanced from this
	// object, so it copies whatever id this object is holding, and the backfill is the same on every load.
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
	// Diagnostics, and nothing else. This runs inside a collection, where taking a tree apart reaches
	// objects the same purge may already be destroying; and nothing is left to take apart that matters:
	// the manager's registry is weak, and a tree dies with the host it is outered to. A registered widget
	// reaching here is one whose host never let its tree go.
	//
	// Reported once per tree, at its root, and as an Error only where there is a world. A tree with NO
	// world never had a manager to leak into -- the state of every tree in a headless test and of every
	// Blueprint authoring tree -- and an Error at collection time lands on whichever test happens to be
	// running, which fails a bystander and teaches people to ignore the suite.
	if (HasRegistered() && Parent.GetEvenIfUnreachable() == nullptr)
	{
		if (const UWorld* World = GetWorld())
		{
			UE_LOG(DreamGUI, Error, TEXT("UDreamWidget tree %s (%s) was not destroyed by its owner. World:%s, WorldType:%d."),
				*GetFullName(), *GetDisplayName(), *World->GetName(), (int32)World->WorldType);
		}
		else
		{
			UE_LOG(DreamGUI, Warning, TEXT("UDreamWidget tree %s (%s) reached the collector still registered, with no world to have leaked into."),
				*GetFullName(), *GetDisplayName());
		}
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
			GetObjectsWithOuter(InOuter, Inner, EGetObjectsFlags::None);
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

	// The exit steps owed, in the order EDreamWidgetLifecycle fixes: EndPlay for the whole tree before
	// any of it unregisters, then Unregister, parents before children both times. Each step is a no-op
	// for a widget not in the state it leaves, so nothing here has to ask first.
	//
	// Both passes run with the hierarchy still writable -- a behaviour may move widgets in either -- so
	// each walks a list that grows: after a widget's step, whatever now hangs under it is appended. A
	// widget that arrived only while the tree was unregistering never had its EndPlay, and takes it just
	// before its own unregister.
	for (int32 EndPlayIndex = 0; EndPlayIndex < TeardownWidgets.Num(); ++EndPlayIndex)
	{
		UDreamWidget* Widget = TeardownWidgets[EndPlayIndex].Get();
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		Widget->EndPlay();
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}

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
			Widget->EndPlay();
			Widget->OnUnregister();
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

	// Which of them this call destroyed: every widget it tore down that still hangs in this subtree. A
	// widget a behaviour moved OUT while the tree came down has a live parent now; it was ended and
	// unregistered -- the caller asked for that and got it -- but it is no longer this subtree's to
	// destroy (see the marking below), so it stays Constructed, free to be registered where it went.
	TArray<UDreamWidget*> DestroyedWidgets;
	DestroyedWidgets.Reserve(TeardownWidgets.Num());
	for (const TObjectPtr<UDreamWidget>& TornDown : TeardownWidgets)
	{
		UDreamWidget* Widget = TornDown.Get();
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		if (Widget != this && !Widget->IsChildOf(this))
		{
			continue;//moved out from under us while we were coming down
		}
		DestroyedWidgets.Add(Widget);
	}
	for (UDreamWidget* Widget : DestroyedWidgets)
	{
		Widget->Lifecycle = EDreamWidgetLifecycle::Destroyed;
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
		WidgetsToMark.Reserve(DestroyedWidgets.Num());
		for (UDreamWidget* Widget : DestroyedWidgets)
		{
			if (!IsValid(Widget) || Widget->IsRooted() || Widget->HasAnyFlags(RF_BeginDestroyed))
			{
				continue;
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
		// None of them is one to write without a look-up any more (DreamUIGone).
		DreamUIGone::Note();
	}
}

UDreamUIManagerWorldSubsystem* UDreamWidget::GetRegisteredManager()const
{
	return RegisteredManager.Get();
}

UWorld* UDreamWidget::GetWorld() const
{
	// The world the level the widget is in plays or is edited in -- ULevel::OwningWorld, which is what
	// AActor::GetWorld answers -- before the UWorld its outers reach. The two are one world for the
	// persistent level; for a sublevel they are not: its own UWorld is the package's asset, which nothing
	// initializes, so a tree hosted there that asked it found no manager, and was neither laid out, nor
	// drawn, nor taken down with the world it was shown in.
	if (const ULevel* Level = GetTypedOuter<ULevel>(); Level != nullptr && Level->OwningWorld != nullptr)
	{
		return Level->OwningWorld;
	}
	return GetTypedOuter<UWorld>();
}


void UDreamWidget::OnRegister()
{
	// Constructed -> Registered, and nothing once registered (EDreamWidgetLifecycle): the manager's
	// AddWidget treats a duplicate as a bug worth an error and a stack dump, so a second call has to be
	// the no-op it is here. Registering a widget you did not create is safe to ask for. Registering one
	// that has been destroyed is not -- whoever asks is holding on to a widget whose host let it go.
	if (Lifecycle != EDreamWidgetLifecycle::Constructed)
	{
		ensureMsgf(Lifecycle != EDreamWidgetLifecycle::Destroyed, TEXT("%s: registering a widget that has been destroyed."), *GetPathName());
		return;
	}
	Lifecycle = EDreamWidgetLifecycle::Registered;
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
		// Kept for every transform change from now on, which would otherwise walk the outers to the world
		// on every write -- a thousand animated panels' worth of writes a frame.
		RegisteredManager = DreamUIManager;
		DreamUIManager->AddWidget(this);
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
	}
	else if (const UWorld* World = GetWorld(); World != nullptr && !IsRunningCommandlet()
		&& (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE
			|| World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview))
	{
		// A world of a kind the manager is made for, with no manager in it: nothing will lay this
		// widget out, draw it, tick it or take it down with the world.
		ensureMsgf(false, TEXT("%s: registered in %s, which has no DreamUI manager."), *GetPathName(), *World->GetName());
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
			Component->Call_OnRegister();
		}
	}
}
void UDreamWidget::OnUnregister()
{
	// Registered -> Constructed, and nothing in any other state: what follows undoes what registering
	// did -- a canvas leaves the manager, a visual leaves its canvas -- and undoing it for a widget that
	// was never registered, or twice, reaches into registries it is not in. A widget still in play ends
	// play first; DestroyWidget does that for a whole tree before any of it unregisters.
	if (Lifecycle != EDreamWidgetLifecycle::Registered)
	{
		return;
	}
	Lifecycle = EDreamWidgetLifecycle::Constructed;
	// No longer one to write without a look-up (DreamUIGone).
	DreamUIGone::Note();
	// From here a transform change is announced on the spot, as for any widget no manager knows of. One
	// already marked is still the manager's to announce: it keeps where the flush starts, weakly.
	RegisteredManager.Reset();

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
	// A behaviour a game world woke before its widget began play (UDreamUIBehaviour::Call_OnWidgetActiveChanged)
	// is awake, and no EndPlay of this widget's is coming to put it to rest: it never began. Unregistering is
	// the last moment it can be disabled and destroyed; for every other behaviour this is a no-op.
	for (UDreamUIBehaviour* Component : ComponentsToUnregister)
	{
		if (IsLiveForTeardown(Component) && Components.Contains(Component))
		{
			Component->EndPlay();
		}
	}
	for (UDreamUIBehaviour* Component : ComponentsToUnregister)
	{
		if (IsLiveForTeardown(Component) && Components.Contains(Component))
		{
			Component->Call_OnUnregister();
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
