// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"

// CalculateCanvasSizeAndScale is the canvas-scaler rule, shared by the runtime (which applies it to
// the root widget on every viewport change) and by the prefab designer's Screen Size picker (which
// previews a device resolution that has no viewport behind it). Drift between those two is exactly
// the "the designer lied to me" failure, so the arithmetic is pinned here against values worked out
// by hand rather than read back from the implementation.
namespace DreamCanvasScalerTestLocal
{
	struct FScopedCanvas
	{
		UWorld* World = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		FScopedCanvas()
		{
			World = UWorld::CreateWorld(EWorldType::None, false);
			Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("CanvasRoot"));
			Canvas = Root->AddComponent<UDreamCanvas>();
		}
		~FScopedCanvas()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
			}
		}
	};

	const float Tolerance = 0.01f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerConstantPixelSizeTest,
	"DreamGUI.Canvas.Scaler.ConstantPixelSizeMatchesViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerConstantPixelSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ConstantPixelSize);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));

	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1136, 640), CanvasSize, Scale);

	// One unit is one pixel: the canvas simply is the viewport, whatever the reference says.
	TestEqual(TEXT("Constant pixel size canvas width equals the viewport"), (float)CanvasSize.X, 1136.0f, Tolerance);
	TestEqual(TEXT("Constant pixel size canvas height equals the viewport"), (float)CanvasSize.Y, 640.0f, Tolerance);
	TestEqual(TEXT("Constant pixel size reports unit scale"), Scale, 1.0f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerMatchWidthTest,
	"DreamGUI.Canvas.Scaler.MatchWidthKeepsReferenceWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerMatchWidthTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));
	Fixture.Canvas->SetMatchFromWidthToHeight(0.0f);//match width

	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1920, 1080), CanvasSize, Scale);

	// Match=0 pins the width to the reference and lets the height follow the viewport aspect:
	// width 1280, height 1280 * 1080/1920 = 720, scale 1920/1280 = 1.5.
	TestEqual(TEXT("Matching width pins the canvas width to the reference"), (float)CanvasSize.X, 1280.0f, Tolerance);
	TestEqual(TEXT("Matching width derives the height from the viewport aspect"), (float)CanvasSize.Y, 720.0f, Tolerance);
	TestEqual(TEXT("Matching width reports the width ratio as scale"), Scale, 1.5f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerMatchHeightTest,
	"DreamGUI.Canvas.Scaler.MatchHeightKeepsReferenceHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerMatchHeightTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));
	Fixture.Canvas->SetMatchFromWidthToHeight(1.0f);//match height, the class default

	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(2560, 1080), CanvasSize, Scale);

	// Match=1 pins the height to the reference: height 720, width 720 * 2560/1080 = 1706.67,
	// scale 1080/720 = 1.5. This is the ultrawide case a 16:9 reference has to stretch for.
	TestEqual(TEXT("Matching height pins the canvas height to the reference"), (float)CanvasSize.Y, 720.0f, Tolerance);
	TestEqual(TEXT("Matching height widens the canvas to the viewport aspect"), (float)CanvasSize.X, 1706.67f, 0.1f);
	TestEqual(TEXT("Matching height reports the height ratio as scale"), Scale, 1.5f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerMatchBlendTest,
	"DreamGUI.Canvas.Scaler.MatchBlendsLinearlyBetweenWidthAndHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerMatchBlendTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));
	Fixture.Canvas->SetMatchFromWidthToHeight(0.5f);

	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1000, 1000), CanvasSize, Scale);

	// Width  = Lerp(1280, 720 * 1000/1000, 0.5)  = Lerp(1280, 720, 0.5) = 1000
	// Height = Lerp(1280 * 1000/1000, 720, 0.5)  = Lerp(1280, 720, 0.5) = 1000
	// Scale  = Lerp(1000/1280, 1000/720, 0.5)    = Lerp(0.78125, 1.38889, 0.5) = 1.08507
	// A LINEAR lerp of the two ratios -- not the log2 blend Unity uses for the same control.
	TestEqual(TEXT("Blended match interpolates the canvas width"), (float)CanvasSize.X, 1000.0f, Tolerance);
	TestEqual(TEXT("Blended match interpolates the canvas height"), (float)CanvasSize.Y, 1000.0f, Tolerance);
	TestEqual(TEXT("Blended match lerps the two ratios linearly"), Scale, 1.08507f, 0.001f);

	// Worth pinning explicitly: at a blended match the REPORTED scale is not the scale the screen
	// actually shows. The canvas fills the viewport, so the true on-screen ratio here is 1.0 while
	// the canvas reports 1.085. Anything reading GetCanvasScale as "pixels per unit" inherits that.
	const float TrueOnScreenScale = 1000.0f / (float)CanvasSize.X;
	TestEqual(TEXT("The true on-screen scale is the viewport over the canvas"), TrueOnScreenScale, 1.0f, Tolerance);
	TestTrue(TEXT("...and the reported scale differs from it at a blended match"),
		!FMath::IsNearlyEqual(Scale, TrueOnScreenScale, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerExpandShrinkTest,
	"DreamGUI.Canvas.Scaler.ExpandAndShrinkPickOppositeAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerExpandShrinkTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));

	// Viewport 2560x1080 is wider than the 16:9 reference, so the two modes disagree about which
	// axis to honour: Expand keeps the reference WIDTH (canvas grows past the reference vertically
	// once divided by the wider aspect), Shrink keeps the reference HEIGHT.
	FVector2D ExpandSize, ShrinkSize;
	float ExpandScale = -1.0f, ShrinkScale = -1.0f;
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::Expand);
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(2560, 1080), ExpandSize, ExpandScale);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::Shrink);
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(2560, 1080), ShrinkSize, ShrinkScale);

	// Expand: width 1280, height 1280 / (2560/1080) = 540, scale 2560/1280 = 2.0
	TestEqual(TEXT("Expand keeps the reference width"), (float)ExpandSize.X, 1280.0f, Tolerance);
	TestEqual(TEXT("Expand derives a shorter canvas"), (float)ExpandSize.Y, 540.0f, Tolerance);
	TestEqual(TEXT("Expand reports the larger ratio"), ExpandScale, 2.0f, Tolerance);
	// Shrink: height 720, width 720 * (2560/1080) = 1706.67, scale 1080/720 = 1.5
	TestEqual(TEXT("Shrink keeps the reference height"), (float)ShrinkSize.Y, 720.0f, Tolerance);
	TestEqual(TEXT("Shrink derives a wider canvas"), (float)ShrinkSize.X, 1706.67f, 0.1f);
	TestEqual(TEXT("Shrink reports the smaller ratio"), ShrinkScale, 1.5f, Tolerance);
	// The compact identity the two modes obey: scale is max/min of the two axis ratios.
	TestTrue(TEXT("Expand takes the larger axis ratio, Shrink the smaller"), ExpandScale > ShrinkScale);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerAspectPreservedTest,
	"DreamGUI.Canvas.Scaler.EveryModePreservesViewportAspect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerAspectPreservedTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));

	// The canvas always fills the viewport, so whatever the rule does to the size it must not
	// change the aspect -- a designer previewing a device must see that device's shape.
	const FIntPoint Viewport(1136, 640);
	const float ViewportAspect = 1136.0f / 640.0f;
	const EDreamCanvasScreenMatchMode MatchModes[] = {
		EDreamCanvasScreenMatchMode::MatchWidthOrHeight,
		EDreamCanvasScreenMatchMode::Expand,
		EDreamCanvasScreenMatchMode::Shrink,
	};
	for (EDreamCanvasScreenMatchMode MatchMode : MatchModes)
	{
		Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
		Fixture.Canvas->SetScreenMatchMode(MatchMode);
		FVector2D CanvasSize;
		float Scale = -1.0f;
		Fixture.Canvas->CalculateCanvasSizeAndScale(Viewport, CanvasSize, Scale);
		TestEqual(FString::Printf(TEXT("Match mode %d preserves the viewport aspect"), (int32)MatchMode),
			(float)(CanvasSize.X / CanvasSize.Y), ViewportAspect, 0.001f);
		TestTrue(FString::Printf(TEXT("Match mode %d produces a positive canvas"), (int32)MatchMode),
			CanvasSize.X > 0.0f && CanvasSize.Y > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerDegenerateViewportTest,
	"DreamGUI.Canvas.Scaler.DegenerateViewportFallsBackToUnitScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerDegenerateViewportTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);

	// A zero axis would divide by zero inside every ScaleWithScreenSize branch. The designer can
	// reach this through a stored size on a freshly created asset, so it must return, not produce
	// infinities that then get rounded into the widget's size.
	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(0, 1080), CanvasSize, Scale);
	TestEqual(TEXT("A zero-width viewport reports unit scale"), Scale, 1.0f, Tolerance);
	TestTrue(TEXT("A zero-width viewport produces no NaN or infinity"),
		FMath::IsFinite(CanvasSize.X) && FMath::IsFinite(CanvasSize.Y));

	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1920, 0), CanvasSize, Scale);
	TestEqual(TEXT("A zero-height viewport reports unit scale"), Scale, 1.0f, Tolerance);
	TestTrue(TEXT("A zero-height viewport produces no NaN or infinity"),
		FMath::IsFinite(CanvasSize.X) && FMath::IsFinite(CanvasSize.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerCustomFallbackTest,
	"DreamGUI.Canvas.Scaler.CustomWithoutHandlerFallsBackToConstantPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerCustomFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::Custom);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));

	FVector2D CanvasSize;
	float Scale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1920, 1080), CanvasSize, Scale);

	// Custom with no CustomScale assigned is documented to behave as ConstantPixelSize rather than
	// leaving the canvas at whatever it was.
	TestEqual(TEXT("Custom without a handler uses the viewport width"), (float)CanvasSize.X, 1920.0f, Tolerance);
	TestEqual(TEXT("Custom without a handler uses the viewport height"), (float)CanvasSize.Y, 1080.0f, Tolerance);
	TestEqual(TEXT("Custom without a handler reports unit scale"), Scale, 1.0f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerEngineDPIMatchesEngineTest,
	"DreamGUI.Canvas.Scaler.EngineDPITakesItsScaleFromEngineSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerEngineDPIMatchesEngineTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithEngineDPI);
	// Deliberately hostile values: this mode must ignore the ScaleWithScreenSize knobs entirely.
	Fixture.Canvas->SetReferenceResolution(FVector2D(640, 480));
	Fixture.Canvas->SetMatchFromWidthToHeight(0.0f);

	const UUserInterfaceSettings* UISettings = GetDefault<UUserInterfaceSettings>();
	const FIntPoint Viewports[] = { FIntPoint(1920, 1080), FIntPoint(1280, 720), FIntPoint(2560, 1440), FIntPoint(1387, 780) };
	for (const FIntPoint& Viewport : Viewports)
	{
		FVector2D CanvasSize;
		float Scale = -1.0f;
		Fixture.Canvas->CalculateCanvasSizeAndScale(Viewport, CanvasSize, Scale);

		// The specification for this mode is "whatever UMG would do", so the assertion is equality
		// with the engine's own answer rather than a re-derivation of its curve here.
		const float EngineScale = UISettings->GetDPIScaleBasedOnSize(Viewport);
		TestEqual(FString::Printf(TEXT("%dx%d takes its scale from the engine DPI settings"), Viewport.X, Viewport.Y),
			Scale, EngineScale, 0.0001f);
		// SDPIScaler's contract: lay out in viewport/scale units, render at scale. Multiplying back
		// must return the viewport, which is what catches an inverted or dropped division.
		TestEqual(FString::Printf(TEXT("%dx%d canvas width times scale returns the viewport"), Viewport.X, Viewport.Y),
			(float)(CanvasSize.X * Scale), (float)Viewport.X, 0.01f);
		TestEqual(FString::Printf(TEXT("%dx%d canvas height times scale returns the viewport"), Viewport.X, Viewport.Y),
			(float)(CanvasSize.Y * Scale), (float)Viewport.Y, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasScalerEngineDPIKeepsDesignHeightTest,
	"DreamGUI.Canvas.Scaler.EngineDPIKeepsTheLayoutRectStableAcrossResolutions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasScalerEngineDPIKeepsDesignHeightTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasScalerTestLocal;
	FScopedCanvas Fixture;

	// This is the property the mode exists for, and the one ScaleWithScreenSize does not have: the
	// rect a designer lays out in must not shrink with the window, or content authored to fill it
	// runs out of room and gets clipped instead of merely rendering smaller. Numbers below come
	// from the engine's shipped ShortestSide curve; a project that retunes its own DPI curve is
	// expected to update them.
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithEngineDPI);
	FVector2D At1080, At720, At1440;
	float Scale1080 = -1.0f, Scale720 = -1.0f, Scale1440 = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1920, 1080), At1080, Scale1080);
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1280, 720), At720, Scale720);
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(2560, 1440), At1440, Scale1440);

	TestEqual(TEXT("1080p renders at unit scale"), Scale1080, 1.0f, 0.005f);
	TestEqual(TEXT("1080p lays out at the design size"), (float)At1080.Y, 1080.0f, 1.0f);
	TestEqual(TEXT("720p lays out at very nearly the same height"), (float)At720.Y, 1080.0f, 2.0f);
	TestEqual(TEXT("1440p lays out at the same height"), (float)At1440.Y, 1080.0f, 1.0f);
	TestTrue(TEXT("720p renders smaller than 1080p rather than laying out smaller"), Scale720 < Scale1080);
	TestTrue(TEXT("1440p renders larger"), Scale1440 > Scale1080);

	// Contrast with the mode that produced the reported clipping: a reference of 1280x720 with a
	// blended match gives a rect that is both smaller than the design size and aspect-dependent,
	// so content authored against a taller rect has nowhere to go.
	Fixture.Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Fixture.Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
	Fixture.Canvas->SetReferenceResolution(FVector2D(1280, 720));
	Fixture.Canvas->SetMatchFromWidthToHeight(0.5f);
	FVector2D Wide;
	float WideScale = -1.0f;
	Fixture.Canvas->CalculateCanvasSizeAndScale(FIntPoint(1268, 662), Wide, WideScale);
	TestTrue(TEXT("A blended match on a wider-than-reference viewport drops below the reference height"),
		Wide.Y < 720.0f);
	return true;
}

#endif
