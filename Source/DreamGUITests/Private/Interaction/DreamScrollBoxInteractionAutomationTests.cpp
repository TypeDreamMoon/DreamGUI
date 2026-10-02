// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamScrollTypes.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIScrollView.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * THE SCROLL BOX UNDER A REAL POINTER.
 *
 * Wheel, drag and scroll bar, each delivered through the driver and judged by the box's offset and
 * by OnUserScrolled -- the event UMG raises for scrolling the user did, as distinct from scrolling
 * code did. The reference is Slate's SScrollBox, which UMG's UScrollBox wraps:
 *
 *  - OnMouseWheel scrolls by -WheelDelta times one notch, clamped to [0, end] (EAllowOverscroll::No),
 *    and ScrollBy raises OnUserScrolled with the new offset. The event is HANDLED -- kept from the
 *    widgets behind -- when the offset moved, or always under EConsumeMouseWheel::Always; at a limit
 *    under the default WhenScrollingPossible it is left unhandled and reaches the box around this one.
 *    A horizontal box spends the same wheel along its own axis.
 *  - A drag with the RIGHT button scrolls (bAllowRightClickDragScrolling, on by default): once the
 *    pointer has travelled the drag trigger distance, every move scrolls by its own delta, the move
 *    that crossed the distance included, so the content stays under the pointer that grabbed it. On
 *    release the box coasts on (BeginInertialScrolling). A left-button drag does not scroll an
 *    SScrollBox at all. This library's scroll view does take one, and whether that difference is
 *    meant is a design decision rather than something for a test to settle, so it is not asserted.
 *  - Dragging the bar's thumb scrolls the box to the matching place: thumb start over track length
 *    is offset over content length, which is the whole meaning of a scroll bar.
 *
 * One notch here is ScrollSensitivity * WheelScrollMultiplier local units -- the box's own statement of
 * what a notch travels, read from the box rather than written down, so a changed default cannot make
 * these tests lie.
 */
namespace DreamScrollBoxInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** One notch toward the user, in the shape the production input actors send a wheel: InputScroll(FVector2D(Axis, Axis)). */
	const FVector2D WheelTowardUser(-1.0, -1.0);
	const FVector2D WheelAwayFromUser(1.0, 1.0);

	/**
	 * A scroll box with InRowCount rows of InRowSize in it, measured and laid out.
	 *
	 * The rows go in through MakeWidget, which parents each one to the content node with a hit-testable
	 * visual, and the box is then told to re-measure. RefreshContentExtent is the public call for exactly
	 * that; AddContent is a TrySetParent followed by the same call.
	 */
	UDreamScrollBox* MakeFilledBox(FDreamDriverRig& InRig, const FString& InName, UDreamWidget* InParent,
		const FVector2D& InSize, int32 InRowCount, const FVector2D& InRowSize, TArray<UDreamWidget*>* OutRows = nullptr)
	{
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(InName, InParent, InSize);
		if (Box == nullptr || Box->GetContentNode() == nullptr)
		{
			return Box;
		}
		for (int32 RowIndex = 0; RowIndex < InRowCount; ++RowIndex)
		{
			UDreamWidget* Row = InRig.MakeWidget(FString::Printf(TEXT("%s_Row%02d"), *InName, RowIndex),
				Box->GetContentNode(), InRowSize);
			if (OutRows != nullptr)
			{
				OutRows->Add(Row);
			}
		}
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	bool HasParts(const UDreamScrollBox* InBox)
	{
		return InBox != nullptr && InBox->ViewportNode != nullptr && InBox->GetContentNode() != nullptr;
	}

	/** How far one notch travels on this box, in local units. */
	float NotchOf(const UDreamScrollBox* InBox)
	{
		return InBox->GetScrollSensitivity() * InBox->GetWheelScrollMultiplier();
	}

	/**
	 * Hold a scroll box nested in another one to two rows, as a nested box on a real screen is held.
	 *
	 * A scroll box measures as its whole content, as SScrollBox does: SScrollPanel's ComputeDesiredSize
	 * sums its children along the scroll axis. The outer box's stack then gives each
	 * child what it measures, so an inner box nothing bounds grows to fit every row it holds and has
	 * nothing left to scroll -- in UMG as here. UMG bounds it with a SizeBox around it; this library's
	 * panel slot carries the same Min/MaxDesiredSize, so the bound goes on the slot.
	 *
	 * False when the box has no slot to hold it by, which is a box outside any panel.
	 */
	bool HoldToTwoRows(UDreamScrollBox* InInner)
	{
		UDreamPanelSlot* InnerSlot = InInner != nullptr ? InInner->GetPanelSlot() : nullptr;
		if (InnerSlot == nullptr)
		{
			return false;
		}
		InnerSlot->SetMinDesiredSize(FVector2D(0.0, 200.0));
		InnerSlot->SetMaxDesiredSize(FVector2D(0.0, 200.0));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionWheelTest,
	"DreamGUI.ScrollBox.ThreeNotchesDownScrollThreeNotchesAndTurningBackStopsAtTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionWheelTest, "DreamGUI.ScrollBox.ThreeNotchesDownScrollThreeNotchesAndTurningBackStopsAtTheTop", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// Twenty rows of 100 in a 400-tall window: plenty to scroll.
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	const float Notch = NotchOf(Box);
	const float End = Box->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("There is more than three notches to scroll (end %.1f, notch %.1f)"), End, Notch),
		Notch > 0.0f && End > 3.0f * Notch))
	{
		return false;
	}

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	for (int32 NotchIndex = 0; NotchIndex < 3; ++NotchIndex)
	{
		TestTrue(TEXT("A notch toward the user completes"), Viewport->ScrollBy(WheelTowardUser));
	}
	TestNearlyEqual(TEXT("Three notches scrolled three notches' distance"), Box->GetScrollOffset(), 3.0f * Notch, 0.5f);
	TestEqual(TEXT("Each notch was reported as the user scrolling"), UserScrolled->NumFloats(), 3);
	TestNearlyEqual(TEXT("The last report carried the offset the box is at"), UserScrolled->LastFloat(-1.0f), Box->GetScrollOffset(), 0.5f);

	// One more notch back than it took to come down: the last one has nowhere to go.
	for (int32 NotchIndex = 0; NotchIndex < 4; ++NotchIndex)
	{
		TestTrue(TEXT("A notch away from the user completes"), Viewport->ScrollBy(WheelAwayFromUser));
	}
	TestNearlyEqual(TEXT("Turning back stops at the top"), Box->GetScrollOffset(), 0.0f, 0.5f);
	TestTrue(TEXT("No report ever carried an offset above the top"), UserScrolled->AllFloatsWithin(0.0f, End, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionWheelAtEndTest,
	"DreamGUI.ScrollBox.TheWheelStopsAtTheEndAndTurningItFurtherChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionWheelAtEndTest, "DreamGUI.ScrollBox.TheWheelStopsAtTheEndAndTurningItFurtherChangesNothing", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionWheelAtEndTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	const float Notch = NotchOf(Box);
	const float End = Box->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("There is more than two notches to scroll (end %.1f, notch %.1f)"), End, Notch),
		Notch > 0.0f && End > 2.0f * Notch))
	{
		return false;
	}
	// A notch and a half short of the end, placed by code: the wheel then has one whole notch to spend
	// and one it can only half spend, which is where the clamp has to happen.
	Box->SetScrollOffset(End - 1.5f * Notch);
	Rig.PumpFrames(1);

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	TestTrue(TEXT("The first notch completes"), Viewport->ScrollBy(WheelTowardUser));
	TestTrue(TEXT("The second notch completes"), Viewport->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The second notch stopped exactly at the end"), Box->GetScrollOffset(), End, 0.5f);
	TestNearlyEqual(TEXT("The last report carried the end"), UserScrolled->LastFloat(-1.0f), End, 0.5f);

	TestTrue(TEXT("A notch past the end completes"), Viewport->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("A notch past the end leaves the box at the end"), Box->GetScrollOffset(), End, 0.5f);
	TestTrue(TEXT("No report ever carried an offset past the end"), UserScrolled->AllFloatsWithin(0.0f, End, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionRightDragTest,
	"DreamGUI.ScrollBox.DraggingTheContentWithTheRightButtonKeepsItUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionRightDragTest, "DreamGUI.ScrollBox.DraggingTheContentWithTheRightButtonKeepsItUnderThePointer", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionRightDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	TestTrue(TEXT("Right-button drag scrolling is on by default, as in UMG"), Box->GetAllowRightClickDragScrolling());

	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FBox2D> ViewportRect = Driver->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->GetPixelRect();
	if (!TestTrue(TEXT("The viewport is on screen"), ViewportRect.IsSet())
		|| !TestTrue(TEXT("The viewport has a height"), ViewportRect->Max.Y - ViewportRect->Min.Y > 1.0))
	{
		return false;
	}
	// Offsets are local units and the pointer moves in pixels; the viewport's own two heights convert.
	const double UnitsPerPixel = Box->ViewportNode->GetHeight() / (ViewportRect->Max.Y - ViewportRect->Min.Y);

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// Grab the content and pull it UP 150 pixels in three moves: the first just past the raycaster's
	// drag threshold -- read rather than assumed, with a margin because the comparison is strictly
	// greater-than -- and the rest in two equal halves. The first move is the one that turns the press
	// into a drag, as crossing the drag trigger distance does under SScrollBox::OnMouseMove, so it
	// already scrolls, and so does every move after.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	const double LaterMove = (150.0 - FirstMove) * 0.5;
	TestTrue(TEXT("The right-button drag completes"),
		Driver->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.WaitFrames(1)
			.Perform());

	// Read with the button still down: letting go of a drag that was moving a moment ago flings the
	// content on (SScrollBox begins inertial scrolling), and the claim here is about the drag itself.
	const float Offset = Box->GetScrollOffset();
	TestTrue(TEXT("Letting go of the right button completes"),
		Driver->Sequence().Release(EDreamUIMouseButtonType::Right).Perform());
	const float Pulled = static_cast<float>(150.0 * UnitsPerPixel);
	// Two claims, so a red run says which one failed: that the drag scrolled at all, the way it was
	// pulled; and that it scrolled the WHOLE distance, the move that started the drag included.
	TestTrue(FString::Printf(TEXT("The drag scrolled the content the way it was pulled (offset %.1f)"), Offset),
		Offset > 0.5f * Pulled);
	TestNearlyEqual(TEXT("The content moved the full 150 pixels, staying under the pointer that grabbed it"),
		Offset, Pulled, static_cast<float>(1.5 * UnitsPerPixel));
	TestTrue(TEXT("The drag was reported as the user scrolling"), UserScrolled->NumFloats() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionRightDragOffTest,
	"DreamGUI.ScrollBox.ARightButtonDragScrollsNothingWhenRightClickDragScrollingIsOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionRightDragOffTest, "DreamGUI.ScrollBox.ARightButtonDragScrollsNothingWhenRightClickDragScrollingIsOff", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionRightDragOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	Box->SetAllowRightClickDragScrolling(false);
	Rig.PumpFrames(1);

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// The same gesture as the test beside this one, with the switch off.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	const double LaterMove = (150.0 - FirstMove) * 0.5;
	TestTrue(TEXT("The right-button drag completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.WaitFrames(1)
			.Release(EDreamUIMouseButtonType::Right)
			.Perform());

	TestNearlyEqual(TEXT("The content did not move"), Box->GetScrollOffset(), 0.0f, 0.5f);
	TestEqual(TEXT("Nothing was reported as the user scrolling"), UserScrolled->NumFloats(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionFlingTest,
	"DreamGUI.ScrollBox.LettingGoOfADragWhileItMovesLeavesTheContentCoastingTheSameWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionFlingTest, "DreamGUI.ScrollBox.LettingGoOfADragWhileItMovesLeavesTheContentCoastingTheSameWay", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionFlingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	UDreamDriverInputModule* DriverInput = Rig.InputModule();
	if (!TestNotNull(TEXT("The rig has an input module"), DriverInput))
	{
		return false;
	}

	// The first move just past the raycaster's drag threshold, read rather than assumed, so the drag is
	// under way before the moves that give it its speed.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	TestTrue(TEXT("The drag gets going"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -40.0))
			.Perform());

	// A fling is letting go WHILE moving: the last move and the release arrive in the same frame. The
	// sequence gives every input step a frame of its own, so this one frame is built by hand on the
	// input module the sequence drives -- the move is taken at once, the release is queued at the new
	// position, and the frame delivers them together.
	DriverInput->MoveBy(FVector2D(0.0, -40.0));
	DriverInput->Release(EDreamUIMouseButtonType::Right);
	Rig.PumpFrames(1);

	const float OffsetAtRelease = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The drag scrolled down through the content (offset %.1f)"), OffsetAtRelease), OffsetAtRelease > 0.0f);
	// SScrollBox::OnMouseButtonUp begins inertial scrolling when a right-button drag was scrolling.
	TestTrue(TEXT("The box is still scrolling after the button came up"), Box->GetIsScrolling());

	Rig.PumpFrames(1);
	const float OffsetAfter = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The content kept moving the way it was flung (%.2f then %.2f)"), OffsetAtRelease, OffsetAfter),
		OffsetAfter > OffsetAtRelease + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionLateReleaseFlingTest,
	"DreamGUI.ScrollBox.LettingGoAFrameAfterTheLastMoveStillLeavesTheContentCoasting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionLateReleaseFlingTest, "DreamGUI.ScrollBox.LettingGoAFrameAfterTheLastMoveStillLeavesTheContentCoasting", "[Pointer][Animated]")

/*
 * A fling took its speed from the release frame's own movement and from nothing else. A held button is
 * delivered a drag every frame, moved or not, so a mouse that let go the frame after its last move --
 * the usual way to let go -- brought no movement to the release and flung nothing at all. SScrollBox
 * keeps a tenth of a second of moves for its fling (FInertialScrollManager), and so does the view now.
 *
 * Checked here: the drag of the fling test above, a frame with the button held still, then the release:
 * the box is still scrolling, and moves on the way it was dragged.
 */
bool FDreamScrollBoxInteractionLateReleaseFlingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}

	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	TestTrue(TEXT("The drag, a still frame and the release complete"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -40.0))
			.WaitFrames(1)
			.Release(EDreamUIMouseButtonType::Right)
			.Perform());

	const float OffsetAtRelease = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The drag scrolled down through the content (offset %.1f)"), OffsetAtRelease), OffsetAtRelease > 0.0f);
	TestTrue(TEXT("The box is still scrolling after the button came up"), Box->GetIsScrolling());
	Rig.PumpFrames(1);
	const float OffsetAfter = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The content kept moving the way it was dragged (%.2f then %.2f)"), OffsetAtRelease, OffsetAfter),
		OffsetAfter > OffsetAtRelease + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionBarHandleTest,
	"DreamGUI.ScrollBox.DraggingTheScrollBarHandleScrollsTheContentToTheMatchingPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionBarHandleTest, "DreamGUI.ScrollBox.DraggingTheScrollBarHandleScrollsTheContentToTheMatchingPlace", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionBarHandleTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	// Permanent, so the bar is out whatever the auto-hide decided when the box was still empty: this
	// test is about what the bar does, and whether it comes out by itself is the last test's question.
	Box->SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility::Permanent);
	Rig.PumpFrames(2);
	UDreamScrollBar* Bar = Box->ScrollBarNode.Get();
	if (!TestNotNull(TEXT("The box has a scroll bar"), Bar)
		|| !TestNotNull(TEXT("The bar has a track"), Bar->TrackNode.Get())
		|| !TestNotNull(TEXT("The bar has a handle"), Bar->HandleNode.Get()))
	{
		return false;
	}

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Bar->HandleNode.Get()));
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Bar->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> HandleBefore = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The bar's track is on screen"), Track.IsSet())
		|| !TestTrue(TEXT("The bar's handle is on screen"), HandleBefore.IsSet()))
	{
		return false;
	}
	const double TrackLength = Track->Max.Y - Track->Min.Y;
	const double TravelLength = TrackLength - (HandleBefore->Max.Y - HandleBefore->Min.Y);
	if (!TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), TravelLength), TravelLength > 150.0))
	{
		return false;
	}

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	const double DragPixels = 100.0;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(0.0, DragPixels)));

	const TOptional<FBox2D> HandleAfter = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen"), HandleAfter.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The handle followed the pointer down"),
		static_cast<float>(HandleAfter->Min.Y - HandleBefore->Min.Y), static_cast<float>(DragPixels), 1.5f);
	// The meaning of a scroll bar: the thumb sits as far down its track as the window sits down the
	// content. Both sides are fractions, so the units -- pixels on one side, local units on the other
	// -- cancel.
	const float ContentLength = Box->GetContentNode()->GetHeight();
	if (!TestTrue(TEXT("The content has a length"), ContentLength > 1.0f))
	{
		return false;
	}
	const float ThumbFraction = static_cast<float>((HandleAfter->Min.Y - Track->Min.Y) / TrackLength);
	TestNearlyEqual(TEXT("The window sits as far down the content as the handle sits down the track"),
		Box->GetScrollOffset() / ContentLength, ThumbFraction, static_cast<float>(1.5 / TrackLength));
	TestTrue(TEXT("Dragging the bar was reported as the user scrolling"), UserScrolled->NumFloats() >= 1);
	TestNearlyEqual(TEXT("The last report carried the offset the box is at"), UserScrolled->LastFloat(-1.0f), Box->GetScrollOffset(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionRevealThenWheelTest,
	"DreamGUI.ScrollBox.TheWheelCarriesOnFromWhereScrollingARowIntoViewLeftTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionRevealThenWheelTest, "DreamGUI.ScrollBox.TheWheelCarriesOnFromWhereScrollingARowIntoViewLeftTheBox", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionRevealThenWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	TArray<UDreamWidget*> Rows;
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0), &Rows);
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestEqual(TEXT("All twenty rows were made"), Rows.Num(), 20)
		|| !TestNotNull(TEXT("The fifteenth row exists"), Rows[14]))
	{
		return false;
	}
	const float Notch = NotchOf(Box);

	// Not animated, so the reveal has finished by the time the wheel turns: the question is where the
	// wheel starts from, not whether it can interrupt a glide.
	TestTrue(TEXT("Scrolling the fifteenth row into view succeeds"), Box->ScrollWidgetIntoView(Rows[14], false));
	Rig.PumpFrames(1);
	const float Revealed = Box->GetScrollOffset();
	if (!TestTrue(FString::Printf(TEXT("Revealing the fifteenth row scrolled the box (offset %.1f)"), Revealed), Revealed > 0.0f)
		|| !TestTrue(TEXT("There is still a notch to go after it"), Revealed + Notch <= Box->GetScrollOffsetOfEnd() + 0.5f))
	{
		return false;
	}

	TestTrue(TEXT("A notch toward the user completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The notch carried on from where the reveal left the box"), Box->GetScrollOffset(), Revealed + Notch, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionNestedHandOnTest,
	"DreamGUI.ScrollBox.AWheelTheInnerBoxCannotSpendAnyMoreScrollsTheOuterBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionNestedHandOnTest, "DreamGUI.ScrollBox.AWheelTheInnerBoxCannotSpendAnyMoreScrollsTheOuterBox", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionNestedHandOnTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// An outer box whose content is an inner box -- two rows tall and holding three, so a row of travel
	// -- and then ten rows of its own.
	UDreamScrollBox* Outer = Rig.MakeControl<UDreamScrollBox>(TEXT("Outer"), nullptr, FVector2D(400.0, 400.0));
	if (!TestTrue(TEXT("The outer box came up with a viewport and a content node"), HasParts(Outer)))
	{
		return false;
	}
	UDreamScrollBox* Inner = MakeFilledBox(Rig, TEXT("Inner"), Outer->GetContentNode(), FVector2D(300.0, 200.0), 3, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The inner box came up with a viewport and a content node"), HasParts(Inner))
		|| !TestTrue(TEXT("The inner box is held to two rows by its slot in the outer box"), HoldToTwoRows(Inner)))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 10; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Outer_Row%02d"), RowIndex), Outer->GetContentNode(), FVector2D(400.0, 100.0));
	}
	// In the order the two sizes decide each other: the outer box's layout pass is what gives the inner
	// box its two rows, and only after it does re-measuring the inner box see a window its rows overflow
	// (RefreshContentExtent reads the viewport's live height).
	Outer->RefreshContentExtent();
	Rig.PumpFrames(2);
	Inner->RefreshContentExtent();
	Rig.PumpFrames(2);

	// UMG's default, and this library's: hand the wheel on at a limit.
	TestTrue(TEXT("The inner box hands the wheel on when it can scroll no further, by default"),
		Inner->GetConsumeMouseWheel() == EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible);
	const float InnerNotch = NotchOf(Inner);
	const float OuterNotch = NotchOf(Outer);
	const float InnerEnd = Inner->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("The inner box has more than one notch to scroll (end %.1f)"), InnerEnd), InnerEnd > InnerNotch + 0.5f)
		|| !TestTrue(FString::Printf(TEXT("The outer box has a notch to scroll (end %.1f)"), Outer->GetScrollOffsetOfEnd()),
			Outer->GetScrollOffsetOfEnd() > OuterNotch))
	{
		return false;
	}

	FDreamElementRef InnerViewport = Rig.Driver()->Find(FDreamBy::Widget(Inner->ViewportNode.Get()));
	TestTrue(TEXT("The first notch completes"), InnerViewport->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The first notch scrolled the box under the pointer"), Inner->GetScrollOffset(), InnerNotch, 0.5f);
	TestNearlyEqual(TEXT("and left the box around it alone"), Outer->GetScrollOffset(), 0.0f, 0.5f);

	for (int32 Guard = 0; Guard < 10 && Inner->GetScrollOffset() < InnerEnd - 0.5f; ++Guard)
	{
		TestTrue(TEXT("A notch toward the inner box's end completes"), InnerViewport->ScrollBy(WheelTowardUser));
	}
	TestNearlyEqual(TEXT("The inner box reached its end"), Inner->GetScrollOffset(), InnerEnd, 0.5f);
	TestNearlyEqual(TEXT("with the outer box still at its top"), Outer->GetScrollOffset(), 0.0f, 0.5f);

	TestTrue(TEXT("The notch past the inner box's end completes"), InnerViewport->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The notch the inner box could not spend scrolled the outer box"), Outer->GetScrollOffset(), OuterNotch, 0.5f);
	TestNearlyEqual(TEXT("and the inner box stayed at its end"), Inner->GetScrollOffset(), InnerEnd, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionNestedConsumeTest,
	"DreamGUI.ScrollBox.AnInnerBoxThatAlwaysConsumesTheWheelKeepsItFromTheOuterBoxAtItsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionNestedConsumeTest, "DreamGUI.ScrollBox.AnInnerBoxThatAlwaysConsumesTheWheelKeepsItFromTheOuterBoxAtItsEnd", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionNestedConsumeTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Outer = Rig.MakeControl<UDreamScrollBox>(TEXT("Outer"), nullptr, FVector2D(400.0, 400.0));
	if (!TestTrue(TEXT("The outer box came up with a viewport and a content node"), HasParts(Outer)))
	{
		return false;
	}
	UDreamScrollBox* Inner = MakeFilledBox(Rig, TEXT("Inner"), Outer->GetContentNode(), FVector2D(300.0, 200.0), 3, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The inner box came up with a viewport and a content node"), HasParts(Inner))
		|| !TestTrue(TEXT("The inner box is held to two rows by its slot in the outer box"), HoldToTwoRows(Inner)))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 10; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Outer_Row%02d"), RowIndex), Outer->GetContentNode(), FVector2D(400.0, 100.0));
	}
	// Measured in the order the test before this one explains: the outer box's pass first.
	Outer->RefreshContentExtent();
	Rig.PumpFrames(2);
	Inner->RefreshContentExtent();
	// EConsumeMouseWheel::Always: SScrollBox::ScrollBy answers handled even when nothing moved.
	Inner->SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel::Always);
	Rig.PumpFrames(2);

	const float InnerEnd = Inner->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("The inner box has somewhere to scroll (end %.1f)"), InnerEnd), InnerEnd > 0.5f)
		|| !TestTrue(TEXT("The outer box has somewhere to scroll"), Outer->GetScrollOffsetOfEnd() > 0.5f))
	{
		return false;
	}

	FDreamElementRef InnerViewport = Rig.Driver()->Find(FDreamBy::Widget(Inner->ViewportNode.Get()));
	for (int32 Guard = 0; Guard < 10 && Inner->GetScrollOffset() < InnerEnd - 0.5f; ++Guard)
	{
		TestTrue(TEXT("A notch toward the inner box's end completes"), InnerViewport->ScrollBy(WheelTowardUser));
	}
	TestNearlyEqual(TEXT("The inner box reached its end"), Inner->GetScrollOffset(), InnerEnd, 0.5f);

	TestTrue(TEXT("A notch past the inner box's end completes"), InnerViewport->ScrollBy(WheelTowardUser));
	TestTrue(TEXT("A second notch past it completes"), InnerViewport->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The inner box kept the wheel: the outer box never moved"), Outer->GetScrollOffset(), 0.0f, 0.5f);
	TestNearlyEqual(TEXT("and the inner box is still at its end"), Inner->GetScrollOffset(), InnerEnd, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionHorizontalWheelTest,
	"DreamGUI.ScrollBox.TheWheelScrollsAHorizontalBoxAlongItsOwnAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionHorizontalWheelTest, "DreamGUI.ScrollBox.TheWheelScrollsAHorizontalBoxAlongItsOwnAxis", "[Pointer][Animated]")

bool FDreamScrollBoxInteractionHorizontalWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// Twenty columns of 100 in a 400-wide strip, turned horizontal after they are in: the style push
	// that SetOrientation makes re-stacks and re-measures them.
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Strip"), nullptr, FVector2D(400.0, 200.0), 20, FVector2D(100.0, 200.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	Box->SetOrientation(EDreamPanelOrientation::Horizontal);
	Rig.PumpFrames(2);
	const float Notch = NotchOf(Box);
	if (!TestTrue(FString::Printf(TEXT("The strip has more than a notch to scroll sideways (end %.1f)"), Box->GetScrollOffsetOfEnd()),
		Box->GetScrollOffsetOfEnd() > Notch))
	{
		return false;
	}

	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// The ordinary wheel, not a sideways one: SScrollBox::OnMouseWheel spends GetWheelDelta along
	// whichever axis the box scrolls. There is no Shift+wheel rule in SScrollBox to copy.
	TestTrue(TEXT("A notch toward the user completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->ScrollBy(WheelTowardUser));
	TestNearlyEqual(TEXT("The strip scrolled one notch along its own axis"), Box->GetScrollOffset(), Notch, 0.5f);
	TestEqual(TEXT("The notch was reported as the user scrolling"), UserScrolled->NumFloats(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionNestedCrossDragTest,
	"DreamGUI.ScrollBox.ADragAcrossAnInnerBoxThatScrollsTheOtherWayScrollsTheOuterBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionNestedCrossDragTest, "DreamGUI.ScrollBox.ADragAcrossAnInnerBoxThatScrollsTheOtherWayScrollsTheOuterBox", "[Pointer][Animated]")

/*
 * A view that scrolls one way claimed every drag that began over it, whatever way the drag ran, and
 * consumed every move of one it had refused: a sideways strip inside a page took the page's vertical
 * drag, moved nothing with it, and kept it from the page. The wheel was already handed on in that case
 * (see the test above); the drag now is too -- refused, or running along the axis the view does not
 * scroll, it reaches the view around it.
 *
 * Checked here: an outer box holding a strip that scrolls sideways; a right-button drag pulled straight
 * up over the strip scrolls the outer box and leaves the strip where it was.
 */
bool FDreamScrollBoxInteractionNestedCrossDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// The strip: six columns of 100 in a box 300 wide, held two rows tall, turned sideways -- and then
	// ten rows of the outer box's own beneath it.
	UDreamScrollBox* Outer = Rig.MakeControl<UDreamScrollBox>(TEXT("Outer"), nullptr, FVector2D(400.0, 400.0));
	if (!TestTrue(TEXT("The outer box came up with a viewport and a content node"), HasParts(Outer)))
	{
		return false;
	}
	UDreamScrollBox* Strip = MakeFilledBox(Rig, TEXT("Strip"), Outer->GetContentNode(), FVector2D(300.0, 200.0), 6, FVector2D(100.0, 200.0));
	if (!TestTrue(TEXT("The strip came up with a viewport and a content node"), HasParts(Strip))
		|| !TestTrue(TEXT("The strip is held to two rows by its slot in the outer box"), HoldToTwoRows(Strip)))
	{
		return false;
	}
	Strip->SetOrientation(EDreamPanelOrientation::Horizontal);
	for (int32 RowIndex = 0; RowIndex < 10; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Outer_Row%02d"), RowIndex), Outer->GetContentNode(), FVector2D(400.0, 100.0));
	}
	// In the order the two sizes decide each other, as the nested wheel tests explain.
	Outer->RefreshContentExtent();
	Rig.PumpFrames(2);
	Strip->RefreshContentExtent();
	Rig.PumpFrames(2);
	if (!TestTrue(FString::Printf(TEXT("The strip has somewhere to scroll sideways (end %.1f)"), Strip->GetScrollOffsetOfEnd()), Strip->GetScrollOffsetOfEnd() > 0.5f)
		|| !TestTrue(FString::Printf(TEXT("and the outer box somewhere to scroll down (end %.1f)"), Outer->GetScrollOffsetOfEnd()), Outer->GetScrollOffsetOfEnd() > 100.0f))
	{
		return false;
	}

	// Straight up over the strip, past the raycaster's drag threshold first: the outer box's own
	// gesture, whichever box the press landed in.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	TestTrue(TEXT("The right-button drag over the strip completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Strip->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -40.0))
			.MoveBy(FVector2D(0.0, -40.0))
			.WaitFrames(1)
			.Release(EDreamUIMouseButtonType::Right)
			.Perform());
	Rig.PumpFrames(1);

	// Canvas units are pixels here, so the pull is the distance the outer box had to follow.
	TestTrue(FString::Printf(TEXT("The outer box scrolled with the drag (offset %.1f)"), Outer->GetScrollOffset()),
		Outer->GetScrollOffset() > 40.0f);
	TestNearlyEqual(TEXT("and the strip did not move"), Strip->GetScrollOffset(), 0.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionNoOverscrollFlingTest,
	"DreamGUI.ScrollBox.WithOverscrollOffNeitherTheDragNorTheFlingCarriesTheContentPastItsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionNoOverscrollFlingTest, "DreamGUI.ScrollBox.WithOverscrollOffNeitherTheDragNorTheFlingCarriesTheContentPastItsEnd", "[Pointer][Animated]")

/*
 * bAllowOverscroll off zeroes the damper that weighs a drag move starting past an end -- and nothing
 * else. The move that CROSSED the end went the whole way out, and a fling's in-range step carried the
 * content past the end on the frame it got there, for the spring to bring back: exactly the overshoot
 * the switch exists to remove.
 *
 * Checked here: a third of a row short of the end, a drag and a fling down the content, and on every
 * frame from the drag to well after the release the box is never past its end -- and it rests there.
 */
bool FDreamScrollBoxInteractionNoOverscrollFlingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	UDreamDriverInputModule* DriverInput = Rig.InputModule();
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestNotNull(TEXT("The rig has an input module"), DriverInput))
	{
		return false;
	}
	Box->SetAllowOverscroll(false);
	const float End = Box->GetScrollOffsetOfEnd();
	Box->SetScrollOffset(End - 30.0f);
	Rig.PumpFrames(1);

	// The fixture of the fling test above: under way past the drag threshold, then the last move and
	// the release in the same frame. The drag alone pulls further than the thirty units left.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	TestTrue(TEXT("The drag gets going"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -40.0))
			.Perform());
	TestTrue(FString::Printf(TEXT("The drag stopped at the end it crossed (offset %.2f, end %.2f)"), Box->GetScrollOffset(), End),
		Box->GetScrollOffset() <= End + 0.5f);
	DriverInput->MoveBy(FVector2D(0.0, -40.0));
	DriverInput->Release(EDreamUIMouseButtonType::Right);
	Rig.PumpFrames(1);
	TestTrue(FString::Printf(TEXT("The fling's own frame is not past the end (offset %.2f)"), Box->GetScrollOffset()),
		Box->GetScrollOffset() <= End + 0.5f);

	// Half a second of whatever the fling and the spring would do.
	float Furthest = Box->GetScrollOffset();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Rig.PumpFrames(1);
		Furthest = FMath::Max(Furthest, Box->GetScrollOffset());
	}
	TestTrue(FString::Printf(TEXT("No frame after the release was past the end (furthest %.2f, end %.2f)"), Furthest, End),
		Furthest <= End + 0.5f);
	TestNearlyEqual(TEXT("and the box rests at its end"), Box->GetScrollOffset(), End, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionNotchDuringRevealTest,
	"DreamGUI.ScrollBox.ANotchDuringAnAnimatedRevealCarriesOnFromWhereTheRevealIsGoing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionNotchDuringRevealTest, "DreamGUI.ScrollBox.ANotchDuringAnAnimatedRevealCarriesOnFromWhereTheRevealIsGoing", "[Pointer][Animated]")

/*
 * An animated reveal is a tween writing the content's position every frame until it lands, and it was
 * started and forgotten: a wheel notch in the meantime moved the content, and the tween's next frame
 * moved it back on its own way, landing on the revealed row as if the notch had never happened. UMG
 * adds a notch to where the scroll in flight is going.
 *
 * Checked here: the fifteenth row revealed with the glide on, a notch toward the user while it glides,
 * and the box comes to rest one notch past where the reveal alone puts it.
 */
bool FDreamScrollBoxInteractionNotchDuringRevealTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	TArray<UDreamWidget*> Rows;
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0), &Rows);
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestEqual(TEXT("All twenty rows were made"), Rows.Num(), 20))
	{
		return false;
	}
	const float Notch = NotchOf(Box);

	// Where the reveal lands, learned without the glide, and then the box put back at its top.
	TestTrue(TEXT("Revealing the fifteenth row succeeds"), Box->ScrollWidgetIntoView(Rows[14], false));
	Rig.PumpFrames(1);
	const float Revealed = Box->GetScrollOffset();
	Box->SetScrollOffset(0.0f);
	Rig.PumpFrames(1);
	if (!TestTrue(FString::Printf(TEXT("The reveal scrolls the box (offset %.1f)"), Revealed), Revealed > 0.0f)
		|| !TestTrue(TEXT("with a notch still to go after it"), Revealed + Notch <= Box->GetScrollOffsetOfEnd() + 0.5f))
	{
		return false;
	}

	TestTrue(TEXT("Revealing it again, gliding, succeeds"), Box->ScrollWidgetIntoView(Rows[14], true));
	Rig.PumpFrames(1);
	const float Gliding = Box->GetScrollOffset();
	if (!TestTrue(FString::Printf(TEXT("The glide has not landed yet (offset %.1f of %.1f)"), Gliding, Revealed), Gliding < Revealed - 0.5f))
	{
		return false;
	}
	TestTrue(TEXT("A notch toward the user completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->ScrollBy(WheelTowardUser));
	// Longer than the reveal's quarter of a second, so anything still gliding has landed.
	Rig.PumpFrames(30);
	TestNearlyEqual(TEXT("The box came to rest one notch past the revealed row"), Box->GetScrollOffset(), Revealed + Notch, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionQuickAnimatedNotchesTest,
	"DreamGUI.ScrollBox.TwoQuickAnimatedNotchesTravelTwoNotches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxInteractionQuickAnimatedNotchesTest, "DreamGUI.ScrollBox.TwoQuickAnimatedNotchesTravelTwoNotches", "[Pointer][Animated]")

/*
 * With wheel animation on, each notch starts a glide. The second notch of a quick pair measured its
 * step from wherever the first glide had got to, and started a second glide while the first one went
 * on writing the content -- two tweens, each towards its own end, and the pair travelled less than two
 * notches. A notch now adds to the destination of the glide in flight and takes it over.
 *
 * Checked here: two notches a couple of frames apart, well inside one glide, and the box comes to rest
 * two notches down.
 */
bool FDreamScrollBoxInteractionQuickAnimatedNotchesTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	Box->SetAnimateWheelScrolling(true);
	Rig.PumpFrames(1);
	const float Notch = NotchOf(Box);
	if (!TestTrue(FString::Printf(TEXT("There is more than two notches to scroll (end %.1f, notch %.1f)"), Box->GetScrollOffsetOfEnd(), Notch),
		Notch > 0.0f && Box->GetScrollOffsetOfEnd() > 2.0f * Notch))
	{
		return false;
	}

	FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	TestTrue(TEXT("The first notch completes"), Viewport->ScrollBy(WheelTowardUser));
	if (!TestTrue(FString::Printf(TEXT("The first notch is still gliding (offset %.1f)"), Box->GetScrollOffset()), Box->GetScrollOffset() < Notch - 0.5f))
	{
		return false;
	}
	TestTrue(TEXT("The second notch completes"), Viewport->ScrollBy(WheelTowardUser));
	// Longer than any glide here, so both have landed.
	Rig.PumpFrames(30);
	TestNearlyEqual(TEXT("The two notches travelled two notches"), Box->GetScrollOffset(), 2.0f * Notch, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInteractionAutoHideBarTest,
	"DreamGUI.ScrollBox.AnAutoHidingBarComesOutWhenContentAddedAtRunTimeOverflows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxInteractionAutoHideBarTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is scrolled: a scroll view's inertia is its Tick
	// and its bar re-places the handle in OnEnable and Start, none of which a world that never began
	// play gives them (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// Built empty, then filled at run time -- the way a list of saves or a shop is filled.
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestNotNull(TEXT("The box has a scroll bar"), Box->ScrollBarNode.Get()))
	{
		return false;
	}
	TestTrue(TEXT("The bar auto-hides by default, as UMG's does"),
		Box->GetScrollBarVisibility() == EDreamScrollBoxScrollbarVisibility::AutoHide);
	if (!TestTrue(FString::Printf(TEXT("The content overflows the window (view fraction %.3f)"), Box->GetViewFraction()),
		Box->GetViewFraction() < 1.0f))
	{
		return false;
	}

	// SScrollBar's visibility follows its track's IsNeeded() -- a thumb smaller than the track -- on
	// every frame, so the bar is out as soon as there is something to scroll, however the content got
	// there. Without it there is no handle to drag.
	TestTrue(TEXT("Content that overflows brought the auto-hiding bar out"), Box->ScrollBarNode->GetWidgetActiveInHierarchy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewFittingContentHoldsStillTest,
	"DreamGUI.ScrollView.ContentThatFitsCannotBePulledPastItsEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollViewFittingContentHoldsStillTest, "DreamGUI.ScrollView.ContentThatFitsCannotBePulledPastItsEnds", "[Pointer][Animated]")

/*
 * SScrollBox holds its offset at zero while its bar is not needed, so content that fits cannot be pulled anywhere. The
 * scroll view took a drag over content that fit and stretched a rubber band out of nothing for the spring to pull
 * back, so a short list wobbled under every grab. CanScrollInSmallSize, the switch that asks for that, now defaults to
 * off, and with it off a drag over content that fits is refused where it starts and handed on to whatever is around
 * the view. A right-button drag over content that fits is refused whatever the switch says, as SScrollBox takes one
 * only while its bar is needed. With the switch on, a left drag stretches the band again.
 */
bool FDreamScrollViewFittingContentHoldsStillTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	// Two rows of 100 in a 400-tall window: nothing to scroll.
	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 2, FVector2D(300.0, 100.0));
	UUIScrollView* View = Box != nullptr ? Box->GetScrollView() : nullptr;
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestNotNull(TEXT("The box has a scroll view"), View)
		|| !TestTrue(FString::Printf(TEXT("The content fits its window (end %.1f)"), Box->GetScrollOffsetOfEnd()),
			Box->GetScrollOffsetOfEnd() <= 0.5f))
	{
		return false;
	}
	TestFalse(TEXT("Content smaller than its window does not scroll by default"), View->GetCanScrollInSmallSize());
	const double RestingY = Box->GetContentNode()->GetAnchoredPosition().Y;

	// A grab and a 60-pixel pull up, the first move just past the drag threshold; read with the button still down, and
	// answered as how far the content was taken from where it rests.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	auto PullAndRead = [&](EDreamUIMouseButtonType InButton) -> float
	{
		TestTrue(TEXT("The drag completes"),
			Rig.Driver()->Sequence()
				.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
				.Press(InButton)
				.MoveBy(FVector2D(0.0, -FirstMove))
				.MoveBy(FVector2D(0.0, -60.0))
				.WaitFrames(1)
				.Perform());
		return static_cast<float>(Box->GetContentNode()->GetAnchoredPosition().Y - RestingY);
	};
	auto LetGo = [&](EDreamUIMouseButtonType InButton)
	{
		TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release(InButton).Perform());
		// Longer than any spring-back here.
		Rig.PumpFrames(90);
	};

	TestNearlyEqual(TEXT("A left drag over content that fits moves nothing"), PullAndRead(EDreamUIMouseButtonType::Left), 0.0f, 0.5f);
	TestNearlyEqual(TEXT("...and opens no band"), Box->GetOverscrollOffset(), 0.0f, 0.01f);
	LetGo(EDreamUIMouseButtonType::Left);
	TestFalse(TEXT("Let go, nothing is left moving"), Box->GetIsScrolling());

	View->SetCanScrollInSmallSize(true);
	TestNearlyEqual(TEXT("A right drag over content that fits moves nothing, whatever the switch says"),
		PullAndRead(EDreamUIMouseButtonType::Right), 0.0f, 0.5f);
	LetGo(EDreamUIMouseButtonType::Right);

	const float SwitchedOnPull = PullAndRead(EDreamUIMouseButtonType::Left);
	TestTrue(FString::Printf(TEXT("With the switch on, a left drag stretches the content out of place (by %.1f)"), SwitchedOnPull),
		FMath::Abs(SwitchedOnPull) > 1.0f);
	TestTrue(TEXT("...as a band past its end"), FMath::Abs(Box->GetOverscrollOffset()) > 1.0f);
	LetGo(EDreamUIMouseButtonType::Left);
	TestNearlyEqual(TEXT("...that springs back once let go"), Box->GetOverscrollOffset(), 0.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewGlideCountsAsScrollingTest,
	"DreamGUI.ScrollView.AGlideToAnOffsetCountsAsScrollingUntilItLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollViewGlideCountsAsScrollingTest, "DreamGUI.ScrollView.AGlideToAnOffsetCountsAsScrollingUntilItLands", "[Animated]")

/*
 * The scroll view could glide only to a child (ScrollTo), never to an offset, and IsScrolling did not count a glide at
 * all -- so a list that says when scrolling has finished said it on every frame of an animated reveal. Its new
 * GlideToScrollOffset clamps the offset as SetScrollOffset does and glides there on the same tween every ScrollTo
 * takes, and IsScrolling is true from the first frame of the glide until it lands. With no duration it is
 * SetScrollOffset.
 */
bool FDreamScrollViewGlideCountsAsScrollingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBox* Box = MakeFilledBox(Rig, TEXT("Box"), nullptr, FVector2D(300.0, 400.0), 20, FVector2D(300.0, 100.0));
	UUIScrollView* View = Box != nullptr ? Box->GetScrollView() : nullptr;
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box))
		|| !TestNotNull(TEXT("The box has a scroll view"), View))
	{
		return false;
	}
	const float End = Box->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("There is room to glide 500 (end %.1f)"), End), End > 600.0f))
	{
		return false;
	}
	TestFalse(TEXT("At rest nothing is scrolling"), View->IsScrolling());

	View->GlideToScrollOffset(FVector2D(0.0, 500.0), 0.25f);
	TestTrue(TEXT("A glide under way counts as scrolling"), View->IsScrolling());
	TestTrue(TEXT("...and the box says so too"), Box->GetIsScrolling());
	Rig.PumpFrames(1);
	TestTrue(FString::Printf(TEXT("A frame in, it has not landed yet (offset %.1f)"), Box->GetScrollOffset()),
		Box->GetScrollOffset() < 499.5f);
	TestTrue(TEXT("...and still counts as scrolling"), View->IsScrolling());

	// Longer than the quarter of a second it takes.
	Rig.PumpFrames(30);
	TestNearlyEqual(TEXT("It lands on the offset it was given"), Box->GetScrollOffset(), 500.0f, 0.5f);
	TestFalse(TEXT("...and, landed, is not scrolling any more"), View->IsScrolling());

	View->GlideToScrollOffset(FVector2D(0.0, End + 1000.0f), 0.25f);
	Rig.PumpFrames(30);
	TestNearlyEqual(TEXT("A glide aimed past the end stops at the end"), Box->GetScrollOffset(), End, 0.5f);

	View->GlideToScrollOffset(FVector2D(0.0, 200.0), 0.0f);
	TestNearlyEqual(TEXT("With no duration it is there at once"), Box->GetScrollOffset(), 200.0f, 0.5f);
	TestFalse(TEXT("...and nothing is left scrolling"), View->IsScrolling());
	return true;
}

namespace DreamScrollBoxInteractionTestLocal
{
	/**
	 * A vertical recycling list on the rig, 300 square with cells 100 tall, nothing in it yet: a host the list behaviour
	 * sits on, a content widget it scrolls, and a cell template under the content carrying the entry the list fills in
	 * -- the list the recycling-list interaction tests build. Begun play first, since the list lays its cells out in
	 * Start.
	 */
	UUIListView* MakeGlidingList(FDreamDriverRig& InRig, UDreamWidget*& OutHost)
	{
		const FVector2D ListSize(300.0, 300.0);
		OutHost = InRig.MakeWidget(TEXT("ListHost"), nullptr, ListSize);
		UDreamWidget* Content = OutHost != nullptr ? InRig.MakeWidget(TEXT("Content"), OutHost, ListSize) : nullptr;
		UDreamWidget* Cell = Content != nullptr ? InRig.MakeWidget(TEXT("Cell"), Content, FVector2D(ListSize.X, 100.0)) : nullptr;
		if (Cell == nullptr)
		{
			return nullptr;
		}
		Cell->AddComponent<UUIListEntry>();
		UUIListView* List = OutHost->AddComponent<UUIListView>();
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetHorizontal(false);
		List->SetVertical(true);
		List->SetContent(Content);
		List->SetCellTemplate(Cell);
		InRig.PumpFrames(2);
		return List;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRecyclingListGlideYieldsToADragTest,
	"DreamGUI.ListView.ARecyclingListGlidingToAnIndexStopsWhereADragTakesTheContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRecyclingListGlideYieldsToADragTest, "DreamGUI.ListView.ARecyclingListGlidingToAnIndexStopsWhereADragTakesTheContent", "[Pointer][Animated]")

/*
 * The recycling list's animated ScrollToByDataIndex started a tween of its own and forgot it, so it was not the view's
 * glide: a grab while it ran stopped nothing, the tween wrote the content back on its way the very next frame, and the
 * content went on to the row as if the player had never touched it. It is the view's glide now, which counts as
 * scrolling and which a drag stops where it got to.
 *
 * Checked here: a hundred rows, a half-second glide to row sixty, a grab a few frames into it held still for longer
 * than the glide had left, and the content is where the hand stopped it, nowhere near row sixty.
 */
bool FDreamRecyclingListGlideYieldsToADragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Host = nullptr;
	UUIListView* List = MakeGlidingList(Rig, Host);
	if (!TestNotNull(TEXT("The recycling list was made"), List))
	{
		return false;
	}
	List->SetListItems(DreamListsInteraction::MakeItems(100));
	Rig.PumpFrames(1);
	constexpr float RowSixty = 60.0f * 100.0f;

	List->ScrollToByDataIndex(60, /*InEaseAnimation*/true, 0.5f);
	TestTrue(TEXT("The glide to the row counts as scrolling"), List->IsScrolling());
	Rig.PumpFrames(2);
	if (!TestTrue(FString::Printf(TEXT("A couple of frames in, it is still on its way (offset %.1f)"), List->GetScrollOffset().Y),
		List->IsScrolling() && List->GetScrollOffset().Y < RowSixty - 1.0))
	{
		return false;
	}

	// A grab: pressed on the list and moved just past the drag threshold, then held still.
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	TestTrue(TEXT("The grab completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Host))
			.Press(EDreamUIMouseButtonType::Left)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.Perform());
	const float Grabbed = static_cast<float>(List->GetScrollOffset().Y);
	TestFalse(TEXT("Grabbed, the content is no longer gliding"), List->IsScrolling());
	// Longer than the half second the glide had in all.
	Rig.PumpFrames(45);
	TestNearlyEqual(TEXT("Held still, nothing carries the content on to the row"),
		static_cast<float>(List->GetScrollOffset().Y), Grabbed, 0.5f);

	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release(EDreamUIMouseButtonType::Left).Perform());
	Rig.PumpFrames(30);
	TestTrue(FString::Printf(TEXT("Let go, the content stays where the hand left it (offset %.1f)"), List->GetScrollOffset().Y),
		FMath::IsNearlyEqual(static_cast<float>(List->GetScrollOffset().Y), Grabbed, 0.5f)
		&& List->GetScrollOffset().Y < RowSixty - 1.0);
	return true;
}

#endif
