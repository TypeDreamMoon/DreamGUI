// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "DreamUISpriteDataBaseObjectThumbnailRenderer.generated.h"

UCLASS()
class UDreamUISpriteDataBaseObjectThumbnailRenderer :public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	UDreamUISpriteDataBaseObjectThumbnailRenderer();

	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, bool bAdditionalViewFamily)override;

	virtual void BeginDestroy()override;

protected:
	void DrawFrame(class UDreamUISpriteData_BaseObject* Sprite, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, FBoxSphereBounds* OverrideRenderBounds);
	void DrawGrid(int32 X, int32 Y, uint32 Width, uint32 Height, FCanvas* Canvas);
};