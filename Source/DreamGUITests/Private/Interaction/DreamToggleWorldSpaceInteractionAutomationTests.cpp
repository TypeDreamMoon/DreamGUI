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
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamToggle on a panel in the level, held to UMG's UCheckBox in a UWidgetComponent.
 *
 * SCheckBox flips on a click (ToggleCheckedState, Slate/Private/Widgets/Input/SCheckBox.cpp:373), takes a touch through
 * Slate's touch fallback, and takes the pad's Accept when focused (SCheckBox::OnKeyDown / OnKeyUp, :102-160). In a widget
 * component all of it happens in the component's window: the box is hit where the interaction's ray lands on it, and focused
 * and marked where it stands in the level.
 */
namespace DreamToggleWorldTestLocal
{
	const FVector2D ToggleSize(48.0, 48.0);

	/** Three unchecked toggles in a row on InPanel at InRowY (panel units, Y up), each heard by its own listener. */
	struct FToggleRow
	{
		TArray<UDreamToggle*> Toggles;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;

		bool IsReady() const { return Toggles.Num() == 3 && !Toggles.Contains(nullptr); }
	};

	FToggleRow MakeToggleRow(FDreamDriverRig& InRig, UDreamWidget* InPanel, double InRowY)
	{
		FToggleRow Row;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Option%d"), Index), InPanel, ToggleSize,
				FVector2D(-140.0 + 140.0 * Index, InRowY));
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
		InRig.PumpFrames(2);
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
	FDreamToggleWorldHitTest,
	"DreamGUI.Toggle.OnAWorldPanelAClickAndATapFlipTheToggleOnlyInsideWhereItIsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleWorldHitTest, "DreamGUI.Toggle.OnAWorldPanelAClickAndATapFlipTheToggleOnlyInsideWhereItIsSeen", "[Pointer][Touch][World]")

/*
 * The first toggle of a row high on a panel three metres ahead: a click three pixels inside its box's left edge as the camera
 * sees it, level with the box's middle, checks it, one three inside its right edge unchecks it, and clicks four pixels past
 * its right and bottom edges change nothing. A finger does the same three pixels inside the top edge and the bottom edge.
 *
 * Inside the middle of an edge rather than inside a corner: the box is rounded (its style's corner radius), and a DreamGUI
 * rect block takes no hit in the corners its radius cuts off (UDreamRectBlock::bRaycastSupportCornerRadius, on by default)
 * -- three pixels in from a corner seen at a little over two pixels to the unit is just off the arc. Slate hit-tests a
 * widget's whole rectangle whatever its brush draws; which of the two a toggle should follow is not this test's question.
 */
bool FDreamToggleWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleWorldTestLocal;
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
	const FToggleRow Row = MakeToggleRow(Rig, Stage.Panel, 80.0);
	if (!TestTrue(TEXT("Three toggles with boxes can be made on the panel"), Row.IsReady()))
	{
		return false;
	}
	UDreamToggle* Toggle = Row.Toggles[0];
	const TArray<EDreamCheckState>& Heard = Row.Listeners[0]->CheckStates;
	const TOptional<FBox2D> Seen = SeenRect(Rig, Toggle->BoxNode.Get());
	if (!TestTrue(TEXT("The camera sees the toggle's box"), Seen.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The box is seen at its size in panel units (%s)"), *Describe(Seen.GetValue())),
		IsSeenAtItsSize(Seen.GetValue(), Toggle->BoxNode->GetSize()));
	const FBox2D Outer = Covering(Seen.GetValue(), SeenRect(Rig, Toggle));

	const FBox2D Box = Seen.GetValue();
	TestTrue(TEXT("A click just inside the left edge completes"), ClickAt(Rig, FVector2D(Box.Min.X + 3.0, Box.GetCenter().Y)));
	TestTrue(TEXT("It checks the toggle"), Toggle->IsChecked());
	TestTrue(TEXT("A click just inside the right edge completes"), ClickAt(Rig, FVector2D(Box.Max.X - 3.0, Box.GetCenter().Y)));
	TestFalse(TEXT("It unchecks it"), Toggle->IsChecked());
	TestTrue(TEXT("Clicks just past the right and bottom edges complete"),
		ClickAt(Rig, PastRightEdge(Outer, 4.0)) && ClickAt(Rig, PastBottomEdge(Outer, 4.0)));
	TestEqual(TEXT("Clicks past the seen edge flip nothing"), Heard.Num(), 2);

	TestTrue(TEXT("A tap just inside the top edge completes"), TapAt(Rig, FVector2D(Box.GetCenter().X, Box.Min.Y + 3.0)));
	TestTrue(TEXT("It checks the toggle"), Toggle->IsChecked());
	TestTrue(TEXT("A tap just inside the bottom edge completes"), TapAt(Rig, FVector2D(Box.GetCenter().X, Box.Max.Y - 3.0)));
	TestFalse(TEXT("It unchecks it"), Toggle->IsChecked());
	TestTrue(TEXT("Taps just past the right and bottom edges complete"),
		TapAt(Rig, PastRightEdge(Outer, 4.0)) && TapAt(Rig, PastBottomEdge(Outer, 4.0)));
	TestEqual(TEXT("Taps past the seen edge flip nothing"), Heard.Num(), 4);
	TestEqual(TEXT("The toggle's neighbours said nothing"), Row.Listeners[1]->CheckStates.Num() + Row.Listeners[2]->CheckStates.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleWorldPadTest,
	"DreamGUI.Toggle.OnAWorldPanelTheDPadReachesTheNextToggleWithTheRingOnItAsSeenAndAcceptFlipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleWorldPadTest, "DreamGUI.Toggle.OnAWorldPanelTheDPadReachesTheNextToggleWithTheRingOnItAsSeenAndAcceptFlipsIt", "[Nav][World]")

/*
 * The focus on the first toggle of a row on the panel; D-pad right takes it to the second, the ring sits on the second where
 * the camera sees it, and the pad's Accept checks the second -- only it.
 */
bool FDreamToggleWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleWorldTestLocal;
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
	const FToggleRow Row = MakeToggleRow(Rig, Stage.Panel, 80.0);
	if (!TestTrue(TEXT("Three toggles with boxes can be made on the panel"), Row.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the first toggle"), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("The D-pad took the focus to the second toggle"), FocusedName(Rig, Row), FString(TEXT("Option1")));
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second toggle where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("Accept checked the focused toggle"), Row.Toggles[1]->IsChecked());
	TestFalse(TEXT("...and not the one the focus left"), Row.Toggles[0]->IsChecked());
	TestEqual(TEXT("...said once"), Row.Listeners[1]->CheckStates.Num(), 1);
	return true;
}

#endif
