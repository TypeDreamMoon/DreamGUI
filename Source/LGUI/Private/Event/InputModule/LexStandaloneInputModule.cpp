// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexStandaloneInputModule.h"
#include "LGUI.h"
#include "Event/LexEventSystem.h"
#include "Event/LexPointerEventData.h"
#include "Engine/GameViewportClient.h"

void ULexStandaloneInputModule::ProcessInput()
{
	if (!CheckEventSystem())return;

	for (auto& keyValue : EventSystem->PointerEventDataMap)
	{
		auto& eventData = keyValue.Value;
		switch (eventData->InputType)
		{
		default:
		case ELexUIPointerInputType::Pointer:
		{
			if (standaloneInputDataArray.Num() == 0)
			{
				if (bOverrideMousePosition)
				{
					eventData->PointerPosition = FVector(overrideMousePosition, 0);
				}
				else
				{
					FVector2D mousePos;
					if (GetMousePosition(mousePos))
					{
						eventData->PointerPosition = FVector(mousePos, 0);
					}
				}

				FLexUIHitResult LexHitResult;
				bool lineTraceHitSomething = LineTrace(eventData, LexHitResult);
				bool resultHitSomething = false;
				FHitResult hitResult;
				ProcessPointerEvent(EventSystem, eventData, lineTraceHitSomething, LexHitResult, resultHitSomething, hitResult);

				auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
				EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
			}
			else
			{
				for (auto& inputData : standaloneInputDataArray)//handle multiple click in one frame
				{
					eventData->PointerPosition = FVector(inputData.mousePosition, 0);
					eventData->bNowIsTriggerPressed = inputData.triggerPress;
					if (inputData.triggerPress)
					{
						eventData->PressTime = inputData.pressTime;
					}
					else
					{
						eventData->ReleaseTime = inputData.releaseTime;
					}
					eventData->MouseButtonType = inputData.mouseButtonType;

					FLexUIHitResult LGUIHitResult;
					bool lineTraceHitSomething = LineTrace(eventData, LGUIHitResult);
					bool resultHitSomething = false;
					FHitResult hitResult;
					ProcessPointerEvent(EventSystem, eventData, lineTraceHitSomething, LGUIHitResult, resultHitSomething, hitResult);

					auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
					EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
				}
				standaloneInputDataArray.Reset();
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
void ULexStandaloneInputModule::InputScroll(const FVector2D& inAxisValue)
{
	if (!CheckEventSystem())return;

	auto eventData = EventSystem->GetPointerEventData(0, true);
	if (IsValid(eventData->EnterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || eventData->ScrollAxisValue != inAxisValue)
		{
			eventData->ScrollAxisValue = inAxisValue;
			if (CheckEventSystem())
			{
				EventSystem->CallOnPointerScroll(eventData->EnterComponent, eventData, eventData->EnterComponentEventFireType);
			}
		}
	}
}

void ULexStandaloneInputModule::InputTrigger(bool inTriggerPress, ELexUIMouseButtonType inMouseButtonType)
{
	if (!CheckEventSystem())return;

	auto eventData = EventSystem->GetPointerEventData(0, true);
	if (EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Pointer))
	{
		standaloneInputDataArray.Reset();//input type change, clear cached input data
	}

	StandaloneInputData inputData;
	inputData.mouseButtonType = inMouseButtonType;
	inputData.triggerPress = inTriggerPress;

	if (bOverrideMousePosition)
	{
		inputData.mousePosition = overrideMousePosition;
	}
	else
	{
		FVector2D mousePos = FVector2D(eventData->PointerPosition);
		GetMousePosition(mousePos);
		inputData.mousePosition = mousePos;
	}

	if (inTriggerPress)
	{
		inputData.pressTime = GetWorld()->TimeSeconds;
		eventData->PressPointerPosition = eventData->PointerPosition;
	}
	else
	{
		inputData.releaseTime = GetWorld()->TimeSeconds;
	}
	standaloneInputDataArray.Add(inputData);
}
bool ULexStandaloneInputModule::GetMousePosition(FVector2D& OutMousePos)
{
	if (auto viewport = this->GetWorld()->GetGameViewport())
	{
		return viewport->GetMousePosition(OutMousePos);
	}
	return false;
}

void ULexStandaloneInputModule::InputOverrideMousePosition(const FVector2D& inMousePosition)
{
	if (!bOverrideMousePosition)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d Check parameter bOverrideMousePosition if you need to use custom mouse position. Or custom mouse position will not work!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	overrideMousePosition = inMousePosition;
}
