// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIButtonComponent.h"
#include "LGUI.h"

bool UUIButtonComponent::OnPointerClick_Implementation(ULexPointerEventData* eventData)
{
	OnClickCPP.Broadcast();
	OnClickBP.Broadcast();
	OnClick.FireEvent();
	return AllowEventBubbleUp;
}
