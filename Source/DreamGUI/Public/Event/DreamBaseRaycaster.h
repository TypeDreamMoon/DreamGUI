// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CollisionQueryParams.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamPointerEventData.h"
#include "Engine/HitResult.h"
#include "DreamBaseRaycaster.generated.h"

/** 
 * Base interaction component that perform a raycast hit test
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamBaseRaycaster : public UActorComponent
{
	GENERATED_BODY()
	
public:	
	UDreamBaseRaycaster();
protected:
	virtual void BeginPlay()override;
	virtual void Activate(bool bReset = false)override;
	virtual void Deactivate()override;
	virtual void OnUnregister()override;

	friend class FUIBaseRaycasterCustomization;

protected:
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	int UserIndex = 0;
	/**
	 * Link PointerID, limit this raycaster to work on specific pointer. This is useful when multiple pointer interact in same level.
	 * Default is -1, means this raycaster will work on all pointers.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		int32 PointerID = INDEX_NONE;
	
	FVector CurrentRayOrigin = FVector::ZeroVector, CurrentRayDirection = FVector(1, 0, 0);
	float CurrentRayLength = 0.0f;
public:
	/** Called by raycaster to get ray */
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength) PURE_VIRTUAL(UDreamGUIBaseRaycaster::GenerateRay, return false;);
	/** Called by InputModule to raycast hit test */
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray) PURE_VIRTUAL(UDreamGUIBaseRaycaster::Raycast, );
	/** Called by InputModule to decide if current trigger press need to convert to drag */
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData) PURE_VIRTUAL(UDreamBaseRaycaster::ShouldStartDrag, return false;);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)virtual void ActivateRaycaster();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)virtual void DeactivateRaycaster();

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetUserIndex()const{return UserIndex;}

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int32 GetPointerID()const { return PointerID; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual bool GetAffectByGamePause()const { return true; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FVector GetRayOrigin()const { return CurrentRayOrigin; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FVector GetRayDirection()const { return CurrentRayDirection; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	virtual float GetRayLength()const { return CurrentRayLength; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetPointerID(int32 Value);
protected:
	void RaycastUI(UDreamPointerEventData* InPointerEventData, UDreamCanvas* InRootCanvas, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray);
	void RaycastWorld(UDreamPointerEventData* InPointerEventData, bool InRequireFaceIndex, ETraceTypeQuery InTraceChannel, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray);
};
