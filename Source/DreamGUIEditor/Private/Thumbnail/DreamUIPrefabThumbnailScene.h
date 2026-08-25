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
	// The widget tree only exists between SetPrefab and ClearPrefab. The mesh materials bind raw
	// texture resource pointers, so a tree kept alive across frames re-renders with dangling
	// pointers once anything else frees those resources; every render must tear down before
	// returning to the caller.
	void ClearPrefab();
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
	FBoxSphereBounds PreviewBounds;
};
