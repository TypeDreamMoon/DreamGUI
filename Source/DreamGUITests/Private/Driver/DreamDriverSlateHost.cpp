// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverSlateHost.h"

#include "Engine/World.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUISlateInputSource.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Interaction/DreamUITextInputTarget.h"

#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverKeys.h"
#include "Driver/DreamDriverSequence.h"

namespace DreamDriverSlateHostLocal
{
	/** The Slate user every event the driver sends belongs to: the keyboard and the mouse of a desk with one player. */
	constexpr uint32 DriverSlateUser = 0;
	/** The player Slate user 0 is, under the test mappers. */
	constexpr int32 DriverPlayer = 0;

	/** The player the rig's event system speaks for, which is whose keyboard the driver's characters are typed on. */
	int32 PlayerOf(const FDreamDriverContext& InContext)
	{
		return IsValid(InContext.EventSystem) ? InContext.EventSystem->GetUserIndex() : DriverPlayer;
	}

	/** One key event to the source, made the way FSlateApplication::OnKeyDown and OnKeyUp make one from a platform message. */
	void SendKeyEvent(FDreamUISlateInputSource& InSource, FSlateApplication& InSlateApp, const FKey& InKey, TConstArrayView<FKey> InHeldModifierKeys, bool bInPressed)
	{
		uint32 KeyCode = 0;
		uint32 CharacterCode = 0;
		DreamDriverKeys::GetKeyCodes(InKey, KeyCode, CharacterCode);
		const FKeyEvent Event(InKey, DreamDriverKeys::MakeModifierKeysState(InHeldModifierKeys), DriverSlateUser,
			/*bInIsRepeat*/ false, CharacterCode, KeyCode);
		if (bInPressed)
		{
			InSource.HandleKeyDownEvent(InSlateApp, Event);
		}
		else
		{
			InSource.HandleKeyUpEvent(InSlateApp, Event);
		}
	}

	/** The mouse key of a button, or false for a button no mouse has (the user-defined ones are the module's alone). */
	bool ButtonKeyOf(EDreamUIMouseButtonType InButton, FKey& OutKey)
	{
		switch (InButton)
		{
		case EDreamUIMouseButtonType::Left:
			OutKey = EKeys::LeftMouseButton;
			return true;
		case EDreamUIMouseButtonType::Right:
			OutKey = EKeys::RightMouseButton;
			return true;
		case EDreamUIMouseButtonType::Middle:
			OutKey = EKeys::MiddleMouseButton;
			return true;
		default:
			return false;
		}
	}
}

bool DreamDriverSlateHost::Build(FDreamDriverContext& InContext, const FIntPoint& InViewportSize, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	UWorld* World = InContext.World;
	UDreamUIInputSubsystem* Input = World != nullptr ? UDreamUIInputSubsystem::Get(World) : nullptr;
	if (Input == nullptr)
	{
		OutWhyNot = TEXT("the world has no DreamUI input subsystem, and the Slate source is the input subsystem's");
		return false;
	}
	if (!FSlateApplication::IsInitialized())
	{
		OutWhyNot = TEXT("Slate is not running, and the Slate source is one of Slate's input pre-processors");
		return false;
	}
	// The world's own source, turned on the way the setting turns it on when play begins -- already on when the
	// project's setting is, in which case this changes nothing.
	Input->SetSlateInputSourceEnabled(true);
	const TSharedPtr<FDreamUISlateInputSource> Source = Input->GetSlateInputSource();
	if (!Source.IsValid())
	{
		OutWhyNot = TEXT("the input subsystem would not turn its Slate source on (its world has been torn down)");
		return false;
	}
	// Off Slate's own list: on it, the machine's real mouse and keyboard would reach the rig between engine frames, and
	// a rig's input is the driver's alone. The subsystem still holds the source, so the world's input is heard from it
	// and the preset actors stand down; turning it off later unregisters nothing, which Slate tolerates.
	FSlateApplication::Get().UnregisterInputPreProcessor(Source);
	// Nor does the desk's window focus reach it: the source lets go of every press and key it holds when the application
	// goes to the background (FDreamUISlateInputSource::HandleApplicationActivationStateChanged), and a key a test holds
	// across frames must not be let go of because somebody clicked another window meanwhile. Its own destructor's removal
	// then finds nothing to remove.
	FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(Source.Get());

	// A world with no game viewport has no geometry to map a screen point through, no Slate users of its own and no
	// widget to hold the keyboard focus: the mappers say what a game viewport filling the screen would. Slate user 0 is
	// player 0, and its keyboard focus is on the viewport; any other Slate user is no player of this world.
	const FIntPoint MappedSize = InViewportSize;
	Source->SetViewportMapperForTesting([MappedSize](const FVector2D& InScreen, FVector2D& OutPixel)
	{
		OutPixel = InScreen;
		return InScreen.X >= 0.0 && InScreen.Y >= 0.0 && InScreen.X < MappedSize.X && InScreen.Y < MappedSize.Y;
	});
	Source->SetUserMapperForTesting([](int32 InSlateUserIndex)
	{
		return InSlateUserIndex == static_cast<int32>(DriverSlateUser) ? DriverPlayer : INDEX_NONE;
	});
	Source->SetKeyboardFocusMapperForTesting([](int32 InSlateUserIndex)
	{
		return InSlateUserIndex == static_cast<int32>(DriverSlateUser);
	});

	// The mouse is Slate's now. With its override on, the driver's module stands a cursor of its own in for the mouse --
	// the source then treats it as a virtual cursor, ignores the mouse's moves and presses where the module points.
	if (UDreamDriverInputModule* Module = InContext.InputModule; IsValid(Module))
	{
		Module->SetOverrideMousePosition(false);
	}
	InContext.SlateMousePixel = FVector2D::ZeroVector;
	InContext.SlateMouseButtonsHeld.Reset();
	return true;
}

void DreamDriverSlateHost::Teardown(FDreamDriverContext& InContext)
{
	if (UDreamUIInputSubsystem* Input = InContext.World != nullptr ? UDreamUIInputSubsystem::Get(InContext.World) : nullptr)
	{
		Input->SetSlateInputSourceEnabled(false);
	}
	InContext.SlateMouseButtonsHeld.Reset();
}

TSharedPtr<FDreamUISlateInputSource> DreamDriverSlateHost::FindSource(const FDreamDriverContext& InContext, FString& OutWhyNot)
{
	if (!FSlateApplication::IsInitialized())
	{
		OutWhyNot = TEXT("Slate is not running, so there is nothing to make Slate's events with");
		return nullptr;
	}
	const UDreamUIInputSubsystem* Input = InContext.World != nullptr ? UDreamUIInputSubsystem::Get(InContext.World) : nullptr;
	if (Input == nullptr)
	{
		OutWhyNot = TEXT("the world has no DreamUI input subsystem");
		return nullptr;
	}
	TSharedPtr<FDreamUISlateInputSource> Source = Input->GetSlateInputSource();
	if (!Source.IsValid())
	{
		OutWhyNot = TEXT("the world's Slate input source is off; something turned it off after the rig turned it on (UDreamUIInputSubsystem::SetSlateInputSourceEnabled)");
	}
	return Source;
}

FVector2D DreamDriverSlateHost::GetMousePixel(const FDreamDriverContext& InContext)
{
	return InContext.SlateMousePixel;
}

bool DreamDriverSlateHost::MovePointer(FDreamDriverContext& InContext, const FVector2D& InPixel, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	const TSharedPtr<FDreamUISlateInputSource> Source = FindSource(InContext, OutWhyNot);
	if (!Source.IsValid())
	{
		return false;
	}
	// Remembered whether or not the UI follows the mouse -- a virtual cursor standing in for it does not -- because it
	// is where the mouse is, which is where the next relative move starts and where the next button goes down.
	const FVector2D LastPixel = InContext.SlateMousePixel;
	InContext.SlateMousePixel = InPixel;
	const FPointerEvent Move(DriverSlateUser, FSlateApplication::CursorPointerIndex, InPixel, LastPixel,
		InContext.SlateMouseButtonsHeld, EKeys::Invalid, 0.0f, FModifierKeysState());
	Source->HandleMouseMoveEvent(FSlateApplication::Get(), Move);
	return true;
}

bool DreamDriverSlateHost::PressMouseButton(FDreamDriverContext& InContext, EDreamUIMouseButtonType InButton, bool bInPressed, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	const TSharedPtr<FDreamUISlateInputSource> Source = FindSource(InContext, OutWhyNot);
	if (!Source.IsValid())
	{
		return false;
	}
	FKey ButtonKey;
	if (!ButtonKeyOf(InButton, ButtonKey))
	{
		OutWhyNot = FString::Printf(TEXT("button %d is not one a mouse has: Slate's mouse events carry Left, Right and Middle, and the user-defined buttons are only reachable through the module"),
			static_cast<int32>(InButton));
		return false;
	}
	// The held set as Slate reports it with the event: the button among them on the way down, gone on the way up.
	if (bInPressed)
	{
		InContext.SlateMouseButtonsHeld.Add(ButtonKey);
	}
	else
	{
		InContext.SlateMouseButtonsHeld.Remove(ButtonKey);
	}
	const FVector2D At = InContext.SlateMousePixel;
	const FPointerEvent Button(DriverSlateUser, FSlateApplication::CursorPointerIndex, At, At,
		InContext.SlateMouseButtonsHeld, ButtonKey, 0.0f, FModifierKeysState());
	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (bInPressed)
	{
		Source->HandleMouseButtonDownEvent(SlateApp, Button);
	}
	else
	{
		Source->HandleMouseButtonUpEvent(SlateApp, Button);
	}
	return true;
}

bool DreamDriverSlateHost::Scroll(FDreamDriverContext& InContext, const FVector2D& InAxisValue, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	const TSharedPtr<FDreamUISlateInputSource> Source = FindSource(InContext, OutWhyNot);
	if (!Source.IsValid())
	{
		return false;
	}
	// One axis, read from the request as the actor hosts read it: a wheel has no second axis, and Slate's wheel event
	// carries one delta, which the source hands on as (v, v).
	double WheelValue = 0.0;
	if (FMath::IsNearlyZero(InAxisValue.X) || FMath::IsNearlyEqual(InAxisValue.X, InAxisValue.Y))
	{
		WheelValue = InAxisValue.Y;
	}
	else if (FMath::IsNearlyZero(InAxisValue.Y))
	{
		WheelValue = InAxisValue.X;
	}
	else
	{
		OutWhyNot = FString::Printf(TEXT("a mouse wheel has one axis and %s asks for two different values; Slate's wheel event carries one delta"),
			*InAxisValue.ToString());
		return false;
	}
	if (FMath::IsNearlyZero(WheelValue))
	{
		return true;//a wheel at rest sends nothing
	}
	// What FSlateApplication::OnMouseWheel makes: at the mouse, the buttons held, no effecting button.
	const FVector2D At = InContext.SlateMousePixel;
	const FPointerEvent Wheel(DriverSlateUser, FSlateApplication::CursorPointerIndex, At, At,
		InContext.SlateMouseButtonsHeld, EKeys::Invalid, static_cast<float>(WheelValue), FModifierKeysState());
	Source->HandleMouseWheelOrGestureEvent(FSlateApplication::Get(), Wheel, nullptr);
	return true;
}

bool DreamDriverSlateHost::Touch(FDreamDriverContext& InContext, EDreamDriverTouchPhase InPhase, int32 InFingerId, const FVector2D& InPixel, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	const TSharedPtr<FDreamUISlateInputSource> Source = FindSource(InContext, OutWhyNot);
	if (!Source.IsValid())
	{
		return false;
	}
	const int32 TouchKeyCount = EKeys::NUM_TOUCH_KEYS;
	if (InFingerId < 0 || InFingerId >= TouchKeyCount)
	{
		OutWhyNot = FString::Printf(TEXT("finger %d is outside the %d touch keys a player has"), InFingerId, TouchKeyCount);
		return false;
	}
	// The touch events FSlateApplication makes from the platform's (ProcessTouchStartedEvent and its siblings): the
	// finger's index as the pointer index, full force while down and none on the way up.
	const uint32 PointerIndex = static_cast<uint32>(InFingerId);
	FSlateApplication& SlateApp = FSlateApplication::Get();
	switch (InPhase)
	{
	case EDreamDriverTouchPhase::Began:
	{
		const FPointerEvent Down(DriverSlateUser, PointerIndex, InPixel, InPixel, 1.0f, /*bPressLeftMouseButton*/ true);
		Source->HandleMouseButtonDownEvent(SlateApp, Down);
		break;
	}
	case EDreamDriverTouchPhase::Moved:
	{
		const FPointerEvent Moved(DriverSlateUser, PointerIndex, InPixel, InPixel, 1.0f, /*bPressLeftMouseButton*/ true);
		Source->HandleMouseMoveEvent(SlateApp, Moved);
		break;
	}
	case EDreamDriverTouchPhase::Ended:
	default:
	{
		const FPointerEvent Up(DriverSlateUser, PointerIndex, InPixel, InPixel, 0.0f, /*bPressLeftMouseButton*/ false);
		Source->HandleMouseButtonUpEvent(SlateApp, Up);
		break;
	}
	}
	return true;
}

bool DreamDriverSlateHost::Navigate(FDreamDriverContext& InContext, EDreamUINavigationDirection InDirection, bool bInPressed, FString& OutWhyNot)
{
	// The keys the actor hosts press for a direction (DreamDriverGameHost::Navigate): the left stick's direction keys --
	// which a pad's stick sends Slate as key events -- Tab for Next, and Shift+Tab for Prev.
	FKey DirectionKey;
	EDreamDriverModifierKeys Modifiers = EDreamDriverModifierKeys::None;
	switch (InDirection)
	{
	case EDreamUINavigationDirection::Left:
		DirectionKey = EKeys::Gamepad_LeftStick_Left;
		break;
	case EDreamUINavigationDirection::Right:
		DirectionKey = EKeys::Gamepad_LeftStick_Right;
		break;
	case EDreamUINavigationDirection::Up:
		DirectionKey = EKeys::Gamepad_LeftStick_Up;
		break;
	case EDreamUINavigationDirection::Down:
		DirectionKey = EKeys::Gamepad_LeftStick_Down;
		break;
	case EDreamUINavigationDirection::Next:
		DirectionKey = EKeys::Tab;
		break;
	case EDreamUINavigationDirection::Prev:
		DirectionKey = EKeys::Tab;
		Modifiers = EDreamDriverModifierKeys::Shift;
		break;
	default:
		OutWhyNot = TEXT("None is not a direction any key presses");
		return false;
	}
	return SendKey(InContext, DirectionKey, Modifiers, bInPressed, OutWhyNot);
}

bool DreamDriverSlateHost::SendKey(FDreamDriverContext& InContext, const FKey& InKey, TConstArrayView<FKey> InHeldModifierKeys, bool bInPressed, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	if (!InKey.IsValid())
	{
		OutWhyNot = TEXT("the key is not a valid key");
		return false;
	}
	for (const FKey& Modifier : InHeldModifierKeys)
	{
		if (!Modifier.IsModifierKey())
		{
			OutWhyNot = FString::Printf(TEXT("%s is not a modifier key, so it cannot be held with %s"), *Modifier.ToString(), *InKey.ToString());
			return false;
		}
	}
	const TSharedPtr<FDreamUISlateInputSource> Source = FindSource(InContext, OutWhyNot);
	if (!Source.IsValid())
	{
		return false;
	}
	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (bInPressed)
	{
		// Each modifier's own event carries itself among the held ones: the platform marks a modifier down before it
		// reports the key that went down.
		TArray<FKey> Down;
		for (const FKey& Modifier : InHeldModifierKeys)
		{
			Down.Add(Modifier);
			SendKeyEvent(*Source, SlateApp, Modifier, Down, true);
		}
		SendKeyEvent(*Source, SlateApp, InKey, Down, true);
		return true;
	}
	// The key first, still with every modifier held; then the modifiers, each one's event no longer carrying itself.
	TArray<FKey> Down(InHeldModifierKeys.GetData(), InHeldModifierKeys.Num());
	SendKeyEvent(*Source, SlateApp, InKey, Down, false);
	for (int32 Index = Down.Num() - 1; Index >= 0; --Index)
	{
		const FKey Modifier = Down[Index];
		Down.RemoveAt(Index);
		SendKeyEvent(*Source, SlateApp, Modifier, Down, false);
	}
	return true;
}

bool DreamDriverSlateHost::SendKey(FDreamDriverContext& InContext, const FKey& InKey, EDreamDriverModifierKeys InModifiers, bool bInPressed, FString& OutWhyNot)
{
	TArray<FKey> Held;
	DreamDriverKeys::GetModifierKeys(InModifiers, Held);
	return SendKey(InContext, InKey, Held, bInPressed, OutWhyNot);
}

bool DreamDriverSlateHost::TypeCharacter(FDreamDriverContext& InContext, TCHAR InCharacter, FString& OutWhyNot)
{
	using namespace DreamDriverSlateHostLocal;

	UDreamUIInputSubsystem* Input = InContext.World != nullptr ? UDreamUIInputSubsystem::Get(InContext.World) : nullptr;
	if (Input == nullptr)
	{
		OutWhyNot = TEXT("the world has no DreamUI input subsystem for the viewport to hand a character to");
		return false;
	}
	const int32 Player = PlayerOf(InContext);
	if (DreamUITextInputRouter::GetActiveTarget(InContext.World, Player) == nullptr)
	{
		OutWhyNot = TEXT("no text field is being edited, so a character has nowhere to go: the viewport hands it to the field the typing player is editing (UDreamUIInputSubsystem::HandleViewportCharacter) and there is none");
		return false;
	}
	// The answer is dropped: a refused character -- read-only, full, a letter in a number field -- is the field deciding.
	Input->HandleViewportCharacter(Player, InCharacter);
	return true;
}
