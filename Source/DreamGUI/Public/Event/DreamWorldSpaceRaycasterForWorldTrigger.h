// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterForWorldTrigger.generated.h"

/**
 * Raycast on common world space objects like StaticMesh and Trigger.
 *
 * What it produces are OCCLUDERS, not hits anything can handle: a world primitive has no widget, so
 * these results take the pointer away from whatever UI is behind them rather than dispatching an
 * event of their own. That is what makes a wall between the player and a world-space panel stop the
 * click, and it is the whole contract -- see UDreamBaseRaycaster::RaycastWorld.
 *
 * Needs a ray source like any other world-space raycaster (RaycasterSourceActor / SourceObject), and
 * its TraceChannel decides what counts as solid.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent))
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
