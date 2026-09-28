// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Components/InputComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "DreamGUI.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "DreamStandaloneInputEventSystemActor"

namespace DreamStandaloneInputEventSystemActorLocal
{
	/** Every mouse button the preset forwards, and the button type each reports. */
	static const TPair<FKey, EDreamUIMouseButtonType> MouseButtons[] = {
		{ EKeys::LeftMouseButton,   EDreamUIMouseButtonType::Left },
		{ EKeys::RightMouseButton,  EDreamUIMouseButtonType::Right },
		{ EKeys::MiddleMouseButton, EDreamUIMouseButtonType::Middle },
	};

	/** Navigation is single-pointer; the Blueprint hard-coded 0 on every call and so does this. */
	static constexpr int32 NavigationPointerID = 0;
	/** Below this the right stick is at rest; a bound axis reports every frame either way. */
	static constexpr float GamepadScrollDeadzone = 0.2f;

	/**
	 * The two rules every binding this preset makes is held to: it consumes its key only if the project
	 * asked for that, and it executes while the game is paused -- always.
	 *
	 * Always, because the engine's pause gate is the wrong place for the decision. UPlayerInput reads
	 * bExecuteWhenPaused off the binding, the binding is made once, and whether a paused game's UI
	 * answers is a project setting that can change at any moment and is read at the moment it matters
	 * everywhere else. Left at the engine's default of false, the flag made the preset deaf from the
	 * frame the game paused: a pause menu could not be clicked, whatever the setting said. The handlers
	 * ask the setting instead, as each key arrives (IsInputSuspendedByGamePause).
	 */
	static void ConfigurePresetBinding(FInputBinding& Binding, bool bInConsumeInput)
	{
		Binding.bConsumeInput = bInConsumeInput;
		Binding.bExecuteWhenPaused = true;
	}
}

ADreamStandaloneInputEventSystemActor::ADreamStandaloneInputEventSystemActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// The preset Blueprints used to carry a DefaultSceneRoot of their own, and a placed actor still
	// wants something to hold a transform, so the root is kept rather than hanging the module off the
	// event system. Both presets are data-only subclasses now and use this one; a subclass must not add
	// a component under either of these two names, or constructing it destroys the native one.
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

	SyncEventSystemUserIndexWithAutoReceiveInput();
	InputModule->RegisterInputModuleToEventSystem(GetEventSystem());
	BindDreamInput();
}

void ADreamStandaloneInputEventSystemActor::SyncEventSystemUserIndexWithAutoReceiveInput()
{
	// AutoReceiveInput is how a placed actor says whose keys it listens to, and the event system's
	// UserIndex is how everything downstream says whose pointer it is. They were two unrelated fields:
	// a second actor set to Player1 still produced pointers stamped player 0, and the cursor, the
	// prompts and the tooltip all went to the first player. One knob now, and it is the engine's own.
	//
	// Disabled means "the project will call EnableInput itself", and the project is then free to set
	// the index by hand -- so nothing is forced in that case.
	if (AutoReceiveInput == EAutoReceiveInput::Disabled)return;
	UDreamEventSystem* Events = GetEventSystem();
	if (Events == nullptr)return;

	const int32 PlayerIndex = (int32)AutoReceiveInput.GetValue() - 1;//Player0 is 1 in the enum
	if (PlayerIndex < 0)return;
	if (Events->GetUserIndex() != PlayerIndex)
	{
		Events->SetUserIndex(PlayerIndex);
	}
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

	// Everything below leaves bConsumeInput at bConsumeBoundInput, which is false unless a project asked
	// otherwise. Consuming here would not mean "the UI took this click": UPlayerInput adds a bound key
	// to the consume list every frame whether or not an event arrived for it, AnyKey stands in for every
	// key in the key state map, and AutoReceiveInput has already put this actor above the pawn -- so the
	// pawn would simply stop receiving input, Enhanced Input actions on those keys included. What the UI
	// actually swallows is decided per event, by whether the pointer is over it.
	//
	// Everything below also executes while the game is paused; whether it should is asked per key, by
	// the handlers (ConfigurePresetBinding says why the binding is the wrong place to ask).
	BindMouseInput();
	BindNavigationAndTouchInput();
	BindActionRouting();
}

void ADreamStandaloneInputEventSystemActor::BindMouseInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	for (const TPair<FKey, EDreamUIMouseButtonType>& Button : MouseButtons)
	{
		ConfigurePresetBinding(InputComponent->BindKey(Button.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonPressed),
			bConsumeBoundInput);
		ConfigurePresetBinding(InputComponent->BindKey(Button.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonReleased),
			bConsumeBoundInput);
	}

	ConfigurePresetBinding(InputComponent->BindVectorAxis(EKeys::Mouse2D, this, &ADreamStandaloneInputEventSystemActor::OnMouseMoved),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ADreamStandaloneInputEventSystemActor::OnMouseWheel),
		bConsumeBoundInput);
}

void ADreamStandaloneInputEventSystemActor::BindNavigationAndTouchInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	ConfigurePresetBinding(InputComponent->BindTouch(IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnTouchPressed),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindTouch(IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnTouchReleased),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindTouch(IE_Repeat, this, &ADreamStandaloneInputEventSystemActor::OnTouchMoved),
		bConsumeBoundInput);

	for (const FKey& Key : DreamUIKeyRouting::GetConfirmKeys())
	{
		ConfigurePresetBinding(InputComponent->BindKey(Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerPressed),
			bConsumeBoundInput);
		ConfigurePresetBinding(InputComponent->BindKey(Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased),
			bConsumeBoundInput);
	}

	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : DreamUIKeyRouting::GetDirectionKeys())
	{
		ConfigurePresetBinding(InputComponent->BindKey(Direction.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed),
			bConsumeBoundInput);
		ConfigurePresetBinding(InputComponent->BindKey(Direction.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased),
			bConsumeBoundInput);
	}

	// Paging. Bound by name like the directions, and for the same reason: a key this preset gives its
	// own meaning must not also reach the AnyKey handler, or the router would be offered it twice.
	for (const TPair<FKey, float>& Page : DreamUIKeyRouting::GetPageKeys())
	{
		ConfigurePresetBinding(InputComponent->BindKey(Page.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed),
			bConsumeBoundInput);
	}
	for (const TPair<FKey, bool>& Extent : DreamUIKeyRouting::GetExtentKeys())
	{
		ConfigurePresetBinding(InputComponent->BindKey(Extent.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed),
			bConsumeBoundInput);
	}
	// The right stick is the gamepad's wheel. Two axes rather than one vector key because there is no
	// Gamepad_Right2D, and an axis binding that reads zero every frame costs nothing.
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_RightX, this, &ADreamStandaloneInputEventSystemActor::OnGamepadScrollX),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_RightY, this, &ADreamStandaloneInputEventSystemActor::OnGamepadScrollY),
		bConsumeBoundInput);
	// The pad's other axes, for the focused widget alone (RouteAnalog): the left stick's directions are navigation,
	// which its direction keys already drive, and the triggers mean nothing to the preset by themselves.
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_LeftX, this, &ADreamStandaloneInputEventSystemActor::OnGamepadLeftX),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_LeftY, this, &ADreamStandaloneInputEventSystemActor::OnGamepadLeftY),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_LeftTriggerAxis, this, &ADreamStandaloneInputEventSystemActor::OnGamepadLeftTrigger),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindAxisKey(EKeys::Gamepad_RightTriggerAxis, this, &ADreamStandaloneInputEventSystemActor::OnGamepadRightTrigger),
		bConsumeBoundInput);
}

void ADreamStandaloneInputEventSystemActor::BindActionRouting()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	// The one binding that makes consumption catastrophic rather than merely wrong: UPlayerInput
	// expands AnyKey to every non-simulated key in the key state map, so consuming it takes the lot.
	// Executing while paused matters most here too: this is the road to the action router, where a
	// pause menu's own actions and hold-to-quit live, and to Back.
	ConfigurePresetBinding(InputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnAnyKeyPressed),
		bConsumeBoundInput);
	ConfigurePresetBinding(InputComponent->BindKey(EKeys::AnyKey, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnAnyKeyReleased),
		bConsumeBoundInput);
}

bool ADreamStandaloneInputEventSystemActor::ShouldReceiveInputWhilePaused()
{
	return DreamUIKeyRouting::ShouldReceiveInputWhilePaused();
}

bool ADreamStandaloneInputEventSystemActor::IsInputSuspendedByGamePause() const
{
	return DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld());
}

bool ADreamStandaloneInputEventSystemActor::IsStandingDownForSlate() const
{
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(this);
	return Input != nullptr && Input->IsSlateInputSourceActive();
}

UDreamUIInputUser* ADreamStandaloneInputEventSystemActor::GetInputUser() const
{
	const UDreamEventSystem* Events = GetEventSystem();
	return Events != nullptr ? Events->GetInputUser() : nullptr;
}

bool ADreamStandaloneInputEventSystemActor::IsNavigationKey(const FKey& Key)
{
	return DreamUIKeyRouting::IsNavigationKey(Key);
}

bool ADreamStandaloneInputEventSystemActor::RouteActionKey(const FKey& Key, bool bPressed)
{
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	UDreamEventSystem* Events = GetEventSystem();
	if (Router == nullptr || Events == nullptr)return false;
	return Router->HandleKey(Events->GetUserIndex(), Key, bPressed);
}

EDreamUINavigationDirection ADreamStandaloneInputEventSystemActor::GetNavigationDirectionForKey(const FKey& Key)
{
	return DreamUIKeyRouting::GetDirectionForKey(Key, false);
}

EDreamUINavigationDirection ADreamStandaloneInputEventSystemActor::ResolveNavigationDirection(const FKey& Key) const
{
	const EDreamUINavigationDirection Direction = GetNavigationDirectionForKey(Key);
	// Tab is the one key in the table whose meaning depends on a modifier, and a legacy binding fires
	// for Tab whether or not shift is down -- FKey carries no modifier state. The live shift state on
	// UPlayerInput is where the answer actually is at the moment the key arrives.
	if (Direction == EDreamUINavigationDirection::Next && Key == EKeys::Tab)
	{
		//this actor's player, not the first one: on a split screen the other player's shift is not ours
		const UDreamEventSystem* Events = GetEventSystem();
		const APlayerController* PlayerController = Events != nullptr ? Events->GetPlayerController() : nullptr;
		const UPlayerInput* Input = PlayerController != nullptr ? PlayerController->PlayerInput.Get() : nullptr;
		if (Input != nullptr && Input->IsShiftPressed())
		{
			return EDreamUINavigationDirection::Prev;
		}
	}
	return Direction;
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
	// Every handler starts here: the bindings execute while paused (ConfigurePresetBinding), so the
	// setting that decides whether a paused game's UI answers is asked now, before anything -- the
	// device report included -- as the engine would have dropped the whole call.
	if (ShouldIgnoreInput())return;
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
	if (ShouldIgnoreInput())return;
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
	//
	// While input is suspended by a pause the delta counts as none, which is what the engine hands an
	// axis binding that does not execute while paused: the device goes unreported, and the position
	// below is still passed on, as it always was -- a paused UI's raycasters are skipped anyway.
	if (IsStandingDownForSlate())
	{
		return;//the Slate source follows the mouse itself
	}
	if (!AxisValue.IsNearlyZero() && !IsInputSuspendedByGamePause())
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
	if (ShouldIgnoreInput())return;//an axis held by a pause reads zero, which is the case above
	ReportDeviceForKey(EKeys::MouseWheelAxis);
	// Both components carry the wheel value: InputScroll documents X as horizontal and Y as vertical,
	// and a mouse wheel has no horizontal axis to distinguish.
	InputModule->InputScroll(FVector2D(AxisValue, AxisValue));
}

void ADreamStandaloneInputEventSystemActor::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (ShouldIgnoreInput())return;
	ReportDeviceForKey(EKeys::TouchKeys[FMath::Clamp((int32)FingerIndex, 0, (int32)EKeys::NUM_TOUCH_KEYS - 1)]);
	InputModule->InputTouchTrigger(true, static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (ShouldIgnoreInput())return;
	InputModule->InputTouchTrigger(false, static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (ShouldIgnoreInput())return;
	InputModule->InputTouchMoved(static_cast<int32>(FingerIndex), Location);
}

void ADreamStandaloneInputEventSystemActor::OnAnyKeyPressed(FKey Key)
{
	if (ShouldIgnoreInput())return;
	if (IsNavigationKey(Key))
	{
		ReportDeviceForKey(Key);
		return;//routed from its own handler; doing it here too would fire a bound action twice
	}
	DreamUIKeyRouting::RouteOtherKey(GetInputUser(), Key, true);
}

void ADreamStandaloneInputEventSystemActor::OnAnyKeyReleased(FKey Key)
{
	if (ShouldIgnoreInput())return;
	if (IsNavigationKey(Key))
	{
		return;
	}
	DreamUIKeyRouting::RouteOtherKey(GetInputUser(), Key, false);
}

UDreamWidget* ADreamStandaloneInputEventSystemActor::GetFocusedWidget() const
{
	return DreamUIKeyRouting::GetKeyTarget(GetInputUser());
}

void ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed(FKey Key)
{
	if (ShouldIgnoreInput())return;
	DreamUIKeyRouting::RouteScrollKey(GetInputUser(), Key);
}

bool ADreamStandaloneInputEventSystemActor::RouteAnalog(const FKey& InKey, float InValue)
{
	if (ShouldIgnoreInput())return false;
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	UDreamEventSystem* Events = GetEventSystem();
	if (Router == nullptr || Events == nullptr)return false;
	return Router->HandleAnalog(Events->GetUserIndex(), InKey, InValue);
}

void ADreamStandaloneInputEventSystemActor::OnGamepadLeftX(float AxisValue)
{
	RouteAnalog(EKeys::Gamepad_LeftX, AxisValue);
}

void ADreamStandaloneInputEventSystemActor::OnGamepadLeftY(float AxisValue)
{
	RouteAnalog(EKeys::Gamepad_LeftY, AxisValue);
}

void ADreamStandaloneInputEventSystemActor::OnGamepadLeftTrigger(float AxisValue)
{
	RouteAnalog(EKeys::Gamepad_LeftTriggerAxis, AxisValue);
}

void ADreamStandaloneInputEventSystemActor::OnGamepadRightTrigger(float AxisValue)
{
	RouteAnalog(EKeys::Gamepad_RightTriggerAxis, AxisValue);
}

void ADreamStandaloneInputEventSystemActor::OnGamepadScrollX(float AxisValue)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	// The focused widget hears the stick first, as an analog value, and a widget that keeps it is not also scrolled.
	if (RouteAnalog(EKeys::Gamepad_RightX, AxisValue))
	{
		return;
	}
	// A resting stick fires this every frame; anything below the deadzone is the stick sitting still.
	// A stick held by a pause reads as sitting still too, as the engine would have zeroed it.
	if (FMath::Abs(AxisValue) < GamepadScrollDeadzone || ShouldIgnoreInput())
	{
		return;
	}
	UDreamWidget* Focused = GetFocusedWidget();
	if (!IsValid(Focused))
	{
		return;
	}
	const UWorld* World = GetWorld();
	// The UI clock: a stick scrolls a list as fast in a slowed-down game as at full speed.
	const float DeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(this, World != nullptr ? World->GetDeltaSeconds() : 0.0f);
	// Through the key-aware road: a scrolling container may have named the analog key that acts as
	// its wheel, and one that named the other axis (or a key this preset does not bind) must not be
	// driven by this one. Naming nothing keeps the stick, which is what every container does by
	// default.
	if (FDreamUINavigationScroll::ScrollByAnalogAxis(Focused, EKeys::Gamepad_RightX,
		FVector2D(AxisValue * GamepadScrollSpeed * DeltaSeconds, 0.0f)))
	{
		ReportDeviceForKey(EKeys::Gamepad_RightX);
	}
}

void ADreamStandaloneInputEventSystemActor::OnGamepadScrollY(float AxisValue)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	if (RouteAnalog(EKeys::Gamepad_RightY, AxisValue))
	{
		return;
	}
	if (FMath::Abs(AxisValue) < GamepadScrollDeadzone || ShouldIgnoreInput())
	{
		return;
	}
	UDreamWidget* Focused = GetFocusedWidget();
	if (!IsValid(Focused))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float DeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(this, World != nullptr ? World->GetDeltaSeconds() : 0.0f);
	// Pushing the stick UP shows earlier content, which is a SMALLER scroll offset -- the offset is
	// the distance scrolled from the start, not the position of the viewport's top edge.
	if (FDreamUINavigationScroll::ScrollByAnalogAxis(Focused, EKeys::Gamepad_RightY,
		FVector2D(0.0f, -AxisValue * GamepadScrollSpeed * DeltaSeconds)))
	{
		ReportDeviceForKey(EKeys::Gamepad_RightY);
	}
}

void ADreamStandaloneInputEventSystemActor::OnNavigationTriggerPressed(FKey Key)
{
	if (ShouldIgnoreInput())return;
	// The one key road: the bindings first, the virtual cursor second, the navigation highlight's press last.
	DreamUIKeyRouting::RouteConfirmKey(GetInputUser(), Key, true);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased(FKey Key)
{
	if (ShouldIgnoreInput())return;
	DreamUIKeyRouting::RouteConfirmKey(GetInputUser(), Key, false);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed(FKey Key)
{
	if (ShouldIgnoreInput())return;
	// Resolved here, where a subclass can say what a key means (ResolveNavigationDirection); routed the one way.
	DreamUIKeyRouting::RouteDirectionKeyAs(GetInputUser(), Key, ResolveNavigationDirection(Key), true);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased(FKey Key)
{
	if (ShouldIgnoreInput())return;
	DreamUIKeyRouting::RouteDirectionKeyAs(GetInputUser(), Key, ResolveNavigationDirection(Key), false);
}

#undef LOCTEXT_NAMESPACE
