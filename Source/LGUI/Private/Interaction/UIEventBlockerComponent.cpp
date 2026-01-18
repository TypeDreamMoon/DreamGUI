// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIEventBlockerComponent.h"
#include "LGUI.h"

bool UUIEventBlockerComponent::OnPointerEnter_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerExit_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerDown_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerUp_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerClick_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerBeginDrag_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerDrag_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerEndDrag_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerDragDrop_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerScroll_Implementation(ULexPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerSelect_Implementation(ULexBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlockerComponent::OnPointerDeselect_Implementation(ULexBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}