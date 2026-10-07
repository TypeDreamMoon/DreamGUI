// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SLIDER UNDER A FINGER.
 *
 * SSlider answers a finger differently from the mouse (Slate/Private/Widgets/Input/SSlider.cpp:352-413). A mouse press on the
 * track jumps the value there and begins a capture at once; a finger landing only remembers where it landed and is handled
 * (so it does not fall back to the mouse handlers), and the slider takes the finger -- OnMouseCaptureBegin, then the value at
 * the finger -- only once it has travelled the drag distance; from then on the value follows the finger and the lift ends the
 * capture at the value under it. A finger that lands and lifts without travelling is a tap that changes nothing: a touch
 * screen's slider must not jump under a finger that was on its way to scroll the page.
 *
 * As in the mouse tests, "along the track" means along the handle's travel: the handle area's length.
 */
namespace DreamSliderTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FPlacedSlider
	{
		UDreamSlider* Slider = nullptr;
		FBox2D Travel = FBox2D(ForceInit);
		double TravelLength = 0.0;

		bool IsReady() const { return Slider != nullptr && TravelLength > 100.0; }
	};

	FPlacedSlider PlaceSlider(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FPlacedSlider Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamSlider* Slider = InRig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0));
		// Two frames: the first lays the control out, the second lets the behaviour re-place the handle.
		InRig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("The slider came up with a handle and a handle area"),
			Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
		{
			return Placed;
		}
		const TOptional<FBox2D> Travel = InRig.Driver()->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
		if (!InTest.TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet()))
		{
			return Placed;
		}
		Placed.Slider = Slider;
		Placed.Travel = Travel.GetValue();
		Placed.TravelLength = Placed.Travel.Max.X - Placed.Travel.Min.X;
		InTest.TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), Placed.TravelLength), Placed.IsReady());
		return Placed;
	}

	/** The pixel InFraction of the way along the travel, on its centre line. */
	FVector2D AlongTheTravel(const FPlacedSlider& InPlaced, double InFraction)
	{
		return FVector2D(InPlaced.Travel.Min.X + InFraction * InPlaced.TravelLength, InPlaced.Travel.GetCenter().Y);
	}

	struct FSliderLog
	{
		TStrongObjectPtr<UDreamDragInteractionProbe> Values;
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureBegins;
		TStrongObjectPtr<UDreamDragInteractionProbe> CaptureEnds;

		explicit FSliderLog(UDreamSlider* InSlider)
			: Values(NewObject<UDreamDragInteractionProbe>())
			, CaptureBegins(NewObject<UDreamDragInteractionProbe>())
			, CaptureEnds(NewObject<UDreamDragInteractionProbe>())
		{
			InSlider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
			InSlider->OnMouseCaptureBegin.AddDynamic(CaptureBegins.Get(), &UDreamDragInteractionProbe::RecordSignal);
			InSlider->OnMouseCaptureEnd.AddDynamic(CaptureEnds.Get(), &UDreamDragInteractionProbe::RecordSignal);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderFingerDragTest,
	"DreamGUI.Slider.AFingerDraggedAlongTheTrackCarriesTheValueToWhereItLifts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderFingerDragTest, "DreamGUI.Slider.AFingerDraggedAlongTheTrackCarriesTheValueToWhereItLifts", "[Touch][Animated]")

/*
 * A finger lands a fifth of the way along, travels to three quarters over several frames, and lifts there. Past the drag
 * distance the slider has the finger, so the value follows it and ends at three quarters; the finger's capture began once
 * and ended once (SSlider::OnTouchMoved, OnTouchEnded).
 */
bool FDreamSliderFingerDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedSlider Placed = PlaceSlider(*this, Rig);
	if (!Placed.IsReady())
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / Placed.TravelLength);
	FSliderLog Log(Placed.Slider);
	const double Threshold = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));
	const FVector2D LandAt = AlongTheTravel(Placed, 0.2);
	const FVector2D LiftAt = AlongTheTravel(Placed, 0.75);

	TestTrue(TEXT("The finger's drag along the track completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, LandAt)
			.TouchMoveTo(0, LandAt + FVector2D(Threshold + 4.0, 0.0))
			.TouchMoveTo(0, (LandAt + LiftAt) * 0.5)
			.TouchMoveTo(0, LiftAt)
			.WaitFrames(1)
			.TouchUp(0)
			.Perform());

	TestNearlyEqual(TEXT("The value is where the finger lifted"), Placed.Slider->GetValue(), 0.75f, 2.0f * OnePixel);
	TestTrue(FString::Printf(TEXT("The value followed the finger on the way (%d changes)"), Log.Values->NumFloats()), Log.Values->NumFloats() >= 2);
	TestEqual(TEXT("The finger's capture began once"), Log.CaptureBegins->Signals, 1);
	TestEqual(TEXT("...and ended once, at the lift"), Log.CaptureEnds->Signals, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderFingerTapTest,
	"DreamGUI.Slider.AFingerThatTapsTheTrackWithoutTravellingLeavesTheValueWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderFingerTapTest, "DreamGUI.Slider.AFingerThatTapsTheTrackWithoutTravellingLeavesTheValueWhereItWas", "[Touch][Animated]")

/*
 * A finger lands three quarters of the way along and lifts where it landed. SSlider::OnTouchStarted only notes the landing
 * and OnTouchEnded acts only on a finger the slider has captured, which it captures only past the drag distance
 * (SSlider.cpp:352-413): the value does not move, nothing is said, and no capture began. A mouse click at the same place does
 * jump the value there, which is checked last, so the difference is the finger's and not the place's.
 */
bool FDreamSliderFingerTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedSlider Placed = PlaceSlider(*this, Rig);
	if (!Placed.IsReady())
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / Placed.TravelLength);
	TestNearlyEqual(TEXT("The slider starts at its minimum"), Placed.Slider->GetValue(), 0.0f, OnePixel);
	FSliderLog Log(Placed.Slider);
	const FVector2D TapAt = AlongTheTravel(Placed, 0.75);

	TestTrue(TEXT("A tap on the track completes"), Rig.Driver()->Sequence().TouchDown(0, TapAt).TouchUp(0).Perform());
	TestNearlyEqual(TEXT("A tap that never travelled leaves the value where it was"), Placed.Slider->GetValue(), 0.0f, OnePixel);
	TestEqual(TEXT("...says nothing"), Log.Values->NumFloats(), 0);
	TestEqual(TEXT("...and begins no capture"), Log.CaptureBegins->Signals, 0);

	TestTrue(TEXT("A mouse click at the same place completes"), Rig.Driver()->Sequence().MoveToPixel(TapAt).Press().Release().Perform());
	TestNearlyEqual(TEXT("The mouse's press does jump the value there"), Placed.Slider->GetValue(), 0.75f, 2.0f * OnePixel);
	return true;
}

#endif
