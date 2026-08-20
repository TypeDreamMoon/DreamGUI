// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "DreamUIRenderTargetInteraction.generated.h"

class UDreamCanvas;
class UDreamEventSystem;

/**
 * Interface for DreamUIRenderTargetInteraction to provide raycast info.
 */
UINTERFACE(Blueprintable, MinimalAPI)
class UDreamUIRenderTargetInteractionSourceInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for DreamUIRenderTargetInteraction to provide raycast info.
 */
class DREAMGUI_API IDreamUIRenderTargetInteractionSourceInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	UDreamCanvas* GetTargetCanvas()const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	bool PerformLineTrace(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV);
};

/**
 * Perform a raycaster and interaction for DreamUICanvas with RenderMode of RenderTarget.
 * This component should be placed on a actor which have a IDreamUIRenderTargetInteractionSourceInterface component.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamUIRenderTargetInteraction : public UDreamScreenSpaceRaycaster
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerScrollInterface
{
	GENERATED_BODY()
	
public:	
	UDreamUIRenderTargetInteraction();
	virtual void BeginPlay()override;
	virtual void OnRegister()override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)override;
	
	virtual void ActivateRaycaster()override;
	virtual void DeactivateRaycaster()override;
protected:
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bAllowEventBubbleUp = false;

	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TWeakObjectPtr<UDreamCanvas> TargetCanvas = nullptr;
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TObjectPtr<UActorComponent> LineTraceSource = nullptr;
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TObjectPtr<UDreamPointerEventData> PointerEventData = nullptr;
	TWeakObjectPtr<UDreamPointerEventData> InputPointerEventData = nullptr;

	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override { return true; }
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	bool LineTrace(FDreamUIHitResultContainer& OutHitResult);
};
