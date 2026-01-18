// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexStandaloneInputModule.h"
#include "LGUI.h"
#include "Event/LexEventSystem.h"
#include "Event/LexPointerEventData.h"
#include "Engine/GameViewportClient.h"

void ULexStandaloneInputModule::ProcessInput()
{
	if (!EventSystem.IsValid())return;

	for (auto& keyValue : EventSystem->GetPointerEventDataMap())
	{
		auto& EventData = keyValue.Value;
		switch (EventData->InputType)
		{
		default:
		case ELexUIPointerInputType::Pointer:
		{
			if (standaloneInputDataArray.Num() == 0)
			{
				if (bOverrideMousePosition)
				{
					EventData->PointerPosition = FVector(overrideMousePosition, 0);
				}
				else
				{
					FVector2D mousePos;
					if (GetMousePosition(mousePos))
					{
						EventData->PointerPosition = FVector(mousePos, 0);
					}
				}

				FLexUIHitResult LexHitResult;
				bool lineTraceHitSomething = LineTrace(EventData, LexHitResult);
				bool resultHitSomething = false;
				FHitResult hitResult;
				ProcessPointerEvent(EventSystem.Get(), EventData, lineTraceHitSomething, LexHitResult, resultHitSomething, hitResult);

				auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
				EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
			}
			else
			{
				for (auto& inputData : standaloneInputDataArray)//handle multiple click in one frame
				{
					EventData->PointerPosition = FVector(inputData.mousePosition, 0);
					EventData->bNowIsTriggerPressed = inputData.triggerPress;
					if (inputData.triggerPress)
					{
						EventData->PressTime = inputData.pressTime;
					}
					else
					{
						EventData->ReleaseTime = inputData.releaseTime;
					}
					EventData->MouseButtonType = inputData.mouseButtonType;

					FLexUIHitResult LGUIHitResult;
					bool lineTraceHitSomething = LineTrace(EventData, LGUIHitResult);
					bool resultHitSomething = false;
					FHitResult hitResult;
					ProcessPointerEvent(EventSystem.Get(), EventData, lineTraceHitSomething, LGUIHitResult, resultHitSomething, hitResult);

					auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
					EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
				}
				standaloneInputDataArray.Reset();
			}
		}
		break;
		case ELexUIPointerInputType::Navigation:
		{
			ProcessInputForNavigation(EventData);
		}
		break;
		}
	}
}
void ULexStandaloneInputModule::InputScroll(const FVector2D& inAxisValue)
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

void ULexStandaloneInputModule::InputTrigger(bool inTriggerPress, ELexUIMouseButtonType inMouseButtonType)
{
	if (!EventSystem.IsValid())return;

	auto EventData = EventSystem->GetPointerEventData(0, true);
	if (EventSystem->SetPointerInputType(EventData, ELexUIPointerInputType::Pointer))
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
		FVector2D mousePos = FVector2D(EventData->PointerPosition);
		GetMousePosition(mousePos);
		inputData.mousePosition = mousePos;
	}

	if (inTriggerPress)
	{
		inputData.pressTime = GetWorld()->TimeSeconds;
		EventData->PressPointerPosition = EventData->PointerPosition;
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
