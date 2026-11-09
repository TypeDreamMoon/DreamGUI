// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/DreamPath.h"
#include "Core/DreamUIWidgetRegistry.h"

namespace DreamPathLocal
{
	/** The uniform Catmull-Rom segment from InP1 to InP2 at InT. */
	FVector2f CatmullRom(const FVector2f& InP0, const FVector2f& InP1, const FVector2f& InP2, const FVector2f& InP3, float InT)
	{
		const float T2 = InT * InT;
		const float T3 = T2 * InT;
		return 0.5f * ((2.0f * InP1) + (InP2 - InP0) * InT + (2.0f * InP0 - 5.0f * InP1 + 4.0f * InP2 - InP3) * T2
			+ (3.0f * InP1 - InP0 - 3.0f * InP2 + InP3) * T3);
	}

	/** Where a direction of length 0 is asked for, the path's own way: along x. */
	FVector2f SafeDirection(const FVector2f& InVector)
	{
		const FVector2f Unit = InVector.GetSafeNormal();
		return Unit.IsNearlyZero() ? FVector2f(1.0f, 0.0f) : Unit;
	}
}

void FDreamPathCurve::Reset()
{
	Points.Reset();
	Distances.Reset();
	bClosed = false;
	bCircle = false;
	Radius = 0.0f;
}

void FDreamPathCurve::Build(TConstArrayView<FVector2D> InControls, EDreamPathCurve InCurve, int32 InSegmentsPerSpan, bool bInClosed)
{
	using namespace DreamPathLocal;
	Reset();
	const int32 Count = InControls.Num();
	if (Count < 2)
	{
		return;
	}
	const bool bLoop = bInClosed && Count >= 3;
	auto Control = [&](int32 InIndex) -> FVector2f
	{
		if (bLoop)
		{
			return FVector2f(InControls[(InIndex % Count + Count) % Count]);
		}
		// An open curve's ends repeat their end points, so it starts and stops on them.
		return FVector2f(InControls[FMath::Clamp(InIndex, 0, Count - 1)]);
	};
	const int32 Spans = bLoop ? Count : Count - 1;
	const int32 Pieces = InCurve == EDreamPathCurve::Smooth ? FMath::Clamp(InSegmentsPerSpan, 1, 64) : 1;
	TArray<FVector2f> Samples;
	Samples.Reserve(Spans * Pieces + 1);
	for (int32 Span = 0; Span < Spans; ++Span)
	{
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const float T = static_cast<float>(Piece) / Pieces;
			Samples.Add(Pieces == 1 ? Control(Span) : CatmullRom(Control(Span - 1), Control(Span), Control(Span + 1), Control(Span + 2), T));
		}
	}
	if (!bLoop)
	{
		Samples.Add(Control(Count - 1));
	}
	SetPolyline(MoveTemp(Samples), bLoop);
}

void FDreamPathCurve::SetPolyline(TArray<FVector2f> InPoints, bool bInClosed)
{
	Reset();
	Points = MoveTemp(InPoints);
	if (Points.Num() < 2)
	{
		Points.Reset();
		return;
	}
	bClosed = bInClosed && Points.Num() >= 3;
	Distances.Reset(Points.Num() + 1);
	float Along = 0.0f;
	Distances.Add(0.0f);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Along += FVector2f::Distance(Points[Index - 1], Points[Index]);
		Distances.Add(Along);
	}
	if (bClosed)
	{
		Along += FVector2f::Distance(Points.Last(), Points[0]);
		Distances.Add(Along);
	}
}

void FDreamPathCurve::BuildCircle(FVector2f InCentre, float InRadius, float InStartDegrees, float InSweepDegrees, bool bInClockwise)
{
	Reset();
	bCircle = true;
	Centre = InCentre;
	Radius = FMath::Max(InRadius, 0.0f);
	StartDegrees = InStartDegrees;
	SweepDegrees = FMath::Clamp(InSweepDegrees, 0.0f, 360.0f);
	bClockwise = bInClockwise;
	bClosed = SweepDegrees >= 360.0f;
}

float FDreamPathCurve::GetLength() const
{
	if (bCircle)
	{
		return Radius * FMath::DegreesToRadians(SweepDegrees);
	}
	return Distances.Num() > 0 ? Distances.Last() : 0.0f;
}

void FDreamPathCurve::Evaluate(float InDistance, FVector2f& OutPoint, FVector2f& OutDirection) const
{
	using namespace DreamPathLocal;
	const float Length = GetLength();
	if (Length <= 0.0f)
	{
		OutPoint = bCircle ? Centre : (Points.Num() > 0 ? Points[0] : FVector2f::ZeroVector);
		OutDirection = FVector2f(1.0f, 0.0f);
		return;
	}
	if (bClosed)
	{
		InDistance = FMath::Fmod(InDistance, Length);
		if (InDistance < 0.0f)
		{
			InDistance += Length;
		}
	}
	if (bCircle)
	{
		// Clockwise from twelve o'clock: the point is (sin, cos) of the angle, and moving clockwise is (cos, -sin).
		const float Turn = (bClockwise ? 1.0f : -1.0f);
		float Angle = FMath::DegreesToRadians(StartDegrees);
		float Along = InDistance;
		const bool bBefore = InDistance < 0.0f;
		const bool bAfter = InDistance > Length;
		const float Clamped = FMath::Clamp(InDistance, 0.0f, Length);
		Angle += Turn * Clamped / FMath::Max(Radius, KINDA_SMALL_NUMBER);
		const FVector2f Point = Centre + FVector2f(FMath::Sin(Angle), FMath::Cos(Angle)) * Radius;
		const FVector2f Direction = FVector2f(FMath::Cos(Angle), -FMath::Sin(Angle)) * Turn;
		Along = bBefore ? InDistance : (bAfter ? InDistance - Length : 0.0f);
		OutPoint = Point + Direction * Along;
		OutDirection = Direction;
		return;
	}
	const int32 Count = Points.Num();
	auto PointAt = [&](int32 InIndex) { return Points[InIndex % Count]; };
	const int32 Segments = bClosed ? Count : Count - 1;
	if (InDistance <= 0.0f)
	{
		const FVector2f Direction = SafeDirection(PointAt(1) - PointAt(0));
		OutPoint = PointAt(0) + Direction * InDistance;
		OutDirection = Direction;
		return;
	}
	if (InDistance >= Length)
	{
		const FVector2f Direction = SafeDirection(PointAt(Segments) - PointAt(Segments - 1));
		OutPoint = PointAt(Segments) + Direction * (InDistance - Length);
		OutDirection = Direction;
		return;
	}
	// The segment InDistance falls in: Distances is ascending.
	int32 Low = 0, High = Segments;
	while (High - Low > 1)
	{
		const int32 Mid = (Low + High) / 2;
		if (Distances[Mid] <= InDistance)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	const float SegmentLength = Distances[Low + 1] - Distances[Low];
	const float T = SegmentLength > 0.0f ? (InDistance - Distances[Low]) / SegmentLength : 0.0f;
	const FVector2f A = PointAt(Low);
	const FVector2f B = PointAt(Low + 1);
	OutPoint = A + (B - A) * T;
	// The direction turns smoothly from one segment's to the next over the half of each segment nearest their joint, so that
	// glyphs placed along a polyline do not snap from one angle to the next.
	const FVector2f Here = SafeDirection(B - A);
	FVector2f Direction = Here;
	if (T < 0.5f && (bClosed || Low > 0))
	{
		const FVector2f Before = SafeDirection(A - PointAt((Low - 1 + Count) % Count));
		Direction = SafeDirection(FMath::Lerp(Before, Here, 0.5f + T));
	}
	else if (T > 0.5f && (bClosed || Low + 1 < Segments))
	{
		const FVector2f After = SafeDirection(PointAt(Low + 2) - B);
		Direction = SafeDirection(FMath::Lerp(Here, After, T - 0.5f));
	}
	OutDirection = Direction;
}

void FDreamPathCurve::Sample(int32 InCount, TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset();
	const float Length = GetLength();
	if (InCount < 2 || Length <= 0.0f)
	{
		return;
	}
	// A closed path's last sample would be its first again.
	const int32 Steps = bClosed ? InCount : InCount - 1;
	for (int32 Index = 0; Index < InCount; ++Index)
	{
		FVector2f Point, Direction;
		Evaluate(Length * Index / Steps, Point, Direction);
		OutPoints.Add(FVector2D(Point));
	}
}

UDreamPath::UDreamPath(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	LineWidth = 4.0f;
}

#if WITH_EDITOR
void UDreamPath::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bClosed)
	{
		EndType = EDream2DLineRenderer_EndType::ConnectStartAndEnd;
	}
	MarkPathChanged();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamPath::MarkPathChanged()
{
	bCurveDirty = true;
	MarkVerticesDirty(true, true, true, true);
	OnPathChanged.Broadcast();
}

const FDreamPathCurve& UDreamPath::GetCurve()
{
	if (bCurveDirty)
	{
		bCurveDirty = false;
		CachedCurve.Build(Points, Curve, SegmentsPerSpan, bClosed);
	}
	return CachedCurve;
}

void UDreamPath::CalculatePoints()
{
	const FDreamPathCurve& Built = GetCurve();
	DrawnPoints.Reset(Built.Points.Num());
	if (bDrawLine)
	{
		for (const FVector2f& Point : Built.Points)
		{
			DrawnPoints.Add(FVector2D(Point));
		}
	}
}

const TArray<FVector2D>& UDreamPath::GetCalcaultedPointArray()
{
	return DrawnPoints;
}

void UDreamPath::SetPoints(const TArray<FVector2D>& Value)
{
	Points = Value;
	MarkPathChanged();
}

void UDreamPath::SetPoint(int32 InIndex, FVector2D InPoint)
{
	if (Points.IsValidIndex(InIndex))
	{
		Points[InIndex] = InPoint;
		MarkPathChanged();
	}
}

void UDreamPath::SetCurveType(EDreamPathCurve Value)
{
	if (Curve != Value)
	{
		Curve = Value;
		MarkPathChanged();
	}
}

void UDreamPath::SetSegmentsPerSpan(int32 Value)
{
	Value = FMath::Clamp(Value, 1, 64);
	if (SegmentsPerSpan != Value)
	{
		SegmentsPerSpan = Value;
		MarkPathChanged();
	}
}

void UDreamPath::SetClosed(bool Value)
{
	if (bClosed != Value)
	{
		bClosed = Value;
		if (bClosed)
		{
			EndType = EDream2DLineRenderer_EndType::ConnectStartAndEnd;
		}
		else if (EndType == EDream2DLineRenderer_EndType::ConnectStartAndEnd)
		{
			EndType = EDream2DLineRenderer_EndType::Cap;
		}
		MarkPathChanged();
	}
}

void UDreamPath::SetDrawLine(bool Value)
{
	if (bDrawLine != Value)
	{
		bDrawLine = Value;
		MarkVerticesDirty(true, true, true, true);
	}
}

float UDreamPath::GetLength()
{
	return GetCurve().GetLength();
}

void UDreamPath::GetPointAtDistance(float InDistance, FVector2D& OutPoint, FVector2D& OutDirection)
{
	FVector2f Point, Direction;
	GetCurve().Evaluate(InDistance, Point, Direction);
	OutPoint = FVector2D(Point);
	OutDirection = FVector2D(Direction);
}

DECLARE_DREAM_GUI_VISUAL("Path", UDreamPath)
