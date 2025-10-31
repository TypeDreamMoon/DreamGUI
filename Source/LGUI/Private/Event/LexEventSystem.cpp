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
#include "Event/LexPointerEventData.h"
#include "Event/InputModule/LexBaseInputModule.h"
#include "LGUI.h"

#define LOCTEXT_NAMESPACE "LGUIEventSystemActor"

ALexEventSystemActor::ALexEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	EventSystem = CreateDefaultSubobject<ULexEventSystem>(TEXT("EventSystem"));
}

DECLARE_CYCLE_STAT(TEXT("EventSystem"), STAT_EventSystem, STATGROUP_LGUI);

ULexEventSystem::ULexEventSystem()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}
ULexEventSystem* ULexEventSystem::GetLexEventSystemInstance(UObject* WorldContextObject, int UserIndex)
{
	auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return ULexUIManagerWorldSubsystem::GetInstance(World)->GetEventSystemByUserIndex(UserIndex);
}
void ULexEventSystem::BeginPlay()
{
	Super::BeginPlay();
	auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld());
	check(LexUIManager != nullptr);
	LexUIManager->AddEventSystem(this);
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
	if (CurrentInputModule.IsValid())
	{
		SCOPE_CYCLE_COUNTER(STAT_EventSystem);
		CurrentInputModule->ProcessInput();
	}
}

void ULexEventSystem::SetRaycastEnable(bool bEnable, bool bClearEvent)
{
	bRayEventEnable = bEnable;
	if (bRayEventEnable == false && bClearEvent)
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
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		LexUIManager->RemoveEventSystem(this);
	}
}

void ULexEventSystem::SetInputModule(ULexBaseInputModule* InputModule)
{
	CurrentInputModule = InputModule;
}

void ULexEventSystem::ClearInputModule()
{
	CurrentInputModule = nullptr;
}

void ULexEventSystem::ClearEvent()
{
	if (CurrentInputModule.IsValid())
	{
		CurrentInputModule->ClearEvent();
	}
}

ULexPointerEventData* ULexEventSystem::GetPointerEventData(int PointerID, bool bCreateIfNotExist)const
{
	if (auto foundPtr = PointerEventDataMap.Find(PointerID))
	{
		return *foundPtr;
	}
	auto newEventData = NewObject<ULexPointerEventData>(const_cast<ULexEventSystem*>(this));
	newEventData->PointerID = PointerID;
	newEventData->InputType = DefaultInputType;
	PointerEventDataMap.Add(PointerID, newEventData);
	return newEventData;
}
void ULexEventSystem::RemovePointerEventData(int PointerID)
{
	PointerEventDataMap.Remove(PointerID);
}

void ULexEventSystem::RaiseHitEvent(bool bHitOrNot, const FHitResult& HitResult, USceneComponent* HitComponent)
{
	if (bRayEventEnable)
	{
		RaycastHitEvent.Broadcast(bHitOrNot, HitResult, HitComponent);
		RaycastHitEventBP.Broadcast(bHitOrNot, HitResult, HitComponent);
	}
}
bool ULexEventSystem::IsPointerOverUIByPointerID(int PointerID)
{
	if (auto foundPtr = PointerEventDataMap.Find(PointerID))
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
bool ULexEventSystem::SetPointerInputType(ULexPointerEventData* InPointerEventData, ELexUIPointerInputType InInputType)
{
	if (InPointerEventData->InputType != InInputType)
	{
		InPointerEventData->InputType = InInputType;
		PointerInputTypedChangedEvent.Broadcast(InPointerEventData->PointerID, InPointerEventData->InputType);
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
	SetSelectComponent(InSelectComp, eventData, eventData->PressComponentEventFireType);
}

void ULexEventSystem::LogEventData(ULexBaseEventData* inEventData)
{
#if WITH_EDITORONLY_DATA
	if (bOutputLog == false)return;
	UE_LOG(LGUI, Log, TEXT("%s"), *inEventData->ToString());
#endif
}

#pragma region CallEvent
void ULexEventSystem::ExecuteEvent_OnPointerEnter(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Enter;
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerEnterExitInterface::StaticClass(),
		ILexPointerEnterExitInterface::Execute_OnPointerEnter, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerExit(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Exit; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerEnterExitInterface::StaticClass(),
		ILexPointerEnterExitInterface::Execute_OnPointerExit, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDown(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Down; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDownUpInterface::StaticClass(),
		ILexPointerDownUpInterface::Execute_OnPointerDown, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerUp(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Up; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDownUpInterface::StaticClass(),
		ILexPointerDownUpInterface::Execute_OnPointerUp, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerClick(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Click; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerClickInterface::StaticClass(),
		ILexPointerClickInterface::Execute_OnPointerClick, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerBeginDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::BeginDrag; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDragInterface::StaticClass(),
		ILexPointerDragInterface::Execute_OnPointerBeginDrag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Drag; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDragInterface::StaticClass(),
		ILexPointerDragInterface::Execute_OnPointerDrag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerEndDrag(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::EndDrag; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDragInterface::StaticClass(),
		ILexPointerDragInterface::Execute_OnPointerEndDrag, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerScroll(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::Scroll; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerScrollInterface::StaticClass(),
		ILexPointerScrollInterface::Execute_OnPointerScroll, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDragDrop(USceneComponent* RootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = ELexUIPointerEventType::DragDrop; 
	ExecuteLexUIInterface(RootComponent,
		PointerEventData, EventFireType,
		ULexPointerDragDropInterface::StaticClass(),
		ILexPointerDragDropInterface::Execute_OnPointerDragDrop, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerSelect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	EventData->EventType = ELexUIPointerEventType::Select; 
	ExecuteLexUIInterface(RootComponent,
		EventData, EventFireType,
		ULexPointerSelectDeselectInterface::StaticClass(),
		ILexPointerSelectDeselectInterface::Execute_OnPointerSelect, AllowEventBubbleUp);
}
void ULexEventSystem::ExecuteEvent_OnPointerDeselect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp)
{
	EventData->EventType = ELexUIPointerEventType::Deselect; 
	ExecuteLexUIInterface(RootComponent,
		EventData, EventFireType,
		ULexPointerSelectDeselectInterface::StaticClass(),
		ILexPointerSelectDeselectInterface::Execute_OnPointerDeselect, AllowEventBubbleUp);
}


void ULexEventSystem::CallOnPointerEnter(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerEnter(RootComponent, EventData, EventFireType, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerExit(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerExit(RootComponent, EventData, EventFireType, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerDown(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDown(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerUp(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerUp(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerClick(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerClick(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerBeginDrag(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerBeginDrag(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerDrag(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDrag(RootComponent, EventData, EventFireType, true);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerEndDrag(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerEndDrag(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void ULexEventSystem::CallOnPointerScroll(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerScroll(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void ULexEventSystem::CallOnPointerDragDrop(USceneComponent* RootComponent, ULexPointerEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDragDrop(RootComponent, EventData, EventFireType, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void ULexEventSystem::CallOnPointerSelect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerSelect(RootComponent, EventData, EventFireType, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void ULexEventSystem::CallOnPointerDeselect(USceneComponent* RootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDeselect(RootComponent, EventData, EventFireType, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
#pragma endregion

#undef LOCTEXT_NAMESPACE