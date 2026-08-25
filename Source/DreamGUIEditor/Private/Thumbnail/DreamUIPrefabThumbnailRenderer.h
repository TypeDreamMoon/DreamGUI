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
	virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override;

	virtual void BeginDestroy()override;

private:
	// One empty preview world, reused across draws; the widget tree inside it lives only for the
	// duration of a single Draw call (see FDreamUIPrefabThumbnailScene::ClearPrefab).
	TUniquePtr<FDreamUIPrefabThumbnailScene> ThumbnailScene;
};
