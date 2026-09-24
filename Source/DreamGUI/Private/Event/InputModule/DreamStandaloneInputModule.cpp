// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Engine/GameViewportClient.h"
#include "Core/DreamUIWorldContext.h"
#include "Misc/ScopeExit.h"

void UDreamStandaloneInputModule::ProcessInput()
{
	if (!EventSystem.IsValid())return;

	// Whatever else this frame does, the pinch recognizer sees the pointers as they end up. Run on the
	// way out rather than the way in, so it reads the positions this frame's input actually produced.
	ON_SCOPE_EXIT{ ProcessPinchGesture(); };

	if (StandaloneInputDataArray.Num() > 0)
	{
		// Take the frame's queue by value first. ProcessPointerEvent below dispatches into game code,
		// and game code that presses or releases anything reaches CommonInputTrigger, which both
		// Add()s to this array and -- on an input-type change -- Reset()s it. Either one reallocates
		// or empties the storage the range-for is walking. Anything queued from inside the loop is
		// simply handled on the next frame, which is what the trailing Reset() did to it anyway.
		TArray<StandaloneInputData> FrameInputDataArray = MoveTemp(StandaloneInputDataArray);
		StandaloneInputDataArray.Reset();
		for (auto& InputData : FrameInputDataArray)//handle multiple click in one frame
		{
			auto EventData = EventSystem->GetPointerEventData(InputData.PointerID, true);
			EventData->PointerPosition = InputData.PointerPosition;
			EventData->bNowIsTriggerPressed = InputData.bTriggerPress;
			if (InputData.bTriggerPress)
			{
				EventData->PressTime = InputData.PressTime;
				EventData->PressPointerPosition = InputData.PointerPosition;
			}
			else
			{
				EventData->ReleaseTime = InputData.ReleaseTime;
			}
			EventData->MouseButtonType = InputData.MouseButtonType;

			FDreamUIHitResultContainer DreamHitResult;
			bool bLineTraceHitSomething = LineTrace(EventData, DreamHitResult);
			bool bResultHitSomething = false;
			FDreamUIHitResult HitResult;
			ProcessPointerEvent(EventSystem.Get(), EventData, bLineTraceHitSomething, DreamHitResult, bResultHitSomething, HitResult);

			auto TempHitComp = HitResult.Widget.Get();
			EventSystem->RaiseHitEvent(bResultHitSomething, HitResult, TempHitComp);

			// A lifted finger is not a pointer any more. Nothing ever retired one: it stayed in the
			// event system's map holding whatever it last touched in hover, and the per-frame branch
			// below kept line-tracing from the position where it left the glass. Ten fingers used once
			// each is ten full raycasts a frame, for the rest of the session.
			if (InputData.bIsTouch && !InputData.bTriggerPress && EventSystem.IsValid())
			{
				ClearEventByID(InputData.PointerID);//fires the Exit the release itself does not
				EventSystem->RemovePointerEventData(InputData.PointerID);
			}
		}
	}
	else
	{
		// Everything dispatched from inside this loop runs game code, and game code reaches
		// UDreamEventSystem::GetPointerEventData -- UDreamWidget::SetFocus alone does it -- which adds
		// to the very map being iterated, for any pointer id it has never seen. One insertion that
		// grows the map rehashes it and leaves the iterator walking freed storage, so this walks a
		// snapshot. A pointer that appears mid-frame is simply picked up on the next one.
		TArray<UDreamPointerEventData*> FrameEventDataArray;
		FrameEventDataArray.Reserve(EventSystem->GetPointerEventDataMap().Num());
		for (const auto& keyValue : EventSystem->GetPointerEventDataMap())
		{
			FrameEventDataArray.Add(keyValue.Value.Get());
		}
		for (auto EventData : FrameEventDataArray)
		{
			// The same game code can tear a pointer down while we are still walking the snapshot.
			if (!IsValid(EventData))continue;
			switch (EventData->InputType)
			{
			default:
			case EDreamUIPointerInputType::Pointer:
				{
					FDreamUIHitResultContainer DreamHitResult;
					bool bLineTraceHitSomething = LineTrace(EventData, DreamHitResult);
					bool bResultHitSomething = false;
					FDreamUIHitResult HitResult;
					ProcessPointerEvent(EventSystem.Get(), EventData, bLineTraceHitSomething, DreamHitResult, bResultHitSomething, HitResult);

					auto TempHitComp = HitResult.Widget.Get();
					EventSystem->RaiseHitEvent(bResultHitSomething, HitResult, TempHitComp);
				}
				break;
			case EDreamUIPointerInputType::Navigation:
				{
					ProcessInputForNavigation(EventData);
				}
				break;
			}
		}
	}
}
void UDreamStandaloneInputModule::InputScroll(const FVector2D& InAxisValue, int InPointerID)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(InPointerID, true);
	if (!InAxisValue.IsZero())
	{
		// Turning a wheel is pointer input, and saying so matters: InputType is a sticky mode bit and the
		// per-frame branch of ProcessInput does not line-trace while it reads Navigation. A wheel turned
		// after a gamepad had claimed the pointer was therefore dispatched to whatever EnterWidget
		// navigation last left behind -- which may be nothing, or the wrong list entirely.
		EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	}
	if (IsValid(EventData->EnterWidget))
	{
		if (InAxisValue != FVector2D::ZeroVector || EventData->ScrollAxisValue != InAxisValue)
		{
			EventData->ScrollAxisValue = InAxisValue;
			EventSystem->CallOnPointerScroll(EventData->EnterWidget, EventData);
		}
	}
}

void UDreamStandaloneInputModule::InputTrigger(const FVector& InMousePosition, bool InTriggerPress, EDreamUIMouseButtonType InMouseButtonType)
{
	if (!EventSystem.IsValid())return;
	CommonInputTrigger(InMousePosition, InTriggerPress, 0, InMouseButtonType);
}
void UDreamStandaloneInputModule::GetMousePosition(FVector2D& OutMousePos)const
{
	// Written before any early-out, because the documented contract is "(0,0) if the position is not
	// valid" and an untouched out parameter is not that -- it is whatever the caller happened to have.
	OutMousePos = FVector2D::ZeroVector;
	if (bOverrideMousePosition)
	{
		OutMousePos = OverridePointerPosition;
		return;
	}
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return;
	if (auto Viewport = World->GetGameViewport())
	{
		Viewport->GetMousePosition(OutMousePos);
	}
}

void UDreamStandaloneInputModule::SetOverrideMousePosition(bool bInOverride)
{
	if (bOverrideMousePosition == bInOverride)
	{
		return;
	}
	bOverrideMousePosition = bInOverride;
	if (bInOverride)
	{
		// Start where the real mouse is, so the substituted pointer does not teleport.
		FVector2D Current = FVector2D::ZeroVector;
		const UWorld* World = DreamUI::GetWorldSafe(this);
		if (auto Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
		{
			Viewport->GetMousePosition(Current);
		}
		OverridePointerPosition = Current;
	}
}

void UDreamStandaloneInputModule::SetOverridePointerPosition(const FVector2D& InPosition)
{
	OverridePointerPosition = InPosition;
	if (bOverrideMousePosition && EventSystem.IsValid())
	{
		InputMouseMove(FVector(InPosition.X, InPosition.Y, 0.0f));
	}
}

void UDreamStandaloneInputModule::CommonInputTrigger(const FVector& InPointerPosition, bool InTriggerPress,
	int InPointerID, EDreamUIMouseButtonType InMouseButtonType, bool bInIsTouch)
{
	auto EventData = EventSystem->GetPointerEventData(InPointerID, true);
	if (EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer))
	{
		StandaloneInputDataArray.Reset();//input type change, clear cached input data
	}

	StandaloneInputData InputData;
	InputData.PointerID = InPointerID;
	InputData.MouseButtonType = InMouseButtonType;
	InputData.bTriggerPress = InTriggerPress;
	InputData.PointerPosition = InPointerPosition;
	InputData.bIsTouch = bInIsTouch;

	// Stamped on the pointer clock, the one long press, hold-to-drag, swipe duration and the
	// double-click window measure against (UDreamEventSystem::GetPointerClockSeconds). It was the game
	// clock, which a pause stops: a press made in a paused game's menu stayed exactly as old as the
	// pause for as long as the pause lasted.
	const double ClockSeconds = UDreamEventSystem::GetPointerClockSeconds(this);
	if (InTriggerPress)
	{
		InputData.PressTime = ClockSeconds;
	}
	else
	{
		InputData.ReleaseTime = ClockSeconds;
	}
	StandaloneInputDataArray.Add(InputData);
}

void UDreamStandaloneInputModule::InputMouseMove(const FVector& InMousePosition)
{
	// This runs from a bound axis delegate that keeps firing while the world tears down, so it needs
	// the same guard the trigger entry points have. (It was not the only one missing it -- the two
	// navigation entry points below were bare too, and are bound to keys that outlive a world just
	// as happily.)
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(0, true);
	// MOVING the mouse IS pointer input, and saying so is not decoration: InputType is a sticky
	// mode bit, ProcessInput's per-frame branch skips the line trace entirely while it reads
	// Navigation, and only a press ever reset it. So one arrow key, Enter or stick nudge --
	// InputNavigation / InputTriggerForNavigation flip pointer 0 -- and hover died for the rest of
	// the session: the position kept updating, nothing re-traced with it, EnterWidget froze on
	// whatever navigation last highlighted. Clicks went on working (the queued branch ignores the
	// mode and resets it), which is exactly the "click fine, hover dead" shape this was reported as.
	//
	// But it has to be an ACTUAL move. Both preset actors call this every single frame regardless:
	// the legacy one from a bound Mouse2D vector axis, which fires whether or not the mouse moved,
	// and the Enhanced Input one from Tick, because Enhanced Input has no absolute-position axis to
	// bind. Claiming Pointer unconditionally therefore re-pinned the mode every frame and nothing
	// could hold Navigation for longer than one: arrow keys never moved focus, and Enter or the
	// gamepad face button arrived at the Pointer branch and read as a mouse press at the cursor.
	// The position is still carried across either way -- it is the mode claim that is the statement.
	if (!InMousePosition.Equals(EventData->PointerPosition))
	{
		EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	}
	EventData->PointerPosition = InMousePosition;
}

void UDreamStandaloneInputModule::InputTouchTrigger(bool InTouchPress, int InTouchID, const FVector& InTouchPointPosition)
{
	if (!EventSystem.IsValid())return;
	CommonInputTrigger(InTouchPointPosition, InTouchPress, InTouchID, EDreamUIMouseButtonType::Left, true);
}

void UDreamStandaloneInputModule::InputTouchMoved(int InTouchID, const FVector& InTouchPointPosition)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(InTouchID, true);
	// Same reason as InputMouseMove: a moving touch is pointer input and has to say so, or a
	// pointer left in Navigation mode never line-traces again.
	EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
	EventData->PointerPosition = InTouchPointPosition;
}

void UDreamStandaloneInputModule::InputNavigation(EDreamUINavigationDirection InDirection, bool InPressOrRelease, int InPointerID)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(InPointerID, true);
	if (InPressOrRelease)
	{
		EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
		EventData->NavigateDirection = InDirection;
	}
	else
	{
		EventData->NavigateDirection = EDreamUINavigationDirection::None;
	}
	EventData->NavigateTickTime = 0;
}
void UDreamStandaloneInputModule::InputTriggerForNavigation(bool InTriggerPress, int InPointerID)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(InPointerID, true);
	if (InTriggerPress)
	{
		EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
	}
	EventData->NavigateTickTime = 0;
	EventData->bNowIsTriggerPressed = InTriggerPress;
}
