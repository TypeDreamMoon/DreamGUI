// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"

/*
 * A gradient (FDreamGradient) as an image editor draws it on the canvas: a line from where its parameter T is 0 to where it
 * is 1, its stops along it, and a centre to move it by. What the viewport's gradient handles show and what a drag of one
 * writes back -- into the gradient's own fields (Angle, Center, Radius, Scale, Offset), so the CSS that a .dui spells it
 * with stays the gradient's whole description.
 *
 * Everything is in fractions of the gradient's box from its top left (x right, y down), the box InAspect wide for its
 * height: what a vertex's UV4 holds, and what FDreamGradient::Evaluate is asked at. GetPointAt(Line, T) is a point where
 * Evaluate's T is exactly T, for every type with a line.
 */
struct DREAMGUIEDITOR_API FDreamGradientLine
{
	/** Where T is 0 and where it is 1. A Conic's End is its start direction, at ConicRadius from its centre. */
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	/** What the gradient is moved by: a Linear's mid-point, the others' Center. */
	FVector2D Center = FVector2D::ZeroVector;
	/** A Radial ellipse's second axis: where T is 1 along it. */
	FVector2D SecondEnd = FVector2D::ZeroVector;
	bool bHasSecondEnd = false;
	/** A Conic's stops go round Center on a circle through End rather than along the line. */
	bool bAround = false;
};

/** Which point of a gradient's line a handle is. */
enum class EDreamGradientPoint : uint8
{
	Center,
	Start,
	End,
	SecondEnd,
};

namespace DreamGradientHandles
{
	/** How far a Conic's start direction is drawn from its centre, in box heights: it has no length of its own. */
	constexpr double ConicRadius = 0.3;

	/** Whether InGradient has a line: every type that paints but Corners. */
	DREAMGUIEDITOR_API bool HasLine(const FDreamGradient& InGradient);
	/** InGradient's line. False for a gradient that has none. */
	DREAMGUIEDITOR_API bool GetLine(const FDreamGradient& InGradient, float InAspect, FDreamGradientLine& OutLine);
	/** Which of the line's points a type shows as handles, in drawing order. */
	DREAMGUIEDITOR_API TArray<EDreamGradientPoint> GetHandlePoints(const FDreamGradient& InGradient);
	DREAMGUIEDITOR_API FVector2D GetPoint(const FDreamGradientLine& InLine, EDreamGradientPoint InPoint);

	/** Where T is InT: along the line from Start to End, or round the circle for a Conic. */
	DREAMGUIEDITOR_API FVector2D GetPointAt(const FDreamGradient& InGradient, const FDreamGradientLine& InLine, float InT, float InAspect);
	/**
	 * The T a point is at along the line -- projected onto it, or its turn round the centre for a Conic -- and how far it is
	 * off the line (off the circle), in box heights.
	 */
	DREAMGUIEDITOR_API float Project(const FDreamGradient& InGradient, const FDreamGradientLine& InLine, const FVector2D& InPoint, float InAspect, double& OutDistanceOff);

	/**
	 * InOutGradient with point InPoint of its line at InTo, the others kept where they are as far as the type allows: a
	 * Linear's or a Reflected's start or end moves alone (its angle, scale and offset follow); a centre moves the whole
	 * gradient; a Radial's or a Diamond's end sets its radius and its turn (a circle's only its radius), a Conic's its start
	 * direction. A radius set this way is Explicit. With bInSnapAngle the turn is kept to steps of 15 degrees.
	 */
	DREAMGUIEDITOR_API void MovePoint(FDreamGradient& InOutGradient, float InAspect, EDreamGradientPoint InPoint, const FVector2D& InTo, bool bInSnapAngle = false);

	/** The colour the gradient's stops mix to at T, as a stop would hold it (sRGB-encoded, straight alpha). */
	DREAMGUIEDITOR_API FColor GetStopColorAt(const FDreamGradient& InGradient, float InT);
	/** InOutGradient with a stop of InColor at InT, kept in order among the others; its index. */
	DREAMGUIEDITOR_API int32 InsertStop(FDreamGradient& InOutGradient, float InT, const FColor& InColor);
}
