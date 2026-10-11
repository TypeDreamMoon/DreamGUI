// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBar.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BAR DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * The reference is SScrollBar with a listener, the bar every UMG scroll box drives (see DreamScrollBarInteractionAutomation
 * Tests.cpp): a press on the thumb grabs it where it landed and every move after puts the thumb back under the pointer at
 * that grab point, one pixel for one pixel, and a press on the track off the thumb brings the thumb's middle to the pointer.
 * A finger does the same through Slate's touch fallback. All of it runs in the bar's geometry, so on a scaled layer the thumb
 * still follows the pointer pixel for pixel and the value is the distance over the travel AS DRAWN. Disabled, the bar is off
 * the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241): no drag, no jump.
 *
 * The bar is as many pixels long at either scale -- so many more units at half scale -- so every drag below is the same
 * distance in pixels.
 */
namespace DreamScrollBarConfigTestLocal
{
	/** The bar's length on screen whatever the scale. */
	const double BarPixels = 400.0;

	/** A standalone vertical bar BarPixels long at InScale, zero at the top, a quarter of the track under the handle. */
	UDreamScrollBar* MakeBar(FDreamDriverRig& InRig, double InScale, const FVector2D& InPosition)
	{
		UDreamScrollBar* Bar = InRig.MakeControl<UDreamScrollBar>(TEXT("Bar"), nullptr, FVector2D(24.0, BarPixels / InScale), InPosition);
		InRig.PumpFrames(2);
		return Bar != nullptr && Bar->TrackNode != nullptr && Bar->HandleNode != nullptr ? Bar : nullptr;
	}

	struct FBarPixels
	{
		FBox2D Track = FBox2D(ForceInit);
		FBox2D Handle = FBox2D(ForceInit);
		double Travel = 0.0;
	};

	bool MeasureBar(const UDreamScrollBar* InBar, FBarPixels& OutPixels)
	{
		const TOptional<FBox2D> Track = DreamControlsConfigTest::DrawnRect(InBar->TrackNode.Get());
		const TOptional<FBox2D> Handle = DreamControlsConfigTest::DrawnRect(InBar->HandleNode.Get());
		if (!Track.IsSet() || !Handle.IsSet())
		{
			return false;
		}
		OutPixels.Track = Track.GetValue();
		OutPixels.Handle = Handle.GetValue();
		OutPixels.Travel = (Track->Max.Y - Track->Min.Y) - (Handle->Max.Y - Handle->Min.Y);
		return true;
	}

	/** A finger on the handle's centre, past the drag distance on its first move, on InPixels down in all, a rest, a lift. */
	bool FingerDragHandle(FDreamDriverRig& InRig, const FBox2D& InHandle, double InPixels)
	{
		const FVector2D Landed = InHandle.GetCenter();
		const double FirstMove = DreamControlsConfigTest::PastDragThreshold(InRig);
		return InRig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed + FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Landed + FVector2D(0.0, (FirstMove + InPixels) * 0.5))
			.TouchMoveTo(0, Landed + FVector2D(0.0, InPixels))
			.WaitFrames(1)
			.TouchUp(0)
			.Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarDisabledTest,
	"DreamGUI.ScrollBar.ADisabledBarsHandleFollowsNoMouseOrFingerAndAPressOnItsTrackMovesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBarDisabledTest, "DreamGUI.ScrollBar.ADisabledBarsHandleFollowsNoMouseOrFingerAndAPressOnItsTrackMovesNothing", "[Pointer][Touch][Disabled]")

/*
 * A disabled bar at zero. The mouse drags its handle 90 pixels down, then clicks the track near its far end; a finger drags
 * the handle 90 pixels down. The handle never moves, the value stays at zero and nothing is reported. Enabled again, the same
 * mouse drag moves the handle with it.
 */
bool FDreamScrollBarDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is touched: the bar re-places its handle in OnEnable and Start.
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamScrollBar* Bar = MakeBar(Rig, 1.0, FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("The bar came up with a track and a handle"), Bar))
	{
		return false;
	}
	Bar->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FBarPixels Before;
	if (!TestTrue(TEXT("The track and the handle are on screen"), MeasureBar(Bar, Before)))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Bar->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
	FDreamElementRef Handle = Rig.Driver()->Find(FDreamBy::Widget(Bar->HandleNode.Get()));

	TestTrue(TEXT("A mouse drag down the disabled bar's handle completes"), Handle->DragBy(FVector2D(0.0, 90.0)));
	TestTrue(TEXT("A click near the far end of its track completes"),
		ClickAt(Rig, FVector2D(Before.Track.GetCenter().X, Before.Track.Max.Y - 20.0)));
	TestTrue(TEXT("A finger's drag down its handle completes"), FingerDragHandle(Rig, Before.Handle, 90.0));
	FBarPixels After;
	if (TestTrue(TEXT("The handle is still on screen"), MeasureBar(Bar, After)))
	{
		TestNearlyEqual(TEXT("The disabled bar's handle did not move"), After.Handle.Min.Y, Before.Handle.Min.Y, 0.5);
	}
	TestNearlyEqual(TEXT("...its value is still zero"), Bar->GetValue(), 0.0f, 1.0e-4f);
	TestEqual(TEXT("...and nothing was reported"), Values->NumFloats(), 0);

	Bar->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same mouse drag with the bar enabled again completes"), Handle->DragBy(FVector2D(0.0, 90.0)));
	TestTrue(TEXT("Enabled again, the drag moves the value"), Bar->GetValue() > 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarScaledTest,
	"DreamGUI.ScrollBar.OnACanvasScaledToHalfAndToDoubleTheHandleFollowsAMouseOrAFingerPixelForPixelAndTheValueIsTheDrawnTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBarScaledTest, "DreamGUI.ScrollBar.OnACanvasScaledToHalfAndToDoubleTheHandleFollowsAMouseOrAFingerPixelForPixelAndTheValueIsTheDrawnTravel", "[Pointer][Touch][Scaled]")

/*
 * A bar 400 pixels long off the middle, at each scale. The mouse drags its handle 90 pixels down: the handle follows it 90
 * pixels and the value is 90 over the travel as drawn. The bar put back to zero, a finger does the same.
 */
bool FDreamScrollBarScaledTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale)
			|| !TestTrue(At(Scale, TEXT("Its UI has begun play, as a game's has")), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
		{
			return false;
		}
		UDreamScrollBar* Bar = MakeBar(Rig, Scale, FVector2D(-200.0, 0.0));
		FBarPixels Before;
		if (!TestNotNull(*At(Scale, TEXT("The bar came up with a track and a handle")), Bar)
			|| !TestTrue(At(Scale, TEXT("The track and the handle are on screen")), MeasureBar(Bar, Before))
			|| !TestTrue(At(Scale, FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), Before.Travel)), Before.Travel > 150.0))
		{
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("The track is drawn at the canvas's scale")), Before.Track.Max.Y - Before.Track.Min.Y,
			static_cast<double>(Bar->TrackNode->GetHeight()) * Scale, 1.5);
		const double DragPixels = 90.0;
		const float Expected = static_cast<float>(DragPixels / Before.Travel);
		const float OnePixel = static_cast<float>(1.0 / Before.Travel);

		TestTrue(At(Scale, TEXT("The mouse's drag down the handle completes")),
			Rig.Driver()->Find(FDreamBy::Widget(Bar->HandleNode.Get()))->DragBy(FVector2D(0.0, DragPixels)));
		FBarPixels AfterMouse;
		if (TestTrue(At(Scale, TEXT("The handle is still on screen")), MeasureBar(Bar, AfterMouse)))
		{
			TestNearlyEqual(*At(Scale, TEXT("The handle followed the mouse, pixel for pixel")), AfterMouse.Handle.Min.Y - Before.Handle.Min.Y, DragPixels, 1.0);
		}
		TestNearlyEqual(*At(Scale, TEXT("...and the value is the drag over the drawn travel")), Bar->GetValue(), Expected, OnePixel);

		Bar->SetValue(0.0f);
		Rig.PumpFrames(2);
		FBarPixels Reset;
		if (!TestTrue(At(Scale, TEXT("Put back to zero, the handle is on screen")), MeasureBar(Bar, Reset)))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("The finger's drag down the handle completes")), FingerDragHandle(Rig, Reset.Handle, DragPixels));
		FBarPixels AfterFinger;
		if (TestTrue(At(Scale, TEXT("The handle is still on screen")), MeasureBar(Bar, AfterFinger)))
		{
			TestNearlyEqual(*At(Scale, TEXT("The handle followed the finger, pixel for pixel")), AfterFinger.Handle.Min.Y - Reset.Handle.Min.Y, DragPixels, 1.0);
		}
		TestNearlyEqual(*At(Scale, TEXT("...and the value is the drag over the drawn travel")), Bar->GetValue(), Expected, OnePixel);
	}
	return true;
}

#endif
