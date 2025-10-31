// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexPointerInputModule.h"
#include "Event/LexPointerEventData.h"
#include "Core/LexUIManager.h"
#include "Event/LexEventSystem.h"
#include "Event/LexBaseRaycaster.h"
#include "Event/Interface/LexNavigationInterface.h"
#include "Interaction/UISelectableComponent.h"

bool ULexPointerInputModule::LineTrace(ULexPointerEventData* InPointerEventData, FLexUIHitResult& OutLexHitResult)
{
	MultiHitResult.Reset();
	auto World = this->GetWorld();
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World))
	{
		auto bIsGamePaused = World->IsPaused();
		auto& AllRaycasterArray = LexUIManager->GetAllRaycasterArray();
		InPointerEventData->HoverComponentArray.Reset();

		FVector RayOrigin(0, 0, 0), RayDir(1, 0, 0), RayEnd(1, 0, 0);
		for (int i = 0; i < AllRaycasterArray.Num(); i++)
		{
			auto& RaycasterItem = AllRaycasterArray[i];
			if (!RaycasterItem.IsValid())continue;
			if (RaycasterItem->GetUserIndex() != EventSystem->GetUserIndex())continue;
			if (RaycasterItem->GetPointerID() != INDEX_NONE && RaycasterItem->GetPointerID() != InPointerEventData->PointerID)continue;
			if (bIsGamePaused && RaycasterItem->GetAffectByGamePause())continue;
			
			TArray<FHitResult> HitResultArray;
			RaycasterItem->Raycast(InPointerEventData, RayOrigin, RayDir, RayEnd, HitResultArray);
			if (HitResultArray.Num() > 0)
			{
				FLexUIHitResult LexHitResult;
				LexHitResult.HitResult = HitResultArray[0];
				LexHitResult.EventFireType = RaycasterItem->GetEventFireType();
				LexHitResult.Raycaster = RaycasterItem.Get();
				LexHitResult.RayOrigin = RayOrigin;
				LexHitResult.RayDirection = RayDir;
				LexHitResult.RayEnd = RayEnd;
				for (auto& HitItem : HitResultArray)
				{
					LexHitResult.HoverArray.Add(HitItem.Component.Get());
				}
				MultiHitResult.Add(LexHitResult);
			}
		}
		if (MultiHitResult.Num() == 0)
		{
			return false;
		}
		else if (MultiHitResult.Num() > 1)
		{
			//sort only on distance (not depth), because multiHitResult only store hit result of same depth
			MultiHitResult.Sort([](const FLexUIHitResult& A, const FLexUIHitResult& B)
				{
					return A.HitResult.Distance < B.HitResult.Distance;
				});
			for (auto& hitResultItem : MultiHitResult)
			{
				for (auto& hoverItem : hitResultItem.HoverArray)
				{
					InPointerEventData->HoverComponentArray.Add(hoverItem);
				}
			}
		}
		else
		{
			for (auto hoverItem : MultiHitResult[0].HoverArray)
			{
				InPointerEventData->HoverComponentArray.Add(hoverItem);
			}
		}
		OutLexHitResult = MultiHitResult[0];
		return true;
	}
	return false;
}

//@todo: these logs is just for editor testing, remove them when ready
#define LOG_ENTER_EXIT 0
void ULexPointerInputModule::ProcessPointerEnterExit(ULexEventSystem* eventSystem, ULexPointerEventData* eventData, USceneComponent* oldObj, USceneComponent* newObj, ELexUIEventFireType enterFireType)
{
	if (oldObj == newObj)return;
	if (IsValid(oldObj) && IsValid(newObj))
	{
		auto commonRoot = FindCommonRoot(oldObj->GetOwner(), newObj->GetOwner());
#if LOG_ENTER_EXIT
		UE_LOG(LGUI, Error, TEXT("-----begin exit 000, commonRoot:%s"), commonRoot != nullptr ? *(commonRoot->GetActorLabel()) : TEXT("null"));
#endif
		//exit old
		for (int i = eventData->EnterComponentStack.Num() - 1; i >= 0; i--)
		{
			if (commonRoot == eventData->EnterComponentStack[i]->GetOwner())
			{
				break;
			}
			if (!eventData->bIsExitFiredAtCurrentFrame)
			{
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerExit(eventData->EnterComponentStack[i], eventData, eventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerExit(eventData->EnterComponentStack[i], eventData, eventData->EnterComponentEventFireType);
				}
			}
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("	%s"), *(eventData->enterComponentStack[i]->GetOwner()->GetActorLabel()));
#endif
			eventData->EnterComponentStack.RemoveAt(i);
		}
		eventData->EnterComponent = nullptr;
		eventData->bIsExitFiredAtCurrentFrame = true;
#if LOG_ENTER_EXIT
		UE_LOG(LGUI, Error, TEXT("*****end exit, stack count:%d\n"), eventData->enterComponentStack.Num());
#endif
		//enter new
		eventData->EnterComponent = newObj;
		eventData->EnterComponentEventFireType = enterFireType;
		AActor* enterObjectActor = newObj->GetOwner();
		if (commonRoot != enterObjectActor)
		{
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("-----begin enter 111"));
#endif
			int insertIndex = eventData->EnterComponentStack.Num();
			if (eventSystem == nullptr)
			{
				ULexEventSystem::ExecuteEvent_OnPointerEnter(newObj, eventData, eventData->EnterComponentEventFireType, false);
			}
			else
			{
				eventSystem->CallOnPointerEnter(newObj, eventData, eventData->EnterComponentEventFireType);
			}
			eventData->HighlightComponentForNavigation = newObj;
			eventData->EnterComponentStack.Add(newObj);
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
			enterObjectActor = enterObjectActor->GetAttachParentActor();
			while (enterObjectActor != nullptr)
			{
				if (commonRoot == enterObjectActor)
				{
					break;
				}
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor->GetRootComponent(), eventData, eventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(enterObjectActor->GetRootComponent(), eventData, eventData->EnterComponentEventFireType);
				}
				eventData->EnterComponentStack.Insert(enterObjectActor->GetRootComponent(), insertIndex);
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
				enterObjectActor = enterObjectActor->GetAttachParentActor();
			}
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("*****end enter, stack count:%d\n"), eventData->enterComponentStack.Num());
#endif
		}
	}
	else
	{
		if (IsValid(oldObj) || eventData->EnterComponentStack.Num() > 0)
		{
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("-----begin exit 222"));
#endif
			//exit old
			for (int i = eventData->EnterComponentStack.Num() - 1; i >= 0; i--)
			{
				if (IsValid(eventData->EnterComponentStack[i]))
				{
					if (!eventData->bIsExitFiredAtCurrentFrame)
					{
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerExit(eventData->EnterComponentStack[i], eventData, eventData->EnterComponentEventFireType, false);
						}
						else
						{
							eventSystem->CallOnPointerExit(eventData->EnterComponentStack[i], eventData, eventData->EnterComponentEventFireType);
						}
					}
#if LOG_ENTER_EXIT
					UE_LOG(LGUI, Error, TEXT("	%s, fireType:%d"), *(eventData->enterComponentStack[i]->GetOwner()->GetActorLabel()), (int)(eventData->enterComponentEventFireType));
#endif
				}
				eventData->EnterComponentStack.RemoveAt(i);
			}
			eventData->EnterComponent = nullptr;
			eventData->bIsExitFiredAtCurrentFrame = true;
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("*****end exit, stack count:%d\n"), eventData->enterComponentStack.Num());
#endif
			eventData->EnterComponentStack.Reset();
		}
		if (IsValid(newObj))
		{
			//enter new
			if (!eventData->EnterComponentStack.Contains(newObj))
			{
				AActor* enterObjectActor = newObj->GetOwner();
				int insertIndex = eventData->EnterComponentStack.Num();
				eventData->EnterComponent = newObj;
				eventData->EnterComponentEventFireType = enterFireType;
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("-----begin enter 333"));
				UE_LOG(LGUI, Error, TEXT("	%s"), *(enterObjectActor->GetActorLabel()));
#endif
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerEnter(newObj, eventData, eventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(newObj, eventData, eventData->EnterComponentEventFireType);
				}
				eventData->HighlightComponentForNavigation = newObj;
				eventData->EnterComponentStack.Add(newObj);
				enterObjectActor = enterObjectActor->GetAttachParentActor();
				while (enterObjectActor != nullptr)
				{
#if LOG_ENTER_EXIT
					UE_LOG(LGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor->GetRootComponent(), eventData, eventData->EnterComponentEventFireType, false);
					}
					else
					{
						eventSystem->CallOnPointerEnter(enterObjectActor->GetRootComponent(), eventData, eventData->EnterComponentEventFireType);
					}
					eventData->EnterComponentStack.Insert(enterObjectActor->GetRootComponent(), insertIndex);
					enterObjectActor = enterObjectActor->GetAttachParentActor();
				}
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("*****end enter, stack count:%d\n"), eventData->enterComponentStack.Num());
#endif
			}
		}
	}
}
AActor* ULexPointerInputModule::FindCommonRoot(AActor* actorA, AActor* actorB)
{
	if (actorA == nullptr || actorB == nullptr)return nullptr;

	while (actorA != nullptr)
	{
		AActor* tempActorB = actorB;
		while (tempActorB != nullptr)
		{
			if (actorA == tempActorB)
				return actorA;
			tempActorB = tempActorB->GetAttachParentActor();
		}
		actorA = actorA->GetAttachParentActor();
	}
	return nullptr;
}
void ULexPointerInputModule::ProcessPointerEvent(ULexEventSystem* eventSystem, ULexPointerEventData* eventData, bool bLineTraceHitSomething, const FLexUIHitResult& LexHitResult, bool& OutIsHitSomething, FHitResult& OutHitResult)
{
	eventData->bIsUpFiredAtCurrentFrame = false;
	eventData->bIsExitFiredAtCurrentFrame = false;
	eventData->bIsEndDragFiredAtCurrentFrame = false;

	eventData->FaceIndex = LexHitResult.HitResult.FaceIndex;
	eventData->Raycaster = LexHitResult.Raycaster;
	OutHitResult = LexHitResult.HitResult;
	OutIsHitSomething = bLineTraceHitSomething;

	if (bLineTraceHitSomething)
	{
		auto nowHitComponent = (USceneComponent*)OutHitResult.Component.Get();
		//fire event
		eventData->WorldPoint = OutHitResult.Location;
		eventData->WorldNormal = OutHitResult.Normal;
		if (eventData->EnterComponent != nowHitComponent)//hit different object
		{
			ProcessPointerEnterExit(eventSystem, eventData, eventData->EnterComponent, nowHitComponent, LexHitResult.EventFireType);
		}
	}
	else
	{
		if (IsValid(eventData->EnterComponent) || eventData->EnterComponentStack.Num() > 0)//prev object
		{
			ProcessPointerEnterExit(eventSystem, eventData, eventData->EnterComponent, nullptr, LexHitResult.EventFireType);
		}
	}

	if (eventData->bNowIsTriggerPressed && eventData->bPrevIsTriggerPressed)//if trigger keep pressing
	{
		if (eventData->bIsDragging)//if dragging
		{
			//trigger drag event
			if (IsValid(eventData->DragComponent))
			{
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType, true);
				}
				else
				{
					eventSystem->CallOnPointerDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType);
				}

				OutHitResult.Distance = eventData->PressDistance;
				OutIsHitSomething = true;//always hit a plane when drag
			}
			else
			{
				eventData->bIsDragging = false;
			}
		}
		else//trigger press but not dragging, only concern if trigger drag event
		{
			if (IsValid(eventData->PressComponent))//if hit something when press
			{
				if (IsValid(eventData->PressRaycaster))
				{
					if (eventData->PressRaycaster->ShouldStartDrag(eventData))
					{
						eventData->bIsDragging = true;
						eventData->DragComponent = eventData->PressComponent;
						eventData->DragComponentEventFireType = eventData->PressComponentEventFireType;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerBeginDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerBeginDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType);
						}
					}
				}
				OutHitResult.Distance = eventData->PressDistance;
				OutIsHitSomething = true;
			}
		}
	}
	else if (!eventData->bNowIsTriggerPressed && !eventData->bPrevIsTriggerPressed)//is trigger keep release, only concern Enter/Exit event
	{
		
	}
	else//trigger state change
	{
		if (eventData->bNowIsTriggerPressed)//now is press, prev is release
		{
			if (bLineTraceHitSomething)
			{
				if (IsValid(eventData->EnterComponent))//now object
				{
					eventData->WorldPoint = OutHitResult.Location;
					eventData->WorldNormal = OutHitResult.Normal;
					eventData->PressDistance = OutHitResult.Distance;
					eventData->PressRayOrigin = LexHitResult.RayOrigin;
					eventData->PressRayDirection = LexHitResult.RayDirection;
					eventData->PressWorldPoint = OutHitResult.Location;
					eventData->PressWorldNormal = OutHitResult.Normal;
					eventData->PressRaycaster = LexHitResult.Raycaster;
					eventData->PressWorldToLocalTransform = eventData->EnterComponent->GetComponentTransform().Inverse();
					eventData->PressComponent = eventData->EnterComponent;
					eventData->PressComponentEventFireType = eventData->EnterComponentEventFireType;
					DeselectIfSelectionChanged(eventSystem, eventData->PressComponent, eventData);
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerDown(eventData->PressComponent, eventData, eventData->PressComponentEventFireType, true);
					}
					else
					{
						eventSystem->CallOnPointerDown(eventData->PressComponent, eventData, eventData->PressComponentEventFireType);
					}
				}
			}
		}
		else//now is release, prev is press
		{
			if (eventData->bIsDragging)//is dragging
			{
				eventData->bIsDragging = false;
				if (IsValid(eventData->PressComponent))
				{
					if (!eventData->bIsUpFiredAtCurrentFrame)
					{
						eventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerUp(eventData->PressComponent, eventData, eventData->PressComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(eventData->PressComponent, eventData, eventData->PressComponentEventFireType);
						}
					}
					eventData->PressComponent = nullptr;
				}
				if (bLineTraceHitSomething)//hit something when stop drag
				{
					//if enter an object when drag, and after one frame trigger release and hit new object, then old object need to call DragExit
					if (IsValid(eventData->EnterComponent) && eventData->EnterComponent != eventData->DragComponent)
					{
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerDragDrop(eventData->EnterComponent, eventData, eventData->EnterComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerDragDrop(eventData->EnterComponent, eventData, eventData->EnterComponentEventFireType);
						}
					}
				}
				//drag end
				if (IsValid(eventData->DragComponent))
				{
					if (!eventData->bIsEndDragFiredAtCurrentFrame)
					{
						eventData->bIsEndDragFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerEndDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerEndDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType);
						}
					}
					eventData->DragComponent = nullptr;
				}
			}
			else//not dragging
			{
				if (IsValid(eventData->PressComponent))
				{
					if (!eventData->bIsUpFiredAtCurrentFrame)
					{
						eventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerUp(eventData->PressComponent, eventData, eventData->PressComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(eventData->PressComponent, eventData, eventData->PressComponentEventFireType);
						}
					}
					eventData->ClickTime = eventData->GetWorld()->GetTimeSeconds();
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerClick(eventData->PressComponent, eventData, eventData->PressComponentEventFireType, true);
					}
					else
					{
						eventSystem->CallOnPointerClick(eventData->PressComponent, eventData, eventData->PressComponentEventFireType);
					}
					eventData->PressComponent = nullptr;
				}
			}
		}
	}

	eventData->bPrevIsTriggerPressed = eventData->bNowIsTriggerPressed;
}
bool ULexPointerInputModule::Navigate(ELexUINavigationDirection direction, ULexPointerEventData* InPointerEventData, FLexUIHitResult& LGUIHitResult)
{
	if (!EventSystem.IsValid())return false;

	auto currentHover = InPointerEventData->HighlightComponentForNavigation.Get();
	UActorComponent* currentNavigateObject = nullptr;
	if (IsValid(currentHover))
	{
		AActor* searchActor = currentHover->GetOwner();
		auto FindNavigationInterface = [](AActor* InActor) {
			auto& Components = InActor->GetComponents();
			for (auto& Comp : Components)
			{
				if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(ULexNavigationInterface::StaticClass()))
				{
					return Comp;
				}
			}
			return (UActorComponent*)nullptr;
		};
		while (IsValid(searchActor))
		{
			currentNavigateObject = FindNavigationInterface(searchActor);
			if (currentNavigateObject != nullptr)
			{
				break;
			}
			searchActor = searchActor->GetAttachParentActor();
		}
	}
	
	if (currentNavigateObject == nullptr)//not find valid selectable object, use default one
	{
		currentNavigateObject = UUISelectableComponent::FindDefaultSelectable(this);//@todo: don't reference UISelectableComponent directly
	}
	else//find valid selectable, do navigation
	{
		TScriptInterface<ILexNavigationInterface> nextNavigateInterface = nullptr;
		if (ILexNavigationInterface::Execute_OnNavigate(currentNavigateObject, direction, nextNavigateInterface))
		{
			auto nextNavigateObject = Cast<UActorComponent>(nextNavigateInterface.GetObject());
			if (nextNavigateObject)
			{
				currentNavigateObject = nextNavigateObject;
			}
		}
	}
	if (currentNavigateObject != nullptr)
	{
		LGUIHitResult.HitResult.Component = (UPrimitiveComponent*)currentNavigateObject->GetOwner()->GetRootComponent();//this convert is incorrect, but I need this pointer
		LGUIHitResult.HitResult.Location = LGUIHitResult.HitResult.Component->GetComponentLocation();
		LGUIHitResult.HitResult.Normal = LGUIHitResult.HitResult.Component->GetComponentTransform().TransformVector(FVector(0, 0, 1));
		LGUIHitResult.HitResult.Normal.Normalize();
		LGUIHitResult.EventFireType = EventSystem->GetEventFireTypeForNavigation();
		LGUIHitResult.Raycaster = nullptr;
		LGUIHitResult.HoverArray.Reset();

		InPointerEventData->HighlightComponentForNavigation = currentNavigateObject->GetOwner()->GetRootComponent();
		return true;
	}
	return false;
}

void ULexPointerInputModule::ProcessInputForNavigation()
{
	for (auto& keyValue : EventSystem->GetPointerEventDataMap())
	{
		ProcessInputForNavigation(keyValue.Value);
	}
}
void ULexPointerInputModule::ProcessInputForNavigation(ULexPointerEventData* eventData)
{
	auto timeSeconds = this->GetWorld()->GetTimeSeconds();
	while (timeSeconds > eventData->NavigateTickTime)
	{
		bool isFirstPressInSequence = eventData->NavigateTickTime == 0.0f;
		auto timeInterval = isFirstPressInSequence ? EventSystem->GetNavigateInputIntervalForFirstTime() : EventSystem->GetNavigateInputInterval();
		if (isFirstPressInSequence)
		{
			eventData->NavigateTickTime = timeSeconds + timeInterval;
		}
		else
		{
			eventData->NavigateTickTime += timeInterval;
		}
		FLexUIHitResult LGUIHitResult;
		bool selectValid = Navigate(eventData->NavigateDirection, eventData, LGUIHitResult);
		bool resultHitSomething = false;
		FHitResult hitResult;
		ProcessPointerEvent(EventSystem.Get(), eventData, selectValid, LGUIHitResult, resultHitSomething, hitResult);
		if (resultHitSomething)
		{
			EventSystem->SetSelectComponent((USceneComponent*)hitResult.Component.Get(), eventData, EventSystem->GetEventFireTypeForNavigation());
		}

		auto tempHitComp = (USceneComponent*)hitResult.Component.Get();
		EventSystem->RaiseHitEvent(resultHitSomething, hitResult, tempHitComp);
	}
}
void ULexPointerInputModule::InputNavigation(ELexUINavigationDirection direction, bool pressOrRelease, int pointerID)
{
	auto eventData = EventSystem->GetPointerEventData(pointerID, true);
	if (pressOrRelease)
	{
		EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Navigation);
		eventData->NavigateDirection = direction;
	}
	else
	{
		eventData->NavigateDirection = ELexUINavigationDirection::None;
	}
	eventData->NavigateTickTime = 0;
}
void ULexPointerInputModule::InputTriggerForNavigation(bool inTriggerPress, int pointerID)
{
	auto eventData = EventSystem->GetPointerEventData(pointerID, true);
	if (inTriggerPress)
	{
		EventSystem->SetPointerInputType(eventData, ELexUIPointerInputType::Navigation);
	}
	eventData->NavigateTickTime = 0;
	eventData->bNowIsTriggerPressed = inTriggerPress;
}

void ULexPointerInputModule::ClearEventByID(int pointerID)
{
	auto eventData = EventSystem->GetPointerEventData(pointerID, false);
	if (eventData == nullptr)return;
	if (!EventSystem.IsValid())return;

	if (eventData->bPrevIsTriggerPressed)//if trigger is pressed
	{
		if (eventData->bIsDragging)
		{
			eventData->bIsDragging = false;
			if (!eventData->bIsEndDragFiredAtCurrentFrame)
			{
				eventData->bIsEndDragFiredAtCurrentFrame = true;
				if (IsValid(eventData->DragComponent))
				{
					EventSystem->CallOnPointerEndDrag(eventData->DragComponent, eventData, eventData->DragComponentEventFireType);
					eventData->DragComponent = nullptr;
				}
			}
		}

		if (!eventData->bIsUpFiredAtCurrentFrame)
		{
			eventData->bIsUpFiredAtCurrentFrame = true;
			if (IsValid(eventData->PressComponent))
			{
				auto oldPressComponent = eventData->PressComponent;
				eventData->PressComponent = nullptr;
				EventSystem->CallOnPointerUp(oldPressComponent, eventData, eventData->PressComponentEventFireType);
			}
		}
		if (!eventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(eventData->EnterComponent) || eventData->EnterComponentStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), eventData, eventData->EnterComponent, nullptr, eventData->EnterComponentEventFireType);
			}
			eventData->bIsExitFiredAtCurrentFrame = true;
		}

		eventData->bPrevIsTriggerPressed = false;
	}
	else
	{
		if (!eventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(eventData->EnterComponent) || eventData->EnterComponentStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), eventData, eventData->EnterComponent, nullptr, eventData->EnterComponentEventFireType);
			}
			eventData->bIsExitFiredAtCurrentFrame = true;
		}
	}
}

bool ULexPointerInputModule::CanHandleInterface(USceneComponent* targetComp, UClass* targetInterfaceClass, ELexUIEventFireType eventFireType)
{
	bool canSelectPressedComponent = false;
	switch (eventFireType)
	{
	case ELexUIEventFireType::OnlyTargetActor:
	{
		if (targetComp->GetOwner()->GetClass()->ImplementsInterface(targetInterfaceClass))
		{
			canSelectPressedComponent = true;
		}
	}
	break;
	case ELexUIEventFireType::OnlyTargetComponent:
	{
		if (targetComp->GetClass()->ImplementsInterface(targetInterfaceClass))
		{
			canSelectPressedComponent = true;
		}
	}
	break;
	case ELexUIEventFireType::TargetActorAndAllItsComponents:
	{
		if (targetComp->GetOwner()->GetClass()->ImplementsInterface(targetInterfaceClass))
		{
			canSelectPressedComponent = true;
		}
		if (!canSelectPressedComponent)
		{
			auto components = targetComp->GetOwner()->GetComponents();
			for (auto item : components)
			{
				if (item->GetClass()->ImplementsInterface(targetInterfaceClass))
				{
					canSelectPressedComponent = true;
					break;
				}
			}
		}
	}
	break;
	}
	return canSelectPressedComponent;
}

USceneComponent* ULexPointerInputModule::GetEventHandle(USceneComponent* targetComp, UClass* targetInterfaceClass, ELexUIEventFireType eventFireType)
{
	if (!IsValid(targetComp))
	{
		return nullptr;
	}

	USceneComponent* rootComp = targetComp;
	while (rootComp != nullptr)
	{
		if (CanHandleInterface(rootComp, targetInterfaceClass, eventFireType))
		{
			return rootComp;
		}
		rootComp = rootComp->GetAttachParent();
	}
	return nullptr;
}
void ULexPointerInputModule::DeselectIfSelectionChanged(ULexEventSystem* eventSystem, USceneComponent* currentPressed, ULexBaseEventData* eventData)
{
	auto selectHandleComp = GetEventHandle(currentPressed, ULexPointerSelectDeselectInterface::StaticClass(), eventData->SelectedComponentEventFireType);
	if (selectHandleComp != eventData->SelectedComponent)
	{
		ULexEventSystem::SetSelectComponent(eventSystem, nullptr, eventData, eventData->SelectedComponentEventFireType);
	}
}

void ULexPointerInputModule::ClearEvent()
{
	for (auto& keyValue : EventSystem->GetPointerEventDataMap())
	{
		ClearEventByID(keyValue.Key);
	}
}


