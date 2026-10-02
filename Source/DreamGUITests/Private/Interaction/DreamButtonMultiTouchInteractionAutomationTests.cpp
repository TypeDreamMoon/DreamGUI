// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamToggle.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamButton and UDreamToggle under more than one finger.
 *
 * A button is one press from the first pointer that takes it to the last that lets it go: the press pair is said once,
 * the pressed look lasts the whole of it, and the press clicks once, for the release that ends it over the button. A
 * toggle is pressed the same way and flips once.
 */
namespace DreamButtonMultiTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonTwoFingersTest,
	"DreamGUI.Button.TwoFingersOnOneButtonAreOnePressThatClicksOnceWhenTheLastLetsGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonTwoFingersTest, "DreamGUI.Button.TwoFingersOnOneButtonAreOnePressThatClicksOnceWhenTheLastLetsGo", "[Touch][Animated]")

/*
 * Two fingers on one button shared one pressed flag: the second finger's press said OnPressed again, the first to lift
 * said OnReleased and drew the button released under the finger still on it, the second lift said nothing, and each
 * finger's release clicked -- the button's action ran twice for one gesture. The button now keeps the presses it took by
 * pointer: OnPressed when the first finger goes down, the pressed look while any finger holds it, OnReleased when the
 * last lets go, and one click, for that last release. A second finger's tap on a button already held is part of that
 * press and clicks nothing of its own, as SButton takes one press at a time and clicks it once. Checked with the
 * driver's fingers: two down and lifted in the order they landed, then a second finger's whole tap under a first.
 */
bool FDreamButtonTwoFingersTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMultiTouchTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Button->OnReleased.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleReleased);
	// Every press here is a press, not the second half of a double click.
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Widget(Button))->GetCentrePixel();
	if (!TestTrue(TEXT("The button is somewhere a finger can reach"), Centre.IsSet()))
	{
		return false;
	}
	const FVector2D FirstAt = Centre.GetValue() - FVector2D(20.0, 0.0);
	const FVector2D SecondAt = Centre.GetValue() + FVector2D(20.0, 0.0);
	FDreamDriverRef Driver = Rig.Driver();

	// Down, down, up, up -- the first finger lifting first.
	TestTrue(TEXT("The first finger lands on the button"), Driver->Sequence().TouchDown(0, FirstAt).Perform());
	TestEqual(TEXT("The first finger presses the button"), Listener->PressedCount, 1);
	TestTrue(TEXT("...which looks pressed"), Button->IsPressed());
	TestTrue(TEXT("The second finger lands on it too"), Driver->Sequence().TouchDown(1, SecondAt).Perform());
	TestEqual(TEXT("A second finger on a button already held is not a second press"), Listener->PressedCount, 1);
	TestTrue(TEXT("The first finger lifts"), Driver->Sequence().TouchUp(0).Perform());
	TestEqual(TEXT("With the second finger still on it, the button is not released"), Listener->ReleasedCount, 0);
	TestTrue(TEXT("...still looks pressed"), Button->IsPressed());
	TestEqual(TEXT("...and is not clicked: its press is not over"), Listener->ClickedCount, 0);
	TestTrue(TEXT("The second finger lifts"), Driver->Sequence().TouchUp(1).Perform());
	TestEqual(TEXT("The last finger lifting releases the button, once"), Listener->ReleasedCount, 1);
	TestFalse(TEXT("...which no longer looks pressed"), Button->IsPressed());
	TestEqual(TEXT("...and clicks it, once"), Listener->ClickedCount, 1);
	const TArray<FName> Expected = { FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")) };
	TestTrue(TEXT("Pressed, released, clicked, in that order, as for one finger"), Listener->LogOnly(Expected) == Expected);

	// A second finger's whole tap while the first holds the button.
	TestTrue(TEXT("A finger holds the button while another taps it"),
		Driver->Sequence().TouchDown(0, FirstAt).TouchDown(1, SecondAt).TouchUp(1).Perform());
	TestEqual(TEXT("The hold is a press, and the tap no press of its own"), Listener->PressedCount, 2);
	TestEqual(TEXT("...nor a click of its own"), Listener->ClickedCount, 1);
	TestTrue(TEXT("...the button still held"), Button->IsPressed() && Listener->ReleasedCount == 1);
	TestTrue(TEXT("The holding finger lifts"), Driver->Sequence().TouchUp(0).Perform());
	TestEqual(TEXT("...releasing the button"), Listener->ReleasedCount, 2);
	TestEqual(TEXT("...and clicking it, once"), Listener->ClickedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleTwoFingersTest,
	"DreamGUI.Toggle.TwoFingersOnOneToggleFlipItOnceWhenTheLastLetsGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleTwoFingersTest, "DreamGUI.Toggle.TwoFingersOnOneToggleFlipItOnceWhenTheLastLetsGo", "[Touch][Animated]")

/*
 * A toggle shared one pressed flag across fingers as the button did, and flipped on every finger's release: two fingers
 * on a check box checked it and unchecked it again, so the gesture left it as it was, and its press pair came out of
 * step. It now keeps the presses it took by pointer, as the button does: one press from the first finger down to the
 * last finger up, the pressed look all the while, and one flip, for that last release. Checked with the driver's
 * fingers, down, down, up, up, on an unchecked box.
 */
bool FDreamToggleTwoFingersTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonMultiTouchTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamToggle* Toggle = Rig.MakeControl<UDreamToggle>(TEXT("Mute"), nullptr, FVector2D(40.0, 40.0));
	if (!TestNotNull(TEXT("A toggle can be made on the rig"), Toggle))
	{
		return false;
	}
	Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
	Toggle->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Toggle->OnReleased.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleReleased);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Widget(Toggle))->GetCentrePixel();
	if (!TestTrue(TEXT("The toggle is somewhere a finger can reach"), Centre.IsSet())
		|| !TestFalse(TEXT("...and starts unchecked"), Toggle->IsChecked()))
	{
		return false;
	}
	const FVector2D FirstAt = Centre.GetValue() - FVector2D(6.0, 0.0);
	const FVector2D SecondAt = Centre.GetValue() + FVector2D(6.0, 0.0);
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Two fingers land on the toggle"), Driver->Sequence().TouchDown(0, FirstAt).TouchDown(1, SecondAt).Perform());
	TestEqual(TEXT("...pressing it once"), Listener->PressedCount, 1);
	TestTrue(TEXT("The first finger lifts"), Driver->Sequence().TouchUp(0).Perform());
	TestEqual(TEXT("With the second still on it, the toggle is not released"), Listener->ReleasedCount, 0);
	TestTrue(TEXT("...still looks pressed"), Toggle->IsPressed());
	TestEqual(TEXT("...and has not flipped"), Listener->CheckStates.Num(), 0);
	TestTrue(TEXT("The second finger lifts"), Driver->Sequence().TouchUp(1).Perform());
	TestEqual(TEXT("The last finger lifting releases the toggle, once"), Listener->ReleasedCount, 1);
	TestFalse(TEXT("...which no longer looks pressed"), Toggle->IsPressed());
	TestEqual(TEXT("...and flips it, once"), Listener->CheckStates.Num(), 1);
	TestTrue(TEXT("...to checked"), Toggle->IsChecked());
	return true;
}

#endif
