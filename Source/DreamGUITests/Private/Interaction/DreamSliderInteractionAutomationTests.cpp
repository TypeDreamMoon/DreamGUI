// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UISlider.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SLIDER UNDER A REAL POINTER.
 *
 * The parity tests (Runtime/DreamControlParityAutomationTests.cpp and friends) prove each knob reaches
 * the behaviour. These prove what the knobs are for: a press, a drag, a click on the track and a
 * wheel, each delivered through the driver -- a projected pixel, a real trace, the production event
 * system -- and judged only by what the control reports.
 *
 * The reference is Slate's SSlider, which UMG's USlider wraps:
 *  - OnMouseButtonDown fires OnMouseCaptureBegin, then CommitValue(PositionToValue(pointer)). A press
 *    on the track JUMPS the value there; there is no separate jump-to-click switch to consult.
 *  - OnMouseMove, while captured, commits PositionToValue(pointer) again: the value follows the
 *    pointer ABSOLUTELY, and every move that changes it is one OnValueChanged.
 *  - Letting go releases the capture, and OnMouseCaptureLost fires OnMouseCaptureEnd: one per press.
 *  - A locked slider's OnMouseButtonDown returns Unhandled before any of that happens.
 *  - There is no OnMouseWheel override. The wheel is not a slider's to spend.
 *
 * "Along the track" always means along the handle's TRAVEL. PositionToValue maps the pointer against
 * the length the handle's centre can actually reach -- the allotted size less the thumb's
 * indentation -- and UDreamSlider's handle area is exactly that length, so the tests aim at fractions
 * of the handle area rather than of the drawn bar.
 */
namespace DreamSliderInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** One notch toward the user, in the shape the production input actors send a wheel: InputScroll(FVector2D(Axis, Axis)). */
	const FVector2D WheelTowardUser(-1.0, -1.0);
	const FVector2D WheelAwayFromUser(1.0, 1.0);

	/** A slider in the middle of the screen, laid out and ready to be pointed at. */
	UDreamSlider* MakeSlider(FDreamDriverRig& InRig, const FVector2D& InSize)
	{
		UDreamSlider* Slider = InRig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, InSize);
		// Two frames: the first lays the control out, the second lets the behaviour re-place the handle
		// against the handle area the first one resolved.
		InRig.PumpFrames(2);
		return Slider;
	}

	/** Whether the slider came up with the three parts every test here aims at. */
	bool HasParts(const UDreamSlider* InSlider)
	{
		return InSlider != nullptr
			&& InSlider->TrackNode != nullptr
			&& InSlider->HandleNode != nullptr
			&& InSlider->HandleAreaNode != nullptr;
	}

	/** What the slider said while a gesture ran, one probe per event. Bound AFTER setup, so setup is not counted. */
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
	FDreamSliderInteractionDragHalfwayTest,
	"DreamGUI.Slider.DraggingTheHandleHalfwayAlongItsTravelMovesTheValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionDragHalfwayTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	if (!TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), TravelLength), TravelLength > 100.0))
	{
		return false;
	}
	// One pixel's worth of value. A projected pixel round-trips to well under that, so anything larger
	// is the value landing somewhere else, not arithmetic noise.
	const float OnePixel = static_cast<float>(1.0 / TravelLength);
	TestNearlyEqual(TEXT("The slider starts at its minimum"), Slider->GetValue(), 0.0f, OnePixel);

	FSliderLog Log(Slider);
	const double TargetX = Travel->Min.X + 0.5 * TravelLength;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(TargetX - Grip->X, 0.0)));

	TestNearlyEqual(TEXT("The value is halfway"), Slider->GetValue(), 0.5f, OnePixel);
	// The driver carries a drag over three moves on three frames (past the threshold, halfway, there),
	// and SSlider commits on every move that changes the value: at least two of them must have.
	TestTrue(FString::Printf(TEXT("The value changed on at least two frames of the drag (it changed on %d)"), Log.Values->NumFloats()),
		Log.Values->NumFloats() >= 2);
	TestNearlyEqual(TEXT("The last change reported the value the drag ended on"), Log.Values->LastFloat(-1.0f), 0.5f, OnePixel);
	TestEqual(TEXT("The press began one mouse capture"), Log.CaptureBegins->Signals, 1);
	TestEqual(TEXT("Letting go ended it, once"), Log.CaptureEnds->Signals, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionClickTrackTest,
	"DreamGUI.Slider.ClickingTheTrackThreeQuartersAlongJumpsTheValueThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionClickTrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Slider->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	if (!TestTrue(TEXT("The track is on screen"), Track.IsSet())
		|| !TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	if (!TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), TravelLength), TravelLength > 100.0))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / TravelLength);

	FSliderLog Log(Slider);
	// On the track's centre line, three quarters of the way along the handle's travel -- nowhere near
	// the handle, which sits at zero, so the press lands on the bar and not on the thing it would move.
	const FVector2D ClickPixel(Travel->Min.X + 0.75 * TravelLength, Track->GetCenter().Y);
	TestTrue(TEXT("The click completes"), Driver->Sequence().MoveToPixel(ClickPixel).Press().Release().Perform());

	TestNearlyEqual(TEXT("The value jumped to three quarters"), Slider->GetValue(), 0.75f, OnePixel);
	// One press, one PositionToValue, one commit; the release does not move anything.
	TestEqual(TEXT("The jump was reported once"), Log.Values->NumFloats(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionMouseStepTest,
	"DreamGUI.Slider.DraggingWithMouseUsesStepOnLandsOnTheNearestStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionMouseStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	const float Step = 0.25f;
	Slider->SetStepSize(Step);
	Slider->SetMouseUsesStep(true);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;

	FSliderLog Log(Slider);
	const double TargetX = Travel->Min.X + 0.6 * TravelLength;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(TargetX - Grip->X, 0.0)));

	// 0.6 is 2.4 steps from zero, so the nearest step is 0.5 -- whichever origin the rounding is
	// measured from. SSlider::PositionToValue rounds the distance from the CURRENT value in whole
	// steps (RoundHalfFromZero); UUISlider rounds from MinValue. A drag that starts on a step, as this
	// one does at zero, gives both the same grid, so this pins the rule without taking sides on origin.
	TestNearlyEqual(TEXT("The value landed on the nearest step, 0.5"), Slider->GetValue(), 0.5f, 0.0001f);
	TestTrue(TEXT("The drag moved the value at all"), Log.Values->NumFloats() >= 1);
	bool bEveryValueOnAStep = true;
	for (const float Value : Log.Values->Floats)
	{
		const float Steps = Value / Step;
		if (!FMath::IsNearlyEqual(Steps, FMath::RoundToFloat(Steps), 0.001f))
		{
			bEveryValueOnAStep = false;
		}
	}
	TestTrue(TEXT("Every value the drag passed through was a whole step"), bEveryValueOnAStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionDragPastEndTest,
	"DreamGUI.Slider.DraggingPastTheEndOfTheTrackPinsTheValueAtTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionDragPastEndTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}

	FSliderLog Log(Slider);
	// Two hundred and fifty pixels beyond the far end of the travel: well off the slider, still inside
	// the 1280-wide viewport. The pointer keeps driving the slider out there because the drag holds it
	// (Slate's mouse capture; the pointer module's DragWidget here).
	const double PastTheEnd = (Travel->Max.X + 250.0) - Grip->X;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(PastTheEnd, 0.0)));

	TestNearlyEqual(TEXT("The value stopped at the maximum"), Slider->GetValue(), Slider->GetMaxValue(), 0.000001f);
	TestTrue(TEXT("No value reported on the way left the range"),
		Log.Values->AllFloatsWithin(Slider->GetMinValue(), Slider->GetMaxValue(), 0.000001f));
	TestEqual(TEXT("Letting go ended the capture once"), Log.CaptureEnds->Signals, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionLockedTest,
	"DreamGUI.Slider.ALockedSliderIgnoresADragAndSaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionLockedTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	Slider->SetLocked(true);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}

	FSliderLog Log(Slider);
	const double TargetX = Travel->Min.X + 0.5 * (Travel->Max.X - Travel->Min.X);
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(TargetX - Grip->X, 0.0)));

	TestNearlyEqual(TEXT("The value did not move"), Slider->GetValue(), 0.0f, 0.000001f);
	TestEqual(TEXT("No value change was reported"), Log.Values->NumFloats(), 0);
	// SSlider::OnMouseButtonDown asks IsLocked() before it does anything, capture included, so a
	// locked slider never begins a capture and therefore never loses one either.
	TestEqual(TEXT("No mouse capture began -- a locked slider refuses the press outright"), Log.CaptureBegins->Signals, 0);
	TestEqual(TEXT("So none ended"), Log.CaptureEnds->Signals, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionVerticalTest,
	"DreamGUI.Slider.DraggingAVerticalSlidersHandleUpwardRaisesTheValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionVerticalTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(40.0, 400.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	// UMG's Orient_Vertical: SSlider::PositionToValue measures (height - y), so zero is the BOTTOM and
	// the top is the maximum. Of the two vertical directions here, BottomToTop is that one.
	Slider->SetDirection(EUISliderDirectionType::BottomToTop);
	Rig.PumpFrames(2);

	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet())
		|| !TestTrue(TEXT("The handle is on screen"), Grip.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.Y - Travel->Min.Y;
	if (!TestTrue(FString::Printf(TEXT("The travel is long enough to aim at (%.1f pixels)"), TravelLength), TravelLength > 100.0))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / TravelLength);
	// Pixel Y grows DOWNWARD, so the bottom of the travel is its LARGEST pixel Y, and "up" is a
	// negative offset. The handle at zero sits there.
	TestNearlyEqual(TEXT("At zero the handle sits at the bottom of its travel"),
		static_cast<float>(Grip->Y), static_cast<float>(Travel->Max.Y), 1.0f);

	const double TargetY = Travel->Max.Y - 0.5 * TravelLength;
	TestTrue(TEXT("The drag completes"), Handle->DragBy(FVector2D(0.0, TargetY - Grip->Y)));

	TestNearlyEqual(TEXT("Dragging up by half the travel raised the value to one half"), Slider->GetValue(), 0.5f, OnePixel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderInteractionWheelTest,
	"DreamGUI.Slider.TurningTheWheelOverASliderLeavesItsValueAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderInteractionWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamSlider* Slider = MakeSlider(Rig, FVector2D(400.0, 40.0));
	if (!TestTrue(TEXT("The slider came up with a track, a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	// Mid-range, so a wheel that did move the value could move it either way and be seen doing so.
	Slider->SetValue(0.5f);
	Rig.PumpFrames(1);

	FSliderLog Log(Slider);
	FDreamElementRef Track = Rig.Driver()->Find(FDreamBy::Widget(Slider->TrackNode.Get()));
	// SSlider overrides no OnMouseWheel: the wheel is not a slider's, in either direction.
	TestTrue(TEXT("A notch toward the user completes"), Track->ScrollBy(WheelTowardUser));
	TestTrue(TEXT("A notch away from the user completes"), Track->ScrollBy(WheelAwayFromUser));

	TestNearlyEqual(TEXT("The value is where it was"), Slider->GetValue(), 0.5f, 0.000001f);
	TestEqual(TEXT("No value change was reported"), Log.Values->NumFloats(), 0);
	return true;
}

#endif
