// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/LexBaseRaycaster.h"
#include "Event/Interface/LexPointerEnterExitInterface.h"
#include "Event/Interface/LexPointerDownUpInterface.h"
#include "Event/Interface/LexPointerScrollInterface.h"
#include "LGUIRenderTargetInteraction.generated.h"

class ULexCanvas;
class ULexEventSystem;
enum class ELexRenderMode :uint8;

/**
 * Interface for LGUIRenderTargetInteraction to provide raycast info.
 */
UINTERFACE(Blueprintable, MinimalAPI)
class ULGUIRenderTargetInteractionSourceInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for LGUIRenderTargetInteraction to provide raycast info.
 */
class LGUI_API ILGUIRenderTargetInteractionSourceInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
	ULexCanvas* GetTargetCanvas()const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
	bool PerformLineTrace(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV);
};

/**
 * Perform a raycaster and interaction for LGUICanvas with RenderMode of RenderTarget.
 * This component should be placed on a actor which have a ILGUIRenderTargetInteractionSourceInterface component.
 */
UCLASS(ClassGroup = LGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class LGUI_API ULGUIRenderTargetInteraction : public ULexBaseRaycaster
	, public ILexPointerEnterExitInterface
	, public ILexPointerDownUpInterface
	, public ILexPointerScrollInterface
{
	GENERATED_BODY()
	
public:	
	ULGUIRenderTargetInteraction();
	virtual void BeginPlay()override;
	virtual void OnRegister()override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)override;
	
	virtual void ActivateRaycaster()override;
	virtual void DeactivateRaycaster()override;
protected:
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = LGUI)
		bool bAllowEventBubbleUp = false;

	UPROPERTY(VisibleAnywhere, Transient, Category = LGUI, AdvancedDisplay) TWeakObjectPtr<ULexCanvas> TargetCanvas = nullptr;
	UPROPERTY(VisibleAnywhere, Transient, Category = LGUI, AdvancedDisplay) TObjectPtr<UActorComponent> LineTraceSource = nullptr;
	UPROPERTY(VisibleAnywhere, Transient, Category = LGUI, AdvancedDisplay) TObjectPtr<ULexPointerEventData> PointerEventData = nullptr;
	TWeakObjectPtr<ULexPointerEventData> InputPointerEventData = nullptr;

	TArray<ELexRenderMode> RenderModeArray;
	virtual bool ShouldSkipCanvas(class ULexCanvas* UICanvas)override;
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)override { return true; }
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData)override;
	virtual bool Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)override;

	virtual bool OnPointerEnter_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerExit_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerScroll_Implementation(ULexPointerEventData* eventData)override;

	bool LineTrace(FLexUIHitResult& hitResult);
};
