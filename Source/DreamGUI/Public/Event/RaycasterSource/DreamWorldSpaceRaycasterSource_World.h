// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterSource_World.generated.h"



UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISceneComponentDirection :uint8
{
	PositiveX		UMETA(DisplayName = "X+"),
	NegativeX		UMETA(DisplayName = "X-"),
	PositiveY		UMETA(DisplayName = "Y+"),
	NegativeY		UMETA(DisplayName = "Y-"),
	PositiveZ		UMETA(DisplayName = "Z+"),
	NegativeZ		UMETA(DisplayName = "Z-"),
};

/**
 * If VR mode, you can use this component to emit ray from hand controller
 */
UCLASS(ClassGroup = DreamGUI, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWorldSpaceRaycasterSource_World : public UDreamWorldSpaceRaycasterSource
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	EDreamUISceneComponentDirection RayDirectionType = EDreamUISceneComponentDirection::PositiveX;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = DreamGUI)
	USceneComponent* TargetSceneComp = nullptr;
	/** drag threshold relate to line trace distance? If true then use ray distance as drag threshold */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = DreamGUI)
	bool bDragThresholdRelateToRayDistance = true;
	/** if bDragThresholdRelateToRayDistance is true, then multiply the ray distance with this value and use the result as drag threshold */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = DreamGUI, meta=(EditCondition=bDragThresholdRelateToRayDistance))
	float RayDistanceMultiply = 0.003f;

public:
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
};

/*
 * This is a preset actor that contains a DreamWorldSpaceRaycasterSource_World component
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamWorldSpaceRaycasterSource_World_Actor : public ADreamWorldSpaceRaycasterSourceActor
{
	GENERATED_BODY()

public:
	ADreamWorldSpaceRaycasterSource_World_Actor();
};
