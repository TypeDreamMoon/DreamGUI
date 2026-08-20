// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamBaseRaycaster.h"
#include "DreamWorldSpaceRaycasterBase.generated.h"

class UDreamWorldSpaceRaycasterBase;

/**
 * Perform a ray source for DreamWorldSpaceRaycaster
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, Abstract, HideCategories = (Sockets, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation))
class DREAMGUI_API UDreamWorldSpaceRaycasterSource : public USceneComponent
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	
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
	float DragThresholdSquare = 0;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetRayLength()const { return RayLength; }
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
	
	/** Generate ray for raycast hit test */
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd);
	/** Should convert press event to drag event? */
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData);
protected:
	/** Generate ray for raycast hit test */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "EmitRay"), Category = "DreamGUI")
		bool ReceiveGenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd);
	/** Should convert press event to drag event? */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ShouldStartDrag"), Category = "DreamGUI")
		bool ReceiveShouldStartDrag(UDreamPointerEventData* InPointerEventData);
};

UCLASS(ClassGroup = DreamGUI, Abstract, HideCategories=(Rendering, Replication, Collision, HLOD, Physics, Networking, Input, Actor, Navigation, LevelInstance, Cooking))
class DREAMGUI_API ADreamWorldSpaceRaycasterSourceActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamWorldSpaceRaycasterSourceActor();
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamWorldSpaceRaycasterSource* GetRaycasterSource()const{return RaycasterSource;}
protected:
	UPROPERTY(Category = "DreamGUI", EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamWorldSpaceRaycasterSource> RaycasterSource;
};

/**
 * Perform a raycaster interaction for WorldSpaceUI and common world space objects.
 */
UCLASS(ClassGroup = DreamGUI, Abstract, Blueprintable)
class DREAMGUI_API UDreamWorldSpaceRaycasterBase : public UDreamBaseRaycaster
{
	GENERATED_BODY()
	
public:	
	UDreamWorldSpaceRaycasterBase();
	virtual void BeginPlay()override;
	virtual void OnRegister()override;
protected:
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TWeakObjectPtr<ADreamWorldSpaceRaycasterSourceActor> RaycasterSourceActor = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	mutable TWeakObjectPtr<UDreamWorldSpaceRaycasterSource> RaycasterSourceObject = nullptr;
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> TraceChannel;
public:
	virtual bool GetAffectByGamePause()const override;
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength) override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData) override;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamWorldSpaceRaycasterSource* GetRaycasterSourceObject()const;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRaycasterSourceObject(UDreamWorldSpaceRaycasterSource* Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRaycasterSourceActor(ADreamWorldSpaceRaycasterSourceActor* Value);
};
