// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamUIPrefabThumbnailScene.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "DreamUIPrefabThumbnailRenderer.generated.h"

UCLASS()
class UDreamUIPrefabThumbnailRenderer :public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
public:
	UDreamUIPrefabThumbnailRenderer();

	virtual bool CanVisualizeAsset(UObject* Object)override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, bool bAdditionalViewFamily)override;

	virtual void BeginDestroy()override;

private:
	FDreamUIPrefabInstanceThumbnailScene ThumbnailScenes;
};