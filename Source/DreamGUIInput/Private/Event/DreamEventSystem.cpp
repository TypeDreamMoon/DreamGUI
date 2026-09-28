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
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Core/DreamUIWorldContext.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "DreamGUIEventSystemActor"

ADreamEventSystemActor::ADreamEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	EventSystem = CreateDefaultSubobject<UDreamEventSystem>(TEXT("EventSystem"));
}

UDreamEventSystem::UDreamEventSystem()
{
	// The input subsystem's tick function runs every player's frame; this component has nothing of its own to tick.
	PrimaryComponentTick.bCanEverTick = false;
}

UDreamEventSystem* UDreamEventSystem::GetDreamEventSystemInstance(UObject* WorldContextObject, int UserIndex)
{
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UDreamUIInputSubsystem* InputSubsystem = World->GetSubsystem<UDreamUIInputSubsystem>())
		{
			if (UDreamEventSystem* Placed = InputSubsystem->GetEventSystemByUserIndex(UserIndex))
			{
				return Placed;
			}
			// No event system placed for that player: one no actor carries speaks for the player all the same, so a
			// Blueprint does not have to spawn an actor to read the player's pointers or hear their events.
			return InputSubsystem->GetOrCreateImplicitEventSystem(UserIndex);
		}
	}
	return nullptr;
}

void UDreamEventSystem::BeginPlay()
{
	Super::BeginPlay();
	UDreamUIInputSubsystem* InputSubsystem = UDreamUIInputSubsystem::Get(this);
	if (!ensureMsgf(InputSubsystem != nullptr, TEXT("%s: began play in a world without DreamUI input; it will route nothing."), *GetPathName()))
	{
		return;
	}
	RegisteredInputSubsystem = InputSubsystem;
	InputSubsystem->AddEventSystem(this);
}

void UDreamEventSystem::UnregisterFromInputSubsystem()
{
	// Remembered at registration rather than looked up again. By BeginDestroy GetWorld() is routinely null, and a
	// level reload used to leave a dead event system in the registry and the next level's UI deaf.
	if (UDreamUIInputSubsystem* InputSubsystem = RegisteredInputSubsystem.Get())
	{
		InputSubsystem->RemoveEventSystem(this);
	}
	RegisteredInputSubsystem.Reset();
	UnbindFromUser();
}

void UDreamEventSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The symmetric half of BeginPlay, and the only one that runs while the world is still whole: the player lets
	// go of every pointer here, with a world for the Exits and Ups to be dispatched into.
	UnregisterFromInputSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UDreamEventSystem::BeginDestroy()
{
	// A backstop for a component destroyed without an EndPlay. Nothing is dispatched from the collector: the
	// registration is dropped and that is all.
	if (UDreamUIInputSubsystem* InputSubsystem = RegisteredInputSubsystem.Get())
	{
		InputSubsystem->ForgetEventSystem(this);
	}
	RegisteredInputSubsystem.Reset();
	if (UDreamUIInputUser* User = BoundUser.Get())
	{
		User->RemoveEventSystemFacade(this);
		User->ClearInputModule(CurrentInputModule.Get());
	}
	BoundUser.Reset();
	Super::BeginDestroy();
}

#pragma region Player
UDreamUIInputUser* UDreamEventSystem::GetInputUser() const
{
	UDreamUIInputUser* User = BoundUser.Get();
	if (User != nullptr && User->GetUserIndex() == UserIndex && !User->IsShutDown())
	{
		return User;
	}
	if (bIsImplicit)
	{
		return nullptr;//its player is gone, and an implicit event system is only ever its own player's
	}
	UDreamUIInputSubsystem* InputSubsystem = RegisteredInputSubsystem.IsValid()
		? RegisteredInputSubsystem.Get()
		: UDreamUIInputSubsystem::Get(this);
	User = InputSubsystem != nullptr ? InputSubsystem->GetOrCreateUser(UserIndex) : nullptr;
	if (User != nullptr)
	{
		const_cast<UDreamEventSystem*>(this)->BindToUser(User);
	}
	return User;
}

void UDreamEventSystem::BindToUser(UDreamUIInputUser* InUser)
{
	if (BoundUser.Get() == InUser)
	{
		return;
	}
	UnbindFromUser();
	BoundUser = InUser;
	if (InUser != nullptr)
	{
		InUser->AddEventSystemFacade(this);
		// A module registered through this event system before it had a player is the player's now.
		if (UDreamBaseInputModule* Module = CurrentInputModule.Get())
		{
			InUser->SetInputModule(Module);
		}
	}
}

void UDreamEventSystem::UnbindFromUser()
{
	UDreamUIInputUser* User = BoundUser.Get();
	BoundUser.Reset();
	if (User != nullptr)
	{
		User->RemoveEventSystemFacade(this);
		User->ClearInputModule(CurrentInputModule.Get());
	}
}

void UDreamEventSystem::WriteSettingsToUser(UDreamUIInputUser* InUser) const
{
	if (InUser == nullptr)
	{
		return;
	}
	FDreamUIInputUserConfig& Config = InUser->GetConfig();
	Config.bRayEventEnable = bRayEventEnable;
	Config.bApplyHoverCursor = bApplyHoverCursor;
	Config.DefaultInputType = DefaultInputType;
	Config.NavigateInputIntervalForFirstTime = FMath::Max(NavigateInputIntervalForFirstTime, MinNavigateInputInterval);
	Config.NavigateInputInterval = FMath::Max(NavigateInputInterval, MinNavigateInputInterval);
	Config.DoubleClickTime = FMath::Max(DoubleClickTime, 0.0f);
	Config.LongPressTime = FMath::Max(LongPressTime, 0.0f);
	Config.SwipeMinDistance = FMath::Max(SwipeMinDistance, 0.0f);
	Config.SwipeMaxDuration = FMath::Max(SwipeMaxDuration, 0.0f);
	Config.PinchMinDistanceChange = FMath::Max(PinchMinDistanceChange, 0.0f);
	Config.bScrollNavigationTargetIntoView = bScrollNavigationTargetIntoView;
	Config.bAnimateNavigationScroll = bAnimateNavigationScroll;
#if WITH_EDITORONLY_DATA
	Config.bOutputLog = bOutputLog;
#endif
}

void UDreamEventSystem::InitializeImplicit(UDreamUIInputSubsystem* InSubsystem, UDreamUIInputUser* InUser)
{
	bIsImplicit = true;
	RegisteredInputSubsystem = InSubsystem;
	UserIndex = InUser != nullptr ? InUser->GetUserIndex() : 0;
	BindToUser(InUser);
	// Its settings are the player's, not the other way round: an implicit event system reads what the player has.
	if (InUser != nullptr)
	{
		const FDreamUIInputUserConfig& Config = InUser->GetConfig();
		bRayEventEnable = Config.bRayEventEnable;
		bApplyHoverCursor = Config.bApplyHoverCursor;
		DefaultInputType = Config.DefaultInputType;
		NavigateInputIntervalForFirstTime = Config.NavigateInputIntervalForFirstTime;
		NavigateInputInterval = Config.NavigateInputInterval;
		DoubleClickTime = Config.DoubleClickTime;
		LongPressTime = Config.LongPressTime;
		SwipeMinDistance = Config.SwipeMinDistance;
		SwipeMaxDuration = Config.SwipeMaxDuration;
		PinchMinDistanceChange = Config.PinchMinDistanceChange;
		bScrollNavigationTargetIntoView = Config.bScrollNavigationTargetIntoView;
		bAnimateNavigationScroll = Config.bAnimateNavigationScroll;
	}
}

void UDreamEventSystem::SetUserIndex(int Value)
{
	if (UserIndex == Value)return;
	// Moving to another player lets the old one go of every pointer, which dispatches: from inside a handler it
	// waits until the event being dispatched is over.
	if (UDreamUIInputUser* OldUser = BoundUser.Get(); OldUser != nullptr && OldUser->IsDispatching())
	{
		OldUser->RunOrDefer([WeakThis = TWeakObjectPtr<UDreamEventSystem>(this), Value]()
		{
			if (UDreamEventSystem* This = WeakThis.Get())
			{
				This->SetUserIndexNow(Value);
			}
		});
		return;
	}
	SetUserIndexNow(Value);
}

void UDreamEventSystem::SetUserIndexNow(int Value)
{
	if (UserIndex == Value)return;
	// The registry is keyed by user index, so the entry moves with the field. The input subsystem is looked up
	// rather than only read from RegisteredInputSubsystem: the preset actor points its event system at the right
	// player before BeginPlay has registered it.
	UDreamUIInputSubsystem* InputSubsystem = RegisteredInputSubsystem.IsValid()
		? RegisteredInputSubsystem.Get()
		: UDreamUIInputSubsystem::Get(this);
	//"registered" means the map really holds THIS one under the old index, not merely that a map exists
	const bool bWasRegistered = InputSubsystem != nullptr && InputSubsystem->GetEventSystemByUserIndex(UserIndex) == this;
	if (bWasRegistered)
	{
		// Lets the old player go of every pointer: they belonged to the player who was using them.
		InputSubsystem->RemoveEventSystem(this);
	}
	else if (UDreamUIInputUser* OldUser = BoundUser.Get())
	{
		OldUser->RetireAllPointers();
	}
	UnbindFromUser();
	UserIndex = Value;
	if (bWasRegistered)
	{
		InputSubsystem->AddEventSystem(this);
		RegisteredInputSubsystem = InputSubsystem;
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
	// No local player at that index. Player 0 is the honest answer only for the default index; for any other,
	// "nobody" beats writing a second player's cursor or reading their sticks.
	return InUserIndex == 0 ? World->GetFirstPlayerController() : nullptr;
}

APlayerController* UDreamEventSystem::GetPlayerController()const
{
	if (const UDreamUIInputUser* User = GetInputUser())
	{
		return User->GetPlayerController();
	}
	return GetPlayerControllerForUser(this, UserIndex);
}

double UDreamEventSystem::GetPointerClockSeconds(const UObject* WorldContextObject)
{
	// UWorld::GetWorld answers itself, so a world passed in directly works as well as anything in it.
	const UWorld* World = DreamUI::GetWorldSafe(WorldContextObject);
	return World != nullptr ? World->GetRealTimeSeconds() : 0.0;
}
#pragma endregion

#pragma region Settings
void UDreamEventSystem::ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->ApplyHoverCursorToPlayer(bWidgetClaimedCursor, InCursor);
	}
}

bool UDreamEventSystem::GetApplyHoverCursor()const
{
	const UDreamUIInputUser* User = BoundUser.Get();
	return User != nullptr ? User->GetConfig().bApplyHoverCursor : bApplyHoverCursor;
}

void UDreamEventSystem::SetApplyHoverCursor(bool Value)
{
	bApplyHoverCursor = Value;
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->SetApplyHoverCursor(Value);
	}
}

void UDreamEventSystem::SetRaycastEnable(bool bEnable, bool bClearEvent)
{
	bRayEventEnable = bEnable;
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->SetRaycastEnable(bEnable, bClearEvent);
	}
}

// Each setting is the player's once this speaks for one: read from it, written through to it -- and kept here
// too, which is what the details panel shows and what the player is given when this registers.
#define DREAMUI_FACADE_SETTING(Type, Name, Field, Clamp) \
Type UDreamEventSystem::Get##Name()const \
{ \
	const UDreamUIInputUser* User = BoundUser.Get(); \
	return User != nullptr ? User->GetConfig().Field : Field; \
} \
void UDreamEventSystem::Set##Name(Type Value) \
{ \
	Field = Clamp; \
	if (UDreamUIInputUser* User = GetInputUser()) \
	{ \
		User->GetConfig().Field = Field; \
	} \
}

DREAMUI_FACADE_SETTING(EDreamUIPointerInputType, DefaultInputType, DefaultInputType, Value)
DREAMUI_FACADE_SETTING(float, NavigateInputIntervalForFirstTime, NavigateInputIntervalForFirstTime, FMath::Max(Value, MinNavigateInputInterval))
DREAMUI_FACADE_SETTING(float, NavigateInputInterval, NavigateInputInterval, FMath::Max(Value, MinNavigateInputInterval))
DREAMUI_FACADE_SETTING(float, DoubleClickTime, DoubleClickTime, FMath::Max(Value, 0.0f))
DREAMUI_FACADE_SETTING(float, LongPressTime, LongPressTime, FMath::Max(Value, 0.0f))
DREAMUI_FACADE_SETTING(float, SwipeMinDistance, SwipeMinDistance, FMath::Max(Value, 0.0f))
DREAMUI_FACADE_SETTING(float, SwipeMaxDuration, SwipeMaxDuration, FMath::Max(Value, 0.0f))
DREAMUI_FACADE_SETTING(float, PinchMinDistanceChange, PinchMinDistanceChange, FMath::Max(Value, 0.0f))
DREAMUI_FACADE_SETTING(bool, ScrollNavigationTargetIntoView, bScrollNavigationTargetIntoView, Value)
DREAMUI_FACADE_SETTING(bool, AnimateNavigationScroll, bAnimateNavigationScroll, Value)
#undef DREAMUI_FACADE_SETTING

void UDreamEventSystem::SetInputModule(UDreamBaseInputModule* InputModule)
{
	CurrentInputModule = InputModule;
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->SetInputModule(InputModule);
	}
}

void UDreamEventSystem::ClearInputModule()
{
	if (UDreamUIInputUser* User = BoundUser.Get())
	{
		User->ClearInputModule(CurrentInputModule.Get());
	}
	CurrentInputModule = nullptr;
}

void UDreamEventSystem::ClearEvent()
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->ReleaseAllPointers();
	}
}
#pragma endregion

#pragma region Pointers
UDreamPointerEventData* UDreamEventSystem::GetPointerEventData(int PointerID, bool bCreateIfNotExist)const
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetPointerEventData(PointerID, bCreateIfNotExist) : nullptr;
}

void UDreamEventSystem::RemovePointerEventData(int PointerID)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->RemovePointerEventData(PointerID);
	}
}

const TMap<int32, TObjectPtr<UDreamPointerEventData>>& UDreamEventSystem::GetPointerEventDataMap()const
{
	if (const UDreamUIInputUser* User = GetInputUser())
	{
		return User->GetPointerEventDataMap();
	}
	static const TMap<int32, TObjectPtr<UDreamPointerEventData>> NoPointers;
	return NoPointers;
}

FDreamUIRaycastHitDelegate& UDreamEventSystem::GetRaycastHitEvent()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetRaycastHitEvent() : UnboundRaycastHitEvent;
}

FDreamUIMulticastDelegateBaseEventData& UDreamEventSystem::GetInputEvent()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetInputEvent() : UnboundInputEvent;
}

FDreamUIPointerInputTypeChangedDelegate& UDreamEventSystem::GetInputChangedEvent()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetInputChangedEvent() : UnboundPointerInputTypeChangedEvent;
}

FDreamUIInputDeviceChangedDelegate& UDreamEventSystem::GetInputDeviceChangedEvent()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetInputDeviceChangedEvent() : UnboundInputDeviceChangedEvent;
}

FDreamUIGamepadModelChangedDelegate& UDreamEventSystem::GetGamepadModelChangedEvent()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetGamepadModelChangedEvent() : UnboundGamepadModelChangedEvent;
}

void UDreamEventSystem::RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->RaiseHitEvent(bHitOrNot, HitResult, HitComponent);
	}
}

void UDreamEventSystem::BroadcastBlueprintInputEvent(UDreamBaseEventData* InEventData)
{
	InputEventBP.Broadcast(InEventData);
}

void UDreamEventSystem::BroadcastBlueprintRaycastHit(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent)
{
	RaycastHitEventBP.Broadcast(bHitOrNot, HitResult, HitComponent);
}

void UDreamEventSystem::BroadcastBlueprintInputDeviceChanged(EDreamUIInputDevice InDevice)
{
	InputDeviceChangedEventBP.Broadcast(InDevice);
}

void UDreamEventSystem::BroadcastBlueprintGamepadModelChanged(EDreamUIGamepadModel InModel)
{
	GamepadModelChangedEventBP.Broadcast(InModel);
}

bool UDreamEventSystem::IsPointerOverUIByPointerID(int PointerID)
{
	const UDreamUIInputUser* User = GetInputUser();
	UDreamPointerEventData* EventData = User != nullptr ? User->FindPointerEventData(PointerID) : nullptr;
	return EventData != nullptr && EventData->IsPointerOverUI();
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
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr && User->SetPointerInputType(InPointerEventData, InInputType);
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
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->SetSelectWidget(InSelectWidget, EventData);
	}
}

void UDreamEventSystem::SetSelectWidget(UDreamEventSystem* InEventSystem, UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData)
{
	if (EventData == nullptr)
	{
		return;
	}
	if (InEventSystem != nullptr && InEventSystem->GetInputUser() != nullptr)
	{
		InEventSystem->SetSelectWidget(InSelectWidget, EventData);
		return;
	}
	// No player to go through: the handlers on the widgets, and nothing else.
	if (EventData->SelectedComponent != InSelectWidget)
	{
		UDreamWidget* OldSelected = EventData->SelectedComponent;
		EventData->SelectedComponent = InSelectWidget;
		const UDreamPointerEventData* PointerEventData = Cast<UDreamPointerEventData>(EventData);
		const int32 PointerId = PointerEventData != nullptr ? PointerEventData->PointerID : 0;
		if (IsValid(OldSelected))
		{
			ExecuteEvent_OnPointerDeselect(OldSelected, EventData, false);
			OldSelected->NotifyFocusLost(0, PointerId);
		}
		if (IsValid(EventData->SelectedComponent))
		{
			ExecuteEvent_OnPointerSelect(EventData->SelectedComponent, EventData, false);
			EventData->SelectedComponent->NotifyFocusReceived(0, PointerId);
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
	SetSelectWidget(InSelectWidget, GetPointerEventData(0, true));
}

void UDreamEventSystem::LogEventData(UDreamBaseEventData* inEventData)
{
	if (const UDreamUIInputUser* User = GetInputUser())
	{
		User->LogEventData(inEventData);
	}
}
#pragma endregion

#pragma region InputDevice
EDreamUIInputDevice UDreamEventSystem::GetInputDeviceForKey(const FKey& InKey)
{
	if (InKey.IsTouch())
	{
		return EDreamUIInputDevice::Touch;
	}
	// A mouse key is neither gamepad nor touch, so it lands with the keyboard -- which is what a prompt wants
	// anyway: the pair is one device as far as the player's hands are concerned.
	return InKey.IsGamepadKey() ? EDreamUIInputDevice::Gamepad : EDreamUIInputDevice::MouseAndKeyboard;
}

EDreamUIInputDevice UDreamEventSystem::GetCurrentInputDevice()const
{
	const UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetCurrentInputDevice() : EDreamUIInputDevice::MouseAndKeyboard;
}

bool UDreamEventSystem::ReportInputDevice(EDreamUIInputDevice InDevice)
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr && User->ReportInputDevice(InDevice);
}

EDreamUIGamepadModel UDreamEventSystem::GetGamepadModelForDeviceName(FName InInputDeviceName, FName InHardwareDeviceIdentifier)
{
	// Matched on substrings of both names because platforms disagree about which one carries the brand, and
	// because the exact spellings differ by platform SDK version.
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
	// Named by nobody, or named something this does not recognize. Generic is the honest answer.
	return EDreamUIGamepadModel::Generic;
}

EDreamUIGamepadModel UDreamEventSystem::GetCurrentGamepadModel()const
{
	const UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetCurrentGamepadModel() : EDreamUIGamepadModel::Generic;
}

bool UDreamEventSystem::RefreshGamepadModel()
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr && User->RefreshGamepadModel();
}

void UDreamEventSystem::SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel)
{
	if (UDreamUIInputUser* User = GetInputUser())
	{
		User->SetGamepadModelOverride(bInOverride, InModel);
	}
}
#pragma endregion

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

// Through the player, which dispatches and broadcasts.
#define DREAMUI_FACADE_CALL(Name, WidgetType, EventDataType) \
void UDreamEventSystem::Name(WidgetType* InTarget, EventDataType* EventData) \
{ \
	if (UDreamUIInputUser* User = GetInputUser()) \
	{ \
		User->Name(InTarget, EventData); \
	} \
}

DREAMUI_FACADE_CALL(CallOnPointerEnter, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerExit, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerDown, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerUp, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerClick, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerDoubleClick, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerLongPress, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerPinch, UDreamWidget, UDreamGestureEventData)
DREAMUI_FACADE_CALL(CallOnPointerSwipe, UDreamWidget, UDreamGestureEventData)
DREAMUI_FACADE_CALL(CallOnPointerBeginDrag, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerDrag, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerEndDrag, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerScroll, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerDragDrop, UDreamWidget, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnPointerSelect, UDreamWidget, UDreamBaseEventData)
DREAMUI_FACADE_CALL(CallOnPointerDeselect, UDreamWidget, UDreamBaseEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetEnter, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetExit, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetDown, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetUp, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetClick, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetDoubleClick, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetLongPress, AActor, UDreamPointerEventData)
DREAMUI_FACADE_CALL(CallOnWorldTargetScroll, AActor, UDreamPointerEventData)
#undef DREAMUI_FACADE_CALL
#pragma endregion

#pragma region WorldTarget
FDreamUIPointerWorldTarget* UDreamEventSystem::GetPointerWorldTarget(int InPointerID, bool bCreateIfNotExist)
{
	UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetPointerWorldTarget(InPointerID, bCreateIfNotExist) : nullptr;
}
AActor* UDreamEventSystem::GetHoveredWorldTarget(int InPointerID)const
{
	const UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetHoveredWorldTarget(InPointerID) : nullptr;
}
AActor* UDreamEventSystem::GetPressedWorldTarget(int InPointerID)const
{
	const UDreamUIInputUser* User = GetInputUser();
	return User != nullptr ? User->GetPressedWorldTarget(InPointerID) : nullptr;
}
#pragma endregion

#undef LOCTEXT_NAMESPACE
