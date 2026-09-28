// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIButton.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"

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
	if (!AcceptsPointerButton(EventData))
	{
		// A mouse button this control does not answer is not a press at all -- no pressed look, no
		// selection, no OnPressed -- which is how SButton meets every button but the left one. Passed
		// on rather than eaten, as SButton leaves it unhandled: whatever is behind still hears it.
		return true;
	}
	// The interactable test is repeated here rather than read off the Super's return value, because
	// that value is the BUBBLING policy and says nothing about whether the press was honoured. A
	// button drawn disabled that still broadcast OnPressed is the same bug as one that still clicks.
	const bool bBubble = Super::OnPointerDown_Implementation(EventData);
	bPressAccepted = IsInteractable();
	if (bPressAccepted)
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
	// Released only what was pressed, whichever button it is that came up: the pointer pipeline
	// carries one trigger per pointer, so this up ends whatever press there was. A press this button
	// took always gets its OnReleased (SButton's Release is gated on bIsPressed, not on the button),
	// and one it never took -- a button it does not answer, a press while disabled -- gets none, so the
	// press pair always comes as a pair.
	const bool bReleasesAPress = bPressAccepted;
	bPressAccepted = false;
	const bool bAnswered = AcceptsPointerButton(EventData);
	// Not gated: the selectable lets go of its pressed look whatever came up, which is what keeps a
	// face from staying pressed after an up it did not answer.
	const bool bBubble = Super::OnPointerUp_Implementation(EventData);
	if (bReleasesAPress)
	{
		OnReleasedCPP.Broadcast();
		OnReleasedBP.Broadcast();
	}
	if (bAnswered && IsInteractable() && ShouldClickOnUp(EventData))
	{
		// MouseUp / ButtonRelease: the release alone fires it, wherever the press landed.
		FireClick();
	}
	// An up this button does not answer goes on to whatever heard the press it passed on.
	return bAnswered ? bBubble : true;
}

bool UUIButton::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	if (!AcceptsPointerButton(EventData))
	{
		// Not a button this control answers: passed on, like the press it belongs to.
		return true;
	}
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
	// This is the second press of a double click, which the event system delivers IN PLACE of that
	// press's down, as Slate does. SButton's answer to it is taken whole: its own double-click handler
	// is asked first, and a double click that handler does not take is treated as a single click --
	// SButton::OnMouseButtonDoubleClick hands it to OnMouseButtonDown -- which is why a double click on
	// a button is still two full clicks. OnDoubleClick's listeners are that handler here; a multicast
	// delegate has no way to say it took the event, so they are told and the press goes on.
	if (!AcceptsPointerButton(EventData))
	{
		// Two clicks of a button this control does not answer are neither a double click of it nor a
		// press of it: passed on, like the down it stands in for.
		return true;
	}
	// The interactable test for OnPointerClick's reason: a button drawn disabled that still announced a
	// double click is the same bug as one that still clicks. No click feedback here -- the click this
	// press ends in plays it, once, as the first one did.
	if (IsInteractable())
	{
		OnDoubleClickCPP.Broadcast();
		OnDoubleClickBP.Broadcast();
		// The listeners are game code, and a row that opens a screen on a double click may have torn
		// this button down on the way. A press on a component that is going away would hand the event
		// system a dying widget to select, so there is no press for one.
		if (!IsValid(this) || !IsValid(GetWidget()))
		{
			return AllowEventBubbleUp;
		}
	}
	// The press, through the interface rather than straight to the C++ body, so it is exactly what a
	// down dispatched to this component would have run, a Blueprint override included. It carries the
	// disabled test, the button filter, OnPressed and the MouseDown click method; the release that ends
	// it gives OnReleased and the click.
	return IDreamPointerDownUpInterface::Execute_OnPointerDown(this, EventData);
}
