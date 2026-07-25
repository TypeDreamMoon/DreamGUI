// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ThumbnailHelpers.h"

class ULexWidget;
class ULexUIPrefab;

class FLexUIPrefabThumbnailScene :public FThumbnailPreviewScene
{
public:
	FLexUIPrefabThumbnailScene();
	virtual ~FLexUIPrefabThumbnailScene() override;
	bool IsValidForVisualization();
	void SetPrefab(ULexUIPrefab* Prefab);
protected:
	virtual void GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom)const override;
	virtual USceneThumbnailInfo* GetSceneThumbnailInfo(const float TargetDistance)const;
	void SpawnPreviewActor();
	void GetBoundsRecursive(ULexWidget* RootWidget, FBoxSphereBounds& OutBounds)const;
private:
	void ClearOldWidgets();
private:
	int32 NumStartingActors;
	TStrongObjectPtr<ULexWidget> RootAgentWidget;
	TWeakObjectPtr<ULexUIPrefab> CurrentPrefab;
	FText CachedPrefabContent;
	FBoxSphereBounds PreviewBounds;
};

class FLexUIPrefabInstanceThumbnailScene
{
public:
	FLexUIPrefabInstanceThumbnailScene();

	TSharedPtr<FLexUIPrefabThumbnailScene> FindThumbnailScene(const FString& InPrefabPath) const;
	TSharedRef<FLexUIPrefabThumbnailScene> EnsureThumbnailScene(const FString& InPrefabPath);
	void Clear();

private:
	TMap<FString, TSharedPtr<FLexUIPrefabThumbnailScene>> InstancedThumbnailScenes;
	const int32 MAX_NUM_SCENES = 400;
};
