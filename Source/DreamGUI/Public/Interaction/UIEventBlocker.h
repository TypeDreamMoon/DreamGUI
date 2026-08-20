// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "UIEventBlocker.generated.h"

//use this component to stop DreamPointerEvent bubble up
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIEventBlocker : public UDreamUIBehaviour
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
	UPROPERTY(EditAnywhere, Category = "UIEventBlocker") bool AllowEventBubbleUp = false;
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
