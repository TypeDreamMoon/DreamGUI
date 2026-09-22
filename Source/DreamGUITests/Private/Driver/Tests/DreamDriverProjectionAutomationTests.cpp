// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"

/*
 * WHERE A WIDGET IS, IN THE PIXELS THE POINTER IS MEASURED IN.
 *
 * Every driver action begins by turning a widget into a pixel and ends by handing that pixel to the
 * input module, which the raycaster then deprojects back into a ray. If the two halves of that round
 * trip disagree by so much as a flipped Y, every test built on the driver fails in the same baffling
 * way: the gesture is delivered, to the wrong thing or to nothing.
 *
 * So the claim these make is not "the arithmetic is what I wrote down", it is "the pixel comes back".
 * The witness is the REAL UDreamScreenSpaceRaycaster, asked the way the input module asks it, walking
 * the real canvas's real visual list. Nothing is hand-fed a hit.
 *
 * None of this was observable before UDreamCanvas::SetViewportSizeOverride: a game world with no
 * player controller reported a 2x2 viewport, which made the canvas two units wide and put every
 * widget in the same pixel.
 */
namespace DreamDriverProjectionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** One pixel. The round trip goes through a 4x4 inverse; exact equality is not the claim. */
	const double PixelTolerance = 1.0;

	/** What the real raycaster finds when the pointer is at InPixel. Null when it finds nothing. */
	UDreamWidget* RaycastAt(FDreamDriverRig& InRig, const FVector2D& InPixel)
	{
		UDreamPointerEventData* EventData = InRig.EventSystem()->GetPointerEventData(0, true);
		if (EventData == nullptr)
		{
			return nullptr;
		}
		EventData->PointerPosition = FVector(InPixel.X, InPixel.Y, 0.0);

		FVector RayOrigin = FVector::ZeroVector;
		FVector RayDirection = FVector::ForwardVector;
		FVector RayEnd = FVector::ForwardVector;
		TArray<FDreamUIHitResult> Hits;
		InRig.Raycaster()->Raycast(EventData, RayOrigin, RayDirection, RayEnd, Hits);
		return Hits.Num() > 0 ? Hits[0].Widget.Get() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionRoundTripTest,
	"DreamGUI.Driver.Projection.TheCentrePixelOfAWidgetIsWhereARayFindsItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// The substituted viewport is the whole reason any of this has a scale. Without it the canvas is
	// two units across and every pixel below is the same pixel.
	TestEqual(TEXT("The canvas reports the substituted viewport width"), Rig.RootCanvas()->GetViewportSize().X, ViewportSize.X);
	TestEqual(TEXT("The canvas reports the substituted viewport height"), Rig.RootCanvas()->GetViewportSize().Y, ViewportSize.Y);
	TestEqual(TEXT("A constant-pixel-size canvas takes the viewport's width"), Rig.Root()->GetWidth(), 1280.0f);
	TestEqual(TEXT("A constant-pixel-size canvas takes the viewport's height"), Rig.Root()->GetHeight(), 720.0f);

	UDreamWidget* Centred = Rig.MakeWidget(TEXT("Centred"), nullptr, FVector2D(200.0, 100.0), FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("The centred widget was built"), Centred))
	{
		return false;
	}
	Rig.PumpFrames(1);

	const TOptional<FVector2D> CentrePixel = FDreamDriverProjection::WidgetCentrePixel(Centred);
	if (!TestTrue(TEXT("A screen-space widget has a pixel"), CentrePixel.IsSet()))
	{
		return false;
	}
	// The canvas is the viewport, so a widget anchored at the canvas's centre is at the viewport's.
	TestEqual(TEXT("A widget at the canvas centre projects to the viewport centre, horizontally"),
		CentrePixel.GetValue().X, 640.0, PixelTolerance);
	TestEqual(TEXT("A widget at the canvas centre projects to the viewport centre, vertically"),
		CentrePixel.GetValue().Y, 360.0, PixelTolerance);

	// The claim that matters: this pixel, given to the real raycaster, comes back to this widget.
	TestSamePtr(TEXT("A ray cast at the centre pixel finds the widget it came from"),
		RaycastAt(Rig, CentrePixel.GetValue()), Centred);

	const TOptional<FBox2D> PixelRect = FDreamDriverProjection::WidgetToPixelRect(Centred);
	if (!TestTrue(TEXT("A screen-space widget has a pixel rect"), PixelRect.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("The centre pixel is inside the widget's own pixel rect"),
		PixelRect.GetValue().IsInside(CentrePixel.GetValue()));
	TestEqual(TEXT("A 200 unit wide widget is 200 pixels wide on a one-to-one canvas"),
		PixelRect.GetValue().GetSize().X, 200.0, PixelTolerance);
	TestEqual(TEXT("A 100 unit tall widget is 100 pixels tall on a one-to-one canvas"),
		PixelRect.GetValue().GetSize().Y, 100.0, PixelTolerance);

	// A pixel outside it is not it -- otherwise the assertion above would pass for any pixel at all.
	TestNull(TEXT("A ray cast well clear of the widget finds nothing"),
		RaycastAt(Rig, CentrePixel.GetValue() + FVector2D(400.0, 0.0)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionYAxisTest,
	"DreamGUI.Driver.Projection.MovingAWidgetUpTheCanvasMovesItsPixelUpTheViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionYAxisTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Anchored positions are in canvas units with Y UPWARD; pointer pixels have Y DOWNWARD from the
	// top of the viewport, because that is the convention UDreamScreenSpaceRaycaster::GenerateRay
	// undoes on its way in. This test is the whole of that sentence.
	UDreamWidget* Offset = Rig.MakeWidget(TEXT("Offset"), nullptr, FVector2D(200.0, 100.0), FVector2D(300.0, 150.0));
	if (!TestNotNull(TEXT("The offset widget was built"), Offset))
	{
		return false;
	}
	Rig.PumpFrames(1);

	const TOptional<FVector2D> OffsetPixel = FDreamDriverProjection::WidgetCentrePixel(Offset);
	if (!TestTrue(TEXT("The offset widget has a pixel"), OffsetPixel.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("300 canvas units to the right is 300 pixels to the right"),
		OffsetPixel.GetValue().X, 940.0, PixelTolerance);
	TestEqual(TEXT("150 canvas units up is 150 pixels nearer the top of the viewport"),
		OffsetPixel.GetValue().Y, 210.0, PixelTolerance);
	TestSamePtr(TEXT("A ray cast at the offset widget's pixel finds the offset widget"),
		RaycastAt(Rig, OffsetPixel.GetValue()), Offset);

	// And the pixel follows the widget rather than being worked out once.
	Offset->SetAnchoredPosition(FVector2D(-300.0, -150.0));
	Rig.PumpFrames(1);
	const TOptional<FVector2D> MovedPixel = FDreamDriverProjection::WidgetCentrePixel(Offset);
	if (!TestTrue(TEXT("The moved widget still has a pixel"), MovedPixel.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("Moving the widget left moves its pixel left"), MovedPixel.GetValue().X, 340.0, PixelTolerance);
	TestEqual(TEXT("Moving the widget down moves its pixel down"), MovedPixel.GetValue().Y, 510.0, PixelTolerance);
	TestSamePtr(TEXT("A ray cast at the moved pixel still finds it"),
		RaycastAt(Rig, MovedPixel.GetValue()), Offset);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverViewportOverrideTest,
	"DreamGUI.Driver.Projection.ClearingTheViewportOverrideRestoresTheHeadlessFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverViewportOverrideTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamCanvas* Canvas = Rig.RootCanvas();
	TestTrue(TEXT("The rig's canvas has a substituted viewport"), Canvas->HasViewportSizeOverride());

	// The fallback is what a ScreenSpaceOverlay canvas answers with in a game world that has no player
	// controller -- which is every headless world. Asserting it here is what makes the override a
	// change rather than a coincidence, and it is the behaviour nothing else may disturb.
	Canvas->ClearViewportSizeOverride();
	TestFalse(TEXT("Clearing it takes the substitution away"), Canvas->HasViewportSizeOverride());
	TestEqual(TEXT("Without a viewport or a substitute the canvas falls back to two pixels wide"),
		Canvas->GetViewportSize().X, 2);
	TestEqual(TEXT("Without a viewport or a substitute the canvas falls back to two pixels tall"),
		Canvas->GetViewportSize().Y, 2);

	// And it can be put back, which is what a fixture that changes resolution mid-test needs.
	Canvas->SetViewportSizeOverride(FIntPoint(800, 600));
	TestEqual(TEXT("A second substitution takes effect, horizontally"), Canvas->GetViewportSize().X, 800);
	TestEqual(TEXT("A second substitution takes effect, vertically"), Canvas->GetViewportSize().Y, 600);
	TestEqual(TEXT("The root widget follows the substituted width"), Rig.Root()->GetWidth(), 800.0f);
	TestEqual(TEXT("The root widget follows the substituted height"), Rig.Root()->GetHeight(), 600.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionWorldSpaceTest,
	"DreamGUI.Driver.Projection.AWorldSpaceCanvasHasNoPixelToOffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionWorldSpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0), FVector2D::ZeroVector);
	if (!TestNotNull(TEXT("The widget was built"), Target))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestTrue(TEXT("A screen-space widget has a pixel to start with"),
		FDreamDriverProjection::WidgetCentrePixel(Target).IsSet());

	// A world-space panel is seen through the PLAYER's camera, not through the canvas's own virtual
	// one, so where it is on screen is a fact about a viewpoint a headless fixture has not got.
	// Refusing is the honest answer; guessing with the canvas's matrix would put the pointer somewhere
	// plausible and wrong, which is the failure mode that costs an afternoon.
	Rig.RootCanvas()->SetRenderMode(EDreamRenderMode::WorldSpace);
	Rig.PumpFrames(1);
	TestFalse(TEXT("A world-space widget has no pixel"),
		FDreamDriverProjection::WidgetCentrePixel(Target).IsSet());
	TestFalse(TEXT("A world-space widget has no pixel rect either"),
		FDreamDriverProjection::WidgetToPixelRect(Target).IsSet());

	return true;
}

#endif
