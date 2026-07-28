// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/LexPanelLayouts.h"
#include "LexLayoutInvalidationTestTypes.generated.h"

/**
 * Overlay that reveals a collapsed widget from inside its own layout pass, one-shot.
 *
 * This reproduces the interesting invalidation timing: SetVisibility during a layout pass reaches
 * MarkRebuildAllLayoutTree either while bIsExecutingLayout swallows it (tick-driven pass) or while an
 * immediate rebuild is iterating the very cache it empties. Either way the revealed subtree must still
 * end up laid out.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexLayoutVisibilityFlipOverlay : public ULexLayoutContainerOverlay
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TObjectPtr<ULexWidget> WidgetToReveal;

	/** How many times the one-shot reveal actually fired. */
	int32 FlipCount = 0;

	virtual void CalculateLayout() override;
};

/**
 * Overlay that immediately rebuilds a batch of unrelated layout roots from inside its own layout pass,
 * one-shot. Each nested rebuild inserts into the manager's layout tree map, forcing it to rehash while an
 * outer CalculateLayoutTree is still iterating — the re-entrancy that must not dangle.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexLayoutReentrantRebuildOverlay : public ULexLayoutContainerOverlay
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULexWidget>> RootsToRebuild;

	/** How many times the one-shot re-entrant rebuild actually fired. */
	int32 ReentryCount = 0;

	virtual void CalculateLayout() override;
};
