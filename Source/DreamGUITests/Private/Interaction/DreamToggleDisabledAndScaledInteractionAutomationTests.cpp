// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamToggle disabled, and on a canvas scaled to half and to double, held to UMG's UCheckBox.
 *
 * SCheckBox flips on a click (ToggleCheckedState, Slate/Private/Widgets/Input/SCheckBox.cpp:373), takes a touch through
 * Slate's touch fallback, and takes the pad's Accept when focused (SCheckBox::OnKeyDown / OnKeyUp, :102-160). Disabled, it
 * is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and passed over by
 * navigation (FindFocusableWidget, HittestGrid.cpp:471). On a scaled layer it is hit and focused where it is painted.
 */
namespace DreamToggleConfigTestLocal
{
	const FVector2D ToggleSize(48.0, 48.0);

	/** Three unchecked toggles in a row at InRowY (canvas units, Y up), each heard by its own listener. */
	struct FToggleRow
	{
		TArray<UDreamToggle*> Toggles;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;

		bool IsReady() const { return Toggles.Num() == 3 && !Toggles.Contains(nullptr); }
	};

	FToggleRow MakeToggleRow(FDreamDriverRig& InRig, double InRowY)
	{
		FToggleRow Row;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Option%d"), Index), nullptr, ToggleSize,
				FVector2D(-160.0 + 160.0 * Index, InRowY));
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			if (Toggle != nullptr && Toggle->BoxNode != nullptr)
			{
				Toggle->SetCheckedState(EDreamCheckState::Unchecked);
				Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			}
			Row.Toggles.Add(Toggle != nullptr && Toggle->BoxNode != nullptr ? Toggle : nullptr);
			Row.Listeners.Add(MoveTemp(Listener));
		}
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(1);
		return Row;
	}

	/** Which of the row's toggles player 0's focus is on, by name; "(none)" when it is on none of them. */
	FString FocusedName(const FDreamDriverRig& InRig, const FToggleRow& InRow)
	{
		for (const UDreamToggle* Toggle : InRow.Toggles)
		{
			if (DreamControlsConfigTest::FocusIsIn(InRig, Toggle))
			{
				return Toggle->GetDisplayName();
			}
		}
		return FString(TEXT("(none)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleDisabledTapTest,
	"DreamGUI.Toggle.ATapOnADisabledToggleChangesNothingAndSaysNothingUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleDisabledTapTest, "DreamGUI.Toggle.ATapOnADisabledToggleChangesNothingAndSaysNothingUntilItIsEnabledAgain", "[Touch][Disabled]")

/*
 * A disabled check box is not on Slate's hit path, so the finger that taps it reaches nothing: the box stays unchecked and
 * announces nothing. Enabled again, the same tap checks it, once.
 */
bool FDreamToggleDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, 0.0);
	if (!TestTrue(TEXT("Three toggles with boxes can be made"), Row.IsReady()))
	{
		return false;
	}
	UDreamToggle* Toggle = Row.Toggles[1];
	Toggle->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Box = Rig.Driver()->Find(FDreamBy::Widget(Toggle));

	TestTrue(TEXT("Tapping the disabled toggle completes"), Box->Tap());
	TestFalse(TEXT("A disabled toggle stays unchecked under a tap"), Toggle->IsChecked());
	TestEqual(TEXT("...and announces nothing"), Row.Listeners[1]->CheckStates.Num(), 0);
	TestFalse(TEXT("...and is not left pressed"), Toggle->IsPressed());

	Toggle->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping it enabled again completes"), Box->Tap());
	TestTrue(TEXT("Enabled again, a tap checks it"), Toggle->IsChecked());
	TestEqual(TEXT("...and says so once"), Row.Listeners[1]->CheckStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleDisabledPadTest,
	"DreamGUI.Toggle.TheDPadPassesOverADisabledToggleAndOnceEnabledStopsOnItSoAcceptFlipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleDisabledPadTest, "DreamGUI.Toggle.TheDPadPassesOverADisabledToggleAndOnceEnabledStopsOnItSoAcceptFlipsIt", "[Nav][Disabled]")

/*
 * Three toggles in a row, the middle one disabled, the focus on the first. FindFocusableWidget passes over a widget that is
 * not enabled, so D-pad right goes straight to the third and left straight back, and the pad's Accept then flips the third
 * -- never the disabled one. Enabled again, the middle toggle is the next stop and Accept flips it.
 */
bool FDreamToggleDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, 0.0);
	if (!TestTrue(TEXT("Three toggles with boxes can be made"), Row.IsReady()))
	{
		return false;
	}
	Row.Toggles[1]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the first toggle"), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("D-pad right passes over the disabled toggle"), FocusedName(Rig, Row), FString(TEXT("Option2")));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("Accept flipped the toggle the focus is on"), Row.Toggles[2]->IsChecked());
	TestFalse(TEXT("...and not the disabled one"), Row.Toggles[1]->IsChecked());
	TestEqual(TEXT("The disabled toggle announced nothing"), Row.Listeners[1]->CheckStates.Num(), 0);
	TestTrue(TEXT("D-pad left completes"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestEqual(TEXT("D-pad left passes back over it"), FocusedName(Rig, Row), FString(TEXT("Option0")));

	Row.Toggles[1]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("D-pad right completes once the middle toggle is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("Enabled again, the middle toggle is the next stop"), FocusedName(Rig, Row), FString(TEXT("Option1")));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("...and flips it"), Row.Toggles[1]->IsChecked());
	TestEqual(TEXT("...saying so once"), Row.Listeners[1]->CheckStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleScaledHitTest,
	"DreamGUI.Toggle.OnACanvasScaledToHalfAndToDoubleAClickAndATapFlipTheToggleExactlyWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleScaledHitTest, "DreamGUI.Toggle.OnACanvasScaledToHalfAndToDoubleAClickAndATapFlipTheToggleExactlyWhereItIsDrawn", "[Pointer][Touch][Scaled]")

/*
 * The first toggle of a row high on the screen, at each scale: a click three pixels inside its top-left corner checks it, a
 * click three inside its bottom-right unchecks it, and clicks four pixels past its right and bottom edges change nothing.
 * A finger does the same at the same points.
 */
bool FDreamToggleScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FToggleRow Row = MakeToggleRow(Rig, 100.0);
		if (!TestTrue(At(Scale, TEXT("Three toggles with boxes can be made")), Row.IsReady()))
		{
			return false;
		}
		UDreamToggle* Toggle = Row.Toggles[0];
		const TArray<EDreamCheckState>& Heard = Row.Listeners[0]->CheckStates;
		const TOptional<FBox2D> Drawn = DrawnRect(Toggle->BoxNode.Get());
		if (!TestTrue(At(Scale, TEXT("The toggle's box is on the viewport")), Drawn.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The box is drawn at the canvas's scale (%s)"), *Describe(Drawn.GetValue()))),
			IsDrawnAtScale(Drawn.GetValue(), Toggle->BoxNode->GetSize(), Scale));
		// Past the whole control as well as the box, so a press there is past everything the toggle draws.
		const FBox2D Outer = Covering(Drawn.GetValue(), DrawnRect(Toggle));

		TestTrue(At(Scale, TEXT("A click just inside the top-left corner completes")), ClickAt(Rig, InsideTopLeft(Drawn.GetValue(), 3.0)));
		TestTrue(At(Scale, TEXT("It checks the toggle")), Toggle->IsChecked());
		TestTrue(At(Scale, TEXT("A click just inside the bottom-right corner completes")), ClickAt(Rig, InsideBottomRight(Drawn.GetValue(), 3.0)));
		TestFalse(At(Scale, TEXT("It unchecks it")), Toggle->IsChecked());
		TestTrue(At(Scale, TEXT("Clicks just past the right and bottom edges complete")),
			ClickAt(Rig, PastRightEdge(Outer, 4.0)) && ClickAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("Clicks past the drawn edge flip nothing")), Heard.Num(), 2);

		TestTrue(At(Scale, TEXT("A tap just inside the top-left corner completes")), TapAt(Rig, InsideTopLeft(Drawn.GetValue(), 3.0)));
		TestTrue(At(Scale, TEXT("It checks the toggle")), Toggle->IsChecked());
		TestTrue(At(Scale, TEXT("A tap just inside the bottom-right corner completes")), TapAt(Rig, InsideBottomRight(Drawn.GetValue(), 3.0)));
		TestFalse(At(Scale, TEXT("It unchecks it")), Toggle->IsChecked());
		TestTrue(At(Scale, TEXT("Taps just past the right and bottom edges complete")),
			TapAt(Rig, PastRightEdge(Outer, 4.0)) && TapAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("Taps past the drawn edge flip nothing")), Heard.Num(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleScaledPadTest,
	"DreamGUI.Toggle.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextToggleWithTheRingOnItAndAcceptFlipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleScaledPadTest, "DreamGUI.Toggle.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextToggleWithTheRingOnItAndAcceptFlipsIt", "[Nav][Scaled]")

/*
 * The focus on the first toggle of a row; D-pad right takes it to the second, the ring sits on the second where it is drawn,
 * and the pad's Accept checks the second -- only it.
 */
bool FDreamToggleScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FToggleRow Row = MakeToggleRow(Rig, 100.0);
		if (!TestTrue(At(Scale, TEXT("Three toggles with boxes can be made")), Row.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first toggle")), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestEqual(*At(Scale, TEXT("The D-pad took the focus to the second toggle")), FocusedName(Rig, Row), FString(TEXT("Option1")));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second toggle where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestTrue(At(Scale, TEXT("Accept checked the focused toggle")), Row.Toggles[1]->IsChecked());
		TestFalse(At(Scale, TEXT("...and not the one the focus left")), Row.Toggles[0]->IsChecked());
		TestEqual(*At(Scale, TEXT("...said once")), Row.Listeners[1]->CheckStates.Num(), 1);
	}
	return true;
}

#endif
