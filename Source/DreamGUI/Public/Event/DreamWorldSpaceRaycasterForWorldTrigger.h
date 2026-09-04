// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterForWorldTrigger.generated.h"

/**
 * Raycast on common world space objects like StaticMesh and Trigger.
 *
 * NOT spawnable, deliberately: its Raycast forwards to UDreamBaseRaycaster::RaycastWorld, which has
 * no implementation -- it was a check(0) until this was noticed, so adding this component to an
 * actor was enough to take the process down on the first pointer frame. It now finds nothing
 * instead. The meta stays off until RaycastWorld exists, so nobody can place one by accident.
 */
UCLASS(ClassGroup = DreamGUI)
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
