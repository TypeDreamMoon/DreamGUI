// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIEventTrigger.h"

bool UUIEventTrigger::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerEnterCPP.Broadcast(EventData);
	OnPointerEnterBP.Broadcast(EventData);
	OnPointerEnter.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerExitCPP.Broadcast(EventData);
	OnPointerExitBP.Broadcast(EventData);
	OnPointerExit.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerDownCPP.Broadcast(EventData);
	OnPointerDownBP.Broadcast(EventData);
	OnPointerDown.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerUpCPP.Broadcast(EventData);
	OnPointerUpBP.Broadcast(EventData);
	OnPointerUp.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerClickCPP.Broadcast(EventData);
	OnPointerClickBP.Broadcast(EventData);
	OnPointerClick.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerBeginDragCPP.Broadcast(EventData);
	OnPointerBeginDragBP.Broadcast(EventData);
	OnPointerBeginDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerDragCPP.Broadcast(EventData);
	OnPointerDragBP.Broadcast(EventData);
	OnPointerDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerEndDragCPP.Broadcast(EventData);
	OnPointerEndDragBP.Broadcast(EventData);
	OnPointerEndDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerDragDropCPP.Broadcast(EventData);
	OnPointerDragDropBP.Broadcast(EventData);
	OnPointerDragDrop.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	OnPointerScrollCPP.Broadcast(EventData);
	OnPointerScrollBP.Broadcast(EventData);
	OnPointerScroll.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerSelect_Implementation(UDreamBaseEventData* EventData)
{
	OnPointerSelectCPP.Broadcast(EventData);
	OnPointerSelectBP.Broadcast(EventData);
	OnPointerSelect.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	OnPointerDeselectCPP.Broadcast(EventData);
	OnPointerDeselectBP.Broadcast(EventData);
	OnPointerDeselect.FireEvent(EventData);
	return AllowEventBubbleUp;
}
