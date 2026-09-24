// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverInputModule.h"

UDreamDriverInputModule::UDreamDriverInputModule()
{
	// Nothing to configure: the base class already decides how an input module ticks, and this one
	// is driven a frame at a time by the rig rather than by the component tick either way.
}

void UDreamDriverInputModule::OnRegister()
{
	Super::OnRegister();
	SetOverrideMousePosition(true);
}

void UDreamDriverInputModule::ProcessInput()
{
	Super::ProcessInput();

	if (PendingScroll.IsSet())
	{
		const FVector2D AxisValue = PendingScroll.GetValue();
		// Cleared before dispatching, not after: InputScroll runs game code, and game code that asks
		// for another scroll would otherwise have its request thrown away by the reset below it.
		PendingScroll.Reset();
		InputScroll(AxisValue, 0);
	}
}

void UDreamDriverInputModule::MoveTo(const FVector2D& InPixel)
{
	// SetOverridePointerPosition is the seam, and it does the push itself: with the override on it
	// calls the module's own InputMouseMove, which is the same entry point a real mouse uses. It is
	// silent until the module has an event system, so the rig binds one before it drives anything.
	// The position it stores is the cursor; nothing here keeps a second copy that could drift from it.
	SetOverridePointerPosition(InPixel);
}

void UDreamDriverInputModule::MoveBy(const FVector2D& InPixelDelta)
{
	MoveTo(GetVirtualCursor() + InPixelDelta);
}

void UDreamDriverInputModule::Press(EDreamUIMouseButtonType InButton)
{
	PressedButtonMask |= ButtonBit(InButton);
	const FVector2D Cursor = GetVirtualCursor();
	InputTrigger(FVector(Cursor.X, Cursor.Y, 0.0), true, InButton);
}

void UDreamDriverInputModule::Release(EDreamUIMouseButtonType InButton)
{
	PressedButtonMask &= ~ButtonBit(InButton);
	const FVector2D Cursor = GetVirtualCursor();
	InputTrigger(FVector(Cursor.X, Cursor.Y, 0.0), false, InButton);
}

void UDreamDriverInputModule::Scroll(const FVector2D& InAxisValue)
{
	PendingScroll = InAxisValue;
}

void UDreamDriverInputModule::TouchPress(int32 InTouchID, const FVector2D& InPixel)
{
	InputTouchTrigger(true, InTouchID, FVector(InPixel.X, InPixel.Y, 0.0));
}

void UDreamDriverInputModule::TouchMoveTo(int32 InTouchID, const FVector2D& InPixel)
{
	InputTouchMoved(InTouchID, FVector(InPixel.X, InPixel.Y, 0.0));
}

void UDreamDriverInputModule::TouchRelease(int32 InTouchID, const FVector2D& InPixel)
{
	InputTouchTrigger(false, InTouchID, FVector(InPixel.X, InPixel.Y, 0.0));
}

void UDreamDriverInputModule::Navigate(EDreamUINavigationDirection InDirection, bool bInPressOrRelease, int32 InPointerID)
{
	InputNavigation(InDirection, bInPressOrRelease, InPointerID);
}

void UDreamDriverInputModule::NavigationTrigger(bool bInTriggerPress, int32 InPointerID)
{
	InputTriggerForNavigation(bInTriggerPress, InPointerID);
}

bool UDreamDriverInputModule::IsButtonPressed(EDreamUIMouseButtonType InButton) const
{
	return (PressedButtonMask & ButtonBit(InButton)) != 0;
}
