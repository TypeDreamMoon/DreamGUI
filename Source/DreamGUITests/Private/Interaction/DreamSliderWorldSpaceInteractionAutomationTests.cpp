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
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * UDreamSlider on a panel in the level, under a finger and the pad.
 *
 * SSlider takes a finger once it has travelled the drag distance and then follows it (OnTouchMoved, Slate/Private/Widgets/
 * Input/SSlider.cpp:352-413), turning the finger's place into a value through its geometry (PositionToValue: the local
 * position along the handle's travel over that travel). In a UWidgetComponent the finger's place is where its pointer index's
 * ray lands on the component's window, so a finger lifted three quarters of the way along the travel AS SEEN leaves the value
 * at three quarters. The pad reaches the slider as a focus stop and, with Accept, locks it to step it (bRequiresControllerLock).
 *
 * As in the slider's other tests, "along the track" means along the handle's travel: the handle area's length.
 */
namespace DreamSliderWorldTestLocal
{
	const FVector2D SliderSize(300.0, 40.0);

	bool HasParts(const UDreamSlider* InSlider)
	{
		return InSlider != nullptr && InSlider->HandleNode != nullptr && InSlider->HandleAreaNode != nullptr;
	}

	/** The pixel InFraction of the way along InTravel, on its centre line. */
	FVector2D AlongTheTravel(const FBox2D& InTravel, double InFraction)
	{
		return FVector2D(InTravel.Min.X + InFraction * (InTravel.Max.X - InTravel.Min.X), InTravel.GetCenter().Y);
	}

	/**
	 * Pumped until the handle is seen centred InFraction of the way along its travel as seen, to within two pixels, for at most
	 * a second -- however the handle gets there.
	 */
	bool WaitForHandleAt(FDreamDriverRig& InRig, const UDreamSlider* InSlider, double InFraction)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		const FDreamDriverRig* Rig = &InRig;
		return InRig.Driver()->Wait(FDreamUntil::Condition([Rig, InSlider, InFraction]()
		{
			const TOptional<FBox2D> Travel = DreamControlsWorldTest::SeenRect(*Rig, InSlider->HandleAreaNode.Get());
			const TOptional<FBox2D> Handle = DreamControlsWorldTest::SeenRect(*Rig, InSlider->HandleNode.Get());
			return Travel.IsSet() && Handle.IsSet() && FMath::Abs(Handle->GetCenter().X - AlongTheTravel(Travel.GetValue(), InFraction).X) <= 2.0;
		}, Timeout), Timeout, FString::Printf(TEXT("the handle seen %.2f of the way along its travel"), InFraction));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderWorldFingerTest,
	"DreamGUI.Slider.OnAWorldPanelAFingerLiftedThreeQuartersAlongTheTravelAsSeenLeavesTheValueAtThreeQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderWorldFingerTest, "DreamGUI.Slider.OnAWorldPanelAFingerLiftedThreeQuartersAlongTheTravelAsSeenLeavesTheValueAtThreeQuarters", "[Touch][World]")

/*
 * A 300-unit slider off the middle of a panel three metres ahead, its travel seen about 640 pixels long. A finger lands a
 * fifth of the way along the travel as seen, crosses the world pointer's drag distance, goes on over two more moves and lifts
 * three quarters of the way along: the value is three quarters, to within two pixels' worth, it was announced, and the handle
 * is seen centred there.
 */
bool FDreamSliderWorldFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderWorldTestLocal;
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
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), Stage.Panel, SliderSize, FVector2D(-20.0, 60.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider came up on the panel with a handle and a handle area"), HasParts(Slider)))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
	const TOptional<FBox2D> Travel = SeenRect(Rig, Slider->HandleAreaNode.Get());
	if (!TestTrue(TEXT("The camera sees the handle's travel"), Travel.IsSet()))
	{
		return false;
	}
	const double TravelLength = Travel->Max.X - Travel->Min.X;
	TestNearlyEqual(TEXT("The travel is seen at its length in panel units"), TravelLength,
		static_cast<double>(Slider->HandleAreaNode->GetWidth()) * PixelsPerUnit(), 1.5);
	const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));

	const FVector2D LandAt = AlongTheTravel(Travel.GetValue(), 0.2);
	const FVector2D LiftAt = AlongTheTravel(Travel.GetValue(), 0.75);
	TestTrue(TEXT("The finger's drag along the travel completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, LandAt)
			.TouchMoveTo(0, LandAt + FVector2D(DreamControlsWorldTest::PastDragThreshold(Stage.Pointer), 0.0))
			.TouchMoveTo(0, (LandAt + LiftAt) * 0.5)
			.TouchMoveTo(0, LiftAt)
			.WaitFrames(1)
			.TouchUp(0)
			.Perform());
	TestNearlyEqual(TEXT("The value is where the finger lifted, three quarters along"), Slider->GetValue(), 0.75f, 2.0f * OnePixel);
	TestTrue(TEXT("...the finger's drag was announced as the value changing"), Values->NumFloats() >= 1);
	TestTrue(TEXT("...and the handle is seen where the finger lifted"), WaitForHandleAt(Rig, Slider, 0.75));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderWorldPadTest,
	"DreamGUI.Slider.OnAWorldPanelThePadReachesTheSliderWithTheRingOnItAsSeenAndAStepMovesTheHandleAQuarterAlong",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSliderWorldPadTest, "DreamGUI.Slider.OnAWorldPanelThePadReachesTheSliderWithTheRingOnItAsSeenAndAStepMovesTheHandleAQuarterAlong", "[Nav][World]")

/*
 * A button left of the slider on the panel, focused. D-pad right takes the focus onto the slider and the ring sits on it where
 * the camera sees it; Accept locks it to the pad, and D-pad right steps it by its navigation interval, a quarter: the value is
 * a quarter and the handle is seen a quarter of the way along its travel.
 */
bool FDreamSliderWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSliderWorldTestLocal;
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
	UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), Stage.Panel, SliderSize, FVector2D(60.0, 60.0));
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), Stage.Panel, FVector2D(80.0, 40.0), FVector2D(-200.0, 60.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The slider and a button left of it came up on the panel"), HasParts(Slider) && Left != nullptr && Left->FaceNode != nullptr))
	{
		return false;
	}
	Slider->SetNavigationChangeInterval(0.25f);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Left->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad took the focus onto the slider"), FocusIsIn(Rig, Slider));
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the slider where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestTrue(TEXT("Accept locked the slider to the pad"), Slider->IsControllerCaptured());
	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestNearlyEqual(TEXT("The step moved the value by the interval"), Slider->GetValue(), 0.25f, 1.0e-3f);
	TestTrue(TEXT("...the focus stayed on the slider"), FocusIsIn(Rig, Slider));
	TestTrue(TEXT("...and the handle is seen a quarter of the way along its travel"), WaitForHandleAt(Rig, Slider, 0.25));
	return true;
}

#endif
