// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "DreamWidgetBlueprintThumbnailRenderer.generated.h"

class FDreamWidgetThumbnailStage;
class UDreamWidget;
class UDreamWidgetBlueprint;
struct FDreamUIAnchorData;

/**
 * A DreamUI Widget Blueprint's thumbnail: the screen it authors, drawn.
 *
 * Without one, every DreamUI hierarchy in the Content Browser was the same generic Blueprint icon --
 * so a folder of twenty screens was twenty identical tiles and the only way to tell them apart was to
 * read the names. The first answer was a wireframe of the authored anchor rects, and it did not tell
 * them apart either: anchors are not where a widget ends up once a layout has arranged it, so most
 * screens came out as a grey tile with a frame or two on it. What tells one screen from another is
 * how it looks, so that is what is drawn now.
 *
 * The way UMG draws its widget thumbnails: an instance of the class, built exactly as the designer
 * builds its preview (FDreamWidgetPreviewHost::InstancePreview), laid out on the design canvas and
 * drawn there and then. A DreamUI widget is drawn by a canvas into a render target rather than by
 * Slate, so the instance goes into a preview world of the renderer's own, under a render-target canvas
 * whose scaler lays it out at DesignerData.CanvasSize; the manager is ticked and the frame submitted
 * by hand until the target holds the picture, and the instance is taken down again before Draw
 * returns. Nothing of the asset outlives the call, so a recompile or an unload never finds a thumbnail
 * holding on to its class.
 *
 * The wireframe stays for what cannot be drawn: a Blueprint without a compiled class, or a process
 * that never renders.
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
	 * Drawn when the asset has no clean thumbnail and when it is saved, never once a frame. Drawing builds
	 * the whole screen, and the Content Browser draws a Realtime thumbnail on every frame the pointer
	 * rests on it -- a thousand-button screen would hitch the editor for as long as it was hovered.
	 */
	virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override;
	virtual void BeginDestroy() override;

	/**
	 * Where a child sits inside its parent, from its anchor block alone.
	 *
	 * The standard anchored-rect resolution this framework lays out with: the anchors name a
	 * reference rect inside the parent, SizeDelta grows or shrinks it, AnchoredPosition moves the
	 * PIVOT, and the pivot decides which part of the rect that position names. Pure and static
	 * because it is arithmetic, and because the wireframe has no world to ask instead.
	 */
	static FBox2D ResolveAnchoredRect(const FBox2D& InParentRect, const FDreamUIAnchorData& InAnchors);

	/** InCanvasSize letterboxed into InThumbnailRect, keeping its aspect ratio. Never inverted. */
	static FBox2D FitCanvasIntoThumbnail(const FBox2D& InThumbnailRect, FIntPoint InCanvasSize);

private:
	/** The screen drawn into InCanvasRect. False, with nothing drawn, when it cannot be. */
	bool DrawPicture(UDreamWidgetBlueprint* InBlueprint, FIntPoint InCanvasSize, const FBox2D& InCanvasRect, FCanvas* InCanvas);
	/** The fallback: the authored anchor rects, nested, inside InCanvasRect. */
	static void DrawWireframe(UDreamWidgetBlueprint* InBlueprint, const FBox2D& InCanvasRect, FCanvas* InCanvas);

	/** InWidget and its descendants, as rects in thumbnail space, parents before children. */
	static void CollectWidgetRects(const UDreamWidget* InWidget, const FBox2D& InParentRect,
		int32 InDepth, TArray<TPair<FBox2D, int32>>& OutRects);

	/** The preview world and the target the pictures are drawn into, made on the first picture. */
	TSharedPtr<FDreamWidgetThumbnailStage> Stage;
};
