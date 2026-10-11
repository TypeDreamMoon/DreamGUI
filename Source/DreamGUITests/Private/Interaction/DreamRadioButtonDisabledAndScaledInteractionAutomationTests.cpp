// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRadioButton.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRadioButton in a group, disabled, and on a canvas scaled to half and to double.
 *
 * UMG's game UI makes a radio from CommonUI's toggleable buttons in a group that requires a selection
 * (UCommonButtonGroupBase, bSelectionRequired): choosing one lets the chosen one go. The input rules are a button's: a
 * disabled one is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and passed
 * over by navigation (FindFocusableWidget, HittestGrid.cpp:471), and CommonUI's group skips a button whose interaction is
 * disabled when it moves its selection along (UCommonButtonGroupBase::SelectNextButton, CommonUI/Private/Groups/
 * CommonButtonGroupBase.cpp:87). On a scaled layer a radio is hit and focused where it is painted.
 */
namespace DreamRadioConfigTestLocal
{
	const FVector2D RadioSize(48.0, 48.0);

	/** A panel with a group that forbids "none", and three radios in a row inside it, the first chosen. */
	struct FRadioGroup
	{
		UDreamWidget* Panel = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamRadioButton*> Radios;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;

		bool IsReady() const { return Panel != nullptr && Group != nullptr && Radios.Num() == 3 && !Radios.Contains(nullptr); }

		/** The radio player 0's focus is in, by index; INDEX_NONE for none. */
		int32 FocusedIndex(const FDreamDriverRig& InRig) const
		{
			for (int32 Index = 0; Index < Radios.Num(); ++Index)
			{
				if (DreamControlsConfigTest::FocusIsIn(InRig, Radios[Index]))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		/** Which radio is chosen, by index, as the radios say; INDEX_NONE for none, and -2 for more than one. */
		int32 CheckedIndex() const
		{
			int32 Found = INDEX_NONE;
			for (int32 Index = 0; Index < Radios.Num(); ++Index)
			{
				if (Radios[Index]->IsChecked())
				{
					Found = Found == INDEX_NONE ? Index : -2;
				}
			}
			return Found;
		}
	};

	/** The group at InPosition (canvas units, Y up), listened to after the first radio was chosen. */
	FRadioGroup MakeRadioGroup(FDreamDriverRig& InRig, const FVector2D& InPosition)
	{
		FRadioGroup Made;
		Made.Panel = InRig.MakeWidget(TEXT("Quality"), nullptr, FVector2D(480.0, 100.0), InPosition);
		Made.Group = Made.Panel != nullptr ? Made.Panel->AddComponent<UUIToggleGroup>() : nullptr;
		if (Made.Group == nullptr)
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(false);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamRadioButton* Radio = InRig.MakeControl<UDreamRadioButton>(FString::Printf(TEXT("Quality_Radio%d"), Index), Made.Panel,
				RadioSize, FVector2D(-160.0 + 160.0 * Index, 0.0));
			if (Radio != nullptr && Radio->BoxNode != nullptr)
			{
				Radio->SetToggleGroup(Made.Group);
			}
			Made.Radios.Add(Radio != nullptr && Radio->BoxNode != nullptr ? Radio : nullptr);
		}
		if (!Made.IsReady())
		{
			return Made;
		}
		Made.Radios[0]->SetIsChecked(true);
		for (UDreamRadioButton* Radio : Made.Radios)
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			Radio->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			Made.Listeners.Add(MoveTemp(Listener));
		}
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(1);
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioDisabledTapTest,
	"DreamGUI.RadioButton.ATapOnADisabledRadioDoesNotChooseItAndTheGroupKeepsItsChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioDisabledTapTest, "DreamGUI.RadioButton.ATapOnADisabledRadioDoesNotChooseItAndTheGroupKeepsItsChoice", "[Touch][Disabled]")

/*
 * The third radio disabled: a tap on it reaches nothing, so the first keeps the group's choice and nobody announces anything.
 * Enabled again, the same tap chooses it and the first lets go.
 */
bool FDreamRadioDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(Rig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Three radios in a group can be made"), Radios.IsReady()))
	{
		return false;
	}
	Radios.Radios[2]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Third = Rig.Driver()->Find(FDreamBy::Widget(Radios.Radios[2]));

	TestTrue(TEXT("Tapping the disabled radio completes"), Third->Tap());
	TestEqual(TEXT("The first radio keeps the choice"), Radios.CheckedIndex(), 0);
	TestTrue(TEXT("...and the group says so"), Radios.Group->GetSelectedItem() == Radios.Radios[0]->ToggleBehaviour.Get());
	int32 Announced = 0;
	for (const TStrongObjectPtr<UDreamPressInteractionListener>& Listener : Radios.Listeners)
	{
		Announced += Listener->CheckStates.Num();
	}
	TestEqual(TEXT("Nothing in the group announced anything"), Announced, 0);

	Radios.Radios[2]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping it enabled again completes"), Third->Tap());
	TestEqual(TEXT("Enabled again, a tap chooses it"), Radios.CheckedIndex(), 2);
	TestEqual(TEXT("...and it says so once"), Radios.Listeners[2]->CheckStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioDisabledPadTest,
	"DreamGUI.RadioButton.TheDPadPassesOverADisabledRadioSoAcceptChoosesTheNextOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioDisabledPadTest, "DreamGUI.RadioButton.TheDPadPassesOverADisabledRadioSoAcceptChoosesTheNextOne", "[Nav][Disabled]")

/*
 * The second radio disabled and the focus on the first: D-pad right passes over the second to the third, and the pad's Accept
 * chooses the third. D-pad left goes back past the second. Enabled again, the second is the next stop and Accept chooses it.
 */
bool FDreamRadioDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(Rig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Three radios in a group can be made"), Radios.IsReady()))
	{
		return false;
	}
	Radios.Radios[1]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the first radio"), PutFocusOn(Rig, Radios.Radios[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("D-pad right passes over the disabled radio"), Radios.FocusedIndex(Rig), 2);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept chose the radio the focus is on"), Radios.CheckedIndex(), 2);
	TestEqual(TEXT("The disabled radio announced nothing"), Radios.Listeners[1]->CheckStates.Num(), 0);
	TestTrue(TEXT("D-pad left completes"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestEqual(TEXT("D-pad left passes back over it"), Radios.FocusedIndex(Rig), 0);

	Radios.Radios[1]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("D-pad right completes once the second radio is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("Enabled again, the second radio is the next stop"), Radios.FocusedIndex(Rig), 1);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("...and chooses it"), Radios.CheckedIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioScaledHitTest,
	"DreamGUI.RadioButton.OnACanvasScaledToHalfAndToDoubleAClickAndATapChooseTheRadioExactlyWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioScaledHitTest, "DreamGUI.RadioButton.OnACanvasScaledToHalfAndToDoubleAClickAndATapChooseTheRadioExactlyWhereItIsDrawn", "[Pointer][Touch][Scaled]")

/*
 * The group high on the screen, at each scale. Clicks four pixels past the third radio's right and bottom edges choose
 * nothing; a click three pixels inside its left edge, level with its centre, chooses it. Taps past the second radio's edges
 * choose nothing; a tap three pixels inside its bottom edge, level with its centre, chooses it.
 *
 * Inside an edge's middle rather than inside a corner: the radio's box is round (its style's corner radius is half its
 * size), and a DreamGUI rect block takes no hit in the corners its radius cuts off (UDreamRectBlock::
 * bRaycastSupportCornerRadius, on by default) -- three pixels in from the corner of the box is off the circle once the
 * corner radius is drawn at double scale. Slate hit-tests a widget's whole rectangle whatever its brush draws; which of the
 * two a radio should follow is not this test's question.
 */
bool FDreamRadioScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FRadioGroup Radios = MakeRadioGroup(Rig, FVector2D(-60.0, 100.0));
		if (!TestTrue(At(Scale, TEXT("Three radios in a group can be made")), Radios.IsReady()))
		{
			return false;
		}
		const TOptional<FBox2D> Third = DrawnRect(Radios.Radios[2]->BoxNode.Get());
		const TOptional<FBox2D> Second = DrawnRect(Radios.Radios[1]->BoxNode.Get());
		if (!TestTrue(At(Scale, TEXT("The radios' boxes are on the viewport")), Third.IsSet() && Second.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("A box is drawn at the canvas's scale (%s)"), *Describe(Third.GetValue()))),
			IsDrawnAtScale(Third.GetValue(), Radios.Radios[2]->BoxNode->GetSize(), Scale));
		const FBox2D ThirdOuter = Covering(Third.GetValue(), DrawnRect(Radios.Radios[2]));
		const FBox2D SecondOuter = Covering(Second.GetValue(), DrawnRect(Radios.Radios[1]));

		TestTrue(At(Scale, TEXT("Clicks just past the third radio's right and bottom edges complete")),
			ClickAt(Rig, PastRightEdge(ThirdOuter, 4.0)) && ClickAt(Rig, PastBottomEdge(ThirdOuter, 4.0)));
		TestEqual(*At(Scale, TEXT("A click past the drawn edge chooses nothing")), Radios.CheckedIndex(), 0);
		TestTrue(At(Scale, TEXT("A click just inside the third radio's left edge completes")),
			ClickAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It chooses the third radio")), Radios.CheckedIndex(), 2);

		TestTrue(At(Scale, TEXT("Taps just past the second radio's right and bottom edges complete")),
			TapAt(Rig, PastRightEdge(SecondOuter, 4.0)) && TapAt(Rig, PastBottomEdge(SecondOuter, 4.0)));
		TestEqual(*At(Scale, TEXT("A tap past the drawn edge chooses nothing")), Radios.CheckedIndex(), 2);
		TestTrue(At(Scale, TEXT("A tap just inside the second radio's bottom edge completes")),
			TapAt(Rig, FVector2D(Second->GetCenter().X, Second->Max.Y - 3.0)));
		TestEqual(*At(Scale, TEXT("It chooses the second radio")), Radios.CheckedIndex(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioScaledPadTest,
	"DreamGUI.RadioButton.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextRadioWithTheRingOnItAndAcceptChoosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioScaledPadTest, "DreamGUI.RadioButton.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextRadioWithTheRingOnItAndAcceptChoosesIt", "[Nav][Scaled]")

/*
 * The focus on the chosen first radio; D-pad right takes it to the second, the ring sits on the second where it is drawn,
 * moving the focus chose nothing, and the pad's Accept chooses the second.
 */
bool FDreamRadioScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioConfigTestLocal;
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
		const FRadioGroup Radios = MakeRadioGroup(Rig, FVector2D(-60.0, 100.0));
		if (!TestTrue(At(Scale, TEXT("Three radios in a group can be made")), Radios.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first radio")), PutFocusOn(Rig, Radios.Radios[0]->BoxNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestEqual(*At(Scale, TEXT("The D-pad took the focus to the second radio")), Radios.FocusedIndex(Rig), 1);
		TestEqual(*At(Scale, TEXT("Moving the focus chose nothing")), Radios.CheckedIndex(), 0);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second radio where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept chose the focused radio")), Radios.CheckedIndex(), 1);
	}
	return true;
}

#endif
