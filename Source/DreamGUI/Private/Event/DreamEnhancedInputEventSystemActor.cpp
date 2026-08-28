// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamEnhancedInputEventSystemActor.h"

#include "DreamGUI.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#define LOCTEXT_NAMESPACE "DreamEnhancedInputEventSystemActor"

ADreamEnhancedInputEventSystemActor::ADreamEnhancedInputEventSystemActor()
{
	// Enhanced Input has no vector axis for absolute mouse position, so the position is read every
	// frame instead of on a movement event. The legacy preset can stay tickless; this one cannot.
	PrimaryActorTick.bCanEverTick = true;

	// No input-component override here: AActor::EnableInput builds one from
	// UInputSettings::GetDefaultInputComponentClass(), and OverrideInputComponentClass is APawn-only.
	// A project on Enhanced Input already has that set to UEnhancedInputComponent; BindMouseInput
	// says so plainly if it is not.

	// The four actions and the context are deliberately left empty and set on the Blueprint. Filling
	// them from a path here would be a hard reference to plugin content baked into the class, which
	// is exactly the pattern the rest of the plugin just moved away from.
}

void ADreamEnhancedInputEventSystemActor::BeginPlay()
{
	Super::BeginPlay();
	AddMappingContextToLocalPlayer();
}

void ADreamEnhancedInputEventSystemActor::AddMappingContextToLocalPlayer()
{
	if (!IsValid(MappingContext))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No MappingContext; the mouse actions will never fire."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No local player yet; the mapping context was not added."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	if (auto* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Subsystem->AddMappingContext(MappingContext, MappingContextPriority);
	}
}

void ADreamEnhancedInputEventSystemActor::BindMouseInput()
{
	// Deliberately does not call Super: the point of this class is that the mouse arrives through
	// Input Actions instead of raw keys, and binding both would deliver every click twice.
	auto* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d InputComponent is not a UEnhancedInputComponent, so no mouse input is bound. ")
			TEXT("Set DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent in DefaultInput.ini, ")
			TEXT("or place ADreamStandaloneInputEventSystemActor instead."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// Started and Completed rather than Triggered: these are boolean actions, and the module wants
	// the press and the release as separate calls, which is exactly what those two trigger events are.
	if (IsValid(TriggerLeftAction))
	{
		EnhancedInput->BindAction(TriggerLeftAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
		EnhancedInput->BindAction(TriggerLeftAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
	}
	if (IsValid(TriggerRightAction))
	{
		EnhancedInput->BindAction(TriggerRightAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
		EnhancedInput->BindAction(TriggerRightAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
	}
	if (IsValid(TriggerMiddleAction))
	{
		EnhancedInput->BindAction(TriggerMiddleAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
		EnhancedInput->BindAction(TriggerMiddleAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
	}
	if (IsValid(MouseWheelAction))
	{
		EnhancedInput->BindAction(MouseWheelAction, ETriggerEvent::Triggered, this, &ADreamEnhancedInputEventSystemActor::OnMouseWheelAction);
	}
}

void ADreamEnhancedInputEventSystemActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsValid(InputModule))
	{
		InputModule->InputMouseMove(GetPointerPosition());
	}
}

void ADreamEnhancedInputEventSystemActor::ForwardTrigger(const FInputActionValue& Value, EDreamUIMouseButtonType ButtonType)
{
	// These four actions are the mouse half of the preset, and an Input Action has no FKey to classify
	// -- the mapping context is what decided the key. Tick is deliberately left out: it polls movement
	// every frame regardless of whether the mouse moved, which would pin the device to the mouse.
	ReportDeviceForKey(EKeys::LeftMouseButton);
	if (IsValid(InputModule))
	{
		InputModule->InputTrigger(GetPointerPosition(), Value.Get<bool>(), ButtonType);
	}
}

void ADreamEnhancedInputEventSystemActor::OnTriggerLeft(const FInputActionValue& Value)
{
	ForwardTrigger(Value, EDreamUIMouseButtonType::Left);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerRight(const FInputActionValue& Value)
{
	ForwardTrigger(Value, EDreamUIMouseButtonType::Right);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerMiddle(const FInputActionValue& Value)
{
	ForwardTrigger(Value, EDreamUIMouseButtonType::Middle);
}

void ADreamEnhancedInputEventSystemActor::OnMouseWheelAction(const FInputActionValue& Value)
{
	const float AxisValue = Value.Get<float>();
	if (FMath::IsNearlyZero(AxisValue))
	{
		return;
	}
	ReportDeviceForKey(EKeys::MouseWheelAxis);
	if (IsValid(InputModule))
	{
		InputModule->InputScroll(FVector2D(AxisValue, AxisValue));
	}
}

#undef LOCTEXT_NAMESPACE
