// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUINestedSurface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
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
class DREAMGUIEXTENSIONS_API IDreamUIRenderTargetInteractionSourceInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	UDreamCanvas* GetTargetCanvas()const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	bool PerformLineTrace(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV);
};

/**
 * Lets pointers reach the UI a DreamUICanvas with RenderMode of RenderTarget draws onto a mesh.
 * This component should be placed on a actor which have a IDreamUIRenderTargetInteractionSourceInterface component.
 *
 * A nested surface (IDreamUINestedSurface): when a pointer's world ray lands on this actor, the input system asks
 * this component where on the target canvas the ray lands, and the widget there is what that pointer is over. The
 * one pipeline serves it -- every pointer and every player with its own state inside the surface, the broadcasts
 * tooltips and drag visuals hang off, double clicks. It used to run a pipeline of its own in its tick, with one
 * synthesised pointer for every pointer and player there was, and none of that.
 *
 * The pointer interfaces are still implemented, and do nothing but answer bAllowEventBubbleUp: over a part of the
 * surface with no widget, the actor is what the pointer is over, and its events bubble as they always have.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUIEXTENSIONS_API UDreamUIRenderTargetInteraction : public UDreamScreenSpaceRaycaster
	, public IDreamUINestedSurface
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerDoubleClickInterface
	, public IDreamPointerScrollInterface
{
	GENERATED_BODY()

public:
	UDreamUIRenderTargetInteraction();
	virtual void ActivateRaycaster()override;
	virtual void DeactivateRaycaster()override;

	//~ Begin IDreamUINestedSurface
	virtual bool ResolveNestedHit(const FDreamUIHitResultContainer& InOuterHit, UDreamPointerEventData* InPointer,
		FDreamUIHitResultContainer& OutInnerHit) override;
	//~ End IDreamUINestedSurface

	/** The canvas this surface shows, once it has been found on the actor's source component. */
	UDreamCanvas* GetSurfaceCanvas() const { return TargetCanvas.Get(); }
protected:
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bAllowEventBubbleUp = false;
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TWeakObjectPtr<UDreamCanvas> TargetCanvas = nullptr;
	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI, AdvancedDisplay) TObjectPtr<UActorComponent> LineTraceSource = nullptr;
	/**
	 * Find the source component on this actor and the canvas it shows, once. False, with an error in the log, when
	 * the actor has no source or the source no canvas.
	 */
	bool ResolveSource();
	/** The error for a missing source or canvas is logged once, not once per pointer per frame. */
	bool bReportedMissingSource = false;

	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override { return true; }
	// ShouldStartDrag is deliberately NOT overridden: nothing about a render-target surface changes how far a pointer
	// has to travel before it counts as a drag.
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;
};
