// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIButton.h"
#include "LGUI.h"

bool UUIButton::OnPointerClick_Implementation(ULexPointerEventData* EventData)
{
	OnClickCPP.Broadcast();
	OnClickBP.Broadcast();
	OnClick.FireEvent();
	return AllowEventBubbleUp;
}
