// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUITabOrder.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamUIScrollbarInterface.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/UISelectable.h"

/*
 * Tab order is the widget tree, depth first: a sequence that reverses exactly, as a browser's and Slate's do. It used to be
 * "the nearest control to the right, else the nearest below", which skipped grid cells, jumped from the first field of a
 * column to a right-aligned button, never reversed and never wrapped.
 *
 * One walk of the part of the tree a press is held in lists its ENTRIES in Tab order -- the stops, and each single-stop
 * container (TabNavigation Once) once, as itself -- and notes where the start fell among them: an entry, or a place between
 * two when the start is no stop (a widget focused by a click, or with bIsTabStop off). The step is then the entry after
 * that place, or before it backwards.
 */
namespace DreamUITabOrderLocal
{
	/** How deep a chain of single-stop containers inside one another is entered before the walk settles where it is. */
	constexpr int32 MaxTabEntryDepth = 8;

	/**
	 * One walk of a subtree in Tab order, and what it found. The world's facts are read once a walk, and the start's chain of
	 * ancestors is a set, so whether a node holds the start is one look-up.
	 */
	struct FTabWalk
	{
		const UDreamWidget* Start = nullptr;
		TSet<const UDreamWidget*> StartChain;
		/** Every player's tooltip bubble: nothing in one is a stop. */
		TArray<const UDreamWidget*, TInlineAllocator<2>> TooltipBubbles;
		/**
		 * Every open popup the walk is not inside: another player's list or menu, lifted onto a screen root players can
		 * share, or the player's own behind a dialog the focus is on. Nothing in one is a stop of this walk; a popup domain
		 * walks its own popup.
		 */
		TArray<const UDreamWidget*, TInlineAllocator<4>> OutsidePopups;
		/** The world's UI has begun play: a stop has then begun play too, else it is registered. */
		bool bWorldPlays = false;
		/** Stop the walk once this many entries are found: 1 asks whether there is any, 0 wants them all. */
		int32 MaxEntries = 0;

		/** The stops and single-stop containers, in Tab order. */
		TArray<UDreamWidget*> Entries;
		/** The entry that is the start, or the single-stop container that holds it. */
		int32 StartEntry = INDEX_NONE;
		/** When the start is no entry and inside none: how many entries come before it in Tab order. */
		int32 StartInsertion = INDEX_NONE;

		bool IsFull() const { return MaxEntries > 0 && Entries.Num() >= MaxEntries; }
	};

	void GatherTooltipBubbles(const UWorld* InWorld, TArray<const UDreamWidget*, TInlineAllocator<2>>& OutBubbles)
	{
		const UDreamUITooltipSubsystem* Tooltips = InWorld != nullptr ? UDreamUITooltipSubsystem::Get(InWorld) : nullptr;
		const UDreamUIInputServices* Services = InWorld != nullptr ? UDreamUIInputServices::Get(InWorld) : nullptr;
		if (Tooltips == nullptr || Services == nullptr)
		{
			return;
		}
		TArray<int32> UserIndices;
		Services->GetUserIndices(UserIndices);
		for (const int32 UserIndex : UserIndices)
		{
			if (const UDreamWidget* Bubble = Tooltips->GetBubbleForUser(UserIndex); IsValid(Bubble))
			{
				OutBubbles.Add(Bubble);
			}
		}
	}

	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot);

	/** Every player's open popup InRoot is not inside (FTabWalk::OutsidePopups). */
	void GatherOutsidePopups(const UWorld* InWorld, const UDreamWidget* InRoot, TArray<const UDreamWidget*, TInlineAllocator<4>>& OutPopups)
	{
		const UDreamUIPopupLayer* Popups = InWorld != nullptr ? UDreamUIPopupLayer::Get(InWorld) : nullptr;
		const UDreamUIInputServices* Services = InWorld != nullptr ? UDreamUIInputServices::Get(InWorld) : nullptr;
		if (Popups == nullptr || Services == nullptr)
		{
			return;
		}
		TArray<int32> UserIndices;
		Services->GetUserIndices(UserIndices);
		TArray<UDreamWidget*> Open;
		for (const int32 UserIndex : UserIndices)
		{
			Popups->GetOpenPopups(UserIndex, Open);
			for (const UDreamWidget* Popup : Open)
			{
				if (IsValid(Popup) && !IsInside(InRoot, Popup))
				{
					OutPopups.AddUnique(Popup);
				}
			}
		}
	}

	void PrepareWalk(FTabWalk& OutWalk, const UDreamWidget* InRoot, const UDreamWidget* InStart)
	{
		UWorld* World = InRoot != nullptr ? InRoot->GetWorld() : nullptr;
		const UDreamUIManagerWorldSubsystem* Manager = World != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(World) : nullptr;
		OutWalk.bWorldPlays = Manager != nullptr && Manager->HasBegunPlay();
		GatherTooltipBubbles(World, OutWalk.TooltipBubbles);
		GatherOutsidePopups(World, InRoot, OutWalk.OutsidePopups);
		OutWalk.Start = InStart;
		for (const UDreamWidget* Walker = InStart; Walker != nullptr; Walker = Walker->GetParent())
		{
			OutWalk.StartChain.Add(Walker);
			if (Walker == InRoot)
			{
				break;
			}
		}
	}

	/** In play as the focus-return paths read it: begun play in a world whose UI has, registered in one that never began. */
	bool IsInPlay(const UDreamWidget* InWidget, bool bInWorldPlays)
	{
		return bInWorldPlays ? InWidget->HasBegunPlay() : InWidget->HasRegistered();
	}

	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot)
	{
		return IsValid(InWidget) && IsValid(InRoot) && (InWidget == InRoot || InWidget->IsChildOf(InRoot));
	}

	bool IsNavigable(UDreamWidget* InWidget)
	{
		return IsValid(InWidget) && DreamUINavigationScan::CanNavigateTo(DreamUINavigationScan::FindNavigationBehaviour(InWidget));
	}

	/**
	 * Nothing in InNode's subtree is a stop, InNode included: inactive, not drawn, a container whose TabNavigation is None, a
	 * scroll bar (its track and handle are dragged and stepped by the arrows, never Tabbed to), a tooltip's bubble, or an open
	 * popup the walk is not inside.
	 */
	bool IsClosedSubtree(const UDreamWidget* InNode, const FTabWalk& InWalk)
	{
		if (!InNode->GetWidgetActiveInHierarchy() || !InNode->GetRenderVisibleInHierarchy() || InNode->IsParked())
		{
			return true;
		}
		if (InNode->GetTabNavigation() == EDreamWidgetTabNavigation::None)
		{
			return true;
		}
		if (InNode->GetComponentByInterface(UDreamUIScrollbarInterface::StaticClass()) != nullptr)
		{
			return true;
		}
		return InWalk.TooltipBubbles.Contains(InNode) || InWalk.OutsidePopups.Contains(InNode);
	}

	/** InNode's own stop, its subtree already known open: see FDreamUITabOrder's class comment. */
	bool IsStopHere(UDreamWidget* InNode, const FTabWalk& InWalk)
	{
		if (!InNode->GetIsTabStop() || !InNode->GetIsFocusable() || !IsInPlay(InNode, InWalk.bWorldPlays)
			|| !InNode->GetInteractableInHierarchy())
		{
			return false;
		}
		if (!IsNavigable(InNode))
		{
			return false;
		}
		// On screen, or one scroll from it -- the step reveals it. Hidden behind a mask for any other reason is no stop.
		const FVector2D LocalCenter = InNode->GetLocalSpaceCenter();
		const FVector CenterInWorld = InNode->GetWorldTransform().TransformPosition(FVector(0.0, LocalCenter.X, LocalCenter.Y));
		return InNode->IsPointVisibleOnClip(CenterInWorld) || FDreamUINavigationScroll::IsReachableByScrolling(InNode);
	}

	void NoteStartInsertion(FTabWalk& InOutWalk)
	{
		if (InOutWalk.StartEntry == INDEX_NONE && InOutWalk.StartInsertion == INDEX_NONE)
		{
			InOutWalk.StartInsertion = InOutWalk.Entries.Num();
		}
	}

	void WalkNode(UDreamWidget* InNode, bool bInIsWalkRoot, FTabWalk& InOutWalk);

	/** InNode's children in Tab order: stably by TabIndex, so siblings with the same index keep the hierarchy's order. */
	void WalkChildren(UDreamWidget* InNode, FTabWalk& InOutWalk)
	{
		const TArray<UDreamWidget*>& Children = InNode->GetChildren();
		bool bAnyTabIndex = false;
		for (const UDreamWidget* Child : Children)
		{
			if (IsValid(Child) && Child->GetTabIndex() != 0)
			{
				bAnyTabIndex = true;
				break;
			}
		}
		if (!bAnyTabIndex)
		{
			for (UDreamWidget* Child : Children)
			{
				WalkNode(Child, false, InOutWalk);
				if (InOutWalk.IsFull())
				{
					return;
				}
			}
			return;
		}
		TArray<UDreamWidget*, TInlineAllocator<16>> Ordered;
		Ordered.Reserve(Children.Num());
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				Ordered.Add(Child);
			}
		}
		Ordered.StableSort([](const UDreamWidget& A, const UDreamWidget& B)
		{
			return A.GetTabIndex() < B.GetTabIndex();
		});
		for (UDreamWidget* Child : Ordered)
		{
			WalkNode(Child, false, InOutWalk);
			if (InOutWalk.IsFull())
			{
				return;
			}
		}
	}

	/** Whether there is any stop below InNode, nested single-stop containers counting. */
	bool HasStopInside(UDreamWidget* InNode, const FTabWalk& InOuter)
	{
		FTabWalk Inner;
		Inner.TooltipBubbles = InOuter.TooltipBubbles;
		Inner.OutsidePopups = InOuter.OutsidePopups;
		Inner.bWorldPlays = InOuter.bWorldPlays;
		Inner.MaxEntries = 1;
		WalkChildren(InNode, Inner);
		return Inner.Entries.Num() > 0;
	}

	void WalkNode(UDreamWidget* InNode, bool bInIsWalkRoot, FTabWalk& InOutWalk)
	{
		if (!IsValid(InNode) || InOutWalk.IsFull())
		{
			return;
		}
		const bool bHoldsStart = InOutWalk.Start != nullptr && InOutWalk.StartChain.Contains(InNode);
		if (IsClosedSubtree(InNode, InOutWalk))
		{
			// A start in there is placed where its subtree stands: the next Tab goes to what follows the subtree.
			if (bHoldsStart)
			{
				NoteStartInsertion(InOutWalk);
			}
			return;
		}
		// One stop where it stands, when there is anything to enter: itself, or a stop inside. The walk that enters it is
		// the step's (EnterSingleStop), so nothing here asks the container to scroll or build anything.
		if (!bInIsWalkRoot && InNode->GetTabNavigation() == EDreamWidgetTabNavigation::Once)
		{
			if (IsStopHere(InNode, InOutWalk) || HasStopInside(InNode, InOutWalk))
			{
				if (bHoldsStart)
				{
					InOutWalk.StartEntry = InOutWalk.Entries.Num();
				}
				InOutWalk.Entries.Add(InNode);
			}
			else if (bHoldsStart)
			{
				NoteStartInsertion(InOutWalk);
			}
			return;
		}
		// The widget's own stop comes before those inside it.
		if (IsStopHere(InNode, InOutWalk))
		{
			if (InNode == InOutWalk.Start)
			{
				InOutWalk.StartEntry = InOutWalk.Entries.Num();
			}
			InOutWalk.Entries.Add(InNode);
			if (InOutWalk.IsFull())
			{
				return;
			}
		}
		else if (InNode == InOutWalk.Start)
		{
			NoteStartInsertion(InOutWalk);
		}
		WalkChildren(InNode, InOutWalk);
	}

	/** Every entry of InRoot's subtree, InRoot walked as a plain container whatever its own TabNavigation says. */
	void WalkAll(UDreamWidget* InRoot, const UDreamWidget* InStart, int32 InMaxEntries, FTabWalk& OutWalk)
	{
		PrepareWalk(OutWalk, InRoot, InStart);
		OutWalk.MaxEntries = InMaxEntries;
		WalkNode(InRoot, /*bInIsWalkRoot*/true, OutWalk);
	}

	/** Whether InDomain goes round at its ends: its root's own Cycle or Contained when it says so, else the domain's rule. */
	bool DomainWraps(const FDreamUITabDomain& InDomain)
	{
		const UDreamWidget* Root = InDomain.GetRoot();
		const EDreamWidgetTabNavigation Mode = Root != nullptr ? Root->GetTabNavigation() : EDreamWidgetTabNavigation::Continue;
		if (Mode == EDreamWidgetTabNavigation::Cycle)
		{
			return true;
		}
		if (Mode == EDreamWidgetTabNavigation::Contained)
		{
			return false;
		}
		return InDomain.bWraps;
	}

	/**
	 * The entry after InFrom (before it backwards) inside the part of InDomain its Tab is held in -- its innermost Cycle or
	 * Contained container above every single-stop container it is in, else the whole domain. At that part's end: round to
	 * its other end when it wraps or bInForceWrap says so, else null. bOutRanOffEnd says the end was reached, whichever way
	 * that went. InFrom is inside the domain.
	 */
	UDreamWidget* FindNextEntry(const FDreamUITabDomain& InDomain, const UDreamWidget* InFrom, bool bInBackward, bool bInForceWrap,
		bool& bOutRanOffEnd)
	{
		bOutRanOffEnd = false;
		UDreamWidget* Root = InDomain.GetRoot();
		// Above every single-stop container InFrom is in: the next Tab leaves one, whatever is inside it.
		const UDreamWidget* SearchFrom = InFrom;
		for (const UDreamWidget* Walker = InFrom; Walker != nullptr && Walker != Root; Walker = Walker->GetParent())
		{
			if (Walker->GetTabNavigation() == EDreamWidgetTabNavigation::Once)
			{
				SearchFrom = Walker->GetParent();
			}
		}
		UDreamWidget* Part = Root;
		bool bPartWraps = DomainWraps(InDomain);
		for (const UDreamWidget* Walker = SearchFrom; Walker != nullptr && Walker != Root; Walker = Walker->GetParent())
		{
			const EDreamWidgetTabNavigation Mode = Walker->GetTabNavigation();
			if (Mode == EDreamWidgetTabNavigation::Cycle || Mode == EDreamWidgetTabNavigation::Contained)
			{
				Part = const_cast<UDreamWidget*>(Walker);
				bPartWraps = Mode == EDreamWidgetTabNavigation::Cycle;
				break;
			}
		}

		FTabWalk Walk;
		WalkAll(Part, InFrom, 0, Walk);
		if (Walk.Entries.Num() == 0)
		{
			return nullptr;
		}
		int32 NextIndex = INDEX_NONE;
		if (Walk.StartEntry != INDEX_NONE)
		{
			NextIndex = Walk.StartEntry + (bInBackward ? -1 : 1);
		}
		else if (Walk.StartInsertion != INDEX_NONE)
		{
			NextIndex = bInBackward ? Walk.StartInsertion - 1 : Walk.StartInsertion;
		}
		else
		{
			// Not met on the way, which a start inside the part cannot be: entered from outside, then.
			return bInBackward ? Walk.Entries.Last() : Walk.Entries[0];
		}
		if (Walk.Entries.IsValidIndex(NextIndex))
		{
			return Walk.Entries[NextIndex];
		}
		bOutRanOffEnd = true;
		if (bPartWraps || bInForceWrap)
		{
			return bInBackward ? Walk.Entries.Last() : Walk.Entries[0];
		}
		return nullptr;
	}

	/**
	 * Where focus lands for the entry InEntry: InEntry, or inside it when it is a single-stop container -- what its
	 * ResolveTabEntry answers (a list's selected row, scrolled into view and built), asked only for a real step; else its
	 * first stop, its last backwards, its own stop being its first. Followed into containers inside containers.
	 */
	UDreamWidget* EnterSingleStop(UDreamWidget* InEntry, bool bInBackward, bool bInForReal, bool& bOutEntered)
	{
		UDreamWidget* Entry = InEntry;
		for (int32 Depth = 0; Depth < MaxTabEntryDepth && IsValid(Entry)
			&& Entry->GetTabNavigation() == EDreamWidgetTabNavigation::Once; ++Depth)
		{
			if (bInForReal)
			{
				// Rows are virtualized: only the container knows, by index, which one the entry is and how to make it exist.
				UDreamWidget* Entered = Entry->ResolveTabEntry(bInBackward);
				if (IsValid(Entered) && Entered != Entry && IsNavigable(Entered))
				{
					bOutEntered = true;
					return Entered;
				}
			}
			FTabWalk Inner;
			WalkAll(Entry, nullptr, 0, Inner);
			if (Inner.Entries.Num() == 0)
			{
				return nullptr;
			}
			UDreamWidget* Inside = bInBackward ? Inner.Entries.Last() : Inner.Entries[0];
			if (Inside == Entry)
			{
				return Entry;
			}
			Entry = Inside;
		}
		return Entry;
	}

	enum class ETabLinkKind : uint8
	{
		/** No opinion: the order answers. */
		None,
		/** Go exactly there. */
		Target,
		/** Focus stays where it is. */
		Stay,
		/** The order answers, and at the end goes round whatever the part it is in says. */
		Wrap,
		/** The order answers, and at the end the rule's delegate is asked first. */
		Boundary,
	};

	struct FTabLink
	{
		ETabLinkKind Kind = ETabLinkKind::None;
		UDreamWidget* Target = nullptr;
		UDreamWidgetNavigation* Navigation = nullptr;
	};

	bool IsUsableLinkTarget(UDreamWidget* InTarget, const UDreamWidget* InRoot)
	{
		return IsInside(InTarget, InRoot) && IsNavigable(InTarget);
	}

	/**
	 * InFrom's own say about Tab, read as UUISelectable::FindNavigableOn reads it: the widget's navigation rules for Next or
	 * Previous first, then its selectable's NavigationNext or NavigationPrev. A link is followed when it leads somewhere
	 * that can be navigated to inside InRoot; one that cannot -- a target gone, outside a dialog that holds the player --
	 * leaves the order to answer, so a broken link is never a dead end. A Custom rule's delegate decides even when it
	 * answers nothing, and Stop and a selectable's None keep focus where it is, as they do for every other direction.
	 */
	FTabLink ResolveLink(const UDreamWidget* InFrom, bool bInBackward, const UDreamWidget* InRoot)
	{
		FTabLink Link;
		UDreamWidget* From = const_cast<UDreamWidget*>(InFrom);
		const EDreamUINavigationDirection Direction = bInBackward ? EDreamUINavigationDirection::Prev : EDreamUINavigationDirection::Next;
		if (UDreamWidgetNavigation* Navigation = From->GetNavigation(); IsValid(Navigation) && Navigation->HasRuleFor(Direction))
		{
			switch (Navigation->GetNavigationData(Direction).Rule)
			{
			case EDreamUINavigationRule::Stop:
				Link.Kind = ETabLinkKind::Stay;
				return Link;
			case EDreamUINavigationRule::Wrap:
				Link.Kind = ETabLinkKind::Wrap;
				return Link;
			case EDreamUINavigationRule::CustomBoundary:
				Link.Kind = ETabLinkKind::Boundary;
				Link.Navigation = Navigation;
				return Link;
			case EDreamUINavigationRule::Explicit:
			case EDreamUINavigationRule::Custom:
			{
				const bool bCustom = Navigation->GetNavigationData(Direction).Rule == EDreamUINavigationRule::Custom;
				bool bHandled = false;
				UDreamWidget* Target = Navigation->ResolveTarget(Direction, bHandled);
				if (IsUsableLinkTarget(Target, InRoot))
				{
					Link.Kind = ETabLinkKind::Target;
					Link.Target = Target;
				}
				else if (bCustom)
				{
					Link.Kind = ETabLinkKind::Stay;
				}
				return Link;
			}
			default:
				break;
			}
		}
		if (UUISelectable* Selectable = From->GetComponent<UUISelectable>())
		{
			const EUISelectableNavigationMode Mode = bInBackward ? Selectable->GetNavigationPrev() : Selectable->GetNavigationNext();
			if (Mode == EUISelectableNavigationMode::None)
			{
				Link.Kind = ETabLinkKind::Stay;
			}
			else if (Mode == EUISelectableNavigationMode::Explicit)
			{
				// The author's chain is followed past what cannot be used, as for every other direction.
				UUISelectable* Resolved = UUISelectable::ResolveExplicitTarget(
					bInBackward ? Selectable->GetNavigationPrevExplicit() : Selectable->GetNavigationNextExplicit(), Direction);
				UDreamWidget* Target = Resolved != nullptr ? Resolved->GetWidget() : nullptr;
				if (IsUsableLinkTarget(Target, InRoot))
				{
					Link.Kind = ETabLinkKind::Target;
					Link.Target = Target;
				}
			}
		}
		return Link;
	}

	/**
	 * Where a press from the focus InFocus starts: InFocus -- except a control focused as a whole, a user widget that takes no
	 * navigation itself while one of its own parts does (a focusable scroll box's face): that part, where the navigation step
	 * starts its arrows too, so the press moves past the control rather than onto the part of it that already showed focus.
	 * A nested control's parts are its own, and are not looked into.
	 */
	const UDreamWidget* ResolveTabStart(const UDreamWidget* InFocus)
	{
		if (!IsValid(InFocus) || !InFocus->IsA<UDreamUserWidget>() || IsNavigable(const_cast<UDreamWidget*>(InFocus)))
		{
			return InFocus;
		}
		TArray<UDreamWidget*, TInlineAllocator<16>> Pending;
		const TArray<UDreamWidget*>& FocusChildren = InFocus->GetChildren();
		for (int32 Index = FocusChildren.Num() - 1; Index >= 0; --Index)
		{
			Pending.Add(FocusChildren[Index]);
		}
		while (Pending.Num() > 0)
		{
			UDreamWidget* Candidate = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Candidate) || Candidate->IsA<UDreamUserWidget>())
			{
				continue;
			}
			if (IsNavigable(Candidate))
			{
				return Candidate;
			}
			const TArray<UDreamWidget*>& CandidateChildren = Candidate->GetChildren();
			for (int32 Index = CandidateChildren.Num() - 1; Index >= 0; --Index)
			{
				Pending.Add(CandidateChildren[Index]);
			}
		}
		return InFocus;
	}

	/**
	 * Whether InWidget is drawn in front of the open popup InPopup, as the popup layer judges the focus on a layer put up
	 * after a popup opened -- a dialog, a page -- whose Back that layer then gets first (UDreamUIPopupLayer::HandleBack): on
	 * the popup's root canvas, in a canvas sorted above the popup's, and in no open popup itself.
	 */
	bool IsDrawnInFrontOfPopup(const UDreamUIPopupLayer& InPopups, const UDreamWidget* InWidget, const UDreamWidget* InPopup)
	{
		if (!IsValid(InWidget) || !IsValid(InPopup) || InPopups.FindPopupContaining(InWidget, INDEX_NONE) != nullptr)
		{
			return false;
		}
		const UDreamCanvas* WidgetCanvas = InWidget->GetRenderCanvas();
		const UDreamCanvas* PopupCanvas = InPopup->GetRenderCanvas();
		if (!IsValid(WidgetCanvas) || !IsValid(PopupCanvas) || WidgetCanvas->GetRootCanvas() != PopupCanvas->GetRootCanvas())
		{
			return false;
		}
		return WidgetCanvas->GetActualSortOrder() > PopupCanvas->GetActualSortOrder();
	}

	/** The widget whose subtree is InWidget's screen for Tab: its root canvas's widget, else the top of its hierarchy. */
	UDreamWidget* FindScreenRootOf(const UDreamWidget* InWidget)
	{
		if (const UDreamCanvas* RootCanvas = InWidget->GetRootCanvas())
		{
			if (UDreamWidget* CanvasWidget = RootCanvas->GetWidget())
			{
				return CanvasWidget;
			}
		}
		// No canvas draws it yet -- a tree being built, a headless fixture: the hierarchy it is in is all there is.
		const UDreamWidget* Top = InWidget;
		while (Top->GetParent() != nullptr)
		{
			Top = Top->GetParent();
		}
		return const_cast<UDreamWidget*>(Top);
	}

	FDreamUITabStep ComputeStep(const UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom, bool bInBackward,
		bool bInForReal)
	{
		FDreamUITabStep Result;
		UWorld* World = InWorldContext != nullptr ? InWorldContext->GetWorld() : nullptr;
		const UDreamWidget* From = ResolveTabStart(IsValid(InFrom) ? InFrom : nullptr);
		FDreamUITabDomain Domain = FDreamUITabOrder::FindDomain(InWorldContext, InUserIndex, From);

		// A popup that closes on Tab -- a dropdown's list, a menu -- is closed, every popup of the player with it, and the Tab
		// goes on from the bottom one's opener in the opener's domain: past the dropdown's face, not round its list.
		if (bInForReal && Domain.Kind == EDreamUITabDomainKind::Popup && Domain.bCloseOnTab)
		{
			if (UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(World))
			{
				const TWeakObjectPtr<UDreamWidget> TopPopup = Domain.Root;
				UDreamWidget* Opener = Popups->CloseForTab(InUserIndex);
				// Owner code ran in there, committing rows and giving focus back: only what the layer answers now is trusted.
				Result.bClosedPopups = !TopPopup.IsValid() || !Popups->IsOpen(TopPopup.Get());
				if (Result.bClosedPopups)
				{
					const UDreamUIInputServices* Services = UDreamUIInputServices::Get(World);
					From = ResolveTabStart(IsValid(Opener) ? Opener : (Services != nullptr ? Services->GetFocusedWidget(InUserIndex) : nullptr));
					Domain = FDreamUITabOrder::FindDomain(InWorldContext, InUserIndex, From);
				}
			}
		}
		Result.Domain = Domain;
		UDreamWidget* Root = Domain.GetRoot();
		if (!Domain.IsValid())
		{
			return Result;
		}

		UDreamWidget* Target = nullptr;
		if (From != nullptr && IsInside(From, Root))
		{
			const FTabLink Link = ResolveLink(From, bInBackward, Root);
			if (Link.Kind == ETabLinkKind::Stay)
			{
				return Result;
			}
			if (Link.Kind == ETabLinkKind::Target)
			{
				Target = Link.Target;
			}
			else
			{
				bool bRanOffEnd = false;
				Target = FindNextEntry(Domain, From, bInBackward, Link.Kind == ETabLinkKind::Wrap, bRanOffEnd);
				if (bRanOffEnd && Link.Kind == ETabLinkKind::Boundary && Link.Navigation != nullptr)
				{
					// The order ran out, which is the one moment a boundary rule's delegate is asked; an answer it cannot
					// land on leaves the order's.
					UDreamWidget* FromDelegate = Link.Navigation->AskBoundaryDelegate(
						bInBackward ? EDreamUINavigationDirection::Prev : EDreamUINavigationDirection::Next);
					if (IsUsableLinkTarget(FromDelegate, Root))
					{
						Target = FromDelegate;
					}
				}
			}
		}
		else
		{
			// No focus, or focus outside the popup or dialog the player is held in: Tab enters at the first stop, Shift+Tab
			// at the last.
			Target = bInBackward ? FDreamUITabOrder::FindLastStop(Domain) : FDreamUITabOrder::FindFirstStop(Domain);
		}

		bool bEntered = false;
		Target = EnterSingleStop(Target, bInBackward, bInForReal, bEntered);
		if (Target == nullptr || Target == From)
		{
			return Result;
		}
		UDreamUIBehaviour* Receiver = DreamUINavigationScan::FindNavigationBehaviour(Target);
		if (Receiver == nullptr)
		{
			return Result;
		}
		Result.Target = Target;
		Result.Receiver = Receiver;
		Result.bEnteredContainer = bEntered;
		return Result;
	}
}

FDreamUITabDomain FDreamUITabOrder::FindDomain(const UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom)
{
	using namespace DreamUITabOrderLocal;
	FDreamUITabDomain Domain;
	Domain.UserIndex = InUserIndex;
	UWorld* World = InWorldContext != nullptr ? InWorldContext->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Domain;
	}

	// The player's top popup: in front of every screen, and lifted out of every scope's subtree -- unless the focus is on a
	// dialog or a page put up in front of it after it opened, whose Tab it is then, as its Back is.
	if (const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(World))
	{
		if (UDreamWidget* Popup = Popups->GetTopPopup(InUserIndex); IsValid(Popup) && !IsDrawnInFrontOfPopup(*Popups, InFrom, Popup))
		{
			Domain.Kind = EDreamUITabDomainKind::Popup;
			Domain.Root = Popup;
			Domain.bWraps = true;
			Domain.bCloseOnTab = Popups->GetTopPopupTabBehavior(InUserIndex) == EDreamPopupTabBehavior::CloseAndContinue;
			return Domain;
		}
	}
	// The player's top scope when it confines: a dialog, a modal. Entered from outside, never left.
	if (const UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(World))
	{
		if (UDreamWidget* ScopeWidget = Stack->FindConfiningWidgetForUser(InUserIndex))
		{
			Domain.Kind = EDreamUITabDomainKind::Scope;
			Domain.Root = ScopeWidget;
			Domain.bWraps = true;
			return Domain;
		}
	}
	// The screen the focus is on -- for world-space and render-target UI the canvas root -- else the player's own screen,
	// the first of their screens that has a stop.
	UDreamWidget* ScreenRoot = nullptr;
	if (IsValid(InFrom))
	{
		ScreenRoot = FindScreenRootOf(InFrom);
	}
	else
	{
		TArray<UDreamWidget*> Roots;
		GetPlayerScreenRoots(World, InUserIndex, Roots);
		for (UDreamWidget* Candidate : Roots)
		{
			FTabWalk Probe;
			WalkAll(Candidate, nullptr, 1, Probe);
			if (Probe.Entries.Num() > 0)
			{
				ScreenRoot = Candidate;
				break;
			}
		}
		if (ScreenRoot == nullptr && Roots.Num() > 0)
		{
			ScreenRoot = Roots[0];
		}
	}
	if (ScreenRoot != nullptr)
	{
		Domain.Kind = EDreamUITabDomainKind::Screen;
		Domain.Root = ScreenRoot;
		Domain.bWraps = UDreamGUISettings::Get()->bTabWrapsAtScreenEnd;
	}
	return Domain;
}

bool FDreamUITabOrder::IsTabStop(const UDreamWidget* InWidget)
{
	using namespace DreamUITabOrderLocal;
	if (!IsValid(InWidget))
	{
		return false;
	}
	FTabWalk Facts;
	PrepareWalk(Facts, InWidget, nullptr);
	// Nothing above it closes it off: an inactive or undrawn ancestor, a None container, a scroll bar, a tooltip.
	for (const UDreamWidget* Walker = InWidget; Walker != nullptr; Walker = Walker->GetParent())
	{
		if (!IsValid(Walker) || IsClosedSubtree(Walker, Facts) || !IsInPlay(Walker, Facts.bWorldPlays))
		{
			return false;
		}
	}
	return IsStopHere(const_cast<UDreamWidget*>(InWidget), Facts);
}

void FDreamUITabOrder::CollectStops(const FDreamUITabDomain& InDomain, TArray<UDreamWidget*>& OutStops)
{
	using namespace DreamUITabOrderLocal;
	OutStops.Reset();
	UDreamWidget* Root = InDomain.GetRoot();
	if (!IsValid(Root))
	{
		return;
	}
	FTabWalk Walk;
	WalkAll(Root, nullptr, 0, Walk);
	OutStops = MoveTemp(Walk.Entries);
}

UDreamWidget* FDreamUITabOrder::FindFirstStop(const FDreamUITabDomain& InDomain)
{
	using namespace DreamUITabOrderLocal;
	UDreamWidget* Root = InDomain.GetRoot();
	if (!IsValid(Root))
	{
		return nullptr;
	}
	FTabWalk Walk;
	WalkAll(Root, nullptr, 1, Walk);
	return Walk.Entries.Num() > 0 ? Walk.Entries[0] : nullptr;
}

UDreamWidget* FDreamUITabOrder::FindLastStop(const FDreamUITabDomain& InDomain)
{
	using namespace DreamUITabOrderLocal;
	UDreamWidget* Root = InDomain.GetRoot();
	if (!IsValid(Root))
	{
		return nullptr;
	}
	FTabWalk Walk;
	WalkAll(Root, nullptr, 0, Walk);
	return Walk.Entries.Num() > 0 ? Walk.Entries.Last() : nullptr;
}

UDreamWidget* FDreamUITabOrder::FindNextStop(const FDreamUITabDomain& InDomain, const UDreamWidget* InFrom, bool bInBackward)
{
	using namespace DreamUITabOrderLocal;
	UDreamWidget* Root = InDomain.GetRoot();
	if (!IsValid(Root))
	{
		return nullptr;
	}
	if (!IsInside(InFrom, Root))
	{
		return bInBackward ? FindLastStop(InDomain) : FindFirstStop(InDomain);
	}
	bool bRanOffEnd = false;
	return FindNextEntry(InDomain, InFrom, bInBackward, /*bInForceWrap*/false, bRanOffEnd);
}

FDreamUITabStep FDreamUITabOrder::Step(UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom, bool bInBackward)
{
	return DreamUITabOrderLocal::ComputeStep(InWorldContext, InUserIndex, InFrom, bInBackward, /*bInForReal*/true);
}

FDreamUITabStep FDreamUITabOrder::Peek(const UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom, bool bInBackward)
{
	return DreamUITabOrderLocal::ComputeStep(InWorldContext, InUserIndex, InFrom, bInBackward, /*bInForReal*/false);
}

void FDreamUITabOrder::GetPlayerScreenRoots(const UObject* InWorldContext, int32 InUserIndex, TArray<UDreamWidget*>& OutRoots)
{
	OutRoots.Reset();
	UWorld* World = InWorldContext != nullptr ? InWorldContext->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	// The screen the player looks at: their own, or for a script player, which has no local player and so no screen of its
	// own, the first local player's -- where its pointers land too.
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(World);
	const int32 ScreenPlayer = Input != nullptr ? Input->GetScreenIndexForUser(InUserIndex) : InUserIndex;

	// Every local player's screen root, by player, so another player's root is told from an authored canvas. A root is
	// only found through its player's controller; the first local player's is also what no controller finds.
	TMap<const UDreamWidget*, int32> ScreenRootPlayers;
	if (const UDreamScreenUISubsystem* Screens = UDreamScreenUISubsystem::Get(World))
	{
		if (const UDreamWidget* FirstRoot = Screens->GetScreenRoot(nullptr); IsValid(FirstRoot))
		{
			ScreenRootPlayers.Add(FirstRoot, UDreamWidget::GetLocalPlayerIndexOf(World->GetFirstPlayerController()));
		}
		for (const int32 PlayerIndex : Screens->GetScreenPlayerIndices())
		{
			if (APlayerController* Controller = UDreamEventSystem::GetPlayerControllerForUser(World, PlayerIndex))
			{
				if (const UDreamWidget* PlayerRoot = Screens->GetScreenRoot(Controller); IsValid(PlayerRoot))
				{
					ScreenRootPlayers.Add(PlayerRoot, PlayerIndex);
				}
			}
		}
	}

	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (Manager == nullptr)
	{
		return;
	}
	TArray<UDreamCanvas*> Canvases;
	for (const TWeakObjectPtr<UDreamCanvas>& CanvasPtr : Manager->GetAllCanvasArray())
	{
		UDreamCanvas* Canvas = CanvasPtr.Get();
		if (!IsValid(Canvas) || !Canvas->IsRootCanvas() || Canvas->GetActualRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)
		{
			continue;
		}
		const UDreamWidget* CanvasWidget = Canvas->GetWidget();
		if (!IsValid(CanvasWidget) || !CanvasWidget->GetWidgetActiveInHierarchy())
		{
			continue;
		}
		// Whose it is: the player whose screen root it is, else whoever owns its widget (the first local player for a canvas
		// nobody owns).
		const int32* RootPlayer = ScreenRootPlayers.Find(CanvasWidget);
		const int32 Owner = RootPlayer != nullptr ? *RootPlayer : CanvasWidget->GetOwningPlayerIndex();
		if (Owner == ScreenPlayer)
		{
			Canvases.Add(Canvas);
		}
	}
	// The player's screen root first, then the rest frontmost first.
	Canvases.StableSort([&ScreenRootPlayers](const UDreamCanvas& A, const UDreamCanvas& B)
	{
		const bool bAIsScreenRoot = ScreenRootPlayers.Contains(A.GetWidget());
		const bool bBIsScreenRoot = ScreenRootPlayers.Contains(B.GetWidget());
		if (bAIsScreenRoot != bBIsScreenRoot)
		{
			return bAIsScreenRoot;
		}
		return A.GetActualSortOrder() > B.GetActualSortOrder();
	});
	for (const UDreamCanvas* Canvas : Canvases)
	{
		OutRoots.AddUnique(Canvas->GetWidget());
	}
}
