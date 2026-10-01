// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "Controls/DreamInputKeySelector.h"
#include "Framework/Commands/InputChord.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverGameHost.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A KEY BINDER, used the way a settings screen is used: click it, press the key you want.
 *
 * UMG's InputKeySelector arms on a click (OnIsSelectingKeyChanged), takes the next key as its
 * selection (OnKeySelected) and disarms; an escape key disarms without binding anything. The key
 * reaches this control through NotifyChordPressed -- the entry its own capture agent feeds and the
 * one a project's input layer is told to call -- which is what an armed selector hears first in a
 * game, since its agent sits at the top of the player's input stack.
 */
namespace DreamInputKeySelectorInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamInputKeySelector* MakeObservedSelector(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamInputKeySelector* Selector = InRig.MakeControl<UDreamInputKeySelector>(TEXT("Jump"), nullptr, FVector2D(220.0, 60.0));
		if (Selector != nullptr && InListener != nullptr)
		{
			Selector->OnKeySelected.AddDynamic(InListener, &UDreamTextInteractionListener::HandleKeySelected);
			Selector->OnIsListeningChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleListeningChanged);
		}
		return Selector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorBindsTheNextKeyTest,
	"DreamGUI.InputKeySelector.ClickingArmsItAndTheNextKeyPressedBecomesTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamInputKeySelectorBindsTheNextKeyTest, "DreamGUI.InputKeySelector.ClickingArmsItAndTheNextKeyPressedBecomesTheBinding", "[Pointer][Text][Animated]")

bool FDreamInputKeySelectorBindsTheNextKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the selector came up"), Rig.IsUsable() && Selector != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The element's Type clicks first -- which is what arms a selector -- then presses the key.
	TestTrue(TEXT("Clicking the selector and pressing F completes"),
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Jump")))->Type(EKeys::F));

	TestTrue(TEXT("F is the new binding"), Selector->GetSelectedKey() == EKeys::F);
	TestEqual(TEXT("And OnKeySelected said so once"), Listener->KeySelectedCount, 1);
	TestTrue(TEXT("Carrying the key"), Listener->LastSelectedKey == EKeys::F);
	TestFalse(TEXT("Binding a key disarmed the selector"), Selector->GetIsListening());
	// Armed on the click, disarmed by the key: two edges, each reported.
	TestEqual(TEXT("The armed state was reported going on and coming off"), Listener->ListeningChangedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorEscapeCancelsTest,
	"DreamGUI.InputKeySelector.EscapeWhileArmedCancelsAndKeepsTheKeyItHad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamInputKeySelectorEscapeCancelsTest, "DreamGUI.InputKeySelector.EscapeWhileArmedCancelsAndKeepsTheKeyItHad", "[Pointer][Text][Animated]")

bool FDreamInputKeySelectorEscapeCancelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the selector came up"), Rig.IsUsable() && Selector != nullptr))
	{
		return false;
	}
	Selector->SetSelectedKey(EKeys::G);
	Rig.PumpFrames(1);
	Listener->KeySelectedCount = 0;
	Listener->ListeningChangedCount = 0;

	FDreamElementRef JumpBinding = Rig.Driver()->Find(FDreamBy::Name(TEXT("Jump")));
	JumpBinding->Click();
	TestTrue(TEXT("A click armed the selector"), Selector->GetIsListening());

	// Already armed, so the element does not click again (a second click would disarm it); Escape
	// goes straight to the selector, which hears keys before anything else while it is armed.
	JumpBinding->Type(EKeys::Escape);

	TestFalse(TEXT("Escape disarmed it"), Selector->GetIsListening());
	TestTrue(TEXT("Without binding Escape"), Selector->GetSelectedKey() == EKeys::G);
	TestEqual(TEXT("Nothing was reported as selected"), Listener->KeySelectedCount, 0);
	TestEqual(TEXT("The armed state went on and off once each"), Listener->ListeningChangedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorChordTest,
	"DreamGUI.InputKeySelector.AKeyPressedWithShiftHeldIsBoundAsAChord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamInputKeySelectorChordTest, "DreamGUI.InputKeySelector.AKeyPressedWithShiftHeldIsBoundAsAChord", "[Pointer][Text][Animated]")

bool FDreamInputKeySelectorChordTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the selector came up"), Rig.IsUsable() && Selector != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// bAllowModifierKeys is on by default, as in UMG, so a key pressed with Shift held binds the
	// chord rather than the bare key -- "Shift+F" is one binding, not F.
	Rig.Driver()->Find(FDreamBy::Name(TEXT("Jump")))->TypeChord(EKeys::LeftShift, EKeys::F);

	const FInputChord Bound = Selector->GetSelectedChord();
	TestTrue(TEXT("The chord's key is F"), Bound.Key == EKeys::F);
	TestTrue(TEXT("And Shift is part of it"), Bound.bShift != 0);
	TestTrue(TEXT("And nothing else is"), Bound.bCtrl == 0 && Bound.bAlt == 0 && Bound.bCmd == 0);
	TestEqual(TEXT("One binding was reported"), Listener->KeySelectedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorPadKeyPassesTest,
	"DreamGUI.InputKeySelector.AnArmedKeyboardOnlySelectorLetsAPadKeyReachTheBindingsBelowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamInputKeySelectorPadKeyPassesTest, "DreamGUI.InputKeySelector.AnArmedKeyboardOnlySelectorLetsAPadKeyReachTheBindingsBelowIt", "[Pointer][Text][Animated]")

/*
 * With pad keys turned off, an armed selector's capture agent still bound every pad key -- and a binding
 * consumes its key whether or not its handler takes it -- so the D-pad, A and the shoulders reached nothing
 * else on the player's input stack for as long as the selector waited for a keyboard key. The property
 * promises the opposite: a pad key the selector will not bind goes on to whatever else wanted it. Here a
 * binding of the player's own sits below the agent and listens for the D-pad; the selector is armed by a
 * click, and the D-pad goes in through the player's controller the way a pad's press does, then through
 * the controller's input frame.
 */
bool FDreamInputKeySelectorPadKeyPassesTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamInputKeySelector* Selector = MakeObservedSelector(Rig, Listener.Get());
	APlayerController* Player = Rig.GetPlayerController();
	if (!TestTrue(TEXT("The rig, the selector and the player came up"),
		Rig.IsUsable() && Selector != nullptr && Player != nullptr && Player->PlayerInput != nullptr))
	{
		return false;
	}
	Selector->SetAllowGamepadKeys(false);
	Rig.PumpFrames(1);

	// What a game's own pad controls are: a binding on the player's stack, under anything a control
	// pushes on top of it.
	UInputComponent* Below = NewObject<UInputComponent>(Player);
	Below->Priority = 0;
	Below->BindKey(EKeys::Gamepad_DPad_Down, IE_Pressed, Listener.Get(), &UDreamTextInteractionListener::HandlePassedKey);
	Player->PushInputComponent(Below);

	TestTrue(TEXT("Clicking the selector completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Jump")))->Click());
	if (!TestTrue(TEXT("The click armed it"), Selector->GetIsListening()))
	{
		Player->PopInputComponent(Below);
		return false;
	}

	FString WhyNot;
	TestTrue(TEXT("The D-pad goes into the player's controller"),
		DreamDriverGameHost::TypeKey(Rig.Context(), EKeys::Gamepad_DPad_Down, FKey(), WhyNot));
	// The controller's input frames: the press goes down the stack in the first, its release in the second.
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);

	TestEqual(TEXT("The pad key reached the binding below the armed selector"), Listener->PassedKeyCount, 1);
	TestTrue(TEXT("The selector is still waiting for a keyboard key"), Selector->GetIsListening());
	TestEqual(TEXT("And bound nothing"), Listener->KeySelectedCount, 0);

	Player->PopInputComponent(Below);
	return true;
}

#endif
