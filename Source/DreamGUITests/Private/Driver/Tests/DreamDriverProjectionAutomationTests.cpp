// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"

#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"

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
 *
 * The same claim for a WORLD-SPACE canvas, whose pixels are a player's rather than the canvas's: there
 * the witness is the real UDreamWorldSpaceRaycaster (the driver's subclass swaps only where its ray
 * comes from), and the eye both halves use is one FDreamDriverVirtualCamera. The camera's own claims --
 * that it sees what a local player with the same view would see -- are pinned against the engine's
 * definitions of field of view and aspect constraint, not against its own arithmetic.
 */
namespace DreamDriverProjectionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** One pixel. The round trip goes through a 4x4 inverse; exact equality is not the claim. */
	const double PixelTolerance = 1.0;

	/** The angle, in degrees, between two directions. */
	double DegreesBetween(const FVector& InA, const FVector& InB)
	{
		const double Cosine = FVector::DotProduct(InA.GetSafeNormal(), InB.GetSafeNormal());
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.0, 1.0)));
	}

	/**
	 * Whether a local player with the engine's configured axis constraint keeps the HORIZONTAL field of
	 * view of a view whose aspect ratio does not match the viewport. MaintainXFOV does, MajorAxisFOV does
	 * on a landscape viewport, MaintainYFOV (the engine's shipped default) keeps the vertical one instead.
	 */
	bool LocalPlayerKeepsHorizontalFieldOfView(const FIntPoint& InViewportSize)
	{
		const ULocalPlayer* Defaults = GetDefault<ULocalPlayer>();
		const EAspectRatioAxisConstraint Constraint = Defaults != nullptr
			? Defaults->AspectRatioAxisConstraint.GetValue()
			: AspectRatio_MaintainYFOV;
		return Constraint == AspectRatio_MaintainXFOV
			|| (Constraint == AspectRatio_MajorAxisFOV && InViewportSize.X > InViewportSize.Y);
	}

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
	// one, so where it is on screen is a fact about a viewpoint -- and without a camera there is no
	// viewpoint to state it from. Refusing is the honest answer; guessing with the canvas's matrix would
	// put the pointer somewhere plausible and wrong, which is the failure mode that costs an afternoon.
	// With a camera (DreamDriverWorld::AttachWorldPointer) the same widget does have a pixel; see
	// WithACameraAWorldSpaceWidgetsPixelIsWhereTheWorldPointerFindsItAgain.
	Rig.RootCanvas()->SetRenderMode(EDreamRenderMode::WorldSpace);
	Rig.PumpFrames(1);
	TestFalse(TEXT("Without a camera a world-space widget has no pixel"),
		FDreamDriverProjection::WidgetCentrePixel(Target).IsSet());
	TestFalse(TEXT("... and no pixel rect either"),
		FDreamDriverProjection::WidgetToPixelRect(Target).IsSet());
	// A null camera IS the camera-less call, not a different one.
	TestFalse(TEXT("A null camera gives a world-space widget no pixel"),
		FDreamDriverProjection::WidgetCentrePixel(Target, nullptr).IsSet());
	TestFalse(TEXT("... and no pixel rect"),
		FDreamDriverProjection::WidgetToPixelRect(Target, nullptr).IsSet());
	// And the driver itself has none to aim with: nothing attached a world pointer to this rig, so every
	// step that resolves a widget to a pixel is asking the camera-less question.
	TestFalse(TEXT("A rig nobody attached a world pointer to has no camera"), Rig.Context().Camera.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionWorldRoundTripTest,
	"DreamGUI.Driver.Projection.WithACameraAWorldSpaceWidgetsPixelIsWhereTheWorldPointerFindsItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionWorldRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The eye at the origin looking down +X, and a panel three metres ahead that is also turned and
	// raised, so the round trip is not only ever tried along the view axis.
	UDreamDriverWorldSpaceRaycaster* Pointer = DreamDriverWorld::AttachWorldPointer(Rig,
		DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize), EDreamWorldPointerSource::Mouse);
	UDreamWidget* Panel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("Panel"),
		FTransform(FRotator(0.0, 20.0, 0.0), FVector(300.0, -40.0, 30.0)), FVector2D(400.0, 300.0));
	if (!TestNotNull(TEXT("A world pointer"), Pointer) || !TestNotNull(TEXT("A world panel"), Panel))
	{
		return false;
	}
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), Panel, FVector2D(100.0, 60.0), FVector2D(80.0, 40.0));
	if (!TestNotNull(TEXT("A widget on the panel"), Target))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FDreamDriverVirtualCamera* Camera = Rig.Context().Camera.Get();
	if (!TestNotNull(TEXT("Attaching the world pointer gave the rig its camera"), Camera))
	{
		return false;
	}
	TestTrue(TEXT("... the very camera the pointer makes its rays from"), Pointer->VirtualCamera.Get() == Camera);

	const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(Target, Camera);
	if (!TestTrue(TEXT("Through the camera, a world-space widget has a pixel"), Pixel.IsSet()))
	{
		return false;
	}

	// The claim: that pixel, given to the REAL world raycaster the way the input module gives it,
	// comes back to this widget -- and the point it lands on projects back to that pixel.
	UDreamPointerEventData* EventData = Rig.EventSystem()->GetPointerEventData(0, true);
	if (!TestNotNull(TEXT("A pointer to aim with"), EventData))
	{
		return false;
	}
	EventData->PointerPosition = FVector(Pixel->X, Pixel->Y, 0.0);
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	FVector RayEnd = FVector::ForwardVector;
	TArray<FDreamUIHitResult> Hits;
	Pointer->Raycast(EventData, RayOrigin, RayDirection, RayEnd, Hits);
	if (!TestTrue(TEXT("The world pointer's ray through the pixel hits something"), Hits.Num() > 0))
	{
		return false;
	}
	TestSamePtr(TEXT("... the widget the pixel was worked out from"), Hits[0].Widget.Get(), Target);
	const TOptional<FVector2D> Back = Camera->Project(Hits[0].Location);
	if (TestTrue(TEXT("The point the ray landed on has a pixel"), Back.IsSet()))
	{
		TestEqual(TEXT("... the pixel it was aimed from, across"), Back->X, Pixel->X, PixelTolerance);
		TestEqual(TEXT("... and down"), Back->Y, Pixel->Y, PixelTolerance);
	}

	// A pixel well clear of it is not it -- otherwise the assertions above would pass for any pixel.
	EventData->PointerPosition = FVector(Pixel->X + 400.0, Pixel->Y, 0.0);
	Hits.Reset();
	Pointer->Raycast(EventData, RayOrigin, RayDirection, RayEnd, Hits);
	TestEqual(TEXT("A ray well clear of the widget finds nothing"), Hits.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionCameraFieldOfViewTest,
	"DreamGUI.Driver.Projection.AVirtualCameraSeesHalfItsFieldOfViewAtTheViewportsEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionCameraFieldOfViewTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverVirtualCamera Camera;
	Camera.View = DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize);
	Camera.ViewportSize = ViewportSize;

	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	if (!TestTrue(TEXT("The middle of the viewport deprojects"), Camera.Deproject(FVector2D(640.0, 360.0), Origin, Direction)))
	{
		return false;
	}
	TestTrue(TEXT("The middle of the viewport looks straight down the view"), Direction.Equals(FVector::ForwardVector, 0.001));

	// The engine's definition of FOV: the full horizontal angle across the viewport. With the view's
	// aspect ratio matching the viewport's, every axis constraint agrees on it.
	TestTrue(TEXT("The left edge deprojects"), Camera.Deproject(FVector2D(0.0, 360.0), Origin, Direction));
	TestEqual(TEXT("The left edge is half the field of view off the axis"), DegreesBetween(Direction, FVector::ForwardVector), 45.0, 0.2);
	TestTrue(TEXT("... to the left, which is -Y for a camera looking down +X"), Direction.Y < 0.0);

	// And the vertical half angle is the one the viewport's shape leaves: tan(v) = tan(45) * 720 / 1280.
	TestTrue(TEXT("The top edge deprojects"), Camera.Deproject(FVector2D(640.0, 0.0), Origin, Direction));
	const double ExpectedVertical = FMath::RadiansToDegrees(FMath::Atan(720.0 / 1280.0));
	TestEqual(TEXT("The top edge is the vertical half angle off the axis"), DegreesBetween(Direction, FVector::ForwardVector), ExpectedVertical, 0.2);
	TestTrue(TEXT("... upward, because pixel Y grows downward"), Direction.Z > 0.0);

	// Project undoes Deproject -- up to the pixel's integer corner, which is where the engine's
	// deprojection starts its ray.
	TestTrue(TEXT("A fractional pixel deprojects"), Camera.Deproject(FVector2D(123.75, 456.25), Origin, Direction));
	const TOptional<FVector2D> Back = Camera.Project(Origin + Direction * 500.0);
	if (TestTrue(TEXT("A point on its ray projects"), Back.IsSet()))
	{
		TestEqual(TEXT("... back to the pixel's integer corner, across"), Back->X, 123.0, 0.01);
		TestEqual(TEXT("... and down"), Back->Y, 456.0, 0.01);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionCameraAxisConstraintTest,
	"DreamGUI.Driver.Projection.AVirtualCameraKeepsTheFieldOfViewTheLocalPlayersAxisConstraintKeeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionCameraAxisConstraintTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	// A view left at FMinimalViewInfo's own 4:3 on a 16:9 viewport -- what a PIE player with a default
	// camera manager is given. Which angle survives is the local player's axis constraint's decision
	// (BaseEngine.ini ships MaintainYFOV), and the camera has to make the same one.
	FDreamDriverVirtualCamera Camera;
	Camera.View.FOV = 90.0f;
	Camera.ViewportSize = ViewportSize;
	const double ViewAspect = Camera.View.AspectRatio;
	const double ViewportAspect = static_cast<double>(ViewportSize.X) / static_cast<double>(ViewportSize.Y);

	double ExpectedHorizontal = 45.0;
	double ExpectedVertical = FMath::RadiansToDegrees(FMath::Atan(1.0 / ViewportAspect));
	if (!LocalPlayerKeepsHorizontalFieldOfView(ViewportSize))
	{
		// The vertical half angle the view's own aspect ratio gives the horizontal FOV, kept; the
		// horizontal one then follows from the viewport's shape.
		const double HalfVertical = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(45.0)) / ViewAspect);
		ExpectedVertical = FMath::RadiansToDegrees(HalfVertical);
		ExpectedHorizontal = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(HalfVertical) * ViewportAspect));
	}

	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	TestTrue(TEXT("The right edge deprojects"), Camera.Deproject(FVector2D(1280.0, 360.0), Origin, Direction));
	TestEqual(TEXT("The right edge is at the horizontal half angle the axis constraint keeps"),
		DegreesBetween(Direction, FVector::ForwardVector), ExpectedHorizontal, 0.2);
	TestTrue(TEXT("The bottom edge deprojects"), Camera.Deproject(FVector2D(640.0, 720.0), Origin, Direction));
	TestEqual(TEXT("The bottom edge is at the vertical half angle the axis constraint keeps"),
		DegreesBetween(Direction, FVector::ForwardVector), ExpectedVertical, 0.2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionCameraOrthographicTest,
	"DreamGUI.Driver.Projection.AnOrthographicVirtualCameraCastsParallelRaysFromOnePlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionCameraOrthographicTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverVirtualCamera Camera;
	Camera.View = DreamDriverWorld::MakeOrthographicView(FVector(-500.0, 0.0, 0.0), FRotator::ZeroRotator, 1280.0f, ViewportSize);
	Camera.ViewportSize = ViewportSize;

	FVector CentreOrigin = FVector::ZeroVector;
	FVector CentreDirection = FVector::ZeroVector;
	FVector LeftOrigin = FVector::ZeroVector;
	FVector LeftDirection = FVector::ZeroVector;
	FVector TopOrigin = FVector::ZeroVector;
	FVector TopDirection = FVector::ZeroVector;
	if (!TestTrue(TEXT("Three pixels deproject"),
		Camera.Deproject(FVector2D(640.0, 360.0), CentreOrigin, CentreDirection)
		&& Camera.Deproject(FVector2D(0.0, 360.0), LeftOrigin, LeftDirection)
		&& Camera.Deproject(FVector2D(640.0, 0.0), TopOrigin, TopDirection)))
	{
		return false;
	}
	// Orthographic: every ray runs down the view, and they start side by side on one plane.
	TestTrue(TEXT("The middle ray runs down the view"), CentreDirection.Equals(FVector::ForwardVector, 0.001));
	TestTrue(TEXT("So does the left edge's -- the rays are parallel"), LeftDirection.Equals(FVector::ForwardVector, 0.001));
	TestTrue(TEXT("... and the top edge's"), TopDirection.Equals(FVector::ForwardVector, 0.001));
	TestEqual(TEXT("They start on one plane across the view"), LeftOrigin.X, CentreOrigin.X, 0.01);
	TestEqual(TEXT("... all three of them"), TopOrigin.X, CentreOrigin.X, 0.01);
	TestTrue(TEXT("The left edge's ray starts to the left"), LeftOrigin.Y < CentreOrigin.Y);
	TestTrue(TEXT("The top edge's ray starts above"), TopOrigin.Z > CentreOrigin.Z);
	// Square pixels: a pixel is as many world units across as it is up, whichever extent the axis
	// constraint made OrthoWidth describe.
	const double UnitsPerPixelAcross = (CentreOrigin.Y - LeftOrigin.Y) / 640.0;
	const double UnitsPerPixelUp = (TopOrigin.Z - CentreOrigin.Z) / 360.0;
	TestEqual(TEXT("A pixel is as wide as it is tall"), UnitsPerPixelAcross, UnitsPerPixelUp, 0.01);

	// And a point projects to the pixel whose ray passes through it.
	const FVector Somewhere = CentreOrigin + FVector(800.0, 200.0 * UnitsPerPixelAcross, -100.0 * UnitsPerPixelUp);
	const TOptional<FVector2D> Pixel = Camera.Project(Somewhere);
	if (TestTrue(TEXT("A point in front of the camera projects"), Pixel.IsSet()))
	{
		TestEqual(TEXT("... to the pixel its offset across the view says, across"), Pixel->X, 840.0, 0.01);
		TestEqual(TEXT("... and down"), Pixel->Y, 460.0, 0.01);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverProjectionCameraBehindEyeTest,
	"DreamGUI.Driver.Projection.APointAtOrBehindAVirtualCamerasEyeHasNoPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverProjectionCameraBehindEyeTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverProjectionTestLocal;
	FDreamDriverVirtualCamera Camera;
	Camera.View = DreamDriverWorld::MakeView(FVector(100.0, 0.0, 0.0), FRotator::ZeroRotator, 90.0f, ViewportSize);
	Camera.ViewportSize = ViewportSize;

	// A point behind the eye has only a mirrored pixel, on the far side of the middle, and a ray from
	// that pixel runs away from it. No answer is the right answer.
	TestFalse(TEXT("A point behind the eye has no pixel"), Camera.Project(FVector(-100.0, 50.0, 0.0)).IsSet());
	TestFalse(TEXT("The eye itself has no pixel"), Camera.Project(FVector(100.0, 0.0, 0.0)).IsSet());
	const TOptional<FVector2D> Ahead = Camera.Project(FVector(400.0, 0.0, 0.0));
	if (TestTrue(TEXT("A point ahead on the axis has one"), Ahead.IsSet()))
	{
		TestEqual(TEXT("... in the middle of the viewport, across"), Ahead->X, 640.0, 0.01);
		TestEqual(TEXT("... and down"), Ahead->Y, 360.0, 0.01);
	}

	// A viewport with no area is refused both ways, as ULocalPlayer::GetProjectionData refuses it.
	FDreamDriverVirtualCamera Empty = Camera;
	Empty.ViewportSize = FIntPoint(0, 720);
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	TestFalse(TEXT("A viewport with no width deprojects nothing"), Empty.Deproject(FVector2D(0.0, 0.0), Origin, Direction));
	TestFalse(TEXT("... and projects nothing"), Empty.Project(FVector(400.0, 0.0, 0.0)).IsSet());
	return true;
}

#endif
