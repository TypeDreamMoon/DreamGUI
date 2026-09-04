// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Engine/GameViewportClient.h"
#include "Core/DreamUIWorldContext.h"

void UDreamStandaloneInputModule::ProcessInput()
{
	if (!EventSystem.IsValid())return;

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
void UDreamStandaloneInputModule::InputScroll(const FVector2D& InAxisValue)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(0, true);
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
	int InPointerID, EDreamUIMouseButtonType InMouseButtonType)
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

	if (InTriggerPress)
	{
		InputData.PressTime = GetWorld()->TimeSeconds;
	}
	else
	{
		InputData.ReleaseTime = GetWorld()->TimeSeconds;
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
	CommonInputTrigger(InTouchPointPosition, InTouchPress, InTouchID);
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
