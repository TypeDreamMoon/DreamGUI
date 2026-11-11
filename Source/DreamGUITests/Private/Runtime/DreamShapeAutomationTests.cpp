// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Extensions/DreamShape.h"

/*
 * UDreamShape::Triangulate fills an outline by ear clipping. What matters about a fill is that it covers exactly the
 * outline: the triangles' areas add up to the outline's, none of them is wound backwards (a backwards triangle in a fan
 * cancels area it should add), and an outline it cannot fill is refused rather than filled wrongly.
 */

namespace DreamShapeTestLocal
{
	double OutlineArea(TConstArrayView<FVector2D> P)
	{
		double Twice = 0.0;
		for (int32 I = 0; I < P.Num(); ++I)
		{
			const FVector2D& A = P[I];
			const FVector2D& B = P[(I + 1) % P.Num()];
			Twice += A.X * B.Y - B.X * A.Y;
		}
		return FMath::Abs(Twice) * 0.5;
	}

	/** Sum of the triangles' signed areas, and whether every one of them is wound counter-clockwise. */
	double FillArea(TConstArrayView<FVector2D> P, const TArray<int32>& Indices, bool& bAllCounterClockwise)
	{
		double Sum = 0.0;
		bAllCounterClockwise = true;
		for (int32 T = 0; T + 2 < Indices.Num(); T += 3)
		{
			const FVector2D& A = P[Indices[T]];
			const FVector2D& B = P[Indices[T + 1]];
			const FVector2D& C = P[Indices[T + 2]];
			const double Signed = ((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
			bAllCounterClockwise &= Signed > 0.0;
			Sum += Signed;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeTriangulateTest,
	"DreamGUI.Extensions.Shape.FillsExactlyTheOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShapeTriangulateTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeTestLocal;

	struct FCase
	{
		const TCHAR* Name;
		TArray<FVector2D> Outline;
		int32 Triangles;
	};
	const FCase Cases[] = {
		{ TEXT("a triangle"), { {0, 0}, {1, 0}, {0, 1} }, 1 },
		{ TEXT("a square"), { {0, 0}, {1, 0}, {1, 1}, {0, 1} }, 2 },
		{ TEXT("a parallelogram"), { {0, 0}, {0.8, 0}, {1, 1}, {0.2, 1} }, 2 },
		{ TEXT("the same parallelogram wound clockwise"), { {0.2, 1}, {1, 1}, {0.8, 0}, {0, 0} }, 2 },
		{ TEXT("a concave L"), { {0, 0}, {1, 0}, {1, 0.4}, {0.4, 0.4}, {0.4, 1}, {0, 1} }, 4 },
		{ TEXT("an arrow head, concave at its notch"), { {0, 0}, {1, 0.5}, {0, 1}, {0.3, 0.5} }, 2 },
		{ TEXT("a square with a point in the middle of one side"), { {0, 0}, {0.5, 0}, {1, 0}, {1, 1}, {0, 1} }, 3 },
		{ TEXT("pixel coordinates"), { {0, 0}, {400, 0}, {460, 90}, {60, 90} }, 2 },
	};
	for (const FCase& Case : Cases)
	{
		TArray<int32> Indices;
		if (!TestTrue(FString::Printf(TEXT("%s is filled"), Case.Name), UDreamShape::Triangulate(Case.Outline, Indices)))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s takes %d triangles"), Case.Name, Case.Triangles), Indices.Num(), Case.Triangles * 3);
		bool bAllCounterClockwise = false;
		const double Filled = FillArea(Case.Outline, Indices, bAllCounterClockwise);
		TestTrue(FString::Printf(TEXT("%s: every triangle is wound one way"), Case.Name), bAllCounterClockwise);
		TestTrue(FString::Printf(TEXT("%s: the triangles cover its area (%.6f of %.6f)"), Case.Name, Filled, OutlineArea(Case.Outline)),
			FMath::IsNearlyEqual(Filled, OutlineArea(Case.Outline), 1e-6 * FMath::Max(1.0, OutlineArea(Case.Outline))));
	}

	TArray<int32> Indices;
	TestFalse(TEXT("two points are not an outline"), UDreamShape::Triangulate({ FVector2D(0, 0), FVector2D(1, 1) }, Indices));
	TestFalse(TEXT("three points on a line enclose nothing"),
		UDreamShape::Triangulate({ FVector2D(0, 0), FVector2D(0.5, 0.5), FVector2D(1, 1) }, Indices));
	TestFalse(TEXT("a bow tie crosses itself and is refused"),
		UDreamShape::Triangulate({ FVector2D(0, 0), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 1) }, Indices));
	TestEqual(TEXT("and a refusal appends nothing"), Indices.Num(), 0);
	return true;
}

#endif
