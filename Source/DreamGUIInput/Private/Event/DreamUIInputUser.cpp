// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIInputUser.h"

#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CoreGlobals.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamWidgetNavigation.h"
#include "DreamGUI.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamGameViewportClient.h"
#include "Event/DreamGestureEventData.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamPointerPolicy.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIKeyRouting.h"
#include "Event/DreamUINestedSurface.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerLongPressInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/DreamUITabOrder.h"
#include "Interaction/DreamUITextInputTarget.h"
#include "Interaction/UISelectable.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_CYCLE_STAT(TEXT("InputUserFrame"), STAT_DreamUIInputUserFrame, STATGROUP_DreamGUI);

int32 DreamUIPointerIds::ForTouch(int32 InFingerIndex)
{
	return TouchBase + InFingerIndex;
}

float DreamUIInputClock::GetUIDeltaSeconds(const UObject* InWorldContext, float InTickDeltaSeconds)
{
	const UWorld* World = InWorldContext != nullptr ? InWorldContext->GetWorld() : nullptr;
	return World != nullptr && World->DeltaRealTimeSeconds > 0.0f ? World->DeltaRealTimeSeconds : InTickDeltaSeconds;
}

FDreamUIInputDispatchScope::FDreamUIInputDispatchScope(UDreamUIInputUser* InUser)
	: User(InUser)
{
	if (InUser != nullptr)
	{
		++InUser->DispatchDepth;
	}
}

FDreamUIInputDispatchScope::~FDreamUIInputDispatchScope()
{
	if (UDreamUIInputUser* InputUser = User.Get())
	{
		if (--InputUser->DispatchDepth == 0)
		{
			InputUser->DrainDeferredCommands();
		}
	}
}

void UDreamUIInputUser::InitializeUser(int32 InUserIndex, bool bInIsScriptUser)
{
	UserIndex = InUserIndex;
	bIsScriptUser = bInIsScriptUser;
	// What the focus-visible listeners are told is a change from, so the first focus does not announce one that is not.
	bFocusVisibleBroadcast = IsFocusVisible();
}

UDreamUIInputSubsystem* UDreamUIInputUser::GetInputSubsystem() const
{
	return GetTypedOuter<UDreamUIInputSubsystem>();
}

UWorld* UDreamUIInputUser::GetWorld() const
{
	const UDreamUIInputSubsystem* Subsystem = GetInputSubsystem();
	return Subsystem != nullptr ? Subsystem->GetWorld() : nullptr;
}

ULocalPlayer* UDreamUIInputUser::GetLocalPlayer() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetLocalPlayerByIndex(UserIndex) : nullptr;
}

APlayerController* UDreamUIInputUser::GetPlayerController() const
{
	return UDreamEventSystem::GetPlayerControllerForUser(GetWorld(), UserIndex);
}

#pragma region Settings
void UDreamUIInputUser::SetRaycastEnable(bool bEnable, bool bClearEvent)
{
	Config.bRayEventEnable = bEnable;
	if (!bEnable)
	{
		// What is waiting for the next frame would be dispatched the frame tracing comes back, wherever the pointer is
		// by then. A press is dropped. A release ends its press at once, with its up and without a click or a drop:
		// kept for later, it landed whenever tracing came back, on whatever was under the place it was let go by then.
		TArray<int32, TInlineAllocator<4>> ReleasedPointers;
		for (const FQueuedButton& Queued : QueuedButtons)
		{
			if (!Queued.bPressed)
			{
				ReleasedPointers.AddUnique(Queued.PointerID);
			}
		}
		QueuedButtons.Reset();
		QueuedScrolls.Reset();
		for (const int32 PointerID : ReleasedPointers)
		{
			CancelPointerPress(PointerID);
		}
	}
	if (!bEnable && bClearEvent)
	{
		ReleaseAllPointers();
	}
}

void UDreamUIInputUser::SetApplyHoverCursor(bool bInApply)
{
	if (Config.bApplyHoverCursor == bInApply)
	{
		return;
	}
	Config.bApplyHoverCursor = bInApply;
	if (!bInApply)
	{
		// Turning it off gives the cursor back rather than leaving whatever the last hover asked for frozen on screen.
		RestoreHoverCursor();
	}
}

void UDreamUIInputUser::SetInputModule(UDreamBaseInputModule* InModule)
{
	InputModule = InModule;
}

void UDreamUIInputUser::ClearInputModule(const UDreamBaseInputModule* InModule)
{
	if (InputModule.Get() == InModule)
	{
		InputModule.Reset();
	}
}
#pragma endregion

#pragma region Pointers
UDreamPointerEventData* UDreamUIInputUser::FindPointerEventData(int32 InPointerID) const
{
	const TObjectPtr<UDreamPointerEventData>* Found = PointerEventDataMap.Find(InPointerID);
	return Found != nullptr ? Found->Get() : nullptr;
}

UDreamPointerEventData* UDreamUIInputUser::GetPointerEventData(int32 InPointerID, bool bCreateIfNotExist)
{
	if (UDreamPointerEventData* Found = FindPointerEventData(InPointerID))
	{
		return Found;
	}
	if (!bCreateIfNotExist || bShutDown)
	{
		// Merely asking about a pointer must not mint one: every pointer is traced every frame, so a query for
		// an id that was never pressed used to cost a permanent trace at a position nobody is pointing at.
		return nullptr;
	}
	UDreamPointerEventData* NewEventData = NewObject<UDreamPointerEventData>(this);
	NewEventData->PointerID = InPointerID;
	// Stamped here because this is the only place a pointer is born, and the only place that knows whose it
	// is: handlers read EventData->UserIndex to find their own player back.
	NewEventData->UserIndex = UserIndex;
	NewEventData->InputType = Config.DefaultInputType;
	// Off the viewport until something places it. Born at (0,0) -- the navigation cursor's pointer, made by a focus or a
	// key before any mouse moved it -- it was traced at the top-left pixel every frame, hovering whatever was there.
	NewEventData->PointerPosition = DreamUIPointerPosition::OffViewport();
	PointerEventDataMap.Add(InPointerID, NewEventData);
	RestoreLiftedFingerClickRun(NewEventData);
	return NewEventData;
}

void UDreamUIInputUser::KeepLiftedFingerClickRun(const UDreamPointerEventData* InEventData)
{
	if (InEventData == nullptr)
	{
		return;
	}
	if (InEventData->ClickCount <= 0)
	{
		LiftedFingerClickRuns.Remove(InEventData->PointerID);
		return;
	}
	// Whether the finger's next tap continues the run is the press's own question (UDreamPointerInputModule::
	// ProcessPointerEvent): the same widget, the same button, inside DoubleClickTime and the drag threshold. All this
	// keeps is what that question reads.
	FLiftedFingerClickRun& Run = LiftedFingerClickRuns.FindOrAdd(InEventData->PointerID);
	Run.ClickCount = InEventData->ClickCount;
	Run.ClickTime = InEventData->ClickTime;
	Run.LastClickWidget = InEventData->LastClickWidget.Get();
	Run.LastClickMouseButtonType = InEventData->LastClickMouseButtonType;
	Run.LastClickPressPointerPosition = InEventData->LastClickPressPointerPosition;
	Run.LastClickPressWorldPoint = InEventData->LastClickPressWorldPoint;
	Run.LastClickedActor.Reset();
	Run.LastClickedActorTime = 0.0;
	if (const FDreamUIPointerWorldTarget* WorldTarget = PointerWorldTargetMap.Find(InEventData->PointerID))
	{
		Run.LastClickedActor = WorldTarget->LastClicked;
		Run.LastClickedActorTime = WorldTarget->LastClickedTime;
	}
}

void UDreamUIInputUser::RestoreLiftedFingerClickRun(UDreamPointerEventData* InEventData)
{
	FLiftedFingerClickRun Run;
	if (InEventData == nullptr || !LiftedFingerClickRuns.RemoveAndCopyValue(InEventData->PointerID, Run))
	{
		return;
	}
	InEventData->ClickCount = Run.ClickCount;
	InEventData->ClickTime = Run.ClickTime;
	InEventData->LastClickWidget = Run.LastClickWidget.Get();
	InEventData->LastClickMouseButtonType = Run.LastClickMouseButtonType;
	InEventData->LastClickPressPointerPosition = Run.LastClickPressPointerPosition;
	InEventData->LastClickPressWorldPoint = Run.LastClickPressWorldPoint;
	if (Run.LastClickedActor.IsValid())
	{
		FDreamUIPointerWorldTarget& WorldTarget = PointerWorldTargetMap.FindOrAdd(InEventData->PointerID);
		WorldTarget.LastClicked = Run.LastClickedActor;
		WorldTarget.LastClickedTime = Run.LastClickedActorTime;
	}
}

void UDreamUIInputUser::RetirePointer(int32 InPointerID)
{
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this), InPointerID]()
	{
		if (UDreamUIInputUser* This = WeakThis.Get())
		{
			This->ReleasePointerNow(InPointerID);
			This->PointerEventDataMap.Remove(InPointerID);
			This->PointerWorldTargetMap.Remove(InPointerID);
			This->TraceCache.Remove(InPointerID);
			This->PressRaycasters.Remove(InPointerID);
			This->PointersMovedSinceTrace.Remove(InPointerID);
		}
	});
}

void UDreamUIInputUser::RemovePointerEventData(int32 InPointerID)
{
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this), InPointerID]()
	{
		if (UDreamUIInputUser* This = WeakThis.Get())
		{
			// What that pointer was doing to the world goes with it. The caller has already let go of whatever
			// it pressed and exited whatever it was over, so this is the bookkeeping, not an event.
			This->PointerEventDataMap.Remove(InPointerID);
			This->PointerWorldTargetMap.Remove(InPointerID);
			This->TraceCache.Remove(InPointerID);
			This->PressRaycasters.Remove(InPointerID);
			This->PointersMovedSinceTrace.Remove(InPointerID);
		}
	});
}

bool UDreamUIInputUser::SetPointerInputType(UDreamPointerEventData* InEventData, EDreamUIPointerInputType InInputType)
{
	if (InEventData == nullptr || InEventData->InputType == InInputType)
	{
		return false;
	}
	InEventData->InputType = InInputType;
	PointerInputTypeChangedEvent.Broadcast(InEventData->PointerID, InEventData->InputType);
	return true;
}

FDreamUIPointerWorldTarget* UDreamUIInputUser::GetPointerWorldTarget(int32 InPointerID, bool bCreateIfNotExist)
{
	if (bCreateIfNotExist)
	{
		return &PointerWorldTargetMap.FindOrAdd(InPointerID);
	}
	return PointerWorldTargetMap.Find(InPointerID);
}

AActor* UDreamUIInputUser::GetHoveredWorldTarget(int32 InPointerID) const
{
	const FDreamUIPointerWorldTarget* State = PointerWorldTargetMap.Find(InPointerID);
	return State != nullptr ? State->Hovered.Get() : nullptr;
}

AActor* UDreamUIInputUser::GetPressedWorldTarget(int32 InPointerID) const
{
	const FDreamUIPointerWorldTarget* State = PointerWorldTargetMap.Find(InPointerID);
	return State != nullptr ? State->Pressed.Get() : nullptr;
}

void UDreamUIInputUser::SetSelectWidget(UDreamWidget* InSelectWidget, UDreamBaseEventData* InEventData)
{
	// A pointer's own press -- a selectable taking the focus as it is clicked or tapped -- is Pointer; anything else that
	// moves the focus without saying what it is (a Blueprint, a control selecting itself) is code.
	const UDreamPointerEventData* PointerEventData = Cast<UDreamPointerEventData>(InEventData);
	const bool bPointerPress = PointerEventData != nullptr && PointerEventData->InputType == EDreamUIPointerInputType::Pointer;
	SetSelectWidgetForCause(InSelectWidget, InEventData, bPointerPress ? EDreamUIFocusCause::Pointer : EDreamUIFocusCause::Script);
}

void UDreamUIInputUser::SetSelectWidgetForCause(UDreamWidget* InSelectWidget, UDreamBaseEventData* InEventData, EDreamUIFocusCause InCause)
{
	if (InEventData == nullptr)
	{
		return;
	}
	// Recorded before anything hears of the change: a widget's Select handler -- a field deciding whether to begin its
	// edit, a control drawing its Focused look -- reads the cause of its own selection. Inside a navigation step it is
	// the step's, whoever asked: a handler that moves the focus on from inside the step moves it as part of it.
	FocusCause = !IsValid(InSelectWidget) ? EDreamUIFocusCause::None
		: StepFocusCause != EDreamUIFocusCause::None ? StepFocusCause : InCause;
	ON_SCOPE_EXIT{ UpdateFocusVisible(); };
	// The player's focus is what changes, whichever pointer asks. A pointer used to keep a focus of its own, and a
	// finger's went with the finger: tap a field, lift, and nothing was focused -- while the field went on editing
	// with nothing able to end it.
	UDreamWidget* OldSelected = FocusedWidget.Get();
	InEventData->SelectedComponent = InSelectWidget;
	if (OldSelected == InSelectWidget)
	{
		MirrorFocusOntoPointers();
		return;
	}
	FocusedWidget = InSelectWidget;
	MirrorFocusOntoPointers();
	const UDreamPointerEventData* PointerEventData = Cast<UDreamPointerEventData>(InEventData);
	const int32 PointerId = PointerEventData != nullptr ? PointerEventData->PointerID : 0;
	if (IsValid(OldSelected))
	{
		CallOnPointerDeselect(OldSelected, InEventData);
		if (IsValid(OldSelected))
		{
			OldSelected->NotifyFocusLost(UserIndex, PointerId);
		}
	}
	// Only while the focus is still what this call gave it: a Deselect handler is game code, and may already have
	// moved it on -- and that move sent its own Select.
	if (UDreamWidget* NewSelected = FocusedWidget.Get(); IsValid(NewSelected) && NewSelected == InSelectWidget)
	{
		CallOnPointerSelect(NewSelected, InEventData);
		if (IsValid(NewSelected))
		{
			NewSelected->NotifyFocusReceived(UserIndex, PointerId);
		}
	}
}

void UDreamUIInputUser::MirrorFocusOntoPointers()
{
	UDreamWidget* Focus = FocusedWidget.Get();
	for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Pair : PointerEventDataMap)
	{
		if (UDreamPointerEventData* EventData = Pair.Value.Get())
		{
			EventData->SelectedComponent = Focus;
		}
	}
}
#pragma endregion

#pragma region Focus
EDreamUIFocusCause UDreamUIInputUser::GetFocusCause() const
{
	return IsValid(FocusedWidget.Get()) ? FocusCause : EDreamUIFocusCause::None;
}

bool UDreamUIInputUser::IsFocusVisible() const
{
	if (!UDreamGUISettings::Get()->bFocusVisibleOnlyFromKeys)
	{
		return true;
	}
	// CSS's :focus-visible. Keys and the pad draw the focus, a pointer does not -- a clicked button is not drawn focused --
	// and code that moves the focus draws it when the player was last on the keys or the pad: a dialog Enter opened shows
	// where its focus is, one a click opened does not. A step landing right now is a key's, whatever it lands on.
	const EDreamUIFocusCause Cause = StepFocusCause != EDreamUIFocusCause::None ? StepFocusCause : FocusCause;
	switch (Cause)
	{
	case EDreamUIFocusCause::Navigation:
	case EDreamUIFocusCause::Tab:
		return true;
	case EDreamUIFocusCause::Pointer:
		return false;
	default:
		return bLatestInputFromKeys;
	}
}

void UDreamUIInputUser::RequestNavigationStep(EDreamUINavigationDirection InDirection)
{
	if (!bShutDown)
	{
		RequestedNavigationStep = InDirection;
	}
}

void UDreamUIInputUser::NoteInputFromKeys(bool bInFromKeys)
{
	if (bLatestInputFromKeys == bInFromKeys)
	{
		return;
	}
	bLatestInputFromKeys = bInFromKeys;
	UpdateFocusVisible();
}

void UDreamUIInputUser::UpdateFocusVisible()
{
	const bool bVisible = IsFocusVisible();
	if (bVisible == bFocusVisibleBroadcast)
	{
		return;
	}
	bFocusVisibleBroadcast = bVisible;
	FocusVisibleChangedEvent.Broadcast(bVisible);
}
#pragma endregion

#pragma region Text
void UDreamUIInputUser::SetTextTarget(UObject* InTarget)
{
	if (InTarget == nullptr || bShutDown || !ensureMsgf(InTarget->Implements<UDreamUITextInputTarget>(),
		TEXT("%s claimed a player's keyboard but does not take text input"), *InTarget->GetPathName()))
	{
		return;
	}
	TextTarget = InTarget;
	RefreshTextKeys();
}

void UDreamUIInputUser::ClearTextTarget(const UObject* InTarget)
{
	if (InTarget == nullptr || TextTarget.Get() != InTarget)
	{
		return;
	}
	TextTarget.Reset();
	PopTextKeys();
}

void UDreamUIInputUser::PopTextKeys()
{
	if (APlayerController* Controller = TextKeysController.Get(); Controller != nullptr && TextKeys != nullptr)
	{
		Controller->PopInputComponent(TextKeys);
	}
	TextKeysController.Reset();
}

void UDreamUIInputUser::RefreshTextKeys()
{
	PopTextKeys();
	const IDreamUITextInputTarget* Target = Cast<IDreamUITextInputTarget>(TextTarget.Get());
	// A player with no controller -- a script player -- has no keyboard of its own to bind: its field is typed into
	// through the field's own entries.
	APlayerController* Controller = Target != nullptr && !bShutDown ? GetPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return;
	}
	if (TextKeys == nullptr || TextKeys->GetOuter() != Controller)
	{
		TextKeys = NewObject<UInputComponent>(Controller, UInputSettings::GetDefaultInputComponentClass(), NAME_None, RF_Transient);
		// Stated, as AActor::EnableInput states its component's: the field is typed into above everything the
		// controller listens to -- one below a key selector's capture, which is listening for exactly the next key.
		// Left unset, the priority was whatever the memory held, and the component could land under the preset,
		// which then heard Enter as a confirm and clicked the field straight back into its edit.
		TextKeys->Priority = TNumericLimits<int32>::Max() - 1;
		TextKeys->bBlockInput = false;
	}
	TextKeys->KeyBindings.Reset();
	TArray<FKey> Keys;
	Target->GetTextInputKeys(Keys);
	for (const FKey& Key : Keys)
	{
		// Executed while the game is paused too: a pause menu's field has to take Backspace, Enter and the arrows. Left
		// at the default, the bindings would still consume their keys in a paused game -- UPlayerInput counts a
		// consuming binding whether or not its delegate runs -- and the keys would reach nothing at all. Whether the
		// field answers while paused is its own call, made as the key arrives.
		TextKeys->BindKey(Key, IE_Pressed, this, &UDreamUIInputUser::HandleTextKeyPressed).bExecuteWhenPaused = true;
		TextKeys->BindKey(Key, IE_Repeat, this, &UDreamUIInputUser::HandleTextKey).bExecuteWhenPaused = true;
		// The release too: these bindings take their key from everything below them, the release included, and a key
		// held since before the edit began -- the arrow that navigated into the field -- is still owed its release by
		// whatever took its press.
		TextKeys->BindKey(Key, IE_Released, this, &UDreamUIInputUser::HandleTextKeyReleased).bExecuteWhenPaused = true;
	}
	// Pushed last, so it sits above everything the controller already listens to, the preset included: a key taken
	// by a field being edited is typing -- not a shortcut, not a navigation step, not the pawn's to move with.
	Controller->PushInputComponent(TextKeys);
	TextKeysController = Controller;
}

void UDreamUIInputUser::HandleTextKey(FKey InKey)
{
	if (IDreamUITextInputTarget* Target = Cast<IDreamUITextInputTarget>(TextTarget.Get()))
	{
		Target->HandleTextInputKey(InKey, TextKeysController.Get());
	}
}

void UDreamUIInputUser::HandleTextKeyPressed(FKey InKey)
{
	FDreamUIKeyPress Press;
	Press.Taker = EDreamUIKeyPressTaker::Text;
	NoteKeyPress(InKey, Press);
	HandleTextKey(InKey);
}

void UDreamUIInputUser::HandleTextKeyReleased(FKey InKey)
{
	// Only a press still on the books is owed its release from here. A source that routed the press routes its release
	// too -- the Slate source, which hears the key before the controller does -- and takes the press as it goes; this
	// binding hears the same release after it, and routing it again found no press and handed the key-up to the
	// bindings a second time, the focused widget included.
	if (FindKeyPress(InKey) != nullptr)
	{
		DreamUIKeyRouting::RouteKeyRelease(this, InKey, nullptr);
	}
}
#pragma endregion

#pragma region Keys
void UDreamUIInputUser::NoteKeyPress(const FKey& InKey, const FDreamUIKeyPress& InPress)
{
	if (InKey.IsValid() && !bShutDown)
	{
		KeyPresses.Add(InKey, InPress);
	}
}

bool UDreamUIInputUser::TakeKeyPress(const FKey& InKey, FDreamUIKeyPress& OutPress)
{
	return KeyPresses.RemoveAndCopyValue(InKey, OutPress);
}
#pragma endregion

#pragma region DeviceAndCursor
namespace DreamUIInputUserDeviceLocal
{
	/** The model InDeviceId's descriptor names (the registry rather than FInputDeviceScope, which is deprecated in 5.8 and valid only inside the platform's own dispatch); Generic for a device with none. */
	EDreamUIGamepadModel DetectModelOf(FInputDeviceId InDeviceId)
	{
		if (const TOptional<FInputDeviceDescriptor> Descriptor = FInputDeviceRegistry::FindDescriptor(InDeviceId))
		{
			return UDreamEventSystem::GetGamepadModelForDeviceName(Descriptor->InputDeviceName, Descriptor->HardwareDeviceIdentifier);
		}
		return EDreamUIGamepadModel::Generic;
	}
}

bool UDreamUIInputUser::ReportInputDevice(EDreamUIInputDevice InDevice, FInputDeviceId InDeviceId)
{
	// A pad's button or stick is a key as far as code-moved focus is concerned, and a finger is a pointer. The keyboard
	// and the mouse share a device, so which of the two it was is noted where the key or the move is in hand.
	if (InDevice == EDreamUIInputDevice::Gamepad)
	{
		NoteInputFromKeys(true);
		// Every pad input, not only the first after another device: a second pad of another make picked up mid-session is
		// still a pad. Before the device change goes out, so a prompt bar rebuilding from it already sees the right glyphs.
		NoteGamepadDevice(InDeviceId, CurrentInputDevice != EDreamUIInputDevice::Gamepad);
	}
	else if (InDevice == EDreamUIInputDevice::Touch)
	{
		NoteInputFromKeys(false);
	}
	if (CurrentInputDevice == InDevice)
	{
		return false;//every key comes through here; broadcasting each one would rebuild prompts per frame
	}
	CurrentInputDevice = InDevice;
	ApplyCursorForDevice(InDevice);
	InputDeviceChangedEvent.Broadcast(InDevice);
	if (UDreamUIInputSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->GetOnInputDeviceChanged().Broadcast(UserIndex, InDevice);
	}
	for (UDreamEventSystem* Facade : GetEventSystemFacades())
	{
		Facade->BroadcastBlueprintInputDeviceChanged(InDevice);
	}
	return true;
}

void UDreamUIInputUser::ReportInputKey(const FKey& InKey, FInputDeviceId InDeviceId)
{
	if (!InKey.IsValid())
	{
		return;
	}
	// The mouse's keys -- its buttons, its axes, the wheel -- and a finger are pointers; every other key is the keyboard's
	// or the pad's.
	const EDreamUIInputDevice Device = UDreamEventSystem::GetInputDeviceForKey(InKey);
	if (Device == EDreamUIInputDevice::MouseAndKeyboard)
	{
		NoteInputFromKeys(!InKey.IsMouseButton());
	}
	ReportInputDevice(Device, InDeviceId);
}

void UDreamUIInputUser::NoteGamepadDevice(FInputDeviceId InDeviceId, bool bInPickedUp)
{
	FInputDeviceId Device = InDeviceId;
	if (Device.IsValid())
	{
		GamepadDeviceNamedFrame = GFrameCounter;
	}
	else if (GamepadDeviceNamedFrame == GFrameCounter)
	{
		return;//the source that heard this input first named its pad; the platform's guess is no newer than that
	}
	else
	{
		Device = FindMostRecentGamepadDevice();
		if (!Device.IsValid() && bInPickedUp)
		{
			// No record of recent use -- a platform without the input device subsystem: the player's primary device, as
			// before, on the press that picks a pad up.
			if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
			{
				Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(LocalPlayer->GetPlatformUserId());
			}
		}
	}
	if (!Device.IsValid() || Device == GamepadModelDevice)
	{
		return;//the same pad as last time says the same thing
	}
	// Remembered under an override as well, so dropping it reads the pad in the player's hands then.
	GamepadModelDevice = Device;
	if (!bGamepadModelOverridden)//the project has told us; the platform does not get a vote
	{
		ApplyGamepadModel(DreamUIInputUserDeviceLocal::DetectModelOf(Device));
	}
}

FInputDeviceId UDreamUIInputUser::FindMostRecentGamepadDevice() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UInputDeviceSubsystem* Devices = LocalPlayer != nullptr ? UInputDeviceSubsystem::Get() : nullptr;
	if (Devices == nullptr)
	{
		return INPUTDEVICEID_NONE;
	}
	// What the engine's own input pre-processor saw last for this player -- the device that sent the input being handled
	// now, which reached the engine before it reached any binding. Never the keyboard and mouse: a key pressed after the
	// pad's in the same frame says nothing about which pad is in the player's hands.
	const FInputDeviceId Latest = Devices->GetMostRecentlyUsedInputDeviceId(LocalPlayer->GetPlatformUserId());
	if (!Latest.IsValid() || Devices->GetInputDeviceHardwareIdentifier(Latest).PrimaryDeviceType == EHardwareDevicePrimaryType::KeyboardAndMouse)
	{
		return INPUTDEVICEID_NONE;
	}
	return Latest;
}

bool UDreamUIInputUser::ApplyGamepadModel(EDreamUIGamepadModel InModel)
{
	if (CurrentGamepadModel == InModel)
	{
		return false;
	}
	CurrentGamepadModel = InModel;
	GamepadModelChangedEvent.Broadcast(CurrentGamepadModel);
	for (UDreamEventSystem* Facade : GetEventSystemFacades())
	{
		Facade->BroadcastBlueprintGamepadModelChanged(CurrentGamepadModel);
	}
	return true;
}

bool UDreamUIInputUser::RefreshGamepadModel()
{
	if (bGamepadModelOverridden)return false;//the project has told us; the platform does not get a vote

	// The pad this player used last, else what the platform saw last; with neither, nothing names a pad: Generic.
	FInputDeviceId Device = GamepadModelDevice;
	if (!Device.IsValid())
	{
		Device = FindMostRecentGamepadDevice();
	}
	if (!Device.IsValid())
	{
		if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(LocalPlayer->GetPlatformUserId());
		}
	}
	GamepadModelDevice = Device;
	return ApplyGamepadModel(Device.IsValid() ? DreamUIInputUserDeviceLocal::DetectModelOf(Device) : EDreamUIGamepadModel::Generic);
}

void UDreamUIInputUser::SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel)
{
	bGamepadModelOverridden = bInOverride;
	if (!bInOverride)
	{
		RefreshGamepadModel();//back to whatever the platform says, right now rather than at the next press
		return;
	}
	ApplyGamepadModel(InModel);
}

void UDreamUIInputUser::ApplyCursorForDevice(EDreamUIInputDevice InDevice)
{
	// A finger says nothing about the cursor, and the cursor is only DreamGUI's to hide while DreamGUI is what shows it:
	// its UI-only input mode, held by its viewport client. Any other mode leaves it to the game.
	if (InDevice == EDreamUIInputDevice::Touch || !UDreamGUISettings::Get()->bHideCursorOnGamepad)
	{
		return;
	}
	APlayerController* PlayerController = GetPlayerController();
	if (PlayerController == nullptr || !UDreamGameViewportClient::IsDreamUIOnlyInputFor(PlayerController))
	{
		return;
	}
	PlayerController->bShowMouseCursor = InDevice != EDreamUIInputDevice::Gamepad;
}

void UDreamUIInputUser::ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor)
{
	if (!Config.bApplyHoverCursor)return;
	APlayerController* PlayerController = GetPlayerController();
	if (PlayerController == nullptr)return;

	if (bWidgetClaimedCursor)
	{
		if (!bHoverCursorOverrideActive)
		{
			// Taken once, at the moment the UI first takes the cursor over, so that whatever the game was
			// showing is what comes back -- not EMouseCursor::Default, which is the plugin's idea of neutral.
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

void UDreamUIInputUser::RestoreHoverCursor()
{
	if (!bHoverCursorOverrideActive)
	{
		return;
	}
	bHoverCursorOverrideActive = false;
	if (APlayerController* PlayerController = GetPlayerController())
	{
		PlayerController->CurrentMouseCursor = CursorBeforeHoverOverride;
	}
}
#pragma endregion

#pragma region Dispatch
void UDreamUIInputUser::RunOrDefer(TFunction<void()>&& InCommand)
{
	if (DispatchDepth > 0 || bDrainingDeferredCommands)
	{
		DeferredCommands.Add(MoveTemp(InCommand));
		return;
	}
	InCommand();
}

void UDreamUIInputUser::DrainDeferredCommands()
{
	if (bDrainingDeferredCommands || DispatchDepth > 0)
	{
		return;
	}
	TGuardValue<bool> Draining(bDrainingDeferredCommands, true);
	// A command may dispatch, and a handler of that may defer another: they join the end of the line. The cap
	// only stops two handlers that answer each other from spinning forever.
	constexpr int32 MaxCommands = 1024;
	int32 Ran = 0;
	while (DeferredCommands.Num() > 0 && Ran < MaxCommands)
	{
		TFunction<void()> Command = MoveTemp(DeferredCommands[0]);
		DeferredCommands.RemoveAt(0);
		++Ran;
		if (Command)
		{
			Command();
		}
	}
	ensureMsgf(DeferredCommands.Num() == 0, TEXT("%s: input commands kept deferring each other; %d dropped."), *GetName(), DeferredCommands.Num());
	DeferredCommands.Reset();
}

void UDreamUIInputUser::BroadcastInputEvent(UDreamBaseEventData* InEventData)
{
	InputEvent.Broadcast(InEventData);
	if (UDreamUIInputSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->GetOnInputEvent().Broadcast(InEventData);
	}
	for (UDreamEventSystem* Facade : GetEventSystemFacades())
	{
		Facade->BroadcastBlueprintInputEvent(InEventData);
	}
}

void UDreamUIInputUser::SetEventSystem(UDreamEventSystem* InEventSystem)
{
	EventSystem = InEventSystem;
}

void UDreamUIInputUser::AddEventSystemFacade(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem != nullptr)
	{
		EventSystemFacades.AddUnique(InEventSystem);
	}
}

void UDreamUIInputUser::RemoveEventSystemFacade(UDreamEventSystem* InEventSystem)
{
	EventSystemFacades.Remove(InEventSystem);
	if (EventSystem.Get() == InEventSystem)
	{
		EventSystem.Reset();
	}
}

TArray<UDreamEventSystem*, TInlineAllocator<2>> UDreamUIInputUser::GetEventSystemFacades()
{
	TArray<UDreamEventSystem*, TInlineAllocator<2>> Facades;
	EventSystemFacades.RemoveAll([](const TWeakObjectPtr<UDreamEventSystem>& Facade) { return !Facade.IsValid(); });
	for (const TWeakObjectPtr<UDreamEventSystem>& Facade : EventSystemFacades)
	{
		Facades.Add(Facade.Get());
	}
	return Facades;
}

void UDreamUIInputUser::RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& InHitResult, UDreamWidget* InHitWidget)
{
	if (!Config.bRayEventEnable)
	{
		return;
	}
	RaycastHitEvent.Broadcast(bHitOrNot, InHitResult, InHitWidget);
	for (UDreamEventSystem* Facade : GetEventSystemFacades())
	{
		Facade->BroadcastBlueprintRaycastHit(bHitOrNot, InHitResult, InHitWidget);
	}
}

void UDreamUIInputUser::LogEventData(UDreamBaseEventData* InEventData) const
{
#if WITH_EDITORONLY_DATA
	if (Config.bOutputLog && InEventData != nullptr)
	{
		UE_LOG(DreamGUI, Log, TEXT("%s"), *InEventData->ToString());
	}
#endif
}

// Each is its dispatch -- the handlers on the widget, bubbling where the event bubbles -- and then the
// broadcasts, with the dispatch counted while it runs so that what a handler changes waits for it to end.
#define DREAMUI_CALL_ON(Name, EventDataType, Bubble) \
void UDreamUIInputUser::CallOnPointer##Name(UDreamWidget* InWidget, EventDataType* InEventData) \
{ \
	if (InEventData == nullptr || !IsValid(InWidget)) return; \
	FDreamUIInputDispatchScope Dispatch(this); \
	LogEventData(InEventData); \
	UDreamEventSystem::ExecuteEvent_OnPointer##Name(InWidget, InEventData, Bubble); \
	BroadcastInputEvent(InEventData); \
}

DREAMUI_CALL_ON(Enter, UDreamPointerEventData, false)
DREAMUI_CALL_ON(Exit, UDreamPointerEventData, false)
DREAMUI_CALL_ON(Down, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Up, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Click, UDreamPointerEventData, true)
DREAMUI_CALL_ON(DoubleClick, UDreamPointerEventData, true)
DREAMUI_CALL_ON(LongPress, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Pinch, UDreamGestureEventData, true)
DREAMUI_CALL_ON(Swipe, UDreamGestureEventData, true)
DREAMUI_CALL_ON(BeginDrag, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Drag, UDreamPointerEventData, true)
DREAMUI_CALL_ON(EndDrag, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Scroll, UDreamPointerEventData, true)
DREAMUI_CALL_ON(DragDrop, UDreamPointerEventData, true)
DREAMUI_CALL_ON(Select, UDreamBaseEventData, false)
DREAMUI_CALL_ON(Deselect, UDreamBaseEventData, false)
#undef DREAMUI_CALL_ON

namespace DreamUIInputUserLocal
{
	/**
	 * ExecuteDreamUIInterface for an actor: what a world ray hit that is not a widget.
	 *
	 * The actor first, then each of its components, every one that implements the interface. Bubbling follows
	 * the widget rule: only when the caller allows it and no handler said stop, and up the attachment chain,
	 * which is the nearest thing an actor has to a parent widget. A copy of the component list is walked,
	 * because a handler may add or remove components on the actor it was handed.
	 */
	template<class UEventData, class UInterfaceFunction>
	void ExecuteDreamUIInterfaceOnActor(AActor* InActor, UEventData* InEventData, UClass* InInterfaceClass,
		UInterfaceFunction InInterfaceFunction, bool bInAllowEventBubbleUp)
	{
		if (!IsValid(InActor))
		{
			return;
		}
		bool bBubbleUp = bInAllowEventBubbleUp;
		if (InActor->GetClass()->ImplementsInterface(InInterfaceClass))
		{
			if (InInterfaceFunction(InActor, InEventData) == false)
			{
				bBubbleUp = false;
			}
		}
		TInlineComponentArray<UActorComponent*> Components(InActor);
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component))continue;
			if (Component->GetClass()->ImplementsInterface(InInterfaceClass))
			{
				if (InInterfaceFunction(Component, InEventData) == false)
				{
					bBubbleUp = false;
				}
			}
		}
		// Asked again: a handler may have destroyed the actor it was dispatched to.
		if (bBubbleUp && IsValid(InActor))
		{
			if (AActor* ParentActor = InActor->GetAttachParentActor())
			{
				ExecuteDreamUIInterfaceOnActor(ParentActor, InEventData, InInterfaceClass, InInterfaceFunction, true);
			}
		}
	}
}

// Each one what its CallOnPointer* counterpart is -- event type, bubbling, broadcasts -- with the actor in
// place of the widget. The broadcast matters to more than the Blueprint: the tooltip listens for Enter, Exit
// and Down, and a press on a surface dismisses a tooltip as a press anywhere does.
#define DREAMUI_CALL_ON_WORLD(Name, EventTypeName, InterfaceName, FunctionName, Bubble) \
void UDreamUIInputUser::CallOnWorldTarget##Name(AActor* InTarget, UDreamPointerEventData* InEventData) \
{ \
	if (InEventData == nullptr) return; \
	FDreamUIInputDispatchScope Dispatch(this); \
	InEventData->EventType = EDreamUIPointerEventType::EventTypeName; \
	LogEventData(InEventData); \
	DreamUIInputUserLocal::ExecuteDreamUIInterfaceOnActor(InTarget, InEventData, \
		U##InterfaceName::StaticClass(), I##InterfaceName::Execute_##FunctionName, Bubble); \
	BroadcastInputEvent(InEventData); \
}

DREAMUI_CALL_ON_WORLD(Enter, Enter, DreamPointerEnterExitInterface, OnPointerEnter, false)
DREAMUI_CALL_ON_WORLD(Exit, Exit, DreamPointerEnterExitInterface, OnPointerExit, false)
DREAMUI_CALL_ON_WORLD(Down, Down, DreamPointerDownUpInterface, OnPointerDown, true)
DREAMUI_CALL_ON_WORLD(Up, Up, DreamPointerDownUpInterface, OnPointerUp, true)
DREAMUI_CALL_ON_WORLD(Click, Click, DreamPointerClickInterface, OnPointerClick, true)
DREAMUI_CALL_ON_WORLD(DoubleClick, DoubleClick, DreamPointerDoubleClickInterface, OnPointerDoubleClick, true)
DREAMUI_CALL_ON_WORLD(LongPress, LongPress, DreamPointerLongPressInterface, OnPointerLongPress, true)
DREAMUI_CALL_ON_WORLD(Scroll, Scroll, DreamPointerScrollInterface, OnPointerScroll, true)
#undef DREAMUI_CALL_ON_WORLD
#pragma endregion

#pragma region Frame
void UDreamUIInputUser::ProcessFrame(float InDeltaSeconds)
{
	if (bShutDown || !Config.bRayEventEnable)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_DreamUIInputUserFrame);
	if (UDreamBaseInputModule* Module = InputModule.Get())
	{
		// Through the module, whose ProcessInput is RunPipeline unless a subclass adds to it.
		Module->ProcessInput();
	}
	else
	{
		RunPipeline();
	}
}

void UDreamUIInputUser::RunPipeline()
{
	if (!ensureMsgf(!bInPipeline, TEXT("%s: the input pipeline was asked to run from inside itself -- a handler called ProcessInput. Refused; the pipeline is not re-entrant."), *GetName()))
	{
		return;
	}
	if (bShutDown)
	{
		return;
	}
	TGuardValue<bool> InPipeline(bInPipeline, true);
	RunPipelineBody();
}

void UDreamUIInputUser::QueuePointerButton(int32 InPointerID, const FVector& InPosition, bool bInPressed, EDreamUIMouseButtonType InButton, bool bInIsTouch)
{
	if (bShutDown)
	{
		return;
	}
	if (!Config.bRayEventEnable)
	{
		// A player whose tracing is off takes no press: kept for later, it would land the frame tracing comes back, on
		// whatever the pointer is over by then. The release of a pointer still held ends its press now, with its up
		// and without a click or a drop -- nothing is traced where it was let go -- rather than waiting in the queue to
		// land wherever the pointer is when tracing comes back.
		const UDreamPointerEventData* Held = FindPointerEventData(InPointerID);
		if (!bInPressed && Held != nullptr && Held->bNowIsTriggerPressed)
		{
			CancelPointerPress(InPointerID);
		}
		return;
	}
	if (bInPressed)
	{
		NoteInputFromKeys(false);//a press is a pointer's: code that moves the focus after it does not draw it
	}
	FQueuedButton& Queued = QueuedButtons.AddDefaulted_GetRef();
	Queued.PointerID = InPointerID;
	Queued.Position = InPosition;
	Queued.bPressed = bInPressed;
	Queued.Button = InButton;
	Queued.bIsTouch = bInIsTouch;
	// Stamped on the pointer clock, the one long press, hold-to-drag, swipe duration and the double-click
	// window measure against (UDreamEventSystem::GetPointerClockSeconds).
	Queued.ClockSeconds = UDreamEventSystem::GetPointerClockSeconds(GetWorld());
}

void UDreamUIInputUser::MovePointer(int32 InPointerID, const FVector& InPosition)
{
	UDreamPointerEventData* EventData = GetPointerEventData(InPointerID, true);
	if (EventData == nullptr)
	{
		return;
	}
	// MOVING IS pointer input -- InputType is a sticky mode bit, and a pointer in navigation mode is not traced -- but
	// it has to be an ACTUAL move: the preset actors report the position every frame regardless.
	if (!InPosition.Equals(EventData->PointerPosition))
	{
		SetPointerInputType(EventData, EDreamUIPointerInputType::Pointer);
		// And only an actual move lets the pointer's hover take the navigation highlight on its next trace.
		PointersMovedSinceTrace.Add(InPointerID);
	}
	EventData->PointerPosition = InPosition;
}

void UDreamUIInputUser::InputNavigation(EDreamUINavigationDirection InDirection, bool bInPressed, int32 InPointerID)
{
	UDreamPointerEventData* EventData = GetPointerEventData(InPointerID, true);
	if (EventData == nullptr)
	{
		return;
	}
	// Every direction held is remembered, so letting go of one is not letting go of all: a stick or a D-pad rolled
	// from Down to Right goes Down pressed, Right pressed, Down released, and Right is still held. Each direction is
	// held once, the newest last: one pressed again -- by a second key, or by a caller that says so every frame -- is
	// the newest again, and one release lets go of it. None is no direction: pressing it holds nothing, and letting go
	// of it lets go of every direction.
	TArray<EDreamUINavigationDirection>& Held = EventData->HeldNavigateDirections;
	if (bInPressed)
	{
		SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
		if (InDirection != EDreamUINavigationDirection::None)
		{
			Held.Remove(InDirection);
			Held.Add(InDirection);
			EventData->NavigateDirection = InDirection;
			EventData->NavigateTickTime = 0;
		}
		return;
	}
	if (InDirection == EDreamUINavigationDirection::None)
	{
		Held.Reset();
	}
	else if (Held.Remove(InDirection) == 0)
	{
		return;//nothing holds it: the release of a press this pointer never took
	}
	const EDreamUINavigationDirection StillHeld = Held.Num() > 0 ? Held.Last() : EDreamUINavigationDirection::None;
	if (StillHeld == EventData->NavigateDirection)
	{
		return;//another key still holds the direction that is stepping, or the one let go of was not stepping
	}
	EventData->NavigateDirection = StillHeld;
	// Back to a direction that was held all along goes on at the repeat the step it took already set; with none
	// left, the next press is the first of a new sequence.
	if (StillHeld == EDreamUINavigationDirection::None)
	{
		EventData->NavigateTickTime = 0;
	}
}

void UDreamUIInputUser::InputTriggerForNavigation(bool bInPressed, int32 InPointerID)
{
	UDreamPointerEventData* EventData = GetPointerEventData(InPointerID, true);
	if (EventData == nullptr)
	{
		return;
	}
	if (bInPressed)
	{
		SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
	}
	// The confirm's own edges dated on the pointer clock, and its press placed where the pointer is, as a queued press
	// and release are (QueuePointerButton). Long press and swipe read these, and a confirm that left them as the
	// mouse's last press set them measured its hold from that press -- a long press at the first step -- and its
	// travel from where that press was made -- a swipe on every confirm.
	if (bInPressed != EventData->bNowIsTriggerPressed)
	{
		const double ClockSeconds = UDreamEventSystem::GetPointerClockSeconds(GetWorld());
		if (bInPressed)
		{
			EventData->PressTime = ClockSeconds;
			EventData->PressPointerPosition = EventData->PointerPosition;
		}
		else
		{
			EventData->ReleaseTime = ClockSeconds;
		}
	}
	EventData->NavigateTickTime = 0;
	EventData->bNowIsTriggerPressed = bInPressed;
}

void UDreamUIInputUser::QueuePointerScroll(int32 InPointerID, const FVector2D& InAxisValue)
{
	if (bShutDown || !Config.bRayEventEnable)
	{
		return;
	}
	if (!InAxisValue.IsZero())
	{
		NoteInputFromKeys(false);//the wheel is the mouse's
	}
	QueuedScrolls.Add({ InPointerID, InAxisValue });
}

void UDreamUIInputUser::RunPipelineBody()
{
	// Whatever else this frame does, the pinch recognizer sees the pointers as they end up.
	ON_SCOPE_EXIT{ ProcessPinchGesture(); };

	ReleasePressesWhoseRaycasterWent();
	// Before anything is traced, so a press lost to a move under another parent ends where its drag last was: the ray
	// a slider reads its value from is still last frame's.
	ReleasePressesWhosePathBroke();

	TSet<int32, DefaultKeyFuncs<int32>, TInlineSetAllocator<8>> TracedByQueue;

	// A step asked for from inside a key handler -- the text field Tab commits -- before the pointers: it was asked for
	// by the keys the player pressed since the last frame. Its pointer takes no second step this frame.
	if (RequestedNavigationStep != EDreamUINavigationDirection::None)
	{
		RunRequestedNavigationStep();
		TracedByQueue.Add(DreamUIPointerIds::Mouse);
	}

	// The presses and releases, in the order they arrived. Taken by value first: dispatch runs game code, and game
	// code that presses or releases anything queues more -- which is simply the next frame's. Each is one record:
	// what a handler changes of the pointers waits for the record to end.
	if (QueuedButtons.Num() > 0)
	{
		TArray<FQueuedButton> FrameButtons = MoveTemp(QueuedButtons);
		QueuedButtons.Reset();
		for (const FQueuedButton& Queued : FrameButtons)
		{
			if (bShutDown)
			{
				break;
			}
			FDreamUIInputDispatchScope Record(this);
			UDreamPointerEventData* EventData = GetPointerEventData(Queued.PointerID, true);
			if (!IsValid(EventData))
			{
				continue;
			}
			// A press or a release somewhere else than the pointer was is the pointer moving there: a tap is a finger's whole
			// journey, and what it lands on takes the highlight as a mouse's hover would.
			if (!Queued.Position.Equals(EventData->PointerPosition))
			{
				PointersMovedSinceTrace.Add(Queued.PointerID);
			}
			EventData->PointerPosition = Queued.Position;
			EventData->bNowIsTriggerPressed = Queued.bPressed;
			if (Queued.bPressed)
			{
				EventData->PressTime = Queued.ClockSeconds;
				EventData->PressPointerPosition = Queued.Position;
			}
			else
			{
				EventData->ReleaseTime = Queued.ClockSeconds;
			}
			EventData->MouseButtonType = Queued.Button;

			FDreamUIHitResultContainer HitContainer;
			const bool bLineTraceHitSomething = LineTrace(EventData, HitContainer);
			bool bResultHitSomething = false;
			FDreamUIHitResult HitResult;
			UDreamPointerInputModule::ProcessPointerEvent(this, EventData, bLineTraceHitSomething, HitContainer, bResultHitSomething, HitResult);
			PointersMovedSinceTrace.Remove(Queued.PointerID);
			NotePressRaycaster(EventData);
			NotePressPath(EventData);
			RaiseHitEvent(bResultHitSomething, HitResult, HitResult.Widget.Get());
			TracedByQueue.Add(Queued.PointerID);

			// A lifted finger is not a pointer any more: it would stay in the map holding whatever it last touched
			// in hover, and be traced every frame from where it left the glass. Its click run is kept, so the next tap
			// of the same finger can be the second of a double tap.
			if (Queued.bIsTouch && !Queued.bPressed)
			{
				ReleasePointerNow(Queued.PointerID);//fires the Exit the release itself does not
				KeepLiftedFingerClickRun(FindPointerEventData(Queued.PointerID));
				PointerEventDataMap.Remove(Queued.PointerID);
				PointerWorldTargetMap.Remove(Queued.PointerID);
				TraceCache.Remove(Queued.PointerID);
				PressRaycasters.Remove(Queued.PointerID);
				PointersMovedSinceTrace.Remove(Queued.PointerID);
			}
		}
	}

	// Every pointer's own frame, in id order so the order does not hang on the map's hashing -- the pointers the
	// queue already traced this frame aside. Re-resolved one at a time: a handler for one pointer may have taken
	// another away, and a pointer that appears mid-frame is simply the next frame's.
	TArray<int32> PointerIDs;
	PointerEventDataMap.GenerateKeyArray(PointerIDs);
	PointerIDs.Sort();
	for (const int32 PointerID : PointerIDs)
	{
		if (bShutDown)
		{
			break;
		}
		if (TracedByQueue.Contains(PointerID))
		{
			continue;
		}
		UDreamPointerEventData* EventData = FindPointerEventData(PointerID);
		if (!IsValid(EventData))
		{
			continue;
		}
		FDreamUIInputDispatchScope Record(this);
		switch (EventData->InputType)
		{
		default:
		case EDreamUIPointerInputType::Pointer:
		{
			FDreamUIHitResultContainer HitContainer;
			const bool bLineTraceHitSomething = LineTrace(EventData, HitContainer);
			bool bResultHitSomething = false;
			FDreamUIHitResult HitResult;
			UDreamPointerInputModule::ProcessPointerEvent(this, EventData, bLineTraceHitSomething, HitContainer, bResultHitSomething, HitResult);
			NotePressRaycaster(EventData);
			NotePressPath(EventData);
			RaiseHitEvent(bResultHitSomething, HitResult, HitResult.Widget.Get());
			break;
		}
		case EDreamUIPointerInputType::Navigation:
			ProcessInputForNavigation(EventData);
			break;
		}
		// Traced: whatever it enters from now on, until it moves again, it enters with the pointer at rest.
		PointersMovedSinceTrace.Remove(PointerID);
	}

	// The wheel turns, after the traces: a wheel goes to what the pointer is over now, not to what it was over
	// before it moved this frame.
	if (QueuedScrolls.Num() > 0)
	{
		TArray<FQueuedScroll> FrameScrolls = MoveTemp(QueuedScrolls);
		QueuedScrolls.Reset();
		for (const FQueuedScroll& Queued : FrameScrolls)
		{
			if (bShutDown)
			{
				break;
			}
			UDreamPointerEventData* EventData = FindPointerEventData(Queued.PointerID);
			if (!IsValid(EventData))
			{
				continue;
			}
			FDreamUIInputDispatchScope Record(this);
			if (IsValid(EventData->EnterWidget))
			{
				if (Queued.AxisValue != FVector2D::ZeroVector || EventData->ScrollAxisValue != Queued.AxisValue)
				{
					EventData->ScrollAxisValue = Queued.AxisValue;
					CallOnPointerScroll(EventData->EnterWidget, EventData);
				}
			}
			// Over no widget but over an actor, the wheel goes to what the pointer is over, by the same rule.
			else if (AActor* WorldTarget = GetHoveredWorldTarget(Queued.PointerID))
			{
				if (Queued.AxisValue != FVector2D::ZeroVector || EventData->ScrollAxisValue != Queued.AxisValue)
				{
					EventData->ScrollAxisValue = Queued.AxisValue;
					CallOnWorldTargetScroll(WorldTarget, EventData);
				}
			}
		}
	}
}
#pragma endregion

#pragma region Trace
bool UDreamUIInputUser::LineTrace(UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamHitResult)
{
	UWorld* World = GetWorld();
	UDreamUIManagerWorldSubsystem* Manager = World != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(World) : nullptr;
	if (Manager == nullptr || InPointerEventData == nullptr)
	{
		return false;
	}
	const bool bIsGamePaused = World->IsPaused();
	const int32 PointerID = InPointerEventData->PointerID;
	const uint64 Generation = Manager->GetHitTestGeneration();

	// A pointer at rest, over UI that has not changed since its last trace, hits what it hit then. Only for a
	// pointer that is not pressed -- a press, a drag and a long press all read what the trace says each frame --
	// and only when every raycaster that answered is one whose ray does not follow a camera.
	const bool bMayReuse = InPointerEventData->InputType == EDreamUIPointerInputType::Pointer
		&& !InPointerEventData->bNowIsTriggerPressed && !InPointerEventData->bPrevIsTriggerPressed
		&& !InPointerEventData->bIsDragging;
	if (bMayReuse)
	{
		if (const FTraceCache* Cache = TraceCache.Find(PointerID);
			Cache != nullptr && Cache->Generation == Generation && Cache->bPaused == bIsGamePaused
			&& Cache->Position == InPointerEventData->PointerPosition)
		{
			bool bStillStands = !Cache->bHit || Cache->Raycaster.IsValid();
			TArray<UDreamWidget*> Hovered;
			Hovered.Reserve(Cache->Hovered.Num());
			for (const TWeakObjectPtr<UDreamWidget>& Widget : Cache->Hovered)
			{
				if (!Widget.IsValid())
				{
					bStillStands = false;
					break;
				}
				Hovered.Add(Widget.Get());
			}
			if (bStillStands)
			{
				InPointerEventData->HoverComponentArray.Reset();
				for (UDreamWidget* Widget : Hovered)
				{
					InPointerEventData->HoverComponentArray.Add(Widget);
				}
				if (Cache->bHit)
				{
					OutDreamHitResult.HitResult = Cache->Hit;
					OutDreamHitResult.RayOrigin = Cache->RayOrigin;
					OutDreamHitResult.RayDirection = Cache->RayDirection;
					OutDreamHitResult.RayEnd = Cache->RayEnd;
					OutDreamHitResult.Raycaster = Cache->Raycaster.Get();
					OutDreamHitResult.HoverArray = MoveTemp(Hovered);
				}
				return Cache->bHit;
			}
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_LineTrace);
	++LineTraceCount;
	MultiHitResult.Reset();
	InPointerEventData->HoverComponentArray.Reset();
	bool bOnlyCameraFreeRaycasters = true;

	// A copy of the list: a raycast runs no game code today, but the list is the manager's and this is the one
	// place in the frame that walks it while calling out.
	const TArray<TWeakObjectPtr<UDreamBaseRaycaster>> Raycasters = Manager->GetAllRaycasterArray();
	FVector RayOrigin(0, 0, 0), RayDir(1, 0, 0), RayEnd(1, 0, 0);
	// Asked once per trace, and per hit only while some player's popups have a sheet up.
	const UDreamUIPopupLayer* PopupLayer = UDreamUIPopupLayer::Get(World);
	const bool bMayHitOtherSheets = PopupLayer != nullptr && PopupLayer->HasSheets();
	for (const TWeakObjectPtr<UDreamBaseRaycaster>& RaycasterItem : Raycasters)
	{
		UDreamBaseRaycaster* Raycaster = RaycasterItem.Get();
		if (!IsValid(Raycaster))continue;
		if (Raycaster->GetUserIndex() != UserIndex)continue;
		if (Raycaster->GetPointerID() != INDEX_NONE && Raycaster->GetPointerID() != PointerID)continue;
		if (bIsGamePaused && Raycaster->GetAffectByGamePause())continue;
		if (!Raycaster->IsA<UDreamScreenSpaceRaycaster>())
		{
			bOnlyCameraFreeRaycasters = false;
		}

		HitResultArray.Reset();
		Raycaster->Raycast(InPointerEventData, RayOrigin, RayDir, RayEnd, HitResultArray);
		// The ray this raycaster made for THIS pointer, for the drag helpers: the raycaster keeps only its latest.
		if (Raycaster == InPointerEventData->PressRaycaster)
		{
			InPointerEventData->SetPressRaycasterRay(RayOrigin, RayDir);
		}
		// A dragged widget is still raycastable and sits directly under the cursor; without this it wins its own
		// hit test every frame and the pointer never reaches what is underneath (see DreamPointerPolicy).
		HitResultArray.RemoveAll([InPointerEventData](const FDreamUIHitResult& InHit)
		{
			return DreamPointerPolicy::ShouldIgnoreHitWhileDragging(
				InHit.Widget.Get(), InPointerEventData->DragWidget, InPointerEventData->bIsDragging);
		});
		// The sheet behind another player's open popups stops that player's pointer only. Players can share a screen
		// root, where it can lie over this player's own popup: the presses on it would read as presses outside it and
		// close it, and the hover over it would go nowhere (UDreamUIPopupLayer::IsSheetOfAnotherPlayer).
		if (bMayHitOtherSheets)
		{
			const int32 SheetViewerIndex = UserIndex;
			HitResultArray.RemoveAll([PopupLayer, SheetViewerIndex](const FDreamUIHitResult& InSheetHit)
			{
				return PopupLayer->IsSheetOfAnotherPlayer(InSheetHit.Widget.Get(), SheetViewerIndex);
			});
		}
		if (HitResultArray.Num() > 0)
		{
			// An uninteractable widget in front occludes -- along the ray that struck it only, which is this
			// raycaster's own. Its results stay hidden; every other raycaster still runs. A hit with no widget is
			// a world occluder, which wins by distance and is dispatched to as a world target.
			UDreamWidget* TopHitWidget = HitResultArray[0].Widget.Get();
			if (TopHitWidget != nullptr && !TopHitWidget->GetInteractableInHierarchy())
			{
				continue;
			}
			FDreamUIHitResultContainer DreamHitResult;
			DreamHitResult.HitResult = HitResultArray[0];
			DreamHitResult.Raycaster = Raycaster;
			DreamHitResult.RayOrigin = RayOrigin;
			DreamHitResult.RayDirection = RayDir;
			DreamHitResult.RayEnd = RayEnd;
			for (const FDreamUIHitResult& HitItem : HitResultArray)
			{
				//a world occluder has no widget to hover; only real widgets belong in this list
				if (UDreamWidget* HoveredWidget = HitItem.Widget.Get())
				{
					DreamHitResult.HoverArray.Add(HoveredWidget);
				}
			}
			MultiHitResult.Add(MoveTemp(DreamHitResult));
		}
	}

	bool bHit = false;
	if (MultiHitResult.Num() > 0)
	{
		if (MultiHitResult.Num() > 1)
		{
			// Screen-space first. Two screen-space answers are two overlay canvases drawn over one another, and the
			// one drawn on top answers -- the higher canvas sort order, as within one raycaster's own answer
			// (UDreamBaseRaycaster::RaycastCandidates); a distance between them says nothing about which is on top.
			// Then by distance. Stable, so two raycasters' answers at one sort order and one distance keep the order
			// the raycasters are listed in.
			const auto SortOrderOf = [](const FDreamUIHitResultContainer& InHit)
			{
				const UDreamWidget* Widget = InHit.HitResult.Widget.Get();
				const UDreamCanvas* Canvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
				return Canvas != nullptr ? Canvas->GetActualSortOrder() : 0;
			};
			MultiHitResult.StableSort([&SortOrderOf](const FDreamUIHitResultContainer& A, const FDreamUIHitResultContainer& B)
			{
				const bool bAIsScreenSpace = A.Raycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass());
				const bool bBIsScreenSpace = B.Raycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass());
				if (bAIsScreenSpace != bBIsScreenSpace)
				{
					return bAIsScreenSpace;
				}
				if (bAIsScreenSpace)
				{
					const int32 ASortOrder = SortOrderOf(A);
					const int32 BSortOrder = SortOrderOf(B);
					if (ASortOrder != BSortOrder)
					{
						return ASortOrder > BSortOrder;
					}
				}
				return A.HitResult.Distance < B.HitResult.Distance;
			});
		}
		for (const FDreamUIHitResultContainer& HitResultItem : MultiHitResult)
		{
			for (UDreamWidget* HoverItem : HitResultItem.HoverArray)
			{
				InPointerEventData->HoverComponentArray.Add(HoverItem);
			}
		}
		OutDreamHitResult = MultiHitResult[0];
		bHit = true;

		// A world hit on an actor that shows a canvas of its own is carried into that canvas: a widget there is
		// what the pointer is over, served by this same pipeline (IDreamUINestedSurface).
		if (OutDreamHitResult.HitResult.Widget == nullptr && IsValid(OutDreamHitResult.Raycaster))
		{
			const UPrimitiveComponent* HitComponent = OutDreamHitResult.Raycaster->GetWorldHitComponent(OutDreamHitResult.HitResult);
			AActor* HitActor = HitComponent != nullptr ? HitComponent->GetOwner() : nullptr;
			if (IsValid(HitActor))
			{
				TInlineComponentArray<UActorComponent*> Components(HitActor);
				for (UActorComponent* Component : Components)
				{
					IDreamUINestedSurface* Surface = Cast<IDreamUINestedSurface>(Component);
					if (Surface == nullptr || !IsValid(Component))
					{
						continue;
					}
					FDreamUIHitResultContainer InnerHit;
					if (Surface->ResolveNestedHit(OutDreamHitResult, InPointerEventData, InnerHit))
					{
						bOnlyCameraFreeRaycasters = false;
						if (InnerHit.Raycaster != nullptr && InnerHit.Raycaster == InPointerEventData->PressRaycaster)
						{
							InPointerEventData->SetPressRaycasterRay(InnerHit.RayOrigin, InnerHit.RayDirection);
						}
						if (InnerHit.HitResult.Widget.IsValid())
						{
							InPointerEventData->HoverComponentArray.Reset();
							for (UDreamWidget* HoverItem : InnerHit.HoverArray)
							{
								InPointerEventData->HoverComponentArray.Add(HoverItem);
							}
							OutDreamHitResult = MoveTemp(InnerHit);
						}
						break;
					}
				}
			}
		}
	}

	if (bOnlyCameraFreeRaycasters)
	{
		FTraceCache& Cache = TraceCache.FindOrAdd(PointerID);
		Cache.Position = InPointerEventData->PointerPosition;
		Cache.Generation = Generation;
		Cache.bPaused = bIsGamePaused;
		Cache.bHit = bHit;
		Cache.Hit = bHit ? OutDreamHitResult.HitResult : FDreamUIHitResult();
		Cache.RayOrigin = OutDreamHitResult.RayOrigin;
		Cache.RayDirection = OutDreamHitResult.RayDirection;
		Cache.RayEnd = OutDreamHitResult.RayEnd;
		Cache.Raycaster = bHit ? OutDreamHitResult.Raycaster : nullptr;
		Cache.Hovered.Reset();
		for (const TObjectPtr<UDreamWidget>& Widget : InPointerEventData->HoverComponentArray)
		{
			Cache.Hovered.Add(Widget.Get());
		}
	}
	else
	{
		TraceCache.Remove(PointerID);
	}
	return bHit;
}
#pragma endregion

#pragma region Navigation
namespace DreamUIInputUserNavigationLocal
{
	/** The behaviour on InWidget that receives a move, when it can be moved to now. */
	UDreamUIBehaviour* FindReceiverOn(UDreamWidget* InWidget)
	{
		UDreamUIBehaviour* Receiver = DreamUINavigationScan::FindNavigationBehaviour(InWidget);
		return Receiver != nullptr && DreamUINavigationScan::CanNavigateTo(Receiver) ? Receiver : nullptr;
	}

	/**
	 * The behaviour a move from InFrom -- the player's focus -- starts on: InFrom's, else the nearest widget's above it
	 * that has one, chosen as DreamUINavigationScan::FindNavigationBehaviour chooses -- a widget navigation component with
	 * rules before a selectable, whatever order the components were added in, where the pipeline used to take whichever it
	 * reached first -- else the first one inside InFrom, depth first: a control given the focus itself, whose selectable
	 * lives on one of its parts (a button's face). Null when there is none.
	 */
	UDreamUIBehaviour* FindNavigationReceiver(UDreamWidget* InFrom)
	{
		if (!IsValid(InFrom))
		{
			return nullptr;
		}
		for (UDreamWidget* Walker = InFrom; IsValid(Walker); Walker = Walker->GetParent())
		{
			if (UDreamUIBehaviour* Receiver = FindReceiverOn(Walker))
			{
				return Receiver;
			}
		}
		TArray<UDreamWidget*, TInlineAllocator<16>> Pending;
		const TArray<UDreamWidget*>& FromChildren = InFrom->GetChildren();
		for (int32 Index = FromChildren.Num() - 1; Index >= 0; --Index)
		{
			Pending.Add(FromChildren[Index]);
		}
		while (Pending.Num() > 0)
		{
			UDreamWidget* Candidate = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Candidate) || !Candidate->GetWidgetActiveInHierarchy())
			{
				continue;
			}
			if (UDreamUIBehaviour* Receiver = FindReceiverOn(Candidate))
			{
				return Receiver;
			}
			const TArray<UDreamWidget*>& CandidateChildren = Candidate->GetChildren();
			for (int32 Index = CandidateChildren.Num() - 1; Index >= 0; --Index)
			{
				Pending.Add(CandidateChildren[Index]);
			}
		}
		return nullptr;
	}

	/** The player's default control's widget (UUISelectable::FindDefaultSelectable), or null. */
	UDreamWidget* FindDefaultTarget(UDreamUIInputUser* InUser)
	{
		UUISelectable* Default = UUISelectable::FindDefaultSelectable(InUser, InUser->GetUserIndex());
		return Default != nullptr ? Default->GetWidget() : nullptr;
	}

	/** What a focus moved by a step in InDirection is recorded as: Tab for the sequence, Navigation for a direction or the confirm. */
	EDreamUIFocusCause CauseOfStep(EDreamUINavigationDirection InDirection)
	{
		return InDirection == EDreamUINavigationDirection::Next || InDirection == EDreamUINavigationDirection::Prev
			? EDreamUIFocusCause::Tab : EDreamUIFocusCause::Navigation;
	}
}

bool UDreamUIInputUser::Navigate(EDreamUINavigationDirection InDirection, UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamUIHitResult)
{
	using namespace DreamUIInputUserNavigationLocal;
	// Every step starts from the player's focus, never from the hover highlight, which every hover rewrote: a Tab from the
	// first field with the mouse resting on the third went to the fourth, and the confirm pressed what the mouse had last
	// passed over rather than what the keys had reached.
	UDreamWidget* Focus = FocusedWidget.Get();
	if (!IsValid(Focus))
	{
		Focus = nullptr;
	}
	UDreamUIBehaviour* From = FindNavigationReceiver(Focus);
	UDreamWidget* Target = nullptr;
	const bool bSequential = InDirection == EDreamUINavigationDirection::Next || InDirection == EDreamUINavigationDirection::Prev;
	if (bSequential && UDreamGUISettings::Get()->TabOrder == EDreamUITabOrder::Hierarchy)
	{
		// The tab order: the next stop of the player's domain after the focus -- the first (the last, backwards) with nothing
		// focused or the focus outside it -- an explicit link first, and popups that close on Tab closed on the way.
		const FDreamUITabStep Step = FDreamUITabOrder::Step(this, UserIndex, Focus, InDirection == EDreamUINavigationDirection::Prev);
		Target = IsValid(Step.Target) ? Step.Target : nullptr;
		if (Target == nullptr && Step.bClosedPopups)
		{
			// The popups closed on the way and gave the focus back to their opener, with nowhere after it to go -- a dropdown
			// that is its screen's only control: the focus stays where the closing put it. What was focused before the step
			// is in a popup that is gone, and taking the focus back there would leave it on a hidden row.
			UDreamWidget* FocusAfterClose = FocusedWidget.Get();
			const UDreamUIBehaviour* ReceiverAfterClose = FindNavigationReceiver(IsValid(FocusAfterClose) ? FocusAfterClose : nullptr);
			Target = ReceiverAfterClose != nullptr ? ReceiverAfterClose->GetWidget() : nullptr;
		}
		else if (Target == nullptr)
		{
			// An end that holds, or nowhere to go: the focus stays where it is. With nothing focused and no stop in the
			// domain at all, the player's default control, as a direction would find it.
			Target = From != nullptr ? From->GetWidget() : (Focus == nullptr ? FindDefaultTarget(this) : nullptr);
		}
	}
	else if (From == nullptr)
	{
		// Nothing focused to move from. A direction lands on the player's default control -- and no further: that press only
		// finds where to begin. A confirm presses nothing: the keys act on the focus, and there is none.
		Target = InDirection != EDreamUINavigationDirection::None ? FindDefaultTarget(this) : nullptr;
	}
	else
	{
		UDreamUIBehaviour* Landing = From;
		TScriptInterface<IDreamNavigationInterface> NextNavigateInterface = nullptr;
		if (InDirection != EDreamUINavigationDirection::None && IDreamNavigationInterface::Execute_OnNavigate(From, InDirection, NextNavigateInterface))
		{
			if (UDreamUIBehaviour* NextNavigateObject = Cast<UDreamUIBehaviour>(NextNavigateInterface.GetObject()))
			{
				Landing = NextNavigateObject;
			}
		}
		Target = Landing->GetWidget();
	}
	if (!IsValid(Target))
	{
		return false;
	}
	OutDreamUIHitResult.HitResult.Widget = Target;
	OutDreamUIHitResult.HitResult.Location = Target->GetWorldLocation();
	OutDreamUIHitResult.HitResult.Normal = Target->GetWorldTransform().TransformVector(FVector(0, 0, 1));
	OutDreamUIHitResult.HitResult.Normal.Normalize();
	OutDreamUIHitResult.Raycaster = nullptr;
	OutDreamUIHitResult.HoverArray.Reset();

	InPointerEventData->HighlightWidgetForNavigation = Target;
	// Whatever the navigation policy picked, put it on screen -- only when a direction was actually given: a None
	// call is the confirm button resolving what it is pressing, and scrolling the view under the player at that
	// moment is not navigation.
	if (InDirection != EDreamUINavigationDirection::None && Config.bScrollNavigationTargetIntoView)
	{
		FDreamUINavigationScroll::RevealWidget(Target, Config.bAnimateNavigationScroll);
	}
	return true;
}

void UDreamUIInputUser::ProcessInputForNavigation(UDreamPointerEventData* EventData)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || EventData == nullptr)return;

	// Something has to be happening: a direction held, a confirm edge, or a confirm held down. A pointer parked in
	// navigation mode with nothing held takes no steps -- it used to re-run a step every interval forever,
	// reveal-scrolling its highlighted widget back into view under a player scrolling the list with the wheel.
	const bool bHasNavigateDirection = EventData->NavigateDirection != EDreamUINavigationDirection::None;
	const bool bTriggerStateChanged = EventData->bNowIsTriggerPressed != EventData->bPrevIsTriggerPressed;
	// A held confirm is a press under way, and a press is looked at on every frame it is held: that is where a held mouse
	// button becomes a long press, and a held confirm becomes one the same way, timed from its own press. Left until it
	// was let go, it never did.
	const bool bConfirmHeld = EventData->bNowIsTriggerPressed && EventData->bPrevIsTriggerPressed;
	if (!bHasNavigateDirection && !bTriggerStateChanged && !bConfirmHeld)
	{
		return;
	}

	// Real time, as Slate's key repeat is: holding a direction in a paused game's menu has to go on stepping.
	const double TimeSeconds = UDreamEventSystem::GetPointerClockSeconds(World);
	const bool bRepeatIsDue = TimeSeconds > EventData->NavigateTickTime;
	const bool bTakeNavigateStep = bHasNavigateDirection && bRepeatIsDue;
	if (!bTakeNavigateStep && !bTriggerStateChanged && !bConfirmHeld)
	{
		return;//direction held, but the repeat is not due yet
	}

	// One step per frame, and the next deadline measured from NOW: a hitch must not come back as a burst of
	// catch-up steps, and a zero interval must not become a deadline that can never be passed.
	if (bTakeNavigateStep)
	{
		const bool bIsFirstPressInSequence = EventData->NavigateTickTime == 0.0f;
		const float TimeInterval = bIsFirstPressInSequence ? Config.NavigateInputIntervalForFirstTime : Config.NavigateInputInterval;
		EventData->NavigateTickTime = TimeSeconds + FMath::Max(TimeInterval, MinNavigateInputInterval);
	}

	// None unless a step is due: the confirm button must not also move focus, and holding it is no step either. Navigate
	// still resolves the focused widget into the hit result, which is what Down/Up/Click are dispatched to, and it
	// reveal-scrolls only for a direction. A frame that only holds the confirm looks at its press, and announces nothing.
	const EDreamUINavigationDirection StepDirection = bTakeNavigateStep ? EventData->NavigateDirection : EDreamUINavigationDirection::None;
	RunNavigationFrame(EventData, StepDirection, bTakeNavigateStep || bTriggerStateChanged);
}

void UDreamUIInputUser::RunNavigationFrame(UDreamPointerEventData* EventData, EDreamUINavigationDirection InStepDirection, bool bInAnnounce)
{
	// Everything this frame does to the focus -- the step's landing, and whatever a handler of the press, the click or the
	// selection moves it on to -- is a key's or the pad's: recorded so, and drawn so (IsFocusVisible). The listeners hear
	// the answer once the step is over (declared first, so it runs after the cause is given back).
	ON_SCOPE_EXIT{ UpdateFocusVisible(); };
	TGuardValue<EDreamUIFocusCause> StepCause(StepFocusCause, DreamUIInputUserNavigationLocal::CauseOfStep(InStepDirection));
	FDreamUIHitResultContainer DreamUIHitResult;
	const bool bSelectValid = Navigate(InStepDirection, EventData, DreamUIHitResult);
	bool bResultHitSomething = false;
	FDreamUIHitResult HitResult;
	UDreamPointerInputModule::ProcessPointerEvent(this, EventData, bSelectValid, DreamUIHitResult, bResultHitSomething, HitResult);
	if (!bInAnnounce)
	{
		// A frame that only holds the confirm: its press was looked at, and nothing else moves. The focus stays where the
		// press or the last step put it -- or wherever game code has moved it since -- and no hit is announced again.
		return;
	}
	// The step's target, or what the confirm pressed, takes the focus -- while the cursor is still on it. A handler the
	// press or the click ran may have moved focus and cursor on together (a dropdown's chosen row giving them back to its
	// face, a dialog or a tab page taking them in), or taken the cursor off a widget it hid; selecting the pressed widget
	// regardless put the focus back on it, under whatever the handler had moved it to. A widget gone meanwhile is
	// nothing to select either.
	if (UDreamWidget* NavigatedWidget = HitResult.Widget.Get();
		bResultHitSomething && NavigatedWidget != nullptr && EventData->HighlightWidgetForNavigation.Get() == NavigatedWidget)
	{
		SetSelectWidgetForCause(NavigatedWidget, EventData, StepFocusCause);
	}
	RaiseHitEvent(bResultHitSomething, HitResult, HitResult.Widget.Get());
}

void UDreamUIInputUser::RunRequestedNavigationStep()
{
	const EDreamUINavigationDirection Direction = RequestedNavigationStep;
	RequestedNavigationStep = EDreamUINavigationDirection::None;
	// As a key step is: off in gameplay, as the key itself would be (DreamUIKeyRouting::HasBuiltInMeanings).
	if (Direction == EDreamUINavigationDirection::None || bShutDown || !DreamUIKeyRouting::HasBuiltInMeanings(this))
	{
		return;
	}
	UDreamPointerEventData* EventData = GetPointerEventData(DreamUIPointerIds::Mouse, true);
	if (!IsValid(EventData))
	{
		return;
	}
	FDreamUIInputDispatchScope Record(this);
	// The navigation cursor's, as a key's step: the pointer stops being the mouse's until the mouse moves it again.
	SetPointerInputType(EventData, EDreamUIPointerInputType::Navigation);
	RunNavigationFrame(EventData, Direction, true);
}

void UDreamUIInputUser::ProcessPinchGesture()
{
	// Exactly two pressed pointers. A third finger ends the pinch rather than being ignored: three fingers moving
	// is not a pinch, and picking two of them would be a guess the player cannot see or correct.
	UDreamPointerEventData* First = nullptr;
	UDreamPointerEventData* Second = nullptr;
	int32 PressedCount = 0;
	for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& KeyValue : PointerEventDataMap)
	{
		UDreamPointerEventData* Data = KeyValue.Value.Get();
		if (!IsValid(Data) || !Data->bNowIsTriggerPressed)continue;
		if (Data->InputType != EDreamUIPointerInputType::Pointer)continue;
		++PressedCount;
		if (First == nullptr) { First = Data; }
		else if (Second == nullptr) { Second = Data; }
	}
	if (PressedCount != 2 || First == nullptr || Second == nullptr)
	{
		bPinchActive = false;
		return;
	}
	//ordered by id so the pair does not swap between frames as the map rehashes
	if (First->PointerID > Second->PointerID)
	{
		Swap(First, Second);
	}

	const FVector2D PositionA(First->PointerPosition.X, First->PointerPosition.Y);
	const FVector2D PositionB(Second->PointerPosition.X, Second->PointerPosition.Y);
	const double Distance = FVector2D::Distance(PositionA, PositionB);
	if (!bPinchActive)
	{
		// The frame the second finger lands only establishes the baseline.
		bPinchActive = true;
		PinchStartDistance = Distance;
		PinchLastReportedDistance = Distance;
		return;
	}

	const double DistanceDelta = Distance - PinchLastReportedDistance;
	if (FMath::Abs(DistanceDelta) < (double)Config.PinchMinDistanceChange)
	{
		return;//two fingers resting on the glass are not pinching
	}
	PinchLastReportedDistance = Distance;

	if (!IsValid(PinchEventData))
	{
		PinchEventData = NewObject<UDreamGestureEventData>(this);
	}
	PinchEventData->GestureType = EDreamUIGestureType::Pinch;
	PinchEventData->UserIndex = First->UserIndex;
	PinchEventData->PointerIDs = { First->PointerID, Second->PointerID };
	PinchEventData->ScreenPosition = (PositionA + PositionB) * 0.5;
	PinchEventData->PinchDistance = (float)Distance;
	PinchEventData->PinchDistanceDelta = (float)DistanceDelta;
	PinchEventData->PinchScale = PinchStartDistance > UE_KINDA_SMALL_NUMBER ? (float)(Distance / PinchStartDistance) : 1.0f;
	// Dispatched where the first finger went down, the only widget the whole gesture can be said to belong to.
	UDreamWidget* GestureWidget = First->PressWidget != nullptr ? First->PressWidget.Get() : First->EnterWidget.Get();
	PinchEventData->Widget = GestureWidget;
	if (IsValid(GestureWidget))
	{
		CallOnPointerPinch(GestureWidget, PinchEventData);
	}
}
#pragma endregion

#pragma region LettingGo
void UDreamUIInputUser::ReleasePointer(int32 InPointerID)
{
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this), InPointerID]()
	{
		if (UDreamUIInputUser* This = WeakThis.Get())
		{
			This->ReleasePointerNow(InPointerID);
		}
	});
}

void UDreamUIInputUser::EndPressNow(UDreamPointerEventData* EventData)
{
	if (!EventData->bPrevIsTriggerPressed)
	{
		return;//no press is held
	}
	if (EventData->bIsDragging)
	{
		EventData->bIsDragging = false;
		if (!EventData->bIsEndDragFiredAtCurrentFrame)
		{
			EventData->bIsEndDragFiredAtCurrentFrame = true;
			if (IsValid(EventData->DragWidget))
			{
				UDreamWidget* DragWidget = EventData->DragWidget;
				EventData->DragWidget = nullptr;
				CallOnPointerEndDrag(DragWidget, EventData);
			}
			else if (UDreamDragDropOperation* EndedOperation = EventData->DragOperation.Get())
			{
				// With the source destroyed nobody else can deliver the cancel, and a drag that ends in silence
				// leaves every OnDragCancelled handler -- the one that puts the item back in its slot -- waiting.
				EndedOperation->NotifyDragCancelled();
			}
			EventData->DragOperation = nullptr;
		}
	}

	if (!EventData->bIsUpFiredAtCurrentFrame)
	{
		EventData->bIsUpFiredAtCurrentFrame = true;
		if (IsValid(EventData->PressWidget))
		{
			UDreamWidget* OldPressWidget = EventData->PressWidget;
			EventData->PressWidget = nullptr;
			CallOnPointerUp(OldPressWidget, EventData);
		}
		// A press on an actor is let go the same way: its up, and no click -- a pointer taken away is not a
		// pointer released over what it pressed.
		UDreamPointerInputModule::ReleaseWorldTarget(this, EventData, /*bInClick*/ false);
	}
	EventData->bNowIsTriggerPressed = false;
	EventData->bPrevIsTriggerPressed = false;
}

void UDreamUIInputUser::CancelPointerPress(int32 InPointerID)
{
	// Dropped now rather than when the cancel runs: a press still waiting for the next frame would land after it and
	// hold again, and a release still waiting would click or drop.
	QueuedButtons.RemoveAll([InPointerID](const FQueuedButton& InQueued) { return InQueued.PointerID == InPointerID; });
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this), InPointerID]()
	{
		UDreamUIInputUser* This = WeakThis.Get();
		UDreamPointerEventData* EventData = This != nullptr ? This->FindPointerEventData(InPointerID) : nullptr;
		if (EventData == nullptr)
		{
			return;
		}
		FDreamUIInputDispatchScope Record(This);
		This->EndPressNow(EventData);
		// A navigation confirm goes down on the pointer itself, not through the queue, and one the next frame has not
		// read yet is dropped the same way.
		EventData->bNowIsTriggerPressed = false;
		EventData->bPrevIsTriggerPressed = false;
		This->PressRaycasters.Remove(InPointerID);
	});
}

void UDreamUIInputUser::ReleasePointerNow(int32 InPointerID)
{
	UDreamPointerEventData* EventData = FindPointerEventData(InPointerID);
	if (EventData == nullptr)
	{
		return;
	}
	FDreamUIInputDispatchScope Record(this);
	EndPressNow(EventData);
	if (!EventData->bIsExitFiredAtCurrentFrame)
	{
		if (IsValid(EventData->EnterWidget) || EventData->EnterWidgetStack.Num() > 0)
		{
			UDreamPointerInputModule::ProcessPointerEnterExit(this, EventData, EventData->EnterWidget, nullptr);
		}
		// ...and so is the actor it was over, so every Enter an actor was sent still gets its Exit.
		UDreamPointerInputModule::ExitWorldTargetUnless(this, EventData, nullptr);
		EventData->bIsExitFiredAtCurrentFrame = true;
	}
	TraceCache.Remove(InPointerID);
	PressRaycasters.Remove(InPointerID);
}

void UDreamUIInputUser::NotePressRaycaster(const UDreamPointerEventData* InEventData)
{
	if (InEventData == nullptr)
	{
		return;
	}
	if (!InEventData->bNowIsTriggerPressed)
	{
		PressRaycasters.Remove(InEventData->PointerID);
	}
	else if (IsValid(InEventData->PressRaycaster))
	{
		PressRaycasters.FindOrAdd(InEventData->PointerID) = InEventData->PressRaycaster.Get();
	}
}

void UDreamUIInputUser::ReleasePressesWhoseRaycasterWent()
{
	// A press made through a raycaster that has gone since -- a render-target surface destroyed while one of its
	// buttons was held -- is let go of before anything else this frame: its up, and no click, since there is nothing
	// left to release it over. Left alone, the release landed on the widget it pressed as if the surface were there.
	TArray<int32, TInlineAllocator<4>> Orphaned;
	for (const TPair<int32, TWeakObjectPtr<UDreamBaseRaycaster>>& Pair : PressRaycasters)
	{
		if (!Pair.Value.IsValid())
		{
			Orphaned.Add(Pair.Key);
		}
	}
	for (const int32 PointerID : Orphaned)
	{
		PressRaycasters.Remove(PointerID);
		ReleasePointerNow(PointerID);
	}
}

void UDreamUIInputUser::NotePressPath(const UDreamPointerEventData* InEventData)
{
	if (InEventData == nullptr)
	{
		return;
	}
	UDreamWidget* Pressed = InEventData->PressWidget.Get();
	if (!InEventData->bNowIsTriggerPressed || !IsValid(Pressed))
	{
		PressPaths.Remove(InEventData->PointerID);
		return;
	}
	FPressPath* Known = PressPaths.Find(InEventData->PointerID);
	if (Known != nullptr && Known->Widget.Get(/*bEvenIfGarbage*/ true) == Pressed)
	{
		return;//the path is the one the press was taken along, not wherever the widget is now
	}
	FPressPath& Path = PressPaths.Add(InEventData->PointerID);
	Path.Widget = Pressed;
	Path.Parents.Reset();
	for (UDreamWidget* Walker = Pressed->GetParent(); Walker != nullptr; Walker = Walker->GetParent())
	{
		Path.Parents.Add(Walker);
	}
}

void UDreamUIInputUser::ReleasePressesWhosePathBroke()
{
	// A held press is lost the way Slate loses a mouse capture: once the path it was taken along no longer leads to the
	// widget (FSlateUser::GetCaptorPath lets go of a captor path that resolves Truncated) -- the widget moved under another
	// parent, taken off its parent, or destroyed. SButton answers the lost capture by releasing itself
	// (SButton::OnMouseCaptureLost), and its release then clicks nothing, the press being gone (OnMouseButtonUp); an
	// SSlider's drag is that capture and ends with it (SSlider::OnMouseCaptureLost), and the moves after it are not the
	// slider's. Here the same: the up, the drag ended, no click, and the button's own release later lets go of nothing.
	// A drag and drop under way is left to run, as Slate's is: once begun it is the operation's, not the source's capture.
	TArray<int32, TInlineAllocator<4>> Broken;
	TArray<int32, TInlineAllocator<4>> Stale;
	for (const TPair<int32, FPressPath>& Pair : PressPaths)
	{
		const UDreamPointerEventData* EventData = FindPointerEventData(Pair.Key);
		const FPressPath& Path = Pair.Value;
		const UDreamWidget* Pressed = EventData != nullptr ? EventData->PressWidget.Get() : nullptr;
		const UDreamWidget* Recorded = Path.Widget.Get(/*bEvenIfGarbage*/ true);
		if (EventData == nullptr || !EventData->bPrevIsTriggerPressed || EventData->DragOperation.Get() != nullptr
			|| (Pressed != Recorded && !(Pressed == nullptr && Recorded == nullptr)))
		{
			Stale.Add(Pair.Key);//no press held any more, another press, or a drag and drop's to end
			continue;
		}
		bool bStillLeads = Path.Widget.IsValid();
		if (bStillLeads)
		{
			int32 Index = 0;
			for (const UDreamWidget* Walker = Path.Widget->GetParent(); bStillLeads && Walker != nullptr; Walker = Walker->GetParent(), ++Index)
			{
				bStillLeads = Path.Parents.IsValidIndex(Index) && Path.Parents[Index].Get() == Walker;
			}
			bStillLeads = bStillLeads && Index == Path.Parents.Num();
		}
		if (!bStillLeads)
		{
			Broken.Add(Pair.Key);
		}
	}
	for (const int32 PointerID : Stale)
	{
		PressPaths.Remove(PointerID);
	}
	for (const int32 PointerID : Broken)
	{
		PressPaths.Remove(PointerID);
		UDreamPointerEventData* EventData = FindPointerEventData(PointerID);
		if (EventData == nullptr)
		{
			continue;
		}
		FDreamUIInputDispatchScope Record(this);
		// This frame's first event: what the last frame dispatched says nothing about it.
		EventData->bIsUpFiredAtCurrentFrame = false;
		EventData->bIsEndDragFiredAtCurrentFrame = false;
		EndPressNow(EventData);
		// A destroyed widget is told nothing, and is not left behind as the press either.
		EventData->PressWidget = nullptr;
		EventData->DragWidget = nullptr;
		PressRaycasters.Remove(PointerID);
	}
}

void UDreamUIInputUser::ReleaseAllPointers()
{
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this)]()
	{
		UDreamUIInputUser* This = WeakThis.Get();
		if (This == nullptr)
		{
			return;
		}
		// Ids first: letting go fires Exit/Up/EndDrag into game code, and game code can make pointers.
		TArray<int32> PointerIDs;
		This->PointerEventDataMap.GenerateKeyArray(PointerIDs);
		PointerIDs.Sort();
		for (const int32 PointerID : PointerIDs)
		{
			This->ReleasePointerNow(PointerID);
		}
	});
}

void UDreamUIInputUser::RetireAllPointers()
{
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this)]()
	{
		UDreamUIInputUser* This = WeakThis.Get();
		if (This == nullptr)
		{
			return;
		}
		TArray<int32> PointerIDs;
		This->PointerEventDataMap.GenerateKeyArray(PointerIDs);
		PointerIDs.Sort();
		for (const int32 PointerID : PointerIDs)
		{
			This->ReleasePointerNow(PointerID);
		}
		// Forgotten after every one has let go: an Exit handler may still ask about a pointer that has not.
		for (const int32 PointerID : PointerIDs)
		{
			This->PointerEventDataMap.Remove(PointerID);
			This->PointerWorldTargetMap.Remove(PointerID);
			This->TraceCache.Remove(PointerID);
			This->PressRaycasters.Remove(PointerID);
			This->PointersMovedSinceTrace.Remove(PointerID);
		}
	});
}

void UDreamUIInputUser::Shutdown()
{
	if (bShutDown)
	{
		return;
	}
	RunOrDefer([WeakThis = TWeakObjectPtr<UDreamUIInputUser>(this)]()
	{
		UDreamUIInputUser* This = WeakThis.Get();
		if (This == nullptr || This->bShutDown)
		{
			return;
		}
		// Everything still owed goes out now, while the world is whole: drag ends and cancels, ups, exits.
		TArray<int32> PointerIDs;
		This->PointerEventDataMap.GenerateKeyArray(PointerIDs);
		PointerIDs.Sort();
		for (const int32 PointerID : PointerIDs)
		{
			This->ReleasePointerNow(PointerID);
		}
		// Nobody is left to own the focus. Its handlers are owed their Deselect, through one of the player's pointers
		// when one is left, else through event data made for it.
		if (IsValid(This->FocusedWidget.Get()))
		{
			UDreamBaseEventData* EventData = This->FindPointerEventData(DreamUIPointerIds::Mouse);
			if (EventData == nullptr && PointerIDs.Num() > 0)
			{
				EventData = This->FindPointerEventData(PointerIDs[0]);
			}
			if (EventData == nullptr)
			{
				UDreamPointerEventData* Made = NewObject<UDreamPointerEventData>(This);
				Made->UserIndex = This->UserIndex;
				EventData = Made;
			}
			This->SetSelectWidget(nullptr, EventData);
		}
		This->FocusedWidget.Reset();
		// And nobody types: the keys the player's field held go back to the controller.
		This->TextTarget.Reset();
		This->PopTextKeys();
		This->RestoreHoverCursor();
		This->bShutDown = true;
		This->QueuedButtons.Reset();
		This->QueuedScrolls.Reset();
		This->PointerEventDataMap.Reset();
		This->PointerWorldTargetMap.Reset();
		This->TraceCache.Reset();
		This->PressRaycasters.Reset();
		This->PressPaths.Reset();
		This->KeyPresses.Reset();
		This->LiftedFingerClickRuns.Reset();
		This->PointersMovedSinceTrace.Reset();
		This->RequestedNavigationStep = EDreamUINavigationDirection::None;
		This->FocusCause = EDreamUIFocusCause::None;
		This->bPinchActive = false;
		This->InputModule.Reset();
	});
}
#pragma endregion
