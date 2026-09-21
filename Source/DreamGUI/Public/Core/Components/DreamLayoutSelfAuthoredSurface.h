// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamLayout.h"
#include "DreamLayoutSelfAuthoredSurface.generated.h"

/**
 * The widget's desired size is its AUTHORED size, per axis -- the measure walk stops here.
 *
 * Made for controls that host scrolled content. A viewport's size is the author's statement, never
 * its content's: the column behind a list is as tall as ALL the rows, and the measure walk
 * (UDreamPanelLayoutBase::GetDesiredSize) crosses every container-less widget on its way down, so
 * without a boundary an Auto consumer is handed the scroll range as a footprint. Worse, the answer
 * is not even stable -- it depends on whether the row labels happen to have a text layout at the
 * instant somebody asks, which is how a list's height flapped between one wrapped label and the
 * whole column (59 <-> 1299) in the designer.
 *
 * WHY A LAYOUT-SELF AND NOT IgnoreLayout ON THE INNER TREE
 * --------------------------------------------------------
 * IgnoreLayout answers a different question ("nobody arranges me") and it answers it in TWO
 * pipelines: the measure walk skips the subtree, but the invalidation walk out of
 * UDreamWidget::MarkLayoutForRebuild ALSO breaks on it. A list's inner nodes invalidate on every
 * rebuild and every scroll-furniture pass; with the walk broken at the boundary their dirty root
 * degraded from "the consumer's container" to "whatever container-less inner node spoke", each of
 * which seeded its own layout tree in the manager -- 32 passes a frame, an editor too busy to keep
 * a details panel alive. (Measured, 2026-08-31.) The dropdown's popup wears IgnoreLayout correctly
 * because a popup really is arranged by nobody; a control's FACE is arranged by the control, and
 * only its MEASURE must stay private.
 *
 * A layout-self is the walk's own idiom for both halves at once: measurement consults it FIRST and
 * lets a controlled axis override everything below (see GetIntrinsicSize), and the invalidation
 * walk treats it as a marker on its way UP -- the control becomes the rebuild root and the walk
 * carries on to the consumer, exactly as it would without the boundary. Nothing about arrangement
 * changes: like UDreamLayoutSelfSpacer, this class states a preferred size and controls nothing
 * else -- CalculateSize stays empty, panels arrange the control by its slot as always.
 *
 * PER AXIS, ZERO MEANS NO STATEMENT
 * --------------------------------
 * The Slate ImageSize rule, already the family's word for it: an axis whose authored size is zero
 * is an axis the author left to somebody else (a .dui line of `AnchorData.SizeDelta = (0, 100)`
 * under `HorizontalAlignment = Fill` states a height and no width), so only positive axes claim.
 * An unclaimed axis falls back to the ordinary walk -- content for a list's width, which is also
 * what UMG would say.
 *
 * The authored size is read from the panel slot's authored snapshot when there is one -- the one
 * store that layout output cannot reach (MarkLayoutGeometryApplied captures it first, and
 * SyncAuthoredDesiredSizeFromWidget only follows non-layout writes). A widget with no slot (an
 * anchor child, or a control authored bare in a test) reads its own anchor data instead, where a
 * point-anchored axis's SizeDelta IS the authored size and a stretched axis has none to give.
 */
UCLASS(BlueprintType, DisplayName = "Authored Surface")
class DREAMGUI_API UDreamLayoutSelfAuthoredSurface : public UDreamLayoutSelf
{
	GENERATED_BODY()

public:
	virtual FVector2f GetLayoutPreferredSize() const override;
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* Widget) const override;
};
