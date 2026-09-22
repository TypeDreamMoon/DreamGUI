// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Components/InputComponent.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
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

	/**
	 * Keys that mean Back when nothing has bound an action to them. A project that wants its own can
	 * put Back in its action table and bind it; that is offered the key first and wins.
	 */
	static const FKey BackKeys[] = {
		EKeys::Escape,
		EKeys::Gamepad_FaceButton_Right,
	};

	/**
	 * Direction keys, paired with the direction they mean. Read by GetNavigationDirectionForKey.
	 *
	 * Tab is listed as Next and turns into Prev when shift is held -- see ResolveNavigationDirection.
	 * Next and Prev were implemented on UUISelectable from the start and reachable from nothing: no
	 * key in this table produced either, so the sequential-focus half of navigation was dead code in
	 * every project using the preset.
	 */
	static const TPair<FKey, EDreamUINavigationDirection> NavigationDirectionKeys[] = {
		{ EKeys::Left,                     EDreamUINavigationDirection::Left },
		{ EKeys::Right,                    EDreamUINavigationDirection::Right },
		{ EKeys::Up,                       EDreamUINavigationDirection::Up },
		{ EKeys::Down,                     EDreamUINavigationDirection::Down },
		{ EKeys::Tab,                      EDreamUINavigationDirection::Next },
		{ EKeys::Gamepad_LeftStick_Left,   EDreamUINavigationDirection::Left },
		{ EKeys::Gamepad_LeftStick_Right,  EDreamUINavigationDirection::Right },
		{ EKeys::Gamepad_LeftStick_Up,     EDreamUINavigationDirection::Up },
		{ EKeys::Gamepad_LeftStick_Down,   EDreamUINavigationDirection::Down },
	};

	/**
	 * Keys that page the scrolling container around whatever has focus.
	 *
	 * A list longer than a screen was reachable a row at a time and no faster: navigation revealed the
	 * next row, and there was no way at all to move by a screenful or jump to an end without a mouse
	 * wheel. The value is how many screenfuls one press moves.
	 */
	static const TPair<FKey, float> ScrollPageKeys[] = {
		{ EKeys::PageUp,   -1.0f },
		{ EKeys::PageDown,  1.0f },
	};
	/** Home and End: all the way to one extent. The bool is "towards the start". */
	static const TPair<FKey, bool> ScrollExtentKeys[] = {
		{ EKeys::Home, true },
		{ EKeys::End,  false },
	};

	/** Navigation is single-pointer; the Blueprint hard-coded 0 on every call and so does this. */
	static constexpr int32 NavigationPointerID = 0;
	/** Below this the right stick is at rest; a bound axis reports every frame either way. */
	static constexpr float GamepadScrollDeadzone = 0.2f;
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
	BindMouseInput();
	BindNavigationAndTouchInput();
	BindActionRouting();
}

void ADreamStandaloneInputEventSystemActor::BindMouseInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	for (const TPair<FKey, EDreamUIMouseButtonType>& Button : MouseButtons)
	{
		InputComponent->BindKey(Button.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonPressed)
			.bConsumeInput = bConsumeBoundInput;
		InputComponent->BindKey(Button.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnMouseButtonReleased)
			.bConsumeInput = bConsumeBoundInput;
	}

	InputComponent->BindVectorAxis(EKeys::Mouse2D, this, &ADreamStandaloneInputEventSystemActor::OnMouseMoved)
		.bConsumeInput = bConsumeBoundInput;
	InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ADreamStandaloneInputEventSystemActor::OnMouseWheel)
		.bConsumeInput = bConsumeBoundInput;
}

void ADreamStandaloneInputEventSystemActor::BindNavigationAndTouchInput()
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	InputComponent->BindTouch(IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnTouchPressed)
		.bConsumeInput = bConsumeBoundInput;
	InputComponent->BindTouch(IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnTouchReleased)
		.bConsumeInput = bConsumeBoundInput;
	InputComponent->BindTouch(IE_Repeat, this, &ADreamStandaloneInputEventSystemActor::OnTouchMoved)
		.bConsumeInput = bConsumeBoundInput;

	for (const FKey& Key : NavigationTriggerKeys)
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerPressed)
			.bConsumeInput = bConsumeBoundInput;
		InputComponent->BindKey(Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased)
			.bConsumeInput = bConsumeBoundInput;
	}

	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : NavigationDirectionKeys)
	{
		InputComponent->BindKey(Direction.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed)
			.bConsumeInput = bConsumeBoundInput;
		InputComponent->BindKey(Direction.Key, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased)
			.bConsumeInput = bConsumeBoundInput;
	}

	// Paging. Bound by name like the directions, and for the same reason: a key this preset gives its
	// own meaning must not also reach the AnyKey handler, or the router would be offered it twice.
	for (const TPair<FKey, float>& Page : ScrollPageKeys)
	{
		InputComponent->BindKey(Page.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed)
			.bConsumeInput = bConsumeBoundInput;
	}
	for (const TPair<FKey, bool>& Extent : ScrollExtentKeys)
	{
		InputComponent->BindKey(Extent.Key, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed)
			.bConsumeInput = bConsumeBoundInput;
	}
	// The right stick is the gamepad's wheel. Two axes rather than one vector key because there is no
	// Gamepad_Right2D, and an axis binding that reads zero every frame costs nothing.
	InputComponent->BindAxisKey(EKeys::Gamepad_RightX, this, &ADreamStandaloneInputEventSystemActor::OnGamepadScrollX)
		.bConsumeInput = bConsumeBoundInput;
	InputComponent->BindAxisKey(EKeys::Gamepad_RightY, this, &ADreamStandaloneInputEventSystemActor::OnGamepadScrollY)
		.bConsumeInput = bConsumeBoundInput;
}

void ADreamStandaloneInputEventSystemActor::BindActionRouting()
{
	// The one binding that makes consumption catastrophic rather than merely wrong: UPlayerInput
	// expands AnyKey to every non-simulated key in the key state map, so consuming it takes the lot.
	InputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &ADreamStandaloneInputEventSystemActor::OnAnyKeyPressed)
		.bConsumeInput = bConsumeBoundInput;
	InputComponent->BindKey(EKeys::AnyKey, IE_Released, this, &ADreamStandaloneInputEventSystemActor::OnAnyKeyReleased)
		.bConsumeInput = bConsumeBoundInput;
}

bool ADreamStandaloneInputEventSystemActor::IsNavigationKey(const FKey& Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	for (const FKey& Trigger : NavigationTriggerKeys)
	{
		if (Trigger == Key)return true;
	}
	for (const TPair<FKey, float>& Page : ScrollPageKeys)
	{
		if (Page.Key == Key)return true;
	}
	for (const TPair<FKey, bool>& Extent : ScrollExtentKeys)
	{
		if (Extent.Key == Key)return true;
	}
	return GetNavigationDirectionForKey(Key) != EDreamUINavigationDirection::None;
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

bool ADreamStandaloneInputEventSystemActor::TryHandleWithVirtualCursor(const FKey& Key, bool bPressed)
{
	UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(this);
	if (Cursor == nullptr || !Cursor->IsVirtualCursorActive())
	{
		return false;
	}
	// The cursor owns the left stick and the confirm button while it is up, and directional
	// navigation must not also run: both used to act on pointer 0 and rewrite its input type every
	// frame, so which of the two ProcessInput believed was a race, and one press of the face button
	// pressed BOTH the navigation highlight and whatever the cursor happened to be over.
	//
	// A confirm still has a meaning here, just a different one -- it is the cursor's click -- so it
	// is forwarded rather than dropped. A direction is simply not the cursor's, so it is dropped.
	for (const FKey& Trigger : DreamStandaloneInputEventSystemActorLocal::NavigationTriggerKeys)
	{
		if (Trigger == Key)
		{
			Cursor->SetConfirmPressed(bPressed);
			return true;
		}
	}
	return true;
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

void ADreamStandaloneInputEventSystemActor::OnAnyKeyPressed(FKey Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;

	ReportDeviceForKey(Key);
	if (IsNavigationKey(Key))
	{
		return;//routed from its own handler; doing it here too would fire a bound action twice
	}
	if (Key.IsModifierKey())
	{
		// Ctrl is not a key anything is bound to; it is what qualifies the key that follows. Offering it
		// to the router meant every Ctrl press was a keypress in its own right -- and since an action
		// with an unset KeyboardKey matches nothing, it mostly just cost a scan of every binding before
		// the real key arrived. Now that actions can require modifiers, the router reads their state
		// directly when the qualified key comes through.
		return;
	}
	if (RouteActionKey(Key, true))
	{
		return;
	}
	// Only once nothing has claimed the key: a project that binds its own Back action gets to define
	// what Back does, and the built-in behaviour is the fallback for one that has not.
	for (const FKey& BackKey : BackKeys)
	{
		if (BackKey != Key)continue;
		// A drag in flight outranks Back. Escape is the universal "put it back" while something is
		// held, and closing the screen out from under a half-finished drag instead is the one thing
		// a player pressing it cannot have meant. Only drags carrying an operation count -- a scroll
		// is a drag too, and Escape has never cancelled a scroll anywhere.
		if (UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(this))
		{
			if (DragDrop->CancelActiveDrag())
			{
				return;
			}
		}
		if (UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(this))
		{
			UDreamEventSystem* Events = GetEventSystem();
			Stack->HandleBack(Events != nullptr ? Events->GetUserIndex() : 0);
		}
		return;
	}
}

void ADreamStandaloneInputEventSystemActor::OnAnyKeyReleased(FKey Key)
{
	if (IsNavigationKey(Key))
	{
		return;
	}
	if (Key.IsModifierKey())
	{
		return;//same as the press: a modifier qualifies a key, it is not one
	}
	RouteActionKey(Key, false);
}

UDreamWidget* ADreamStandaloneInputEventSystemActor::GetFocusedWidget() const
{
	UDreamEventSystem* Events = GetEventSystem();
	if (Events == nullptr)
	{
		return nullptr;
	}
	// The navigation highlight first, because that is what the player is looking at during gamepad
	// input; the selected widget is the answer after a click, when there is no highlight.
	if (UDreamWidget* Highlighted = Events->GetHighlightedComponentForNavigation(
		DreamStandaloneInputEventSystemActorLocal::NavigationPointerID))
	{
		return Highlighted;
	}
	return Events->GetCurrentSelectedComponent(DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnScrollKeyPressed(FKey Key)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	ReportDeviceForKey(Key);
	// Offered to the router first, like every other key this preset names: a screen that binds End to
	// "jump to newest" must win over the built-in meaning.
	if (RouteActionKey(Key, true))return;

	UDreamWidget* Focused = GetFocusedWidget();
	if (!IsValid(Focused))
	{
		return;//nothing has focus, so there is no list to page
	}
	for (const TPair<FKey, float>& Page : ScrollPageKeys)
	{
		if (Page.Key == Key)
		{
			FDreamUINavigationScroll::ScrollByPages(Focused, Page.Value);
			return;
		}
	}
	for (const TPair<FKey, bool>& Extent : ScrollExtentKeys)
	{
		if (Extent.Key == Key)
		{
			FDreamUINavigationScroll::ScrollToExtent(Focused, Extent.Value);
			return;
		}
	}
}

void ADreamStandaloneInputEventSystemActor::OnGamepadScrollX(float AxisValue)
{
	using namespace DreamStandaloneInputEventSystemActorLocal;
	// A resting stick fires this every frame; anything below the deadzone is the stick sitting still.
	if (FMath::Abs(AxisValue) < GamepadScrollDeadzone)
	{
		return;
	}
	UDreamWidget* Focused = GetFocusedWidget();
	if (!IsValid(Focused))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World != nullptr ? World->GetDeltaSeconds() : 0.0f;
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
	if (FMath::Abs(AxisValue) < GamepadScrollDeadzone)
	{
		return;
	}
	UDreamWidget* Focused = GetFocusedWidget();
	if (!IsValid(Focused))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World != nullptr ? World->GetDeltaSeconds() : 0.0f;
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
	ReportDeviceForKey(Key);
	// An action explicitly bound to this key outranks the preset's built-in meaning for it. Confirm is
	// the likeliest thing a screen binds to Enter, and it must not also press whatever navigation is
	// sitting on -- one keypress, one outcome.
	if (RouteActionKey(Key, true))return;
	// The router first, the cursor second, navigation last -- one key, one consumer, in that order.
	if (TryHandleWithVirtualCursor(Key, true))return;
	InputModule->InputTriggerForNavigation(true, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationTriggerReleased(FKey Key)
{
	if (RouteActionKey(Key, false))return;
	if (TryHandleWithVirtualCursor(Key, false))return;
	InputModule->InputTriggerForNavigation(false, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionPressed(FKey Key)
{
	// Reported BEFORE the cursor is consulted, and that ordering matters: a keyboard arrow reports
	// the keyboard, which is what takes an auto-mode virtual cursor down, so the very same press
	// then falls through to directional navigation instead of being eaten by a cursor that is on
	// its way out.
	ReportDeviceForKey(Key);
	if (RouteActionKey(Key, true))return;
	if (TryHandleWithVirtualCursor(Key, true))return;
	InputModule->InputNavigation(
		ResolveNavigationDirection(Key), true, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

void ADreamStandaloneInputEventSystemActor::OnNavigationDirectionReleased(FKey Key)
{
	if (RouteActionKey(Key, false))return;
	if (TryHandleWithVirtualCursor(Key, false))return;
	// The direction is still passed on release even though the module ignores it there, so the two
	// handlers stay symmetric and a future module change cannot silently depend on a None here.
	InputModule->InputNavigation(
		ResolveNavigationDirection(Key), false, DreamStandaloneInputEventSystemActorLocal::NavigationPointerID);
}

#undef LOCTEXT_NAMESPACE
