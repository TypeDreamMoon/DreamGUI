// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamBaseRaycaster.h"
#include "DreamWorldSpaceRaycaster.generated.h"

/** Where in the owning player's view a world-space pointer's ray is aimed. */
UENUM(BlueprintType)
enum class EDreamWorldPointerSource : uint8
{
	/** Through the pointer's own screen position -- a mouse, a touch, a virtual cursor. */
	Mouse,
	/** Through the middle of the view, wherever the pointer happens to be -- a first-person reticle. */
	ScreenCenter,
};

/**
 * Pointer interaction with every world-space canvas ONE local player can reach.
 *
 * One raycaster per player, not one per panel: the ray is aimed by that player's view, and which
 * canvases it can hit is answered by asking the manager for the world-space roots whose TraceChannel
 * matches. Adding, moving or destroying a panel is therefore nothing this component has to be told
 * about, and a panel is not required to carry an interaction component of its own.
 *
 * UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer creates one on a transient
 * "DreamInteractionHost_P%d" actor for any player that needs it. Placing one by hand on any actor
 * overrides that: the manager sees a world-space raycaster already speaking for that UserIndex and
 * leaves it alone, so an authored raycaster with its own pointer source, ray length or drag
 * behaviour is the one that runs.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamWorldSpaceRaycaster : public UDreamBaseRaycaster
{
	GENERATED_BODY()

public:
	UDreamWorldSpaceRaycaster();
	virtual void BeginPlay()override;
#if WITH_EDITOR
	/**
	 * Keeps DragThresholdSquare in step with a DragThreshold typed into the Details panel, for the
	 * same reason UDreamScreenSpaceRaycaster does: otherwise the square is only ever recomputed by
	 * the constructor, BeginPlay and SetDragThreshold, and an author editing the value in the editor
	 * is still measured against whatever it was when the component was constructed.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

protected:
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	EDreamWorldPointerSource PointerSource = EDreamWorldPointerSource::Mouse;
	/**
	 * Let solid world geometry take the pointer off the UI behind it.
	 *
	 * The world hit carries no widget, so it dispatches nothing; it merely wins on distance and the
	 * panel behind it stops being pointed at. Off by default, because a panel floating in front of a
	 * wall is the ordinary case and paying for a line trace per pointer per frame to discover that
	 * is not.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	bool bOccludeByWorld = false;
	/**
	 * Which world-space canvases this raycaster answers for: only roots whose own TraceChannel is the
	 * same. With bOccludeByWorld it is also the collision channel the occlusion trace uses, so "what
	 * this pointer can touch" and "what can block it" stay one setting.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> TraceChannel;
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

public:
	virtual bool GetAffectByGamePause()const override;
	/** Kept virtual so a fixture -- or a hand-written pointer device -- can supply a ray directly. */
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;

	virtual float GetRayLength()const override { return RayLength; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamWorldPointerSource GetPointerSource()const { return PointerSource; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetOccludeByWorld()const { return bOccludeByWorld; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetDragThreshold()const { return DragThreshold; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetHoldToDrag()const { return bHoldToDrag; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetHoldToDragTime()const { return HoldToDragTime; }
	float GetDragThresholdSquare()const { return DragThresholdSquare; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetPointerSource(EDreamWorldPointerSource Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetOccludeByWorld(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRayLength(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDragThreshold(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDrag(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDragTime(float Value);
};
