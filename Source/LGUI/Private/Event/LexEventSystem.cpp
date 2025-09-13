// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexEventSystem.h"
#include "Event/Interface/LexPointerClickInterface.h"
#include "Event/Interface/LexPointerEnterExitInterface.h"
#include "Event/Interface/LexPointerDownUpInterface.h"
#include "Event/Interface/LexPointerDragInterface.h"
#include "Event/Interface/LexPointerScrollInterface.h"
#include "Event/Interface/LexPointerDragDropInterface.h"
#include "Event/Interface/LexPointerSelectDeselectInterface.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexWidget.h"
#include "Event/LexBaseRaycaster.h"
#include "Event/LexPointerEventData.h"
#include "Event/InputModule/LexBaseInputModule.h"
#include "Utils/LexUIUtils.h"
#include "LGUI.h"

#define LOCTEXT_NAMESPACE "LGUIEventSystemActor"

ALGUIEventSystemActor::ALGUIEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;

	EventSystem = CreateDefaultSubobject<ULexEventSystem>(TEXT("EventSystem"));
}

DECLARE_CYCLE_STAT(TEXT("EventSystem"), STAT_EventSystem, STATGROUP_LGUI);

TMap<UWorld*, ULexEventSystem*> ULexEventSystem::WorldToInstanceMap;
ULexEventSystem::ULexEventSystem()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}
ULexEventSystem* ULexEventSystem::GetLexEventSystemInstance(UObject* WorldContextObject)
{
	auto world = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (auto result = WorldToInstanceMap.Find(world))
	{
		return *result;
	}
	else
	{
		return nullptr;
	}
}
void ULexEventSystem::BeginPlay()
{
	Super::BeginPlay();
	//UE_LOG(LGUI, Error, TEXT("LGUIEventSystem, world:%d, this:%d, isClientOrServer:%d, worldPath:%s"), this->GetWorld(), this, this->GetWorld()->IsClient(), *this->GetWorld()->GetPathName());
	if (auto world = this->GetWorld())
	{
		if (auto instancePtr = WorldToInstanceMap.Find(world))
		{
			auto instance = *instancePtr;
			FString actorName =
#if WITH_EDITOR
				instance->GetOwner()->GetActorLabel();
#else
				instance->GetOwner()->GetName();
#endif
			FString errorMsg = FString::Printf(TEXT("LGUIEventSystem component is already exist in actor:%s, pathName:%s, world:%s, multiple LGUIEventSystem in same world is not allowed!"), *actorName, *instance->GetPathName(), *world->GetPathName());
			UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg);
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(FText::FromString(errorMsg), 10);
#endif
			this->SetComponentTickEnabled(false);
		}
		else
		{
			WorldToInstanceMap.Add(world, this);
			existInInstanceMap = true;
		}
	}
}

void ULexEventSystem::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bRayEventEnable)
	{
		ProcessInputEvent();
	}
}

void ULexEventSystem::ProcessInputEvent()
{
	if (auto LGUIManagerActor = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		auto CurrentInputModule = LGUIManagerActor->GetCurrentInputModule();
		if (CurrentInputModule.IsValid())
		{
			SCOPE_CYCLE_COUNTER(STAT_EventSystem);
			CurrentInputModule->ProcessInput();
		}
	}
}

void ULexEventSystem::SetRaycastEnable(bool enable, bool clearEvent)
{
	bRayEventEnable = enable;
	if (bRayEventEnable == false && clearEvent)
	{
		ClearEvent();
	}
}

void ULexEventSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
void ULexEventSystem::BeginDestroy()
{
	Super::BeginDestroy();
	if (WorldToInstanceMap.Num() > 0 && existInInstanceMap)
	{
		bool removed = false;
		if (auto world = this->GetWorld())
		{
			WorldToInstanceMap.Remove(world);
			//UE_LOG(LGUI, Error, TEXT("[%s].%dRemove instance:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, this);
			removed = true;
		}
		else
		{
			world = nullptr;
			for (auto keyValue : WorldToInstanceMap)
			{
				if (keyValue.Value == this)
				{
					world = keyValue.Key;
				}
			}
			if (world != nullptr)
			{
				WorldToInstanceMap.Remove(world);
				//UE_LOG(LGUI, Error, TEXT("[%s].%d Remove instance:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, this);
				removed = true;
			}
		}
		if (removed)
		{
			existInInstanceMap = false;
		}
		else
		{
			UE_LOG(LGUI, Warning, TEXT("[%s].%d Instance not removed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	if(WorldToInstanceMap.Num() <= 0)
	{
		UE_LOG(LGUI, Log, TEXT("[%s].%d All instance removed."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}

void ULexEventSystem::ClearEvent()
{
	if (auto LGUIManagerActor = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		auto CurrentInputModule = LGUIManagerActor->GetCurrentInputModule();
		if (CurrentInputModule.IsValid())
		{
			CurrentInputModule->ClearEvent();
		}
	}
}

ULexPointerEventData* ULexEventSystem::GetPointerEventData(int pointerID, bool createIfNotExist)const
{
	if (auto foundPtr = PointerEventDataMap.Find(pointerID))
	{
		return *foundPtr;
	}
	auto newEventData = NewObject<ULexPointerEventData>(const_cast<ULexEventSystem*>(this));
	newEventData->pointerID = pointerID;
	newEventData->inputType = defaultInputType;
	PointerEventDataMap.Add(pointerID, newEventData);
	return newEventData;
}
void ULexEventSystem::RemovePointerEventData(int pointerID)
{
	PointerEventDataMap.Remove(pointerID);
}


void ULexEventSystem::RegisterHitEvent(const FLGUIHitDelegate& InEvent)
{
	hitEvent.Add(InEvent);
}
void ULexEventSystem::UnregisterHitEvent(const FLGUIHitDelegate& InEvent)
{
	hitEvent.Remove(InEvent.GetHandle());
}
void ULexEventSystem::RegisterGlobalListener(const FLGUIBaseEventDelegate& InEvent)
{
	globalListenerPointerEvent.Add(InEvent);
}
void ULexEventSystem::UnregisterGlobalListener(const FLGUIBaseEventDelegate& InEvent)
{
	globalListenerPointerEvent.Remove(InEvent.GetHandle());
}


FLGUIDelegateHandleWrapper ULexEventSystem::RegisterHitEvent(const FLGUIHitDynamicDelegate& InDelegate)
{
	auto delegateHandle = hitEvent.AddLambda([InDelegate](bool isHit, const FHitResult& hitResult, USceneComponent* hitComp) {
		InDelegate.ExecuteIfBound(isHit, hitResult, hitComp);
	});
	return FLGUIDelegateHandleWrapper(delegateHandle);
}
void ULexEventSystem::UnregisterHitEvent(const FLGUIDelegateHandleWrapper& InHandle)
{
	hitEvent.Remove(InHandle.DelegateHandle);
}
FLGUIDelegateHandleWrapper ULexEventSystem::RegisterGlobalListener(const FLGUIBaseEventDynamicDelegate& InDelegate)
{
	auto delegateHandle = globalListenerPointerEvent.AddLambda([InDelegate](ULexBaseEventData* eventData) {
		InDelegate.ExecuteIfBound(eventData);
	});
	return FLGUIDelegateHandleWrapper(delegateHandle);
}
void ULexEventSystem::UnregisterGlobalListener(const FLGUIDelegateHandleWrapper& InHandle)
{
	globalListenerPointerEvent.Remove(InHandle.DelegateHandle);
}
void ULexEventSystem::RaiseHitEvent(bool hitOrNot, const FHitResult& hitResult, USceneComponent* hitComponent)
{
	if (hitEvent.IsBound() && bRayEventEnable)
	{
		hitEvent.Broadcast(hitOrNot, hitResult, hitComponent);
	}
}
bool ULexEventSystem::IsPointerOverUIByPointerID(int pointerID)
{
	if (auto foundPtr = PointerEventDataMap.Find(pointerID))
	{
		return (*foundPtr)->IsPointerOverUI();
	}
	return false;
}

void ULexEventSystem::SetHighlightedComponentForNavigation(USceneComponent* InComp, int InPointerID)
{
	if (auto eventData = GetPointerEventData(InPointerID, true))
	{
		eventData->SetHighlightedComponentForNavigation(InComp);
	}
}
USceneComponent* ULexEventSystem::GetHighlightedComponentForNavigation(int InPointerID)const
{
	if (auto eventData = GetPointerEventData(InPointerID, false))
	{
		return eventData->GetHighlightedComponentForNavigation();
	}
	return nullptr;
}

bool ULexEventSystem::SetPointerInputTypeByPointerID(int InPointerID, ELexUIPointerInputType InInputType)
{
	if (auto eventData = GetPointerEventData(InPointerID, false))
	{
		return SetPointerInputType(eventData, InInputType);
	}
	return false;
}
bool ULexEventSystem::SetPointerInputType(class ULexPointerEventData* InPointerEventData, ELexUIPointerInputType InInputType)
{
	if (InPointerEventData->inputType != InInputType)
	{
		InPointerEventData->inputType = InInputType;
		inputChangeDelegate.Broadcast(InPointerEventData->pointerID, InPointerEventData->inputType);
		return true;
	}
	return false;
}
void ULexEventSystem::ActivateNavigationInput(int InPointerID, USceneComponent* InDefaultHighlightedComponent)
{
	if (auto eventData = GetPointerEventData(InPointerID, false))
	{
		SetPointerInputType(eventData, ELexUIPointerInputType::Navigation);
		eventData->SetHighlightedComponentForNavigation(InDefaultHighlightedComponent);
	}
}
void ULexEventSystem::RegisterInputChangeEvent(const FLGUIPointerInputChange_Delegate& pointerInputChange)
{
	inputChangeDelegate.Add(pointerInputChange);
}
void ULexEventSystem::UnregisterInputChangeEvent(const FDelegateHandle& delegateHandle)
{
	inputChangeDelegate.Remove(delegateHandle);
}
FLGUIDelegateHandleWrapper ULexEventSystem::RegisterInputChangeEvent(const FLGUIPointerInputChange_DynamicDelegate& pointerInputChange)
{
	auto delegateHandle = inputChangeDelegate.AddLambda([pointerInputChange](int pointerID, ELexUIPointerInputType pointerInputType) {
		if (pointerInputChange.IsBound())pointerInputChange.Execute(pointerID, pointerInputType);
		});
	return FLGUIDelegateHandleWrapper(delegateHandle);
}
void ULexEventSystem::UnregisterInputChangeEvent(FLGUIDelegateHandleWrapper delegateHandle)
{
	inputChangeDelegate.Remove(delegateHandle.DelegateHandle);
}


void ULexEventSystem::SetSelectComponent(USceneComponent* InSelectComp, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType)
{
	if (eventData->SelectedComponent != InSelectComp)//select new object
	{
		auto oldSelectedComp = eventData->SelectedComponent;
		eventData->SelectedComponent = InSelectComp;
		if (IsValid(oldSelectedComp))
		{
			CallOnPointerDeselect(oldSelectedComp, eventData, eventFireType);
		}
		if (IsValid(eventData->SelectedComponent))
		{
			CallOnPointerSelect(eventData->SelectedComponent, eventData, eventFireType);
		}
		eventData->SelectedComponentEventFireType = eventFireType;
	}
}

void ULexEventSystem::SetSelectComponent(ULexEventSystem* InEventSystem, USceneComponent* InSelectComp, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType)
{
	if (InEventSystem != nullptr)
	{
		InEventSystem->SetSelectComponent(InSelectComp, eventData, eventFireType);
	}
	else
	{
		if (eventData->SelectedComponent != InSelectComp)//select new object
		{
			auto oldSelectedComp = eventData->SelectedComponent;
			eventData->SelectedComponent = InSelectComp;
			if (IsValid(oldSelectedComp))
			{
				ExecuteEvent_OnPointerDeselect(oldSelectedComp, eventData, eventFireType, false);
			}
			if (IsValid(eventData->SelectedComponent))
			{
				ExecuteEvent_OnPointerSelect(eventData->SelectedComponent, eventData, eventFireType, false);
			}
			eventData->SelectedComponentEventFireType = eventFireType;
		}
	}
}

USceneComponent* ULexEventSystem::GetCurrentSelectedComponent(int InPointerID)const
{
	if (auto eventData = GetPointerEventData(InPointerID, false))
	{
		return eventData->SelectedComponent;
	}
	return nullptr;
}

void ULexEventSystem::SetSelectComponentWithDefault(USceneComponent* InSelectComp)
{
	auto eventData = GetPointerEventData(0, true);
	SetSelectComponent(InSelectComp, eventData, eventData->pressComponentEventFireType);
}
ULexBaseInputModule* ULexEventSystem::GetCurrentInputModule()
{
	if (auto LGUIManagerActor = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		return LGUIManagerActor->GetCurrentInputModule().Get();
	}
	return nullptr;
}


void ULexEventSystem::LogEventData(ULexBaseEventData* inEventData)
{
#if WITH_EDITORONLY_DATA
	if (bOutputLog == false)return;
	UE_LOG(LGUI, Log, TEXT("%s"), *inEventData->ToString());
#endif
}


#define CALL_LGUIINTERFACE(component, inEventData, eventFireType, interface, function, allowBubble)\
{\
	inEventData->EventType = ELexUIPointerEventType::function;\
	bool eventAllowBubble = allowBubble;\
	switch(eventFireType)\
	{\
		case ELexUIEventFireType::OnlyTargetActor:\
		{\
			auto ownerActor = component->GetOwner(); \
			if (ownerActor->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
			{\
				if (ILexPointer##interface##Interface::Execute_OnPointer##function(ownerActor, inEventData) == false)\
				{\
					eventAllowBubble = false;\
				}\
			}\
		}\
		break;\
		case ELexUIEventFireType::OnlyTargetComponent:\
		{\
			if (component->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
			{\
				if (ILexPointer##interface##Interface::Execute_OnPointer##function(component, inEventData) == false)\
				{\
					eventAllowBubble = false;\
				}\
			}\
		}\
		break;\
		case ELexUIEventFireType::TargetActorAndAllItsComponents:\
		{\
			auto ownerActor = component->GetOwner(); \
			if (ownerActor->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
			{\
				if (ILexPointer##interface##Interface::Execute_OnPointer##function(ownerActor, inEventData) == false)\
				{\
					eventAllowBubble = false;\
				}\
			}\
			auto components = ownerActor->GetComponents();\
			for (auto item : components)\
			{\
				if (item->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
				{\
					if (ILexPointer##interface##Interface::Execute_OnPointer##function(item, inEventData) == false)\
					{\
						eventAllowBubble = false;\
					}\
				}\
			}\
		}\
		break;\
	}\
	if (eventAllowBubble)\
	{\
		if (auto parentActor = component->GetOwner()->GetAttachParentActor())\
		{\
			BubbleOnPointer##function(parentActor, inEventData);\
		}\
	}\
}

#define BUBBLE_LGUIINTERFACE(actor, inEventData, interface, function)\
{\
	bool eventAllowBubble = true;\
	if (actor->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
	{\
		if (ILexPointer##interface##Interface::Execute_OnPointer##function(actor, inEventData) == false)\
		{\
			eventAllowBubble = false;\
		}\
	}\
	auto components = actor->GetComponents();\
	for (auto item : components)\
	{\
		if (item->GetClass()->ImplementsInterface(ULexPointer##interface##Interface::StaticClass()))\
		{\
			if (ILexPointer##interface##Interface::Execute_OnPointer##function(item, inEventData) == false)\
			{\
				eventAllowBubble = false;\
			}\
		}\
	}\
	if (eventAllowBubble)\
	{\
		if (auto parentActor = actor->GetAttachParentActor())\
		{\
			BubbleOnPointer##function(parentActor, inEventData);\
		}\
	}\
}


#pragma region BubbleEvent
void ULexEventSystem::BubbleOnPointerEnter(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, EnterExit, Enter);
}
void ULexEventSystem::BubbleOnPointerExit(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, EnterExit, Exit);
}
void ULexEventSystem::BubbleOnPointerDown(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, DownUp, Down);
}
void ULexEventSystem::BubbleOnPointerUp(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, DownUp, Up);
}
void ULexEventSystem::BubbleOnPointerClick(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, Click, Click);
}
void ULexEventSystem::BubbleOnPointerBeginDrag(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, Drag, BeginDrag);
}
void ULexEventSystem::BubbleOnPointerDrag(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, Drag, Drag);
}
void ULexEventSystem::BubbleOnPointerEndDrag(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, Drag, EndDrag);
}

void ULexEventSystem::BubbleOnPointerScroll(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, Scroll, Scroll);
}

void ULexEventSystem::BubbleOnPointerDragDrop(AActor* actor, ULexPointerEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, DragDrop, DragDrop);
}

void ULexEventSystem::BubbleOnPointerSelect(AActor* actor, ULexBaseEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, SelectDeselect, Select);
}
void ULexEventSystem::BubbleOnPointerDeselect(AActor* actor, ULexBaseEventData* inEventData)
{
	BUBBLE_LGUIINTERFACE(actor, inEventData, SelectDeselect, Deselect);
}
#pragma endregion

#pragma region CallEvent
void ULexEventSystem::ExecuteEvent_OnPointerEnter(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, EnterExit, Enter, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerExit(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, EnterExit, Exit, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDown(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, DownUp, Down, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerUp(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, DownUp, Up, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerClick(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, Click, Click, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerBeginDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, Drag, BeginDrag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, Drag, Drag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerEndDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, Drag, EndDrag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerScroll(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, Scroll, Scroll, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDragDrop(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, PointerEventData, EventFireType, DragDrop, DragDrop, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerSelect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, EventData, EventFireType, SelectDeselect, Select, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDeselect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	CALL_LGUIINTERFACE(RootComponent, EventData, EventFireType, SelectDeselect, Deselect, AllowEventBubbleUp);
}


void ULexEventSystem::CallOnPointerEnter(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerEnter(component, inEventData, eventFireType, false);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerExit(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerExit(component, inEventData, eventFireType, false);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerDown(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerDown(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerUp(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerUp(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerClick(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerClick(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerBeginDrag(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerBeginDrag(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerDrag(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerDrag(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerEndDrag(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerEndDrag(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}

void ULexEventSystem::CallOnPointerScroll(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerScroll(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}

void ULexEventSystem::CallOnPointerDragDrop(USceneComponent* component, ULexPointerEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerDragDrop(component, inEventData, eventFireType, true);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}

void ULexEventSystem::CallOnPointerSelect(USceneComponent* component, ULexBaseEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerSelect(component, inEventData, eventFireType, false);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
void ULexEventSystem::CallOnPointerDeselect(USceneComponent* component, ULexBaseEventData* inEventData, ELexUIEventFireType eventFireType)
{
	LogEventData(inEventData);
	ExecuteEvent_OnPointerDeselect(component, inEventData, eventFireType, false);
	if (globalListenerPointerEvent.IsBound())
	{
		globalListenerPointerEvent.Broadcast(inEventData);
	}
}
#pragma endregion

#undef LOCTEXT_NAMESPACE