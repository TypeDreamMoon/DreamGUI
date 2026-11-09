// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamGradientHandleMath.h"

/*
 * The arithmetic is FDreamGradient's reference evaluation (DreamTextPaint.h) run backwards, in "height space": a box
 * fraction (u, v) is the point (u * A, v), so lengths along both axes are in box heights, as the evaluation measures them.
 * With a the Angle in radians, S the Scale and o the Offset:
 *  - Linear: through the box's middle m along d = (sin a, -cos a), as long as L = |A sin a| + |cos a|. T is 0 at
 *    m + d (o - 1/2) S L and 1 at m + d (o + 1/2) S L -- and on every point across from those, so the line is drawn
 *    through Center, which a Linear's colours do not use and its handles keep their place across the line in.
 *  - Reflected: |the same line's T| measured from Center c, twice as fast: T is 0 at c + d o S L / 2, 1 at c + d (1 + o) S L / 2.
 *  - Radial, Diamond: from c along the turned first axis x = (cos a, sin a), the first radius R: T is 0 at c + x R S o and
 *    1 at c + x R S (1 + o); a Radial ellipse's second axis (-sin a, cos a) the same with its second radius.
 *  - Conic: round c from d, a turn being T's whole range once S and o are taken off.
 */
namespace DreamGradientHandleMathLocal
{
	FVector2D ToHeights(const FVector2D& InFraction, double InAspect)
	{
		return FVector2D(InFraction.X * InAspect, InFraction.Y);
	}

	FVector2D ToFraction(const FVector2D& InHeights, double InAspect)
	{
		return FVector2D(InHeights.X / FMath::Max(InAspect, 1e-6), InHeights.Y);
	}

	double Scale(const FDreamGradient& InGradient)
	{
		return FMath::Max(static_cast<double>(InGradient.Scale), 1e-4);
	}

	/** 1 + Offset, kept from reaching 0: the line would have no length. */
	double OnePlusOffset(const FDreamGradient& InGradient)
	{
		return FMath::Max(1.0 + InGradient.Offset, 1e-3);
	}

	/** CSS's direction for an angle: 0 up, clockwise, y down. */
	FVector2D Direction(double InDegrees)
	{
		double Sine = 0.0;
		double Cosine = 1.0;
		FMath::SinCos(&Sine, &Cosine, FMath::DegreesToRadians(InDegrees));
		return FVector2D(Sine, -Cosine);
	}

	/** The first axis a Radial or a Diamond is turned to: +x turned clockwise by the angle, y down. */
	FVector2D FirstAxis(double InDegrees)
	{
		double Sine = 0.0;
		double Cosine = 1.0;
		FMath::SinCos(&Sine, &Cosine, FMath::DegreesToRadians(InDegrees));
		return FVector2D(Cosine, Sine);
	}

	double LinearLength(double InDegrees, double InAspect)
	{
		const FVector2D D = Direction(InDegrees);
		return FMath::Max(FMath::Abs(InAspect * D.X) + FMath::Abs(D.Y), 1e-6);
	}

	/** An angle in [0, 360). */
	float Wrap(double InDegrees)
	{
		double Wrapped = FMath::Fmod(InDegrees, 360.0);
		if (Wrapped < 0.0)
		{
			Wrapped += 360.0;
		}
		return static_cast<float>(Wrapped >= 360.0 ? 0.0 : Wrapped);
	}

	float Snap(double InDegrees, bool bInSnap)
	{
		return Wrap(bInSnap ? FMath::RoundToDouble(InDegrees / 15.0) * 15.0 : InDegrees);
	}

	/** The angle CSS gives a direction (Direction's inverse). */
	double AngleOfDirection(const FVector2D& InDirection)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(InDirection.X, -InDirection.Y));
	}

	/** The angle a first axis is turned by (FirstAxis's inverse). */
	double AngleOfAxis(const FVector2D& InAxis)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(InAxis.Y, InAxis.X));
	}

	/** A Radial's or a Diamond's radii made Explicit at what they measure now, so that moving its centre leaves them be. */
	void MakeRadiiExplicit(FDreamGradient& InOutGradient, double InAspect, const FVector2D& InRadiiHeights)
	{
		InOutGradient.Size = EDreamPaintRadialSize::Explicit;
		InOutGradient.Radius = FVector2f(static_cast<float>(InRadiiHeights.X / FMath::Max(InAspect, 1e-6)), static_cast<float>(InRadiiHeights.Y));
	}

	/** Linear and Reflected: angle, scale and (Linear) offset or (Reflected) centre for a line from InStart to InEnd, in heights. */
	void SetLineEnds(FDreamGradient& InOutGradient, double InAspect, const FVector2D& InStart, const FVector2D& InEnd, bool bInSnapAngle)
	{
		FVector2D Along = InEnd - InStart;
		double Length = Along.Size();
		if (Length < 1e-6)
		{
			return;
		}
		const float Angle = Snap(AngleOfDirection(Along / Length), bInSnapAngle);
		const FVector2D D = Direction(Angle);
		// Snapped, the end follows the turn: the length is the line's along the new direction.
		Length = FMath::Max(FVector2D::DotProduct(Along, D), 1e-6);
		const double L = LinearLength(Angle, InAspect);
		InOutGradient.Angle = Angle;
		if (InOutGradient.Type == EDreamPaintType::Linear)
		{
			InOutGradient.Scale = static_cast<float>(Length / L);
			// The middle of the two ends along the line through the box's middle: Offset S L from it.
			const FVector2D Middle(0.5 * InAspect, 0.5);
			const FVector2D Between = (InStart + InEnd) * 0.5;
			InOutGradient.Offset = static_cast<float>(FVector2D::DotProduct(Between - Middle, D) / Length);
			// A Linear's colours do not use its Center: it keeps where across the line the handles were left, so that the
			// end not dragged stays where it is.
			InOutGradient.Center = FVector2f(ToFraction(Between, InAspect));
		}
		else
		{
			InOutGradient.Scale = static_cast<float>(2.0 * Length / L);
			const FVector2D Centre = InStart - D * (InOutGradient.Offset * Scale(InOutGradient) * L * 0.5);
			InOutGradient.Center = FVector2f(ToFraction(Centre, InAspect));
		}
	}
}

bool DreamGradientHandles::HasLine(const FDreamGradient& InGradient)
{
	switch (InGradient.Type)
	{
	case EDreamPaintType::Linear:
	case EDreamPaintType::Radial:
	case EDreamPaintType::Conic:
	case EDreamPaintType::Diamond:
	case EDreamPaintType::Reflected:
		return true;
	default:
		return false;
	}
}

bool DreamGradientHandles::GetLine(const FDreamGradient& InGradient, float InAspect, FDreamGradientLine& OutLine)
{
	using namespace DreamGradientHandleMathLocal;
	if (!HasLine(InGradient))
	{
		return false;
	}
	const double A = FMath::IsFinite(InAspect) && InAspect > 0.0f ? InAspect : 1.0;
	const double S = Scale(InGradient);
	const double O = InGradient.Offset;
	OutLine = FDreamGradientLine();
	const FVector2D Centre = ToHeights(FVector2D(InGradient.Center), A);
	switch (InGradient.Type)
	{
	case EDreamPaintType::Linear:
	{
		// Along the line through the box's middle, slid across to where its Center (which its colours do not use) says.
		const FVector2D Middle(0.5 * A, 0.5);
		const FVector2D D = Direction(InGradient.Angle);
		const double L = LinearLength(InGradient.Angle, A);
		const double Across = FVector2D::DotProduct(Centre - Middle, D);
		OutLine.Start = ToFraction(Centre + D * ((O - 0.5) * S * L - Across), A);
		OutLine.End = ToFraction(Centre + D * ((O + 0.5) * S * L - Across), A);
		OutLine.Center = (OutLine.Start + OutLine.End) * 0.5;
		break;
	}
	case EDreamPaintType::Reflected:
	{
		const FVector2D D = Direction(InGradient.Angle);
		const double Half = S * LinearLength(InGradient.Angle, A) * 0.5;
		OutLine.Start = ToFraction(Centre + D * (O * Half), A);
		OutLine.End = ToFraction(Centre + D * ((1.0 + O) * Half), A);
		OutLine.Center = ToFraction(Centre, A);
		break;
	}
	case EDreamPaintType::Radial:
	case EDreamPaintType::Diamond:
	{
		const FVector2D Radii(InGradient.GetEndingRadii(static_cast<float>(A)));
		const FVector2D X = FirstAxis(InGradient.Angle);
		const FVector2D Y(-X.Y, X.X);
		OutLine.Start = ToFraction(Centre + X * (Radii.X * S * O), A);
		OutLine.End = ToFraction(Centre + X * (Radii.X * S * (1.0 + O)), A);
		OutLine.Center = ToFraction(Centre, A);
		const bool bCircle = InGradient.Type == EDreamPaintType::Radial && InGradient.Shape == EDreamPaintRadialShape::Circle;
		OutLine.bHasSecondEnd = InGradient.Type == EDreamPaintType::Radial && !bCircle;
		OutLine.SecondEnd = ToFraction(Centre + Y * (Radii.Y * S * (1.0 + O)), A);
		break;
	}
	case EDreamPaintType::Conic:
	{
		OutLine.Start = ToFraction(Centre, A);
		OutLine.End = ToFraction(Centre + Direction(InGradient.Angle) * ConicRadius, A);
		OutLine.Center = OutLine.Start;
		OutLine.bAround = true;
		break;
	}
	default:
		return false;
	}
	return true;
}

TArray<EDreamGradientPoint> DreamGradientHandles::GetHandlePoints(const FDreamGradient& InGradient)
{
	switch (InGradient.Type)
	{
	case EDreamPaintType::Linear:
	case EDreamPaintType::Reflected:
		return { EDreamGradientPoint::Start, EDreamGradientPoint::End };
	case EDreamPaintType::Diamond:
	case EDreamPaintType::Conic:
		return { EDreamGradientPoint::Center, EDreamGradientPoint::End };
	case EDreamPaintType::Radial:
		if (InGradient.Shape == EDreamPaintRadialShape::Circle)
		{
			return { EDreamGradientPoint::Center, EDreamGradientPoint::End };
		}
		return { EDreamGradientPoint::Center, EDreamGradientPoint::End, EDreamGradientPoint::SecondEnd };
	default:
		return {};
	}
}

FVector2D DreamGradientHandles::GetPoint(const FDreamGradientLine& InLine, EDreamGradientPoint InPoint)
{
	switch (InPoint)
	{
	case EDreamGradientPoint::Start: return InLine.Start;
	case EDreamGradientPoint::End: return InLine.End;
	case EDreamGradientPoint::SecondEnd: return InLine.SecondEnd;
	default: return InLine.Center;
	}
}

FVector2D DreamGradientHandles::GetPointAt(const FDreamGradient& InGradient, const FDreamGradientLine& InLine, float InT, float InAspect)
{
	using namespace DreamGradientHandleMathLocal;
	if (!InLine.bAround)
	{
		return InLine.Start + (InLine.End - InLine.Start) * InT;
	}
	// Round the centre: T's share of a turn, scaled and moved back the way the evaluation takes them off.
	const double A = FMath::IsFinite(InAspect) && InAspect > 0.0f ? InAspect : 1.0;
	const double Turn = (InT + InGradient.Offset) * Scale(InGradient);
	return ToFraction(ToHeights(InLine.Center, A) + Direction(InGradient.Angle + 360.0 * Turn) * ConicRadius, A);
}

float DreamGradientHandles::Project(const FDreamGradient& InGradient, const FDreamGradientLine& InLine, const FVector2D& InPoint, float InAspect, double& OutDistanceOff)
{
	using namespace DreamGradientHandleMathLocal;
	const double A = FMath::IsFinite(InAspect) && InAspect > 0.0f ? InAspect : 1.0;
	const FVector2D Point = ToHeights(InPoint, A);
	if (InLine.bAround)
	{
		const FVector2D FromCentre = Point - ToHeights(InLine.Center, A);
		OutDistanceOff = FMath::Abs(FromCentre.Size() - ConicRadius);
		double Turn = (AngleOfDirection(FromCentre) - InGradient.Angle) / 360.0;
		Turn -= FMath::FloorToDouble(Turn);
		return static_cast<float>(Turn / Scale(InGradient) - InGradient.Offset);
	}
	const FVector2D Start = ToHeights(InLine.Start, A);
	const FVector2D Along = ToHeights(InLine.End, A) - Start;
	const double LengthSquared = Along.SizeSquared();
	if (LengthSquared < 1e-12)
	{
		OutDistanceOff = FVector2D::Distance(Point, Start);
		return 0.0f;
	}
	const double T = FVector2D::DotProduct(Point - Start, Along) / LengthSquared;
	OutDistanceOff = FVector2D::Distance(Point, Start + Along * T);
	return static_cast<float>(T);
}

void DreamGradientHandles::MovePoint(FDreamGradient& InOutGradient, float InAspect, EDreamGradientPoint InPoint, const FVector2D& InTo, bool bInSnapAngle)
{
	using namespace DreamGradientHandleMathLocal;
	FDreamGradientLine Line;
	if (!GetLine(InOutGradient, InAspect, Line))
	{
		return;
	}
	const double A = FMath::IsFinite(InAspect) && InAspect > 0.0f ? InAspect : 1.0;
	const FVector2D To = ToHeights(InTo, A);
	const FVector2D Centre = ToHeights(Line.Center, A);
	switch (InOutGradient.Type)
	{
	case EDreamPaintType::Linear:
	case EDreamPaintType::Reflected:
	{
		FVector2D Start = ToHeights(Line.Start, A);
		FVector2D End = ToHeights(Line.End, A);
		if (InPoint == EDreamGradientPoint::Start)
		{
			Start = To;
		}
		else if (InPoint == EDreamGradientPoint::End)
		{
			End = To;
		}
		else
		{
			// The centre moves the line as a whole.
			const FVector2D Delta = To - Centre;
			Start += Delta;
			End += Delta;
		}
		SetLineEnds(InOutGradient, A, Start, End, bInSnapAngle && InPoint != EDreamGradientPoint::Center);
		break;
	}
	case EDreamPaintType::Radial:
	case EDreamPaintType::Diamond:
	case EDreamPaintType::Conic:
	{
		if (InPoint == EDreamGradientPoint::Center || InPoint == EDreamGradientPoint::Start)
		{
			// A size keyword measures from the centre, so it goes on measuring from where the centre goes.
			InOutGradient.Center = FVector2f(ToFraction(To, A));
			break;
		}
		const FVector2D FromCentre = To - Centre;
		const double Distance = FromCentre.Size();
		if (Distance < 1e-6)
		{
			break;
		}
		if (InOutGradient.Type == EDreamPaintType::Conic)
		{
			InOutGradient.Angle = Snap(AngleOfDirection(FromCentre / Distance), bInSnapAngle);
			break;
		}
		const FVector2D Radii(InOutGradient.GetEndingRadii(static_cast<float>(A)));
		const double Reach = Distance / (Scale(InOutGradient) * OnePlusOffset(InOutGradient));
		const bool bCircle = InOutGradient.Type == EDreamPaintType::Radial && InOutGradient.Shape == EDreamPaintRadialShape::Circle;
		if (bCircle)
		{
			InOutGradient.Size = EDreamPaintRadialSize::Explicit;
			InOutGradient.Radius.X = static_cast<float>(Reach / A);
			break;
		}
		if (InPoint == EDreamGradientPoint::SecondEnd)
		{
			// The second axis is the first turned a quarter clockwise.
			InOutGradient.Angle = Snap(AngleOfAxis(FromCentre / Distance) - 90.0, bInSnapAngle);
			MakeRadiiExplicit(InOutGradient, A, FVector2D(Radii.X, Reach));
		}
		else
		{
			InOutGradient.Angle = Snap(AngleOfAxis(FromCentre / Distance), bInSnapAngle);
			// A Diamond keeps its shape; a Radial ellipse its other radius.
			const double Second = InOutGradient.Type == EDreamPaintType::Diamond && Radii.X > 1e-9 ? Radii.Y * Reach / Radii.X : Radii.Y;
			MakeRadiiExplicit(InOutGradient, A, FVector2D(Reach, Second));
		}
		break;
	}
	default:
		break;
	}
}

FColor DreamGradientHandles::GetStopColorAt(const FDreamGradient& InGradient, float InT)
{
	if (InGradient.Stops.IsEmpty())
	{
		return FColor::White;
	}
	// The stops alone, left to right across a square box, mixed in the gradient's own space.
	FDreamGradient Strip;
	Strip.Type = EDreamPaintType::Linear;
	Strip.Angle = 90.0f;
	Strip.Stops = InGradient.Stops;
	Strip.Interpolation = InGradient.Interpolation;
	return Strip.Evaluate(FVector2f(InT, 0.5f), 1.0f).ToFColor(/*bSRGB*/ true);
}

int32 DreamGradientHandles::InsertStop(FDreamGradient& InOutGradient, float InT, const FColor& InColor)
{
	int32 Index = 0;
	while (Index < InOutGradient.Stops.Num() && InOutGradient.Stops[Index].Position <= InT)
	{
		++Index;
	}
	InOutGradient.Stops.Insert(FDreamGradientStop(InT, InColor), Index);
	return Index;
}
