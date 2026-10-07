// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUISlateInputSource.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "Input/Events.h"
#include "Misc/ScopeExit.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamInputPipelineTestTypes.h"
#include "DreamPlayerScreenTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * INPUT HEARD FROM SLATE.
 *
 * The Slate input source is an input pre-processor: it sees the mouse, the keys and the sticks before the game
 * viewport does, and so in every input mode, the engine's UI-only one included. Here it is driven directly, with a
 * viewport that maps screen space one to one onto the rig's pixels, and never registered with Slate: the machine's
 * own mouse must not reach a test. The rig's driver stands a virtual cursor in for the mouse, and a virtual cursor is
 * where a mouse button presses; so every test but the one about that cursor puts it away, and moves the mouse itself.
 */
namespace DreamSlateInputSourceTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	FVector2D CentreOf(const UDreamWidget* InWidget)
	{
		const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(InWidget);
		return Pixel.IsSet() ? Pixel.GetValue() : FVector2D::ZeroVector;
	}

	/**
	 * A source for the rig's world: screen space is the rig's pixels, every Slate user is player 0. The mouse is the
	 * mouse, unless InKeepVirtualCursor keeps the driver's cursor in its place.
	 */
	TSharedRef<FDreamUISlateInputSource> MakeSource(FDreamDriverRig& InRig, bool bInKeepVirtualCursor = false)
	{
		if (UDreamDriverInputModule* Module = InRig.InputModule(); Module != nullptr && !bInKeepVirtualCursor)
		{
			Module->SetOverrideMousePosition(false);
		}
		TSharedRef<FDreamUISlateInputSource> Source = MakeShared<FDreamUISlateInputSource>(UDreamUIInputSubsystem::Get(InRig.GetWorld()));
		Source->SetViewportMapperForTesting([](const FVector2D& InScreen, FVector2D& OutPixel)
		{
			OutPixel = InScreen;
			return InScreen.X >= 0.0 && InScreen.Y >= 0.0 && InScreen.X < ViewportSize.X && InScreen.Y < ViewportSize.Y;
		});
		Source->SetUserMapperForTesting([](int32) { return 0; });
		return Source;
	}

	FPointerEvent MouseAt(const FVector2D& InAt)
	{
		return FPointerEvent(0, FSlateApplication::CursorPointerIndex, InAt, InAt, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
	}

	FPointerEvent LeftButtonAt(const FVector2D& InAt, bool bInDown)
	{
		TSet<FKey> Held;
		if (bInDown)
		{
			Held.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(0, FSlateApplication::CursorPointerIndex, InAt, InAt, Held, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	}

	FKeyEvent Key(const FKey& InKey, bool bInRepeat = false)
	{
		return FKeyEvent(InKey, FModifierKeysState(), 0, bInRepeat, 0, 0);
	}

	/** A key pressed with a chord held, as Slate delivers one: the modifiers ride on the event. */
	FKeyEvent KeyWith(const FKey& InKey, bool bInShift, bool bInCtrl)
	{
		const FModifierKeysState Chord(bInShift, false, bInCtrl, false, false, false, false, false, false);
		return FKeyEvent(InKey, Chord, 0, false, 0, 0);
	}

	/** A key from a particular input device, for Slate user 0. */
	FKeyEvent KeyFrom(const FKey& InKey, FInputDeviceId InDeviceId)
	{
		return FKeyEvent(InKey, FModifierKeysState(), InDeviceId, false, 0, 0, TOptional<int32>(0));
	}

	UDreamWidget* FaceOf(const UDreamButton* InButton)
	{
		return InButton != nullptr ? InButton->FaceNode.Get() : nullptr;
	}

	UDreamButton* MakeListenedButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, FVector2D(200.0, 60.0), InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		}
		return Button;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSlateWheelAfterNavigationTest,
	"DreamGUI.Input.SlateSource.AWheelAfterNavigationTracesItsPositionAndPreservesTheVirtualCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateWheelAfterNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	for (const bool bVirtualCursor : { false, true })
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("Rig came up"), Rig.IsUsable())
			|| !TestTrue(TEXT("UI began play"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld()))) { return false; }
		UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("WheelBox"), nullptr, FVector2D(300.0, 400.0));
		UDreamButton* Button = MakeListenedButton(Rig, TEXT("NavigationTarget"), FVector2D(-350.0, 250.0), nullptr);
		if (!TestNotNull(TEXT("Scroll box"), Box) || !TestNotNull(TEXT("Box content"), Box->GetContentNode())
			|| !TestNotNull(TEXT("Navigation button"), Button)) { return false; }
		for (int32 Row = 0; Row < 20; ++Row)
		{
			Rig.MakeWidget(FString::Printf(TEXT("WheelRow%d"), Row), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		Rig.PumpFrames(2);
		const FVector2D BoxPixel = CentreOf(Box->ViewportNode.Get());
		const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig, bVirtualCursor);
		FSlateApplication& Slate = FSlateApplication::Get();
		if (bVirtualCursor) { Rig.InputModule()->MoveTo(BoxPixel); }
		else { Source->HandleMouseMoveEvent(Slate, MouseAt(CentreOf(Button))); }
		Rig.PumpFrames(1);
		UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
		if (!TestTrue(TEXT("Button focused for navigation"), Input->FocusForNavigation(FaceOf(Button), 0))) { return false; }
		Source->HandleKeyDownEvent(Slate, Key(EKeys::Right));
		Rig.PumpFrames(1);
		Source->HandleKeyUpEvent(Slate, Key(EKeys::Right));
		Rig.PumpFrames(1);
		UDreamPointerEventData* Mouse = Input->GetUser(0)->FindPointerEventData(DreamUIPointerIds::Mouse);
		if (!TestNotNull(TEXT("Mouse pointer"), Mouse)) { return false; }
		TestEqual(TEXT("Keyboard left mouse in navigation mode"), Mouse->InputType, EDreamUIPointerInputType::Navigation);
		const FVector2D EventPixel = bVirtualCursor ? FVector2D(20.0, 20.0) : BoxPixel;
		auto Wheel = [EventPixel](float Delta)
		{
			return FPointerEvent(0, FSlateApplication::CursorPointerIndex, EventPixel, EventPixel,
				TSet<FKey>(), EKeys::Invalid, Delta, FModifierKeysState());
		};
		Source->HandleMouseWheelOrGestureEvent(Slate, Wheel(0.0f), nullptr);
		TestEqual(TEXT("Zero wheel delta leaves navigation alone"), Mouse->InputType, EDreamUIPointerInputType::Navigation);
		Source->HandleMouseWheelOrGestureEvent(Slate, Wheel(-1.0f), nullptr);
		Rig.PumpFrames(1);
		TestEqual(TEXT("Wheel restores pointer tracing"), Mouse->InputType, EDreamUIPointerInputType::Pointer);
		TestTrue(TEXT("Wheel traces at physical or virtual pointer position"), Mouse->PointerPosition.Equals(FVector(BoxPixel, 0.0)));
		TestNearlyEqual(TEXT("Wheel reaches the box rather than the navigation target"), Box->GetScrollOffset(),
			Box->GetScrollSensitivity() * Box->GetWheelScrollMultiplier(), 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceClickTest,
	"DreamGUI.Input.SlateSource.APressAndAReleaseHeardFromSlateClickAButtonOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateSourceClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D::ZeroVector, Listener.Get());
	if (!TestNotNull(TEXT("A button"), Button))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Button);

	TestFalse(TEXT("A move is never kept from the game"), Source->HandleMouseMoveEvent(Slate, MouseAt(At)));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(At, true));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The press heard from Slate pressed the button"), Listener->PressedCount, 1);
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(At, false));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and the release clicked it, once"), Listener->ClickedCount, 1);
	const UDreamUIInputUser* User = UDreamUIInputSubsystem::Get(Rig.GetWorld())->GetUser(0);
	const UDreamPointerEventData* Mouse = User != nullptr ? User->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("It moved the mouse's own pointer, where Slate said"), Mouse != nullptr && Mouse->PointerPosition.Equals(FVector(At, 0.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceVirtualCursorTest,
	"DreamGUI.Input.SlateSource.WhileAVirtualCursorStandsInForTheMouseItsButtonsPressWhereTheCursorIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateSourceVirtualCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Rig.InputModule() != nullptr))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D::ZeroVector, Listener.Get());
	if (!TestNotNull(TEXT("A button"), Button))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig, /*bInKeepVirtualCursor*/ true);
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D OnButton = CentreOf(Button);
	const FVector2D Elsewhere(20.0, 20.0);

	// The cursor on the button, the platform's mouse in a corner of the viewport.
	Rig.InputModule()->MoveTo(OnButton);
	Rig.PumpFrames(1);
	Source->HandleMouseMoveEvent(Slate, MouseAt(Elsewhere));
	Rig.PumpFrames(1);
	const UDreamUIInputUser* User = UDreamUIInputSubsystem::Get(Rig.GetWorld())->GetUser(0);
	const UDreamPointerEventData* Mouse = User != nullptr ? User->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("The platform's mouse moving does not move the cursor standing in for it"),
		Mouse != nullptr && Mouse->PointerPosition.Equals(FVector(OnButton, 0.0)));
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(Elsewhere, true));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(Elsewhere, false));
	Rig.PumpFrames(1);
	TestEqual(TEXT("Its button pressed where the cursor is, on the button"), Listener->PressedCount, 1);
	TestEqual(TEXT("...and clicked it, once"), Listener->ClickedCount, 1);
	TestTrue(TEXT("The cursor is still where it was"), Mouse != nullptr && Mouse->PointerPosition.Equals(FVector(OnButton, 0.0)));

	// Off the viewport the platform's click is not this world's, wherever the cursor is.
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(FVector2D(-50.0, -50.0), true));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(FVector2D(-50.0, -50.0), false));
	Rig.PumpFrames(1);
	TestEqual(TEXT("A click off the viewport presses nothing"), Listener->PressedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceConsumeTest,
	"DreamGUI.Input.SlateSource.WhatTheUIKeepsFromTheGameFollowsTheConsumePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateSourceConsumeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D(-300.0, 0.0), nullptr);
	UDreamWidget* Plain = Rig.MakeWidget(TEXT("Plain"), nullptr, FVector2D(200.0, 100.0), FVector2D(300.0, 0.0));
	if (!TestTrue(TEXT("A button and a plain widget"), Button != nullptr && IsValid(Plain)))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D OnButton = CentreOf(Button);
	const FVector2D OnPlain = CentreOf(Plain);
	const FVector2D OnNothing(20.0, 20.0);
	// A press where the pointer is, a frame after it moved there: the policy reads what the pointer is over.
	auto PressAndRelease = [&](const FVector2D& InAt, bool& bOutPressKept, bool& bOutReleaseKept)
	{
		Source->HandleMouseMoveEvent(Slate, MouseAt(InAt));
		Rig.PumpFrames(1);
		bOutPressKept = Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(InAt, true));
		Rig.PumpFrames(1);
		bOutReleaseKept = Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(InAt, false));
		Rig.PumpFrames(1);
	};
	bool bPressKept = false, bReleaseKept = false;
	TestTrue(TEXT("By default the UI keeps nothing, as the presets never did"), Source->GetConsumePolicy() == EDreamUIInputConsumePolicy::Never);

	// Keys first, before any pointer has hovered anything and left a navigation highlight behind. The space bar is a
	// confirm, and with nothing focused or highlighted it is the game's -- a jump.
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenHandled);
	TestFalse(TEXT("A confirm key with nothing to press is the game's"), Source->HandleKeyDownEvent(Slate, Key(EKeys::SpaceBar)));
	TestFalse(TEXT("...and so is its release"), Source->HandleKeyUpEvent(Slate, Key(EKeys::SpaceBar)));
	Rig.EventSystem()->SetSelectComponentWithDefault(Button);
	TestTrue(TEXT("With a button focused, it is the UI's"), Source->HandleKeyDownEvent(Slate, Key(EKeys::SpaceBar)));
	TestTrue(TEXT("...and so is its release"), Source->HandleKeyUpEvent(Slate, Key(EKeys::SpaceBar)));
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::Never);

	PressAndRelease(OnButton, bPressKept, bReleaseKept);
	TestFalse(TEXT("Never: a press on a button is the game's too"), bPressKept || bReleaseKept);

	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenOverUI);
	PressAndRelease(OnButton, bPressKept, bReleaseKept);
	TestTrue(TEXT("WhenOverUI: a press on the button is the UI's alone"), bPressKept);
	TestTrue(TEXT("...and so is its release"), bReleaseKept);
	PressAndRelease(OnPlain, bPressKept, bReleaseKept);
	TestTrue(TEXT("...as is a press on any UI"), bPressKept && bReleaseKept);
	PressAndRelease(OnNothing, bPressKept, bReleaseKept);
	TestFalse(TEXT("...but not one on nothing"), bPressKept || bReleaseKept);

	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenHandled);
	PressAndRelease(OnButton, bPressKept, bReleaseKept);
	TestTrue(TEXT("WhenHandled: a press a widget answers is the UI's"), bPressKept && bReleaseKept);
	PressAndRelease(OnPlain, bPressKept, bReleaseKept);
	TestFalse(TEXT("...one nothing answers is the game's"), bPressKept || bReleaseKept);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceTypingTest,
	"DreamGUI.Input.SlateSource.KeysTypedIntoAFieldAreAlwaysKeptAndBackStillEndsTheEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateSourceTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0));
	if (!TestTrue(TEXT("A field"), Field != nullptr && Field->InputBehaviour != nullptr))
	{
		return false;
	}
	UUITextInput::SetHostDeliversCharacterEventsForTesting(Rig.GetWorld(), false);
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Field);
	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(At, true));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(At, false));
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("A click heard from Slate began an edit"), Field->InputBehaviour->IsInputActive()))
	{
		return false;
	}

	// Never is the policy, and typing is kept all the same: a letter typed into a field is not the pawn's.
	TestTrue(TEXT("A key typed into the field is kept from the game, whatever the policy"), Source->HandleKeyDownEvent(Slate, Key(EKeys::A)));
	TestTrue(TEXT("...its repeat too"), Source->HandleKeyDownEvent(Slate, Key(EKeys::A, true)));
	TestTrue(TEXT("...and its release"), Source->HandleKeyUpEvent(Slate, Key(EKeys::A)));
	const bool bCapsLocked = Slate.GetModifierKeys().AreCapsLocked();
	const FString Letter = bCapsLocked ? TEXT("A") : TEXT("a");
	TestEqual(TEXT("The press and its repeat each typed the letter"), Field->GetText(), Letter + Letter);

	// Escape is not one of the field's keys: it goes the one road on, to Back, which ends the edit.
	TestFalse(TEXT("Escape is not typing, and under Never the UI keeps nothing else"), Source->HandleKeyDownEvent(Slate, Key(EKeys::Escape)));
	Source->HandleKeyUpEvent(Slate, Key(EKeys::Escape));
	TestFalse(TEXT("...and Back ended the edit"), Field->InputBehaviour->IsInputActive());
	TestFalse(TEXT("A letter with no field being edited is not typing"), Source->HandleKeyDownEvent(Slate, Key(EKeys::B)));
	Source->HandleKeyUpEvent(Slate, Key(EKeys::B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourcePresetStandsDownTest,
	"DreamGUI.Input.SlateSource.WhileItIsOnThePresetActorStandsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlateSourcePresetStandsDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up, with a preset actor and a player controller"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D::ZeroVector, Listener.Get());
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("A button and the world's input"), Button != nullptr && Input != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// This world has no game viewport, so nothing Slate hears is its own: switched on, the source only stands the preset down.
	Input->SetSlateInputSourceEnabled(true);
	TestTrue(TEXT("The Slate source is on"), Input->IsSlateInputSourceActive());
	Rig.Driver()->Find(FDreamBy::Widget(Button))->Click();
	TestEqual(TEXT("A click through the player controller reaches nothing while it is on"), Listener->ClickedCount, 0);

	Input->SetSlateInputSourceEnabled(false);
	TestFalse(TEXT("Switched off"), Input->IsSlateInputSourceActive());
	TestTrue(TEXT("A click through the controller completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("...and the preset clicks the button again"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceFocusLostTest,
	"DreamGUI.Input.SlateSource.WhatIsHeldWhenTheApplicationLosesTheFocusIsLetGoOfWithoutAClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A mouse button or a key held while the application goes to the background -- Alt+Tab with it down -- is let go of in
 * the other application, and Slate never hears that release: the button under the pointer stayed pressed for good, a
 * held D-pad direction went on stepping the highlight, and a held confirm clicked whenever a release finally came. The
 * source now lets go of everything it holds when FSlateApplication announces the application is no longer active: a press
 * gets its up and no click, a held direction stops. Checked by broadcasting that announcement as Slate makes it, with the
 * left button held on a button, then the D-pad held, then the confirm key held on the focused button.
 */
bool FDreamSlateSourceFocusLostTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> PlayListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> QuitListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Play = MakeListenedButton(Rig, TEXT("Play"), FVector2D(-200.0, 0.0), PlayListener.Get());
	UDreamButton* Quit = MakeListenedButton(Rig, TEXT("Quit"), FVector2D(200.0, 0.0), QuitListener.Get());
	if (!TestTrue(TEXT("Two buttons"), Play != nullptr && Quit != nullptr))
	{
		return false;
	}
	Play->OnReleased.AddDynamic(PlayListener.Get(), &UDreamPressInteractionListener::HandleReleased);
	Quit->OnReleased.AddDynamic(QuitListener.Get(), &UDreamPressInteractionListener::HandleReleased);
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Play);
	auto Presses = [&PlayListener, &QuitListener]() { return PlayListener->PressedCount + QuitListener->PressedCount; };
	auto Releases = [&PlayListener, &QuitListener]() { return PlayListener->ReleasedCount + QuitListener->ReleasedCount; };
	auto Clicks = [&PlayListener, &QuitListener]() { return PlayListener->ClickedCount + QuitListener->ClickedCount; };

	// The left button held on a button, and the application sent to the background.
	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(At, true));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The press heard from Slate pressed the button"), PlayListener->PressedCount, 1);
	Slate.OnApplicationActivationStateChanged().Broadcast(false);
	Rig.PumpFrames(2);
	TestEqual(TEXT("The application losing the focus let go of the press"), PlayListener->ReleasedCount, 1);
	TestEqual(TEXT("...with no click"), PlayListener->ClickedCount, 0);
	const UDreamUIInputUser* User = UDreamUIInputSubsystem::Get(Rig.GetWorld())->GetUser(0);
	const UDreamPointerEventData* Mouse = User != nullptr ? User->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("...and the mouse's pointer is no longer pressed"), Mouse != nullptr && !Mouse->bNowIsTriggerPressed);
	// Should a release reach Slate after all, it belongs to no press of this source's any more.
	TestFalse(TEXT("A release heard afterwards is not the UI's"), Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(At, false)));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and clicks nothing"), PlayListener->ClickedCount, 0);

	// The D-pad held, and the application sent to the background again.
	Source->HandleKeyDownEvent(Slate, Key(EKeys::Gamepad_DPad_Right));
	Rig.PumpFrames(2);
	TestTrue(TEXT("The D-pad heard from Slate holds its direction"),
		Mouse != nullptr && Mouse->NavigateDirection == EDreamUINavigationDirection::Right);
	Slate.OnApplicationActivationStateChanged().Broadcast(false);
	TestTrue(TEXT("The application losing the focus let go of the direction"),
		Mouse != nullptr && Mouse->NavigateDirection == EDreamUINavigationDirection::None && Mouse->HeldNavigateDirections.Num() == 0);
	Source->HandleKeyUpEvent(Slate, Key(EKeys::Gamepad_DPad_Right));
	Rig.PumpFrames(1);

	// The confirm held on whichever button the D-pad left the focus on.
	const int32 PressesBefore = Presses();
	const int32 ReleasesBefore = Releases();
	Source->HandleKeyDownEvent(Slate, Key(EKeys::Enter));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The confirm heard from Slate pressed the focused button"), Presses(), PressesBefore + 1);
	Slate.OnApplicationActivationStateChanged().Broadcast(false);
	Rig.PumpFrames(2);
	TestEqual(TEXT("The application losing the focus let go of the confirm's press"), Releases(), ReleasesBefore + 1);
	TestEqual(TEXT("...with no click, from either button"), Clicks(), 0);
	Source->HandleKeyUpEvent(Slate, Key(EKeys::Enter));
	Rig.PumpFrames(1);
	TestEqual(TEXT("A release of the confirm heard afterwards clicks nothing either"), Clicks(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceUMGOnTopTest,
	"DreamGUI.Input.SlateSource.APointerOverUMGOnTopAndKeysAFocusedUMGWidgetTakesAreNotTheUIsUnderneath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The source took every pointer event on the viewport's area for DreamUI, and every key while the keyboard focus was
 * anywhere inside the viewport: a UMG widget drawn over a DreamUI button passed its clicks through to the button, and a
 * UMG text box in the viewport's overlay had its keys taken by DreamUI's bindings and navigation as well. Pointer events
 * now reach DreamUI only where the widget Slate's hit test finds under the pointer is the viewport itself, and keys only
 * while the viewport itself has the keyboard focus. A headless editor paints no hit-test grid and has no game viewport to
 * focus, so both answers are handed to the source through its seams: what is checked is what the source does with them.
 */
bool FDreamSlateSourceUMGOnTopTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), FVector2D(-200.0, 0.0), Listener.Get());
	UDreamWidget* Focused = Rig.MakeWidget(TEXT("Focused"), nullptr, FVector2D(200.0, 100.0), FVector2D(200.0, 0.0));
	UDreamKeyRecordingBehaviour* Recorder = IsValid(Focused) ? Focused->AddComponent<UDreamKeyRecordingBehaviour>() : nullptr;
	if (!TestTrue(TEXT("A button, and a widget that hears keys"), Button != nullptr && Recorder != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	bool bUMGOnTop = true;
	Source->SetCoverMapperForTesting([&bUMGOnTop](const FVector2D&) { return bUMGOnTop; });
	bool bViewportFocused = false;
	Source->SetKeyboardFocusMapperForTesting([&bViewportFocused](int32) { return bViewportFocused; });
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Button);
	auto ClickAt = [&](const FVector2D& InAt, bool& bOutPressKept)
	{
		Source->HandleMouseMoveEvent(Slate, MouseAt(InAt));
		Rig.PumpFrames(1);
		bOutPressKept = Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(InAt, true));
		Rig.PumpFrames(1);
		Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(InAt, false));
		Rig.PumpFrames(1);
	};
	bool bPressKept = false;

	// A UMG widget drawn over the button takes the click.
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenOverUI);
	ClickAt(At, bPressKept);
	TestEqual(TEXT("A click on a UMG widget drawn over the button does not press it"), Listener->PressedCount, 0);
	TestEqual(TEXT("...nor click it"), Listener->ClickedCount, 0);
	TestFalse(TEXT("...and is not kept from the UMG widget"), bPressKept);

	// The UMG widget gone, the same click is the button's.
	bUMGOnTop = false;
	ClickAt(At, bPressKept);
	TestEqual(TEXT("With nothing drawn over it, the click presses the button"), Listener->PressedCount, 1);
	TestEqual(TEXT("...and clicks it"), Listener->ClickedCount, 1);
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::Never);

	// Keys: a UMG widget holding the keyboard focus has them, and DreamUI's focused widget does not.
	Rig.EventSystem()->SetSelectComponentWithDefault(Focused);
	TestFalse(TEXT("A key while a UMG widget has the keyboard focus is not the UI's"), Source->HandleKeyDownEvent(Slate, Key(EKeys::F)));
	TestFalse(TEXT("...nor its repeat"), Source->HandleKeyDownEvent(Slate, Key(EKeys::F, true)));
	TestFalse(TEXT("...nor its release"), Source->HandleKeyUpEvent(Slate, Key(EKeys::F)));
	TestEqual(TEXT("...and DreamUI's focused widget never heard it"), Recorder->KeyDownCount, 0);
	bViewportFocused = true;
	Source->HandleKeyDownEvent(Slate, Key(EKeys::F));
	Source->HandleKeyUpEvent(Slate, Key(EKeys::F));
	TestEqual(TEXT("With the viewport itself focused, DreamUI's focused widget hears the key"), Recorder->KeyDownCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceMouseLeavesTest,
	"DreamGUI.Input.SlateSource.TheMouseLeavingTheViewportLeavesWhatItWasOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A move off the viewport with no button held was ignored, so the mouse's pointer stayed where the mouse was last seen on
 * the viewport, over the widget at the edge it went out by: that widget's hover look, its tooltip and its cursor stayed on
 * while the mouse was over another window. The pointer is now put over nothing when the mouse leaves -- the viewport, or
 * for a UMG widget drawn on top of it -- and a pointer held down is still followed off the viewport, as a drag is.
 */
bool FDreamSlateSourceMouseLeavesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Edge = Rig.MakeWidget(TEXT("Edge"), nullptr, FVector2D(200.0, 100.0), FVector2D(500.0, 0.0));
	UDreamPointerLedger* Ledger = IsValid(Edge) ? Edge->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestNotNull(TEXT("A widget by the viewport's edge, keeping books"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	bool bUMGOnTop = false;
	Source->SetCoverMapperForTesting([&bUMGOnTop](const FVector2D&) { return bUMGOnTop; });
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Edge);
	const FVector2D PastTheEdge(ViewportSize.X + 40.0, At.Y);

	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The mouse is over the widget"), Ledger->Enter, 1);
	Source->HandleMouseMoveEvent(Slate, MouseAt(PastTheEdge));
	Rig.PumpFrames(1);
	TestEqual(TEXT("Gone past the viewport's edge, it has left the widget"), Ledger->Exit, 1);

	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	TestEqual(TEXT("Back over the widget"), Ledger->Enter, 2);
	bUMGOnTop = true;
	Source->HandleMouseMoveEvent(Slate, MouseAt(At + FVector2D(2.0, 0.0)));
	Rig.PumpFrames(1);
	TestEqual(TEXT("Onto a UMG widget drawn over it, it has left it too"), Ledger->Exit, 2);
	bUMGOnTop = false;

	// Held, the pointer is followed past the edge: a drag dragged off the viewport is still that drag.
	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(At, true));
	Rig.PumpFrames(1);
	Source->HandleMouseMoveEvent(Slate, MouseAt(PastTheEdge));
	Rig.PumpFrames(1);
	const UDreamUIInputUser* User = UDreamUIInputSubsystem::Get(Rig.GetWorld())->GetUser(0);
	const UDreamPointerEventData* Mouse = User != nullptr ? User->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("A held pointer is still followed past the edge"), Mouse != nullptr && Mouse->PointerPosition.Equals(FVector(PastTheEdge, 0.0)));
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(PastTheEdge, false));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and its release lets go of it"), Ledger->Up, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceCursorLeftWindowTest,
	"DreamGUI.Input.SlateSource.TheCursorLeavingTheWindowWithNoMoveHeardLeavesWhatTheMouseWasOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The mouse's pointer was put over nothing only when a move off the viewport was heard, and leaving a borderless or
 * fullscreen window sends none that this source hears: Slate synthesizes the moves that tell the viewport the cursor has
 * gone, and synthesized moves skip the input pre-processors. The pointer went on hovering the widget at the edge it went
 * out by. The source now asks every tick where the viewport last saw the cursor -- FSceneViewport parks it at (-1,-1)
 * when the cursor leaves -- and puts the mouse over nothing once it has gone, unless a button holds it. Checked with the
 * viewport's answer stood in for: the cursor gone with no move heard, and gone again with a button held.
 */
bool FDreamSlateSourceCursorLeftWindowTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Edge = Rig.MakeWidget(TEXT("Edge"), nullptr, FVector2D(200.0, 100.0), FVector2D(500.0, 0.0));
	UDreamPointerLedger* Ledger = IsValid(Edge) ? Edge->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestNotNull(TEXT("A widget by the viewport's edge, keeping books"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	bool bCursorOnViewport = true;
	Source->SetCursorOnViewportMapperForTesting([&bCursorOnViewport]() { return bCursorOnViewport; });
	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D At = CentreOf(Edge);

	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The mouse is over the widget"), Ledger->Enter, 1);
	Source->FollowCursorOffViewport(0);
	Rig.PumpFrames(1);
	TestEqual(TEXT("While the viewport has the cursor, the mouse stays over the widget"), Ledger->Exit, 0);
	bCursorOnViewport = false;
	Source->FollowCursorOffViewport(0);
	Rig.PumpFrames(1);
	TestEqual(TEXT("The cursor gone from the viewport with no move heard, the mouse has left the widget"), Ledger->Exit, 1);

	// Held, the pointer stays where its press goes on, as a held pointer is followed past the edge.
	bCursorOnViewport = true;
	Source->HandleMouseMoveEvent(Slate, MouseAt(At));
	Rig.PumpFrames(1);
	Source->HandleMouseButtonDownEvent(Slate, LeftButtonAt(At, true));
	Rig.PumpFrames(1);
	bCursorOnViewport = false;
	Source->FollowCursorOffViewport(0);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Gone with a button held, the mouse is still over the widget it pressed"), Ledger->Exit, 1);
	Source->HandleMouseButtonUpEvent(Slate, LeftButtonAt(At, false));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and its release lets go of it"), Ledger->Up, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceTabConsumeTest,
	"DreamGUI.Input.SlateSource.ATabThatMovesTheFocusIsKeptFromTheGameAndOneWithNowhereToGoIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Whether Tab was the UI's was judged from the hover highlight, and modifiers were not asked at all. A Tab is the UI's now
 * while the player has a Tab stop to go to -- kept from the game under a policy that keeps what the UI takes -- and Slate's
 * chord rides on the key: Shift+Tab steps back, Ctrl+Tab is no step and not the UI's. Two buttons side by side.
 */
bool FDreamSlateSourceTabConsumeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Left = MakeListenedButton(Rig, TEXT("Left"), FVector2D(-250.0, 0.0), nullptr);
	UDreamButton* Right = MakeListenedButton(Rig, TEXT("Right"), FVector2D(250.0, 0.0), nullptr);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("Two buttons and the world's input"), FaceOf(Left) != nullptr && FaceOf(Right) != nullptr && Services != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenHandled);
	if (!TestTrue(TEXT("The left button has the focus"), Services->FocusForNavigation(FaceOf(Left), 0)))
	{
		return false;
	}
	// A press, a frame for the step it asked for, its release.
	auto Press = [&](const FKeyEvent& InEvent, bool& bOutPressKept, bool& bOutReleaseKept)
	{
		bOutPressKept = Source->HandleKeyDownEvent(Slate, InEvent);
		Rig.PumpFrames(1);
		bOutReleaseKept = Source->HandleKeyUpEvent(Slate, InEvent);
		Rig.PumpFrames(1);
	};
	bool bPressKept = false, bReleaseKept = false;

	Press(Key(EKeys::Tab), bPressKept, bReleaseKept);
	TestEqual(TEXT("Tab moved the focus on"), Services->GetFocusedWidget(0), FaceOf(Right));
	TestTrue(TEXT("...and was kept from the game"), bPressKept && bReleaseKept);

	Press(KeyWith(EKeys::Tab, /*bInShift*/ true, /*bInCtrl*/ false), bPressKept, bReleaseKept);
	TestEqual(TEXT("Shift+Tab, its shift on the event, stepped back"), Services->GetFocusedWidget(0), FaceOf(Left));
	TestTrue(TEXT("...and was kept too"), bPressKept);

	Press(KeyWith(EKeys::Tab, /*bInShift*/ false, /*bInCtrl*/ true), bPressKept, bReleaseKept);
	TestEqual(TEXT("Ctrl+Tab is no step"), Services->GetFocusedWidget(0), FaceOf(Left));
	TestFalse(TEXT("...and, nothing having taken it, is the game's"), bPressKept || bReleaseKept);

	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::Never);
	Press(Key(EKeys::Tab), bPressKept, bReleaseKept);
	TestEqual(TEXT("Under Never, Tab still steps"), Services->GetFocusedWidget(0), FaceOf(Right));
	TestFalse(TEXT("...and keeps nothing from the game"), bPressKept || bReleaseKept);
	Source->SetConsumePolicy(EDreamUIInputConsumePolicy::WhenHandled);

	// No Tab stop anywhere: the Tab goes nowhere, and is the game's.
	FaceOf(Left)->SetIsTabStop(false);
	FaceOf(Right)->SetIsTabStop(false);
	Press(Key(EKeys::Tab), bPressKept, bReleaseKept);
	TestEqual(TEXT("With no Tab stop the focus stays"), Services->GetFocusedWidget(0), FaceOf(Right));
	TestFalse(TEXT("...and the Tab, which moved nothing, is not kept"), bPressKept || bReleaseKept);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourcePadModelTest,
	"DreamGUI.Input.SlateSource.APadsModelIsReadFromTheDeviceItsKeyCameFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A Slate key event names the input device that sent it, and the pad's model is read from that device: a key from a
 * PlayStation pad says PlayStation, and then one from an Xbox pad says Xbox, with the device class Gamepad all along.
 * Two pads the device registry is told about for the test.
 */
bool FDreamSlateSourcePadModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateInputSourceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUIInputUser* User = Rig.IsUsable() ? UDreamUIInputSubsystem::Get(Rig.GetWorld())->GetUser(0) : nullptr;
	if (!TestNotNull(TEXT("A player"), User))
	{
		return false;
	}
	const FInputDeviceId XboxPad = FInputDeviceId::CreateFromInternalId(4201);
	const FInputDeviceId SonyPad = FInputDeviceId::CreateFromInternalId(4202);
	FInputDeviceDescriptor XboxDescriptor;
	XboxDescriptor.HardwareDeviceHandle = XboxPad;
	XboxDescriptor.InputDeviceName = TEXT("XInputInterface");
	XboxDescriptor.HardwareDeviceIdentifier = TEXT("XInputController");
	FInputDeviceDescriptor SonyDescriptor;
	SonyDescriptor.HardwareDeviceHandle = SonyPad;
	SonyDescriptor.InputDeviceName = TEXT("PS5Controller");
	SonyDescriptor.HardwareDeviceIdentifier = TEXT("DualSense");
	FInputDeviceRegistry::SetSimulatedDescriptor(XboxPad, XboxDescriptor);
	FInputDeviceRegistry::SetSimulatedDescriptor(SonyPad, SonyDescriptor);
	ON_SCOPE_EXIT
	{
		FInputDeviceRegistry::ClearSimulatedDescriptor(XboxPad);
		FInputDeviceRegistry::ClearSimulatedDescriptor(SonyPad);
	};
	int32 ModelChanges = 0;
	const FDelegateHandle Listening = User->GetGamepadModelChangedEvent().AddLambda([&ModelChanges](EDreamUIGamepadModel) { ++ModelChanges; });
	ON_SCOPE_EXIT{ User->GetGamepadModelChangedEvent().Remove(Listening); };
	const TSharedRef<FDreamUISlateInputSource> Source = MakeSource(Rig);
	FSlateApplication& Slate = FSlateApplication::Get();

	Source->HandleKeyDownEvent(Slate, KeyFrom(EKeys::Gamepad_FaceButton_Left, SonyPad));
	Source->HandleKeyUpEvent(Slate, KeyFrom(EKeys::Gamepad_FaceButton_Left, SonyPad));
	TestEqual(TEXT("A key from the PlayStation pad"), User->GetCurrentInputDevice(), EDreamUIInputDevice::Gamepad);
	TestEqual(TEXT("...reads that pad's model"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::PlayStation);
	const int32 ChangesAfterSony = ModelChanges;

	Source->HandleKeyDownEvent(Slate, KeyFrom(EKeys::Gamepad_FaceButton_Left, XboxPad));
	Source->HandleKeyUpEvent(Slate, KeyFrom(EKeys::Gamepad_FaceButton_Left, XboxPad));
	TestEqual(TEXT("A key from the Xbox pad, the device class unchanged, reads the Xbox pad's"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::Xbox);
	TestEqual(TEXT("...and says so, once"), ModelChanges, ChangesAfterSony + 1);
	Rig.PumpFrames(1);
	return true;
}

#endif
