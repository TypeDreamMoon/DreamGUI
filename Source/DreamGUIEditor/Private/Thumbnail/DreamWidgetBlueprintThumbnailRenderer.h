// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "DreamWidgetBlueprintThumbnailRenderer.generated.h"

class UDreamWidget;
struct FDreamUIAnchorData;

/**
 * A DreamUI Widget Blueprint's thumbnail: a wireframe of the screen it authors.
 *
 * Without one, every DreamUI hierarchy in the Content Browser was the same generic Blueprint icon --
 * so a folder of twenty screens was twenty identical tiles and the only way to tell them apart was to
 * read the names. What distinguishes one screen from another is its SHAPE, so that is what is drawn:
 * the design canvas at its own aspect ratio with the authored hierarchy nested inside it.
 *
 * Canvas drawing, not a rendered scene. The alternative -- instancing the class into a preview world
 * and photographing it -- needs a world, a registered hierarchy and a real RHI for every tile the
 * browser scrolls past, and it would show a screen with no data in it: the runtime fills these from
 * bindings that have nothing to bind to at thumbnail time. A wireframe is honest about being a
 * diagram, costs nothing, and is computed from the authored anchor blocks alone.
 */
UCLASS()
class DREAMGUIEDITOR_API UDreamWidgetBlueprintThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;

	/**
	 * Where a child sits inside its parent, from its anchor block alone.
	 *
	 * The standard anchored-rect resolution this framework lays out with: the anchors name a
	 * reference rect inside the parent, SizeDelta grows or shrinks it, AnchoredPosition moves the
	 * PIVOT, and the pivot decides which part of the rect that position names. Pure and static
	 * because it is arithmetic, and because a thumbnail has no world to ask instead.
	 */
	static FBox2D ResolveAnchoredRect(const FBox2D& InParentRect, const FDreamUIAnchorData& InAnchors);

	/** InCanvasSize letterboxed into InThumbnailRect, keeping its aspect ratio. Never inverted. */
	static FBox2D FitCanvasIntoThumbnail(const FBox2D& InThumbnailRect, FIntPoint InCanvasSize);

private:
	/** InWidget and its descendants, as rects in thumbnail space, parents before children. */
	static void CollectWidgetRects(const UDreamWidget* InWidget, const FBox2D& InParentRect,
		int32 InDepth, TArray<TPair<FBox2D, int32>>& OutRects);
};
