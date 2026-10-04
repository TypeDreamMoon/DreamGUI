// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Event/DreamPointerEventData.h"

class FModifierKeysState;
class UDreamUIInputUser;
class UDreamWidget;
class UWorld;

/**
 * What a player's keys mean to DreamGUI, in the one order every input source offers them: the field the player is
 * typing into, then the action bindings (the focused widget before them), then the virtual cursor, then the built-in
 * meaning -- navigation, confirm, Back, paging, tab switching -- unless the player's input mode is Game
 * (HasBuiltInMeanings). The preset actors and the Slate input source both route through here, so a key means the same
 * thing whichever of them heard it. The built-in meanings act on the player's focus, never on what the mouse is over.
 *
 * A key's release goes where its press went (RouteKeyRelease), not where the key would go if it were pressed now. Each
 * player remembers what took each held key (UDreamUIInputUser::NoteKeyPress), so a field that began its edit, or a
 * binding that was registered, while the key was held does not swallow a release whose press it never saw.
 *
 * A null InModifiers reads the chord from the player's controller, which is right for a key that arrived through it;
 * a source that has the chord in hand -- Slate's key event carries it -- passes it.
 */
namespace DreamUIKeyRouting
{
	/**
	 * Keys that act as confirm: Slate's Accept (FNavigationConfig's KeyActionRules) -- by default Enter, the space bar and
	 * the pad's bottom face button, with the platform's own accept put in (see the tables' note below). Gamepad and
	 * keyboard together on purpose: a player can move with the stick and confirm with Enter in the same session.
	 */
	DREAMGUIINPUT_API TConstArrayView<FKey> GetConfirmKeys();
	/** Keys that mean Back when nothing has bound an action to them. */
	DREAMGUIINPUT_API TConstArrayView<FKey> GetBackKeys();
	/**
	 * Direction keys and the direction each means. Tab is Next, and Prev with shift held. A row of Next or Prev is left
	 * out while UDreamGUISettings::bTabNavigation is off: Tab is then an ordinary key.
	 */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, EDreamUINavigationDirection>> GetDirectionKeys();
	/** Keys that page the scrolling container around the focus, and by how many screenfuls: Page Up and Down, the triggers. */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, float>> GetPageKeys();
	/** Home and End: all the way to one extent. The bool is "towards the start". */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, bool>> GetExtentKeys();

	/** The direction InKey means, with Tab turned into Prev while shift is held; None for a key that is not a direction. */
	DREAMGUIINPUT_API EDreamUINavigationDirection GetDirectionForKey(const FKey& InKey, bool bInShiftDown);
	/**
	 * The direction InKey means for a press with this chord: GetDirectionForKey, except that Tab -- a row of Next or
	 * Prev -- is no direction at all while Ctrl, Alt or Cmd is held, as in Slate: Ctrl+Tab is a shortcut, not a step. A
	 * legacy key binding fires for Tab whatever modifiers are held, so whoever hears the key asks this with the chord.
	 */
	DREAMGUIINPUT_API EDreamUINavigationDirection GetDirectionForChord(const FKey& InKey, bool bInShiftDown, bool bInCtrlAltOrCmdDown);
	/** Whether InKey has a built-in meaning of its own: confirm, a direction, a page, an extent or a tab switch. Back is only a fallback, so it is not one. */
	DREAMGUIINPUT_API bool IsNavigationKey(const FKey& InKey);

	/*
	 * The tables above come from the project settings (UDreamGUISettings's ConfirmKeys, BackKeys, DirectionKeys, PageKeys,
	 * ExtentKeys, PreviousTabKeys, NextTabKeys), read when asked: a remap takes effect at the next key.
	 */
	/**
	 * The pad's confirm and Back buttons: the platform's (FPlatformInput::GetGamepadAcceptKey and GetGamepadBackKey, which a
	 * Switch swaps) while UDreamGUISettings::bUsePlatformAcceptBack, else the bottom and the right face buttons.
	 */
	DREAMGUIINPUT_API FKey GetGamepadAcceptKey();
	DREAMGUIINPUT_API FKey GetGamepadBackKey();
	/** Whether InKey confirms (GetConfirmKeys), and whether it is Back (GetBackKeys): what a text field asks to end its edit on the pad's confirm. */
	DREAMGUIINPUT_API bool IsConfirmKey(const FKey& InKey);
	DREAMGUIINPUT_API bool IsBackKey(const FKey& InKey);
	/** -1 for a key of PreviousTabKeys, 1 for one of NextTabKeys, 0 for any other. */
	DREAMGUIINPUT_API int32 GetTabSwitchDelta(const FKey& InKey);
	/**
	 * The widget InUser's tab-switch keys switch tabs on (IDreamUITabSwitchTarget): the nearest one at or above the player's
	 * focus that answers CanSwitchTab; with the focus in none, the first one, depth first, under the player's active scope's
	 * widget, else under their screen root. Null when there is none: the keys are then the game's. What the routing sends
	 * the keys to, and what the action bar shows their prompts for.
	 */
	DREAMGUIINPUT_API UDreamWidget* FindTabSwitchTarget(const UDreamUIInputUser* InUser);

	/**
	 * Whether InUser's keys and pad have their built-in meanings now: false while the player's input mode is Game
	 * (UDreamUINavigationStack::GetEffectiveInputMode), when navigation, confirm, Back, paging, tab switching and the
	 * right stick's scrolling are the game's and the key is left to it -- the bindings, the pointers and typing still work.
	 */
	DREAMGUIINPUT_API bool HasBuiltInMeanings(const UDreamUIInputUser* InUser);
	/**
	 * Whether InUser's Tab has somewhere to go: a domain with a stop in it (FDreamUITabOrder::FindDomain and FindFirstStop,
	 * from the player's focus). What makes a Tab press the UI's rather than the game's, and what tells the Slate guard
	 * that DreamGUI has UI up (UDreamUIInputSubsystem::ShouldSwallowSlateNavigation).
	 */
	DREAMGUIINPUT_API bool HasTabStops(const UDreamUIInputUser* InUser);

	/** Whether the UI takes input while the game is paused: the inverse of the screen-space pause setting, read when asked. */
	DREAMGUIINPUT_API bool ShouldReceiveInputWhilePaused();
	/** True while InWorld is paused and the settings pause the UI with it: the state in which a key is dropped. */
	DREAMGUIINPUT_API bool IsInputSuspendedByGamePause(const UWorld* InWorld);

	/**
	 * What a key acts on for InUser: the player's focus, while it is drawn and interactable -- never the hover highlight.
	 * Where the mouse rests says nothing about where the keys are: Enter presses the focused button, not the one under
	 * the pointer, and the paging keys and the right stick scroll the container around the focus.
	 */
	DREAMGUIINPUT_API UDreamWidget* GetKeyTarget(const UDreamUIInputUser* InUser);

	/** A confirm key, pressed or released: the bindings, the virtual cursor's click, then a press of what the player has focused. */
	DREAMGUIINPUT_API bool RouteConfirmKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/**
	 * A direction key: the bindings, the virtual cursor (which drops it), then a navigation step from the player's focus.
	 * Tab with Ctrl, Alt or Cmd held is no direction (GetDirectionForChord) and goes as RouteOtherKey: the bindings, then
	 * nothing. A Tab counts as taken while the player has a Tab stop to go to (HasTabStops).
	 */
	DREAMGUIINPUT_API bool RouteDirectionKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/** RouteDirectionKey, with the direction already resolved by whoever heard the key; None routes the key as RouteOtherKey. */
	DREAMGUIINPUT_API bool RouteDirectionKeyAs(UDreamUIInputUser* InUser, const FKey& InKey, EDreamUINavigationDirection InDirection, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/** A page or extent key, pressed: the bindings, then the scrolling container around what the key acts on. */
	DREAMGUIINPUT_API bool RouteScrollKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers = nullptr);
	/** A tab-switch key, pressed: the bindings, then IDreamUITabSwitchTarget::SwitchTab on FindTabSwitchTarget's answer. True when either took it. */
	DREAMGUIINPUT_API bool RouteTabSwitchKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers = nullptr);
	/** Any other key: the bindings, then -- for a Back key nobody took -- a drag cancelled, or Back. */
	DREAMGUIINPUT_API bool RouteOtherKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/**
	 * A press or a repeat for the field InUser is typing into, when it is one of the keys the field types with: typed and
	 * taken, or not. What comes first for every press a source hears. A release is not taken here; it goes where its
	 * press went (RouteKeyRelease).
	 */
	DREAMGUIINPUT_API bool RouteTextKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers);
	/**
	 * A key let go of, to whatever took its press and nowhere else: the field that typed it, the binding that took it,
	 * the virtual cursor, or navigation. Every release the field did not type still reaches the focused widget, as UMG's
	 * OnKeyUp does, and a key whose press InUser did not route goes nowhere else. True when the press was taken.
	 */
	DREAMGUIINPUT_API bool RouteKeyRelease(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers = nullptr);
	/**
	 * A key whose release will never come -- it was let go of in another application, after this one lost the focus --
	 * given up where RouteKeyRelease would send its release, except that a confirm it holds, the navigation's or the
	 * virtual cursor's, ends with its up and no click, as a pointer taken away does (UDreamUIInputUser::CancelPointerPress).
	 * A held direction stops, a held binding is let go of. Nothing for a key whose press InUser did not route.
	 */
	DREAMGUIINPUT_API void AbandonKeyPress(UDreamUIInputUser* InUser, const FKey& InKey);
	/**
	 * Any key, whichever of the four above it is -- after the field InUser is typing into, which takes the keys it types
	 * with before anything else does -- and any release, to where its press went. What a source that hears every key
	 * calls. True when something took it; typing is reported in bOutTyped, since a key typed into a field is never
	 * anything else's.
	 */
	DREAMGUIINPUT_API bool RouteKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers, bool& bOutTyped);
}
