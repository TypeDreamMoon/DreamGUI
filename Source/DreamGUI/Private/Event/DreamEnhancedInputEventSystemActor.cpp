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
	//
	// The Blueprint that fills them ships with the plugin:
	// /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput (IMC_DreamUIInputContext plus
	// IA_Trigger / IA_TriggerRight / IA_TriggerMiddle / IA_MouseWheel, all under /DreamGUI/EnhancedInput).
	// Set Project Settings > Plugins > Dream GUI > EventSystemActorClass to THAT, not to this class --
	// this class on its own has no mapping context and therefore no mouse.
}

void ADreamEnhancedInputEventSystemActor::BeginPlay()
{
	Super::BeginPlay();
	AddMappingContextToLocalPlayer();
}

void ADreamEnhancedInputEventSystemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveMappingContextFromLocalPlayer();
	Super::EndPlay(EndPlayReason);
}

UEnhancedInputLocalPlayerSubsystem* ADreamEnhancedInputEventSystemActor::GetEnhancedInputSubsystem()const
{
	// This actor's player, not merely the first one: the event system already knows whose input it
	// handles, and on a split screen the first controller is somebody else's.
	const UDreamEventSystem* Events = GetEventSystem();
	const APlayerController* PlayerController = Events != nullptr ? Events->GetPlayerController() : nullptr;
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void ADreamEnhancedInputEventSystemActor::AddMappingContextToLocalPlayer()
{
	if (!IsValid(MappingContext))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' has no MappingContext, so the mouse actions will never fire. This class ships with its context and actions empty for a Blueprint to fill; use /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput, or set MappingContext and the four action properties on your own subclass."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName());
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (Subsystem == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No local player yet; the mapping context was not added."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	Subsystem->AddMappingContext(MappingContext, MappingContextPriority);
}

void ADreamEnhancedInputEventSystemActor::RemoveMappingContextFromLocalPlayer()
{
	// The other half of AddMappingContext, which nothing anywhere in the plugin used to call. A
	// ULocalPlayer outlives the level it was playing, and so does its Enhanced Input subsystem: the
	// context pushed by the actor in one level was still mapping mouse buttons to this plugin's UI
	// actions in the next, where this actor no longer exists to receive them.
	if (!IsValid(MappingContext))return;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RemoveMappingContext(MappingContext);
	}
}

void ADreamEnhancedInputEventSystemActor::BindMouseInput()
{
	// Deliberately does not call Super: the point of this class is that the mouse arrives through
	// Input Actions instead of raw keys, and binding both would deliver every click twice.
	//
	// bConsumeBoundInput does not reach these four: an Input Action decides for itself whether it
	// consumes its keys, and that lives in the asset. The flag still governs the legacy navigation and
	// touch bindings inherited from the base, which this override does not replace.
	auto* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d InputComponent is not a UEnhancedInputComponent, so no mouse input is bound. ")
			TEXT("Set DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent in DefaultInput.ini, ")
			TEXT("or place ADreamStandaloneInputEventSystemActor instead."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// Started, Completed and Canceled rather than Triggered: these are boolean actions, and the module
	// wants the press and the release as separate calls.
	//
	// Canceled is not optional. Any trigger that can refuse to complete -- Hold, Tap, Pressed with a
	// chord -- ends there instead of at Completed, and binding only the other two meant the release was
	// never delivered: the pointer stayed logically down forever, which is a drag that can never be let
	// go of. It costs one more binding and the handler already knows which event it was.
	if (IsValid(TriggerLeftAction))
	{
		EnhancedInput->BindAction(TriggerLeftAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
		EnhancedInput->BindAction(TriggerLeftAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
		EnhancedInput->BindAction(TriggerLeftAction, ETriggerEvent::Canceled, this, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
	}
	if (IsValid(TriggerRightAction))
	{
		EnhancedInput->BindAction(TriggerRightAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
		EnhancedInput->BindAction(TriggerRightAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
		EnhancedInput->BindAction(TriggerRightAction, ETriggerEvent::Canceled, this, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
	}
	if (IsValid(TriggerMiddleAction))
	{
		EnhancedInput->BindAction(TriggerMiddleAction, ETriggerEvent::Started, this, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
		EnhancedInput->BindAction(TriggerMiddleAction, ETriggerEvent::Completed, this, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
		EnhancedInput->BindAction(TriggerMiddleAction, ETriggerEvent::Canceled, this, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
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

void ADreamEnhancedInputEventSystemActor::ReportDeviceForAction(const UInputAction* InAction)
{
	// An Input Action has no FKey of its own -- the mapping context decided that -- so this used to
	// report LeftMouseButton for all three buttons unconditionally, which told every prompt bar "mouse
	// and keyboard" even when the click had arrived from a gamepad face button through a pad mapping.
	// The keys mapped to the action are knowable, and the one the player is holding is the one that
	// produced this event.
	if (InAction == nullptr)return;
	const APlayerController* PlayerController = GetEventSystem() != nullptr ? GetEventSystem()->GetPlayerController() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (PlayerController == nullptr || Subsystem == nullptr)return;

	for (const FKey& Key : Subsystem->QueryKeysMappedToAction(InAction))
	{
		if (Key.IsValid() && PlayerController->IsInputKeyDown(Key))
		{
			ReportDeviceForKey(Key);
			return;
		}
	}
	// Nothing held: this is the release of a key the press already reported. Saying nothing leaves the
	// device where the press put it, which beats guessing between the keyboard and pad spellings.
}

void ADreamEnhancedInputEventSystemActor::ForwardTrigger(const FInputActionInstance& Instance, EDreamUIMouseButtonType ButtonType)
{
	// Which trigger event arrived is what says press or release. Reading it from the value instead
	// depended on EnhancedInput.bAlwaysGetRealValueFromActionInstanceData, and a Canceled -- the reason
	// the Canceled binding exists at all -- carries no value to read either way.
	const bool bPressed = Instance.GetTriggerEvent() == ETriggerEvent::Started;
	ReportDeviceForAction(Instance.GetSourceAction());
	if (IsValid(InputModule))
	{
		InputModule->InputTrigger(GetPointerPosition(), bPressed, ButtonType);
	}
}

void ADreamEnhancedInputEventSystemActor::OnTriggerLeft(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Left);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerRight(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Right);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerMiddle(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Middle);
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
