// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIVirtualCursor.h"
#include "Core/DreamUIManager.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"

namespace
{
	// Above everything, tooltip included: the pointer is the one thing that must never be covered.
	constexpr int32 CursorSortOrder = 31000;
	constexpr float BuiltInCursorSize = 22.0f;
	const FColor BuiltInCursorColor(250, 250, 250, 255);
}

UDreamUIVirtualCursorSubsystem* UDreamUIVirtualCursorSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	return IsValid(World) ? World->GetSubsystem<UDreamUIVirtualCursorSubsystem>() : nullptr;
}

bool UDreamUIVirtualCursorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UDreamUIVirtualCursorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamUIVirtualCursorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
	// Every player's device changes, from the moment the world has input. Whether the cursor follows the device is the
	// setting's call, asked when the device changes.
	if (UDreamUIInputSubsystem* Input = Collection.InitializeDependency<UDreamUIInputSubsystem>())
	{
		Input->GetOnInputDeviceChanged().AddUObject(this, &UDreamUIVirtualCursorSubsystem::HandleInputDeviceChanged);
		InputSubsystem = Input;
	}
}

void UDreamUIVirtualCursorSubsystem::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the world had
	// no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	Super::Deinitialize();
}

void UDreamUIVirtualCursorSubsystem::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	TArray<int32> UserIndices;
	UserStates.GetKeys(UserIndices);
	for (const int32 UserIndex : UserIndices)
	{
		DeactivateVirtualCursorForUser(UserIndex);
	}
	bTornDownForWorld = true;
	if (UDreamUIInputSubsystem* Input = InputSubsystem.Get())
	{
		Input->GetOnInputDeviceChanged().RemoveAll(this);
	}
	InputSubsystem.Reset();
	UserStates.Reset();
}

TStatId UDreamUIVirtualCursorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUIVirtualCursorSubsystem, STATGROUP_Tickables);
}

UDreamStandaloneInputModule* UDreamUIVirtualCursorSubsystem::GetInputModule(int32 InUserIndex) const
{
	const UDreamUIInputSubsystem* Input = InputSubsystem.Get();
	const UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
	return User != nullptr ? Cast<UDreamStandaloneInputModule>(User->GetInputModule()) : nullptr;
}

void UDreamUIVirtualCursorSubsystem::HandleInputDeviceChanged(int32 InUserIndex, EDreamUIInputDevice InDevice)
{
	if (!UDreamGUISettings::Get()->bAutoVirtualCursorOnGamepad)
	{
		return;
	}
	if (InDevice == EDreamUIInputDevice::Gamepad)
	{
		ActivateVirtualCursorForUser(InUserIndex);
	}
	else
	{
		DeactivateVirtualCursorForUser(InUserIndex);
	}
}

void UDreamUIVirtualCursorSubsystem::ActivateVirtualCursor()
{
	ActivateVirtualCursorForUser(0);
}

void UDreamUIVirtualCursorSubsystem::DeactivateVirtualCursor()
{
	DeactivateVirtualCursorForUser(0);
}

bool UDreamUIVirtualCursorSubsystem::IsVirtualCursorActive() const
{
	return IsVirtualCursorActiveForUser(0);
}

void UDreamUIVirtualCursorSubsystem::SetConfirmPressed(bool bInPressed)
{
	SetConfirmPressedForUser(0, bInPressed);
}

FVector2D UDreamUIVirtualCursorSubsystem::GetVirtualCursorPosition() const
{
	return GetVirtualCursorPositionForUser(0);
}

bool UDreamUIVirtualCursorSubsystem::IsVirtualCursorActiveForUser(int32 InUserIndex) const
{
	const FDreamUIVirtualCursorUserState* State = UserStates.Find(InUserIndex);
	return State != nullptr && State->bActive;
}

FVector2D UDreamUIVirtualCursorSubsystem::GetVirtualCursorPositionForUser(int32 InUserIndex) const
{
	const FDreamUIVirtualCursorUserState* State = UserStates.Find(InUserIndex);
	return State != nullptr ? State->CursorPosition : FVector2D::ZeroVector;
}

void UDreamUIVirtualCursorSubsystem::ActivateVirtualCursorForUser(int32 InUserIndex)
{
	if (bTornDownForWorld || IsVirtualCursorActiveForUser(InUserIndex))
	{
		return;
	}
	UDreamStandaloneInputModule* Module = GetInputModule(InUserIndex);
	if (Module == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[VirtualCursor] Player %d has no standalone input module to drive; is its event system alive?"), InUserIndex);
		return;
	}
	FDreamUIVirtualCursorUserState& State = UserStates.FindOrAdd(InUserIndex);
	State.bActive = true;
	State.bConfirmDown = false;
	Module->SetOverrideMousePosition(true);
	FVector2D Start = FVector2D::ZeroVector;
	Module->GetMousePosition(Start);
	State.CursorPosition = Start;

	// The visual, on this player's screen. A settings class when one is named, else the built-in square -- crude on
	// purpose: visible everywhere with zero assets, replaced the moment a project cares.
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	const UDreamUIInputSubsystem* Input = InputSubsystem.Get();
	const int32 ScreenIndex = Input != nullptr ? Input->GetScreenIndexForUser(InUserIndex) : InUserIndex;
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForUserIndex(ScreenIndex) : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return;
	}
	State.CursorHolder = NewObject<UDreamWidget>(this, NAME_None, RF_Transient);
	State.CursorHolder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	State.CursorHolder->SetDisplayName(TEXT("DreamUIVirtualCursor"));

	UClass* CursorClass = UDreamGUISettings::LoadSettingClass(UDreamGUISettings::Get()->VirtualCursorClass, TEXT("VirtualCursorClass"));
	if (CursorClass == nullptr)
	{
		UDreamRectBlock* Block = State.CursorHolder->CreateNewVisual<UDreamRectBlock>();
		Block->SetColor(BuiltInCursorColor);
		State.CursorHolder->SetSizeDelta(FVector2D(BuiltInCursorSize, BuiltInCursorSize));
	}
	State.CursorHolder->SetParentBeforeRegister(ScreenRoot);
	RegisterDreamWidgetHierarchy(State.CursorHolder);
	if (CursorClass != nullptr)
	{
		State.CursorWidget = CreateDreamWidget(GetWorld(), CursorClass, State.CursorHolder);
		if (IsValid(State.CursorWidget))
		{
			State.CursorHolder->SetSizeDelta(FVector2D(State.CursorWidget->GetWidth(), State.CursorWidget->GetHeight()));
			State.CursorWidget->SetAnchoredPosition(FVector2D::ZeroVector);
		}
	}
	UDreamCanvas* Canvas = State.CursorHolder->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(State.CursorHolder->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		Canvas->SetSortOrder(CursorSortOrder, /*PropagateToChildrenCanvas*/true);
	}
	UpdateCursorVisualPosition(InUserIndex, State);
}

void UDreamUIVirtualCursorSubsystem::DeactivateVirtualCursorForUser(int32 InUserIndex)
{
	FDreamUIVirtualCursorUserState* State = UserStates.Find(InUserIndex);
	if (State == nullptr || !State->bActive)
	{
		return;
	}
	State->bActive = false;
	if (UDreamStandaloneInputModule* Module = GetInputModule(InUserIndex))
	{
		if (State->bConfirmDown)
		{
			Module->InputTrigger(FVector(State->CursorPosition.X, State->CursorPosition.Y, 0.0f), false);
		}
		Module->SetOverrideMousePosition(false);
	}
	State->bConfirmDown = false;
	DestroyCursorVisual(*State);
}

void UDreamUIVirtualCursorSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	UDreamUIInputSubsystem* Input = InputSubsystem.Get();
	if (World == nullptr || Input == nullptr)
	{
		return;
	}
	// The UI clock: a cursor in a slowed-down game moves as fast as it does at full speed.
	const float UIDeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(this, DeltaTime);
	for (TPair<int32, FDreamUIVirtualCursorUserState>& Pair : UserStates)
	{
		FDreamUIVirtualCursorUserState& State = Pair.Value;
		if (!State.bActive)
		{
			continue;
		}
		const UDreamUIInputUser* User = Input->GetUser(Pair.Key);
		UDreamStandaloneInputModule* Module = GetInputModule(Pair.Key);
		// This player's stick, not the first controller's: a second player's cursor used to follow the first
		// player's thumb.
		APlayerController* PlayerController = User != nullptr ? User->GetPlayerController() : nullptr;
		if (Module == nullptr || PlayerController == nullptr)
		{
			continue;
		}
		float StickX = 0.0f;
		float StickY = 0.0f;
		PlayerController->GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick, StickX, StickY);
		const FVector2D Stick(StickX, StickY);
		if (!Stick.IsNearlyZero(0.08f))
		{
			// Viewport coordinates run top-left down, the stick runs up: Y flips.
			State.CursorPosition += FVector2D(Stick.X, -Stick.Y) * UDreamGUISettings::Get()->VirtualCursorSpeed * UIDeltaSeconds;
			FVector2D ViewportSize(1920.0f, 1080.0f);
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->GetViewportSize(ViewportSize);
			}
			State.CursorPosition.X = FMath::Clamp(State.CursorPosition.X, 0.0f, ViewportSize.X);
			State.CursorPosition.Y = FMath::Clamp(State.CursorPosition.Y, 0.0f, ViewportSize.Y);
			Module->SetOverridePointerPosition(State.CursorPosition);
			UpdateCursorVisualPosition(Pair.Key, State);
		}
		// The confirm button is NOT polled here: the preset actor pushes it through SetConfirmPressedForUser after
		// offering the key to the action router, so a claimed confirm never becomes a click.
	}
}

void UDreamUIVirtualCursorSubsystem::SetConfirmPressedForUser(int32 InUserIndex, bool bInPressed)
{
	FDreamUIVirtualCursorUserState* State = UserStates.Find(InUserIndex);
	if (State == nullptr || !State->bActive || bInPressed == State->bConfirmDown)
	{
		return;
	}
	UDreamStandaloneInputModule* Module = GetInputModule(InUserIndex);
	if (Module == nullptr)
	{
		return;
	}
	State->bConfirmDown = bInPressed;
	Module->InputTrigger(FVector(State->CursorPosition.X, State->CursorPosition.Y, 0.0f), bInPressed);
}

void UDreamUIVirtualCursorSubsystem::UpdateCursorVisualPosition(int32 InUserIndex, FDreamUIVirtualCursorUserState& State)
{
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	const UDreamUIInputSubsystem* Input = InputSubsystem.Get();
	const int32 ScreenIndex = Input != nullptr ? Input->GetScreenIndexForUser(InUserIndex) : InUserIndex;
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForUserIndex(ScreenIndex) : nullptr;
	if (!IsValid(State.CursorHolder) || !IsValid(ScreenRoot))
	{
		return;
	}
	UDreamCanvas* RootCanvas = ScreenRoot->GetComponent<UDreamCanvas>();
	if (!IsValid(RootCanvas))
	{
		return;
	}
	FVector2D InCanvas = FVector2D::ZeroVector;
	if (RootCanvas->ConvertPositionFromViewportToCanvas(State.CursorPosition, InCanvas))
	{
		// Bottom-left-origin out of the conversion, center-origin into the anchored position.
		InCanvas -= FVector2D(ScreenRoot->GetWidth() * 0.5f, ScreenRoot->GetHeight() * 0.5f);
		State.CursorHolder->SetAnchoredPosition(InCanvas);
	}
}

void UDreamUIVirtualCursorSubsystem::DestroyCursorVisual(FDreamUIVirtualCursorUserState& State)
{
	if (IsValid(State.CursorHolder))
	{
		State.CursorHolder->DestroyWidget();
	}
	State.CursorHolder = nullptr;
	State.CursorWidget = nullptr;
}
