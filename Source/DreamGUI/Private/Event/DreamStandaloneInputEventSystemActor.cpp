// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamStandaloneInputEventSystemActor.h"

#include "Components/InputComponent.h"
#include "DreamGUI.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"

#define LOCTEXT_NAMESPACE "DreamStandaloneInputEventSystemActor"

namespace DreamStandaloneInputEventSystemActorLocal
{
	/** Every mouse button the preset forwards, and the button type each reports. */
	static const TPair<FKey, EDreamUIMouseButtonType> MouseButtons[] = {
		{ EKeys::LeftMouseButton,   EDreamUIMouseButtonType::Left },
		{ EKeys::RightMouseButton,  EDreamUIMouseButtonType::Right },
		{ EKeys::MiddleMouseButton, EDreamUIMouseButtonType::Middle },
	};

	/**
	 * Keys that act as "confirm" rather than as a direction.
	 *
	 * Gamepad and keyboard are bound together on purpose: navigation is pointer 0 either way, so a
	 * player can move with the stick and confirm with Enter in the same session.
	 */
	static const FKey NavigationTriggerKeys[] = {
		EKeys::Enter,
		EKeys::Gamepad_FaceButton_Bottom,
	};

	/** Direction keys, paired with the direction they mean. Read by GetNavigationDirectionForKey. */
	static const TPair<FKey, EDreamUINavigationDirection> NavigationDirectionKeys[] = {
		{ EKeys::Left,                     EDreamUINavigationDirection::Left },
		{ EKeys::Right,                    EDreamUINavigationDirection::Right },
		{ EKeys::Up,                       EDreamUINavigationDirection::Up },
		{ EKeys::Down,                     EDreamUINavigationDirection::Down },
		{ EKeys::Gamepad_LeftStick_Left,   EDreamUINavigationDirection::Left },
		{ EKeys::Gamepad_LeftStick_Right,  EDreamUINavigationDirection::Right },
		{ EKeys::Gamepad_LeftStick_Up,     EDreamUINavigationDirection::Up },
		{ EKeys::Gamepad_LeftStick_Down,   EDreamUINavigationDirection::Down },
	};

	/** Navigation is single-pointer; the Blueprint hard-coded 0 on every call and so does this. */
	static constexpr int32 NavigationPointerID = 0;
}

ADreamStandaloneInputEventSystemActor::ADreamStandaloneInputEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// The preset Blueprint carried a DefaultSceneRoot, and a placed actor still wants something to
	// hold a transform, so the root is kept rather than hanging the module off the event system.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(Root);

	// UDreamBaseInputModule is a UActorComponent, not a scene component -- there is nothing to attach.
	InputModule = CreateDefaultSubobject<UDreamStandaloneInputModule>(TEXT("DreamStandaloneInputModule"));

	// What makes this a drop-in: no project input setup, no possession, it just listens as player 0.
	AutoReceiveInput = EAutoReceiveInput::Player0;
}

void ADreamStandaloneInputEventSystemActor::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(InputModule))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d InputModule is missing; no input will reach the event system."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	InputModule->RegisterInputModuleToEventSystem(GetEventSystem());
	BindDreamInput();
}

void ADreamStandaloneInputEventSystemActor::BindDreamInput()
{
	// AutoReceiveInput has EnableInput build this during PreInitializeComponents, so by BeginPlay it
	// exists -- unless the actor was spawned with AutoReceiveInput cleared, which is a valid choice.
	if (!IsValid(InputComponent))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No InputComponent. Set AutoReceiveInput, or call EnableInput before BeginPlay."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	BindMouseInput();
	BindNavigationAndTouchInput();
}

void ADreamStandaloneInputEventSystemActor::BindMouseInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	for (const TPair<FKey, EDreamUIMouseButtonType>& Button : MouseButtons)
	{
		InputComponent->BindKey(Button.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonPressed);
		InputComponent->BindKey(Button.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonReleased);
	}

	InputComponent->BindVectorAxis(EKeys::Mouse2D, this, &ADreamStandaloneInputEventSystemActor::OnMouseMoved);
	InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ADreamStandaloneInputEventSystemActor::OnMouseWheel);
}

void ADreamStandaloneInputEventSystemActor::BindNavigationAndTouchInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	InputComponent->BindTouch(IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnTouchPressed);
	InputComponent->BindTouch(IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnTouchReleased);
	InputComponent->BindTouch(IE_Repeat, this, &ADreamStandaloneInputEventSystemActor::OnTouchMoved);

	for (const FKey& Key : NavigationTriggerKeys)
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerPressed);
		InputComponent->BindKey(Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased);
	}

	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : NavigationDirectionKeys)
	{
		InputComponent->BindKey(Direction.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed);
		InputComponent->BindKey(Direction.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased);
	}
}

EDreamUINavigationDirection ADreamStandaloneInputEventSystemActor::GetNavigationDirectionForKey(const FKey& Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : NavigationDirectionKeys)
	{
		if (Direction.Key == Key)
		{
			return Direction.Value;
		}
	}
	return EDreamUINavigationDirection::None;
}

FVector ADreamStandaloneInputEventSystemActor::GetPointerPosition() const
{
	FVector2D MousePosition = FVector2D::ZeroVector;
	if (IsValid(InputModule))
	{
		InputModule->GetMousePosition(MousePosition);
	}
	return FVector(MousePosition.X, MousePosition.Y, 0.0f);
}

void ADreamStandaloneInputEventSystemActor::ReportDeviceForKey(const FKey& Key)
{
	// Every bound handler goes through here. Which device the player has their hands on is only ever
	// visible at the moment a key arrives, and a prompt bar drawn from anything else is guessing.
	if (UDreamEventSystem* Events = GetEventSystem())
	{
		Events->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(Key));
	}
}

void ADreamStandaloneInputEventSystemActor::OnMouseButtonPressed(FKey Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	ReportDeviceForKey(Key);

	for (const TPair<FKey, EDreamUIMouseButtonType>& Button : MouseButtons)
	{
		if (Button.Key == Key)
		{
			InputModule->InputTrigger(GetPointerPosition(), true, Button.Value);
			return;
		}
	}
}

void ADreamStandaloneInputEventSystemActor::OnMouseButtonReleased(FKey Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	ReportDeviceForKey(Key);

	for (const TPair<FKey, EDreamUIMouseButtonType>& Button : MouseButtons)
	{
		if (Button.Key == Key)
		{
			InputModule->InputTrigger(GetPointerPosition(), false, Button.Value);
			return;
		}
	}
}

void ADreamStandaloneInputEventSystemActor::OnMouseMoved(FVector AxisValue)
{
	// A bound vector axis fires every frame whether or not the mouse moved, so only an actual delta
	// counts as the player using it -- reporting unconditionally would pin the device to the mouse and
	// no gamepad prompt would ever appear.
	if (!AxisValue.IsNearlyZero())
	{
		ReportDeviceForKey(EKeys::Mouse2D);
	}
	// The axis delta is only the wake-up; the module is asked for the absolute position, because that
	// is what the pointer API wants and what bOverrideMousePosition may have replaced.
	InputModule->InputMouseMove(GetPointerPosition());
}

void ADreamStandaloneInputEventSystemActor::OnMouseWheel(float AxisValue)
{
	if (FMath::IsNearlyZero(AxisValue))
	{
		return;//a resting wheel fires every frame; reporting it would pin the device to the mouse
	}
	ReportDeviceForKey(EKeys::MouseWheelAxis);
	// Both components carry the wheel value: InputScroll documents X as horizontal and Y as vertical,
	// and a mouse wheel has no horizontal axis to distinguish.
	InputModule->InputScroll(FVector2D(AxisValue, AxisValue));
}

void ADreamStandaloneInputEventSystemActor::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
	ReportDeviceForKey(EKeys::TouchKeys[FMath::Clamp((int32)FingerIndex, 0, (int32)EKeys::NUM_TOUCH_KEYS - 1)]);
	InputModule->InputTouchTrigger(true, static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
	InputModule->InputTouchTrigger(false, static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
	InputModule->InputTouchMoved(static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationTriggerPressed(FKey Key)
{
	ReportDeviceForKey(Key);
	InputModule->InputTriggerForNavigation(true, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased(FKey Key)
{
	InputModule->InputTriggerForNavigation(false, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed(FKey Key)
{
	ReportDeviceForKey(Key);
	InputModule->InputNavigation(
		GetNavigationDirectionForKey(Key), true, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased(FKey Key)
{
	// The direction is still passed on release even though the module ignores it there, so the two
	// handlers stay symmetric and a future module change cannot silently depend on a None here.
	InputModule->InputNavigation(
		GetNavigationDirectionForKey(Key), false, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

#undef LOCTEXT_NAMESPACE
