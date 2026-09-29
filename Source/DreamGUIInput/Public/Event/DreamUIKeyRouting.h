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
 * meaning -- navigation, confirm, Back, paging. The preset actors and the Slate input source both route through here,
 * so a key means the same thing whichever of them heard it.
 *
 * A null InModifiers reads the chord from the player's controller, which is right for a key that arrived through it;
 * a source that has the chord in hand -- Slate's key event carries it -- passes it.
 */
namespace DreamUIKeyRouting
{
	/**
	 * Keys that act as confirm: Slate's Accept (FNavigationConfig's KeyActionRules) -- Enter, the space bar and the pad's
	 * bottom face button. Gamepad and keyboard together on purpose: a player can move with the stick and confirm with
	 * Enter in the same session.
	 */
	DREAMGUIINPUT_API TConstArrayView<FKey> GetConfirmKeys();
	/** Keys that mean Back when nothing has bound an action to them. */
	DREAMGUIINPUT_API TConstArrayView<FKey> GetBackKeys();
	/** Direction keys and the direction each means. Tab is Next, and Prev with shift held. */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, EDreamUINavigationDirection>> GetDirectionKeys();
	/** Keys that page the scrolling container around the focus, and by how many screenfuls. */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, float>> GetPageKeys();
	/** Home and End: all the way to one extent. The bool is "towards the start". */
	DREAMGUIINPUT_API TConstArrayView<TPair<FKey, bool>> GetExtentKeys();

	/** The direction InKey means, with Tab turned into Prev while shift is held; None for a key that is not a direction. */
	DREAMGUIINPUT_API EDreamUINavigationDirection GetDirectionForKey(const FKey& InKey, bool bInShiftDown);
	/** Whether InKey has a built-in meaning of its own: confirm, a direction, a page or an extent. Back is only a fallback, so it is not one. */
	DREAMGUIINPUT_API bool IsNavigationKey(const FKey& InKey);

	/** Whether the UI takes input while the game is paused: the inverse of the screen-space pause setting, read when asked. */
	DREAMGUIINPUT_API bool ShouldReceiveInputWhilePaused();
	/** True while InWorld is paused and the settings pause the UI with it: the state in which a key is dropped. */
	DREAMGUIINPUT_API bool IsInputSuspendedByGamePause(const UWorld* InWorld);

	/** What a key acts on for InUser: the navigation highlight while there is one, else the player's focus. */
	DREAMGUIINPUT_API UDreamWidget* GetKeyTarget(const UDreamUIInputUser* InUser);

	/** A confirm key, pressed or released: the bindings, the virtual cursor's click, then a press of the navigation highlight. */
	DREAMGUIINPUT_API bool RouteConfirmKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/** A direction key: the bindings, the virtual cursor (which drops it), then a navigation step. */
	DREAMGUIINPUT_API bool RouteDirectionKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/** RouteDirectionKey, with the direction already resolved by whoever heard the key. */
	DREAMGUIINPUT_API bool RouteDirectionKeyAs(UDreamUIInputUser* InUser, const FKey& InKey, EDreamUINavigationDirection InDirection, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/** A page or extent key, pressed: the bindings, then the scrolling container around what the key acts on. */
	DREAMGUIINPUT_API bool RouteScrollKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers = nullptr);
	/** Any other key: the bindings, then -- for a Back key nobody took -- a drag cancelled, or Back. */
	DREAMGUIINPUT_API bool RouteOtherKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers = nullptr);
	/**
	 * A key for the field InUser is typing into, when it is one of the keys the field types with: taken -- and, for a
	 * press or a repeat, typed -- or not. What comes first for every key a source hears.
	 */
	DREAMGUIINPUT_API bool RouteTextKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers);
	/**
	 * Any key, whichever of the four above it is -- after the field InUser is typing into, which takes the keys it types
	 * with before anything else does. What a source that hears every key calls. True when something took it; typing
	 * is reported in bOutTyped, since a key typed into a field is never anything else's.
	 */
	DREAMGUIINPUT_API bool RouteKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers, bool& bOutTyped);
}
