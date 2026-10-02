// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UDreamWidget;
class UWorld;

/**
 * Every player's focus around something popup-like -- a dropdown list, a menu, a dialog, a ring menu, a collapsing body,
 * a tab page: where it was when the thing opened, and where it goes when the thing closes.
 *
 * Capture at open, before the popup takes focus. Return at close, before anything is hidden, put back or destroyed:
 * hiding is what clears a focus that cannot stay (UDreamWidget's release of focus from what stopped drawing), and a
 * Return after it would find focus already nowhere. For each player the input system has (UDreamUIInputServices::
 * GetUserIndices), Return acts only when the player's focus is
 *   - inside: the popup root or a descendant of it (a popup lifted to the screen root still counts, by its own subtree), or
 *   - nowhere, although the player had focus at Capture (a click catcher that took focus and was destroyed, a hide);
 * a player who moved focus elsewhere meanwhile, or never had any, is left alone: focus is never taken back. It then
 * focuses, through UDreamUIInputServices::FocusForNavigation, the first usable candidate of: the opener, the focus
 * captured at open, the player's active scope's target (ResolveScopeFocusTarget). Usable: outside the popup root, alive
 * and in play along its whole parent chain, active, drawn, interactable, and accepted by FocusForNavigation. Focus moves
 * in one step -- row to face, never through nothing -- so the popup's deselect handlers see where it went. When no
 * candidate is usable and the focus was inside, it is cleared.
 *
 * Plain data, game thread only. Weak throughout: the opener, the captured widgets and the root may all be gone by Return,
 * which is why the world is remembered too. The public calls are the contract; the members are the implementer's.
 */
struct DREAMGUI_API FDreamFocusReturn
{
	/** Record every player's focus now, and InOpener. Replaces an earlier capture. Does nothing without a world or input. */
	void Capture(UDreamWidget* InOpener);
	/**
	 * Put focus back for the players it concerns (see the struct), then forget the capture. InPopupRoot may already be
	 * gone; "inside" then means nothing. Returns how many players' focus it moved or cleared.
	 */
	int32 Return(const UDreamWidget* InPopupRoot);
	/** Forget the capture without returning anything. */
	void Reset();
	bool IsCaptured() const { return bCaptured; }
	UDreamWidget* GetOpener() const { return Opener.Get(); }

	/**
	 * A Return with nothing captured, for what hides part of a widget rather than opening a popup: a collapsing expandable
	 * area (root: its body, fallback: its header), a tab switch (root: the old page, fallback: the active tab). Every
	 * player whose focus is inside InRoot goes to InFallback, else to the scope's target, else nowhere; a player whose focus
	 * is nowhere is left alone, since nothing says they had any. Returns how many players' focus it moved or cleared.
	 */
	static int32 MoveFocusOutOf(const UDreamWidget* InRoot, UDreamWidget* InFallback);

private:
	struct FUserFocus
	{
		int32 UserIndex = 0;
		/** What the player focused at Capture; null for no focus. */
		TWeakObjectPtr<UDreamWidget> Focused;
		bool bHadFocus = false;
	};
	TWeakObjectPtr<UDreamWidget> Opener;
	TWeakObjectPtr<UWorld> World;
	TArray<FUserFocus> Users;
	bool bCaptured = false;
};
