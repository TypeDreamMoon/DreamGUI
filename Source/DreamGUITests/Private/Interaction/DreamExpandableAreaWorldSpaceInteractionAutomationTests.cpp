// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamExpandableArea.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamExpandableArea on a panel in the level.
 *
 * UExpandableArea is SExpandableArea: its header is a button whose click flips the area (OnHeaderClicked ->
 * SetExpanded_Animated, Slate/Private/Widgets/Layout/SExpandableArea.cpp:114-134, 165-176) and its body has no click of its
 * own. In a UWidgetComponent the header is hit where the interaction's ray lands on it -- the body beneath it is not the
 * header there either -- and the pad walks onto the header and presses it with Accept where it stands in the level.
 *
 * The area is pinned by its top edge so the header stays where it is while the body opens and closes beneath it, and opens
 * and closes at once (the control's default), so each gesture's result is there when the gesture is.
 */
namespace DreamExpandableAreaWorldTestLocal
{
	const FVector2D AreaSize(260.0, 180.0);

	struct FPlacedArea
	{
		UDreamExpandableArea* Area = nullptr;
		UDreamWidget* Body = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;

		bool IsReady() const { return Area != nullptr && Area->HeaderNode != nullptr && Body != nullptr; }
	};

	/** An area pinned by its top edge at InPosition on InPanel (panel units, Y up), expanded, with a body 90 tall, its flips counted. */
	FPlacedArea PlaceArea(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition)
	{
		FPlacedArea Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamExpandableArea* Area = InRig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), InPanel, AreaSize, InPosition);
		if (Area == nullptr || Area->HeaderNode == nullptr || Area->ContentNode == nullptr)
		{
			return Placed;
		}
		// Pinned by its top edge: Y is up, so a pivot of one is the top.
		Area->SetPivot(FVector2D(0.5, 1.0));
		Placed.Body = InRig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(220.0, 90.0));
		Area->SetExpansionDuration(0.0f);
		Area->SetIsExpanded(true);
		Area->OnExpansionChanged.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleExpansionChanged);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		Placed.Area = Area;
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaWorldHitTest,
	"DreamGUI.ExpandableArea.OnAWorldPanelAClickAndATapFlipTheAreaOnlyOnTheHeaderAsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaWorldHitTest, "DreamGUI.ExpandableArea.OnAWorldPanelAClickAndATapFlipTheAreaOnlyOnTheHeaderAsSeen", "[Pointer][Touch][World]")

/*
 * An open area off the middle of a panel three metres ahead. A click four pixels below the header's bottom edge, as the camera
 * sees it, is in the body and flips nothing; a click three pixels inside that edge, level with its middle, closes the area.
 * Then, closed, a tap four pixels past the header's right edge flips nothing and a tap three pixels inside that edge, level
 * with its middle, opens it.
 *
 * Inside the middle of an edge rather than inside a corner: the header's face is rounded (its style's corner radius), and a
 * DreamGUI rect block takes no hit in the corners its radius cuts off (UDreamRectBlock::bRaycastSupportCornerRadius, on by
 * default) -- three pixels in from a corner seen at a little over two pixels to the unit is just off the arc. Slate hit-tests
 * a widget's whole rectangle whatever its brush draws; which of the two a header should follow is not this test's question.
 */
bool FDreamExpandableAreaWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaWorldTestLocal;
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
	const FPlacedArea Placed = PlaceArea(Rig, Stage.Panel, FVector2D(-100.0, 140.0));
	if (!TestTrue(TEXT("An expandable area with a header and a body can be made on the panel"), Placed.IsReady()))
	{
		return false;
	}
	const TOptional<FBox2D> Header = SeenRect(Rig, Placed.Area->HeaderNode.Get());
	const TOptional<FBox2D> Body = SeenRect(Rig, Placed.Body);
	if (!TestTrue(TEXT("The camera sees the header and the body"), Header.IsSet() && Body.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The header is seen at its size in panel units (%s)"), *Describe(Header.GetValue())),
		IsSeenAtItsSize(Header.GetValue(), Placed.Area->HeaderNode->GetSize()));
	TestTrue(FString::Printf(TEXT("...with the body seen below it (%s)"), *Describe(Body.GetValue())), Body->Min.Y >= Header->Max.Y - 1.5);
	const TArray<bool>& Heard = Placed.Listener->ExpansionStates;

	TestTrue(TEXT("A click just below the header's seen bottom edge completes"), ClickAt(Rig, FVector2D(Header->GetCenter().X, Header->Max.Y + 4.0)));
	TestTrue(TEXT("A click in the body leaves the area open"), Placed.Area->GetIsExpanded());
	TestTrue(TEXT("A click just inside the header's bottom edge completes"), ClickAt(Rig, FVector2D(Header->GetCenter().X, Header->Max.Y - 3.0)));
	TestFalse(TEXT("It closes the area"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...said once"), Heard.Num(), 1);
	Rig.PumpFrames(1);

	const TOptional<FBox2D> ClosedHeader = SeenRect(Rig, Placed.Area->HeaderNode.Get());
	if (!TestTrue(TEXT("The camera sees the closed area's header"), ClosedHeader.IsSet()))
	{
		return false;
	}
	const FBox2D Outer = Covering(ClosedHeader.GetValue(), SeenRect(Rig, Placed.Area));
	TestTrue(TEXT("A tap just past the header's right edge completes"), TapAt(Rig, PastRightEdge(Outer, 4.0)));
	TestFalse(TEXT("A tap past the seen edge leaves it closed"), Placed.Area->GetIsExpanded());
	TestTrue(TEXT("A tap just inside the header's right edge completes"), TapAt(Rig, FVector2D(ClosedHeader->Max.X - 3.0, ClosedHeader->GetCenter().Y)));
	TestTrue(TEXT("It opens the area"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...said once more"), Heard.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaWorldPadTest,
	"DreamGUI.ExpandableArea.OnAWorldPanelTheDPadReachesTheHeaderWithTheRingOnItAsSeenAndAcceptFlipsTheArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaWorldPadTest, "DreamGUI.ExpandableArea.OnAWorldPanelTheDPadReachesTheHeaderWithTheRingOnItAsSeenAndAcceptFlipsTheArea", "[Nav][World]")

/*
 * A button on the panel level with the area's header, to its left, focused. D-pad right takes the focus onto the header, the
 * ring sits on the header where the camera sees it, and the pad's Accept closes the area; Accept again opens it.
 */
bool FDreamExpandableAreaWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaWorldTestLocal;
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
	const FPlacedArea Placed = PlaceArea(Rig, Stage.Panel, FVector2D(80.0, 140.0));
	if (!TestTrue(TEXT("An expandable area with a header and a body can be made on the panel"), Placed.IsReady()))
	{
		return false;
	}
	// Level with the header, in the panel's units with Y up.
	const FVector2D HeaderOnPanel = OnPanel(Stage.Panel, Placed.Area->HeaderNode.Get());
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), Stage.Panel, FVector2D(90.0, 40.0), FVector2D(-180.0, HeaderOnPanel.Y));
	if (!TestTrue(TEXT("A button left of the header can be made on the panel"), Left != nullptr && Left->FaceNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Left->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad took the focus onto the header"), FocusIsIn(Rig, Placed.Area->HeaderNode.Get()));
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the header where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestFalse(TEXT("Accept closed the area"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...said once"), Placed.Listener->ExpansionStates.Num(), 1);
	TestTrue(TEXT("The pad's Accept completes again"), PadConfirm(Rig));
	TestTrue(TEXT("Accept again opened it"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...said once more"), Placed.Listener->ExpansionStates.Num(), 2);
	return true;
}

#endif
