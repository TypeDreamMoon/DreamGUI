// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamEventSystem.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Core/DreamUIManager.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"

#define LOCTEXT_NAMESPACE "DreamGUIEventSystemActor"

ADreamEventSystemActor::ADreamEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	EventSystem = CreateDefaultSubobject<UDreamEventSystem>(TEXT("EventSystem"));
}

DECLARE_CYCLE_STAT(TEXT("EventSystem"), STAT_EventSystem, STATGROUP_DreamGUI);

UDreamEventSystem::UDreamEventSystem()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}
UDreamEventSystem* UDreamEventSystem::GetDreamEventSystemInstance(UObject* WorldContextObject, int UserIndex)
{
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			return DreamUIManager->GetEventSystemByUserIndex(UserIndex);
		}
	}
	return nullptr;
}
void UDreamEventSystem::BeginPlay()
{
	Super::BeginPlay();
	auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld());
	check(DreamUIManager != nullptr);
	DreamUIManager->AddEventSystem(this);
}

void UDreamEventSystem::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bRayEventEnable)
	{
		ProcessInputEvent();
	}
}

void UDreamEventSystem::ProcessInputEvent()
{
	if (CurrentInputModule.IsValid())
	{
		SCOPE_CYCLE_COUNTER(STAT_EventSystem);
		CurrentInputModule->ProcessInput();
	}
}

void UDreamEventSystem::SetRaycastEnable(bool bEnable, bool bClearEvent)
{
	bRayEventEnable = bEnable;
	if (bRayEventEnable == false && bClearEvent)
	{
		ClearEvent();
	}
}

void UDreamEventSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
void UDreamEventSystem::BeginDestroy()
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->RemoveEventSystem(this);
	}
	Super::BeginDestroy();
}

void UDreamEventSystem::SetInputModule(UDreamBaseInputModule* InputModule)
{
	CurrentInputModule = InputModule;
}

void UDreamEventSystem::ClearInputModule()
{
	CurrentInputModule = nullptr;
}

void UDreamEventSystem::ClearEvent()
{
	if (CurrentInputModule.IsValid())
	{
		CurrentInputModule->ClearEvent();
	}
}

UDreamPointerEventData* UDreamEventSystem::GetPointerEventData(int PointerID, bool bCreateIfNotExist)const
{
	if (auto foundPtr = PointerEventDataMap.Find(PointerID))
	{
		return *foundPtr;
	}
	auto newEventData = NewObject<UDreamPointerEventData>(const_cast<UDreamEventSystem*>(this));
	newEventData->PointerID = PointerID;
	newEventData->InputType = DefaultInputType;
	PointerEventDataMap.Add(PointerID, newEventData);
	return newEventData;
}
void UDreamEventSystem::RemovePointerEventData(int PointerID)
{
	PointerEventDataMap.Remove(PointerID);
}

void UDreamEventSystem::RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent)
{
	if (bRayEventEnable)
	{
		RaycastHitEvent.Broadcast(bHitOrNot, HitResult, HitComponent);
		RaycastHitEventBP.Broadcast(bHitOrNot, HitResult, HitComponent);
	}
}
bool UDreamEventSystem::IsPointerOverUIByPointerID(int PointerID)
{
	if (auto foundPtr = PointerEventDataMap.Find(PointerID))
	{
		return (*foundPtr)->IsPointerOverUI();
	}
	return false;
}

void UDreamEventSystem::SetHighlightedComponentForNavigation(UDreamWidget* InComp, int InPointerID)
{
	if (auto EventData = GetPointerEventData(InPointerID, true))
	{
		EventData->SetHighlightedWidgetForNavigation(InComp);
	}
}
UDreamWidget* UDreamEventSystem::GetHighlightedComponentForNavigation(int InPointerID)const
{
	if (auto EventData = GetPointerEventData(InPointerID, false))
	{
		return EventData->GetHighlightedComponentForNavigation();
	}
	return nullptr;
}

EDreamUIInputDevice UDreamEventSystem::GetInputDeviceForKey(const FKey& InKey)
{
	if (InKey.IsTouch())
	{
		return EDreamUIInputDevice::Touch;
	}
	// A mouse key is neither gamepad nor touch, so it lands with the keyboard -- which is what a prompt
	// wants anyway: the pair is one device as far as the player's hands are concerned.
	return InKey.IsGamepadKey() ? EDreamUIInputDevice::Gamepad : EDreamUIInputDevice::MouseAndKeyboard;
}

bool UDreamEventSystem::ReportInputDevice(EDreamUIInputDevice InDevice)
{
	if (CurrentInputDevice == InDevice)
	{
		return false;//every key comes through here; broadcasting each one would rebuild prompts per frame
	}
	CurrentInputDevice = InDevice;
	InputDeviceChangedEvent.Broadcast(InDevice);
	InputDeviceChangedEventBP.Broadcast(InDevice);
	return true;
}

bool UDreamEventSystem::SetPointerInputTypeByPointerID(int InPointerID, EDreamUIPointerInputType InInputType)
{
	if (auto EventData = GetPointerEventData(InPointerID, false))
	{
		return SetPointerInputType(EventData, InInputType);
	}
	return false;
}
bool UDreamEventSystem::SetPointerInputType(UDreamPointerEventData* InPointerEventData, EDreamUIPointerInputType InInputType)
{
	if (InPointerEventData->InputType != InInputType)
	{
		InPointerEventData->InputType = InInputType;
		PointerInputTypedChangedEvent.Broadcast(InPointerEventData->PointerID, InPointerEventData->InputType);
		return true;
	}
	return false;
}
void UDreamEventSystem::ActivateNavigationInput(int InPointerID, UDreamWidget* InDefaultHighlightedComponent)
{
	if (auto EventData = GetPointerEventData(InPointerID, false))
	{
		SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
		EventData->SetHighlightedWidgetForNavigation(InDefaultHighlightedComponent);
	}
}

void UDreamEventSystem::SetSelectWidget(UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData)
{
	if (EventData->SelectedComponent != InSelectWidget)//select new object
	{
		auto oldSelectedComp = EventData->SelectedComponent;
		EventData->SelectedComponent = InSelectWidget;
		if (IsValid(oldSelectedComp))
		{
			CallOnPointerDeselect(oldSelectedComp, EventData);
			const int32 PointerId = Cast<UDreamPointerEventData>(EventData) ? CastChecked<UDreamPointerEventData>(EventData)->PointerID : 0;
			oldSelectedComp->NotifyFocusLost(UserIndex, PointerId);
		}
		if (IsValid(EventData->SelectedComponent))
		{
			CallOnPointerSelect(EventData->SelectedComponent, EventData);
			const int32 PointerId = Cast<UDreamPointerEventData>(EventData) ? CastChecked<UDreamPointerEventData>(EventData)->PointerID : 0;
			EventData->SelectedComponent->NotifyFocusReceived(UserIndex, PointerId);
		}
	}
}

void UDreamEventSystem::SetSelectWidget(UDreamEventSystem* InEventSystem, UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData)
{
	if (InEventSystem != nullptr)
	{
		InEventSystem->SetSelectWidget(InSelectWidget, EventData);
	}
	else
	{
		if (EventData->SelectedComponent != InSelectWidget)//select new object
		{
			auto oldSelectedComp = EventData->SelectedComponent;
			EventData->SelectedComponent = InSelectWidget;
			if (IsValid(oldSelectedComp))
			{
				ExecuteEvent_OnPointerDeselect(oldSelectedComp, EventData, false);
				const int32 PointerId = Cast<UDreamPointerEventData>(EventData) ? CastChecked<UDreamPointerEventData>(EventData)->PointerID : 0;
				oldSelectedComp->NotifyFocusLost(0, PointerId);
			}
			if (IsValid(EventData->SelectedComponent))
			{
				ExecuteEvent_OnPointerSelect(EventData->SelectedComponent, EventData, false);
				const int32 PointerId = Cast<UDreamPointerEventData>(EventData) ? CastChecked<UDreamPointerEventData>(EventData)->PointerID : 0;
				EventData->SelectedComponent->NotifyFocusReceived(0, PointerId);
			}
		}
	}
}

UDreamWidget* UDreamEventSystem::GetCurrentSelectedComponent(int InPointerID)const
{
	if (auto EventData = GetPointerEventData(InPointerID, false))
	{
		return EventData->SelectedComponent;
	}
	return nullptr;
}

void UDreamEventSystem::SetSelectComponentWithDefault(UDreamWidget* InSelectWidget)
{
	auto EventData = GetPointerEventData(0, true);
	SetSelectWidget(InSelectWidget, EventData);
}

void UDreamEventSystem::LogEventData(UDreamBaseEventData* inEventData)
{
#if WITH_EDITORONLY_DATA
	if (bOutputLog == false)return;
	UE_LOG(DreamGUI, Log, TEXT("%s"), *inEventData->ToString());
#endif
}

#pragma region CallEvent
void UDreamEventSystem::ExecuteEvent_OnPointerEnter(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Enter;
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerEnterExitInterface::StaticClass(),
		IDreamPointerEnterExitInterface::Execute_OnPointerEnter, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerExit(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Exit; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerEnterExitInterface::StaticClass(),
		IDreamPointerEnterExitInterface::Execute_OnPointerExit, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerDown(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Down; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDownUpInterface::StaticClass(),
		IDreamPointerDownUpInterface::Execute_OnPointerDown, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerUp(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Up; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDownUpInterface::StaticClass(),
		IDreamPointerDownUpInterface::Execute_OnPointerUp, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerClick(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Click; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerClickInterface::StaticClass(),
		IDreamPointerClickInterface::Execute_OnPointerClick, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerBeginDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::BeginDrag; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDragInterface::StaticClass(),
		IDreamPointerDragInterface::Execute_OnPointerBeginDrag, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Drag; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDragInterface::StaticClass(),
		IDreamPointerDragInterface::Execute_OnPointerDrag, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerEndDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::EndDrag; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDragInterface::StaticClass(),
		IDreamPointerDragInterface::Execute_OnPointerEndDrag, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerScroll(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::Scroll; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerScrollInterface::StaticClass(),
		IDreamPointerScrollInterface::Execute_OnPointerScroll, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerDragDrop(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::DragDrop; 
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDragDropInterface::StaticClass(),
		IDreamPointerDragDropInterface::Execute_OnPointerDragDrop, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerSelect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData, bool AllowEventBubbleUp)
{
	EventData->EventType = EDreamUIPointerEventType::Select; 
	ExecuteDreamUIInterface(TargetWidget,
		EventData,
		UDreamPointerSelectDeselectInterface::StaticClass(),
		IDreamPointerSelectDeselectInterface::Execute_OnPointerSelect, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerDeselect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData, bool AllowEventBubbleUp)
{
	EventData->EventType = EDreamUIPointerEventType::Deselect; 
	ExecuteDreamUIInterface(TargetWidget,
		EventData,
		UDreamPointerSelectDeselectInterface::StaticClass(),
		IDreamPointerSelectDeselectInterface::Execute_OnPointerDeselect, AllowEventBubbleUp);
}


void UDreamEventSystem::CallOnPointerEnter(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerEnter(TargetWidget, EventData, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerExit(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerExit(TargetWidget, EventData, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerDown(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDown(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerUp(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerUp(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerClick(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerClick(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerBeginDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerBeginDrag(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDrag(TargetWidget, EventData, true);
	// Both, like every sibling Call*. This one alone skipped the native broadcast, so a C++
	// observer -- a drag visual following the cursor, a drop-target highlighter -- silently never
	// saw drag frames while the Blueprint one did.
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerEndDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerEndDrag(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void UDreamEventSystem::CallOnPointerScroll(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerScroll(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void UDreamEventSystem::CallOnPointerDragDrop(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDragDrop(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}

void UDreamEventSystem::CallOnPointerSelect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerSelect(TargetWidget, EventData, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerDeselect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDeselect(TargetWidget, EventData, false);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
#pragma endregion

#undef LOCTEXT_NAMESPACE
