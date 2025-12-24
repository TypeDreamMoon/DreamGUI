// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "ThumbnailHelpers.h"
#include "PrefabSystem/LexUIPrefab.h"

class FLGUIPrefabThumbnailScene :public FThumbnailPreviewScene
{
public:
	FLGUIPrefabThumbnailScene();
	bool IsValidForVisualization();
	void SetPrefab(class ULexUIPrefab* Prefab);
protected:
	virtual void GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom)const override;
	virtual USceneThumbnailInfo* GetSceneThumbnailInfo(const float TargetDistance)const;
	void SpawnPreviewActor();
	void GetBoundsRecursive(USceneComponent* RootComp, FBoxSphereBounds& OutBounds, bool& IsFirstPrimitive)const;
private:
	void ClearOldActors();
private:
	int32 NumStartingActors;
	TWeakObjectPtr<class ULexUIPrefab> CurrentPrefab;
	FText CachedPrefabContent;
	FBoxSphereBounds PreviewActorsBound;
	bool bIsUI = false;
};

class FLGUIPrefabInstanceThumbnailScene
{
public:
	FLGUIPrefabInstanceThumbnailScene();

	TSharedPtr<FLGUIPrefabThumbnailScene> FindThumbnailScene(const FString& InPrefabPath) const;
	TSharedRef<FLGUIPrefabThumbnailScene> EnsureThumbnailScene(const FString& InPrefabPath);
	void Clear();

private:
	TMap<FString, TSharedPtr<FLGUIPrefabThumbnailScene>> InstancedThumbnailScenes;
	const int32 MAX_NUM_SCENES = 400;
};