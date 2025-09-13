// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexTouchInputModule.h"
#include "LGUI.h"
#include "Event/LexEventSystem.h"
#include "Event/LexPointerEventData.h"

void ULexTouchInputModule::ProcessInput()
{
	if (!CheckEventSystem())return;

	for (auto& keyValue : EventSystem->PointerEventDataMap)
	{
		auto& eventData = keyValue.Value;
		switch (eventData->inputType)
		{
		default:
		case ELexUIPointerInputType::Pointer:
		{
			if (IsValid(eventData))
			{
				if (eventData->nowIsTriggerPressed || eventData->prevIsTriggerPressed)
				{
					FLexUIHitResult LGUIHitResult;
					bool lineTraceHitSomething = LineTrace(eventData, LGUIHitResult);
					bool resultHitSomething = false;
					FHitResult hitResult;
					ProcessPointerEvent(EventSystem, eventData, lineTraceHitSomething, LGUIHitResult, resultHitSomething, hitResult);

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
	if (!CheckEventSystem())return;

	auto eventData = EventSystem->GetPointerEventData(0, true);
	if (IsValid(eventData->enterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || eventData->scrollAxisValue != inAxisValue)
		{
			eventData->scrollAxisValue = inAxisValue;
			EventSystem->CallOnPointerScroll(eventData->enterComponent, eventData, eventData->enterComponentEventFireType);
		}
	}
}

void ULexTouchInputModule::InputTouchTrigger(bool inTouchPress, int inTouchID, const FVector& inTouchPointPosition)
{
	if (!CheckEventSystem())return;

	auto eventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer);
	eventData->nowIsTriggerPressed = inTouchPress;
	eventData->pointerPosition = inTouchPointPosition;
	if (inTouchPress)
	{
		eventData->pressPointerPosition = eventData->pointerPosition;
	}
}

void ULexTouchInputModule::InputTouchMoved(int inTouchID, const FVector& inTouchPointPosition)
{
	if (!CheckEventSystem())return;

	auto eventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer);
	eventData->pointerPosition = inTouchPointPosition;
}
