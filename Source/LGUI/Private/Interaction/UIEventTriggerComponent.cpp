// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIEventTriggerComponent.h"

bool UUIEventTriggerComponent::OnPointerEnter_Implementation(ULexPointerEventData* eventData)
{
	OnPointerEnterCPP.Broadcast(eventData);
	OnPointerEnterBP.Broadcast(eventData);
	OnPointerEnter.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerExit_Implementation(ULexPointerEventData* eventData)
{
	OnPointerExitCPP.Broadcast(eventData);
	OnPointerExitBP.Broadcast(eventData);
	OnPointerExit.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerDown_Implementation(ULexPointerEventData* eventData)
{
	OnPointerDownCPP.Broadcast(eventData);
	OnPointerDownBP.Broadcast(eventData);
	OnPointerDown.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerUp_Implementation(ULexPointerEventData* eventData)
{
	OnPointerUpCPP.Broadcast(eventData);
	OnPointerUpBP.Broadcast(eventData);
	OnPointerUp.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerClick_Implementation(ULexPointerEventData* eventData)
{
	OnPointerClickCPP.Broadcast(eventData);
	OnPointerClickBP.Broadcast(eventData);
	OnPointerClick.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerBeginDrag_Implementation(ULexPointerEventData* eventData)
{
	OnPointerBeginDragCPP.Broadcast(eventData);
	OnPointerBeginDragBP.Broadcast(eventData);
	OnPointerBeginDrag.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerDrag_Implementation(ULexPointerEventData* eventData)
{
	OnPointerDragCPP.Broadcast(eventData);
	OnPointerDragBP.Broadcast(eventData);
	OnPointerDrag.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerEndDrag_Implementation(ULexPointerEventData* eventData)
{
	OnPointerEndDragCPP.Broadcast(eventData);
	OnPointerEndDragBP.Broadcast(eventData);
	OnPointerEndDrag.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerDragDrop_Implementation(ULexPointerEventData* eventData)
{
	OnPointerDragDropCPP.Broadcast(eventData);
	OnPointerDragDropBP.Broadcast(eventData);
	OnPointerDragDrop.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerScroll_Implementation(ULexPointerEventData* eventData)
{
	OnPointerScrollCPP.Broadcast(eventData);
	OnPointerScrollBP.Broadcast(eventData);
	OnPointerScroll.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerSelect_Implementation(ULexBaseEventData* eventData)
{
	OnPointerSelectCPP.Broadcast(eventData);
	OnPointerSelectBP.Broadcast(eventData);
	OnPointerSelect.FireEvent(eventData);
	return AllowEventBubbleUp;
}
bool UUIEventTriggerComponent::OnPointerDeselect_Implementation(ULexBaseEventData* eventData)
{
	OnPointerDeselectCPP.Broadcast(eventData);
	OnPointerDeselectBP.Broadcast(eventData);
	OnPointerDeselect.FireEvent(eventData);
	return AllowEventBubbleUp;
}
