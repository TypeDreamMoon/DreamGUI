// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "ThumbnailHelpers.h"
#include "LexUISpriteThumbnailRenderer.generated.h"

UCLASS()
class ULexUISpriteThumbnailRenderer :public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	ULexUISpriteThumbnailRenderer();

	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, bool bAdditionalViewFamily)override;

	virtual void BeginDestroy()override;

protected:
	void DrawFrame(class ULexUISpriteData* Sprite, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, FBoxSphereBounds* OverrideRenderBounds);
	void DrawGrid(int32 X, int32 Y, uint32 Width, uint32 Height, FCanvas* Canvas);
};