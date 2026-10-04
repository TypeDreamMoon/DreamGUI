// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "DreamGradientThumbnailRenderer.generated.h"

/**
 * A Dream Gradient asset's thumbnail: the gradient painting the tile, as FDreamGradient::Evaluate paints a square box --
 * its type, angle, shape and colour space included -- over a checkerboard where it is transparent.
 */
UCLASS()
class UDreamGradientThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
};
