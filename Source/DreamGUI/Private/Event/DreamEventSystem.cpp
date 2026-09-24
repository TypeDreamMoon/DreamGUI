// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamEventSystem.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "Event/Interface/DreamPointerLongPressInterface.h"
#include "Event/Interface/DreamPointerGestureInterface.h"
#include "Event/DreamGestureEventData.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

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
	RegisteredManager = DreamUIManager;
	DreamUIManager->AddEventSystem(this);
}

void UDreamEventSystem::UnregisterFromManager()
{
	// Remembered at registration rather than looked up again. By BeginDestroy GetWorld() is routinely
	// null -- the component has already been detached from its actor -- so the lookup that used to
	// stand in for unregistering simply did nothing, and a level reload left a dead event system in the
	// manager's map. The next level's system was then refused as a duplicate and its UI never
	// responded, which is the "reload the level and the UI is deaf" report.
	if (UDreamUIManagerWorldSubsystem* Manager = RegisteredManager.Get())
	{
		Manager->RemoveEventSystem(this);
	}
	RegisteredManager.Reset();
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
	//the symmetric half of BeginPlay, and the only one that runs while the world is still whole
	UnregisterFromManager();
	Super::EndPlay(EndPlayReason);
}
void UDreamEventSystem::BeginDestroy()
{
	//a backstop for a component destroyed without an EndPlay; a second call is a no-op
	UnregisterFromManager();
	Super::BeginDestroy();
}

void UDreamEventSystem::SetUserIndex(int Value)
{
	if (UserIndex == Value)return;
	// The manager's map is keyed by user index, so the entry has to move with the field. Writing the
	// field alone would leave this event system registered under the player it used to serve and
	// unfindable under the one it now serves -- which is the whole point of the index.
	//
	// The manager is looked up rather than only read from RegisteredManager, because that back-pointer
	// is set in BeginPlay and this is legitimately called before it: the preset actor points its event
	// system at the right player as soon as it knows which one that is.
	UDreamUIManagerWorldSubsystem* Manager = RegisteredManager.IsValid()
		? RegisteredManager.Get()
		: UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	//"registered" means the map really holds THIS one under the old index, not merely that a map exists
	const bool bWasRegistered = Manager != nullptr && Manager->GetEventSystemByUserIndex(UserIndex) == this;
	if (bWasRegistered)
	{
		Manager->RemoveEventSystem(this);
	}
	UserIndex = Value;
	// Pointers born under the old index carry it; they belong to the player who was using them, and
	// nothing about that pointer is true for the new player.
	PointerEventDataMap.Reset();
	if (bWasRegistered)
	{
		Manager->AddEventSystem(this);
		RegisteredManager = Manager;
	}
}

ULocalPlayer* UDreamEventSystem::GetLocalPlayerForUser(const UObject* WorldContextObject, int InUserIndex)
{
	if (!IsValid(WorldContextObject))return nullptr;
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)return nullptr;
	const UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetLocalPlayerByIndex(InUserIndex) : nullptr;
}

APlayerController* UDreamEventSystem::GetPlayerControllerForUser(const UObject* WorldContextObject, int InUserIndex)
{
	if (!IsValid(WorldContextObject))return nullptr;
	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)return nullptr;
	if (ULocalPlayer* LocalPlayer = GetLocalPlayerForUser(WorldContextObject, InUserIndex))
	{
		return LocalPlayer->GetPlayerController(World);
	}
	// No local player at that index. Player 0 is the honest answer only for the default index; for any
	// other, "nobody" beats writing a second player's cursor or reading their sticks.
	return InUserIndex == 0 ? World->GetFirstPlayerController() : nullptr;
}

APlayerController* UDreamEventSystem::GetPlayerController()const
{
	return GetPlayerControllerForUser(this, UserIndex);
}

double UDreamEventSystem::GetPointerClockSeconds(const UObject* WorldContextObject)
{
	// UWorld::GetWorld answers itself, so a world passed in directly works as well as anything in it.
	const UWorld* World = DreamUI::GetWorldSafe(WorldContextObject);
	return World != nullptr ? World->GetRealTimeSeconds() : 0.0;
}

void UDreamEventSystem::ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor)
{
	if (!bApplyHoverCursor)return;
	APlayerController* PlayerController = GetPlayerController();
	if (PlayerController == nullptr)return;

	if (bWidgetClaimedCursor)
	{
		if (!bHoverCursorOverrideActive)
		{
			// Taken once, at the moment the UI first takes the cursor over, so that whatever the game
			// was showing is what comes back -- not EMouseCursor::Default, which is the plugin's idea
			// of neutral and not the project's.
			bHoverCursorOverrideActive = true;
			CursorBeforeHoverOverride = PlayerController->CurrentMouseCursor;
		}
		PlayerController->CurrentMouseCursor = InCursor;
	}
	else if (bHoverCursorOverrideActive)
	{
		bHoverCursorOverrideActive = false;
		PlayerController->CurrentMouseCursor = CursorBeforeHoverOverride;
	}
}

void UDreamEventSystem::SetApplyHoverCursor(bool Value)
{
	if (bApplyHoverCursor == Value)return;
	bApplyHoverCursor = Value;
	if (!bApplyHoverCursor && bHoverCursorOverrideActive)
	{
		// Turning it off gives the cursor back rather than leaving whatever the last hover asked for
		// frozen on screen.
		bHoverCursorOverrideActive = false;
		if (APlayerController* PlayerController = GetPlayerController())
		{
			PlayerController->CurrentMouseCursor = CursorBeforeHoverOverride;
		}
	}
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
	if (!bCreateIfNotExist)
	{
		// Merely asking about a pointer must not mint one: every entry of this map is raycast every
		// frame by the input module, so a query for an id that was never pressed used to cost a
		// permanent trace at a position nobody is pointing at.
		return nullptr;
	}
	auto newEventData = NewObject<UDreamPointerEventData>(const_cast<UDreamEventSystem*>(this));
	newEventData->PointerID = PointerID;
	// Stamped here because this is the only place a pointer is born, and it is the only place that
	// knows whose it is. Handlers read EventData->UserIndex to find their own event system back; while
	// nothing wrote it, every one of them resolved to player 0.
	newEventData->UserIndex = UserIndex;
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
	// Picking up a pad is the only moment the model can have changed, and it is rare. Done before the
	// device change goes out so that a prompt bar rebuilding from it already sees the right glyphs.
	if (InDevice == EDreamUIInputDevice::Gamepad)
	{
		RefreshGamepadModel();
	}
	InputDeviceChangedEvent.Broadcast(InDevice);
	InputDeviceChangedEventBP.Broadcast(InDevice);
	return true;
}

EDreamUIGamepadModel UDreamEventSystem::GetGamepadModelForDeviceName(FName InInputDeviceName, FName InHardwareDeviceIdentifier)
{
	// Matched on substrings of both names because platforms disagree about which one carries the brand,
	// and because the exact spellings differ by platform SDK version -- "SonyController",
	// "DualSense", "PS5_Controller" all mean the same thing to a prompt.
	const FString Names = InInputDeviceName.ToString() + TEXT(" ") + InHardwareDeviceIdentifier.ToString();
	auto Contains = [&Names](const TCHAR* InToken)
	{
		return Names.Contains(InToken, ESearchCase::IgnoreCase);
	};
	if (Contains(TEXT("Sony")) || Contains(TEXT("DualSense")) || Contains(TEXT("DualShock"))
		|| Contains(TEXT("PlayStation")) || Contains(TEXT("PS4")) || Contains(TEXT("PS5")))
	{
		return EDreamUIGamepadModel::PlayStation;
	}
	if (Contains(TEXT("Switch")) || Contains(TEXT("Nintendo")) || Contains(TEXT("Npad")) || Contains(TEXT("JoyCon")))
	{
		return EDreamUIGamepadModel::Switch;
	}
	if (Contains(TEXT("XInput")) || Contains(TEXT("Xbox")) || Contains(TEXT("GDK")) || Contains(TEXT("Durango")))
	{
		return EDreamUIGamepadModel::Xbox;
	}
	// Named by nobody, or named something this does not recognize. Generic is the honest answer and
	// every icon lookup falls back to the plain gamepad glyph, which is never wrong-brand.
	return EDreamUIGamepadModel::Generic;
}

bool UDreamEventSystem::RefreshGamepadModel()
{
	if (bGamepadModelOverridden)return false;//the project has told us; the platform does not get a vote

	EDreamUIGamepadModel DetectedModel = EDreamUIGamepadModel::Generic;
	if (const ULocalPlayer* LocalPlayer = GetLocalPlayerForUser(this, UserIndex))
	{
		// The registry rather than FInputDeviceScope: the scope is deprecated in 5.8, is only valid
		// inside the platform's own dispatch call, and we are asking from a UI frame.
		const FInputDeviceId DeviceId = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(LocalPlayer->GetPlatformUserId());
		if (const TOptional<FInputDeviceDescriptor> Descriptor = FInputDeviceRegistry::FindDescriptor(DeviceId))
		{
			DetectedModel = GetGamepadModelForDeviceName(Descriptor->InputDeviceName, Descriptor->HardwareDeviceIdentifier);
		}
	}
	if (CurrentGamepadModel == DetectedModel)return false;
	CurrentGamepadModel = DetectedModel;
	GamepadModelChangedEvent.Broadcast(CurrentGamepadModel);
	GamepadModelChangedEventBP.Broadcast(CurrentGamepadModel);
	return true;
}

void UDreamEventSystem::SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel)
{
	bGamepadModelOverridden = bInOverride;
	if (!bInOverride)
	{
		RefreshGamepadModel();//back to whatever the platform says, right now rather than at the next press
		return;
	}
	if (CurrentGamepadModel == InModel)return;
	CurrentGamepadModel = InModel;
	GamepadModelChangedEvent.Broadcast(CurrentGamepadModel);
	GamepadModelChangedEventBP.Broadcast(CurrentGamepadModel);
}

bool UDreamEventSystem::SetPointerInputTypeByPointerID(int InPointerID, EDreamUIPointerInputType InInputType)
{
	//a setter, not a query: the caller is declaring how that pointer behaves, so it is theirs to create
	if (auto EventData = GetPointerEventData(InPointerID, true))
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
	//activation is a command: gamepad navigation must work before that pointer has ever been pressed
	if (auto EventData = GetPointerEventData(InPointerID, true))
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
void UDreamEventSystem::ExecuteEvent_OnPointerDoubleClick(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::DoubleClick;
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerDoubleClickInterface::StaticClass(),
		IDreamPointerDoubleClickInterface::Execute_OnPointerDoubleClick, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerLongPress(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp)
{
	PointerEventData->EventType = EDreamUIPointerEventType::LongPress;
	ExecuteDreamUIInterface(TargetWidget,
		PointerEventData,
		UDreamPointerLongPressInterface::StaticClass(),
		IDreamPointerLongPressInterface::Execute_OnPointerLongPress, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerPinch(UDreamWidget* TargetWidget, UDreamGestureEventData* GestureEventData, bool AllowEventBubbleUp)
{
	GestureEventData->EventType = EDreamUIPointerEventType::Pinch;
	ExecuteDreamUIInterface(TargetWidget,
		GestureEventData,
		UDreamPointerGestureInterface::StaticClass(),
		IDreamPointerGestureInterface::Execute_OnPointerPinch, AllowEventBubbleUp);
}
void UDreamEventSystem::ExecuteEvent_OnPointerSwipe(UDreamWidget* TargetWidget, UDreamGestureEventData* GestureEventData, bool AllowEventBubbleUp)
{
	GestureEventData->EventType = EDreamUIPointerEventType::Swipe;
	ExecuteDreamUIInterface(TargetWidget,
		GestureEventData,
		UDreamPointerGestureInterface::StaticClass(),
		IDreamPointerGestureInterface::Execute_OnPointerSwipe, AllowEventBubbleUp);
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
void UDreamEventSystem::CallOnPointerDoubleClick(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerDoubleClick(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerLongPress(UDreamWidget* TargetWidget, UDreamPointerEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerLongPress(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerPinch(UDreamWidget* TargetWidget, UDreamGestureEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerPinch(TargetWidget, EventData, true);
	InputEvent.Broadcast(EventData);
	InputEventBP.Broadcast(EventData);
}
void UDreamEventSystem::CallOnPointerSwipe(UDreamWidget* TargetWidget, UDreamGestureEventData* EventData)
{
	LogEventData(EventData);
	ExecuteEvent_OnPointerSwipe(TargetWidget, EventData, true);
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
