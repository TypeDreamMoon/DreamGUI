// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamDriverInputModule.generated.h"

/**
 * A standalone input module whose pointer is a variable rather than a mouse.
 *
 * The engine's automation driver has to replace the platform application so Slate sees a cursor that
 * is not there. Nothing like that is needed here: UDreamStandaloneInputModule already takes its input
 * through public entry points -- InputMouseMove, InputTrigger, InputScroll, InputTouch* -- queues it,
 * and drains the queue in ProcessInput without ever asking the platform anything. The only place the
 * base class reaches for a viewport is GetMousePosition, and it already has a documented seam for
 * that: turn SetOverrideMousePosition on and the substituted position is what every caller reads.
 *
 * So this subclass holds the cursor, the button states and a scroll that has not been delivered yet,
 * and pushes them through the base class's own path. It reproduces none of the base class's logic.
 */
UCLASS()
class UDreamDriverInputModule : public UDreamStandaloneInputModule
{
	GENERATED_BODY()

public:
	UDreamDriverInputModule();

	/**
	 * Turns the base class's substituted-pointer seam on, once, as soon as the component exists.
	 *
	 * A driver's pointer is virtual for its whole life, so there is no moment at which reading the
	 * OS mouse would be the right answer -- and headless there is no mouse to read, which
	 * GetMousePosition reports as (0,0) rather than as a failure. Done here rather than in the
	 * constructor so the class default object never runs it.
	 */
	virtual void OnRegister() override;

	/**
	 * The base class's frame, plus the scroll that was asked for since the last one.
	 *
	 * Scroll is delivered AFTER Super, not before, because InputScroll dispatches immediately to
	 * whatever EventData->EnterWidget names -- and what the pointer is over is decided by the line
	 * trace Super just ran. Scrolling before it would send the wheel to wherever the pointer was
	 * hovering a frame ago, which for a cursor that was moved and scrolled in the same step is
	 * nothing at all.
	 */
	virtual void ProcessInput() override;

	/** Put the virtual cursor at a viewport pixel. Y grows downward from the top; see FDreamDriverProjection. */
	void MoveTo(const FVector2D& InPixel);
	/** Move the virtual cursor by a pixel delta. */
	void MoveBy(const FVector2D& InPixelDelta);

	/** Trigger down at the current cursor. Queued; it takes effect on the next ProcessInput. */
	void Press(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);
	/** Trigger up at the current cursor. Queued like the press. */
	void Release(EDreamUIMouseButtonType InButton = EDreamUIMouseButtonType::Left);

	/**
	 * Ask for a wheel turn. Held rather than dispatched, so it lands on the widget this frame's
	 * trace says the pointer is over. A second call before the next frame replaces the first: two
	 * wheel values in one frame is not something a wheel can produce.
	 */
	void Scroll(const FVector2D& InAxisValue);

	/** A finger landing, moving and lifting. Each touch id is its own pointer. */
	void TouchPress(int32 InTouchID, const FVector2D& InPixel);
	void TouchMoveTo(int32 InTouchID, const FVector2D& InPixel);
	void TouchRelease(int32 InTouchID, const FVector2D& InPixel);

	/** Gamepad or keyboard navigation, and its accept button. */
	void Navigate(EDreamUINavigationDirection InDirection, bool bInPressOrRelease, int32 InPointerID = 0);
	void NavigationTrigger(bool bInTriggerPress, int32 InPointerID = 0);

	/** Where the virtual cursor is, in viewport pixels. */
	FVector2D GetVirtualCursor() const { return VirtualCursor; }
	/** Whether this module believes InButton is currently held. Mirrors the presses it was given. */
	bool IsButtonPressed(EDreamUIMouseButtonType InButton) const;
	/** True between a Scroll and the frame that delivers it. */
	bool HasPendingScroll() const { return PendingScroll.IsSet(); }

private:
	/** One bit per EDreamUIMouseButtonType. A TSet of an enum class needs a hash this enum has not got. */
	static uint32 ButtonBit(EDreamUIMouseButtonType InButton) { return 1u << static_cast<uint32>(InButton); }

	FVector2D VirtualCursor = FVector2D::ZeroVector;
	uint32 PressedButtonMask = 0;
	TOptional<FVector2D> PendingScroll;
};
