// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/DreamUIWorldService.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Interaction/DreamUIFocusReturn.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamUIPopupLayer.generated.h"

class UDreamCanvas;
class UDreamUIManagerWorldSubsystem;
class UDreamWidget;

/** What a press outside an open popup -- and outside its child popups -- does to it. */
UENUM(BlueprintType)
enum class EDreamPopupOutsideClick : uint8
{
	/** Dismisses it, and the press goes no further: what DreamGUI's dropdowns and menus always did. */
	Consume,
	/** Dismisses it, and the press then reaches whatever is under the pointer, as Slate's menus let it. */
	PassThrough,
	/** Nothing: it stays open until its owner, Back, its parent or its opener closes it. */
	Ignore,
};

/** Why a popup closed, as its owner's dismissed callback is told. */
UENUM(BlueprintType)
enum class EDreamPopupDismissReason : uint8
{
	/** Its owner closed it (UDreamUIPopupLayer::Dismiss): a row chosen, a toggle, code. */
	Explicit,
	/** A press landed outside it. */
	OutsideClick,
	/** Back or Escape, with it on top of its player's popups. */
	Back,
	/** Its parent popup closed, which closes its children first. */
	ParentClosed,
	/**
	 * Another popup took its place: a new top-level popup of the same player, or a sibling under the same parent. Also a
	 * modal shown for the player (UDreamUIModalSubsystem), which comes up in front of all of their popups and closes them.
	 */
	Replaced,
	/** Its opener stopped being usable: destroyed, out of play, inactive, hidden or no longer interactable. */
	OpenerLost,
	/** The world is coming down. */
	WorldTeardown,
	/**
	 * Tab or Shift+Tab, in a popup that closes on Tab (EDreamPopupTabBehavior::CloseAndContinue): every popup of the player
	 * closed, top down, and the Tab goes on from the bottom one's opener. An owner commits what was highlighted, as a
	 * dropdown with bTabCommitsHighlightedRow does. Appended.
	 */
	Tab,
};

/** What Tab does inside an open popup (FDreamPopupParams::TabBehavior). */
UENUM(BlueprintType)
enum class EDreamPopupTabBehavior : uint8
{
	/** Tab goes round the popup's own controls, as in a dialog: it never leaves. */
	Cycle,
	/** Tab closes the player's popups (EDreamPopupDismissReason::Tab), gives the focus back to the opener and takes its step from there: dropdown lists and menus. */
	CloseAndContinue,
};

/** Places an open popup, already lifted onto its player's screen root. */
DECLARE_DELEGATE_OneParam(FDreamPopupPlaceDelegate, UDreamWidget* /*Popup*/);
/** Told once a popup has closed: its focus returned, and -- unless it asked to stay lifted -- put back where it came from. */
DECLARE_DELEGATE_TwoParams(FDreamPopupDismissedDelegate, UDreamWidget* /*Popup*/, EDreamPopupDismissReason /*Reason*/);

/** What UDreamUIPopupLayer::Push is told about a popup. Its pointers need only live through the call. */
struct FDreamPopupParams
{
	/** The popup: a widget its owner built and positioned under itself; the layer lifts it onto the player's screen root. */
	UDreamWidget* Popup = nullptr;
	/**
	 * What opened it: where focus goes back to, what it follows while open, and what decides its parent -- the player's
	 * deepest open popup that is or contains the opener. Null: a top-level popup with nothing to follow or return to.
	 */
	UDreamWidget* Opener = nullptr;
	/** The player the popup belongs to: whose focus it takes and returns, whose Back closes it, whose presses dismiss it. */
	int32 UserIndex = 0;
	EDreamPopupOutsideClick OutsideClick = EDreamPopupOutsideClick::Consume;
	/** Move the player's focus into the popup once it is up: to InitialFocus when it is usable, else its first navigable control. */
	bool bFocusOnOpen = true;
	/** Where focus goes on open -- a dropdown's selected row; null for the first navigable control. */
	UDreamWidget* InitialFocus = nullptr;
	/**
	 * Put the popup back under its parent, at its old child index, when it closes. False keeps it lifted -- drawn above
	 * everything, but no longer open: no outside clicks, no Back, no following -- so its owner can play a close
	 * animation and call Restore itself.
	 */
	bool bRestoreOnDismiss = true;
	/** What Tab does while it is the player's top popup. Cycle by default; DreamGUI's dropdown lists and menus close on Tab. */
	EDreamPopupTabBehavior TabBehavior = EDreamPopupTabBehavior::Cycle;
	/**
	 * Called right after the lift and again after every frame's layout passes while the popup is open, to place it in
	 * the screen root's plane (Y across, Z up), as Elevate's anchors leave it. It may move and size the popup; it must not
	 * reparent, push or dismiss anything. Unbound: the popup keeps the offset from its opener it had at Push, and so
	 * follows it.
	 */
	FDreamPopupPlaceDelegate Place;
	FDreamPopupDismissedDelegate OnDismissed;
	/**
	 * Told as the popup starts to close, with the reason it will be dismissed with: its child popups closed, it off the
	 * stack, and every player's focus still where it was in it -- the one moment an owner can read what the player had
	 * highlighted, as a dropdown committing its highlighted row on Tab does. For reading only: it must not push, dismiss or
	 * move focus. The focus is given back right after, then OnDismissed follows.
	 */
	FDreamPopupDismissedDelegate OnClosing;
};

/**
 * The screen-top layer transient UI is lifted to: dropdown lists and menus, as a per-player menu stack (Push, Dismiss).
 * Elevate and Restore are the lift alone, kept for owners that manage the rest themselves; the paragraphs below describe
 * them, and Push's comment describes what the stack adds: focus return, outside clicks, Back, nesting, and following the
 * opener after every frame's layout passes (UDreamUIManagerWorldSubsystem::GetOnLayoutPassesFinished).
 *
 * UMG's combo list lives in the Slate menu stack -- a popup is not a child of the control that
 * opened it, which is what keeps it from being clipped by an ancestor's bounds or counted by an
 * ancestor's layout. This is that idea for a mesh UI, kept deliberately small: the popup STAYS the
 * widget it already was, and the layer only moves it.
 *
 * Elevate reparents the widget under the screen root with its world position kept, so whatever
 * positioning its owner ran while it was still a child -- UUIDropdown::Show anchors its list
 * against the face -- survives the move pixel for pixel. Restore hands it back to the parent it
 * came from; owners that re-position on every open (Show does) need nothing else.
 *
 * What Elevate alone does not do: outside clicks, focus, nesting of menus, or following a moving
 * anchor while open. Push does all four.
 */
UCLASS()
class DREAMGUI_API UDreamUIPopupLayer : public UWorldSubsystem, public IDreamUIWorldService
{
	GENERATED_BODY()

public:
	static UDreamUIPopupLayer* Get(const UObject* InWorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual int32 GetTeardownPriority() const override { return DreamUI::WorldServiceTeardownPriority::Layers; }
	/** Forget every lifted widget's way home: the trees they belong to are coming down with the world. */
	virtual void TeardownForWorld(UWorld& InWorld) override;

	/**
	 * Lift InWidget to the screen root, keeping its on-screen position. Safe to call on a widget
	 * already lifted (a re-open re-anchored it under its owner first only if it was restored, so a
	 * second Elevate with no Restore in between is a no-op). Returns false when there is no screen
	 * root to lift to.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Popup")
	bool Elevate(UDreamWidget* InWidget);

	/**
	 * Hand a lifted widget back to the parent Elevate took it from, at the place among its children it had. One still
	 * open on the stack (Push) is dismissed first, as its owner closing it would be.
	 *
	 * It goes home as it left: the panel slot it had there is made again with the values it had (Elevate kept a copy --
	 * see GetHomeSlot), and the canvas Push sorted it with is undone -- removed when Push added it, its own sorting given
	 * back when it had one already.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Popup")
	void Restore(UDreamWidget* InWidget);

	/**
	 * The panel slot a lifted widget had under the parent Elevate took it from -- a copy of its class and values, made as
	 * it left, since the screen root hands out no slots: what places a lifted widget by its home panel's rules reads its
	 * padding, nudge and size bounds here (UDreamLayoutContainerMenuAnchor's lifted menu). Null for a widget that is not
	 * lifted, or that had no slot at home. Read-only: nothing written to it reaches the widget, and Restore refills the
	 * widget's own slot from it.
	 */
	const UDreamPanelSlot* GetHomeSlot(const UDreamWidget* InWidget) const;

	/**
	 * Open a popup on its player's menu stack. Its parent is the player's deepest open popup that is or contains the
	 * opener; a top-level popup dismisses the player's other popups, a child its siblings (Replaced). The popup is lifted
	 * onto the player's screen root with its child index remembered and a canvas of its own that sorts above its opener
	 * and the player's top modal, below the drag (29000) and tooltip (30000) bands; placed (Params.Place); every player's
	 * focus is captured (FDreamFocusReturn) before, when asked, the popup takes this player's. While open it follows its
	 * opener, is dismissed when its opener stops being usable (OpenerLost) -- both checked after every frame's layout
	 * passes -- and by presses and Back as below. False, with nothing changed, when it cannot be lifted (no screen root).
	 * Pushing a popup that is open already changes nothing and answers true.
	 *
	 * True means open when Push returns: false too when the popup closed again before that -- the focus moving into it ran
	 * a handler that closed it, and its OnDismissed has been told. So an owner that records "open" goes by its own state
	 * after the call, which its dismissed callback has kept, rather than by the answer alone.
	 */
	bool Push(const FDreamPopupParams& Params);
	/**
	 * Close an open popup: its child popups first (ParentClosed), then focus returned (FDreamFocusReturn::Return), then
	 * the popup put back (unless it asked to stay lifted), then its OnDismissed. Nothing for a popup that is not open.
	 */
	void Dismiss(UDreamWidget* InPopup, EDreamPopupDismissReason InReason = EDreamPopupDismissReason::Explicit);
	/** Close every popup player InUserIndex has open, the newest first, each with InReason: what a modal shown for the player does. */
	void DismissAll(int32 InUserIndex, EDreamPopupDismissReason InReason);

	/** Whether InPopup is open on the layer: pushed, and neither dismissed nor kept lifted after dismissal. */
	bool IsOpen(const UDreamWidget* InPopup) const;
	/** Player InUserIndex's topmost open popup, or null. */
	UDreamWidget* GetTopPopup(int32 InUserIndex) const;
	/** Player InUserIndex's open popups, bottom first. */
	void GetOpenPopups(int32 InUserIndex, TArray<UDreamWidget*>& OutPopups) const;
	/** The deepest open popup that is InWidget or contains it, of player InUserIndex, or of any player when INDEX_NONE; null when none does. */
	UDreamWidget* FindPopupContaining(const UDreamWidget* InWidget, int32 InUserIndex = INDEX_NONE) const;

	/**
	 * A press of player InUserIndex's pointer, before it is delivered: what the pointer module tells the layer ahead of
	 * its selection change, InHitWidget being what the press hit (null for a press on nothing or on the world). A press
	 * inside open popup k dismisses the popups above k; a press outside all of them dismisses them all; popups whose
	 * outside clicks are Ignore stay. True when a dismissed popup consumes its outside clicks, or the press landed on a
	 * dismissed popup's own opener (which it would otherwise open again): the press then goes no further.
	 *
	 * Except a press on something drawn in front of the popup -- on its root canvas, in a canvas sorted above the popup's,
	 * and in no popup itself: a dialog, a page or a modal put up after the popup opened. That closes the popup as any press
	 * outside it does, and is not consumed by it: it goes on to what it landed on.
	 */
	bool NotifyPointerDown(int32 InUserIndex, UDreamWidget* InHitWidget);
	/**
	 * Back for player InUserIndex: closes that player's top popup only (Back), and says whether there was one.
	 *
	 * The newest layer gets Back first: while the player's focus is on something drawn in front of their top popup (as
	 * NotifyPointerDown judges it) -- a dialog or a page put up after the popup opened -- Back is that layer's, and this
	 * answers false with the popup left open, for the navigation stack to offer Back to the screens.
	 */
	bool HandleBack(int32 InUserIndex);
	/** The TabBehavior player InUserIndex's top popup was pushed with; Cycle when the player has none open. */
	EDreamPopupTabBehavior GetTopPopupTabBehavior(int32 InUserIndex) const;
	/**
	 * Tab for player InUserIndex while their top popup closes on Tab (CloseAndContinue): every popup of the player is
	 * dismissed with EDreamPopupDismissReason::Tab, the top one first -- each owner committing what it had highlighted,
	 * each popup's focus given back as on any dismissal -- and the bottom popup's opener is returned, for the Tab walk to go
	 * on from. Null, with nothing closed, when the top popup cycles or the player has none. Runs owner code synchronously:
	 * the caller -- the Tab walk, inside a navigation step -- holds no pointer into a popup across it.
	 *
	 * "Every popup" stops at one that cycles: a dropdown opened inside a popup pushed to cycle closes, the popup it is in
	 * stays, and the opener returned -- the dropdown's face -- is inside it, where the walk goes on. "The opener" is the
	 * player's focus instead when the focus came back to something inside the opener: a panel menu anchor's opener is the
	 * panel, which holds the trigger the menu gave the focus back to, and the walk goes on past that trigger, not onto it.
	 * Null also when popups did close but the last one closed had no opener, or lost it while the chain closed: the walk
	 * then starts afresh in whatever domain the player is left with.
	 */
	UDreamWidget* CloseForTab(int32 InUserIndex);

	/**
	 * How an open popup answers presses outside it from the next press on -- for an owner whose switch changes while the
	 * popup is up (UDreamMenuAnchor::SetCloseOnClickOutside). Nothing for a popup that is not open.
	 */
	void SetOutsideClick(const UDreamWidget* InPopup, EDreamPopupOutsideClick InOutsideClick);

	/**
	 * Whether a pointer of player InUserIndex goes through InHitWidget: another player's outside-click sheet (see
	 * Sheets), which stops only its own player's pointer. The input system leaves such a hit out of the player's trace.
	 */
	bool IsSheetOfAnotherPlayer(const UDreamWidget* InHitWidget, int32 InUserIndex) const;
	/** Whether any player's sheet may be up: lets a trace skip IsSheetOfAnotherPlayer for every hit while none is. */
	bool HasSheets() const { return Sheets.Num() > 0; }

private:
	/** Where a lifted widget came from: the parent Elevate took it from, and its place among that parent's children. */
	struct FElevatedHome
	{
		TWeakObjectPtr<UDreamWidget> Parent;
		int32 SiblingIndex = INDEX_NONE;
		/**
		 * A copy of the panel slot it had there (GetHomeSlot), made before the move took the slot away; null when it had
		 * none. Owned here -- a transient object of the transient package, holding nothing of the widget's -- and let go
		 * with the home.
		 */
		TStrongObjectPtr<UDreamPanelSlot> HomeSlot;
		/**
		 * The canvas Push sorted the widget with on this trip, and what to put back on the way home: whether Push added it,
		 * and the sorting it had before (a canvas Push added had its fresh defaults). Noted at the trip's first sort only.
		 */
		TWeakObjectPtr<UDreamCanvas> PushCanvas;
		bool bPushAddedCanvas = false;
		bool bCanvasOverrideSortingBefore = false;
		int32 CanvasSortOrderBefore = 0;
		/** ETraceTypeQuery, as its number: this header stays off the engine types. */
		int32 CanvasTraceChannelBefore = 0;
	};
	/**
	 * Who a lifted widget belongs to, for the trip home.
	 *
	 * Keyed by FObjectKey rather than by the widget itself: the key used to be a TObjectPtr in a
	 * UPROPERTY map, which is a STRONG reference, so a popup destroyed without a Restore -- its
	 * owner torn down while the list was open -- stayed alive for as long as the world subsystem
	 * did, with nothing left that could ever come and collect it. Both halves are non-owning now
	 * (an FObjectKey is an identity, not a reference), which also means the map no longer needs to
	 * be reflected at all.
	 */
	TMap<FObjectKey, FElevatedHome> ElevatedHomes;

	/** One popup open on its player's stack, as Push was told about it. Weak throughout, for ElevatedHomes' reason. */
	struct FOpenPopup
	{
		TWeakObjectPtr<UDreamWidget> Popup;
		/** The popup's identity, which outlives the popup: a popup destroyed while open is still found, and so are its children. */
		FObjectKey PopupKey;
		TWeakObjectPtr<UDreamWidget> Opener;
		/** Pushed with an opener: one gone or no longer usable closes the popup (OpenerLost). */
		bool bHasOpener = false;
		/** The popup this one is a child of, by identity; unset for a top-level popup (bTopLevel). */
		FObjectKey ParentKey;
		bool bTopLevel = true;
		int32 UserIndex = 0;
		EDreamPopupOutsideClick OutsideClick = EDreamPopupOutsideClick::Consume;
		bool bRestoreOnDismiss = true;
		EDreamPopupTabBehavior TabBehavior = EDreamPopupTabBehavior::Cycle;
		FDreamPopupPlaceDelegate Place;
		FDreamPopupDismissedDelegate OnDismissed;
		FDreamPopupDismissedDelegate OnClosing;
		/** Every player's focus at Push, given back by Dismiss. */
		FDreamFocusReturn FocusReturn;
		/** Where the opener was, in the popup's parent's plane, when the popup last followed it: an unplaced popup moves by what the opener moved. */
		FVector2D LastOpenerPosition = FVector2D::ZeroVector;
		/** Set while Dismiss closes this popup's children: closed already as far as every question asked from outside is concerned. */
		bool bDismissing = false;
	};
	/** Every player's open popups, in push order. A player's own form a chain -- each push replaces its siblings -- so push order is depth order. */
	TArray<FOpenPopup> OpenPopups;

	/** The post-layout hook, bound while anything is open: see UpdateLayoutHook. */
	FDelegateHandle LayoutPassesFinishedHandle;
	TWeakObjectPtr<UDreamUIManagerWorldSubsystem> HookedManager;

	/**
	 * Each player's sheet: an invisible, unfocusable widget over the whole screen root, sorted just under the player's
	 * popups, up while any of them consumes outside presses. It keeps the hover and the wheel off what is behind an open
	 * menu, as the click catchers it replaces did -- without being a selectable that takes the focus of the press that
	 * lands on it. The press itself is the layer's (NotifyPointerDown), and goes no further.
	 *
	 * Its own player's alone: every other player's pointer goes through it (IsSheetOfAnotherPlayer). Players can share
	 * a screen root, and each push sorts above everything there -- a sheet put up for one player's newer popup lies over
	 * an older popup of another player, and would otherwise have taken that player's presses on their own popup as
	 * presses outside it, and their hover with them; a player with no popup, or one that lets presses through, would
	 * have found the screen behind it dead.
	 */
	TMap<int32, TWeakObjectPtr<UDreamWidget>> Sheets;

	/** Set by TeardownForWorld, which runs once. */
	bool bTornDownForWorld = false;

	/** InPopup's entry in OpenPopups, one being dismissed included; INDEX_NONE when it has none. */
	int32 FindOpenIndex(const UDreamWidget* InPopup) const;
	/** The same by identity, which still finds the entry of a popup destroyed while open. */
	int32 FindOpenIndexByKey(const FObjectKey& InPopupKey) const;
	/**
	 * Dismiss's work, on the entry of identity InPopupKey: its children first, then the entry off the stack, the focus
	 * returned, the popup put back and its owner told. Found by identity throughout, because the owner and deselect
	 * handlers it runs on the way may close, open or destroy popups -- this one included -- and a popup that is gone
	 * is closed all the same: no focus inside it to return, no trip home, its owner told with a null popup.
	 */
	void DismissEntry(FObjectKey InPopupKey, EDreamPopupDismissReason InReason);
	/** Restore's trip home, without the dismissal of an open popup Restore makes first. */
	void RestoreHome(UDreamWidget* InWidget);
	/** Push's canvas: the popup's own, or one added, sorted into the popup band of InScreenRoot -- noted on its home for the trip back. */
	void SortPopupCanvas(UDreamWidget* InPopup, UDreamWidget* InScreenRoot);
	/** The trip home's half of SortPopupCanvas, for a widget back under its home: the canvas removed or its sorting given back. */
	void PutBackPopupCanvas(UDreamWidget* InWidget, const FElevatedHome& InHome);
	/** The trip home's slot, for a widget back under its home: made again with the class and values InHome kept. */
	void RefillHomeSlot(UDreamWidget* InWidget, const FElevatedHome& InHome);
	/**
	 * Whether InWidget is drawn in front of InPopup: on the popup's root canvas, in a canvas sorted above the popup's, and
	 * in no open popup itself (a player's own popups are ordered by their chain, another player's are not a layer put up
	 * over this one). What a press or the focus on a dialog, a page or a modal shown after the popup opened is on.
	 */
	bool IsInFrontOfPopup(const UDreamWidget* InWidget, const UDreamWidget* InPopup) const;
	/** Bind the post-layout hook while a popup is open, and let it go when none is. */
	void UpdateLayoutHook();
	/** After every frame's layout passes: the openers checked, then the popups placed or moved after their openers. */
	void HandleLayoutPassesFinished();
	/** Forget the popups destroyed while open -- and close their children -- with their owners told. */
	void SweepVanishedPopups();
	/** Put player InUserIndex's sheet up, under their lowest open popup, while one of them consumes outside presses; down otherwise. */
	void RefreshSheet(int32 InUserIndex);
};
