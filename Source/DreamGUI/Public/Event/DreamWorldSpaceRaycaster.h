// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycaster.generated.h"

/**
 * Raycast on world space UI, need DreamCanvas component on same actor
 */
UCLASS(ClassGroup = DreamGUI, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWorldSpaceRaycaster : public UDreamWorldSpaceRaycasterBase
{
	GENERATED_BODY()

public:
	UDreamWorldSpaceRaycaster();

protected:
	TWeakObjectPtr<UDreamCanvas> RootCanvas;
	
	virtual void BeginPlay() override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;
};
