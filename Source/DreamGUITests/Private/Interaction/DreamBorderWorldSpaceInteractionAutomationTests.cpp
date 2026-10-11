// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamBorder's pointer events on a panel in the level, held to UMG's UBorder in a UWidgetComponent.
 *
 * UBorder hands SBorder's mouse handlers on, and SBorder is hit where it is painted (FHittestGrid::AddWidget, SlateCore/
 * Private/Input/HittestGrid.cpp). In a widget component the painting is the component's window and the pointer is the
 * interaction component's hit on it: a press that lands just inside the painted edge reaches the border and one that lands
 * just past it does not, wherever in the level the border stands. A finger is the same press through the interaction's
 * pointer index and Slate's touch fallback (bTouchFallbackToMouse).
 */
namespace DreamBorderWorldTestLocal
{
	const FVector2D BorderSize(200.0, 120.0);
	/** Off the middle of the panel, so the ray has to find the position as well as the size. Panel units, Y up. */
	const FVector2D BorderPosition(-110.0, 60.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderWorldHitTest,
	"DreamGUI.Border.OnAWorldPanelAClickAndATapAreHeardExactlyWhereTheBorderIsSeenThroughTheCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamBorderWorldHitTest, "DreamGUI.Border.OnAWorldPanelAClickAndATapAreHeardExactlyWhereTheBorderIsSeenThroughTheCamera", "[Pointer][Touch][World]")

/*
 * A border that reports its pointer events, 200 by 120 units, off the middle of a panel three metres ahead. A click three
 * pixels inside each of its four edges, level with the edge's middle, as the camera sees it, is one button down and one
 * button up; clicks four pixels past its right edge and past its bottom edge land on the bare panel and are heard by nothing.
 * Then the same six points with a finger.
 *
 * Inside the middle of each edge rather than inside a corner: the border's face is rounded (its style's corner radius), and a
 * DreamGUI rect block takes no hit in the corners its radius cuts off (UDreamRectBlock::bRaycastSupportCornerRadius, on by
 * default) -- three pixels in from a corner seen at a little over two pixels to the unit is just off the arc. Slate hit-tests
 * a widget's whole rectangle whatever its brush draws; which of the two a border should follow is not this test's question.
 */
bool FDreamBorderWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
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
	UDreamBorder* Border = Rig.MakeControl<UDreamBorder>(TEXT("Frame"), Stage.Panel, BorderSize, BorderPosition);
	if (!TestTrue(TEXT("A border with a face can be made on the panel"), Border != nullptr && Border->FaceNode != nullptr))
	{
		return false;
	}
	Border->SetReportMouseEvents(true);
	Border->OnMouseButtonDownEvent.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleBorderButtonDown);
	Border->OnMouseButtonUpEvent.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleBorderButtonUp);
	// Every press below is a press of its own, never the second half of a double click.
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(2);

	const TOptional<FBox2D> Face = SeenRect(Rig, Border->FaceNode.Get());
	if (!TestTrue(TEXT("The camera sees the border's face"), Face.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The face is seen at its size in panel units (%s)"), *Describe(Face.GetValue())),
		IsSeenAtItsSize(Face.GetValue(), Border->FaceNode->GetSize()));
	// Past the whole control as well as its face, so a press there is past everything the border draws.
	const FBox2D Outer = Covering(Face.GetValue(), SeenRect(Rig, Border));
	// Three pixels in from the left, top, right and bottom edges as seen, each level with the middle of its edge.
	const FBox2D Seen = Face.GetValue();
	const FVector2D InsideEdges[] =
	{
		FVector2D(Seen.Min.X + 3.0, Seen.GetCenter().Y),
		FVector2D(Seen.GetCenter().X, Seen.Min.Y + 3.0),
		FVector2D(Seen.Max.X - 3.0, Seen.GetCenter().Y),
		FVector2D(Seen.GetCenter().X, Seen.Max.Y - 3.0),
	};

	bool bClicksInside = true;
	for (const FVector2D& Point : InsideEdges)
	{
		bClicksInside = ClickAt(Rig, Point) && bClicksInside;
	}
	TestTrue(TEXT("Clicks just inside the left, top, right and bottom edges complete"), bClicksInside);
	TestEqual(TEXT("Each click just inside the seen edge is a button down"), Listener->BorderButtonDownCount, 4);
	TestEqual(TEXT("...and a button up"), Listener->BorderButtonUpCount, 4);
	TestTrue(TEXT("Clicks just past the right and bottom edges complete"),
		ClickAt(Rig, PastRightEdge(Outer, 4.0)) && ClickAt(Rig, PastBottomEdge(Outer, 4.0)));
	TestEqual(TEXT("A click just past the seen edge reaches nothing"), Listener->BorderButtonDownCount, 4);

	bool bTapsInside = true;
	for (const FVector2D& Point : InsideEdges)
	{
		bTapsInside = TapAt(Rig, Point) && bTapsInside;
	}
	TestTrue(TEXT("Taps just inside the four edges complete"), bTapsInside);
	TestEqual(TEXT("Each tap just inside the seen edge is a button down"), Listener->BorderButtonDownCount, 8);
	TestEqual(TEXT("...and a button up"), Listener->BorderButtonUpCount, 8);
	TestTrue(TEXT("Taps just past the two edges complete"),
		TapAt(Rig, PastRightEdge(Outer, 4.0)) && TapAt(Rig, PastBottomEdge(Outer, 4.0)));
	TestEqual(TEXT("A tap just past the seen edge reaches nothing"), Listener->BorderButtonDownCount, 8);
	TestEqual(TEXT("...down or up"), Listener->BorderButtonUpCount, 8);
	return true;
}

#endif
