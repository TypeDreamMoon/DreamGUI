// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * A SCALED CANVAS IS STILL POINTED AT IN PIXELS.
 *
 * Every other driver test runs on a canvas that is one unit to the pixel. A game rarely is: a canvas
 * scaled with the screen lays out in the reference resolution's units and draws at whatever the
 * viewport really is, so a widget authored 200 wide is 100 pixels wide on half the resolution. The
 * driver's projection and the raycaster's ray read the same view-projection matrix, so aiming should
 * survive any scale unchanged; these prove it rather than trust it.
 *
 * And the one number that has to change with the scale does: the drag threshold is authored in canvas
 * units and compared in pixels (UDreamScreenSpaceRaycaster::GetScaledDragThresholdSquare), so a move
 * just past it starts a drag and a move just short of it does not, at half scale and at double.
 */
namespace DreamDriverCanvasScaleTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Options for a 1280x720 viewport scaled with the screen from InReference, height-matched. */
	FDreamRigOptions ScaledOptions(const FVector2D& InReference)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = InReference;
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	/** The reference resolution that makes a 720-high viewport come out at InScale. */
	FVector2D ReferenceForScale(double InScale)
	{
		return FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
	}

	/** What one widget was sent, counted by the production component that exists to be told. */
	struct FScaledWidgetLog
	{
		int32 Down = 0;
		int32 Click = 0;
		int32 BeginDrag = 0;

		void Observe(UDreamWidget* InWidget)
		{
			UUIEventTrigger* Trigger = InWidget != nullptr ? InWidget->AddComponent<UUIEventTrigger>() : nullptr;
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData*) { ++Down; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
			Trigger->GetOnPointerBeginDragEvent().AddLambda([this](UDreamPointerEventData*) { ++BeginDrag; });
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverCanvasScaleTwoThirdsTest,
	"DreamGUI.Driver.CanvasScale.ScalingA1080pReferenceOntoA720pViewportScalesTheCanvasByTwoThirds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverCanvasScaleTwoThirdsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverCanvasScaleTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(FVector2D(1920.0, 1080.0)));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamCanvas* Canvas = Rig.RootCanvas();
	TestEqual(TEXT("The root canvas scales with the screen, as the options asked"),
		Canvas->GetScaleMode(), EDreamCanvasScaleMode::ScaleWithScreenSize);
	TestNearlyEqual(TEXT("720 pixels of viewport over a 1080-unit reference is a scale of two thirds"),
		Canvas->GetCanvasScale(), 720.0f / 1080.0f, 1.0e-3f);
	// The layout happens in reference units: the root is as big as the reference, not the viewport.
	TestNearlyEqual(TEXT("The root is laid out 1920 units wide"), Rig.Root()->GetWidth(), 1920.0f, 0.5f);
	TestNearlyEqual(TEXT("And 1080 units high"), Rig.Root()->GetHeight(), 1080.0f, 0.5f);

	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(300.0, 150.0));
	FScaledWidgetLog Log;
	Log.Observe(Target);
	Rig.PumpFrames(1);
	FDreamElementRef TargetElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Target")));
	const TOptional<FBox2D> Rect = TargetElement->GetPixelRect();
	if (!TestTrue(TEXT("The widget has a place on the viewport"), Rect.IsSet()))
	{
		return false;
	}
	// The projection reads the scaled matrix, so a 300x150 widget covers two thirds of that in pixels.
	TestNearlyEqual(TEXT("A 300-unit-wide widget is 200 pixels wide"), Rect->GetSize().X, 200.0, 1.0);
	TestNearlyEqual(TEXT("And a 150-unit-high one is 100 pixels high"), Rect->GetSize().Y, 100.0, 1.0);

	TestTrue(TEXT("Clicking its centre completes"), TargetElement->Click());
	TestEqual(TEXT("The click on a scaled canvas lands on the widget"), Log.Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverCanvasScaleClickTest,
	"DreamGUI.Driver.CanvasScale.AClickOnAWidgetsCentreHitsItAtHalfAndAtDoubleScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverCanvasScaleClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverCanvasScaleTestLocal;
	for (const double Scale : { 0.5, 2.0 })
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(ReferenceForScale(Scale)));
		Rig.BindTest(this);
		if (!TestTrue(FString::Printf(TEXT("The rig came up at scale %.1f"), Scale), Rig.IsUsable()))
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("The canvas scale is %.1f"), Scale), (double)Rig.RootCanvas()->GetCanvasScale(), Scale, 1.0e-3);

		// Off centre, so the answer depends on the scale reaching the position too, not just the size:
		// 150 units right of the middle is 75 pixels at half scale and 300 at double.
		UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(160.0, 80.0), FVector2D(150.0, 60.0));
		UDreamWidget* Decoy = Rig.MakeWidget(TEXT("Decoy"), nullptr, FVector2D(160.0, 80.0), FVector2D(-150.0, -60.0));
		FScaledWidgetLog TargetLog;
		FScaledWidgetLog DecoyLog;
		TargetLog.Observe(Target);
		DecoyLog.Observe(Decoy);
		Rig.PumpFrames(1);

		FDreamElementRef TargetElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Target")));
		const TOptional<FBox2D> Rect = TargetElement->GetPixelRect();
		if (!TestTrue(TEXT("The target has a place on the viewport"), Rect.IsSet()))
		{
			return false;
		}
		TestNearlyEqual(FString::Printf(TEXT("At scale %.1f a 160-unit-wide widget is %.0f pixels wide"), Scale, 160.0 * Scale),
			Rect->GetSize().X, 160.0 * Scale, 1.0);
		TestNearlyEqual(FString::Printf(TEXT("And its centre is %.0f pixels right of the viewport's"), 150.0 * Scale),
			Rect->GetCenter().X - ViewportSize.X * 0.5, 150.0 * Scale, 1.0);

		TestTrue(TEXT("Clicking the target's centre completes"), TargetElement->Click());
		TestEqual(FString::Printf(TEXT("At scale %.1f the click lands on the target"), Scale), TargetLog.Click, 1);
		TestEqual(FString::Printf(TEXT("At scale %.1f nothing lands on the decoy"), Scale), DecoyLog.Down, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverCanvasScaleDragThresholdTest,
	"DreamGUI.Driver.CanvasScale.TheDragThresholdIsInCanvasUnitsSoItScalesWithTheCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverCanvasScaleDragThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverCanvasScaleTestLocal;
	for (const double Scale : { 0.5, 2.0 })
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(ReferenceForScale(Scale)));
		Rig.BindTest(this);
		if (!TestTrue(FString::Printf(TEXT("The rig came up at scale %.1f"), Scale), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Handle = Rig.MakeWidget(TEXT("Handle"), nullptr, FVector2D(200.0, 120.0));
		FScaledWidgetLog Log;
		Log.Observe(Handle);
		Rig.PumpFrames(1);

		UDreamScreenSpaceRaycaster* Raycaster = Rig.Raycaster();
		// In pixels: the authored threshold times the scale. Asserted against the raycaster's own
		// answer first, so the moves below are measured from what the pipeline really compares with.
		const double ThresholdPixels = Raycaster->GetDragThreshold() * Scale;
		if (!TestNearlyEqual(FString::Printf(TEXT("At scale %.1f the threshold is the authored %.1f units times the scale"), Scale, Raycaster->GetDragThreshold()),
			FMath::Sqrt((double)Raycaster->GetScaledDragThresholdSquare()), ThresholdPixels, 1.0e-3))
		{
			return false;
		}

		const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Name(TEXT("Handle")))->GetCentrePixel();
		if (!TestTrue(TEXT("The handle has a centre pixel"), Centre.IsSet()))
		{
			return false;
		}
		// Half a pixel either side of the threshold. At double scale "just short" is already further
		// than the unscaled threshold, and at half scale "just past" is still inside it -- so a
		// threshold compared in raw pixels fails one side or the other at each scale.
		const FVector2D JustShort = Centre.GetValue() + FVector2D(ThresholdPixels - 0.5, 0.0);
		const FVector2D JustPast = Centre.GetValue() + FVector2D(ThresholdPixels + 0.5, 0.0);

		const bool bPerformed = Rig.Driver()->Sequence()
			.MoveToPixel(Centre.GetValue())
			.Press()
			.MoveToPixel(JustShort)
			.Then([this, &Log, Scale](FDreamDriverContext& InContext)
			{
				const UDreamPointerEventData* EventData = InContext.GetPointerEventData(0);
				TestTrue(FString::Printf(TEXT("At scale %.1f a move just short of the threshold is still a press"), Scale),
					EventData != nullptr && !EventData->bIsDragging);
				TestEqual(FString::Printf(TEXT("At scale %.1f nothing began dragging"), Scale), Log.BeginDrag, 0);
			})
			.MoveToPixel(JustPast)
			.Then([this, &Log, Scale](FDreamDriverContext& InContext)
			{
				const UDreamPointerEventData* EventData = InContext.GetPointerEventData(0);
				TestTrue(FString::Printf(TEXT("At scale %.1f a move just past the threshold is a drag"), Scale),
					EventData != nullptr && EventData->bIsDragging);
				TestEqual(FString::Printf(TEXT("At scale %.1f the handle was told its drag began, once"), Scale), Log.BeginDrag, 1);
			})
			.Release()
			.Perform();
		TestTrue(FString::Printf(TEXT("The gesture at scale %.1f completes"), Scale), bPerformed);
		TestEqual(FString::Printf(TEXT("At scale %.1f a press that became a drag is not a click"), Scale), Log.Click, 0);
	}
	return true;
}

#endif
