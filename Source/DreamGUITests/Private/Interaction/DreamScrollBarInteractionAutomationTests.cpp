// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamScrollBar.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollbar.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BAR UNDER A REAL POINTER.
 *
 * A bar with no scroll view behind it is a value control in its own right (see UDreamScrollBar): its
 * value is where the handle sits along its TRAVEL -- the track less the handle's own length -- from
 * 0 to 1, and HandleSize is how much of the track the handle covers.
 *
 * The reference for what a pointer does to it is Slate's SScrollBar, the bar every UMG scroll box
 * drives. UMG's standalone UScrollBar never binds OnUserScrolled (its RebuildWidget leaves the event
 * commented out), which makes a UMG bar on its own inert to the pointer; the behaviour worth copying
 * is SScrollBar's WITH a listener, which is how every interactive bar in UMG is used:
 *  - OnMouseButtonDown ON the thumb remembers where along the thumb it was grabbed, and every move
 *    after puts the thumb back under the pointer at that grab point: the thumb follows the pointer
 *    one pixel for one pixel, and its size is never the pointer's business.
 *  - OnMouseButtonDown on the track OFF the thumb takes the grab point to be the thumb's middle and
 *    moves the thumb there at once (ExecuteOnUserScrolled): the thumb's centre jumps to the pointer,
 *    and the drag carries on from there. It does not page.
 */
namespace DreamScrollBarInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * A standalone vertical bar 400 tall in the middle of the screen: zero at the top (TopToBottom, the
	 * default), a quarter of the track under the handle (HandleSize's default), value zero.
	 */
	UDreamScrollBar* MakeBar(FDreamDriverRig& InRig)
	{
		UDreamScrollBar* Bar = InRig.MakeControl<UDreamScrollBar>(TEXT("Bar"), nullptr, FVector2D(24.0, 400.0));
		InRig.PumpFrames(2);
		return Bar;
	}

	bool HasParts(const UDreamScrollBar* InBar)
	{
		return InBar != nullptr && InBar->TrackNode != nullptr && InBar->HandleNode != nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarInteractionDragHandleTest,
	"DreamGUI.ScrollBar.DraggingTheHandleMovesItWithThePointerAndLeavesItsSizeAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarInteractionDragHandleTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is clicked: the bar's behaviour re-places its
	// handle in OnEnable and Start, and without them it keeps the handle it drew before MakeControl
	// gave the bar its height (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBar* Bar = MakeBar(Rig);
	if (!TestTrue(TEXT("The bar came up with a track and a handle"), HasParts(Bar)))
	{
		return false;
	}
	TestTrue(TEXT("The bar runs top to bottom, zero at the top"), Bar->GetDirection() == EUIScrollbarDirectionType::TopToBottom);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Bar->HandleNode.Get()));
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Bar->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> HandleBefore = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The track is on screen"), Track.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), HandleBefore.IsSet()))
	{
		return false;
	}
	const double TrackLength = Track->Max.Y - Track->Min.Y;
	const double HandleLength = HandleBefore->Max.Y - HandleBefore->Min.Y;
	const double TravelLength = TrackLength - HandleLength;
	if (!TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), TravelLength), TravelLength > 150.0))
	{
		return false;
	}
	const float SizeBefore = Bar->GetHandleSize();

	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Bar->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// Down, which is toward the far end of a TopToBottom bar: the value grows.
	const double DragPixels = 90.0;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(0.0, DragPixels)));

	const TOptional<FBox2D> HandleAfter = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen"), HandleAfter.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The handle followed the pointer down, pixel for pixel"),
		static_cast<float>(HandleAfter->Min.Y - HandleBefore->Min.Y), static_cast<float>(DragPixels), 1.0f);
	TestNearlyEqual(TEXT("The value moved by the distance dragged over the travel"),
		Bar->GetValue(), static_cast<float>(DragPixels / TravelLength), static_cast<float>(1.0 / TravelLength));
	TestNearlyEqual(TEXT("The handle is drawn as long as it was"),
		static_cast<float>(HandleAfter->Max.Y - HandleAfter->Min.Y), static_cast<float>(HandleLength), 1.0f);
	TestNearlyEqual(TEXT("The handle size is not the drag's to change"), Bar->GetHandleSize(), SizeBefore, 0.000001f);
	TestTrue(TEXT("The value change was reported"), Values->NumFloats() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarInteractionClickTrackTest,
	"DreamGUI.ScrollBar.ClickingTheTrackBelowTheHandleJumpsTheHandleCentreToThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarInteractionClickTrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is clicked: the bar's behaviour re-places its
	// handle in OnEnable and Start, and without them it keeps the handle it drew before MakeControl
	// gave the bar its height (see DreamDragInteraction::BeginPlayForUI).
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBar* Bar = MakeBar(Rig);
	if (!TestTrue(TEXT("The bar came up with a track and a handle"), HasParts(Bar)))
	{
		return false;
	}

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Bar->HandleNode.Get()));
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Bar->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> HandleBefore = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The track is on screen"), Track.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), HandleBefore.IsSet()))
	{
		return false;
	}
	const double TrackLength = Track->Max.Y - Track->Min.Y;
	const double HandleLength = HandleBefore->Max.Y - HandleBefore->Min.Y;
	const double TravelLength = TrackLength - HandleLength;
	if (!TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), TravelLength), TravelLength > 150.0))
	{
		return false;
	}

	// Three quarters of the way down the track. The handle covers the top quarter, so this is bare
	// track, well clear of it, and far enough down that its centre can land there without clamping.
	const double ClickY = Track->Min.Y + 0.75 * TrackLength;
	if (!TestTrue(TEXT("The click lands on bare track below the handle"), ClickY > HandleBefore->Max.Y + 1.0))
	{
		return false;
	}
	const FVector2D ClickPixel(Track->GetCenter().X, ClickY);
	TestTrue(TEXT("The click completes"), Driver->Sequence().MoveToPixel(ClickPixel).Press().Release().Perform());

	const TOptional<FBox2D> HandleAfter = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen"), HandleAfter.IsSet()))
	{
		return false;
	}
	// SScrollBar::OnMouseButtonDown off the thumb: DragGrabOffset = half the thumb, then
	// ExecuteOnUserScrolled puts the thumb's start at (pointer - DragGrabOffset). Its centre is where
	// the pointer is.
	TestNearlyEqual(TEXT("The handle's centre jumped to where the track was clicked"),
		static_cast<float>(HandleAfter->GetCenter().Y), static_cast<float>(ClickY), 1.5f);
	const double ExpectedValue = (ClickY - 0.5 * HandleLength - Track->Min.Y) / TravelLength;
	TestNearlyEqual(TEXT("The value is the handle's new place along the travel"),
		Bar->GetValue(), static_cast<float>(ExpectedValue), static_cast<float>(1.5 / TravelLength));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarInteractionRightButtonTest,
	"DreamGUI.ScrollBar.ARightButtonDragMovesTheHandleOnlyOnceTheBarIsToldToAnswerIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarInteractionRightButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}

	UDreamScrollBar* Bar = MakeBar(Rig);
	if (!TestTrue(TEXT("The bar came up with a track and a handle"), HasParts(Bar)))
	{
		return false;
	}
	const int32 LeftButton = 1 << static_cast<int32>(EDreamUIMouseButtonType::Left);
	const int32 RightButton = 1 << static_cast<int32>(EDreamUIMouseButtonType::Right);
	// SScrollBar::OnMouseButtonDown answers EKeys::LeftMouseButton and nothing else.
	TestEqual(TEXT("A bar answers the left button alone by default, as SScrollBar does"),
		Bar->GetAcceptedMouseButtons(), LeftButton);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Bar->HandleNode.Get()));
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Bar->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> HandleBefore = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The track is on screen"), Track.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), HandleBefore.IsSet()))
	{
		return false;
	}
	const double TravelLength = (Track->Max.Y - Track->Min.Y) - (HandleBefore->Max.Y - HandleBefore->Min.Y);
	if (!TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), TravelLength), TravelLength > 150.0))
	{
		return false;
	}

	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Bar->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// Grab the handle with the RIGHT button and pull it down 90 pixels: 10, then 40, then 40, the first
	// move past the drag threshold. The same gesture twice, before and after the bar is widened.
	auto RightDragTheHandle = [&Driver, Bar]() -> bool
	{
		return Driver->Sequence()
			.MoveTo(FDreamBy::Widget(Bar->HandleNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, 10.0))
			.MoveBy(FVector2D(0.0, 40.0))
			.MoveBy(FVector2D(0.0, 40.0))
			.WaitFrames(1)
			.Release(EDreamUIMouseButtonType::Right)
			.Perform();
	};

	TestTrue(TEXT("A right-button drag of the handle completes"), RightDragTheHandle());
	const TOptional<FBox2D> HandleAfterRefusal = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen"), HandleAfterRefusal.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("A bar that answers only the left button leaves its value alone"), Bar->GetValue(), 0.0f, 0.000001f);
	TestEqual(TEXT("and announces nothing"), Values->NumFloats(), 0);
	TestNearlyEqual(TEXT("and its handle stays where it was"),
		static_cast<float>(HandleAfterRefusal->Min.Y - HandleBefore->Min.Y), 0.0f, 0.5f);

	Bar->SetAcceptedMouseButtons(LeftButton | RightButton);
	TestTrue(TEXT("The same drag, with the right button answered too, completes"), RightDragTheHandle());
	const TOptional<FBox2D> HandleAfterDrag = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen after the second drag"), HandleAfterDrag.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Now the value moves by the distance dragged over the travel"),
		Bar->GetValue(), static_cast<float>(90.0 / TravelLength), static_cast<float>(1.0 / TravelLength));
	TestTrue(TEXT("and the change is announced"), Values->NumFloats() >= 1);
	TestNearlyEqual(TEXT("and the handle followed the pointer down"),
		static_cast<float>(HandleAfterDrag->Min.Y - HandleBefore->Min.Y), 90.0f, 1.0f);
	return true;
}

#endif
