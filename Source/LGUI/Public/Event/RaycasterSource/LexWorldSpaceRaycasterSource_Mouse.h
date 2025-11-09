// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "LexWorldSpaceRaycasterSource_Mouse.generated.h"

#define BUILD_VP_MATRIX_FROM_CAMERA_MANAGER 0

/**
 * This is for standalone mouse input, it will emit a ray from main viewport mouse position
 */
UCLASS(ClassGroup = LGUI, meta=(BlueprintSpawnableComponent))
class LGUI_API ULexWorldSpaceRaycasterSource_Mouse : public ULexWorldSpaceRaycasterSource
{
	GENERATED_BODY()
public:
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)override;
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData)override;
#if BUILD_VP_MATRIX_FROM_CAMERA_MANAGER
private:
	FMatrix ComputeViewProjectionMatrix(APlayerCameraManager* CameraManager, const FIntPoint& ScreenSize);
	void DeprojectViewPointToWorldForMainViewport(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldLocation, FVector& OutWorldDirection);
#endif
};

/*
 * This is a preset actor that contains a LexWorldSpaceRaycasterSource_Mouse component
 */
UCLASS(ClassGroup = LGUI)
class LGUI_API ALexWorldSpaceRaycasterSource_Mouse_Actor : public ALexWorldSpaceRaycasterSourceActor
{
	GENERATED_BODY()

public:
	ALexWorldSpaceRaycasterSource_Mouse_Actor();
};

