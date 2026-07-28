// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/LexCanvasViewFit.h"

/*
 * Fitting the canvas's projection into a zoomed and panned orthographic viewport.
 *
 * These are deliberately matrix-only -- no world, no ULexCanvas, no widget. The canvas resizes its
 * own root widget from a cached viewport size, which in a headless world is a 2x2 fallback, and
 * every property asserted here is scale-invariant enough to pass just as well on a 2-unit canvas
 * with its eye 1.7 units away. Building both matrices by hand is what makes the numbers below mean
 * what they say.
 *
 * Every case uses a NON-SQUARE canvas in a NON-MATCHING viewport with non-zero pan and a
 * non-default zoom, because a square canvas in a square panel makes the two axis scales equal and
 * the offsets zero -- which is exactly the configuration an author docks to when checking their own
 * work, and exactly the one in which a wrong implementation looks right.
 */

namespace LexCanvasViewFitTestLocal
{
	// How the editor builds an orthographic LVT_OrthoYZ view: EditorViewportClient.cpp:1357-1364
	// for the rotation, :1336-1338 and :1451-1456 for the projection.
	FMatrix MakeOrthoWorldToClip(const FVector& InEye, double InOrthoWidth, double InOrthoHeight)
	{
		const FMatrix Swizzle(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const double ZScale = 0.5 / UE_OLD_HALF_WORLD_MAX;
		const double ZOffset = UE_OLD_HALF_WORLD_MAX;
		return FTranslationMatrix(-InEye) * Swizzle * FReversedZOrthoMatrix(InOrthoWidth, InOrthoHeight, ZScale, ZOffset);
	}

	// How ULexCanvas builds its own: LexCanvas.cpp:2170-2199 for the product, :2126-2141 for the
	// projection. FieldOfView is horizontal and pre-halved into radians; the vertical falls out of
	// the aspect multiplier rather than being specified.
	FMatrix MakeCanvasWorldToClip(const FVector& InPlaneOrigin, double InWidth, double InHeight,
		double InFieldOfViewDegrees, double InNear = 1.0, double InFar = 10000.0)
	{
		const double HalfFOV = FMath::DegreesToRadians(InFieldOfViewDegrees) * 0.5;
		const double Distance = (InWidth * 0.5) / FMath::Tan(HalfFOV);
		const FVector Eye = InPlaneOrigin - FVector(Distance, 0.0, 0.0);//the viewer stands on -X
		const FMatrix Swizzle(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		return FTranslationMatrix(-Eye) * Swizzle
			* FReversedZPerspectiveMatrix(HalfFOV, HalfFOV, 1.0f, InWidth / InHeight, InNear, InFar);
	}

	bool ToNDC(const FMatrix& InWorldToClip, const FVector& InPoint, FVector2D& OutNDC)
	{
		const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(InPoint, 1.0));
		if (FMath::Abs(Clip.W) <= UE_KINDA_SMALL_NUMBER)return false;
		OutNDC = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
		return true;
	}

	struct FFixture
	{
		double Width = 1920.0;
		double Height = 1080.0;
		double FieldOfView = 60.0;
		FVector PlaneOrigin = FVector(0.0, 0.0, 0.0);
		// A panel that is neither the canvas aspect nor square, panned off-centre and zoomed.
		FVector OrthoEye = FVector(-5000.0, 260.0, -140.0);
		double OrthoWidth = 1400.0;
		double OrthoHeight = 1260.0;

		double EyeDistance()const { return (Width * 0.5) / FMath::Tan(FMath::DegreesToRadians(FieldOfView) * 0.5); }
		FMatrix Canvas()const { return MakeCanvasWorldToClip(PlaneOrigin, Width, Height, FieldOfView); }
		FMatrix Ortho()const { return MakeOrthoWorldToClip(OrthoEye, OrthoWidth, OrthoHeight); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasViewFitPlaneKeepsPixelsTest,
	"LGUI.Canvas.ViewFit.TheCanvasPlaneKeepsExactlyThePixelsItHadBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasViewFitPlaneKeepsPixelsTest::RunTest(const FString& Parameters)
{
	using namespace LexCanvasViewFitTestLocal;
	// The claim that decides whether this feature can be switched on at all. If in-plane content
	// keeps its pixels, then the resize handles, the guides, the canvas boundary and every
	// alignment judgement made in this viewport are untouched, and the change costs nothing to the
	// work authors spend most of their time on.
	FFixture Fix;
	const FMatrix CanvasClip = Fix.Canvas();
	const FMatrix OrthoClip = Fix.Ortho();
	FMatrix Correction;
	if (!TestTrue(TEXT("A correction exists"), LexCanvasViewFit::BuildClipCorrection(
		CanvasClip, OrthoClip, Fix.PlaneOrigin, FVector::YAxisVector, FVector::ZAxisVector,
		Fix.Width * 0.5, Correction)))
	{
		return false;
	}
	const FMatrix Corrected = CanvasClip * Correction;

	// A grid rather than the three sampled points, so a correction that merely reproduces its own
	// samples cannot pass. Corners included, since that is where an aspect error is largest.
	for (const double U : { -0.5, -0.23, 0.0, 0.37, 0.5 })
	{
		for (const double V : { -0.5, -0.11, 0.0, 0.42, 0.5 })
		{
			const FVector Point = Fix.PlaneOrigin + FVector(0.0, U * Fix.Width, V * Fix.Height);
			FVector2D Expected, Actual;
			if (!TestTrue(TEXT("The reference view sees the point"), ToNDC(OrthoClip, Point, Expected)))continue;
			if (!TestTrue(TEXT("The corrected canvas view sees the point"), ToNDC(Corrected, Point, Actual)))continue;
			TestEqual(*FString::Printf(TEXT("In-plane point (%.2f,%.2f) keeps its horizontal position"), U, V),
				Actual.X, Expected.X, 1e-4);
			TestEqual(*FString::Printf(TEXT("In-plane point (%.2f,%.2f) keeps its vertical position"), U, V),
				Actual.Y, Expected.Y, 1e-4);
		}
	}

	// The two axis scales must differ here. If an implementation collapsed them to one scalar the
	// grid above would still pass whenever the aspects happened to agree, so this pins the
	// configuration itself rather than trusting the fixture to stay awkward.
	TestNotEqual(TEXT("The fixture genuinely exercises two different axis scales"),
		Correction.M[0][0], Correction.M[1][1]);
	TestTrue(TEXT("The fixture genuinely exercises a non-zero offset"),
		FMath::Abs(Correction.M[3][0]) > 1e-3 || FMath::Abs(Correction.M[3][1]) > 1e-3);
	// The offsets belong in the row that multiplies w. In the third row they would be multiplied by
	// z, which under reversed-Z is not w -- an error only depth-bearing content reveals.
	TestEqual(TEXT("Nothing leaks into the depth row"), Correction.M[2][0], 0.0);
	TestEqual(TEXT("The correction leaves w alone"), Correction.M[3][3], 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasViewFitForeshortensTest,
	"LGUI.Canvas.ViewFit.DepthOffThePlaneStillForeshortens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasViewFitForeshortensTest::RunTest(const FString& Parameters)
{
	using namespace LexCanvasViewFitTestLocal;
	// The other half, and the reason for the whole exercise: pinning the plane must not flatten
	// what is off it. Without this a correction that quietly reproduced the orthographic view --
	// "ortho with a fancier zoom" -- would satisfy every assertion in the test above.
	FFixture Fix;
	FMatrix Correction;
	if (!TestTrue(TEXT("A correction exists"), LexCanvasViewFit::BuildClipCorrection(
		Fix.Canvas(), Fix.Ortho(), Fix.PlaneOrigin, FVector::YAxisVector, FVector::ZAxisVector,
		Fix.Width * 0.5, Correction)))
	{
		return false;
	}
	const FMatrix Corrected = Fix.Canvas() * Correction;
	const double D = Fix.EyeDistance();

	// Measured against the pinhole, not merely as an ordering: an offset from the eye's axis scales
	// by D/(D+depth), with +X being depth away from the viewer.
	const FVector InPlane = Fix.PlaneOrigin + FVector(0.0, 420.0, -260.0);
	FVector2D Flat;
	if (!TestTrue(TEXT("The in-plane point is visible"), ToNDC(Corrected, InPlane, Flat)))return false;
	const FVector2D Origin2D(Correction.M[3][0], Correction.M[3][1]);//where the eye's axis lands

	for (const double Depth : { -300.0, -120.0, 260.0, 700.0 })
	{
		FVector2D Seen;
		if (!TestTrue(*FString::Printf(TEXT("The point at depth %.0f is visible"), Depth),
			ToNDC(Corrected, InPlane + FVector(Depth, 0.0, 0.0), Seen)))continue;
		const double Expected = D / (D + Depth);
		TestEqual(*FString::Printf(TEXT("At depth %.0f the horizontal offset scales by D/(D+depth)"), Depth),
			(Seen.X - Origin2D.X) / (Flat.X - Origin2D.X), Expected, 1e-3);
		TestEqual(*FString::Printf(TEXT("At depth %.0f the vertical offset scales by D/(D+depth)"), Depth),
			(Seen.Y - Origin2D.Y) / (Flat.Y - Origin2D.Y), Expected, 1e-3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasViewFitZoomPanTest,
	"LGUI.Canvas.ViewFit.ZoomAndPanKeepTrackingTheOrthographicView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasViewFitZoomPanTest::RunTest(const FString& Parameters)
{
	using namespace LexCanvasViewFitTestLocal;
	// Zoom and pan live in the editor camera that the substitution replaces, so they have to be
	// recovered from the reference view every frame. Asserted against the orthographic view
	// directly rather than against a formula, because a formula would encode the same assumption
	// twice and agree with itself.
	FFixture Fix;
	const FVector Probe = Fix.PlaneOrigin + FVector(0.0, -510.0, 300.0);

	auto AgreesWithOrtho = [&](const FFixture& InFix, const TCHAR* What)
	{
		FMatrix Correction;
		if (!TestTrue(*FString::Printf(TEXT("%s: a correction exists"), What),
			LexCanvasViewFit::BuildClipCorrection(InFix.Canvas(), InFix.Ortho(), InFix.PlaneOrigin,
				FVector::YAxisVector, FVector::ZAxisVector, InFix.Width * 0.5, Correction)))return;
		FVector2D Expected, Actual;
		if (!ToNDC(InFix.Ortho(), Probe, Expected))return;
		if (!ToNDC(InFix.Canvas() * Correction, Probe, Actual))return;
		TestEqual(*FString::Printf(TEXT("%s: horizontal position still matches"), What), Actual.X, Expected.X, 1e-4);
		TestEqual(*FString::Printf(TEXT("%s: vertical position still matches"), What), Actual.Y, Expected.Y, 1e-4);
	};

	AgreesWithOrtho(Fix, TEXT("At rest"));
	{
		FFixture Zoomed = Fix;
		Zoomed.OrthoWidth *= 0.5;
		Zoomed.OrthoHeight *= 0.5;
		AgreesWithOrtho(Zoomed, TEXT("Zoomed in"));
	}
	{
		FFixture Zoomed = Fix;
		Zoomed.OrthoWidth *= 3.25;
		Zoomed.OrthoHeight *= 3.25;
		AgreesWithOrtho(Zoomed, TEXT("Zoomed out"));
	}
	{
		FFixture Panned = Fix;
		Panned.OrthoEye += FVector(0.0, -830.0, 415.0);
		AgreesWithOrtho(Panned, TEXT("Panned"));
	}
	{
		// The panel resized to a different aspect, which is what makes a single shared scale wrong.
		FFixture Resized = Fix;
		Resized.OrthoWidth = 2400.0;
		Resized.OrthoHeight = 700.0;
		AgreesWithOrtho(Resized, TEXT("Panel reshaped"));
	}
	{
		// A canvas whose own aspect is nothing like the panel's.
		FFixture Tall = Fix;
		Tall.Width = 900.0;
		Tall.Height = 1600.0;
		AgreesWithOrtho(Tall, TEXT("Portrait canvas"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasViewFitRefusalTest,
	"LGUI.Canvas.ViewFit.ImpossibleFitsAreRefusedRatherThanApproximated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasViewFitRefusalTest::RunTest(const FString& Parameters)
{
	using namespace LexCanvasViewFitTestLocal;
	FFixture Fix;
	FMatrix Correction;

	// A canvas wide enough to push its own plane past its own far plane. The eye distance grows
	// with width, the far plane does not, and the symptom is a black viewport with nothing logged.
	TestTrue(TEXT("An ordinary canvas is usable"),
		LexCanvasViewFit::IsCanvasViewUsable(Fix.EyeDistance(), 1.0, 10000.0));
	TestFalse(TEXT("A canvas past its own far plane is refused"),
		LexCanvasViewFit::IsCanvasViewUsable(12000.0, 1.0, 10000.0));
	TestFalse(TEXT("A canvas exactly on the far plane is refused"),
		LexCanvasViewFit::IsCanvasViewUsable(10000.0, 1.0, 10000.0));
	TestFalse(TEXT("A canvas inside its own near plane is refused"),
		LexCanvasViewFit::IsCanvasViewUsable(0.5, 1.0, 10000.0));
	TestFalse(TEXT("A canvas exactly on the near plane is refused"),
		LexCanvasViewFit::IsCanvasViewUsable(1.0, 1.0, 10000.0));

	// A degenerate basis has no plane to pin.
	TestFalse(TEXT("A zero sample length is refused"), LexCanvasViewFit::BuildClipCorrection(
		Fix.Canvas(), Fix.Ortho(), Fix.PlaneOrigin, FVector::YAxisVector, FVector::ZAxisVector, 0.0, Correction));
	TestFalse(TEXT("A zero basis vector is refused"), LexCanvasViewFit::BuildClipCorrection(
		Fix.Canvas(), Fix.Ortho(), Fix.PlaneOrigin, FVector::ZeroVector, FVector::ZAxisVector,
		Fix.Width * 0.5, Correction));

	// An eye aimed off the plane normal -- which bOverrideViewRotation can produce. In-plane points
	// then have different w, the plane stops mapping affinely into clip space, and no scale and
	// offset can pin it. Refusing is the whole point: approximating would ship a view that looks
	// right at the centre and drifts toward the edges.
	const FMatrix Swizzle(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	const double HalfFOV = FMath::DegreesToRadians(Fix.FieldOfView) * 0.5;
	const FVector TiltedEye = Fix.PlaneOrigin - FVector(Fix.EyeDistance(), 0.0, 0.0);
	const FMatrix Tilted = FTranslationMatrix(-TiltedEye) * FInverseRotationMatrix(FRotator(18.0, 0.0, 0.0)) * Swizzle
		* FReversedZPerspectiveMatrix(HalfFOV, HalfFOV, 1.0f, Fix.Width / Fix.Height, 1.0, 10000.0);
	TestFalse(TEXT("An eye aimed off the plane normal is refused, not approximated"),
		LexCanvasViewFit::BuildClipCorrection(Tilted, Fix.Ortho(), Fix.PlaneOrigin,
			FVector::YAxisVector, FVector::ZAxisVector, Fix.Width * 0.5, Correction));

	// And an in-plane sample behind the eye, which must be refused rather than divided through --
	// FSceneView::ScreenToPixel deliberately mirrors a negative w rather than rejecting it, so a
	// caller that trusted the sign would draw a plausible, inside-out view.
	const FVector BehindOrigin = Fix.PlaneOrigin - FVector(Fix.EyeDistance() * 2.0, 0.0, 0.0);
	TestFalse(TEXT("A plane behind the canvas eye is refused"), LexCanvasViewFit::BuildClipCorrection(
		Fix.Canvas(), Fix.Ortho(), BehindOrigin, FVector::YAxisVector, FVector::ZAxisVector,
		Fix.Width * 0.5, Correction));
	return true;
}

#endif
