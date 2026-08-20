// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterSource_Mouse.generated.h"

#define BUILD_VP_MATRIX_FROM_CAMERA_MANAGER 0

/**
 * This is for standalone mouse input, it will emit a ray from main viewport mouse position
 */
UCLASS(ClassGroup = DreamGUI, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWorldSpaceRaycasterSource_Mouse : public UDreamWorldSpaceRaycasterSource
{
	GENERATED_BODY()
public:
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
#if BUILD_VP_MATRIX_FROM_CAMERA_MANAGER
private:
	FMatrix ComputeViewProjectionMatrix(APlayerCameraManager* CameraManager, const FIntPoint& ScreenSize);
	void DeprojectViewPointToWorldForMainViewport(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldLocation, FVector& OutWorldDirection);
#endif
};

/*
 * This is a preset actor that contains a DreamWorldSpaceRaycasterSource_Mouse component
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamWorldSpaceRaycasterSource_Mouse_Actor : public ADreamWorldSpaceRaycasterSourceActor
{
	GENERATED_BODY()

public:
	ADreamWorldSpaceRaycasterSource_Mouse_Actor();
};

