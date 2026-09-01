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
	/**
	 * The pointer this component synthesises for the UI drawn into the render target. It is a
	 * SECOND pointer, distinct from the one that hit the surface in the world -- that one arrives as
	 * InputPointerEventData and is only a source of rays.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TObjectPtr<UDreamPointerEventData> PointerEventData = nullptr;
	TWeakObjectPtr<UDreamPointerEventData> InputPointerEventData = nullptr;

	/**
	 * PointerEventData, creating it if this is the first caller to need it.
	 *
	 * It used to be built in BeginPlay alone, which quietly made "BeginPlay has run" a precondition
	 * of every pointer handler on this class -- and the handlers are reached from the event system,
	 * not from the component's own tick, so nothing enforced the order. A press that arrived in the
	 * same frame the component registered, or anything driving this component from an editor path
	 * where BeginPlay never runs at all, dereferenced null. Creating it on demand is cheaper than
	 * teaching four call sites to check, and it means an early press is HANDLED rather than dropped,
	 * which matters because dropping the press while delivering the release would leave the
	 * synthesised pointer believing a button it never saw go down had come back up.
	 */
	UDreamPointerEventData* EnsurePointerEventData();

	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override { return true; }
	// ShouldStartDrag is deliberately NOT overridden. There used to be an override here whose body
	// was character-for-character the base class's, which meant a drag threshold or hold-to-drag fix
	// made in UDreamScreenSpaceRaycaster reached every raycaster except this one. Nothing about a
	// render-target surface changes how far a pointer has to travel before it counts as a drag: the
	// positions this reads have already been flattened into the target's own pixel space by the time
	// they reach the event data, so the base class's arithmetic is measuring the right thing.
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	bool LineTrace(FDreamUIHitResultContainer& OutHitResult);
};
