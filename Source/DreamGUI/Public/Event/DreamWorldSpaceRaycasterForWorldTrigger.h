// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterForWorldTrigger.generated.h"

/**
 * Raycast on common world space objects like StaticMesh and Trigger
 */
UCLASS(ClassGroup = DreamGUI, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWorldSpaceRaycasterForWorldTrigger : public UDreamWorldSpaceRaycasterBase
{
	GENERATED_BODY()

public:
	UDreamWorldSpaceRaycasterForWorldTrigger();

protected:
	/** Will get FaceIndex when line trace world object's mesh. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bRequireFaceIndex = false;
	
	virtual void BeginPlay() override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;
};
