// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "ThumbnailHelpers.h"

class UDreamWidget;
class UDreamUIPrefab;

class FDreamUIPrefabThumbnailScene :public FThumbnailPreviewScene
{
public:
	FDreamUIPrefabThumbnailScene();
	virtual ~FDreamUIPrefabThumbnailScene() override;
	bool IsValidForVisualization();
	void SetPrefab(UDreamUIPrefab* Prefab);
protected:
	virtual void GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom)const override;
	virtual USceneThumbnailInfo* GetSceneThumbnailInfo(const float TargetDistance)const;
	void SpawnPreviewActor();
	void GetBoundsRecursive(UDreamWidget* RootWidget, FBoxSphereBounds& OutBounds)const;
private:
	void ClearOldWidgets();
private:
	int32 NumStartingActors;
	TStrongObjectPtr<UDreamWidget> RootAgentWidget;
	TWeakObjectPtr<UDreamUIPrefab> CurrentPrefab;
	FText CachedPrefabContent;
	FBoxSphereBounds PreviewBounds;
};

class FDreamUIPrefabInstanceThumbnailScene
{
public:
	FDreamUIPrefabInstanceThumbnailScene();

	TSharedPtr<FDreamUIPrefabThumbnailScene> FindThumbnailScene(const FString& InPrefabPath) const;
	TSharedRef<FDreamUIPrefabThumbnailScene> EnsureThumbnailScene(const FString& InPrefabPath);
	void Clear();

private:
	TMap<FString, TSharedPtr<FDreamUIPrefabThumbnailScene>> InstancedThumbnailScenes;
	const int32 MAX_NUM_SCENES = 400;
};
