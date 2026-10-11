// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"

/*
 * UUIToggleGroup with a member disabled, with the panel holding it disabled, and on a canvas scaled to half and to double.
 *
 * The UMG counterpart is CommonUI's button group over toggleable buttons (CommonUI/Public/Groups/CommonButtonGroupBase.h).
 * A group has no input of its own; what a player does to it they do to its buttons, and a disabled button is out of it:
 * off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241), passed over by navigation
 * (FindFocusableWidget, HittestGrid.cpp:471, which asks every parent too, :336), skipped by the group itself when it moves
 * its selection along (UCommonButtonGroupBase::SelectNextButton, CommonUI/Private/Groups/CommonButtonGroupBase.cpp:87), and
 * never selected by the group's SelectButtonAtIndex (:156). So the group's choice only ever moves to a button the player can
 * use. On a scaled layer each button is hit and focused where it is painted.
 */
namespace DreamToggleGroupConfigTestLocal
{
	const FVector2D ToggleSize(48.0, 48.0);

	struct FToggleRow
	{
		UDreamWidget* Panel = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamToggle*> Toggles;

		bool IsReady() const { return Panel != nullptr && Group != nullptr && Toggles.Num() == 3 && !Toggles.Contains(nullptr); }

		/** Which toggle is the group's choice, by index; INDEX_NONE for none. */
		int32 ChoiceIndex() const
		{
			const UUIToggle* Selected = Group->GetSelectedItem();
			for (int32 Index = 0; Index < Toggles.Num(); ++Index)
			{
				if (Selected != nullptr && Toggles[Index]->ToggleBehaviour.Get() == Selected)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		/** Which toggle player 0's focus is in, by index; INDEX_NONE for none. */
		int32 FocusedIndex(const FDreamDriverRig& InRig) const
		{
			for (int32 Index = 0; Index < Toggles.Num(); ++Index)
			{
				if (DreamControlsConfigTest::FocusIsIn(InRig, Toggles[Index]))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}
	};

	/** Three toggles in a row under one group that allows none, at InPosition (canvas units, Y up), the first chosen. */
	FToggleRow MakeToggleRow(FDreamDriverRig& InRig, const FVector2D& InPosition)
	{
		FToggleRow Made;
		Made.Panel = InRig.MakeWidget(TEXT("Filters"), nullptr, FVector2D(480.0, 100.0), InPosition);
		Made.Group = Made.Panel != nullptr ? Made.Panel->AddComponent<UUIToggleGroup>() : nullptr;
		if (Made.Group == nullptr)
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(true);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Filters_Toggle%d"), Index), Made.Panel,
				ToggleSize, FVector2D(-160.0 + 160.0 * Index, 0.0));
			const bool bReady = Toggle != nullptr && Toggle->ToggleBehaviour != nullptr && Toggle->BoxNode != nullptr;
			if (bReady)
			{
				Toggle->ToggleBehaviour->SetToggleGroup(Made.Group);
			}
			Made.Toggles.Add(bReady ? Toggle : nullptr);
		}
		if (Made.IsReady())
		{
			Made.Toggles[0]->SetIsChecked(true);
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
	FDreamToggleGroupDisabledPressTest,
	"DreamGUI.ToggleGroup.AClickOrATapOnADisabledMemberOrInADisabledGroupLeavesTheGroupsChoiceWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupDisabledPressTest, "DreamGUI.ToggleGroup.AClickOrATapOnADisabledMemberOrInADisabledGroupLeavesTheGroupsChoiceWhereItWas", "[Pointer][Touch][Disabled]")

/*
 * The first toggle chosen and the third disabled: a click on the third and a tap on it each leave the first the group's
 * choice, and the third unchecked. Then the panel holding the whole group disabled, the third enabled again: a click and a
 * tap on the second leave the choice alone too. With the panel enabled again, a click on the third makes it the choice.
 */
bool FDreamToggleGroupDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Three toggles in a group can be made"), Row.IsReady())
		|| !TestEqual(TEXT("The first toggle is the group's choice to begin with"), Row.ChoiceIndex(), 0))
	{
		return false;
	}
	FDreamElementRef Second = Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[1]));
	FDreamElementRef Third = Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[2]));

	Row.Toggles[2]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the disabled member completes"), Third->Click());
	TestEqual(TEXT("A click on a disabled member leaves the choice where it was"), Row.ChoiceIndex(), 0);
	TestTrue(TEXT("Tapping it completes"), Third->Tap());
	TestEqual(TEXT("...and so does a tap"), Row.ChoiceIndex(), 0);
	TestFalse(TEXT("The disabled member is still unchecked"), Row.Toggles[2]->IsChecked());

	Row.Toggles[2]->SetIsEnabled(true);
	Row.Panel->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking a member of the disabled group completes"), Second->Click());
	TestTrue(TEXT("Tapping it completes"), Second->Tap());
	TestEqual(TEXT("In a disabled group neither changes the choice"), Row.ChoiceIndex(), 0);
	TestFalse(TEXT("...nor checks the member"), Row.Toggles[1]->IsChecked());

	Row.Panel->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the third member with everything enabled completes"), Third->Click());
	TestEqual(TEXT("Enabled, a click makes it the choice"), Row.ChoiceIndex(), 2);
	TestFalse(TEXT("...and the first lets go"), Row.Toggles[0]->IsChecked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupDisabledPadTest,
	"DreamGUI.ToggleGroup.TheDPadPassesOverADisabledMemberSoAcceptMovesTheChoiceToTheNextOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupDisabledPadTest, "DreamGUI.ToggleGroup.TheDPadPassesOverADisabledMemberSoAcceptMovesTheChoiceToTheNextOne", "[Nav][Disabled]")

/*
 * The first toggle chosen and focused, the second disabled. D-pad right passes over the second to the third, and the pad's
 * Accept makes the third the group's choice. Enabled again, D-pad left from the third stops on the second, and Accept makes
 * it the choice.
 */
bool FDreamToggleGroupDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Three toggles in a group can be made"), Row.IsReady()))
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
	TestEqual(TEXT("D-pad right passes over the disabled member"), Row.FocusedIndex(Rig), 2);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept made the focused member the group's choice"), Row.ChoiceIndex(), 2);
	TestFalse(TEXT("...the first let go"), Row.Toggles[0]->IsChecked());
	TestFalse(TEXT("...and the disabled one was never checked"), Row.Toggles[1]->IsChecked());

	Row.Toggles[1]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("D-pad left completes once the second member is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestEqual(TEXT("Enabled again, the second member is the next stop"), Row.FocusedIndex(Rig), 1);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("...and makes it the choice"), Row.ChoiceIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupScaledHitTest,
	"DreamGUI.ToggleGroup.OnACanvasScaledToHalfAndToDoubleAClickAndATapMoveTheChoiceOnlyFromInsideAMemberAsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupScaledHitTest, "DreamGUI.ToggleGroup.OnACanvasScaledToHalfAndToDoubleAClickAndATapMoveTheChoiceOnlyFromInsideAMemberAsDrawn", "[Pointer][Touch][Scaled]")

/*
 * The group high on the screen, the first toggle chosen, at each scale. Clicks four pixels past the third toggle's right
 * and bottom edges leave the choice alone; a click three pixels inside its top-left corner makes it the choice. Taps past the
 * second toggle's edges leave the choice alone; a tap three pixels inside its bottom-right corner makes it the choice.
 */
bool FDreamToggleGroupScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FToggleRow Row = MakeToggleRow(Rig, FVector2D(-60.0, 100.0));
		if (!TestTrue(At(Scale, TEXT("Three toggles in a group can be made")), Row.IsReady()))
		{
			return false;
		}
		const TOptional<FBox2D> Third = DrawnRect(Row.Toggles[2]->BoxNode.Get());
		const TOptional<FBox2D> Second = DrawnRect(Row.Toggles[1]->BoxNode.Get());
		if (!TestTrue(At(Scale, TEXT("The members' boxes are on the viewport")), Third.IsSet() && Second.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("A box is drawn at the canvas's scale (%s)"), *Describe(Third.GetValue()))),
			IsDrawnAtScale(Third.GetValue(), Row.Toggles[2]->BoxNode->GetSize(), Scale));
		const FBox2D ThirdOuter = Covering(Third.GetValue(), DrawnRect(Row.Toggles[2]));
		const FBox2D SecondOuter = Covering(Second.GetValue(), DrawnRect(Row.Toggles[1]));

		TestTrue(At(Scale, TEXT("Clicks just past the third member's right and bottom edges complete")),
			ClickAt(Rig, PastRightEdge(ThirdOuter, 4.0)) && ClickAt(Rig, PastBottomEdge(ThirdOuter, 4.0)));
		TestEqual(*At(Scale, TEXT("A click past the drawn edge leaves the choice alone")), Row.ChoiceIndex(), 0);
		TestTrue(At(Scale, TEXT("A click just inside the third member's top-left corner completes")), ClickAt(Rig, InsideTopLeft(Third.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("It makes the third the choice")), Row.ChoiceIndex(), 2);

		TestTrue(At(Scale, TEXT("Taps just past the second member's right and bottom edges complete")),
			TapAt(Rig, PastRightEdge(SecondOuter, 4.0)) && TapAt(Rig, PastBottomEdge(SecondOuter, 4.0)));
		TestEqual(*At(Scale, TEXT("A tap past the drawn edge leaves the choice alone")), Row.ChoiceIndex(), 2);
		TestTrue(At(Scale, TEXT("A tap just inside the second member's bottom-right corner completes")), TapAt(Rig, InsideBottomRight(Second.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("It makes the second the choice")), Row.ChoiceIndex(), 1);
		TestFalse(At(Scale, TEXT("...and the third lets go")), Row.Toggles[2]->IsChecked());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupScaledPadTest,
	"DreamGUI.ToggleGroup.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextMemberWithTheRingOnItAndAcceptMovesTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupScaledPadTest, "DreamGUI.ToggleGroup.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextMemberWithTheRingOnItAndAcceptMovesTheChoice", "[Nav][Scaled]")

/*
 * The first toggle chosen and focused; D-pad right takes the focus to the second, the ring sits on the second where it is
 * drawn, moving the focus left the choice alone, and the pad's Accept moves the group's choice to the second.
 */
bool FDreamToggleGroupScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupConfigTestLocal;
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
		const FToggleRow Row = MakeToggleRow(Rig, FVector2D(-60.0, 100.0));
		if (!TestTrue(At(Scale, TEXT("Three toggles in a group can be made")), Row.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first toggle")), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestEqual(*At(Scale, TEXT("The D-pad took the focus to the second member")), Row.FocusedIndex(Rig), 1);
		TestEqual(*At(Scale, TEXT("Moving the focus left the choice alone")), Row.ChoiceIndex(), 0);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second member where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept moved the group's choice to the focused member")), Row.ChoiceIndex(), 1);
		TestFalse(At(Scale, TEXT("...and the first let go")), Row.Toggles[0]->IsChecked());
	}
	return true;
}

#endif
