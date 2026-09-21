// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIButton.h"
#include "DreamGUI.h"

bool UUIButton::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerEnter_Implementation(EventData);
	OnHoveredCPP.Broadcast();
	OnHoveredBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerExit_Implementation(EventData);
	OnUnhoveredCPP.Broadcast();
	OnUnhoveredBP.Broadcast();
	return bBubble;
}

bool UUIButton::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	// The interactable test is repeated here rather than read off the Super's return value, because
	// that value is the BUBBLING policy and says nothing about whether the press was honoured. A
	// button drawn disabled that still broadcast OnPressed is the same bug as one that still clicks.
	const bool bBubble = Super::OnPointerDown_Implementation(EventData);
	if (IsInteractable())
	{
		OnPressedCPP.Broadcast();
		OnPressedBP.Broadcast();
		if (ShouldClickOnDown(EventData))
		{
			// MouseDown / Touch Down / ButtonPress: the press IS the click. Fired through the same
			// body the ordinary click takes, so a button cannot come to mean two different things
			// depending on which method it was set to.
			FireClick();
		}
	}
	return bBubble;
}

bool UUIButton::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerUp_Implementation(EventData);
	OnReleasedCPP.Broadcast();
	OnReleasedBP.Broadcast();
	if (IsInteractable() && ShouldClickOnUp(EventData))
	{
		// MouseUp / ButtonRelease: the release alone fires it, wherever the press landed.
		FireClick();
	}
	return bBubble;
}

bool UUIButton::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	if (!IsInteractable() || !ShouldClickOnClick(EventData))
	{
		// The click sound and the pad rumble are inside PlayClickFeedback, so this early return is
		// what stops a disabled button from sounding and feeling exactly like a working one -- and
		// the same return is what keeps a method that already fired on the down from firing twice.
		return AllowEventBubbleUp;
	}
	FireClick();
	return AllowEventBubbleUp;
}

void UUIButton::FireClick()
{
	// The one body every click method ends in. Pulled out of OnPointerClick when the methods arrived:
	// three entry points writing the same four lines is three places for the feedback, the native
	// listeners, the Blueprint listeners and the authored event delegate to fall out of step.
	PlayClickFeedback();
	OnClickCPP.Broadcast();
	OnClickBP.Broadcast();
	OnClick.FireEvent();
}

bool UUIButton::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	// The interactable test again, for OnPointerClick's reason: a button drawn disabled that still
	// announced a double click is the same bug as one that still clicks. No click feedback here --
	// the single click that came with it already played it, and playing it twice for one gesture is
	// what a second PlayClickFeedback would do.
	if (!IsInteractable())
	{
		return AllowEventBubbleUp;
	}
	OnDoubleClickCPP.Broadcast();
	OnDoubleClickBP.Broadcast();
	return AllowEventBubbleUp;
}
