// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UDreamUIBehaviour;
class UDreamWidget;
class UObject;

/** What a player's Tab is kept inside (FDreamUITabDomain::Kind). */
enum class EDreamUITabDomainKind : uint8
{
	/** None: the player has nothing on screen a Tab could reach. */
	None,
	/** The player's top open popup (UDreamUIPopupLayer::GetTopPopup): a dropdown's list, a menu. */
	Popup,
	/** The widget of the player's top scope that confines navigation: a dialog, a modal (UDreamUINavigationStack::FindConfiningWidgetFor). */
	Scope,
	/** The player's screen root (UDreamScreenUISubsystem), or for render-target and world-space UI the root canvas's widget. */
	Screen,
};

/**
 * Where one player's Tab runs, and what it does at the ends: the player's top popup, else the top scope that confines
 * navigation, else their screen root. A Tab whose start lies outside an active popup or confining scope enters it.
 */
struct DREAMGUIINPUT_API FDreamUITabDomain
{
	EDreamUITabDomainKind Kind = EDreamUITabDomainKind::None;
	/** The widget whose subtree holds the domain's stops. */
	TWeakObjectPtr<UDreamWidget> Root;
	int32 UserIndex = 0;
	/** At an end, Tab goes round to the other end: always in a popup or a confining scope, at a screen as UDreamGUISettings::bTabWrapsAtScreenEnd says. */
	bool bWraps = false;
	/** A popup pushed to close on Tab (EDreamPopupTabBehavior::CloseAndContinue): a Tab closes the player's popups and goes on from the opener. */
	bool bCloseOnTab = false;

	UDreamWidget* GetRoot() const { return Root.Get(); }
	bool IsValid() const { return Kind != EDreamUITabDomainKind::None && Root.IsValid(); }
};

/** Where one Tab step lands (FDreamUITabOrder::Step). Its pointers are good for the moment it is returned in. */
struct DREAMGUIINPUT_API FDreamUITabStep
{
	/** Where the focus goes: a stop, or the entry of a single-stop container. Null when it stays where it is -- no stop, or an end that holds. */
	UDreamWidget* Target = nullptr;
	/**
	 * The behaviour on Target that receives the move, chosen as DreamUINavigationScan::FindNavigationBehaviour chooses: its
	 * widget's navigation rules first, then its selectable. Null with Target.
	 */
	UDreamUIBehaviour* Receiver = nullptr;
	/** The domain the step ended in: after popups closed on the way, the opener's. */
	FDreamUITabDomain Domain;
	/** Popups closed on the way (CloseAndContinue): their owners heard EDreamPopupDismissReason::Tab, and the focus went back to the opener before the walk went on from it. */
	bool bClosedPopups = false;
	/** Target came from a single-stop container (EDreamWidgetTabNavigation::Once) through UDreamWidget::ResolveTabEntry, which scrolled it into view. */
	bool bEnteredContainer = false;
};

/**
 * Tab order: what Tab and Shift+Tab walk, as UDreamGUISettings::TabOrder Hierarchy has it -- the widget tree depth first,
 * siblings stably sorted by UDreamWidget::TabIndex, a widget's own stop before those inside it; explicit Next and Prev
 * links (a selectable's NavigationNext / NavigationPrev, a UDreamWidgetNavigation rule) win over it.
 *
 * A STOP is a widget with a navigation receiver (a selectable, or a UDreamWidgetNavigation) that can be navigated to, is
 * a Tab stop (UDreamWidget::bIsTabStop), is focusable, is in play, active, drawn and interactable, and is on screen or
 * reachable by scrolling (FDreamUINavigationScroll::IsReachableByScrolling) -- the step scrolls it into view. Tooltips,
 * popup sheets and scrollbar parts never are, nor is anything under a container whose TabNavigation is None. A container
 * whose TabNavigation is Once is one stop, where it stands.
 *
 * Game thread. One walk of the domain per call: a press, not a frame.
 */
class DREAMGUIINPUT_API FDreamUITabOrder
{
public:
	/**
	 * Player InUserIndex's domain now, InFrom being where their Tab starts (their focus, or null): the player's top popup;
	 * else the confining scope that holds InFrom or, when InFrom is outside every one, the player's top confining scope;
	 * else the screen root InFrom is under, or the player's.
	 *
	 * The top popup is passed over while InFrom is drawn in front of it -- a dialog or a page put up after it opened -- as
	 * its Back is (UDreamUIPopupLayer::HandleBack). Open popups a domain's root does not lie in -- another player's, lifted
	 * onto a screen root players share -- hold no stops of that domain.
	 */
	static FDreamUITabDomain FindDomain(const UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom);
	/** Whether InWidget is a stop now (see the class). */
	static bool IsTabStop(const UDreamWidget* InWidget);
	/**
	 * Every stop under InDomain's root, in Tab order -- a single-stop container once, as itself, where it stands: what the
	 * tests and the editor's tab-order view read.
	 */
	static void CollectStops(const FDreamUITabDomain& InDomain, TArray<UDreamWidget*>& OutStops);
	/** The domain's first stop, its last, and the one after InFrom (before it when bInBackward), wrapping and holding as the domain and the containers say; null when there is none. */
	static UDreamWidget* FindFirstStop(const FDreamUITabDomain& InDomain);
	static UDreamWidget* FindLastStop(const FDreamUITabDomain& InDomain);
	static UDreamWidget* FindNextStop(const FDreamUITabDomain& InDomain, const UDreamWidget* InFrom, bool bInBackward);
	/**
	 * The whole of one Tab (Shift+Tab when bInBackward) for player InUserIndex from InFrom, their focus: InFrom's explicit
	 * link when it has one; else, with InFrom null or outside the domain, the domain's first stop (its last backwards);
	 * else the next stop. In a popup that closes on Tab, the player's popups are closed first (UDreamUIPopupLayer::
	 * CloseForTab) and the step goes on from the opener in the opener's domain. Resolves; the caller moves the focus (the
	 * navigation step: highlight, focus, scroll into view) and records the cause as Tab.
	 */
	static FDreamUITabStep Step(UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom, bool bInBackward);

	/**
	 * Step's answer without what Step does on the way, for a question asked rather than a key pressed: no popup is closed
	 * (one that closes on Tab is walked as one that cycles), and a single-stop container is entered at its first stop, its
	 * last backwards, rather than through UDreamWidget::ResolveTabEntry, which scrolls and builds rows. What
	 * UUISelectable::FindNavigableOn answers for Next and Prev under the hierarchy order.
	 */
	static FDreamUITabStep Peek(const UObject* InWorldContext, int32 InUserIndex, const UDreamWidget* InFrom, bool bInBackward);
	/**
	 * The widgets of the screen-space root canvases that are player InUserIndex's screen: the one the screen subsystem
	 * keeps for them (UDreamScreenUISubsystem) first, then every other one they own, frontmost first. A player with no
	 * local player of their own -- a script player -- looks at the first local player's, as their pointers do. Never
	 * world-space or render-target UI, never another player's screen: where a player's Tab with no focus starts, and where
	 * their first directional press with no focus and no scope looks (UUISelectable::FindDefaultSelectable).
	 */
	static void GetPlayerScreenRoots(const UObject* InWorldContext, int32 InUserIndex, TArray<UDreamWidget*>& OutRoots);
};
