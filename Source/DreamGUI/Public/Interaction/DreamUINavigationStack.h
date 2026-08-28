// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamUINavigationStack.generated.h"

class UDreamUINavigationScope;
class UDreamWidget;
class UUISelectable;

/**
 * The order screens were opened in, per player, and the focus that goes with it.
 *
 * Kept apart from the widget hierarchy on purpose: a dialog opened from a page belongs in front of
 * that page whether or not it is parented under it, and "in front" is a property of when it opened,
 * not of where it sits in the tree.
 *
 * Everything here is per user index. Split-screen players have their own stacks and their own focus,
 * and one player opening a dialog must not move the other player's focus.
 */
UCLASS()
class DREAMGUI_API UDreamUINavigationStack : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
	virtual void Deinitialize()override;

	static UDreamUINavigationStack* Get(const UObject* WorldContextObject);

	/**
	 * Push InScope to the top of its player's stack. The scope losing the top spot is asked to
	 * remember where focus was first, then focus moves to wherever the new scope wants it.
	 * Pushing an already-active scope re-raises it rather than stacking a second copy.
	 */
	void PushScope(UDreamUINavigationScope* InScope);
	/**
	 * Remove InScope. When it was the top, focus is restored from whatever is now on top; popping one
	 * buried in the middle just removes it, since focus was never with it.
	 */
	void PopScope(UDreamUINavigationScope* InScope);

	/** Top of InUserIndex's stack, skipping any scope that has gone stale. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	UDreamUINavigationScope* GetActiveScope(int32 InUserIndex = 0)const;
	/**
	 * The top scope that confines navigation and contains InWidget. Null when navigation is free --
	 * no scope, a scope that does not confine, or focus currently outside the confining one, which
	 * happens while a scope is opening and is not a reason to refuse every move.
	 *
	 * InUserIndex of INDEX_NONE asks every player's top scope. That is what a selectable itself has to
	 * do: it is asked which way to move without being told who is moving, and in split-screen the two
	 * players' scopes contain different widgets anyway, so containment settles it.
	 */
	UDreamUINavigationScope* FindConfiningScopeFor(const UDreamWidget* InWidget, int32 InUserIndex = INDEX_NONE)const;

	/** InUserIndex's open screens, topmost first. */
	void GetScopeStack(int32 InUserIndex, TArray<UDreamUINavigationScope*>& OutScopes)const;

	/**
	 * Send Back down InUserIndex's stack. A field being edited swallows it first -- cancelling the edit
	 * is what the player means, not closing the screen out from under them. Then each screen from the
	 * top down is offered it, and closes if it neither handled it nor opted out of closing.
	 * @return true when something took it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	bool HandleBack(int32 InUserIndex = 0);

	/** Whatever holds focus for InUserIndex right now, or null. */
	static UUISelectable* GetFocusedSelectable(const UObject* WorldContextObject, int32 InUserIndex);
	/**
	 * Put focus on InSelectable: both the event system's selection, which is what a control reads to
	 * draw itself focused, and the navigation cursor, which is where the next directional move starts
	 * from. Setting only one of them is the bug this exists to prevent -- the highlight and the next
	 * move would then disagree about where the player is.
	 */
	static bool FocusSelectable(const UObject* WorldContextObject, int32 InUserIndex, UUISelectable* InSelectable);

private:
	/** Push order, all users interleaved. Tiny enough that filtering beats a map of arrays. */
	UPROPERTY()
	TArray<TWeakObjectPtr<UDreamUINavigationScope>> Scopes;

	/** Drop entries whose scope has been destroyed; they would otherwise sit on top forever. */
	void RemoveStaleScopes();
	/** Focus whatever the top scope for InUserIndex asks for. No-op when that stack is empty. */
	void RestoreFocusForTopScope(int32 InUserIndex);
};
