// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "LexLayoutInvalidationTestTypes.generated.h"

/**
 * AspectRatio that counts how many times its apply pass runs.
 *
 * Measuring and applying cannot be told apart by looking at the widget: AspectRatio re-solves eagerly from
 * OnDimensionChanged, so the widget is always already sitting on its own answer and a redundant write is
 * absorbed by the setters' equality checks. The call count is the observable - GetLayoutPreferredSize used
 * to reach CalculateSize to produce its number, and that is exactly what must no longer happen.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexApplyCountingAspectRatio : public ULexLayoutSelfAspectRatio
{
	GENERATED_BODY()
public:
	/** Times the apply pass ran. */
	int32 ApplyCount = 0;

	virtual void CalculateSize() override;
};

/**
 * Overlay that photographs its children's geometry at the end of its own arrange pass.
 *
 * This is the observable for the arrange/commit split. A panel used to write each child's rect the
 * moment it decided it, so by the end of a pass the tree already carried the result and anything that
 * measured mid-pass was reading layout output back in as input. Now the pass records into a fragment
 * and the base commits afterwards, so at this point the children must still be exactly as authored.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexArrangeObservingOverlay : public ULexLayoutContainerOverlay
{
	GENERATED_BODY()
public:
	/** Child sizes as seen at the end of the arrange pass, before anything is committed. */
	TArray<FVector2D> SizesDuringArrange;

	virtual void ArrangeChildren() override;
};

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

	virtual void ArrangeChildren() override;
};

/**
 * Overlay that counts how many times it is actually asked to lay out.
 *
 * An overlay writes its children's geometry through the ordinary setters, so before FLayoutWriteScope
 * existed every write re-dirtied the container that produced it and a single geometry change always cost
 * two passes. Counting the calls is the only way to observe that from a test: the arranged result is the
 * same either way, only the amount of work differs.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexLayoutPassCountingOverlay : public ULexLayoutContainerOverlay
{
	GENERATED_BODY()
public:
	/** Times CalculateLayout ran past its own dirty gate, i.e. genuinely recomputed. */
	int32 PassCount = 0;

	virtual void ArrangeChildren() override;
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

	virtual void ArrangeChildren() override;
};
