// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIButton.h"
#include "DreamGUI.h"

bool UUIButton::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerEnter_Implementation(EventData);
	OnHoveredBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerExit_Implementation(EventData);
	OnUnhoveredBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerDown_Implementation(EventData);
	OnPressedBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerUp_Implementation(EventData);
	OnReleasedBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	OnClickCPP.Broadcast();
	OnClickBP.Broadcast();
	OnClick.FireEvent();
	return AllowEventBubbleUp;
}
