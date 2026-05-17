// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIEventTrigger.h"

bool UUIEventTrigger::OnPointerEnter_Implementation(ULexPointerEventData* EventData)
{
	OnPointerEnterCPP.Broadcast(EventData);
	OnPointerEnterBP.Broadcast(EventData);
	OnPointerEnter.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerExit_Implementation(ULexPointerEventData* EventData)
{
	OnPointerExitCPP.Broadcast(EventData);
	OnPointerExitBP.Broadcast(EventData);
	OnPointerExit.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDown_Implementation(ULexPointerEventData* EventData)
{
	OnPointerDownCPP.Broadcast(EventData);
	OnPointerDownBP.Broadcast(EventData);
	OnPointerDown.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerUp_Implementation(ULexPointerEventData* EventData)
{
	OnPointerUpCPP.Broadcast(EventData);
	OnPointerUpBP.Broadcast(EventData);
	OnPointerUp.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerClick_Implementation(ULexPointerEventData* EventData)
{
	OnPointerClickCPP.Broadcast(EventData);
	OnPointerClickBP.Broadcast(EventData);
	OnPointerClick.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerBeginDrag_Implementation(ULexPointerEventData* EventData)
{
	OnPointerBeginDragCPP.Broadcast(EventData);
	OnPointerBeginDragBP.Broadcast(EventData);
	OnPointerBeginDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDrag_Implementation(ULexPointerEventData* EventData)
{
	OnPointerDragCPP.Broadcast(EventData);
	OnPointerDragBP.Broadcast(EventData);
	OnPointerDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerEndDrag_Implementation(ULexPointerEventData* EventData)
{
	OnPointerEndDragCPP.Broadcast(EventData);
	OnPointerEndDragBP.Broadcast(EventData);
	OnPointerEndDrag.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDragDrop_Implementation(ULexPointerEventData* EventData)
{
	OnPointerDragDropCPP.Broadcast(EventData);
	OnPointerDragDropBP.Broadcast(EventData);
	OnPointerDragDrop.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerScroll_Implementation(ULexPointerEventData* EventData)
{
	OnPointerScrollCPP.Broadcast(EventData);
	OnPointerScrollBP.Broadcast(EventData);
	OnPointerScroll.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerSelect_Implementation(ULexBaseEventData* EventData)
{
	OnPointerSelectCPP.Broadcast(EventData);
	OnPointerSelectBP.Broadcast(EventData);
	OnPointerSelect.FireEvent(EventData);
	return AllowEventBubbleUp;
}
bool UUIEventTrigger::OnPointerDeselect_Implementation(ULexBaseEventData* EventData)
{
	OnPointerDeselectCPP.Broadcast(EventData);
	OnPointerDeselectBP.Broadcast(EventData);
	OnPointerDeselect.FireEvent(EventData);
	return AllowEventBubbleUp;
}
