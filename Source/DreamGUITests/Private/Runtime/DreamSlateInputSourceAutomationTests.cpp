// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUISlateInputSource.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

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

#endif
