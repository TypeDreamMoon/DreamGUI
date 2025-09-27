// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CollisionQueryParams.h"
#include "Event/LexPointerEventData.h"
#include "Engine/HitResult.h"
#include "LexBaseRaycaster.generated.h"

enum class ELexRenderMode :uint8;

/** 
 * Base interaction component that perform a raycast hit test
 */
UCLASS(Abstract)
class LGUI_API ULexBaseRaycaster : public USceneComponent
{
	GENERATED_BODY()
	
public:	
	ULexBaseRaycaster();
protected:
	virtual void BeginPlay()override;
	virtual void Activate(bool bReset = false)override;
	virtual void Deactivate()override;
	virtual void OnUnregister()override;

	friend class FUIBaseRaycasterCustomization;

	/** temp array, hit result */
	TArray<FHitResult> MultiHitResult;
protected:
	/**
	 * Link PointerID, limit this raycaster to work on specific pointer. This is useful when multiple pointer interact in same level.
	 * Default is -1, means this raycaster will work on all pointers.
	 */
	UPROPERTY(EditAnywhere, Category = LGUI)
		int32 PointerID = INDEX_NONE;
	/** 
	 * For multiple LexUIBaseRaycasters with same depth, LexUI will line trace them all and sort result on hit distance.
	 * For multiple LexUIBaseRaycasters with different depth, LexUI will sort raycasters on depth, and line trace from highest depth to lowest, if hit anything then stop line trace.
	 */
	UPROPERTY(EditAnywhere, Category = LGUI)
		int32 Depth = 0;
	/** line trace ray emit length */
	UPROPERTY(EditAnywhere, Category = LGUI)
		float RayLength = 100000;
	UPROPERTY(EditAnywhere, Category = LGUI)
		TEnumAsByte<ETraceTypeQuery> TraceChannel;
	UPROPERTY(EditAnywhere, Category = LGUI)
		ELexUIEventFireType EventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;
	/** click/drag threshold, calculated in target's local space */
	UPROPERTY(EditAnywhere, Category = LGUI)
		float ClickThreshold = 5;
	/** hold press for a little while to entering drag mode */
	UPROPERTY(EditAnywhere, Category = LGUI)
		bool bHoldToDrag = false;
	/** hold press for "holdToDragTime" to entering drag mode */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (EditCondition = "bHoldToDrag"))
		float HoldToDragTime = 0.5f;
	float ClickThresholdSquare = 0;
	FVector CurrentRayOrigin = FVector::ZeroVector, CurrentRayDirection = FVector(1, 0, 0);
public:
	/** Called by raycaster to get ray */
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection) PURE_VIRTUAL(ULGUIBaseRaycaster::GenerateRay, return false;);
	/** Called by InputModule to raycast hit test */
	virtual bool Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray) PURE_VIRTUAL(ULGUIBaseRaycaster::Raycast, return false;);
	/** Called by InputModule to decide if current trigger press need to convert to drag */
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData) PURE_VIRTUAL(ULGUIBaseRaycaster::ShouldStartDrag, return false;);

	UFUNCTION(BlueprintCallable, Category = LGUI)virtual void ActivateRaycaster();
	UFUNCTION(BlueprintCallable, Category = LGUI)virtual void DeactivateRaycaster();

	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetPointerID()const { return PointerID; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetDepth()const { return Depth; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetRayLength()const { return RayLength; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetClickThreshold()const { return ClickThreshold; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexUIEventFireType GetEventFireType()const { return EventFireType; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetHoldToDrag()const { return bHoldToDrag; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetHoldToDragTime()const { return HoldToDragTime; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		virtual bool GetAffectByGamePause()const { return true; }
	float GetClickThresholdSquare()const { return ClickThresholdSquare; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		FVector GetRayOrigin()const { return CurrentRayOrigin; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		FVector GetRayDirection()const { return CurrentRayDirection; }

	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetPointerID(int32 value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetDepth(int32 value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetRayLength(float value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetClickThreshold(float Value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetHoldToDrag(bool Value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetHoldToDragTime(float Value);
protected:
	bool ShouldStartDrag_HoldToDrag(ULexPointerEventData* InPointerEventData);
	virtual bool ShouldSkipCanvas(class ULexCanvas* UICanvas) { return false; }

	bool RaycastUI(ULexPointerEventData* InPointerEventData, const TArray<ELexRenderMode>& InRenderModeArray, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray);
	bool RaycastWorld(bool InRequireFaceIndex, ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray);
};
