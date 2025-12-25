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
					FLexUIHitResult LexHitResult;
					bool bLineTraceHitSomething = LineTrace(eventData, LexHitResult);
					bool bResultHitSomething = false;
					FHitResult HitResult;
					ProcessPointerEvent(EventSystem.Get(), eventData, bLineTraceHitSomething, LexHitResult, bResultHitSomething, HitResult);

					auto TempHitComp = (USceneComponent*)HitResult.Component.Get();
					EventSystem->RaiseHitEvent(bResultHitSomething, HitResult, TempHitComp);
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

	auto EventData = EventSystem->GetPointerEventData(0, true);
	if (IsValid(EventData->EnterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || EventData->ScrollAxisValue != inAxisValue)
		{
			EventData->ScrollAxisValue = inAxisValue;
			EventSystem->CallOnPointerScroll(EventData->EnterComponent, EventData, EventData->EnterComponentEventFireType);
		}
	}
}

void ULexTouchInputModule::InputTouchTrigger(bool inTouchPress, int inTouchID, const FVector& inTouchPointPosition)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(EventData, ELexUIPointerInputType::Pointer);
	EventData->bNowIsTriggerPressed = inTouchPress;
	EventData->PointerPosition = inTouchPointPosition;
	if (inTouchPress)
	{
		EventData->PressPointerPosition = EventData->PointerPosition;
	}
}

void ULexTouchInputModule::InputTouchMoved(int inTouchID, const FVector& inTouchPointPosition)
{
	if (!EventSystem.IsValid())return;

	auto eventData = EventSystem->GetPointerEventData(inTouchID, true);
	EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer);
	eventData->PointerPosition = inTouchPointPosition;
}
