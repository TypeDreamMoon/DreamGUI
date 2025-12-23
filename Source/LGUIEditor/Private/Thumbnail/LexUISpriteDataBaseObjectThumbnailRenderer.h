// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "LexUISpriteDataBaseObjectThumbnailRenderer.generated.h"

UCLASS()
class ULexUISpriteDataBaseObjectThumbnailRenderer :public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	ULexUISpriteDataBaseObjectThumbnailRenderer();

	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, bool bAdditionalViewFamily)override;

	virtual void BeginDestroy()override;

protected:
	void DrawFrame(class ULexUISpriteData_BaseObject* Sprite, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, FBoxSphereBounds* OverrideRenderBounds);
	void DrawGrid(int32 X, int32 Y, uint32 Width, uint32 Height, FCanvas* Canvas);
};