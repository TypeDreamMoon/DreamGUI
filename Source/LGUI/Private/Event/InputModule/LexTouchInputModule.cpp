// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexTouchInputModule.h"
#include "Event/LexEventSystem.h"
#include "Event/LexPointerEventData.h"

void ULexTouchInputModule::ProcessInput()
{
	if (!EventSystem.IsValid())return;

	for (auto& keyValue : EventSystem->GetPointerEventDataMap())
	{
		auto& eventData = keyValue.Value;
		switch (eventData->InputType)
		{
		default:
		case ELexUIPointerInputType::Pointer:
		{
			if (IsValid(eventData))
			{
				if (eventData->bNowIsTriggerPressed || eventData->bPrevIsTriggerPressed)
				{
					FLexUIHitResult LGUIHitResult;
					bool lineTraceHitSomething = LineTrace(eventData, LGUIHitResult);
					bool resultHitSomething = false;
					FHitResult hitResult;
					ProcessPointerEvent(EventSystem.Get(), eventData, lineTraceHitSomething, LGUIHitResult, resultHitSomething, hitResult);

					auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
					EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
				}
			}
		}
		break;
		case ELexUIPointerInputType::Navigation:
		{
			ProcessInputForNavigation(eventData);
		}
		break;
		}
	}
}
void ULexTouchInputModule::InputScroll(const FVector2D& inAxisValue)
{
	if (!EventSystem.IsValid())return;

	auto eventData = EventSystem->GetPointerEventData(0, true);
	if (IsValid(eventData->EnterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || eventData->ScrollAxisValue != inAxisValue)
		{
			eventData->ScrollAxisValue = inAxisValue;
			EventSystem->CallOnPointerScroll(eventData->EnterComponent, eventData, eventData->EnterComponentEventFireType);
		}
	}
}

void ULexTouchInputModule::InputTouchTrigger(bool inTouchPress, int inTouchID, const FVector& inTouchPointPosition)
{
	if (!EventSystem.IsValid())return;

	auto eventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer);
	eventData->bNowIsTriggerPressed = inTouchPress;
	eventData->PointerPosition = inTouchPointPosition;
	if (inTouchPress)
	{
		eventData->PressPointerPosition = eventData->PointerPosition;
	}
}

void ULexTouchInputModule::InputTouchMoved(int inTouchID, const FVector& inTouchPointPosition)
{
	if (!EventSystem.IsValid())return;

	auto eventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer);
	eventData->PointerPosition = inTouchPointPosition;
}
