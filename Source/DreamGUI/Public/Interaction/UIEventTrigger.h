// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Core/DreamUIBehaviour.h"
#include "UIEventTrigger.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIEventTriggerPointerEvent, UDreamPointerEventData*, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIEventTriggerBaseEvent, UDreamBaseEventData*, Value);

//a helper component for quick register and setup DreamPointerEvent
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIEventTrigger : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerClickInterface
	, public IDreamPointerDragInterface
	, public IDreamPointerDragDropInterface
	, public IDreamPointerScrollInterface
	, public IDreamPointerSelectDeselectInterface
{
	GENERATED_BODY()
protected:
	//inherited events of this component can bubble up?
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		bool AllowEventBubbleUp = false;
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerEnter = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerExit = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerDown = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerUp = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerClick = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerBeginDrag = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerDrag = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerEndDrag = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerDragDrop = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerScroll = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerSelect = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);
	UPROPERTY(EditAnywhere, Category = "UIEventTrigger") 
		FDreamUIEventDelegate OnPointerDeselect = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::PointerEvent);

	FDreamUIMulticastDelegatePointerEventData OnPointerEnterCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerExitCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerDownCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerUpCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerClickCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerBeginDragCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerDragCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerEndDragCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerDragDropCPP;
	FDreamUIMulticastDelegatePointerEventData OnPointerScrollCPP;
	FDreamUIMulticastDelegateBaseEventData OnPointerSelectCPP;
	FDreamUIMulticastDelegateBaseEventData OnPointerDeselectCPP;

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
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerEnterEvent(){return OnPointerEnterCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerExitEvent(){return OnPointerExitCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerDownEvent(){return OnPointerDownCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerUpEvent(){return OnPointerUpCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerClickEvent(){return OnPointerClickCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerBeginDragEvent(){return OnPointerBeginDragCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerDragEvent(){return OnPointerDragCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerEndDragEvent(){return OnPointerEndDragCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerDragDropEvent(){return OnPointerDragDropCPP;}
	FDreamUIMulticastDelegatePointerEventData& GetOnPointerScrollEvent(){return OnPointerScrollCPP;}
	FDreamUIMulticastDelegateBaseEventData& GetOnPointerSelectEvent(){return OnPointerSelectCPP;}
	FDreamUIMulticastDelegateBaseEventData& GetOnPointerDeselectEvent(){return OnPointerDeselectCPP;}
	
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerSelect_Implementation(UDreamBaseEventData* EventData)override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)override;
};
