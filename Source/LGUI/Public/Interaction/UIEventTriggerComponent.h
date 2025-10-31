// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerEnterExitInterface.h"
#include "Event/Interface/LexPointerDownUpInterface.h"
#include "Event/Interface/LexPointerClickInterface.h"
#include "Event/Interface/LexPointerDragInterface.h"
#include "Event/Interface/LexPointerDragDropInterface.h"
#include "Event/Interface/LexPointerScrollInterface.h"
#include "Event/Interface/LexPointerSelectDeselectInterface.h"

#include "Event/LexUIEventDelegate.h"
#include "Event/LexDelegateDeclaration.h"
#include "Components/ActorComponent.h"
#include "UIEventTriggerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIEventTriggerPointerEvent, ULexPointerEventData*, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIEventTriggerBaseEvent, ULexBaseEventData*, Value);

//a helper component for quick register and setup LexPointerEvent
UCLASS(HideCategories = (Collision, LOD, Physics, Cooking, Rendering, Activation, Actor, Input, Lighting, Mobile), ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIEventTriggerComponent : public UActorComponent
	, public ILexPointerEnterExitInterface
	, public ILexPointerDownUpInterface
	, public ILexPointerClickInterface
	, public ILexPointerDragInterface
	, public ILexPointerDragDropInterface
	, public ILexPointerScrollInterface
	, public ILexPointerSelectDeselectInterface
{
	GENERATED_BODY()
protected:
	//inherited events of this component can bubble up?
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		bool AllowEventBubbleUp = false;
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerEnter = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerExit = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerDown = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerUp = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerClick = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerBeginDrag = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerDrag = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerEndDrag = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerDragDrop = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerScroll = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerSelect = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FLexUIEventDelegate OnPointerDeselect = FLexUIEventDelegate(ELexUIEventDelegateParameterType::PointerEvent);

	FLexUIMulticastDelegatePointerEventData OnPointerEnterCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerExitCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerDownCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerUpCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerClickCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerBeginDragCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerDragCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerEndDragCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerDragDropCPP;
	FLexUIMulticastDelegatePointerEventData OnPointerScrollCPP;
	FLexUIMulticastDelegateBaseEventData OnPointerSelectCPP;
	FLexUIMulticastDelegateBaseEventData OnPointerDeselectCPP;

	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerEnter")
	FUIEventTriggerPointerEvent OnPointerEnterBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerExit")
	FUIEventTriggerPointerEvent OnPointerExitBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerDown")
	FUIEventTriggerPointerEvent OnPointerDownBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerUp")
	FUIEventTriggerPointerEvent OnPointerUpBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerClick")
	FUIEventTriggerPointerEvent OnPointerClickBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerBeginDrag")
	FUIEventTriggerPointerEvent OnPointerBeginDragBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerDrag")
	FUIEventTriggerPointerEvent OnPointerDragBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerEndDrag")
	FUIEventTriggerPointerEvent OnPointerEndDragBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerDragDrop")
	FUIEventTriggerPointerEvent OnPointerDragDropBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerScroll")
	FUIEventTriggerPointerEvent OnPointerScrollBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerSelect")
	FUIEventTriggerBaseEvent OnPointerSelectBP;
	UPROPERTY(BlueprintAssignable, Category = "UIEventTrigger", DisplayName="OnPointerDeselect")
	FUIEventTriggerBaseEvent OnPointerDeselectBP;
public:
	FLexUIMulticastDelegatePointerEventData& GetOnPointerEnterEvent(){return OnPointerEnterCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerExitEvent(){return OnPointerExitCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerDownEvent(){return OnPointerDownCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerUpEvent(){return OnPointerUpCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerClickEvent(){return OnPointerClickCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerBeginDragEvent(){return OnPointerBeginDragCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerDragEvent(){return OnPointerDragCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerEndDragEvent(){return OnPointerEndDragCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerDragDropEvent(){return OnPointerDragDropCPP;}
	FLexUIMulticastDelegatePointerEventData& GetOnPointerScrollEvent(){return OnPointerScrollCPP;}
	FLexUIMulticastDelegateBaseEventData& GetOnPointerSelectEvent(){return OnPointerSelectCPP;}
	FLexUIMulticastDelegateBaseEventData& GetOnPointerDeselectEvent(){return OnPointerDeselectCPP;}
	
	virtual bool OnPointerEnter_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerExit_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerBeginDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerEndDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDragDrop_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerScroll_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerSelect_Implementation(ULexBaseEventData* eventData)override;
	virtual bool OnPointerDeselect_Implementation(ULexBaseEventData* eventData)override;
};
