// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIVirtualCursor.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "DreamGUI.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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

void UDreamUIVirtualCursorSubsystem::Deinitialize()
{
	DeactivateVirtualCursor();
	Super::Deinitialize();
}

TStatId UDreamUIVirtualCursorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUIVirtualCursorSubsystem, STATGROUP_Tickables);
}

UDreamStandaloneInputModule* UDreamUIVirtualCursorSubsystem::GetInputModule() const
{
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(GetWorld(), 0);
	return IsValid(EventSystem) ? Cast<UDreamStandaloneInputModule>(EventSystem->GetCurrentInputModule()) : nullptr;
}

void UDreamUIVirtualCursorSubsystem::EnsureAutoModeSubscribed()
{
	if (bAutoModeSubscribed || !UDreamGUISettings::Get()->bAutoVirtualCursorOnGamepad)
	{
		return;
	}
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(GetWorld(), 0);
	if (!IsValid(EventSystem))
	{
		return;
	}
	EventSystem->GetInputDeviceChangedEvent().AddUObject(this, &UDreamUIVirtualCursorSubsystem::HandleInputDeviceChanged);
	bAutoModeSubscribed = true;
	// The device the player is already holding counts too, not just the next switch.
	HandleInputDeviceChanged(EventSystem->GetCurrentInputDevice());
}

void UDreamUIVirtualCursorSubsystem::HandleInputDeviceChanged(EDreamUIInputDevice InDevice)
{
	if (InDevice == EDreamUIInputDevice::Gamepad)
	{
		ActivateVirtualCursor();
	}
	else
	{
		DeactivateVirtualCursor();
	}
}

void UDreamUIVirtualCursorSubsystem::ActivateVirtualCursor()
{
	if (bActive)
	{
		return;
	}
	UDreamStandaloneInputModule* Module = GetInputModule();
	if (Module == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[VirtualCursor] No standalone input module to drive; is the event system alive?"));
		return;
	}
	bActive = true;
	bConfirmDown = false;
	Module->SetOverrideMousePosition(true);
	FVector2D Start = FVector2D::ZeroVector;
	Module->GetMousePosition(Start);
	CursorPosition = Start;

	// The visual. A settings class when one is named, else the built-in square -- crude on purpose:
	// visible everywhere with zero assets, replaced the moment a project cares.
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (IsValid(ScreenRoot))
	{
		CursorHolder = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
		CursorHolder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
		CursorHolder->SetDisplayName(TEXT("DreamUIVirtualCursor"));

		UClass* CursorClass = UDreamGUISettings::LoadSettingClass(UDreamGUISettings::Get()->VirtualCursorClass, TEXT("VirtualCursorClass"));
		if (CursorClass == nullptr)
		{
			UDreamRectBlock* Block = CursorHolder->CreateNewVisual<UDreamRectBlock>();
			Block->SetColor(BuiltInCursorColor);
			CursorHolder->SetSizeDelta(FVector2D(BuiltInCursorSize, BuiltInCursorSize));
		}
		CursorHolder->SetParentBeforeRegister(ScreenRoot);
		RegisterDreamWidgetHierarchy(CursorHolder);
		if (CursorClass != nullptr)
		{
			CursorWidget = CreateDreamWidget(GetWorld(), CursorClass, CursorHolder);
			if (IsValid(CursorWidget))
			{
				CursorHolder->SetSizeDelta(FVector2D(CursorWidget->GetWidth(), CursorWidget->GetHeight()));
				CursorWidget->SetAnchoredPosition(FVector2D::ZeroVector);
			}
		}
		UDreamCanvas* Canvas = CursorHolder->GetComponent<UDreamCanvas>();
		if (!IsValid(Canvas))
		{
			Canvas = Cast<UDreamCanvas>(CursorHolder->AddComponent(UDreamCanvas::StaticClass()));
		}
		if (IsValid(Canvas))
		{
			Canvas->SetOverrideSorting(true);
			Canvas->SetSortOrder(CursorSortOrder, /*PropagateToChildrenCanvas*/true);
		}
		UpdateCursorVisualPosition();
	}
}

void UDreamUIVirtualCursorSubsystem::DeactivateVirtualCursor()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	if (UDreamStandaloneInputModule* Module = GetInputModule())
	{
		if (bConfirmDown)
		{
			Module->InputTrigger(FVector(CursorPosition.X, CursorPosition.Y, 0.0f), false);
		}
		Module->SetOverrideMousePosition(false);
	}
	bConfirmDown = false;
	DestroyCursorVisual();
}

void UDreamUIVirtualCursorSubsystem::Tick(float DeltaTime)
{
	EnsureAutoModeSubscribed();
	if (!bActive)
	{
		return;
	}
	UDreamStandaloneInputModule* Module = GetInputModule();
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (Module == nullptr || PlayerController == nullptr)
	{
		return;
	}

	float StickX = 0.0f;
	float StickY = 0.0f;
	PlayerController->GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick, StickX, StickY);
	const FVector2D Stick(StickX, StickY);
	if (!Stick.IsNearlyZero(0.08f))
	{
		// Viewport coordinates run top-left down, the stick runs up: Y flips.
		CursorPosition += FVector2D(Stick.X, -Stick.Y) * UDreamGUISettings::Get()->VirtualCursorSpeed * DeltaTime;
		FVector2D ViewportSize(1920.0f, 1080.0f);
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->GetViewportSize(ViewportSize);
		}
		CursorPosition.X = FMath::Clamp(CursorPosition.X, 0.0f, ViewportSize.X);
		CursorPosition.Y = FMath::Clamp(CursorPosition.Y, 0.0f, ViewportSize.Y);
		Module->SetOverridePointerPosition(CursorPosition);
		UpdateCursorVisualPosition();
	}

	// Confirm button = left mouse button, delivered on edges only.
	const bool bConfirmNow = PlayerController->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom)
		|| PlayerController->IsInputKeyDown(EKeys::Virtual_Accept);
	if (bConfirmNow != bConfirmDown)
	{
		bConfirmDown = bConfirmNow;
		Module->InputTrigger(FVector(CursorPosition.X, CursorPosition.Y, 0.0f), bConfirmNow);
	}
}

void UDreamUIVirtualCursorSubsystem::UpdateCursorVisualPosition()
{
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(CursorHolder) || !IsValid(ScreenRoot))
	{
		return;
	}
	UDreamCanvas* RootCanvas = ScreenRoot->GetComponent<UDreamCanvas>();
	if (!IsValid(RootCanvas))
	{
		return;
	}
	FVector2D InCanvas = FVector2D::ZeroVector;
	if (RootCanvas->ConvertPositionFromViewportToCanvas(CursorPosition, InCanvas))
	{
		// Bottom-left-origin out of the conversion, center-origin into the anchored position --
		// the tooltip's and drag visual's missing shift, third copy.
		InCanvas -= FVector2D(ScreenRoot->GetWidth() * 0.5f, ScreenRoot->GetHeight() * 0.5f);
		CursorHolder->SetAnchoredPosition(InCanvas);
	}
}

void UDreamUIVirtualCursorSubsystem::DestroyCursorVisual()
{
	if (IsValid(CursorHolder))
	{
		CursorHolder->DestroyWidget();
	}
	CursorHolder = nullptr;
	CursorWidget = nullptr;
}
