// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamScrollTypes.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

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

	// Grab the content and pull it UP 150 pixels: 10, then 70, then 70. The first move is past both
	// drag thresholds at once -- the raycaster's 5 canvas units and Slate's 5 pixel drag trigger
	// distance -- so under SScrollBox::OnMouseMove it already scrolls, and so does every move after.
	TestTrue(TEXT("The right-button drag completes"),
		Driver->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -10.0))
			.MoveBy(FVector2D(0.0, -70.0))
			.MoveBy(FVector2D(0.0, -70.0))
			.WaitFrames(1)
			.Release(EDreamUIMouseButtonType::Right)
			.Perform());

	const float Offset = Box->GetScrollOffset();
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
	TestTrue(TEXT("The right-button drag completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -10.0))
			.MoveBy(FVector2D(0.0, -70.0))
			.MoveBy(FVector2D(0.0, -70.0))
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

	TestTrue(TEXT("The drag gets going"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -10.0))
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
	FDreamScrollBoxInteractionBarHandleTest,
	"DreamGUI.ScrollBox.DraggingTheScrollBarHandleScrollsTheContentToTheMatchingPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

#endif
