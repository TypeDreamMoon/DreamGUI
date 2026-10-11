// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamSlider.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamSlider under a finger and the pad when disabled, and on a canvas scaled to half and to double.
 *
 * SSlider takes a finger once it has travelled the drag distance and then follows it (OnTouchMoved, Slate/Private/Widgets/
 * Input/SSlider.cpp:352-413), turning the finger's place into a value through its geometry (PositionToValue: the local
 * position along the handle's travel over that travel), so on a scaled layer a finger lifted three quarters of the way along
 * the travel AS DRAWN leaves the value at three quarters. The pad reaches it as a focus stop and, with Accept, locks it to
 * step it (bRequiresControllerLock). Disabled, it is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/
 * HittestGrid.cpp:241) and passed over by navigation (FindFocusableWidget, HittestGrid.cpp:471).
 *
 * As in the slider's other tests, "along the track" means along the handle's travel: the handle area's length.
 */
namespace DreamSliderConfigTestLocal
{
	const FVector2D SliderSize(400.0, 40.0);

	/** The pixel InFraction of the way along InTravel, on its centre line. */
	FVector2D AlongTheTravel(const FBox2D& InTravel, double InFraction)
	{
		return FVector2D(InTravel.Min.X + InFraction * (InTravel.Max.X - InTravel.Min.X), InTravel.GetCenter().Y);
	}

	bool HasParts(const UDreamSlider* InSlider)
	{
		return InSlider != nullptr && InSlider->HandleNode != nullptr && InSlider->HandleAreaNode != nullptr;
	}

	/**
	 * Pumped until the handle is drawn centred InFraction of the way along its travel as drawn, to within two pixels, for at
	 * most a second -- however the handle gets there.
	 */
	bool WaitForHandleAt(FDreamDriverRig& InRig, const UDreamSlider* InSlider, double InFraction)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InSlider, InFraction]()
		{
			const TOptional<FBox2D> Travel = DreamControlsConfigTest::DrawnRect(InSlider->HandleAreaNode.Get());
			const TOptional<FBox2D> Handle = DreamControlsConfigTest::DrawnRect(InSlider->HandleNode.Get());
			return Travel.IsSet() && Handle.IsSet() && FMath::Abs(Handle->GetCenter().X - AlongTheTravel(Travel.GetValue(), InFraction).X) <= 2.0;
		}, Timeout), Timeout, FString::Printf(TEXT("the handle drawn %.2f of the way along its travel"), InFraction));
	}

	/**
	 * A finger lands InFrom of the way along the travel, crosses the drag distance, goes on to InTo over two more moves, rests a
	 * frame and lifts.
	 */
	bool FingerAlong(FDreamDriverRig& InRig, const FBox2D& InTravel, double InFrom, double InTo)
	{
		const FVector2D LandAt = AlongTheTravel(InTravel, InFrom);
		const FVector2D LiftAt = AlongTheTravel(InTravel, InTo);
		const double Direction = InTo >= InFrom ? 1.0 : -1.0;
		return InRig.Driver()->Sequence()
			.TouchDown(0, LandAt)
			.TouchMoveTo(0, LandAt + FVector2D(Direction * DreamControlsConfigTest::PastDragThreshold(InRig), 0.0))
			.TouchMoveTo(0, (LandAt + LiftAt) * 0.5)
			.TouchMoveTo(0, LiftAt)
			.WaitFrames(1)
			.TouchUp(0)
			.Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderDisabledFingerTest,
	"DreamGUI.Slider.AFingerDraggedAlongADisabledSliderLeavesTheValueWhereItWasUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderDisabledFingerTest, "DreamGUI.Slider.AFingerDraggedAlongADisabledSliderLeavesTheValueWhereItWasUntilItIsEnabledAgain", "[Touch][Disabled]")

/*
 * A disabled slider at a quarter: a finger landing a fifth of the way along and dragged to three quarters reaches nothing --
 * the value stays, no change is reported and no capture begins. Enabled again, the same drag carries the value to three
 * quarters.
 */
bool FDreamSliderDisabledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, SliderSize);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider came up with a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	Slider->SetValue(0.25f);
	Slider->SetIsEnabled(false);
	Rig.PumpFrames(1);
	const TOptional<FBox2D> Travel = DrawnRect(Slider->HandleAreaNode.Get());
	if (!TestTrue(TEXT("The handle's travel is on screen"), Travel.IsSet()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	TStrongObjectPtr<UDreamDragInteractionProbe> CaptureBegins(NewObject<UDreamDragInteractionProbe>());
	Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
	Slider->OnMouseCaptureBegin.AddDynamic(CaptureBegins.Get(), &UDreamDragInteractionProbe::RecordSignal);

	TestTrue(TEXT("The finger's drag along the disabled slider completes"), FingerAlong(Rig, Travel.GetValue(), 0.2, 0.75));
	TestNearlyEqual(TEXT("The value is where it was"), Slider->GetValue(), 0.25f, 1.0e-4f);
	TestEqual(TEXT("No change was reported"), Values->NumFloats(), 0);
	TestEqual(TEXT("...and no capture began"), CaptureBegins->Signals, 0);

	Slider->SetIsEnabled(true);
	Rig.PumpFrames(1);
	const float OnePixel = static_cast<float>(1.0 / FMath::Max(Travel->Max.X - Travel->Min.X, 1.0));
	TestTrue(TEXT("The same drag with the slider enabled again completes"), FingerAlong(Rig, Travel.GetValue(), 0.2, 0.75));
	TestNearlyEqual(TEXT("Enabled again, the drag carries the value to where the finger lifted"), Slider->GetValue(), 0.75f, 2.0f * OnePixel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderDisabledPadTest,
	"DreamGUI.Slider.TheDPadPassesOverADisabledSliderAndOnceEnabledStopsOnItSoAcceptCanLockAndStepIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderDisabledPadTest, "DreamGUI.Slider.TheDPadPassesOverADisabledSliderAndOnceEnabledStopsOnItSoAcceptCanLockAndStepIt", "[Nav][Disabled]")

/*
 * A button left of the slider and one right of it, the slider disabled and the focus on the left button. D-pad right passes
 * over the slider to the right-hand button, and Accept there presses the button and locks nothing. Enabled again, D-pad
 * right from the left button stops on the slider, Accept locks it, and D-pad right steps it by its navigation interval.
 */
bool FDreamSliderDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, SliderSize);
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, FVector2D(120.0, 40.0), FVector2D(-330.0, 0.0));
	UDreamButton* Right = Rig.MakeControl<UDreamButton>(TEXT("Right"), nullptr, FVector2D(120.0, 40.0), FVector2D(330.0, 0.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider and a button either side of it came up"), HasParts(Slider) && Left != nullptr && Right != nullptr
		&& Left->FaceNode != nullptr && Right->FaceNode != nullptr))
	{
		return false;
	}
	Slider->SetNavigationChangeInterval(0.25f);
	TStrongObjectPtr<UDreamPressInteractionListener> RightClicks(NewObject<UDreamPressInteractionListener>());
	Right->OnClicked.AddDynamic(RightClicks.Get(), &UDreamPressInteractionListener::HandleClicked);
	Slider->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the left button"), PutFocusOn(Rig, Left->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("D-pad right passes over the disabled slider to the right-hand button"), FocusIsIn(Rig, Right));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept pressed the right-hand button"), RightClicks->ClickedCount, 1);
	TestFalse(TEXT("...and the slider is not locked to the pad"), Slider->IsControllerCaptured());

	Slider->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The focus goes back onto the left button"), PutFocusOn(Rig, Left->FaceNode.Get()));
	TestTrue(TEXT("D-pad right completes once the slider is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("Enabled again, the slider is the next stop"), FocusIsIn(Rig, Slider));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("...and locks the slider to the pad"), Slider->IsControllerCaptured());
	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestNearlyEqual(TEXT("...and steps the value by the navigation interval"), Slider->GetValue(), 0.25f, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderScaledFingerTest,
	"DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleAFingerLiftedThreeQuartersAlongTheDrawnTravelLeavesTheValueAtThreeQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderScaledFingerTest, "DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleAFingerLiftedThreeQuartersAlongTheDrawnTravelLeavesTheValueAtThreeQuarters", "[Touch][Scaled]")

/*
 * A 400-unit slider is 200 pixels long at half scale and 800 at double. A finger lands a fifth of the way along its travel as
 * drawn, crosses the drag distance -- in canvas units, so that many more pixels at double -- and lifts three quarters of the
 * way along: the value is three quarters at both scales, to within two pixels' worth, and the handle is drawn centred there.
 */
bool FDreamSliderScaledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, SliderSize, FVector2D(-50.0, 90.0));
		Rig.PumpFrames(2);
		if (!TestTrue(At(Scale, TEXT("The slider came up with a handle and a handle area")), HasParts(Slider)))
		{
			return false;
		}
		const TOptional<FBox2D> Travel = DrawnRect(Slider->HandleAreaNode.Get());
		if (!TestTrue(At(Scale, TEXT("The handle's travel is on screen")), Travel.IsSet()))
		{
			return false;
		}
		const double TravelLength = Travel->Max.X - Travel->Min.X;
		TestNearlyEqual(*At(Scale, TEXT("The travel is drawn at the canvas's scale")), TravelLength,
			static_cast<double>(Slider->HandleAreaNode->GetWidth()) * Scale, 1.5);
		const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));

		TestTrue(At(Scale, TEXT("The finger's drag along the travel completes")), FingerAlong(Rig, Travel.GetValue(), 0.2, 0.75));
		TestNearlyEqual(*At(Scale, TEXT("The value is where the finger lifted, three quarters along")), Slider->GetValue(), 0.75f, 2.0f * OnePixel);
		TestTrue(At(Scale, TEXT("...and the handle is drawn where the finger lifted")), WaitForHandleAt(Rig, Slider, 0.75));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderScaledPadTest,
	"DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleThePadReachesTheSliderWithTheRingOnItAndAStepMovesTheHandleAlongTheDrawnTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderScaledPadTest, "DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleThePadReachesTheSliderWithTheRingOnItAndAStepMovesTheHandleAlongTheDrawnTravel", "[Nav][Scaled]")

/*
 * A button left of the slider, focused. D-pad right takes the focus onto the slider and the ring sits on it where it is
 * drawn; Accept locks it to the pad, and D-pad right steps it by its navigation interval, a quarter: the value is a quarter
 * and the handle is drawn a quarter of the way along its travel as drawn.
 */
bool FDreamSliderScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, SliderSize, FVector2D(60.0, 90.0));
		UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, FVector2D(80.0, 40.0), FVector2D(-220.0, 90.0));
		Rig.PumpFrames(2);
		if (!TestTrue(At(Scale, TEXT("The slider and a button left of it came up")), HasParts(Slider) && Left != nullptr && Left->FaceNode != nullptr))
		{
			return false;
		}
		Slider->SetNavigationChangeInterval(0.25f);
		Rig.PumpFrames(1);
		if (!TestTrue(At(Scale, TEXT("The focus goes onto the button")), PutFocusOn(Rig, Left->FaceNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestTrue(At(Scale, TEXT("The D-pad took the focus onto the slider")), FocusIsIn(Rig, Slider));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the slider where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestTrue(At(Scale, TEXT("Accept locked the slider to the pad")), Slider->IsControllerCaptured());
		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestNearlyEqual(*At(Scale, TEXT("The step moved the value by the interval")), Slider->GetValue(), 0.25f, 1.0e-3f);
		TestTrue(At(Scale, TEXT("...and the handle is drawn a quarter of the way along the drawn travel")), WaitForHandleAt(Rig, Slider, 0.25));
	}
	return true;
}

#endif
