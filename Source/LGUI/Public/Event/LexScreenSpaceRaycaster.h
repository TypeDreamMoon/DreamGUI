// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexBaseRaycaster.h"
#include "LexScreenSpaceRaycaster.generated.h"

class ULexCanvas;
enum class ELexRenderMode :uint8;

/**
 * Perform a raycaster interaction for ScreenSpaceUI.
 * This component should be placed on a actor which have a LexCanvas, and RenderMode should set to ScreenSpaceOverlay.
 */
UCLASS(ClassGroup = LGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class LGUI_API ULexScreenSpaceRaycaster : public ULexBaseRaycaster
{
	GENERATED_BODY()
	
public:	
	ULexScreenSpaceRaycaster();
	virtual void BeginPlay()override;
protected:
	
	TWeakObjectPtr<ULexCanvas> RootCanvas = nullptr;

	virtual bool ShouldSkipCanvas(class ULexCanvas* UICanvas)override;
	TArray<ELexRenderMode> RenderModeArray;
public:
	virtual bool GetAffectByGamePause()const override;
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData)override;
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)override;
	virtual bool Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)override;

	static void DeprojectViewPointToWorld(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldLocation, FVector& OutWorldDirection);
};
