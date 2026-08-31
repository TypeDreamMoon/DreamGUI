// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Engine/GameViewportClient.h"

void UDreamStandaloneInputModule::ProcessInput()
{
	if (!EventSystem.IsValid())return;

	if (StandaloneInputDataArray.Num() > 0)
	{
		for (auto& InputData : StandaloneInputDataArray)//handle multiple click in one frame
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
		StandaloneInputDataArray.Reset();
	}
	else
	{
		for (auto& keyValue : EventSystem->GetPointerEventDataMap())
		{
			auto& EventData = keyValue.Value;
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
	if (auto Viewport = this->GetWorld()->GetGameViewport())
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
		if (auto Viewport = this->GetWorld()->GetGameViewport())
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
	// The guard every other entry point here has, and the only one that was missing: this runs from
	// a bound axis delegate that keeps firing while the world tears down.
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(0, true);
	// Moving the mouse IS pointer input, and saying so is not decoration: InputType is a sticky
	// mode bit, ProcessInput's per-frame branch skips the line trace entirely while it reads
	// Navigation, and only a press ever reset it. So one arrow key, Enter or stick nudge --
	// InputNavigation / InputTriggerForNavigation flip pointer 0 -- and hover died for the rest of
	// the session: the position kept updating, nothing re-traced with it, EnterWidget froze on
	// whatever navigation last highlighted. Clicks went on working (the queued branch ignores the
	// mode and resets it), which is exactly the "click fine, hover dead" shape this was reported as.
	EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
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
	auto EventData = EventSystem->GetPointerEventData(InPointerID, true);
	if (InTriggerPress)
	{
		EventSystem->SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
	}
	EventData->NavigateTickTime = 0;
	EventData->bNowIsTriggerPressed = InTriggerPress;
}
