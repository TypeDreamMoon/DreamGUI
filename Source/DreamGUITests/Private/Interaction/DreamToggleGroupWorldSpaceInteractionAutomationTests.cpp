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
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"

/*
 * UUIToggleGroup on a panel in the level.
 *
 * The UMG counterpart is CommonUI's button group with bSelectionRequired off (CommonUI/Public/Groups/CommonButtonGroupBase.h:141)
 * over toggleable buttons: a group has no input of its own, and what a player does to it they do to its buttons. In a
 * UWidgetComponent those buttons are hit where the interaction's ray lands on them and walked to by the pad where they stand,
 * so the group's choice moves exactly as on a screen: to the member pressed or confirmed, and only from inside a member.
 */
namespace DreamToggleGroupWorldTestLocal
{
	const FVector2D ToggleSize(48.0, 48.0);

	struct FToggleRow
	{
		UDreamWidget* Holder = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamToggle*> Toggles;

		bool IsReady() const { return Holder != nullptr && Group != nullptr && Toggles.Num() == 3 && !Toggles.Contains(nullptr); }

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

	/** Three toggles in a row under one group that allows none, at InPosition on InPanel (panel units, Y up), the first chosen. */
	FToggleRow MakeToggleRow(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition)
	{
		FToggleRow Made;
		Made.Holder = InRig.MakeWidget(TEXT("Filters"), InPanel, FVector2D(420.0, 90.0), InPosition);
		Made.Group = Made.Holder != nullptr ? Made.Holder->AddComponent<UUIToggleGroup>() : nullptr;
		if (Made.Group == nullptr)
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(true);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Filters_Toggle%d"), Index), Made.Holder,
				ToggleSize, FVector2D(-140.0 + 140.0 * Index, 0.0));
			const bool bReady = Toggle != nullptr && Toggle->ToggleBehaviour != nullptr && Toggle->BoxNode != nullptr;
			if (bReady)
			{
				Toggle->ToggleBehaviour->SetToggleGroup(Made.Group);
			}
			Made.Toggles.Add(bReady ? Toggle : nullptr);
		}
		if (!Made.IsReady())
		{
			return Made;
		}
		Made.Toggles[0]->SetIsChecked(true);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupWorldHitTest,
	"DreamGUI.ToggleGroup.OnAWorldPanelAClickAndATapMoveTheChoiceOnlyFromInsideAMemberAsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupWorldHitTest, "DreamGUI.ToggleGroup.OnAWorldPanelAClickAndATapMoveTheChoiceOnlyFromInsideAMemberAsSeen", "[Pointer][Touch][World]")

/*
 * The group high on a panel three metres ahead, the first member chosen. Clicks four pixels past the third member's right and
 * bottom edges, as the camera sees them, leave the choice alone; a click three pixels inside its left edge, level with its
 * middle, makes it the choice and the first lets go. Taps past the second member's edges leave the choice alone; a tap three
 * pixels inside its bottom edge, level with its middle, makes the second the choice and the third lets go.
 *
 * Inside the middle of an edge rather than inside a corner: a member's box is rounded (its style's corner radius), and a
 * DreamGUI rect block takes no hit in the corners its radius cuts off (UDreamRectBlock::bRaycastSupportCornerRadius, on by
 * default) -- three pixels in from a corner seen at a little over two pixels to the unit is just off the arc. Slate hit-tests
 * a widget's whole rectangle whatever its brush draws; which of the two a member should follow is not this test's question.
 */
bool FDreamToggleGroupWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, Stage.Panel, FVector2D(-20.0, 80.0));
	if (!TestTrue(TEXT("Three toggles in a group can be made on the panel"), Row.IsReady())
		|| !TestEqual(TEXT("The first is the group's choice to begin with"), Row.ChoiceIndex(), 0))
	{
		return false;
	}
	const TOptional<FBox2D> Third = SeenRect(Rig, Row.Toggles[2]->BoxNode.Get());
	const TOptional<FBox2D> Second = SeenRect(Rig, Row.Toggles[1]->BoxNode.Get());
	if (!TestTrue(TEXT("The camera sees the members' boxes"), Third.IsSet() && Second.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A box is seen at its size in panel units (%s)"), *Describe(Third.GetValue())),
		IsSeenAtItsSize(Third.GetValue(), Row.Toggles[2]->BoxNode->GetSize()));
	const FBox2D ThirdOuter = Covering(Third.GetValue(), SeenRect(Rig, Row.Toggles[2]));
	const FBox2D SecondOuter = Covering(Second.GetValue(), SeenRect(Rig, Row.Toggles[1]));

	TestTrue(TEXT("Clicks just past the third member's right and bottom edges complete"),
		ClickAt(Rig, PastRightEdge(ThirdOuter, 4.0)) && ClickAt(Rig, PastBottomEdge(ThirdOuter, 4.0)));
	TestEqual(TEXT("A click past the seen edge leaves the choice alone"), Row.ChoiceIndex(), 0);
	TestTrue(TEXT("A click just inside the third member's left edge completes"), ClickAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
	TestEqual(TEXT("It makes the third the choice"), Row.ChoiceIndex(), 2);
	TestFalse(TEXT("...and the first lets go"), Row.Toggles[0]->IsChecked());

	TestTrue(TEXT("Taps just past the second member's right and bottom edges complete"),
		TapAt(Rig, PastRightEdge(SecondOuter, 4.0)) && TapAt(Rig, PastBottomEdge(SecondOuter, 4.0)));
	TestEqual(TEXT("A tap past the seen edge leaves the choice alone"), Row.ChoiceIndex(), 2);
	TestTrue(TEXT("A tap just inside the second member's bottom edge completes"), TapAt(Rig, FVector2D(Second->GetCenter().X, Second->Max.Y - 3.0)));
	TestEqual(TEXT("It makes the second the choice"), Row.ChoiceIndex(), 1);
	TestFalse(TEXT("...and the third lets go"), Row.Toggles[2]->IsChecked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupWorldPadTest,
	"DreamGUI.ToggleGroup.OnAWorldPanelTheDPadReachesTheNextMemberWithTheRingOnItAsSeenAndAcceptMovesTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupWorldPadTest, "DreamGUI.ToggleGroup.OnAWorldPanelTheDPadReachesTheNextMemberWithTheRingOnItAsSeenAndAcceptMovesTheChoice", "[Nav][World]")

/*
 * The first member chosen and focused; D-pad right takes the focus to the second, the ring sits on it where the camera sees
 * it, moving the focus left the choice alone, and the pad's Accept moves the group's choice to the second.
 */
bool FDreamToggleGroupWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig, Stage.Panel, FVector2D(-20.0, 80.0));
	if (!TestTrue(TEXT("Three toggles in a group can be made on the panel"), Row.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the first member"), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("The D-pad took the focus to the second member"), Row.FocusedIndex(Rig), 1);
	TestEqual(TEXT("Moving the focus left the choice alone"), Row.ChoiceIndex(), 0);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second member where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept moved the group's choice to the focused member"), Row.ChoiceIndex(), 1);
	TestFalse(TEXT("...and the first let go"), Row.Toggles[0]->IsChecked());
	return true;
}

#endif
