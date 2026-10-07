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
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace DreamUISlateInputSourceLocal
{
	/** Below this the right stick is at rest. */
	static constexpr float StickDeadzone = 0.2f;

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

	/**
	 * The mouse gone from what this world's UI answers for: its pointer is put over nothing -- (-1,-1), where
	 * FSceneViewport parks a cursor that has left it -- and what it was over is left. Not while a virtual cursor stands
	 * in for it, which is where it is, nor while the pointer navigates, which traces nothing there.
	 */
	void ParkMouseOffTheViewport(UDreamUIInputUser* InUser)
	{
		const FVector OffTheViewport = DreamUIPointerPosition::OffViewport();
		const UDreamPointerEventData* Mouse = InUser->FindPointerEventData(DreamUIPointerIds::Mouse);
		FVector2D VirtualCursor = FVector2D::ZeroVector;
		if (Mouse == nullptr || Mouse->InputType != EDreamUIPointerInputType::Pointer || FindVirtualCursor(InUser, VirtualCursor)
			|| Mouse->PointerPosition.Equals(OffTheViewport))
		{
			return;//not the mouse's to move, or over nothing already
		}
		InUser->MovePointer(DreamUIPointerIds::Mouse, OffTheViewport);
	}
}

FDreamUISlateInputSource::FDreamUISlateInputSource(UDreamUIInputSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
	, ConsumePolicy(GetDefault<UDreamGUISettings>()->SlateInputConsumePolicy)
{
	// Heard from the start, registered with Slate or not: what it lets go of is what this source holds, not input it
	// hears.
	if (FSlateApplication::IsInitialized())
	{
		ActivationChangedHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddRaw(
			this, &FDreamUISlateInputSource::HandleApplicationActivationStateChanged);
	}
}

FDreamUISlateInputSource::~FDreamUISlateInputSource()
{
	if (ActivationChangedHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationChangedHandle);
	}
}

void FDreamUISlateInputSource::SetViewportMapperForTesting(TFunction<bool(const FVector2D&, FVector2D&)> InMapper)
{
	ViewportMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::SetUserMapperForTesting(TFunction<int32(int32)> InMapper)
{
	UserMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::SetCoverMapperForTesting(TFunction<bool(const FVector2D&)> InMapper)
{
	CoverMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::SetKeyboardFocusMapperForTesting(TFunction<bool(int32)> InMapper)
{
	KeyboardFocusMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::SetCursorOnViewportMapperForTesting(TFunction<bool()> InMapper)
{
	CursorOnViewportMapper = MoveTemp(InMapper);
}

void FDreamUISlateInputSource::FollowCursorOffViewport(int32 InMouseSlateUserIndex)
{
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return;
	}
	// Only a player the mouse has moved for already: one with no input yet has no pointer to put anywhere.
	const int32 UserIndex = FindUserIndex(InMouseSlateUserIndex);
	UDreamUIInputUser* User = UserIndex != INDEX_NONE ? Input->GetUser(UserIndex) : nullptr;
	if (User == nullptr || IsCursorOnViewport())
	{
		return;
	}
	// A press held is followed wherever the mouse goes, as a move off the viewport follows it: its release is still coming.
	const UDreamPointerEventData* Mouse = User->FindPointerEventData(DreamUIPointerIds::Mouse);
	if (Mouse == nullptr || Mouse->bNowIsTriggerPressed
		|| HeldPresses.Contains(TPair<int32, int32>(User->GetUserIndex(), DreamUIPointerIds::Mouse)))
	{
		return;
	}
	DreamUISlateInputSourceLocal::ParkMouseOffTheViewport(User);
}

bool FDreamUISlateInputSource::IsCursorOnViewport() const
{
	if (CursorOnViewportMapper)
	{
		return CursorOnViewportMapper();
	}
	if (ViewportMapper)
	{
		return true;//a test's world has no viewport for the cursor to leave
	}
	const UWorld* World = GetWorld();
	const UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
	if (Viewport == nullptr)
	{
		return true;//nothing says the cursor has gone
	}
	// FSceneViewport's cached cursor, in its pixels: (-1,-1) once the cursor has left it, whether or not a move said so.
	FIntPoint Cursor;
	Viewport->GetMousePos(Cursor, /*bLocalPosition*/ true);
	const FIntPoint Size = Viewport->GetSizeXY();
	return Cursor.X >= 0 && Cursor.Y >= 0 && Cursor.X < Size.X && Cursor.Y < Size.Y;
}

void FDreamUISlateInputSource::HandleApplicationActivationStateChanged(bool bInIsActive)
{
	if (bInIsActive)
	{
		return;
	}
	// Taken first: letting go runs game code, and a press it makes is the next one, not one of these.
	const TSet<TPair<int32, int32>> Presses = MoveTemp(HeldPresses);
	const TSet<TPair<int32, FKey>> Keys = MoveTemp(RoutedKeys);
	HeldPresses.Reset();
	RoutedKeys.Reset();
	ConsumedPresses.Reset();
	ConsumedKeys.Reset();
	// A stick still tilted is at rest as far as this application will hear: nothing more of it comes.
	RightSticks.Reset();
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr)
	{
		return;
	}
	for (const TPair<int32, int32>& Press : Presses)
	{
		UDreamUIInputUser* User = Input->GetUser(Press.Key);
		if (User == nullptr)
		{
			continue;
		}
		// The up, and no click and no drop: nothing was let go of over anything here.
		User->CancelPointerPress(Press.Value);
		if (Press.Value != DreamUIPointerIds::Mouse)
		{
			User->RetirePointer(Press.Value);//a finger lifted elsewhere is gone, as a lifted finger is
		}
	}
	for (const TPair<int32, FKey>& Key : Keys)
	{
		if (UDreamUIInputUser* User = Input->GetUser(Key.Key))
		{
			DreamUIKeyRouting::AbandonKeyPress(User, Key.Value);
		}
	}
}

UWorld* FDreamUISlateInputSource::GetWorld() const
{
	const UDreamUIInputSubsystem* Input = Subsystem.Get();
	return Input != nullptr ? Input->GetWorld() : nullptr;
}

int32 FDreamUISlateInputSource::FindUserIndex(int32 InSlateUserIndex) const
{
	if (UserMapper)
	{
		return UserMapper(InSlateUserIndex);
	}
	// The local player whose Slate user it is -- every Slate user, with one local player -- as the world's Slate guard
	// reckons it too.
	const UDreamUIInputSubsystem* Input = Subsystem.Get();
	return Input != nullptr ? Input->FindUserIndexForSlateUser(InSlateUserIndex) : INDEX_NONE;
}

UDreamUIInputUser* FDreamUISlateInputSource::FindUser(int32 InSlateUserIndex) const
{
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr)
	{
		return nullptr;
	}
	const int32 UserIndex = FindUserIndex(InSlateUserIndex);
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

bool FDreamUISlateInputSource::IsCoveredBySlate(const FVector2D& InScreen, int32 InSlateUserIndex) const
{
	if (CoverMapper)
	{
		return CoverMapper(InScreen);
	}
	if (ViewportMapper || !FSlateApplication::IsInitialized())
	{
		return false;//a test's world has no viewport for anything to be drawn over
	}
	const UWorld* World = GetWorld();
	UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
	if (!Viewport.IsValid())
	{
		return false;
	}
	// Slate's own hit test, the one it routes this very event by: the deepest widget that takes a pointer there. The
	// layers the viewport and UMG stack over the scene are hit-test invisible where nothing is drawn -- the layer
	// manager's panels, the debug canvas, an editor viewport's border -- so over bare viewport that is the viewport
	// itself. Anything else is what the pointer is over: a UMG widget, or another window on top.
	FSlateApplication& SlateApp = FSlateApplication::Get();
	const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(InScreen, SlateApp.GetInteractiveTopLevelWindows(), false, InSlateUserIndex);
	return !Path.IsValid() || &Path.GetLastWidget().Get() != static_cast<SWidget*>(Viewport.Get());
}

bool FDreamUISlateInputSource::IsKeyForThisWorld(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) const
{
	if (KeyboardFocusMapper)
	{
		return KeyboardFocusMapper(InKeyEvent.GetUserIndex());
	}
	if (ViewportMapper)
	{
		return true;//a test's world has no viewport to hold the focus
	}
	const UWorld* World = GetWorld();
	UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
	const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(InKeyEvent.GetUserIndex());
	// The viewport itself, as Slate routes a key: to the focused widget, so a UMG widget focused in the viewport's
	// overlay -- a text box being typed into -- has the key before the viewport hears of it. An input mode that names
	// no widget to focus focuses the viewport.
	return Viewport.IsValid() && Focused.IsValid() && Focused.Get() == static_cast<SWidget*>(Viewport.Get());
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
	// The mouse really moving is the player on the mouse: the device it reports brings the cursor back where a pad hid it,
	// and the prompts back to the keyboard's. A finger reports itself as it presses.
	if (PointerID == DreamUIPointerIds::Mouse && !MouseEvent.GetCursorDelta().IsNearlyZero())
	{
		User->ReportInputKey(EKeys::Mouse2D);
	}
	FVector2D Pixel;
	bool bInside = false;
	if (!MapToViewport(MouseEvent.GetScreenSpacePosition(), Pixel, bInside))
	{
		return false;
	}
	// A pointer held down is followed off the viewport too, and over UMG, as the viewport's own capture follows it: a
	// drag dragged past the edge is still that drag.
	const UDreamPointerEventData* Existing = User->FindPointerEventData(PointerID);
	const bool bHeld = Existing != nullptr && Existing->bNowIsTriggerPressed;
	if (!bHeld && (!bInside || IsCoveredBySlate(MouseEvent.GetScreenSpacePosition(), MouseEvent.GetUserIndex())))
	{
		// Gone from this world's UI -- off the viewport, or onto a widget drawn over it -- the mouse is over none of it.
		// Left where it last was, its pointer kept hovering the widget at the edge it went out by.
		if (PointerID == DreamUIPointerIds::Mouse)
		{
			DreamUISlateInputSourceLocal::ParkMouseOffTheViewport(User);
		}
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
	// A release goes where its press went -- and nowhere, for a press this source did not queue: one a UMG widget drawn
	// over the viewport took is that widget's, release and all.
	if (!bInPressed && !HeldPresses.Contains(PressKey))
	{
		return false;
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
		if (!bInside || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld())
			|| IsCoveredBySlate(InEvent.GetScreenSpacePosition(), InEvent.GetUserIndex()))
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
		User->ReportInputKey(InEvent.IsTouchEvent() ? EKeys::TouchKeys[0] : InEvent.GetEffectingButton(), InEvent.GetInputDeviceId());
		User->QueuePointerButton(PointerID, FVector(Pixel, 0.0), true, Button, InEvent.IsTouchEvent());
		HeldPresses.Add(PressKey);
		const bool bConsume = WouldConsumePress(User, PointerID);
		if (bConsume)
		{
			ConsumedPresses.Add(PressKey);
		}
		return bConsume;
	}
	// On the viewport or off it -- and kept from the game when its press was.
	HeldPresses.Remove(PressKey);
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
	if (!MapToViewport(InWheelEvent.GetScreenSpacePosition(), Pixel, bInside) || !bInside
		|| IsCoveredBySlate(InWheelEvent.GetScreenSpacePosition(), InWheelEvent.GetUserIndex()))
	{
		return false;
	}
	User->ReportInputKey(EKeys::MouseWheelAxis);
	// A wheel is pointer input even without a preceding move. Trace at the event's position, or
	// at the virtual cursor that currently stands in for the mouse, instead of the navigation target.
	FVector2D VirtualCursor;
	if (DreamUISlateInputSourceLocal::FindVirtualCursor(User, VirtualCursor))
	{
		Pixel = VirtualCursor;
	}
	User->MovePointer(DreamUIPointerIds::Mouse, FVector(Pixel, 0.0));
	User->SetPointerInputType(User->GetPointerEventData(DreamUIPointerIds::Mouse, true), EDreamUIPointerInputType::Pointer);
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
		// the press. Kept from the game as its press was -- and not heard at all when its press was not, which a UMG
		// widget holding the focus took.
		if (!RoutedKeys.Contains(KeyOfUser))
		{
			return false;
		}
		const bool bTyped = DreamUIKeyRouting::RouteTextKey(User, Key, true, InKeyEvent.GetModifierKeys());
		return bTyped || ConsumedKeys.Contains(KeyOfUser);
	}
	if (!IsKeyForThisWorld(SlateApp, InKeyEvent) || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return false;
	}
	RoutedKeys.Add(KeyOfUser);
	// The event names the device that sent it: a pad's model is read from that pad, not guessed.
	User->ReportInputKey(Key, InKeyEvent.GetInputDeviceId());
	bool bTyped = false;
	// With its chord: Tab is no step with Ctrl, Alt or Cmd held, and Shift+Tab steps back.
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
			User->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(Key), InAnalogInputEvent.GetInputDeviceId());
		}
	}
	return WouldConsumeKey(bTaken, false);
}

void FDreamUISlateInputSource::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	using namespace DreamUISlateInputSourceLocal;
	FollowCursorOffViewport(SlateApp.GetUserIndexForMouse());
	UDreamUIInputSubsystem* Input = Subsystem.Get();
	if (Input == nullptr || RightSticks.Num() == 0 || DreamUIKeyRouting::IsInputSuspendedByGamePause(GetWorld()))
	{
		return;
	}
	// The UI clock: a stick scrolls a list as fast in a slowed-down game as at full speed. The speed is the project's
	// (UDreamGUISettings::SlateInputStickScrollSpeed), read every tick so a change takes at once.
	const float DeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(GetWorld(), DeltaTime);
	const float StickScrollSpeed = FMath::Max(0.0f, UDreamGUISettings::Get()->SlateInputStickScrollSpeed);
	for (const TPair<int32, FVector2D>& Stick : RightSticks)
	{
		UDreamUIInputUser* User = Input->GetUser(Stick.Key);
		// In gameplay (input mode Game) the right stick is the game's camera, not the HUD's scroll bar.
		UDreamWidget* Target = DreamUIKeyRouting::HasBuiltInMeanings(User) ? DreamUIKeyRouting::GetKeyTarget(User) : nullptr;
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
