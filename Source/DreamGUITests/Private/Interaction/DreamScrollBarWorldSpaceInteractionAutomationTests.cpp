// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBar.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BAR ON A PANEL IN THE LEVEL.
 *
 * The reference is SScrollBar with a listener, the bar every UMG scroll box drives (see DreamScrollBarInteractionAutomation
 * Tests.cpp): a press on the thumb grabs it where it landed and every move after puts the thumb back under the pointer at that
 * grab point, and a press on the track off the thumb brings the thumb's middle to the pointer. A finger does the same through
 * Slate's touch fallback. All of it runs in the bar's geometry; in a UWidgetComponent the pointer is where the interaction's
 * ray lands on the component's window, so the thumb follows a mouse or a finger across the panel point for point -- pixel for
 * pixel as the camera sees a face-on panel -- and the value is the distance over the travel.
 */
namespace DreamScrollBarWorldTestLocal
{
	struct FBarPixels
	{
		FBox2D Track = FBox2D(ForceInit);
		FBox2D Handle = FBox2D(ForceInit);
		double Travel = 0.0;
	};

	bool MeasureBar(const FDreamDriverRig& InRig, const UDreamScrollBar* InBar, FBarPixels& OutPixels)
	{
		const TOptional<FBox2D> Track = DreamControlsWorldTest::SeenRect(InRig, InBar->TrackNode.Get());
		const TOptional<FBox2D> Handle = DreamControlsWorldTest::SeenRect(InRig, InBar->HandleNode.Get());
		if (!Track.IsSet() || !Handle.IsSet())
		{
			return false;
		}
		OutPixels.Track = Track.GetValue();
		OutPixels.Handle = Handle.GetValue();
		OutPixels.Travel = (Track->Max.Y - Track->Min.Y) - (Handle->Max.Y - Handle->Min.Y);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarWorldDragTest,
	"DreamGUI.ScrollBar.OnAWorldPanelTheHandleFollowsAMouseOrAFingerAndTheValueIsTheTravelOverTheTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBarWorldDragTest, "DreamGUI.ScrollBar.OnAWorldPanelTheHandleFollowsAMouseOrAFingerAndTheValueIsTheTravelOverTheTrack", "[Pointer][Touch][World]")

/*
 * A standalone vertical bar 180 units long on a panel three metres ahead, zero at the top. The mouse drags its handle 90
 * pixels down: the handle follows it 90 pixels as the camera sees it and the value is 90 over the travel as seen. Put back to
 * zero, a finger does the same.
 */
bool FDreamScrollBarWorldDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is touched: the bar re-places its handle in OnEnable and Start.
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamScrollBar* Bar = Rig.MakeControl<UDreamScrollBar>(TEXT("Bar"), Stage.Panel, FVector2D(24.0, 180.0), FVector2D(-150.0, 0.0));
	Rig.PumpFrames(2);
	FBarPixels Before;
	if (!TestTrue(TEXT("The bar came up on the panel with a track and a handle"), Bar != nullptr && Bar->TrackNode != nullptr && Bar->HandleNode != nullptr)
		|| !TestTrue(TEXT("The camera sees the track and the handle"), MeasureBar(Rig, Bar, Before))
		|| !TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), Before.Travel), Before.Travel > 150.0))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The track is seen at its length in panel units"), Before.Track.Max.Y - Before.Track.Min.Y,
		static_cast<double>(Bar->TrackNode->GetHeight()) * PixelsPerUnit(), 1.5);
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Bar->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
	const double DragPixels = 90.0;
	const float Expected = static_cast<float>(DragPixels / Before.Travel);
	const float OnePixel = static_cast<float>(1.0 / Before.Travel);
	const double FirstMove = DreamControlsWorldTest::PastDragThreshold(Stage.Pointer);

	const FVector2D MouseLanded = Before.Handle.GetCenter();
	TestTrue(TEXT("The mouse's drag down the handle completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(MouseLanded)
			.Press()
			.MoveToPixel(MouseLanded + FVector2D(0.0, FirstMove))
			.MoveToPixel(MouseLanded + FVector2D(0.0, (FirstMove + DragPixels) * 0.5))
			.MoveToPixel(MouseLanded + FVector2D(0.0, DragPixels))
			.WaitFrames(1)
			.Release()
			.Perform());
	FBarPixels AfterMouse;
	if (TestTrue(TEXT("The camera still sees the handle"), MeasureBar(Rig, Bar, AfterMouse)))
	{
		TestNearlyEqual(TEXT("The handle followed the mouse, pixel for pixel as seen"), AfterMouse.Handle.Min.Y - Before.Handle.Min.Y, DragPixels, 1.0);
	}
	TestNearlyEqual(TEXT("...and the value is the drag over the travel"), Bar->GetValue(), Expected, OnePixel);
	TestTrue(TEXT("...announced as the value changing"), Values->NumFloats() >= 1);

	Bar->SetValue(0.0f);
	Rig.PumpFrames(2);
	FBarPixels Reset;
	if (!TestTrue(TEXT("Put back to zero, the camera sees the handle"), MeasureBar(Rig, Bar, Reset)))
	{
		return false;
	}
	const FVector2D FingerLanded = Reset.Handle.GetCenter();
	TestTrue(TEXT("The finger's drag down the handle completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, FingerLanded)
			.TouchMoveTo(0, FingerLanded + FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, FingerLanded + FVector2D(0.0, (FirstMove + DragPixels) * 0.5))
			.TouchMoveTo(0, FingerLanded + FVector2D(0.0, DragPixels))
			.WaitFrames(1)
			.TouchUp(0)
			.Perform());
	FBarPixels AfterFinger;
	if (TestTrue(TEXT("The camera still sees the handle"), MeasureBar(Rig, Bar, AfterFinger)))
	{
		TestNearlyEqual(TEXT("The handle followed the finger, pixel for pixel as seen"), AfterFinger.Handle.Min.Y - Reset.Handle.Min.Y, DragPixels, 1.0);
	}
	TestNearlyEqual(TEXT("...and the value is the drag over the travel"), Bar->GetValue(), Expected, OnePixel);
	return true;
}

#endif
