// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "DreamBaseRaycaster.h"
#include "DreamScreenSpaceRaycaster.generated.h"

class UDreamCanvas;
enum class EDreamRenderMode :uint8;

/**
 * Perform a raycaster interaction for ScreenSpaceUI.
 * This component should be placed on a actor which have a DreamCanvas, and RenderMode should set to ScreenSpaceOverlay.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamScreenSpaceRaycaster : public UDreamBaseRaycaster
{
	GENERATED_BODY()
	
public:
	UDreamScreenSpaceRaycaster();
	virtual void BeginPlay()override;
#if WITH_EDITOR
	/**
	 * Keeps DragThresholdSquare in step with a DragThreshold typed into the Details panel.
	 *
	 * Without this the square is only ever recomputed by the constructor, BeginPlay and
	 * SetDragThreshold, so an author who edits the threshold in the editor and then drags in the
	 * viewport is still being measured against whatever the value was when the component was
	 * constructed. Play-in-editor hid it, because BeginPlay catches up before anyone can notice.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	/** Bind the screen-space root used for projection and hit testing. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void SetRootCanvas(UDreamCanvas* InRootCanvas);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	UDreamCanvas* GetRootCanvas() const { return RootCanvas.Get(); }
protected:
	/** ray length for line trace hit */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	float RayLength = 100000;
	/** drag threshold, calculated in target's local space */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	float DragThreshold = 5;
	/** hold press for a little while to entering drag mode */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	bool bHoldToDrag = false;
	/** hold press for "holdToDragTime" to entering drag mode */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (EditCondition = "bHoldToDrag"))
	float HoldToDragTime = 0.5f;
	/**
	 * DragThreshold squared, so the per-move comparison can use DistSquared and skip a square root.
	 * Derived, never authored: every write to DragThreshold has to be followed by a write to this,
	 * which is why DragThreshold is protected and SetDragThreshold is the only supported way to
	 * change it while the game runs.
	 */
	float DragThresholdSquare = 0;
	
	TWeakObjectPtr<UDreamCanvas> RootCanvas = nullptr;
public:
	virtual bool GetAffectByGamePause()const override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResult)override;

	static void DeprojectViewPointToWorld(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldLocation, FVector& OutWorldDirection);

	virtual float GetRayLength()const override { return RayLength; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetDragThreshold()const { return DragThreshold; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetHoldToDrag()const { return bHoldToDrag; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetHoldToDragTime()const { return HoldToDragTime; }
	float GetDragThresholdSquare()const { return DragThresholdSquare; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRayLength(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDragThreshold(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDrag(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDragTime(float Value);
};
