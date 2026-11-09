// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Designer/DreamDesignerHandles.h"
#include "Designer/DreamGradientHandleMath.h"

/*
 * A gradient drawn on the canvas as an image editor's gradient tool draws it (DreamGradientHandles): where its line runs is
 * where FDreamGradient::Evaluate's T runs from 0 to 1, for every type; moving a point of the line moves the gradient so that
 * the point is where it was put; and a stop added on the line takes the colour already there.
 */
namespace DreamGradientHandleTestLocal
{
	/** A ramp mixed in linear light, black to white, so that Evaluate's red is its T wherever T is in [0, 1]. */
	FDreamGradient MakeRamp(const TCHAR* InCss)
	{
		FDreamGradient Gradient;
		FDreamGradient::ParseCss(InCss, Gradient);
		Gradient.Interpolation = EDreamPaintInterpolation::Linear;
		Gradient.Stops = { FDreamGradientStop(0.0f, FColor::Black), FDreamGradientStop(1.0f, FColor::White) };
		return Gradient;
	}

	const TCHAR* const RampCases[] =
	{
		TEXT("linear-gradient(60deg, #000, #fff)"),
		TEXT("linear-gradient(200deg scale 0.5 offset 0.2, #000, #fff)"),
		TEXT("radial-gradient(30deg ellipse 40% 25% at 35% 60%, #000, #fff)"),
		TEXT("radial-gradient(circle closest-side at 60% 40%, #000, #fff)"),
		TEXT("diamond-gradient(20deg 45% 30% at 50% 45%, #000, #fff)"),
		TEXT("reflected-gradient(120deg at 40% 50% scale 0.8, #000, #fff)"),
		TEXT("conic-gradient(from 30deg at 50% 50%, #000, #fff)"),
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientHandleLineTest,
	"DreamGUI.Editor.GradientHandles.APointOfAGradientsLineIsWhereEvaluatesTIsThat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientHandleLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientHandleTestLocal;
	for (const float Aspect : { 1.0f, 2.0f, 0.6f })
	{
		for (const TCHAR* Css : RampCases)
		{
			const FDreamGradient Ramp = MakeRamp(Css);
			FDreamGradientLine Line;
			if (!TestTrue(*FString::Printf(TEXT("%s has a line"), Css), DreamGradientHandles::GetLine(Ramp, Aspect, Line)))
			{
				continue;
			}
			for (const float T : { 0.05f, 0.25f, 0.5f, 0.8f, 0.95f })
			{
				const FVector2D Point = DreamGradientHandles::GetPointAt(Ramp, Line, T, Aspect);
				const float Evaluated = Ramp.Evaluate(FVector2f(Point), Aspect).R;
				TestEqual(*FString::Printf(TEXT("%s at aspect %.1f: T is %.2f at its point for it"), Css, Aspect, T), Evaluated, T, 2e-4f);
				double Off = 0.0;
				TestEqual(*FString::Printf(TEXT("%s at aspect %.1f: the point projects back to %.2f"), Css, Aspect, T),
					DreamGradientHandles::Project(Ramp, Line, Point, Aspect, Off), T, 2e-4f);
				TestTrue(TEXT("on the line"), Off < 1e-4);
			}
		}
	}
	FDreamGradient Corners = MakeRamp(TEXT("corners-gradient(#000, #fff)"));
	FDreamGradientLine Unused;
	TestFalse(TEXT("four corners have no line"), DreamGradientHandles::GetLine(Corners, 1.0f, Unused));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientHandleMoveTest,
	"DreamGUI.Editor.GradientHandles.MovingAPointOfTheLinePutsItWhereItWasMoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientHandleMoveTest::RunTest(const FString& Parameters)
{
	using namespace DreamGradientHandleTestLocal;
	const float Aspect = 1.6f;

	// A linear's start and end, each on its own: the other stays.
	FDreamGradient Linear = MakeRamp(TEXT("linear-gradient(90deg, #000, #fff)"));
	FDreamGradientLine Before;
	DreamGradientHandles::GetLine(Linear, Aspect, Before);
	DreamGradientHandles::MovePoint(Linear, Aspect, EDreamGradientPoint::End, FVector2D(0.7, 0.9));
	FDreamGradientLine After;
	DreamGradientHandles::GetLine(Linear, Aspect, After);
	TestTrue(*FString::Printf(TEXT("the end is where it was put: %s"), *After.End.ToString()), After.End.Equals(FVector2D(0.7, 0.9), 1e-4));
	TestTrue(*FString::Printf(TEXT("the start stayed: %s, was %s"), *After.Start.ToString(), *Before.Start.ToString()), After.Start.Equals(Before.Start, 1e-4));
	DreamGradientHandles::MovePoint(Linear, Aspect, EDreamGradientPoint::Start, FVector2D(0.1, 0.2));
	DreamGradientHandles::GetLine(Linear, Aspect, After);
	TestTrue(TEXT("and the start moves alone too"), After.Start.Equals(FVector2D(0.1, 0.2), 1e-4) && After.End.Equals(FVector2D(0.7, 0.9), 1e-4));
	TestEqual(TEXT("which Evaluate agrees with: 0 at the start"), Linear.Evaluate(FVector2f(0.1f, 0.2f), Aspect).R, 0.0f, 1e-4f);
	TestEqual(TEXT("and 1 at the end"), Linear.Evaluate(FVector2f(0.7f, 0.9f), Aspect).R, 1.0f, 1e-4f);

	// Shift keeps the turn to fifteen degrees.
	FDreamGradient Snapped = MakeRamp(TEXT("linear-gradient(90deg, #000, #fff)"));
	DreamGradientHandles::MovePoint(Snapped, 1.0f, EDreamGradientPoint::End, FVector2D(0.9, 0.62), /*bInSnapAngle*/ true);
	TestEqual(*FString::Printf(TEXT("snapped to a step: %.3f"), Snapped.Angle), FMath::Fmod(Snapped.Angle, 15.0f), 0.0f, 1e-3f);

	// A reflected's, the same.
	FDreamGradient Reflected = MakeRamp(TEXT("reflected-gradient(#000, #fff)"));
	DreamGradientHandles::MovePoint(Reflected, Aspect, EDreamGradientPoint::End, FVector2D(0.9, 0.3));
	DreamGradientHandles::GetLine(Reflected, Aspect, After);
	TestTrue(TEXT("a reflected's end is where it was put"), After.End.Equals(FVector2D(0.9, 0.3), 1e-4));
	TestTrue(TEXT("its start stayed at the centre"), After.Start.Equals(FVector2D(0.5, 0.5), 1e-4));

	// A radial's centre moves it whole; its end sets its radius and its turn; its second end its other radius.
	FDreamGradient Radial = MakeRamp(TEXT("radial-gradient(#000, #fff)"));
	DreamGradientHandles::MovePoint(Radial, Aspect, EDreamGradientPoint::Center, FVector2D(0.3, 0.4));
	TestTrue(TEXT("the centre is moved"), Radial.Center.Equals(FVector2f(0.3f, 0.4f), 1e-5f));
	DreamGradientHandles::MovePoint(Radial, Aspect, EDreamGradientPoint::End, FVector2D(0.6, 0.6));
	DreamGradientHandles::GetLine(Radial, Aspect, After);
	TestTrue(*FString::Printf(TEXT("the end is where it was put: %s"), *After.End.ToString()), After.End.Equals(FVector2D(0.6, 0.6), 1e-4));
	TestTrue(TEXT("an explicit radius now"), Radial.Size == EDreamPaintRadialSize::Explicit);
	TestEqual(TEXT("1 at the end"), Radial.Evaluate(FVector2f(0.6f, 0.6f), Aspect).R, 1.0f, 1e-4f);
	DreamGradientHandles::MovePoint(Radial, Aspect, EDreamGradientPoint::SecondEnd, After.SecondEnd + (After.SecondEnd - After.Center) * 0.5);
	FDreamGradientLine Longer;
	DreamGradientHandles::GetLine(Radial, Aspect, Longer);
	TestTrue(TEXT("the second end goes out, the first stays"), Longer.End.Equals(After.End, 1e-4));

	// A diamond's end turns it and keeps its shape; a conic's end turns where it starts.
	FDreamGradient Diamond = MakeRamp(TEXT("diamond-gradient(40% 20%, #000, #fff)"));
	const FVector2f RadiiBefore = Diamond.GetEndingRadii(Aspect);
	DreamGradientHandles::MovePoint(Diamond, Aspect, EDreamGradientPoint::End, FVector2D(0.5, 0.95));
	const FVector2f RadiiAfter = Diamond.GetEndingRadii(Aspect);
	TestEqual(TEXT("a diamond keeps the ratio of its radii"), RadiiAfter.Y / RadiiAfter.X, RadiiBefore.Y / RadiiBefore.X, 1e-4f);
	TestEqual(TEXT("1 at its end"), Diamond.Evaluate(FVector2f(0.5f, 0.95f), Aspect).R, 1.0f, 1e-4f);
	FDreamGradient Conic = MakeRamp(TEXT("conic-gradient(#000, #fff)"));
	DreamGradientHandles::MovePoint(Conic, 1.0f, EDreamGradientPoint::End, FVector2D(0.9, 0.5));
	TestEqual(TEXT("a conic pointed right starts at 90 degrees"), Conic.Angle, 90.0f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGradientHandleStopTest,
	"DreamGUI.Editor.GradientHandles.AStopAddedOnTheLineTakesTheColourThereAndItsPlaceInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGradientHandleStopTest::RunTest(const FString& Parameters)
{
	FDreamGradient Gradient;
	FDreamGradient::ParseCss(TEXT("linear-gradient(90deg in srgb-linear, #000000, #ffffff)"), Gradient);
	const FColor Halfway = DreamGradientHandles::GetStopColorAt(Gradient, 0.5f);
	TestTrue(*FString::Printf(TEXT("half way in linear light is #BCBCBC: %s"), *Halfway.ToHex()), FMath::Abs(Halfway.R - 188) <= 1 && Halfway.A == 255);
	const int32 Index = DreamGradientHandles::InsertStop(Gradient, 0.5f, Halfway);
	TestEqual(TEXT("between the two"), Index, 1);
	TestEqual(TEXT("three stops"), Gradient.Stops.Num(), 3);
	TestEqual(TEXT("one at the end goes last"), DreamGradientHandles::InsertStop(Gradient, 1.0f, FColor::Red), 3);

	// The rect fractions and the widget's local space, round and back.
	TestTrue(TEXT("no widget, no rect"), FDreamDesignerHandles::RectFractionToLocal(nullptr, FVector2D(0.5, 0.5)).IsZero());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
