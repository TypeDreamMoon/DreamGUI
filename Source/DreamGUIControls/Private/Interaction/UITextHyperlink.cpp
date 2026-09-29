// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UITextHyperlink.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"

UDreamText* UUITextHyperlink::GetTextVisual()const
{
	if (IsValid(TextVisual))
	{
		return TextVisual;
	}
	// The usual arrangement is one text on the widget this behaviour is on, so nobody has to wire it.
	if (UDreamWidget* Widget = GetWidget())
	{
		return Cast<UDreamText>(Widget->GetVisual());
	}
	return nullptr;
}

void UUITextHyperlink::SetTextVisual(UDreamText* Value)
{
	TextVisual = Value;
}

bool UUITextHyperlink::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	UDreamText* Text = GetTextVisual();
	if (Text == nullptr || EventData == nullptr)
	{
		return true;
	}
	// The event carries where the pointer met the widget's plane, which is the point the glyph quads
	// are in once the widget's transform is undone.
	if (Text->TryClickHyperlinkAtWorldPosition(EventData->GetWorldPointInPlane()))
	{
		return false;//the link took it
	}
	return bAllowEventBubbleUpWhenNoLinkHit;
}
