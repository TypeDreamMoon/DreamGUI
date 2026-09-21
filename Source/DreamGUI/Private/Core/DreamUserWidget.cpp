// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUserWidget.h"
#include "Core/DreamUIEachAdapter.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamGestureEventData.h"
#include "Event/DreamKeyEventData.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Animation/DreamUISequence.h"
#include "Interaction/DreamContentWidget.h"
// BindEventBindings routes to FDreamUIEventDelegate events as well as to multicast delegates.
#include "Event/DreamUIEventDelegate.h"
#include "DreamGUI.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

/**
 * Bring a freshly built hierarchy to life, exactly as the prefab loader does at the end of a load.
 *
 * BeginPlay is gated on the MANAGER having begun play, not the world. The prefab loader learned
 * that the hard way and left a note: World->HasBegunPlay() returns false even when called from
 * BeginPlay. When it has not, the manager's own OnWorldBeginPlay picks these up later.
 */
bool DreamWidget_ShouldEditorExpandContents(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (!InWidget->IsA<UDreamUserWidget>())
	{
		// A plain widget's children are its own hierarchy. Nothing to hide.
		return true;
	}
	// Walk up rather than asking the editor which asset is open: the outermost instance on this path
	// is the one being edited, and that is decidable from the widget alone. In the designer the walk
	// stops at the design canvas's root agent; at runtime, at whatever the screen was added to.
	for (const UDreamWidget* Ancestor = InWidget->GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (Ancestor->IsA<UDreamUserWidget>())
		{
			return false;
		}
	}
	return true;
}

void CollectDreamEditorChildren(UDreamWidget* InWidget, TArray<UDreamWidget*>& OutChildren)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (DreamWidget_ShouldEditorExpandContents(InWidget))
	{
		OutChildren.Append(InWidget->GetChildren());
		return;
	}
	// A nested instance. Its own contents are another asset's; its slots are holes this host is
	// invited to fill, so those are the only thing it shows -- named, in the order the class declares
	// them, and present whether or not anything is in them yet. An empty slot the author cannot see
	// is a slot nobody uses.
	const UDreamUserWidget* Nested = Cast<UDreamUserWidget>(InWidget);
	if (Nested == nullptr)
	{
		return;
	}
	// The CLASS, not this instance's tree: a native control has no tree object to read slots off,
	// and asking the instance would show the author no rows at all for one.
	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(Nested->GetClass(), Declared);
	for (const FName& SlotName : Declared)
	{
		if (UDreamWidget* SlotWidget = Nested->FindSlotWidget(SlotName))
		{
			OutChildren.Add(SlotWidget);
		}
	}
}

void CollectDreamWidgetsToNestedBoundary(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets, bool bIncludeRoot)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	if (bIncludeRoot)
	{
		OutWidgets.Add(InRoot);
	}
	TArray<UDreamWidget*> Children;
	CollectDreamEditorChildren(InRoot, Children);
	for (UDreamWidget* Child : Children)
	{
		CollectDreamWidgetsToNestedBoundary(Child, OutWidgets, true);
	}
}

void RegisterDreamWidgetHierarchy(UDreamWidget* InRoot)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(InRoot, AllWidgets, true);

	// Parents before children, which CollectChildrenWidgets already gives us: OnRegister reads the
	// parent link to reconcile panel slots.
	for (UDreamWidget* Widget : AllWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->OnRegister();
		}
	}

	// Everything this subtree inherits from the parent it was just attached to. OnRegister does
	// this itself only for a hierarchy ROOT; a subtree parented through SetParentBeforeRegister
	// raises no attach event, so without this it registers holding its birth defaults -- visible
	// under a hidden parent, raycastable under a disabled one, and off the parent's render canvas.
	// One call here rather than at each of the four call sites, because this function IS the seam
	// every one of them goes through.
	if (InRoot->GetParent() != nullptr)
	{
		InRoot->RefreshInheritedStateFromParentChain();
	}

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InRoot->GetWorld()))
	{
		// A layout tree is collected once and cached against the widget it is rooted at, and nothing
		// invalidates that cache when a widget appears -- so a subtree registered after its ancestor's
		// tree was cached is laid out by nobody, and keeps its authored defaults until something
		// re-dirties everything top-down (a viewport resize). That is how a list cell created on a
		// second pass ends up drawn as a 100x100 block of overlapping text.
		//
		// Only ancestors: a cached tree not rooted above this subtree cannot contain it. Bounded,
		// because a parent chain is only acyclic while nothing has corrupted it.
		UDreamWidget* Parent = InRoot->GetParent();
		int32 DepthGuard = 0;
		for (UDreamWidget* Ancestor = Parent;
			Ancestor != nullptr && DepthGuard < 256;
			Ancestor = Ancestor->GetParent(), ++DepthGuard)
		{
			Manager->MarkRebuildLayoutTree(Ancestor);
		}
		// Dropping the cache only decides what the next pass would see; something still has to ask for
		// a pass. Ask on the parent, since that is the widget whose contents just changed.
		if (Parent != nullptr)
		{
			Manager->AddLayoutDirtyWidget(Parent);
		}

		if (Manager->HasBegunPlay())
		{
			for (UDreamWidget* Widget : AllWidgets)
			{
				// Skipped the way OnRegister above skips an already-registered widget, and for the same
				// reason: this walk does not own every widget it covers. It collects the whole subtree,
				// nested user widgets and all, and anything built by CreateDreamWidget has already
				// registered and begun on its own -- a nested widget when its own class initialized it,
				// an `each` list's cells the moment the list was given a data source, which is before
				// the containing widget is registered at all. BeginPlay asserts rather than tolerating a
				// second call, so the caller is the one that has to know.
				if (IsValid(Widget) && !Widget->HasBegunPlay())
				{
					Widget->BeginPlay();
				}
			}
		}
	}
}

namespace DreamUserWidgetDuplicateLocal
{
	/**
	 * Hand every DUPLICATED user widget the copy it was actually given.
	 *
	 * UDreamWidget::DuplicateSubtree deep-copies contents and re-aims intra-subtree references, but a
	 * nested user widget's WidgetTree is neither: it is a plain object property holding a
	 * UDreamWidgetTree, which the counterpart map does not cover, so the copy came out pointing at
	 * the SOURCE's tree -- and nothing ever initialized it. The consequences were all silent:
	 * GetContentRoot, FindSlotWidget and every animation verb answered about the template instead of
	 * the copy, and the copy's own `<-`, `->` and `each` bindings were never resolved at all, which
	 * is every Prefab list cell, dropdown row and ring-menu wedge.
	 *
	 * Pairs are found structurally, walking both hierarchies in step. Both sides are read through
	 * GetChildren(), which sorts by SiblingIndex: the copy's array starts out as the source's array
	 * minus its invalid entries, the keys are the same values on both sides, and a stable sort of a
	 * subsequence is the subsequence of the sorted sequence -- so the two walks stay aligned whether
	 * or not the source had been sorted before it was copied.
	 *
	 * Collected first and initialized afterwards: initializing resolves `each` blocks, which build
	 * list cells, which adds children to a hierarchy this walk would otherwise still be iterating.
	 */
	void AdoptDuplicatedUserWidgets(UDreamWidget* InSource, UDreamWidget* InCopy)
	{
		if (!IsValid(InSource) || !IsValid(InCopy))
		{
			return;
		}
		TMap<const UDreamWidget*, UDreamWidget*> SourceToCopy;
		TArray<TPair<TWeakObjectPtr<UDreamUserWidget>, TWeakObjectPtr<UDreamUserWidget>>> Pairs;
		struct FPairWalk
		{
			static void Walk(UDreamWidget* InFrom, UDreamWidget* InTo,
				TMap<const UDreamWidget*, UDreamWidget*>& OutMap,
				TArray<TPair<TWeakObjectPtr<UDreamUserWidget>, TWeakObjectPtr<UDreamUserWidget>>>& OutPairs)
			{
				OutMap.Add(InFrom, InTo);
				UDreamUserWidget* FromUserWidget = Cast<UDreamUserWidget>(InFrom);
				UDreamUserWidget* ToUserWidget = Cast<UDreamUserWidget>(InTo);
				if (FromUserWidget != nullptr && ToUserWidget != nullptr)
				{
					OutPairs.Emplace(FromUserWidget, ToUserWidget);
				}
				const TArray<UDreamWidget*> FromChildren = InFrom->GetChildren();
				const TArray<UDreamWidget*> ToChildren = InTo->GetChildren();
				int32 ToIndex = 0;
				for (UDreamWidget* FromChild : FromChildren)
				{
					if (!IsValid(FromChild))
					{
						continue;
					}
					while (ToChildren.IsValidIndex(ToIndex) && !IsValid(ToChildren[ToIndex]))
					{
						++ToIndex;
					}
					if (!ToChildren.IsValidIndex(ToIndex))
					{
						break;
					}
					Walk(FromChild, ToChildren[ToIndex++], OutMap, OutPairs);
				}
			}
		};
		FPairWalk::Walk(InSource, InCopy, SourceToCopy, Pairs);

		for (const TPair<TWeakObjectPtr<UDreamUserWidget>, TWeakObjectPtr<UDreamUserWidget>>& Pair : Pairs)
		{
			UDreamUserWidget* SourceUserWidget = Pair.Key.Get();
			UDreamUserWidget* CopyUserWidget = Pair.Value.Get();
			if (!IsValid(SourceUserWidget) || !IsValid(CopyUserWidget))
			{
				continue;
			}
			// A source with no tree of its own is a NATIVE control, whose contents are code-built
			// children rather than an instanced hierarchy. The copy of one already works, and
			// running the class's build road over it would give it a second set of contents.
			const UDreamWidgetTree* SourceTree = SourceUserWidget->GetWidgetTree();
			const UDreamWidget* SourceContentRoot = IsValid(SourceTree) ? SourceTree->RootWidget.Get() : nullptr;
			if (!IsValid(SourceContentRoot))
			{
				continue;
			}
			UDreamWidget** ContentRootCopy = SourceToCopy.Find(SourceContentRoot);
			if (ContentRootCopy == nullptr || !IsValid(*ContentRootCopy))
			{
				UE_LOG(DreamGUI, Warning,
					TEXT("[%s].%d Duplicated '%s' has no counterpart for its content root; it keeps the template's tree."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SourceUserWidget->GetPathDisplayName());
				continue;
			}
			CopyUserWidget->InitializeAsDuplicate(*ContentRootCopy);
		}
	}
}

UDreamWidget* DuplicateDreamWidgetHierarchy(UObject* InOuter, UDreamWidget* InTemplate, UDreamWidget* InParent)
{
	if (!IsValid(InTemplate))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Nothing to duplicate."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	UObject* Outer = InOuter != nullptr ? InOuter : InTemplate->GetOuter();
	if (Outer == nullptr)
	{
		return nullptr;
	}
	// The deep copy, and the parent back-pointers with it.
	UDreamWidget* Copy = UDreamWidget::DuplicateSubtree(Outer, InTemplate);
	if (!IsValid(Copy))
	{
		return nullptr;
	}
	// Before parenting and before registration, which is where Initialize sits on the other road.
	DreamUserWidgetDuplicateLocal::AdoptDuplicatedUserWidgets(InTemplate, Copy);
	if (InParent != nullptr)
	{
		Copy->SetParentBeforeRegister(InParent);
	}
	// Registration now re-derives everything the copy inherits from its new parent, the render
	// canvas among it -- which is what kept duplicated list cells built, laid out, active and
	// invisible until it was found.
	RegisterDreamWidgetHierarchy(Copy);
	return Copy;
}

void UDreamUserWidget::Initialize()
{
	// Walk up for the tree: a subclass that only adds logic declares none of its own, and has to
	// instance its parent's. Resolving this on the class rather than here keeps a native subclass
	// (which never gets a generated class at all) working the same way.
	InitializeFromArchetype(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass()));
}

void UDreamUserWidget::InitializeFromArchetype(UDreamWidgetTree* InArchetype)
{
	if (bInitialized || IsTemplate())
	{
		return;
	}
	bInitialized = true;

	// UMG's PreConstruct: the moment before this widget has any contents, which is where a graph sets
	// the properties those contents are about to be built from. It runs in the designer preview too,
	// and unlike On Initialized it is told which one it is in.
	PreConstruct(IsDesignTime());

	// What the HOST hung on this widget, taken before this widget makes anything of its own. Both
	// roads that produce contents run below -- InitializeWidgetStatic instances an archetype,
	// NativeOnInitialized realizes a native control's tree -- so this snapshot is exactly "not
	// mine", and AdoptUnslottedChildren needs no other rule to tell guests from furniture.
	HostSuppliedChildren.Reset();
	for (UDreamWidget* Child : GetChildren())
	{
		HostSuppliedChildren.Add(Child);
	}

	UDreamWidgetGeneratedClass::InitializeWidgetStatic(this, GetClass(), InArchetype);

	// The Blueprint surface's wiring, before NativeOnInitialized so OnInitialized code can already
	// SetWantsTick or SetAllowEventBubbleUp and have it hold. The bridge carries lifecycle, pointer,
	// drag and navigation delivery (see UDreamUserWidgetEventBridge); focus rides this widget's own
	// existing broadcasts, bound here because Initialize is the one moment every instance passes
	// through exactly once.
	EnsureEventBridge();
	OnFocusReceived.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusReceivedBroadcast);
	OnFocusLost.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusLostBroadcast);
	{
		// Whether the Blueprint actually implemented OnTick: the UFunction on a Blueprint-compiled
		// class is an override, the one on the native declaring class is the empty stub. Same probe
		// UDreamUIBehaviour runs for ReceiveTick, cached for the same per-frame reason.
		static const FName OnTickName(TEXT("OnTick"));
		const UFunction* TickFunction = GetClass()->FindFunctionByName(OnTickName);
		bHasBlueprintOnTick = TickFunction != nullptr
			&& TickFunction->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
		ProbeBlueprintPointerMove();
	}

	// UMG's TickFrequency=Auto, translated: implementing On Tick IS opting in. bWantsTick stays
	// off by default because the widgets that never tick should pay nothing, but a Blueprint that
	// put the event in its graph means it -- without this the graph compiles, PIE runs, and nothing
	// fires, with no line anywhere saying why. Before NativeOnInitialized, so an OnInitialized that
	// explicitly calls SetWantsTick(false) still wins.
	// On Mouse Move opts a widget in as surely as On Tick does: it is derived from the pointer on the
	// bridge's own tick, so without a tick it would never fire and the graph would compile, run and do
	// nothing with no line anywhere saying why.
	if ((bHasBlueprintOnTick || bHasBlueprintPointerMove) && !bWantsTick)
	{
		SetWantsTick(true);
	}

	// Before anything reads this widget's data. Everything below pulls from it -- a binding reads a
	// property, an `each` list asks its source how many rows there are -- so a subclass filling a list
	// source gets the floor here. Later is after the first frame has already been composed from
	// whatever the source held, which for a list built at Begin Play is nothing.
	NativeOnInitialized();

	// Both kinds of contents now exist, and nothing is registered yet: the one moment that works for
	// an archetype-instanced tree and a code-built one alike.
	AttachNamedSlotContent();
	AdoptUnslottedChildren();
	NativeOnSlotContentAttached();
	// Whoever wanted it has had it. Kept any longer it is a list of widgets that have since moved,
	// been reparented or been destroyed, answering a question nobody should still be asking.
	HostSuppliedChildren.Reset();

	// After the tree exists: the bindings name widgets in it.
	ResolvePropertyBindings();
	BindEventBindings();
	ResolveEachBindings();
	if (ResolvedBindings.Num() > 0)
	{
		// Once now, so the first frame shows bound values rather than the authored ones. All of
		// them: a subscribed binding's broadcast only fires on the NEXT change, and the current
		// value has to reach the widget too.
		EvaluatePropertyBindings();
		if (HasPolledPropertyBindings())
		{
			// Only the polled remainder needs the per-frame visit; the subscribed bindings
			// re-evaluate from their field's broadcast.
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
			{
				Manager->AddPropertyBindingUser(this);
			}
		}
	}
}

void UDreamUserWidget::InitializeAsDuplicate(UDreamWidget* InContentRoot)
{
	if (bInitialized || IsTemplate() || !IsValid(InContentRoot))
	{
		return;
	}
	bInitialized = true;

	// A duplicate's contents already exist -- they were deep-copied with it -- so the one thing
	// missing is a tree OBJECT of its own to call them. Built the same shape InitializeWidgetStatic
	// builds one: outered to this widget, transactional and transient taken from this widget, so a
	// copy of a preview does not become saveable and a copy of an authored widget stays undoable.
	UDreamWidgetTree* OwnTree = NewObject<UDreamWidgetTree>(this, UDreamWidgetTree::StaticClass(), NAME_None,
		GetMaskedFlags(RF_Transactional));
	OwnTree->SetFlags(GetMaskedFlags(RF_Transient));
	OwnTree->RootWidget = InContentRoot;
	WidgetTree = OwnTree;

	// Transient and therefore copied verbatim from the source, where they drive the SOURCE's list
	// views. ResolveEachBindings below makes this widget's own.
	EachAdapters.Reset();

	// The same wiring Initialize does, for the same reasons, minus the two steps that only make
	// sense for freshly built contents: nothing here instances an archetype, and the host's named
	// slot content came across with the copy already placed, so re-attaching it would move widgets
	// that are exactly where they belong.
	EnsureEventBridge();
	OnFocusReceived.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusReceivedBroadcast);
	OnFocusLost.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusLostBroadcast);
	{
		static const FName OnTickName(TEXT("OnTick"));
		const UFunction* TickFunction = GetClass()->FindFunctionByName(OnTickName);
		bHasBlueprintOnTick = TickFunction != nullptr
			&& TickFunction->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
		ProbeBlueprintPointerMove();
	}
	// On Mouse Move opts a widget in as surely as On Tick does: it is derived from the pointer on the
	// bridge's own tick, so without a tick it would never fire and the graph would compile, run and do
	// nothing with no line anywhere saying why.
	if ((bHasBlueprintOnTick || bHasBlueprintPointerMove) && !bWantsTick)
	{
		SetWantsTick(true);
	}

	NativeOnInitialized();

	// After the tree exists: the bindings name widgets in it. This is the whole point of the
	// exercise -- a duplicated cell used to resolve none of them.
	ResolvePropertyBindings();
	BindEventBindings();
	ResolveEachBindings();
	if (ResolvedBindings.Num() > 0)
	{
		EvaluatePropertyBindings();
		if (HasPolledPropertyBindings())
		{
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
			{
				Manager->AddPropertyBindingUser(this);
			}
		}
	}
}

void UDreamUserWidget::NativeOnSlotContentAttached()
{
	OnSlotContentAttached();
}

bool UDreamUserWidget::NeedsReinitializeFromClass() const
{
	// The signature of a reinstanced survivor: contents underneath, no tree to call them, and no
	// record of ever having been initialized. A widget that simply has not been initialized yet has
	// no children either, which is what keeps this from firing on one.
	if (bInitialized || IsValid(WidgetTree) || IsTemplate())
	{
		return false;
	}
	// LIVE contents, which is not the same as a non-empty array. The reinstancer's copy shares the
	// original's children; when the original's owner tears it down instead of adopting the copy --
	// the designer's preview host does exactly that -- those children are destroyed, and the next
	// collection leaves the copy holding nulls. Such a copy has nothing on screen to repair and nobody
	// who owns it: counting its holes as contents sent it through a rebuild that then registered a
	// hierarchy for an orphan, and the first walk over the array fell into the hole.
	bool bHasLiveContents = false;
	for (const UDreamWidget* Child : GetChildren())
	{
		if (IsValid(Child))
		{
			bHasLiveContents = true;
			break;
		}
	}
	return bHasLiveContents
		&& UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass()) != nullptr;
}

void UDreamUserWidget::ReinitializeFromClass()
{
	ReinitializeFromArchetype(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass()));
}

void UDreamUserWidget::ReinitializeFromArchetype(UDreamWidgetTree* InArchetype)
{
	if (IsTemplate())
	{
		return;
	}
	// Nothing to rebuild FROM. Destroying the contents anyway would turn a widget that still works
	// into an empty one -- strictly worse than the half-dead state this exists to repair -- and the
	// two ways to get here are both ordinary: a class that declares no hierarchy of its own (a
	// logic-only subclass, a native class) and one whose Blueprint has not compiled yet.
	if (!IsValid(InArchetype))
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("[%s].%d '%s' has no hierarchy to rebuild from, so its contents are left as they are."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
		return;
	}
	// The host's widgets, which are not this class's to destroy. NamedSlotContent is a persistent
	// Instanced map and therefore the one thing that survives reinstancing intact, which is exactly
	// what makes "mine" and "the host's" separable here at all.
	TSet<const UDreamWidget*> HostContent;
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& SlotPair : NamedSlotContent)
	{
		if (IsValid(SlotPair.Value))
		{
			HostContent.Add(SlotPair.Value);
		}
	}

	// Everything else under this widget came from the OLD class's tree. Detach the host's content
	// first so destroying the rest cannot take it down with it -- AttachNamedSlotContent puts it back
	// into the new tree's slots a moment later.
	//
	// Through whichever door matches the widget's state, the same pair AttachNamedSlotContent uses on
	// the way back in: SetParentBeforeRegister asserts !bIsRegistered, and the host content of a LIVE
	// instance -- which is every instance this function exists for -- is registered.
	//
	// Holes first. A reinstanced copy can arrive with null entries where destroyed children were (see
	// NeedsReinitializeFromClass), the two loops below skip what is not valid and so would leave them
	// in place, and everything after this -- instancing, slot adoption, registration -- walks the
	// array assuming each entry is a widget.
	EnsureUIChildrenValid();
	const TArray<UDreamWidget*> PreviousChildren = GetChildren();
	for (UDreamWidget* Child : PreviousChildren)
	{
		if (IsValid(Child) && HostContent.Contains(Child))
		{
			if (Child->HasRegistered())
			{
				Child->TrySetParent(nullptr, false);
			}
			else
			{
				Child->SetParentBeforeRegister(nullptr);
			}
		}
	}
	for (UDreamWidget* Child : PreviousChildren)
	{
		if (IsValid(Child) && !HostContent.Contains(Child))
		{
			Child->DestroyWidget();
		}
	}
	// And again, for what the loop above just produced. A reinstanced copy's children arrive without
	// their Parent link (it is DuplicateTransient), so a child destroyed here cannot take itself out of
	// this array the way a properly attached one does -- it stays behind as a garbage entry, ahead of
	// the root the rebuild is about to append.
	EnsureUIChildrenValid();

	// Back to the pre-Initialize state, then through the ordinary road: the archetype is instanced,
	// the by-name bindings resolve against it, and the host's slot content is re-attached.
	//
	// InitializeFromArchetype rather than Initialize, for the same reason InitializeWidgetStatic takes
	// its archetype as a parameter: the caller may know which hierarchy this instance is being rebuilt
	// from -- the designer's authoring tree, a test's fixture -- and re-deriving it from the class here
	// would silently rebuild the wrong one, or nothing at all for a class that declares none.
	EachAdapters.Reset();
	ResolvedBindings.Reset();
	PolledBindingCount = 0;
	bInitialized = false;
	WidgetTree = nullptr;
	InitializeFromArchetype(InArchetype);
	// Registration is what makes the new subtree lay out and draw; nothing else re-registers it.
	RegisterDreamWidgetHierarchy(this);
}

UDreamWidget* UDreamUserWidget::GetWidgetFromName(FName InVariableName) const
{
	// The tree's own resolver, which is the same one the compiler declares variables with -- so a
	// graph asking by name and a binding resolving by name cannot disagree.
	if (IsValid(WidgetTree))
	{
		if (UDreamWidget* Found = WidgetTree->FindWidgetByVariableName(InVariableName))
		{
			return Found;
		}
	}
	// A native control has no tree; its contents are its own children, and display name is the only
	// name they have.
	return FindChildByDisplayName(InVariableName.ToString(), true);
}

TArray<FName> UDreamUserWidget::K2_GetDeclaredSlotNames() const
{
	TArray<FName> Names;
	CollectDeclaredSlotNames(GetClass(), Names);
	return Names;
}

void UDreamUserWidget::ProbeBlueprintPointerMove()
{
	// Same probe as On Tick, and for the same reason: the UFunction on a Blueprint-compiled class is
	// an override, the one on this native class is the empty stub.
	static const FName OnMouseMoveName(TEXT("OnMouseMove"));
	const UFunction* MoveFunction = GetClass()->FindFunctionByName(OnMouseMoveName);
	bHasBlueprintPointerMove = MoveFunction != nullptr
		&& MoveFunction->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
}

void UDreamUserWidget::NativeOnInitialized()
{
	OnInitialized();
}

#pragma region BlueprintSurface
void UDreamUserWidget::EnsureEventBridge()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		// Edit worlds -- the designer's preview above all -- never get a bridge: behaviour lifecycle
		// refuses edit mode anyway, and a component the author did not add has no business in a
		// preview whose component list the editor reads back.
		return;
	}
	if (GetComponent<UDreamUserWidgetEventBridge>() != nullptr)
	{
		// A duplicated instance (a list cell) arrives with its copy through the Instanced Components
		// array; a second bridge would double every event.
		return;
	}
	if (UDreamUIBehaviour* Bridge = AddComponent<UDreamUserWidgetEventBridge>())
	{
		// The class is already Transient; the instance flag keeps even the REFERENCE out of any
		// serializer that ever walks a live game-world tree.
		Bridge->SetFlags(RF_Transient);
	}
}

void UDreamUserWidget::NativeOnConstruct()
{
	bConstructed = true;
	// Paired with the unregister in NativeOnDestruct. Construct/destruct is the window in which the
	// widget is live on screen, which is exactly when a culture change is worth hearing about, and
	// pairing it here is what spares every Blueprint the manual register/unregister the interface
	// used to demand.
	UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(this);
	OnConstruct();
}

void UDreamUserWidget::NativeOnDestruct()
{
	bConstructed = false;
	OnDestruct();
	// Input action bindings die with the widget that asked for them. Leaving them would fire a
	// callback into a destroyed widget the next time the key was pressed -- the same defect the
	// polled bindings had below, in the one other list this widget puts itself on.
	StopListeningForAllInputActions();
	UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(this);
	// Stop being polled. DestroyWidget unregisters, ends play and detaches without ever marking the
	// object garbage, so the manager's own !IsValid sweep never sees this widget go -- it would keep
	// calling the binding source functions of a widget that has run EndPlay until the next full GC.
	// The manager drops it on unregister as well; this covers a widget that ends play without ever
	// having been registered.
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->RemovePropertyBindingUser(this);
	}
}

void UDreamUserWidget::OnCultureChanged_Implementation()
{
}

#pragma region OwningPlayer
int32 UDreamUserWidget::GetLocalPlayerIndexOf(const APlayerController* InPlayerController)
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

APlayerController* UDreamUserWidget::GetOwningPlayer() const
{
	if (APlayerController* Explicit = OwningPlayer.Get(); IsValid(Explicit))
	{
		return Explicit;
	}
	// Inherit from whoever hosts this widget. A nested instance, a list cell and a dialog pushed on a
	// player's stack all belong to the player whose hierarchy they are in, and saying so once here
	// spares every one of them a SetOwningPlayer call.
	for (const UDreamWidget* Ancestor = GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (const UDreamUserWidget* HostUserWidget = Cast<const UDreamUserWidget>(Ancestor))
		{
			if (APlayerController* Inherited = HostUserWidget->OwningPlayer.Get(); IsValid(Inherited))
			{
				return Inherited;
			}
		}
	}
	// The whole answer in a single-player game, and what UMG's CreateWidget defaults to.
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetFirstPlayerController() : nullptr;
}

void UDreamUserWidget::SetOwningPlayer(APlayerController* InPlayerController)
{
	OwningPlayer = InPlayerController;
}

ULocalPlayer* UDreamUserWidget::GetOwningLocalPlayer() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return IsValid(PlayerController) ? PlayerController->GetLocalPlayer() : nullptr;
}

APawn* UDreamUserWidget::GetOwningPlayerPawn() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
}

int32 UDreamUserWidget::GetOwningPlayerIndex() const
{
	return GetLocalPlayerIndexOf(GetOwningPlayer());
}

bool UDreamUserWidget::SetKeyboardFocus()
{
	return SetFocus(GetOwningPlayerIndex());
}

bool UDreamUserWidget::HasKeyboardFocus() const
{
	return HasFocus(GetOwningPlayerIndex());
}

bool UDreamUserWidget::HasUserFocus(APlayerController* InPlayerController) const
{
	return HasFocus(GetLocalPlayerIndexOf(InPlayerController));
}

void UDreamUserWidget::ClearKeyboardFocus()
{
	ClearFocus(GetOwningPlayerIndex());
}

void UDreamUserWidget::PlaySound(USoundBase* InSound, float InVolumeMultiplier, float InPitchMultiplier)
{
	// The same gate the controls' own hover and click sounds use: nothing outside a game world, so a
	// designer preview stays silent whatever it instances.
	UWorld* World = GetWorld();
	if (!IsValid(InSound) || !IsValid(World) || !World->IsGameWorld())
	{
		return;
	}
	UGameplayStatics::PlaySound2D(World, InSound, InVolumeMultiplier, InPitchMultiplier);
}
#pragma endregion

#pragma region ViewportPlacementAndGeometry
FVector2D UDreamUserWidget::GetLocalSize() const
{
	return FVector2D(GetWidth(), GetHeight());
}

FBox2D UDreamUserWidget::GetScreenSpaceRect() const
{
	// The root canvas is the screen's space; without one there is no screen rectangle to report and an
	// empty box is the honest answer rather than a rectangle at the origin.
	const UDreamCanvas* RootCanvas = GetRenderCanvas();
	if (!IsValid(RootCanvas))
	{
		return FBox2D(ForceInit);
	}
	// The widget's own rectangle, pivot included -- not a half-size guess around the origin.
	const FVector2D LeftBottom = GetLocalSpaceLeftBottomPoint();
	const FVector2D RightTop = GetLocalSpaceRightTopPoint();
	const FTransform& WidgetToWorld = GetWorldTransform();
	// The four corners rather than two, because a rotated widget's screen rectangle is the bounds of
	// its corners and not the transform of its min and max. X is depth in this framework's UI space;
	// the plane is YZ, which is what every other local-to-world conversion here assumes.
	FBox2D Result(ForceInit);
	const FVector2D LocalCorners[4] = {
		LeftBottom, FVector2D(RightTop.X, LeftBottom.Y), RightTop, FVector2D(LeftBottom.X, RightTop.Y) };
	for (const FVector2D& LocalCorner : LocalCorners)
	{
		const FVector WorldCorner = WidgetToWorld.TransformPosition(FVector(0.0, LocalCorner.X, LocalCorner.Y));
		Result += FVector2D(WorldCorner.Y, WorldCorner.Z);
	}
	return Result;
}

void UDreamUserWidget::SetPositionInViewport(FVector2D InPosition)
{
	MarkViewportPlacementCustom();
	SetAnchoredPosition(InPosition);
}

void UDreamUserWidget::SetDesiredSizeInViewport(FVector2D InSize)
{
	MarkViewportPlacementCustom();
	SetSizeDelta(InSize);
}

void UDreamUserWidget::SetAlignmentInViewport(FVector2D InAlignment)
{
	MarkViewportPlacementCustom();
	SetPivot(InAlignment);
}

void UDreamUserWidget::SetAnchorsInViewport(FVector2D InAnchorMin, FVector2D InAnchorMax)
{
	MarkViewportPlacementCustom();
	SetHorizontalAndVerticalAnchorMinMax(InAnchorMin, InAnchorMax, false, false);
}

void UDreamUserWidget::ClearPlacementInViewport()
{
	if (UDreamScreenUISubsystem* Screen = UDreamScreenUISubsystem::Get(GetWorld()))
	{
		Screen->SetPageHasCustomPlacement(this, false);
	}
}

void UDreamUserWidget::MarkViewportPlacementCustom()
{
	// Tell the screen subsystem to stop re-applying full-bleed to this page. Without it the stack
	// restored anchors 0..1 and a zero inset on its next refresh, which is what made "a window that is
	// not full screen" impossible to keep on the page stack.
	if (UDreamScreenUISubsystem* Screen = UDreamScreenUISubsystem::Get(GetWorld()))
	{
		Screen->SetPageHasCustomPlacement(this, true);
	}
}
#pragma endregion

#pragma region InputActions
FDreamUIActionHandle UDreamUserWidget::ListenForInputAction(const FDataTableRowHandle& InAction,
	FDreamUIActionExecutedDelegate InCallback, bool bDisplayInActionBar)
{
	FDreamUIActionHandle Handle;
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	if (Router == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No action router in this world; '%s' heard nothing."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
		return Handle;
	}
	// Scoped to the screen this widget is inside, so the binding is live only while that screen is in
	// front. A widget with no scope above it binds globally, which is the honest reading of "there is
	// no screen this belongs to".
	UDreamUINavigationScope* Scope = nullptr;
	for (UDreamWidget* Walker = this; IsValid(Walker) && Scope == nullptr; Walker = Walker->GetParent())
	{
		Scope = Walker->GetComponent<UDreamUINavigationScope>();
	}
	Handle = Router->RegisterAction(Scope, InAction, InCallback, GetOwningPlayerIndex(), bDisplayInActionBar);
	if (Handle.IsValidHandle())
	{
		ListenedInputActions.Add(Handle);
	}
	return Handle;
}

void UDreamUserWidget::StopListeningForInputAction(const FDreamUIActionHandle& InHandle)
{
	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		Router->UnregisterAction(InHandle);
	}
	ListenedInputActions.RemoveAll([&InHandle](const FDreamUIActionHandle& Held) { return Held == InHandle; });
}

void UDreamUserWidget::StopListeningForAllInputActions()
{
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	// A copy: UnregisterAction is free to do anything, and the array is this widget's own bookkeeping.
	const TArray<FDreamUIActionHandle> Held = ListenedInputActions;
	ListenedInputActions.Reset();
	if (Router == nullptr)
	{
		return;
	}
	for (const FDreamUIActionHandle& Handle : Held)
	{
		Router->UnregisterAction(Handle);
	}
}

bool UDreamUserWidget::IsListeningForInputAction(const FDreamUIActionHandle& InHandle) const
{
	return ListenedInputActions.ContainsByPredicate(
		[&InHandle](const FDreamUIActionHandle& Held) { return Held == InHandle; });
}
#pragma endregion

void UDreamUserWidget::NativeOnEnable()
{
	OnEnable();
}

void UDreamUserWidget::NativeOnDisable()
{
	OnDisable();
}

void UDreamUserWidget::NativeOnTick(float DeltaTime)
{
	if (bHasBlueprintOnTick)
	{
		OnTick(DeltaTime);
	}
}

void UDreamUserWidget::SetWantsTick(bool Value)
{
	if (bWantsTick == Value)
	{
		return;
	}
	bWantsTick = Value;
	if (UDreamUserWidgetEventBridge* Bridge = GetComponent<UDreamUserWidgetEventBridge>())
	{
		Bridge->SyncTickEnabled(Value);
	}
	// No bridge -- a template, an edit world, or before Initialize -- means nothing is registered
	// anywhere; the bridge reads bWantsTick when it awakes.
}

bool UDreamUserWidget::NativeOnPointerEnter(UDreamPointerEventData* EventData)
{
	OnPointerEnter(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerExit(UDreamPointerEventData* EventData)
{
	OnPointerExit(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerDown(UDreamPointerEventData* EventData)
{
	OnPointerDown(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerUp(UDreamPointerEventData* EventData)
{
	OnPointerUp(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerClick(UDreamPointerEventData* EventData)
{
	OnPointerClick(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnBeginDrag(UDreamPointerEventData* EventData)
{
	OnBeginDrag(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnDrag(UDreamPointerEventData* EventData)
{
	OnDrag(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnEndDrag(UDreamPointerEventData* EventData)
{
	OnEndDrag(EventData);
	// UMG's OnDragCancelled, on the widget that STARTED the drag: the drop has already run by the time
	// the drag ends, so an operation nobody handled is a drag that was cancelled. Same reading
	// UDreamUIDragSource makes one line later when it tells the operation the same thing.
	if (UDreamDragDropOperation* Operation = EventData != nullptr ? EventData->DragOperation.Get() : nullptr)
	{
		if (!Operation->bDropWasHandled)
		{
			NativeOnDragCancelled(Operation);
		}
	}
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnDrop(UDreamPointerEventData* EventData)
{
	OnDrop(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerScroll(UDreamPointerEventData* EventData)
{
	OnMouseWheel(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerDoubleClick(UDreamPointerEventData* EventData)
{
	OnDoubleClick(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerLongPress(UDreamPointerEventData* EventData)
{
	OnLongPress(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerPinch(UDreamGestureEventData* EventData)
{
	OnPinch(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerSwipe(UDreamGestureEventData* EventData)
{
	OnSwipe(EventData);
	return bAllowEventBubbleUp;
}

void UDreamUserWidget::NativeOnPointerMove(UDreamPointerEventData* EventData)
{
	OnMouseMove(EventData);
}

void UDreamUserWidget::NativeOnDragEnter(UDreamPointerEventData* EventData, UDreamDragDropOperation* Operation)
{
	OnDragEnter(EventData, Operation);
}

void UDreamUserWidget::NativeOnDragOver(UDreamPointerEventData* EventData, UDreamDragDropOperation* Operation)
{
	OnDragOver(EventData, Operation);
}

void UDreamUserWidget::NativeOnDragLeave(UDreamPointerEventData* EventData, UDreamDragDropOperation* Operation)
{
	OnDragLeave(EventData, Operation);
}

void UDreamUserWidget::NativeOnDragCancelled(UDreamDragDropOperation* Operation)
{
	OnDragCancelled(Operation);
}

// The key channels answer HANDLED rather than "may this bubble": a key somebody kept must not also be
// read as navigation or fire a bound action, so the Blueprint event's own return value is the answer
// and EventData->bHandled is the other way to say the same thing.
bool UDreamUserWidget::NativeOnKeyDown(UDreamKeyEventData* EventData)
{
	return ReceiveKeyDown(EventData);
}

bool UDreamUserWidget::NativeOnKeyUp(UDreamKeyEventData* EventData)
{
	return ReceiveKeyUp(EventData);
}

bool UDreamUserWidget::NativeOnKeyChar(UDreamKeyEventData* EventData)
{
	return ReceiveKeyChar(EventData);
}

bool UDreamUserWidget::NativeOnAnalogValueChanged(UDreamKeyEventData* EventData)
{
	return ReceiveAnalogValueChanged(EventData);
}

void UDreamUserWidget::NativeOnFocusReceived(int32 UserIndex, int32 PointerId)
{
	ReceiveFocusReceived(UserIndex, PointerId);
}

void UDreamUserWidget::NativeOnFocusLost(int32 UserIndex, int32 PointerId)
{
	ReceiveFocusLost(UserIndex, PointerId);
}

void UDreamUserWidget::HandleFocusReceivedBroadcast(int32 UserIndex, int32 PointerId)
{
	NativeOnFocusReceived(UserIndex, PointerId);
}

void UDreamUserWidget::HandleFocusLostBroadcast(int32 UserIndex, int32 PointerId)
{
	NativeOnFocusLost(UserIndex, PointerId);
}

void UDreamUserWidget::NativeOnNavigate(EDreamUINavigationDirection Direction, UDreamWidget*& OutNextWidget)
{
	OutNextWidget = OnNavigate(Direction);
}
#pragma endregion

#pragma region EventBridge
UDreamUserWidgetEventBridge::UDreamUserWidgetEventBridge()
{
	// Opt-in cost: never in the tick list until the widget asks. Awake re-reads the widget's
	// bWantsTick, so these are only the values for the window before Awake -- both lowered so
	// IsTickForwardingEnabled cannot claim ticking that is not armed.
	bStartWithTickEnabled = false;
	bCanExecuteTick = false;
}

UDreamUserWidget* UDreamUserWidgetEventBridge::GetUserWidget() const
{
	return Cast<UDreamUserWidget>(GetWidget());
}

void UDreamUserWidgetEventBridge::SyncTickEnabled(bool bValue)
{
	bStartWithTickEnabled = bValue;
	if (bIsEnableCalled)
	{
		// Enabled: the standard door, which adds to or removes from the manager's tick list once
		// Start has run and leaves the flag for the start pass to read when it has not.
		SetCanExecuteTick(bValue);
	}
	else
	{
		// Disabled (or not yet begun): no tick registration exists, so going through
		// SetCanExecuteTick on a started behaviour would try to remove what OnDisable already
		// removed and log a spurious warning. OnEnable reads the flag and registers.
		bCanExecuteTick = bValue;
	}
}

void UDreamUserWidgetEventBridge::Awake()
{
	Super::Awake();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		// Before UDreamUIBehaviour::BeginPlay copies bStartWithTickEnabled into bCanExecuteTick,
		// which happens right after Awake returns.
		bStartWithTickEnabled = UserWidget->GetWantsTick();
		UserWidget->NativeOnConstruct();
		++ConstructForwardCount;
	}
}

void UDreamUserWidgetEventBridge::OnEnable()
{
	Super::OnEnable();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnEnable();
		++EnableForwardCount;
	}
}

void UDreamUserWidgetEventBridge::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnTick(DeltaTime);
		++TickForwardCount;
	}
	// The derived mouse-move event. Costs one flag read on a widget that did not ask for it.
	TickPointerMoveWatch();
}

void UDreamUserWidgetEventBridge::OnDisable()
{
	Super::OnDisable();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnDisable();
		++DisableForwardCount;
	}
}

void UDreamUserWidgetEventBridge::OnDestroy()
{
	Super::OnDestroy();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnDestruct();
		++DestructForwardCount;
	}
}

namespace DreamUserWidgetBridgeLocal
{
	/** The operation a pointer is carrying, or null when this pointer is not a live, meaningful drag. */
	UDreamDragDropOperation* LiveDragOperation(const UDreamPointerEventData* InEventData)
	{
		return InEventData != nullptr && InEventData->bIsDragging ? InEventData->DragOperation.Get() : nullptr;
	}
}

bool UDreamUserWidgetEventBridge::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget == nullptr)
	{
		return true;
	}
	// A pointer entering while it carries a drag IS the drag entering -- UMG's OnDragEnter, which
	// arrives on the widget under the pointer rather than on the one being dragged. The enter/exit
	// channel keeps running through a drag, so this needs no second dispatch of its own.
	if (UDreamDragDropOperation* Operation = DreamUserWidgetBridgeLocal::LiveDragOperation(EventData))
	{
		UserWidget->NativeOnDragEnter(EventData, Operation);
	}
	return UserWidget->NativeOnPointerEnter(EventData);
}

bool UDreamUserWidgetEventBridge::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget == nullptr)
	{
		return true;
	}
	if (UDreamDragDropOperation* Operation = DreamUserWidgetBridgeLocal::LiveDragOperation(EventData))
	{
		UserWidget->NativeOnDragLeave(EventData, Operation);
	}
	return UserWidget->NativeOnPointerExit(EventData);
}

bool UDreamUserWidgetEventBridge::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerDown(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerUp(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerClick(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnBeginDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnEndDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnDrop(EventData) : true;
}

bool UDreamUserWidgetEventBridge::CanNavigateHere_Implementation() const
{
	const UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(GetWidget());
	return UserWidget != nullptr
		&& UserWidget->GetCanNavigateHere()
		&& UserWidget->GetWidgetActiveInHierarchy()
		&& UserWidget->GetInteractableInHierarchy();
}

bool UDreamUserWidgetEventBridge::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget == nullptr)
	{
		return false;
	}
	UDreamWidget* NextWidget = nullptr;
	UserWidget->NativeOnNavigate(direction, NextWidget);
	if (IsValid(NextWidget))
	{
		// The navigation module speaks to behaviours (its result is cast to UDreamUIBehaviour), so a
		// widget answer resolves to that widget's navigation-capable component -- a UISelectable, a
		// nested user widget's own bridge, or any custom handler.
		if (UDreamUIBehaviour* NextHandler = NextWidget->GetComponentByInterface(UDreamNavigationInterface::StaticClass()))
		{
			result.SetObject(NextHandler);
			result.SetInterface(Cast<IDreamNavigationInterface>(NextHandler));
		}
		else
		{
			UE_LOG(DreamGUI, Warning,
				TEXT("[%s].%d OnNavigate on '%s' returned '%s', which has no navigation-capable behaviour; staying put."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *UserWidget->GetPathDisplayName(), *NextWidget->GetPathDisplayName());
		}
	}
	// True with a null result keeps the highlight here: an opted-in widget that names no successor
	// is a navigation sink, which is what it opted in to be.
	return true;
}

bool UDreamUserWidgetEventBridge::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerScroll(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerDoubleClick(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerLongPress_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerLongPress(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerPinch_Implementation(UDreamGestureEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerPinch(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerSwipe_Implementation(UDreamGestureEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerSwipe(EventData) : true;
}

void UDreamUserWidgetEventBridge::OnKeyDown_Implementation(UDreamKeyEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget != nullptr && EventData != nullptr && UserWidget->NativeOnKeyDown(EventData))
	{
		EventData->bHandled = true;
	}
}

void UDreamUserWidgetEventBridge::OnKeyUp_Implementation(UDreamKeyEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget != nullptr && EventData != nullptr && UserWidget->NativeOnKeyUp(EventData))
	{
		EventData->bHandled = true;
	}
}

void UDreamUserWidgetEventBridge::OnKeyChar_Implementation(UDreamKeyEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget != nullptr && EventData != nullptr && UserWidget->NativeOnKeyChar(EventData))
	{
		EventData->bHandled = true;
	}
}

void UDreamUserWidgetEventBridge::OnAnalogValueChanged_Implementation(UDreamKeyEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget != nullptr && EventData != nullptr && UserWidget->NativeOnAnalogValueChanged(EventData))
	{
		EventData->bHandled = true;
	}
}

void UDreamUserWidgetEventBridge::TickPointerMoveWatch()
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget == nullptr || !UserWidget->HasBlueprintPointerMove())
	{
		bHasWatchedPointerPoint = false;
		return;
	}
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(
		UserWidget, UserWidget->GetOwningPlayerIndex());
	UDreamPointerEventData* PointerEvent = IsValid(EventSystem) ? EventSystem->GetPointerEventData(0, false) : nullptr;
	// Over this widget means over it or anything inside it, which is what the enter stack records and
	// what "the mouse is over my button" means to the widget that owns the button.
	const bool bIsOverThisWidget = PointerEvent != nullptr
		&& PointerEvent->EnterWidgetStack.ContainsByPredicate(
			[UserWidget](const UDreamWidget* Entry) { return Entry == UserWidget; });
	if (!bIsOverThisWidget)
	{
		bHasWatchedPointerPoint = false;
		return;
	}
	const FVector CurrentPoint = PointerEvent->WorldPoint;
	if (bHasWatchedPointerPoint && CurrentPoint.Equals(LastWatchedPointerWorldPoint))
	{
		return;
	}
	const bool bWasMove = bHasWatchedPointerPoint;
	LastWatchedPointerWorldPoint = CurrentPoint;
	bHasWatchedPointerPoint = true;
	if (bWasMove)
	{
		// The first frame over the widget is the ENTER, which has its own event; a move needs a
		// previous position to be a move at all.
		UserWidget->NativeOnPointerMove(PointerEvent);
		// And the same move while a drag is overhead is UMG's OnDragOver -- the middle of a hover,
		// never its start, which is why it rides the move and not the enter.
		if (UDreamDragDropOperation* Operation = DreamUserWidgetBridgeLocal::LiveDragOperation(PointerEvent))
		{
			UserWidget->NativeOnDragOver(PointerEvent, Operation);
		}
	}
}
#pragma endregion

void UDreamUserWidget::FFieldNotificationClassDescriptor::ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const
{
	if (const UBlueprintGeneratedClass* BPClass = Cast<const UBlueprintGeneratedClass>(Class))
	{
		BPClass->ForEachFieldNotify(Callback, true);
	}
}

FDelegateHandle UDreamUserWidget::AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	return NotificationDelegates.AddFieldValueChangedDelegate(this, InFieldId, MoveTemp(InNewDelegate));
}

bool UDreamUserWidget::RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle)
{
	return NotificationDelegates.RemoveFieldValueChangedDelegate(this, InFieldId, InHandle);
}

int32 UDreamUserWidget::RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InUserObject);
}

int32 UDreamUserWidget::RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InFieldId, InUserObject);
}

const UE::FieldNotification::IClassDescriptor& UDreamUserWidget::GetFieldNotificationDescriptor() const
{
	static FFieldNotificationClassDescriptor Local;
	return Local;
}

void UDreamUserWidget::BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId)
{
	NotificationDelegates.BroadcastFieldValueChanged(this, InFieldId);
}

void UDreamUserWidget::K2_AddFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.AddFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamUserWidget::K2_RemoveFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.RemoveFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamUserWidget::BindEventBindings()
{
	TArray<FDreamWidgetEventBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectEventBindings(GetClass(), Bindings);
	for (const FDreamWidgetEventBinding& Binding : Bindings)
	{
		FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.WidgetName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}
		UDreamWidget* TargetWidget = Cast<UDreamWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(this));
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Binding.Target, Binding.BehaviourIndex);
		if (!IsValid(Target) || FindFunction(Binding.FunctionName) == nullptr)
		{
			// The compiler checked all of this; reaching here means the class moved underneath us,
			// which is the property bindings' rule too: skip, never guess.
			continue;
		}
		FProperty* EventProperty = Target->GetClass()->FindPropertyByName(Binding.EventName);
		// The OTHER kind of event this plugin has. `Controls/*` declares BlueprintAssignable dynamic
		// multicast delegates; the older `Interaction/*` behaviours declare FDreamUIEventDelegate
		// struct properties instead, and until this branch existed a `->` route could not name one --
		// which left that whole family of controls with no canonical way to be handled at all, and the
		// legacy per-instance event list as the only option. Both are routed the same way now.
		if (FStructProperty* StructEvent = CastField<FStructProperty>(EventProperty);
			StructEvent != nullptr && StructEvent->Struct == FDreamUIEventDelegate::StaticStruct())
		{
			if (FDreamUIEventDelegate* Delegate = StructEvent->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Target))
			{
				Delegate->AddRuntimeRoute(this, Binding.FunctionName);
			}
			continue;
		}
		FMulticastDelegateProperty* Event = CastField<FMulticastDelegateProperty>(EventProperty);
		if (Event == nullptr)
		{
			continue;
		}
		// AddDelegate, not Set: an event can have other listeners, and a route from the file is one
		// more of them, not a replacement. The delegate holds this widget weakly, so an instance
		// dying does not need an unbind pass -- broadcast skips dead entries.
		FScriptDelegate Route;
		Route.BindUFunction(this, Binding.FunctionName);
		Event->AddDelegate(MoveTemp(Route), Target);
	}
}

void UDreamUserWidget::ResolvePropertyBindings()
{
	ResolvedBindings.Reset();
	PolledBindingCount = 0;

	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(GetClass(), Bindings);
	if (Bindings.Num() == 0)
	{
		return;
	}

	// One subscription per distinct source field, no matter how many bindings read it; the handler
	// fans out to every binding carrying that id.
	TSet<int32> SubscribedFieldIndices;

	for (const FDreamWidgetPropertyBinding& Binding : Bindings)
	{
		// The target widget is reached the same way everything else reaches one: the class property
		// the compiler named after it, which InitializeWidgetStatic has already filled in.
		FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.WidgetName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}
		UDreamWidget* TargetWidget = Cast<UDreamWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(this));
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Binding.Target, Binding.BehaviourIndex);
		if (!IsValid(Target))
		{
			continue;
		}
		UFunction* SourceFunction = FindFunction(Binding.FunctionName);
		UFunction* Setter = Target->FindFunction(Binding.SetterName);
		if (SourceFunction == nullptr || Setter == nullptr)
		{
			continue;
		}

		FResolvedBinding& Resolved = ResolvedBindings.AddDefaulted_GetRef();
		Resolved.Target = Target;
		Resolved.SourceFunction = SourceFunction;
		Resolved.Setter = Setter;

		// A source function the class marked FieldNotify tells us when it changes; everything else
		// can change silently and stays on the per-frame poll. The classification is per instance
		// only because the resolution is -- the answer is a pure function of the class. A two-way
		// binding names its VARIABLE in NotifyField -- the function is only its generated getter,
		// which no broadcast will ever carry -- so the subscription keys on the variable instead.
		Resolved.SourceFieldId = GetFieldNotificationDescriptor().GetField(GetClass(),
			Binding.NotifyField.IsNone() ? Binding.FunctionName : Binding.NotifyField);
		if (Resolved.SourceFieldId.IsValid())
		{
			bool bAlreadySubscribed = false;
			SubscribedFieldIndices.Add(Resolved.SourceFieldId.GetIndex(), &bAlreadySubscribed);
			if (!bAlreadySubscribed)
			{
				// Bound to self: the delegate store lives on this same object, so lifetime is
				// co-terminal and no unbind pass is owed.
				AddFieldValueChangedDelegate(Resolved.SourceFieldId,
					FFieldValueChangedDelegate::CreateUObject(this, &UDreamUserWidget::HandleSourceFieldValueChanged));
			}
		}
		else
		{
			++PolledBindingCount;
		}
	}
}

void UDreamUserWidget::EvaluateBinding(const FResolvedBinding& Binding)
{
	// A `<->` binding's push can come straight back: the control's setter fires OnValueChanged,
	// the generated setter writes the variable, the FieldNotify broadcast re-enters here for the
	// very same binding with the value it was just handed. Controls without a ...WithoutNotify
	// setter only break that loop by early-outing on an equal value, and not all of them do.
	// Refusing to re-enter a binding that is mid-push ends the echo regardless of the control.
	if (Binding.bEvaluating)
	{
		return;
	}
	TGuardValue<bool> EvaluatingGuard(Binding.bEvaluating, true);

	UObject* Target = Binding.Target.Get();
	if (!IsValid(Target) || Binding.SourceFunction == nullptr || Binding.Setter == nullptr)
	{
		return;
	}

	// FStructOnScope rather than a raw buffer: a returned FText or FString has to be constructed
	// before ProcessEvent writes it and destroyed afterwards, and this does both.
	FStructOnScope SourceFrame(Binding.SourceFunction);
	ProcessEvent(Binding.SourceFunction, SourceFrame.GetStructMemory());

	FProperty* ReturnProperty = Binding.SourceFunction->GetReturnProperty();
	FProperty* SetterParameter = nullptr;
	for (TFieldIterator<FProperty> It(Binding.Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		SetterParameter = *It;
		break;
	}
	if (ReturnProperty == nullptr || SetterParameter == nullptr)
	{
		// The compiler checked this pairing; reaching here means the class moved underneath us.
		return;
	}

	FStructOnScope SetterFrame(Binding.Setter);
	// Through the shared conversion rather than a raw CopyCompleteValue, which is what makes the
	// compiler's widened check safe: an exact pair is still copied whole, and a numeric pair of two
	// different widths is converted instead of memcpy'd. Its false is the old SameType refusal --
	// the class moved underneath us -- and leaves the setter uncalled.
	if (!CopyDreamWidgetBoundValue(ReturnProperty,
		ReturnProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()),
		SetterParameter, SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory())))
	{
		return;
	}
	Target->ProcessEvent(Binding.Setter, SetterFrame.GetStructMemory());
}

void UDreamUserWidget::EvaluatePropertyBindings()
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		EvaluateBinding(Binding);
	}
}

void UDreamUserWidget::EvaluatePolledPropertyBindings()
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		if (!Binding.SourceFieldId.IsValid())
		{
			EvaluateBinding(Binding);
		}
	}
}

void UDreamUserWidget::HandleSourceFieldValueChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId)
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		if (Binding.SourceFieldId.IsValid() && Binding.SourceFieldId.GetName() == InFieldId.GetName())
		{
			EvaluateBinding(Binding);
		}
	}
}

void UDreamUserWidget::ResolveEachBindings()
{
	EachAdapters.Reset();

	TArray<FDreamWidgetEachBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectEachBindings(GetClass(), Bindings);
	if (Bindings.Num() == 0)
	{
		return;
	}

	TSet<int32> SubscribedFieldIndices;
	for (const FDreamWidgetEachBinding& Binding : Bindings)
	{
		auto FindWidgetByVariable = [this](FName InName) -> UDreamWidget*
		{
			FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), InName);
			return Property != nullptr ? Cast<UDreamWidget>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
		};
		UDreamWidget* Host = FindWidgetByVariable(Binding.HostWidgetName);
		UDreamWidget* Template = FindWidgetByVariable(Binding.TemplateWidgetName);
		UUIRecyclableScrollView* ListView = IsValid(Host) ? Host->GetComponent<UUIRecyclableScrollView>() : nullptr;
		if (!IsValid(ListView) || !IsValid(Template))
		{
			// The compiler and builder vetted all of this; the class moved underneath us. Skip.
			continue;
		}

		// The view's Content pointer was authored against the archetype; re-aim it at THIS
		// instance's content, the same per-instance re-wiring the template gets below. Without it
		// every cell the view clones lands in the invisible archetype tree. The synthesized
		// content may not have earned a class variable, so the template's own parent -- which IS
		// that content whenever the builder synthesized one -- is the fallback.
		if (!Binding.ContentWidgetName.IsNone())
		{
			UDreamWidget* Content = FindWidgetByVariable(Binding.ContentWidgetName);
			if (!IsValid(Content) && Template->GetParent() != Host)
			{
				Content = Template->GetParent();
			}
			if (IsValid(Content))
			{
				ListView->SetContent(Content);
			}
		}

		UDreamUIEachAdapter* Adapter = NewObject<UDreamUIEachAdapter>(this);
		Adapter->Initialize(this, Binding, ListView);
		EachAdapters.Add(Adapter);

		ListView->SetCellTemplate(Template);
		TScriptInterface<IUIRecyclableScrollViewDataSource> DataSource;
		DataSource.SetObject(Adapter);
		DataSource.SetInterface(Cast<IUIRecyclableScrollViewDataSource>(Adapter));
		ListView->SetDataSource(DataSource);

		// A variable source that broadcasts refreshes its list the way a FieldNotify binding
		// re-evaluates: from the change, not from a poll.
		if (!Binding.bSourceIsFunction)
		{
			const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), Binding.SourceName);
			if (FieldId.IsValid())
			{
				bool bAlreadySubscribed = false;
				SubscribedFieldIndices.Add(FieldId.GetIndex(), &bAlreadySubscribed);
				if (!bAlreadySubscribed)
				{
					AddFieldValueChangedDelegate(FieldId,
						FFieldValueChangedDelegate::CreateUObject(this, &UDreamUserWidget::HandleEachSourceChanged));
				}
			}
		}
	}
}

void UDreamUserWidget::HandleEachSourceChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId)
{
	for (UDreamUIEachAdapter* Adapter : EachAdapters)
	{
		if (IsValid(Adapter) && !Adapter->GetBinding().bSourceIsFunction
			&& Adapter->GetBinding().SourceName == InFieldId.GetName())
		{
			Adapter->Refresh();
		}
	}
}

void UDreamUserWidget::RefreshEachBindings()
{
	for (UDreamUIEachAdapter* Adapter : EachAdapters)
	{
		if (IsValid(Adapter))
		{
			Adapter->Refresh();
		}
	}
}

UDreamWidget* UDreamUserWidget::GetContentRoot() const
{
	return IsValid(WidgetTree) ? WidgetTree->RootWidget : nullptr;
}

void UDreamUserWidget::CollectDeclaredSlotNames(const UDreamWidgetTree* InTree, TArray<FName>& OutNames)
{
	if (!IsValid(InTree))
	{
		return;
	}
	InTree->ForEachWidget([&OutNames](UDreamWidget* Widget)
	{
		if (const UDreamNamedSlot* Slot = Widget->GetComponent<UDreamNamedSlot>())
		{
			const FName SlotName = Slot->GetSlotName();
			// A duplicate slot name is a mistake the class author has to see; the compiler reports it
			// (DreamWidgetBlueprintCompiler). Listing it once here keeps every consumer agreeing on
			// what the class offers rather than each de-duplicating differently.
			if (!SlotName.IsNone())
			{
				OutNames.AddUnique(SlotName);
			}
		}
	});
}

void UDreamUserWidget::CollectDeclaredSlotNames(const UClass* InClass, TArray<FName>& OutNames)
{
	if (InClass == nullptr || !InClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		return;
	}
	// The archetype's slots first, so an archetype-built class is answered exactly as before.
	CollectDeclaredSlotNames(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass), OutNames);

	// Then what the class declares for itself. The CDO, because this is a question about the class
	// and is asked before any instance exists -- the compiler asks it, and so does a designer
	// hierarchy row for a control nobody has placed yet.
	if (const UDreamUserWidget* Defaults = InClass->GetDefaultObject<UDreamUserWidget>())
	{
		for (const FName& SlotName : Defaults->GetNativeSlotNames())
		{
			if (!SlotName.IsNone())
			{
				OutNames.AddUnique(SlotName);
			}
		}
	}
}

TArray<FName> UDreamUserWidget::GetNativeSlotNames() const
{
	// Nothing by default: a class built from an archetype declares its slots in that archetype, and
	// answering for it here would be a second source for the same question.
	return TArray<FName>();
}

FName UDreamUserWidget::GetDefaultSlotName() const
{
	return NAME_None;
}

UDreamWidget* UDreamUserWidget::FindSlotWidget(FName InSlotName) const
{
	if (InSlotName.IsNone())
	{
		return nullptr;
	}
	UDreamWidget* Found = nullptr;
	const auto Consider = [&Found, InSlotName](UDreamWidget* Widget)
	{
		if (Found != nullptr || !IsValid(Widget))
		{
			return;
		}
		if (const UDreamNamedSlot* Slot = Widget->GetComponent<UDreamNamedSlot>())
		{
			if (Slot->GetSlotName() == InSlotName)
			{
				Found = Widget;
			}
		}
	};

	if (IsValid(WidgetTree))
	{
		WidgetTree->ForEachWidget(Consider);
		return Found;
	}

	// A native control: no tree object, because nothing instanced a template to make its contents --
	// it built them under itself. A plain structural walk, and NOT CollectDreamWidgetsToNestedBoundary,
	// whose children are editor-semantic: for a nested instance that function returns the very slot
	// rows this function is being asked for, so using it here would ask the question to answer it.
	//
	// Stopping at a nested instance is the same boundary either way. A slot inside a Button placed
	// inside this control is that Button's hole, and answering with it would let a host fill it from
	// outside the asset that opened it.
	TArray<UDreamWidget*> Pending(GetChildren());
	while (Pending.Num() > 0 && Found == nullptr)
	{
		UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
		if (!IsValid(Widget))
		{
			continue;
		}
		Consider(Widget);
		if (!Widget->IsA<UDreamUserWidget>())
		{
			Pending.Append(Widget->GetChildren());
		}
	}
	return Found;
}

void UDreamUserWidget::AttachNamedSlotContent()
{
	// The content objects belong to the host's tree and arrived with it; all that is left is to hang
	// each under the UDreamNamedSlot of that name inside this instance. Done by this widget rather
	// than by the host, because only it knows where its own slots are.
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& Binding : NamedSlotContent)
	{
		UDreamWidget* Content = Binding.Value;
		if (!IsValid(Content))
		{
			continue;
		}
		UDreamWidget* SlotWidget = FindSlotWidget(Binding.Key);
		if (!IsValid(SlotWidget))
		{
			// The class dropped or renamed a slot the host still binds. Silently discarding it is how
			// content disappears from a screen with nothing in the log to say why; the compiler
			// reports this as an error on the host too, but a class can change after that compile.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has no slot named '%s'; the content bound to it is not shown."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetClass()->GetName(), *Binding.Key.ToString());
			continue;
		}
		if (Content->HasRegistered())
		{
			Content->TrySetParent(SlotWidget, false);
		}
		else
		{
			Content->SetParentBeforeRegister(SlotWidget);
		}
	}
}

void UDreamUserWidget::AdoptUnslottedChildren()
{
	const FName DefaultSlot = GetDefaultSlotName();
	if (DefaultSlot.IsNone() || HostSuppliedChildren.Num() == 0)
	{
		return;
	}
	UDreamWidget* SlotWidget = FindSlotWidget(DefaultSlot);
	if (!IsValid(SlotWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' names '%s' as its default slot but opens no slot of that name."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetClass()->GetName(), *DefaultSlot.ToString());
		return;
	}
	for (UDreamWidget* Content : HostSuppliedChildren)
	{
		// Still a child of this widget means no named binding claimed it: AttachNamedSlotContent
		// re-parents what it places, so anything it took is already somewhere else.
		if (!IsValid(Content) || Content->GetParent() != this || SlotWidget->IsChildOf(Content))
		{
			continue;
		}
		if (Content->HasRegistered())
		{
			// Try, not Set: a refusal is real (a cycle, or a slot already holding its one child) and
			// silent otherwise -- the content would vanish from a hierarchy that still looks right.
			// World position dropped on purpose, the same call AddChild makes: content handed to a
			// slot is handed to that slot's arrangement.
			if (!Content->TrySetParent(SlotWidget, false))
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused '%s' as default-slot content."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SlotWidget->GetDisplayName(), *Content->GetDisplayName());
			}
		}
		else
		{
			Content->SetParentBeforeRegister(SlotWidget);
		}
	}
}

UDreamWidget* UDreamUserWidget::GetContentForNamedSlot(FName InSlotName) const
{
	const TObjectPtr<UDreamWidget>* Found = NamedSlotContent.Find(InSlotName);
	return Found != nullptr && IsValid(*Found) ? Found->Get() : nullptr;
}

bool UDreamUserWidget::SetContentForNamedSlot(FName InSlotName, UDreamWidget* InContent)
{
	if (InSlotName.IsNone())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot bind content to an unnamed slot on '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
		return false;
	}
	if (InContent == nullptr)
	{
		NamedSlotContent.Remove(InSlotName);
		return true;
	}
	if (InContent == this || InContent->IsChildOf(this) || this->IsChildOf(InContent))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot go into a slot of '%s': one contains the other."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InContent->GetPathDisplayName(), *GetPathDisplayName());
		return false;
	}
	// The content must be the host's own. Taking a widget out of a third asset would make this a
	// cross-asset difference record, which is the thing P4 deleted and is not coming back.
	if (InContent->GetTypedOuter<UDreamWidgetTree>() != this->GetTypedOuter<UDreamWidgetTree>())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' belongs to another hierarchy; a slot is filled by the host that placed '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InContent->GetPathDisplayName(), *GetPathDisplayName());
		return false;
	}
	NamedSlotContent.Add(InSlotName, InContent);
	return true;
}

UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive)
{
	if (!IsValid(InWorld))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid world."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (!IsValid(InClass))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid class."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	// Same ownership rule a prefab load follows: join the parent's tree, or mint one outered to the
	// world so GetTypedOuter<UWorld> resolves for everything inside.
	UObject* Owner = nullptr;
	UDreamWidgetTree* OwnedTree = nullptr;
	if (IsValid(InParent) && InParent->GetOuter() != nullptr)
	{
		Owner = InParent->GetOuter();
	}
	else
	{
		OwnedTree = NewObject<UDreamWidgetTree>(InWorld);
		Owner = OwnedTree;
	}

	UDreamUserWidget* UserWidget = NewObject<UDreamUserWidget>(Owner, InClass, NAME_None, RF_Transactional);
	if (OwnedTree != nullptr)
	{
		OwnedTree->RootWidget = UserWidget;
	}
	UserWidget->Initialize();
	// Parent first, then registration: OnRegister reconciles the panel slot against the parent, so
	// registering an orphan and attaching it afterwards produces a widget the parent never laid out.
	if (IsValid(InParent))
	{
		UserWidget->SetParentBeforeRegister(InParent);
	}
	// Last chance to reshape what was built before anything observes it. See the header.
	if (InCallbackBeforeAlive)
	{
		InCallbackBeforeAlive(UserWidget);
	}
	RegisterDreamWidgetHierarchy(UserWidget);
	return UserWidget;
}

// ---------------------------------------------------------------------------------- animation
//
// Forwarding, not reimplementation. Every one of these finds the UDreamWidgetAnimationComponent
// that owns the thing it was handed and calls the component's own method, because the component's
// bookkeeping (its live players, its finished delegates) is what makes stopping and querying work.
// Routing a handle to the WRONG component would not error -- the component's guard would simply
// find the player is not one of its own and do nothing at all -- so the outer chain, which is
// exact, is used in preference to searching.

void UDreamUserWidget::CollectAnimationComponents(TArray<UDreamWidgetAnimationComponent*>& OutComponents) const
{
	// This widget's OWN components first. GetContentRoot is the root of the tree this widget builds
	// and is never the widget itself, so an animation component placed directly on the user widget was
	// invisible to everything routed through here -- StopAllAnimations, IsAnyAnimationPlaying,
	// FlushAnimations, QueueStopAllAnimations, PlayAnimationByName -- even though PlayAnimation finds
	// it through the outer chain and GetOwningUserWidget recognises that placement on purpose.
	for (UDreamUIBehaviour* Component : GetAllComponents())
	{
		if (UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component))
		{
			OutComponents.Add(Animator);
		}
	}

	UDreamWidget* ContentRoot = GetContentRoot();
	if (!IsValid(ContentRoot))
	{
		// Every animation verb on this widget routes through here, so with no contents of its own
		// all six of them are no-ops -- and five of the six did that in complete silence, which is
		// how "my list cell's animation never plays" became unanswerable from a log. Two ways to
		// land here: a widget that was never initialized (a hand-made NewObject, or a duplicate
		// that has not been through Initialize), and a native control, whose tree is null by design
		// and whose animations live on the control's own components rather than in a tree.
		if (!bWarnedMissingAnimationRoot)
		{
			bWarnedMissingAnimationRoot = true;
			UE_LOG(DreamGUI, Warning,
				TEXT("Animation API used on '%s', which has no content root, so it does nothing. Initialized: %s."),
				*GetPathName(), bInitialized ? TEXT("yes, this class declares no hierarchy of its own") : TEXT("no"));
		}
		return;
	}
	// To the nested boundary: a nested instance's animations are its own to stop and to name, and
	// it exposes this same API for doing it.
	TArray<UDreamWidget*> Widgets;
	CollectDreamWidgetsToNestedBoundary(ContentRoot, Widgets);
	for (UDreamWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component))
			{
				OutComponents.Add(Animator);
			}
		}
	}
}

UDreamWidgetAnimationComponent* UDreamUserWidget::FindAnimationComponentFor(UMovieSceneSequence* InAnimation) const
{
	if (!IsValid(InAnimation))
	{
		return nullptr;
	}
	// An embedded animation is a sub-object of its component, so the outer chain names the owner
	// outright -- and names the INSTANCED component when handed the instanced animation, which is
	// what the generated variables hold.
	if (UDreamWidgetAnimationComponent* Owner = InAnimation->GetTypedOuter<UDreamWidgetAnimationComponent>())
	{
		return Owner;
	}
	// A standalone sequence asset is outer'd to its own package, so it has to be looked for among
	// the components that reference it.
	UDreamUISequence* Asset = Cast<UDreamUISequence>(InAnimation);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		if (Component->GetSequenceAssets().Contains(Asset))
		{
			return Component;
		}
	}
	return nullptr;
}

UMovieSceneSequence* UDreamUserWidget::GetAnimationByName(const FString& Name) const
{
	// The same two-step lookup PlayAnimationByName does: embedded animations answer to their display
	// name, standalone sequence assets to their asset name.
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		if (UDreamWidgetAnimation* Embedded = Component->GetSequenceByDisplayName(Name))
		{
			return Embedded;
		}
		for (UDreamUISequence* Asset : Component->GetSequenceAssets())
		{
			if (IsValid(Asset) && Asset->GetName() == Name)
			{
				return Asset;
			}
		}
	}
	return nullptr;
}

FDreamUIAnimationHandle UDreamUserWidget::PlayAnimation(
	UMovieSceneSequence* Animation,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation);
	if (Component == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("PlayAnimation on '%s': '%s' does not belong to this widget."),
			*GetPathName(), IsValid(Animation) ? *Animation->GetName() : TEXT("(none)"));
		return FDreamUIAnimationHandle();
	}
	return Component->PlayAnimation(Animation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamUserWidget::PlayAnimationByName(
	const FString& Name,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		// Asked of the component that HAS it, so a component without it stays silent instead of
		// logging a not-found for every animation this widget owns but that one does not.
		if (Component->GetSequenceByDisplayName(Name) != nullptr)
		{
			return Component->PlayAnimationByDisplayName(Name, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
		}
	}
	// Not embedded anywhere: the component's own fallback covers the standalone assets, and the
	// first component is as good an owner as any for a name none of them claimed.
	if (Animators.Num() > 0)
	{
		return Animators[0]->PlayAnimationByDisplayName(Name, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
	}
	UE_LOG(DreamGUI, Warning, TEXT("PlayAnimationByName on '%s': no animation component in this widget."), *GetPathName());
	return FDreamUIAnimationHandle();
}

namespace
{
	/** The component a handle's player was created under; see the note above. */
	UDreamWidgetAnimationComponent* ComponentForHandle(const FDreamUIAnimationHandle& InHandle)
	{
		return IsValid(InHandle.Player) ? InHandle.Player->GetTypedOuter<UDreamWidgetAnimationComponent>() : nullptr;
	}
}

UDreamWidgetAnimationComponent* UDreamUserWidget::RequireAnimationComponentFor(UMovieSceneSequence* InAnimation, const TCHAR* Caller) const
{
	UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(InAnimation);
	if (Component == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("%s on '%s': '%s' does not belong to this widget."),
			Caller, *GetPathName(), IsValid(InAnimation) ? *InAnimation->GetName() : TEXT("(none)"));
	}
	return Component;
}

FDreamUIAnimationHandle UDreamUserWidget::PlayAnimationTimeRange(
	UMovieSceneSequence* Animation,
	float StartAtTime,
	float EndAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("PlayAnimationTimeRange"));
	return Component != nullptr
		? Component->PlayAnimationTimeRange(Animation, StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState)
		: FDreamUIAnimationHandle();
}

FDreamUIAnimationHandle UDreamUserWidget::PlayAnimationForward(UMovieSceneSequence* Animation, float PlaybackSpeed, bool bRestoreState)
{
	UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("PlayAnimationForward"));
	return Component != nullptr ? Component->PlayAnimationForward(Animation, PlaybackSpeed, bRestoreState) : FDreamUIAnimationHandle();
}

FDreamUIAnimationHandle UDreamUserWidget::PlayAnimationReverse(UMovieSceneSequence* Animation, float PlaybackSpeed, bool bRestoreState)
{
	UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("PlayAnimationReverse"));
	return Component != nullptr ? Component->PlayAnimationReverse(Animation, PlaybackSpeed, bRestoreState) : FDreamUIAnimationHandle();
}

void UDreamUserWidget::QueuePlayAnimation(UMovieSceneSequence* Animation, float StartAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	if (UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("QueuePlayAnimation")))
	{
		Component->QueuePlayAnimation(Animation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
	}
}

void UDreamUserWidget::QueuePlayAnimationTimeRange(UMovieSceneSequence* Animation, float StartAtTime, float EndAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	if (UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("QueuePlayAnimationTimeRange")))
	{
		Component->QueuePlayAnimationTimeRange(Animation, StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
	}
}

void UDreamUserWidget::QueueStopAnimation(FDreamUIAnimationHandle Handle)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->QueueStopAnimation(Handle);
	}
}

void UDreamUserWidget::QueueStopAllAnimations()
{
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		Component->QueueStopAllAnimations();
	}
}

void UDreamUserWidget::QueuePauseAnimation(FDreamUIAnimationHandle Handle)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->QueuePauseAnimation(Handle);
	}
}

float UDreamUserWidget::PauseAnimation(FDreamUIAnimationHandle Handle)
{
	UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle);
	return Component != nullptr ? Component->PauseAnimation(Handle) : 0.0f;
}

void UDreamUserWidget::ResumeAnimation(FDreamUIAnimationHandle Handle)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->ResumeAnimation(Handle);
	}
}

void UDreamUserWidget::StopAnimation(FDreamUIAnimationHandle Handle)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->StopAnimation(Handle);
	}
}

void UDreamUserWidget::ReverseAnimation(FDreamUIAnimationHandle Handle)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->ReverseAnimation(Handle);
	}
}

bool UDreamUserWidget::IsAnimationPlaying(FDreamUIAnimationHandle Handle) const
{
	UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle);
	return Component != nullptr && Component->IsAnimationPlaying(Handle);
}

bool UDreamUserWidget::IsAnimationPaused(FDreamUIAnimationHandle Handle) const
{
	UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle);
	return Component != nullptr && Component->IsAnimationPaused(Handle);
}

bool UDreamUserWidget::IsAnimationPlayingForward(FDreamUIAnimationHandle Handle) const
{
	UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle);
	return Component != nullptr && Component->IsAnimationPlayingForward(Handle);
}

float UDreamUserWidget::GetAnimationCurrentTime(FDreamUIAnimationHandle Handle) const
{
	UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle);
	return Component != nullptr ? Component->GetAnimationCurrentTime(Handle) : 0.0f;
}

void UDreamUserWidget::SetAnimationCurrentTime(FDreamUIAnimationHandle Handle, float InTime)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->SetAnimationCurrentTime(Handle, InTime);
	}
}

void UDreamUserWidget::SetNumLoopsToPlay(FDreamUIAnimationHandle Handle, int32 NumLoopsToPlay)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->SetNumLoopsToPlay(Handle, NumLoopsToPlay);
	}
}

void UDreamUserWidget::SetPlaybackSpeed(FDreamUIAnimationHandle Handle, float PlaybackSpeed)
{
	if (UDreamWidgetAnimationComponent* Component = ComponentForHandle(Handle))
	{
		Component->SetPlaybackSpeed(Handle, PlaybackSpeed);
	}
}

FDreamUIAnimationHandle UDreamUserWidget::FindAnimationInstance(UMovieSceneSequence* Animation) const
{
	UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation);
	return Component != nullptr ? Component->FindAnimationInstance(Animation) : FDreamUIAnimationHandle();
}

bool UDreamUserWidget::HasPlayingAnimation(UMovieSceneSequence* Animation) const
{
	UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation);
	return Component != nullptr && Component->HasPlayingAnimation(Animation);
}

void UDreamUserWidget::StopAnimationsOf(UMovieSceneSequence* Animation)
{
	if (UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation))
	{
		Component->StopAnimationsOf(Animation);
	}
}

float UDreamUserWidget::PauseAnimationsOf(UMovieSceneSequence* Animation)
{
	UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation);
	return Component != nullptr ? Component->PauseAnimationsOf(Animation) : 0.0f;
}

bool UDreamUserWidget::IsAnyAnimationPlaying() const
{
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		if (Component->IsAnyAnimationPlaying())
		{
			return true;
		}
	}
	return false;
}

void UDreamUserWidget::StopAllAnimations()
{
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		Component->StopAllAnimations();
	}
}

void UDreamUserWidget::FlushAnimations()
{
	TArray<UDreamWidgetAnimationComponent*> Animators;
	CollectAnimationComponents(Animators);
	for (UDreamWidgetAnimationComponent* Component : Animators)
	{
		Component->FlushAnimations();
	}
}

void UDreamUserWidget::BindToAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	BindToAnimationEvent(Animation, Delegate, EDreamUIAnimationEvent::Started);
}

void UDreamUserWidget::UnbindFromAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	if (UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation))
	{
		Component->UnbindFromAnimationStarted(Animation, Delegate);
	}
}

void UDreamUserWidget::UnbindAllFromAnimationStarted(UMovieSceneSequence* Animation)
{
	if (UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation))
	{
		Component->UnbindAllFromAnimationStarted(Animation);
	}
}

void UDreamUserWidget::BindToAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	BindToAnimationEvent(Animation, Delegate, EDreamUIAnimationEvent::Finished);
}

void UDreamUserWidget::UnbindFromAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	if (UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation))
	{
		Component->UnbindFromAnimationFinished(Animation, Delegate);
	}
}

void UDreamUserWidget::UnbindAllFromAnimationFinished(UMovieSceneSequence* Animation)
{
	if (UDreamWidgetAnimationComponent* Component = FindAnimationComponentFor(Animation))
	{
		Component->UnbindAllFromAnimationFinished(Animation);
	}
}

void UDreamUserWidget::BindToAnimationEvent(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate, EDreamUIAnimationEvent AnimationEvent)
{
	if (UDreamWidgetAnimationComponent* Component = RequireAnimationComponentFor(Animation, TEXT("BindToAnimationEvent")))
	{
		Component->BindToAnimationEvent(Animation, Delegate, AnimationEvent);
	}
}

void UDreamUserWidget::NotifyAnimationStarted(UMovieSceneSequence* Animation)
{
	OnAnimationStarted(Animation);
}

void UDreamUserWidget::NotifyAnimationFinished(UMovieSceneSequence* Animation)
{
	OnAnimationFinished(Animation);
}

void UDreamUserWidget::NotifyAnimationEvent(FName EventName)
{
	OnAnimationEvent.Broadcast(EventName);
}
