// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/InputModule/LexPointerInputModule.h"
#include "Event/LexPointerEventData.h"
#include "Core/LexUIManager.h"
#include "Event/LexEventSystem.h"
#include "Event/LexBaseRaycaster.h"
#include "Event/LexScreenSpaceRaycaster.h"
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
				auto AIsScreenSpace = A.Raycaster->IsA(ULexScreenSpaceRaycaster::StaticClass());
				auto BIsScreenSpace = B.Raycaster->IsA(ULexScreenSpaceRaycaster::StaticClass());
				if (AIsScreenSpace && !BIsScreenSpace)
				{
					return true;
				}
				if (BIsScreenSpace && !AIsScreenSpace)
				{
					return false;
				}
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
void ULexPointerInputModule::ProcessPointerEnterExit(ULexEventSystem* eventSystem, ULexPointerEventData* EventData, USceneComponent* oldObj, USceneComponent* newObj, ELexUIEventFireType enterFireType)
{
	if (oldObj == newObj)return;
	if (IsValid(oldObj) && IsValid(newObj))
	{
		auto commonRoot = FindCommonRoot(oldObj->GetOwner(), newObj->GetOwner());
#if LOG_ENTER_EXIT
		UE_LOG(LGUI, Error, TEXT("-----begin exit 000, commonRoot:%s"), commonRoot != nullptr ? *(commonRoot->GetActorLabel()) : TEXT("null"));
#endif
		//exit old
		for (int i = EventData->EnterComponentStack.Num() - 1; i >= 0; i--)
		{
			if (commonRoot == EventData->EnterComponentStack[i]->GetOwner())
			{
				break;
			}
			if (!EventData->bIsExitFiredAtCurrentFrame)
			{
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerExit(EventData->EnterComponentStack[i], EventData, EventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerExit(EventData->EnterComponentStack[i], EventData, EventData->EnterComponentEventFireType);
				}
			}
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("	%s"), *(EventData->enterComponentStack[i]->GetOwner()->GetActorLabel()));
#endif
			EventData->EnterComponentStack.RemoveAt(i);
		}
		EventData->EnterComponent = nullptr;
		EventData->bIsExitFiredAtCurrentFrame = true;
#if LOG_ENTER_EXIT
		UE_LOG(LGUI, Error, TEXT("*****end exit, stack count:%d\n"), EventData->enterComponentStack.Num());
#endif
		//enter new
		EventData->EnterComponent = newObj;
		EventData->EnterComponentEventFireType = enterFireType;
		AActor* enterObjectActor = newObj->GetOwner();
		if (commonRoot != enterObjectActor)
		{
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("-----begin enter 111"));
#endif
			int insertIndex = EventData->EnterComponentStack.Num();
			if (eventSystem == nullptr)
			{
				ULexEventSystem::ExecuteEvent_OnPointerEnter(newObj, EventData, EventData->EnterComponentEventFireType, false);
			}
			else
			{
				eventSystem->CallOnPointerEnter(newObj, EventData, EventData->EnterComponentEventFireType);
			}
			EventData->HighlightComponentForNavigation = newObj;
			EventData->EnterComponentStack.Add(newObj);
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
					ULexEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor->GetRootComponent(), EventData, EventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(enterObjectActor->GetRootComponent(), EventData, EventData->EnterComponentEventFireType);
				}
				EventData->EnterComponentStack.Insert(enterObjectActor->GetRootComponent(), insertIndex);
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
				enterObjectActor = enterObjectActor->GetAttachParentActor();
			}
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("*****end enter, stack count:%d\n"), EventData->enterComponentStack.Num());
#endif
		}
	}
	else
	{
		if (IsValid(oldObj) || EventData->EnterComponentStack.Num() > 0)
		{
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("-----begin exit 222"));
#endif
			//exit old
			for (int i = EventData->EnterComponentStack.Num() - 1; i >= 0; i--)
			{
				if (IsValid(EventData->EnterComponentStack[i]))
				{
					if (!EventData->bIsExitFiredAtCurrentFrame)
					{
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerExit(EventData->EnterComponentStack[i], EventData, EventData->EnterComponentEventFireType, false);
						}
						else
						{
							eventSystem->CallOnPointerExit(EventData->EnterComponentStack[i], EventData, EventData->EnterComponentEventFireType);
						}
					}
#if LOG_ENTER_EXIT
					UE_LOG(LGUI, Error, TEXT("	%s, fireType:%d"), *(EventData->enterComponentStack[i]->GetOwner()->GetActorLabel()), (int)(EventData->enterComponentEventFireType));
#endif
				}
				EventData->EnterComponentStack.RemoveAt(i);
			}
			EventData->EnterComponent = nullptr;
			EventData->bIsExitFiredAtCurrentFrame = true;
#if LOG_ENTER_EXIT
			UE_LOG(LGUI, Error, TEXT("*****end exit, stack count:%d\n"), EventData->enterComponentStack.Num());
#endif
			EventData->EnterComponentStack.Reset();
		}
		if (IsValid(newObj))
		{
			//enter new
			if (!EventData->EnterComponentStack.Contains(newObj))
			{
				AActor* enterObjectActor = newObj->GetOwner();
				int insertIndex = EventData->EnterComponentStack.Num();
				EventData->EnterComponent = newObj;
				EventData->EnterComponentEventFireType = enterFireType;
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("-----begin enter 333"));
				UE_LOG(LGUI, Error, TEXT("	%s"), *(enterObjectActor->GetActorLabel()));
#endif
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerEnter(newObj, EventData, EventData->EnterComponentEventFireType, false);
				}
				else
				{
					eventSystem->CallOnPointerEnter(newObj, EventData, EventData->EnterComponentEventFireType);
				}
				EventData->HighlightComponentForNavigation = newObj;
				EventData->EnterComponentStack.Add(newObj);
				enterObjectActor = enterObjectActor->GetAttachParentActor();
				while (enterObjectActor != nullptr)
				{
#if LOG_ENTER_EXIT
					UE_LOG(LGUI, Error, TEXT("	:%s"), *(enterObjectActor->GetActorLabel()));
#endif
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerEnter(enterObjectActor->GetRootComponent(), EventData, EventData->EnterComponentEventFireType, false);
					}
					else
					{
						eventSystem->CallOnPointerEnter(enterObjectActor->GetRootComponent(), EventData, EventData->EnterComponentEventFireType);
					}
					EventData->EnterComponentStack.Insert(enterObjectActor->GetRootComponent(), insertIndex);
					enterObjectActor = enterObjectActor->GetAttachParentActor();
				}
#if LOG_ENTER_EXIT
				UE_LOG(LGUI, Error, TEXT("*****end enter, stack count:%d\n"), EventData->enterComponentStack.Num());
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
void ULexPointerInputModule::ProcessPointerEvent(ULexEventSystem* eventSystem, ULexPointerEventData* EventData, bool bLineTraceHitSomething, const FLexUIHitResult& LexHitResult, bool& OutIsHitSomething, FHitResult& OutHitResult)
{
	EventData->bIsUpFiredAtCurrentFrame = false;
	EventData->bIsExitFiredAtCurrentFrame = false;
	EventData->bIsEndDragFiredAtCurrentFrame = false;

	EventData->FaceIndex = LexHitResult.HitResult.FaceIndex;
	EventData->Raycaster = LexHitResult.Raycaster;
	OutHitResult = LexHitResult.HitResult;
	OutIsHitSomething = bLineTraceHitSomething;

	if (bLineTraceHitSomething)
	{
		auto nowHitComponent = (USceneComponent*)OutHitResult.Component.Get();
		//fire event
		EventData->WorldPoint = OutHitResult.Location;
		EventData->WorldNormal = OutHitResult.Normal;
		if (EventData->EnterComponent != nowHitComponent)//hit different object
		{
			ProcessPointerEnterExit(eventSystem, EventData, EventData->EnterComponent, nowHitComponent, LexHitResult.EventFireType);
		}
	}
	else
	{
		if (IsValid(EventData->EnterComponent) || EventData->EnterComponentStack.Num() > 0)//prev object
		{
			ProcessPointerEnterExit(eventSystem, EventData, EventData->EnterComponent, nullptr, LexHitResult.EventFireType);
		}
	}

	if (EventData->bNowIsTriggerPressed && EventData->bPrevIsTriggerPressed)//if trigger keep pressing
	{
		if (EventData->bIsDragging)//if dragging
		{
			//trigger drag event
			if (IsValid(EventData->DragComponent))
			{
				if (eventSystem == nullptr)
				{
					ULexEventSystem::ExecuteEvent_OnPointerDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType, true);
				}
				else
				{
					eventSystem->CallOnPointerDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType);
				}

				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;//always hit a plane when drag
			}
			else
			{
				EventData->bIsDragging = false;
			}
		}
		else//trigger press but not dragging, only concern if trigger drag event
		{
			if (IsValid(EventData->PressComponent))//if hit something when press
			{
				if (IsValid(EventData->PressRaycaster))
				{
					if (EventData->PressRaycaster->ShouldStartDrag(EventData))
					{
						EventData->bIsDragging = true;
						EventData->DragComponent = EventData->PressComponent;
						EventData->DragComponentEventFireType = EventData->PressComponentEventFireType;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerBeginDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerBeginDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType);
						}
					}
				}
				OutHitResult.Distance = EventData->PressDistance;
				OutIsHitSomething = true;
			}
		}
	}
	else if (!EventData->bNowIsTriggerPressed && !EventData->bPrevIsTriggerPressed)//is trigger keep release, only concern Enter/Exit event
	{
		
	}
	else//trigger state change
	{
		if (EventData->bNowIsTriggerPressed)//now is press, prev is release
		{
			if (bLineTraceHitSomething)
			{
				if (IsValid(EventData->EnterComponent))//now object
				{
					EventData->WorldPoint = OutHitResult.Location;
					EventData->WorldNormal = OutHitResult.Normal;
					EventData->PressDistance = OutHitResult.Distance;
					EventData->PressRayOrigin = LexHitResult.RayOrigin;
					EventData->PressRayDirection = LexHitResult.RayDirection;
					EventData->PressWorldPoint = OutHitResult.Location;
					EventData->PressWorldNormal = OutHitResult.Normal;
					EventData->PressRaycaster = LexHitResult.Raycaster;
					EventData->PressWorldToLocalTransform = EventData->EnterComponent->GetComponentTransform().Inverse();
					EventData->PressComponent = EventData->EnterComponent;
					EventData->PressComponentEventFireType = EventData->EnterComponentEventFireType;
					DeselectIfSelectionChanged(eventSystem, EventData->PressComponent, EventData);
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerDown(EventData->PressComponent, EventData, EventData->PressComponentEventFireType, true);
					}
					else
					{
						eventSystem->CallOnPointerDown(EventData->PressComponent, EventData, EventData->PressComponentEventFireType);
					}
				}
			}
		}
		else//now is release, prev is press
		{
			if (EventData->bIsDragging)//is dragging
			{
				EventData->bIsDragging = false;
				if (IsValid(EventData->PressComponent))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerUp(EventData->PressComponent, EventData, EventData->PressComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(EventData->PressComponent, EventData, EventData->PressComponentEventFireType);
						}
					}
					EventData->PressComponent = nullptr;
				}
				if (bLineTraceHitSomething)//hit something when stop drag
				{
					//if enter an object when drag, and after one frame trigger release and hit new object, then old object need to call DragExit
					if (IsValid(EventData->EnterComponent) && EventData->EnterComponent != EventData->DragComponent)
					{
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerDragDrop(EventData->EnterComponent, EventData, EventData->EnterComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerDragDrop(EventData->EnterComponent, EventData, EventData->EnterComponentEventFireType);
						}
					}
				}
				//drag end
				if (IsValid(EventData->DragComponent))
				{
					if (!EventData->bIsEndDragFiredAtCurrentFrame)
					{
						EventData->bIsEndDragFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerEndDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerEndDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType);
						}
					}
					EventData->DragComponent = nullptr;
				}
			}
			else//not dragging
			{
				if (IsValid(EventData->PressComponent))
				{
					if (!EventData->bIsUpFiredAtCurrentFrame)
					{
						EventData->bIsUpFiredAtCurrentFrame = true;
						if (eventSystem == nullptr)
						{
							ULexEventSystem::ExecuteEvent_OnPointerUp(EventData->PressComponent, EventData, EventData->PressComponentEventFireType, true);
						}
						else
						{
							eventSystem->CallOnPointerUp(EventData->PressComponent, EventData, EventData->PressComponentEventFireType);
						}
					}
					EventData->ClickTime = EventData->GetWorld()->GetTimeSeconds();
					if (eventSystem == nullptr)
					{
						ULexEventSystem::ExecuteEvent_OnPointerClick(EventData->PressComponent, EventData, EventData->PressComponentEventFireType, true);
					}
					else
					{
						eventSystem->CallOnPointerClick(EventData->PressComponent, EventData, EventData->PressComponentEventFireType);
					}
					EventData->PressComponent = nullptr;
				}
			}
		}
	}

	EventData->bPrevIsTriggerPressed = EventData->bNowIsTriggerPressed;
}
bool ULexPointerInputModule::Navigate(ELexUINavigationDirection InDirection, ULexPointerEventData* InPointerEventData, FLexUIHitResult& OutLexUIHitResult)
{
	if (InDirection == ELexUINavigationDirection::None)return false;
	auto CurrentHover = InPointerEventData->HighlightComponentForNavigation.Get();
	UActorComponent* CurrentNavigateObject = nullptr;
	if (IsValid(CurrentHover))
	{
		AActor* SearchActor = CurrentHover->GetOwner();
		auto FindNavigationInterface = [](AActor* InActor) {
			auto& Components = InActor->GetComponents();
			for (auto& Comp : Components)
			{
				if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(ULexNavigationInterface::StaticClass()))
				{
					if (ILexNavigationInterface::Execute_CanNavigateHere(Comp))
					{
						return Comp;
					}
				}
			}
			return (UActorComponent*)nullptr;
		};
		while (IsValid(SearchActor))
		{
			CurrentNavigateObject = FindNavigationInterface(SearchActor);
			if (CurrentNavigateObject != nullptr)
			{
				break;
			}
			SearchActor = SearchActor->GetAttachParentActor();
		}
	}
	
	if (CurrentNavigateObject == nullptr)//not find valid selectable object, use default one
	{
		CurrentNavigateObject = UUISelectableComponent::FindDefaultSelectable(this);//@todo: don't reference UISelectableComponent directly
	}
	else//find valid selectable, do navigation
	{
		TScriptInterface<ILexNavigationInterface> NextNavigateInterface = nullptr;
		if (ILexNavigationInterface::Execute_OnNavigate(CurrentNavigateObject, InDirection, NextNavigateInterface))
		{
			if (auto NextNavigateObject = Cast<UActorComponent>(NextNavigateInterface.GetObject()))
			{
				CurrentNavigateObject = NextNavigateObject;
			}
		}
	}
	if (CurrentNavigateObject != nullptr)
	{
		OutLexUIHitResult.HitResult.Component = (UPrimitiveComponent*)CurrentNavigateObject->GetOwner()->GetRootComponent();//this convert is incorrect, but I need this pointer
		OutLexUIHitResult.HitResult.Location = OutLexUIHitResult.HitResult.Component->GetComponentLocation();
		OutLexUIHitResult.HitResult.Normal = OutLexUIHitResult.HitResult.Component->GetComponentTransform().TransformVector(FVector(0, 0, 1));
		OutLexUIHitResult.HitResult.Normal.Normalize();
		OutLexUIHitResult.EventFireType = EventSystem->GetEventFireTypeForNavigation();
		OutLexUIHitResult.Raycaster = nullptr;
		OutLexUIHitResult.HoverArray.Reset();

		InPointerEventData->HighlightComponentForNavigation = CurrentNavigateObject->GetOwner()->GetRootComponent();
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
void ULexPointerInputModule::ProcessInputForNavigation(ULexPointerEventData* EventData)
{
	auto TimeSeconds = this->GetWorld()->GetTimeSeconds();
	while (TimeSeconds > EventData->NavigateTickTime)
	{
		bool bIsFirstPressInSequence = EventData->NavigateTickTime == 0.0f;
		auto TimeInterval = bIsFirstPressInSequence ? EventSystem->GetNavigateInputIntervalForFirstTime() : EventSystem->GetNavigateInputInterval();
		if (bIsFirstPressInSequence)
		{
			EventData->NavigateTickTime = TimeSeconds + TimeInterval;
		}
		else
		{
			EventData->NavigateTickTime += TimeInterval;
		}
		FLexUIHitResult LexUIHitResult;
		bool bSelectValid = Navigate(EventData->NavigateDirection, EventData, LexUIHitResult);
		bool bResultHitSomething = false;
		FHitResult HitResult;
		ProcessPointerEvent(EventSystem.Get(), EventData, bSelectValid, LexUIHitResult, bResultHitSomething, HitResult);
		if (bResultHitSomething)
		{
			EventSystem->SetSelectComponent(HitResult.Component.Get(), EventData, EventSystem->GetEventFireTypeForNavigation());
		}

		auto TempHitComp = (USceneComponent*)HitResult.Component.Get();
		EventSystem->RaiseHitEvent(bResultHitSomething, HitResult, TempHitComp);
	}
}
void ULexPointerInputModule::InputNavigation(ELexUINavigationDirection direction, bool pressOrRelease, int pointerID)
{
	auto EventData = EventSystem->GetPointerEventData(pointerID, true);
	if (pressOrRelease)
	{
		EventSystem->SetPointerInputType(EventData, ELexUIPointerInputType::Navigation);
		EventData->NavigateDirection = direction;
	}
	else
	{
		EventData->NavigateDirection = ELexUINavigationDirection::None;
	}
	EventData->NavigateTickTime = 0;
}
void ULexPointerInputModule::InputTriggerForNavigation(bool inTriggerPress, int pointerID)
{
	auto EventData = EventSystem->GetPointerEventData(pointerID, true);
	if (inTriggerPress)
	{
		EventSystem->SetPointerInputType(EventData, ELexUIPointerInputType::Navigation);
	}
	EventData->NavigateTickTime = 0;
	EventData->bNowIsTriggerPressed = inTriggerPress;
}

void ULexPointerInputModule::ClearEventByID(int pointerID)
{
	auto EventData = EventSystem->GetPointerEventData(pointerID, false);
	if (EventData == nullptr)return;
	if (!EventSystem.IsValid())return;

	if (EventData->bPrevIsTriggerPressed)//if trigger is pressed
	{
		if (EventData->bIsDragging)
		{
			EventData->bIsDragging = false;
			if (!EventData->bIsEndDragFiredAtCurrentFrame)
			{
				EventData->bIsEndDragFiredAtCurrentFrame = true;
				if (IsValid(EventData->DragComponent))
				{
					EventSystem->CallOnPointerEndDrag(EventData->DragComponent, EventData, EventData->DragComponentEventFireType);
					EventData->DragComponent = nullptr;
				}
			}
		}

		if (!EventData->bIsUpFiredAtCurrentFrame)
		{
			EventData->bIsUpFiredAtCurrentFrame = true;
			if (IsValid(EventData->PressComponent))
			{
				auto oldPressComponent = EventData->PressComponent;
				EventData->PressComponent = nullptr;
				EventSystem->CallOnPointerUp(oldPressComponent, EventData, EventData->PressComponentEventFireType);
			}
		}
		if (!EventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(EventData->EnterComponent) || EventData->EnterComponentStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), EventData, EventData->EnterComponent, nullptr, EventData->EnterComponentEventFireType);
			}
			EventData->bIsExitFiredAtCurrentFrame = true;
		}

		EventData->bPrevIsTriggerPressed = false;
	}
	else
	{
		if (!EventData->bIsExitFiredAtCurrentFrame)
		{
			if (IsValid(EventData->EnterComponent) || EventData->EnterComponentStack.Num() > 0)
			{
				ProcessPointerEnterExit(EventSystem.Get(), EventData, EventData->EnterComponent, nullptr, EventData->EnterComponentEventFireType);
			}
			EventData->bIsExitFiredAtCurrentFrame = true;
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
void ULexPointerInputModule::DeselectIfSelectionChanged(ULexEventSystem* eventSystem, USceneComponent* currentPressed, ULexBaseEventData* EventData)
{
	auto selectHandleComp = GetEventHandle(currentPressed, ULexPointerSelectDeselectInterface::StaticClass(), EventData->SelectedComponentEventFireType);
	if (selectHandleComp != EventData->SelectedComponent)
	{
		ULexEventSystem::SetSelectComponent(eventSystem, nullptr, EventData, EventData->SelectedComponentEventFireType);
	}
}

void ULexPointerInputModule::ClearEvent()
{
	for (auto& keyValue : EventSystem->GetPointerEventDataMap())
	{
		ClearEventByID(keyValue.Key);
	}
}


