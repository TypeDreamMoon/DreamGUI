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
	if (auto Viewport = this->GetWorld()->GetGameViewport())
	{
		Viewport->GetMousePosition(OutMousePos);
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
	auto EventData = EventSystem->GetPointerEventData(0, true);
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
