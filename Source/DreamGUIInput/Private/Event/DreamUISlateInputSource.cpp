// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUISlateInputSource.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SViewport.h"

namespace DreamUISlateInputSourceLocal
{
	/** Below this the right stick is at rest. */
	static constexpr float StickDeadzone = 0.2f;
	/** How fast the right stick scrolls what has focus, in canvas units a second at full tilt: the presets' default. */
	static constexpr float StickScrollSpeed = 1500.0f;

	bool ToButton(const FKey& InKey, EDreamUIMouseButtonType& OutButton)
	{
		if (InKey == EKeys::LeftMouseButton) { OutButton = EDreamUIMouseButtonType::Left; return true; }
		if (InKey == EKeys::RightMouseButton) { OutButton = EDreamUIMouseButtonType::Right; return true; }
		if (InKey == EKeys::MiddleMouseButton) { OutButton = EDreamUIMouseButtonType::Middle; return true; }
		return false;
	}

	/** Where a virtual cursor standing in for InUser's mouse is, when one does: the position its module reports. */
	bool FindVirtualCursor(const UDreamUIInputUser* InUser, FVector2D& OutPosition)
	{
		const UDreamStandaloneInputModule* Module = Cast<UDreamStandaloneInputModule>(InUser->GetInputModule());
		if (Module == nullptr || !Module->GetOverrideMousePosition())
		{
			return false;
		}
		Module->GetMousePosition(OutPosition);
		return true;
	}
}

FDreamUISlateInputSource::FDreamUISlateInputSource(UDreamUIInputSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
	, ConsumePolicy(GetDefault<UDreamGUISettings>()->SlateInputConsumePolicy)
{
}

void FDreamUISlateInputSource::SetViewportMapperForTesting(TFunction<bool(const FVector2D&, FVector2D&)> InMapper)
{
	ViewportMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::SetUserMapperForTesting(TFunction<int32(int32)> InMapper)
{
	UserMapper = MoveTemp(InMapper);
}

UWorld* FDreamUISlateInputSource::GetWorld() const
{
	const UDreamUIInputSubsystem* Input = Subsystem.Get();
	return Input != nullptr ? Input->GetWorld() : nullptr;
}

UDreamUIInputUser* FDreamUISlateInputSource::FindUser(int32 InSlateUserIndex) const
{
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr)
	{
		return nullptr;
	}
	int32 UserIndex = INDEX_NONE;
	if (UserMapper)
	{
		UserIndex = UserMapper(InSlateUserIndex);
	}
	else if (const UWorld* World = GetWorld(); World != nullptr && World->GetGameInstance() != nullptr)
	{
		const TArray<ULocalPlayer*>& LocalPlayers = World->GetGameInstance()->GetLocalPlayers();
		for (int32 Index = 0; Index < LocalPlayers.Num(); ++Index)
		{
			const ULocalPlayer* LocalPlayer = LocalPlayers[Index];
			const TSharedPtr<const FSlateUser> SlateUser = LocalPlayer != nullptr ? LocalPlayer->GetSlateUser() : nullptr;
			if (SlateUser.IsValid() && SlateUser->GetUserIndex() == InSlateUserIndex)
			{
				UserIndex = Index;
				break;
			}
		}
		// With one local player every Slate user is theirs: the keyboard and the mouse are Slate user 0, whoever holds
		// the pad.
		if (UserIndex == INDEX_NONE && LocalPlayers.Num() == 1)
		{
			UserIndex = 0;
		}
	}
	return UserIndex != INDEX_NONE ? Input->GetOrCreateUser(UserIndex) : nullptr;
}

bool FDreamUISlateInputSource::MapToViewport(const FVector2D& InScreen, FVector2D& OutPixel, bool& bOutInside) const
{
	if (ViewportMapper)
	{
		bOutInside = ViewportMapper(InScreen, OutPixel);
		return true;
	}
	const UWorld* World = GetWorld();
	UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Widget = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
	if (!Widget.IsValid())
	{
		return false;
	}
	const FGeometry& Geometry = Widget->GetCachedGeometry();
	bOutInside = Geometry.IsUnderLocation(InScreen);
	// FSceneViewport's own reckoning of where the cursor is: local to the viewport widget, in its pixels.
	const FVector2D Local = Geometry.AbsoluteToLocal(InScreen);
	OutPixel = Local * Geometry.Scale;
	return true;
}

bool FDreamUISlateInputSource::IsKeyForThisWorld(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) const
{
	if (ViewportMapper)
	{
		return true;//a test's world has no viewport to hold the focus
	}
	const UWorld* World = GetWorld();
	UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
	const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(InKeyEvent.GetUserIndex());
	if (!Viewport.IsValid() || !Focused.IsValid())
	{
		return false;
	}
	if (Focused.Get() == static_cast<SWidget*>(Viewport.Get()))
	{
		return true;
	}
	// Inside it: a UMG widget in the viewport's overlay holding the focus in a UI-only mode is this world's too.
	FWidgetPath Path;
	return SlateApp.FindPathToWidget(Focused.ToSharedRef(), Path) && Path.ContainsWidget(Viewport.Get());
}

int32 FDreamUISlateInputSource::PointerIDFor(const FPointerEvent& InEvent)
{
	return InEvent.IsTouchEvent() ? DreamUIPointerIds::ForTouch(InEvent.GetPointerIndex()) : DreamUIPointerIds::Mouse;
}

bool FDreamUISlateInputSource::FollowsTheMouse(const UDreamUIInputUser* InUser)
{
	// A virtual cursor in place of the mouse writes the mouse's pointer itself, through its module's override; the
	// real mouse moving must not fight it.
	FVector2D VirtualCursor = FVector2D::ZeroVector;
	return !DreamUISlateInputSourceLocal::FindVirtualCursor(InUser, VirtualCursor);
}

bool FDreamUISlateInputSource::WouldConsumePress(const UDreamUIInputUser* InUser, int32 InPointerID) const
{
	if (ConsumePolicy == EDreamUIInputConsumePolicy::Never)
	{
		return false;
	}
	// What the pointer is over as of its last trace -- the frame's moves come before its presses, so that is where the
	// press lands.
	const UDreamPointerEventData* EventData = InUser->FindPointerEventData(InPointerID);
	UDreamWidget* Over = EventData != nullptr ? EventData->EnterWidget.Get() : nullptr;
	if (!IsValid(Over))
	{
		return false;
	}
	if (ConsumePolicy == EDreamUIInputConsumePolicy::WhenOverUI)
	{
		return true;
	}
	return UDreamPointerInputModule::GetEventHandle(Over, UDreamPointerDownUpInterface::StaticClass()) != nullptr;
}

bool FDreamUISlateInputSource::WouldConsumeKey(bool bInTaken, bool bInTyped) const
{
	// Typing is never anything else's, whatever the policy: a letter typed into a field is not the pawn's to move with.
	return bInTyped || (bInTaken && ConsumePolicy != EDreamUIInputConsumePolicy::Never);
}

bool FDreamUISlateInputSource::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	UDreamUIInputUser* User = FindUser(MouseEvent.GetUserIndex());
	if (User == nullptr || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return false;
	}
	const int32 PointerID = PointerIDFor(MouseEvent);
	FVector2D Pixel;
	bool bInside = false;
	if (!MapToViewport(MouseEvent.GetScreenSpacePosition(), Pixel, bInside))
	{
		return false;
	}
	// A pointer held down is followed off the viewport too, as the viewport's own capture follows it: a drag dragged
	// past the edge is still that drag.
	const UDreamPointerEventData* Existing = User->FindPointerEventData(PointerID);
	const bool bHeld = Existing != nullptr && Existing->bNowIsTriggerPressed;
	if (!bInside && !bHeld)
	{
		return false;
	}
	if (PointerID == DreamUIPointerIds::Mouse && !FollowsTheMouse(User))
	{
		return false;
	}
	User->MovePointer(PointerID, FVector(Pixel, 0.0));
	return false;//a move is never kept from the game
}

bool FDreamUISlateInputSource::HandlePress(const FPointerEvent& InEvent, bool bInPressed)
{
	using namespace DreamUISlateInputSourceLocal;
	UDreamUIInputUser* User = FindUser(InEvent.GetUserIndex());
	if (User == nullptr)
	{
		return false;
	}
	const int32 PointerID = PointerIDFor(InEvent);
	const TPair<int32, int32> PressKey(User->GetUserIndex(), PointerID);
	EDreamUIMouseButtonType Button = EDreamUIMouseButtonType::Left;
	if (!InEvent.IsTouchEvent() && !ToButton(InEvent.GetEffectingButton(), Button))
	{
		return false;//a button the pointer pipeline has no name for
	}
	FVector2D Pixel;
	bool bInside = false;
	if (!MapToViewport(InEvent.GetScreenSpacePosition(), Pixel, bInside))
	{
		return false;
	}
	// A virtual cursor standing in for the mouse is where the mouse's buttons press, as a preset presses them (at its
	// module's pointer position): the platform's cursor says only that the click was on this viewport.
	FVector2D VirtualCursor = FVector2D::ZeroVector;
	const bool bAtVirtualCursor = PointerID == DreamUIPointerIds::Mouse && FindVirtualCursor(User, VirtualCursor);
	if (bInPressed)
	{
		if (!bInside || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
		{
			return false;
		}
		if (bAtVirtualCursor)
		{
			Pixel = VirtualCursor;
		}
		else
		{
			User->MovePointer(PointerID, FVector(Pixel, 0.0));
		}
		User->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(InEvent.IsTouchEvent() ? EKeys::TouchKeys[0] : InEvent.GetEffectingButton()));
		User->QueuePointerButton(PointerID, FVector(Pixel, 0.0), true, Button, InEvent.IsTouchEvent());
		const bool bConsume = WouldConsumePress(User, PointerID);
		if (bConsume)
		{
			ConsumedPresses.Add(PressKey);
		}
		return bConsume;
	}
	// A release goes where its press went, on the viewport or off it -- and is kept from the game when its press was.
	if (bAtVirtualCursor)
	{
		Pixel = VirtualCursor;
	}
	User->QueuePointerButton(PointerID, FVector(Pixel, 0.0), false, Button, InEvent.IsTouchEvent());
	return ConsumedPresses.Remove(PressKey) > 0;
}

bool FDreamUISlateInputSource::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return HandlePress(MouseEvent, true);
}

bool FDreamUISlateInputSource::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	// Slate sends a second press as a double click; the pipeline tells a double click from two presses itself.
	return HandlePress(MouseEvent, true);
}

bool FDreamUISlateInputSource::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return HandlePress(MouseEvent, false);
}

bool FDreamUISlateInputSource::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent)
{
	if (InGestureEvent != nullptr && InGestureEvent->GetGestureType() != EGestureEvent::None)
	{
		return false;//a trackpad gesture, which the wheel is not
	}
	UDreamUIInputUser* User = FindUser(InWheelEvent.GetUserIndex());
	const float Delta = InWheelEvent.GetWheelDelta();
	if (User == nullptr || FMath::IsNearlyZero(Delta) || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return false;
	}
	FVector2D Pixel;
	bool bInside = false;
	if (!MapToViewport(InWheelEvent.GetScreenSpacePosition(), Pixel, bInside) || !bInside)
	{
		return false;
	}
	User->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(EKeys::MouseWheelAxis));
	// Both components carry the wheel: a mouse wheel has no horizontal axis to tell apart.
	User->QueuePointerScroll(DreamUIPointerIds::Mouse, FVector2D(Delta, Delta));
	return WouldConsumePress(User, DreamUIPointerIds::Mouse);
}

bool FDreamUISlateInputSource::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	UDreamUIInputUser* User = FindUser(InKeyEvent.GetUserIndex());
	if (User == nullptr)
	{
		return false;
	}
	const TPair<int32, FKey> KeyOfUser(User->GetUserIndex(), Key);
	if (InKeyEvent.IsRepeat())
	{
		// A held key repeats for typing only: the navigation cursor repeats on its own clock, and a binding fires on
		// the press. Kept from the game as its press was.
		const bool bTyped = DreamUIKeyRouting::RouteTextKey(User, Key, true, InKeyEvent.GetModifierKeys());
		return bTyped || ConsumedKeys.Contains(KeyOfUser);
	}
	if (!IsKeyForThisWorld(SlateApp, InKeyEvent) || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return false;
	}
	RoutedKeys.Add(KeyOfUser);
	bool bTyped = false;
	const bool bTaken = DreamUIKeyRouting::RouteKey(User, Key, true, InKeyEvent.GetModifierKeys(), bTyped);
	const bool bConsume = WouldConsumeKey(bTaken, bTyped);
	if (bConsume)
	{
		ConsumedKeys.Add(KeyOfUser);
	}
	return bConsume;
}

bool FDreamUISlateInputSource::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	UDreamUIInputUser* User = FindUser(InKeyEvent.GetUserIndex());
	if (User == nullptr)
	{
		return false;
	}
	// The release goes where its press went, wherever the focus is by now -- or nowhere, for a press this source did not route.
	const TPair<int32, FKey> KeyOfUser(User->GetUserIndex(), InKeyEvent.GetKey());
	if (RoutedKeys.Remove(KeyOfUser) == 0)
	{
		return false;
	}
	bool bTyped = false;
	DreamUIKeyRouting::RouteKey(User, InKeyEvent.GetKey(), false, InKeyEvent.GetModifierKeys(), bTyped);
	return ConsumedKeys.Remove(KeyOfUser) > 0;
}

bool FDreamUISlateInputSource::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	UDreamUIInputUser* User = FindUser(InAnalogInputEvent.GetUserIndex());
	if (User == nullptr || !IsKeyForThisWorld(SlateApp, InAnalogInputEvent) || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return false;
	}
	const FKey Key = InAnalogInputEvent.GetKey();
	const float Value = InAnalogInputEvent.GetAnalogValue();
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(GetWorld());
	const bool bTaken = Router != nullptr && Router->HandleAnalog(User->GetUserIndex(), Key, Value);
	// The right stick is the gamepad's wheel -- unless the focused widget kept it -- and scrolls on every tick it is held.
	if (Key == EKeys::Gamepad_RightX || Key == EKeys::Gamepad_RightY)
	{
		FVector2D& Stick = RightSticks.FindOrAdd(User->GetUserIndex());
		(Key == EKeys::Gamepad_RightX ? Stick.X : Stick.Y) = bTaken ? 0.0f : Value;
		if (FMath::Abs(Value) >= DreamUISlateInputSourceLocal::StickDeadzone)
		{
			User->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(Key));
		}
	}
	return WouldConsumeKey(bTaken, false);
}

void FDreamUISlateInputSource::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	using namespace DreamUISlateInputSourceLocal;
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr || RightSticks.Num() == 0 || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return;
	}
	// The UI clock: a stick scrolls a list as fast in a slowed-down game as at full speed.
	const float DeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(GetWorld(), DeltaTime);
	for (const TPair<int32, FVector2D>& Stick : RightSticks)
	{
		UDreamUIInputUser* User = Input->GetUser(Stick.Key);
		UDreamWidget* Target = DreamUIKeyRouting::GetKeyTarget(User);
		if (!IsValid(Target))
		{
			continue;
		}
		if (FMath::Abs(Stick.Value.X) >= StickDeadzone)
		{
			FDreamUINavigationScroll::ScrollByAnalogAxis(Target, EKeys::Gamepad_RightX, FVector2D(Stick.Value.X * StickScrollSpeed * DeltaSeconds, 0.0f));
		}
		if (FMath::Abs(Stick.Value.Y) >= StickDeadzone)
		{
			// Up shows earlier content: a smaller scroll offset.
			FDreamUINavigationScroll::ScrollByAnalogAxis(Target, EKeys::Gamepad_RightY, FVector2D(0.0f, -Stick.Value.Y * StickScrollSpeed * DeltaSeconds));
		}
	}
}
