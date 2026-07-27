// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/LexPerspective.h"

/*
 * The per-subtree perspective map, on its own, before anything calls it.
 *
 * These state correctness as the property the feature actually has to have -- "the canvas's eye now
 * sees what the subtree's eye would have seen" -- rather than by restating the matrix algebra in a
 * second place, which would only prove the algebra agrees with itself. Concretely: a point and its
 * remapped image must cross the scope's plane at the SAME place, one seen from the inner eye and
 * one from the outer. That is what "the same picture" means.
 */

namespace LexPerspectiveTestLocal
{
	using namespace LexPerspective;

	FScope MakeScope(const FVector& PlanePoint, const FVector& Normal, double EyeDistance)
	{
		FScope Scope;
		Scope.PlanePoint = PlanePoint;
		Scope.PlaneNormal = Normal;
		Scope.EyePosition = PlanePoint + Normal.GetSafeNormal() * EyeDistance;
		return Scope;
	}

	/** Where a viewer at Eye sees Point land on the scope's plane. */
	bool ImageOf(const FVector& Eye, const FVector& Point, const FScope& Scope, FVector& Out)
	{
		return ProjectOntoPlane(Eye, Point, Scope.PlanePoint, Scope.PlaneNormal, Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveReproducesTheInnerViewTest,
	"LGUI.Perspective.Math.TheOuterEyeSeesWhatTheInnerEyeWouldHave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveReproducesTheInnerViewTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveTestLocal;

	// A scope whose eye is much closer than the canvas's, which is the interesting direction: a
	// strong local perspective inside a weak global one.
	const FScope Scope = MakeScope(FVector::ZeroVector, FVector::XAxisVector, 300.0);
	const FVector CanvasEye(1200.0, 0.0, 0.0);
	const FMatrix Remap = MakeRemap(Scope, CanvasEye);

	// Points at assorted depths and offsets, including behind the plane, which is what a card
	// leaning away from the viewer looks like.
	const TArray<FVector> Points = {
		FVector(60.0, 120.0, -40.0),
		FVector(-45.0, -30.0, 90.0),
		FVector(10.0, 0.0, 0.0),
		FVector(-120.0, 200.0, 200.0),
	};
	for (const FVector& Point : Points)
	{
		FVector SeenByInnerEye, SeenByOuterEye;
		if (!TestTrue(TEXT("The inner eye can see the point"), ImageOf(Scope.EyePosition, Point, Scope, SeenByInnerEye)))continue;
		const FVector Remapped = Remap.TransformPosition(Point);
		if (!TestTrue(TEXT("The outer eye can see the remapped point"), ImageOf(CanvasEye, Remapped, Scope, SeenByOuterEye)))continue;

		TestTrue(*FString::Printf(TEXT("Point %s lands in the same place either way"), *Point.ToString()),
			SeenByInnerEye.Equals(SeenByOuterEye, 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectivePlaneIsFixedTest,
	"LGUI.Perspective.Math.ThePlaneItselfDoesNotMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectivePlaneIsFixedTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveTestLocal;

	// Off the origin and tilted, so that a map which only happened to work for an axis-aligned scope
	// at the world origin is caught here.
	const FScope Scope = MakeScope(FVector(50.0, -20.0, 15.0), FVector(1.0, 0.4, -0.2), 400.0);
	const FMatrix Remap = MakeRemap(Scope, FVector(900.0, 100.0, -50.0));

	// Everything a perspective scope contains that has no depth of its own -- which is most of a UI
	// -- must come out untouched, or enabling a perspective would shift a flat interface sideways.
	const FVector Normal = Scope.PlaneNormal.GetSafeNormal();
	const FVector InPlaneA = Scope.PlanePoint + FVector::CrossProduct(Normal, FVector::ZAxisVector).GetSafeNormal() * 130.0;
	const FVector InPlaneB = Scope.PlanePoint + FVector::CrossProduct(Normal, InPlaneA - Scope.PlanePoint).GetSafeNormal() * 80.0;
	for (const FVector& Point : { Scope.PlanePoint, InPlaneA, InPlaneB })
	{
		TestTrue(*FString::Printf(TEXT("%s is left alone"), *Point.ToString()),
			Remap.TransformPosition(Point).Equals(Point, 0.001));
	}

	// A point off the plane must not be, or the map is doing nothing at all.
	const FVector OffPlane = Scope.PlanePoint + Normal * 60.0;
	TestFalse(TEXT("A point with depth does move"), Remap.TransformPosition(OffPlane).Equals(OffPlane, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveNestingTest,
	"LGUI.Perspective.Math.NestedScopesHandOffToEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveNestingTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveTestLocal;

	// Two perspectives, one inside the other. The tempting implementation is "the nearest
	// perspective ancestor wins and the outer one is ignored", which passes every single-scope test
	// and is wrong here.
	const FScope Inner = MakeScope(FVector(0.0, 0.0, 0.0), FVector::XAxisVector, 250.0);
	const FScope Outer = MakeScope(FVector(-100.0, 0.0, 0.0), FVector::XAxisVector, 700.0);
	const FVector CanvasEye(1500.0, 0.0, 0.0);

	const TArray<FScope> Scopes = { Inner, Outer };
	const FMatrix Composed = ComposeRemap(Scopes, CanvasEye);

	// The property, applied twice: after the inner map, the OUTER eye must see what the inner eye
	// saw; and after the outer map on top, the canvas eye must see what the outer eye saw.
	const FVector Point(70.0, 90.0, -55.0);

	const FMatrix InnerRemap = MakeRemap(Inner, Outer.EyePosition);
	const FVector AfterInner = InnerRemap.TransformPosition(Point);
	{
		FVector SeenByInner, SeenByOuter;
		if (TestTrue(TEXT("Inner eye sees the point"), ImageOf(Inner.EyePosition, Point, Inner, SeenByInner))
			&& TestTrue(TEXT("Outer eye sees the once-remapped point"), ImageOf(Outer.EyePosition, AfterInner, Inner, SeenByOuter)))
		{
			TestTrue(TEXT("The inner hand-off preserves the picture"), SeenByInner.Equals(SeenByOuter, 0.01));
		}
	}

	const FMatrix OuterRemap = MakeRemap(Outer, CanvasEye);
	const FVector ByHand = OuterRemap.TransformPosition(AfterInner);
	TestTrue(TEXT("Composing agrees with applying the two in order"),
		Composed.TransformPosition(Point).Equals(ByHand, 0.01));

	// The second hand-off, measured on the OUTER scope's plane -- which is where that hand-off is
	// defined and the only place it holds. It is tempting to instead assert that the canvas ends up
	// seeing the innermost eye's view, measured on the inner plane; that is false, and believing it
	// is the quickest way to "fix" a correct implementation into a broken one. The two sight lines
	// meet on the outer plane by construction and nowhere else. Nested perspectives are a chain of
	// projections, exactly as they are in CSS -- not one projection wearing a disguise.
	{
		FVector SeenByOuter, SeenByCanvas;
		if (TestTrue(TEXT("Outer eye sees the once-remapped point"), ImageOf(Outer.EyePosition, AfterInner, Outer, SeenByOuter))
			&& TestTrue(TEXT("Canvas sees the twice-remapped point"), ImageOf(CanvasEye, ByHand, Outer, SeenByCanvas)))
		{
			TestTrue(TEXT("The outer hand-off preserves the picture"), SeenByOuter.Equals(SeenByCanvas, 0.01));
		}
	}

	// Nearest-ancestor-wins would produce this instead. It has to differ, or the nesting test proves
	// nothing about nesting.
	const FMatrix NearestOnly = MakeRemap(Inner, CanvasEye);
	TestFalse(TEXT("Ignoring the outer scope gives a different answer"),
		NearestOnly.TransformPosition(Point).Equals(Composed.TransformPosition(Point), 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveDegenerateTest,
	"LGUI.Perspective.Math.DegenerateScopesAreIdentityNotGarbage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveDegenerateTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveTestLocal;
	const FVector Probe(33.0, -14.0, 77.0);

	// Same eye: nothing to re-aim, and the map must be exactly identity rather than a
	// floating-point approximation of it, because this is the path a disabled scope takes.
	{
		const FScope Scope = MakeScope(FVector::ZeroVector, FVector::XAxisVector, 500.0);
		const FMatrix Remap = MakeRemap(Scope, Scope.EyePosition);
		TestTrue(TEXT("Coincident eyes give identity"), Remap.Equals(FMatrix::Identity, 0.0));
	}
	// Eye in its own plane: sees the subtree edge-on, has no perspective to describe, and would
	// otherwise divide by a vanishing height.
	{
		FScope Scope = MakeScope(FVector::ZeroVector, FVector::XAxisVector, 500.0);
		Scope.EyePosition = FVector(0.0, 300.0, 0.0);//on the plane
		const FMatrix Remap = MakeRemap(Scope, FVector(1000.0, 0.0, 0.0));
		TestTrue(TEXT("An eye in the plane is refused"), Remap.Equals(FMatrix::Identity, 0.0));
	}
	// A normal that is not a direction at all.
	{
		FScope Scope;
		Scope.PlaneNormal = FVector::ZeroVector;
		Scope.EyePosition = FVector(500.0, 0.0, 0.0);
		TestTrue(TEXT("A zero normal is refused"), MakeRemap(Scope, FVector(1000.0, 0.0, 0.0)).Equals(FMatrix::Identity, 0.0));
	}
	// No scopes at all composes to identity, which is the everyday case.
	TestTrue(TEXT("No scopes is identity"),
		ComposeRemap(TArrayView<const FScope>(), FVector(1000.0, 0.0, 0.0)).Equals(FMatrix::Identity, 0.0));
	// Identity really is identity: a probe survives it unchanged.
	TestTrue(TEXT("...and leaves points alone"),
		ComposeRemap(TArrayView<const FScope>(), FVector(1000.0, 0.0, 0.0)).TransformPosition(Probe).Equals(Probe, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveInvertibleTest,
	"LGUI.Perspective.Math.TheMapInvertsSoClicksCanBeTracedBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveInvertibleTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveTestLocal;

	// Hit testing has to undo this map to turn a click back into a widget-local position, so an
	// inverse that does not round-trip would mean clicking where the widget is not.
	const FScope Scope = MakeScope(FVector(20.0, 5.0, -8.0), FVector(1.0, 0.25, 0.1), 320.0);
	const FMatrix Remap = MakeRemap(Scope, FVector(1100.0, -60.0, 40.0));
	const FMatrix Inverse = Remap.Inverse();

	for (const FVector& Point : { FVector(80.0, 40.0, 20.0), FVector(-30.0, -90.0, 110.0), Scope.PlanePoint })
	{
		const FVector RoundTripped = Inverse.TransformPosition(Remap.TransformPosition(Point));
		TestTrue(*FString::Printf(TEXT("%s survives a round trip"), *Point.ToString()),
			RoundTripped.Equals(Point, 0.01));
	}
	TestFalse(TEXT("The map is not singular"), FMath::IsNearlyZero(Remap.Determinant(), 1e-9));
	return true;
}

#endif
