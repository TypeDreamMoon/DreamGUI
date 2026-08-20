// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIEventBlocker.h"
#include "DreamGUI.h"

bool UUIEventBlocker::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerSelect_Implementation(UDreamBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUIEventBlocker::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}