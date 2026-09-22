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
	/**
	 * Whether this press landed close enough to where the pointer's last click was PRESSED to be the
	 * second press of a double click. Called by the input module at a press that would otherwise
	 * continue a click run (same widget, same button, inside DoubleClickTime).
	 *
	 * Measured the way ShouldStartDrag measures a drag, against the same threshold, and no new knob:
	 * on the desktop the double-click rectangle and the drag threshold are the same few pixels
	 * (Windows' SM_CXDOUBLECLK and SM_CXDRAG both default to 4), and Slate's double click is the
	 * platform's, so a second press that has moved as far as a drag would have is a new press, not a
	 * double click. The default answers yes: a raycaster with no distance of its own to measure adds
	 * no condition, and the double click is decided by the widget, the button and the time alone.
	 */
	virtual bool IsWithinDoubleClickDistance(const UDreamPointerEventData* InPointerEventData) const { return true; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)virtual void ActivateRaycaster();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)virtual void DeactivateRaycaster();

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetUserIndex()const{return UserIndex;}

	/**
	 * Which local player this raycaster speaks for -- the same index as that player's event system.
	 *
	 * Authored in the Details panel for a placed raycaster; this setter exists for the ones the screen
	 * subsystem creates, one per local player, which have no author to type it.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetUserIndex(int InUserIndex){ UserIndex = InUserIndex; }

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
	/**
	 * Trace the world along this raycaster's ray and report what is in the way.
	 *
	 * At most one result, and it has no Widget -- a world primitive is not one -- so it acts purely as
	 * an occluder: the input module sorts all hits by distance, so a world hit in front of a world-space
	 * panel takes the pointer away from it. One, because what blocks a pointer is the nearest blocking
	 * hit; overlap-only volumes are not walls and a multi trace would report them as if they were. See
	 * the definition for why occlusion is the whole of what a world hit can mean in a widget-dispatched
	 * event model.
	 */
	void RaycastWorld(UDreamPointerEventData* InPointerEventData, bool InRequireFaceIndex, ETraceTypeQuery InTraceChannel, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray);
};
