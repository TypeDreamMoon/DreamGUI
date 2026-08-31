// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamVisual.h"
#include "DreamRingSectorRaycast.generated.h"

/**
 * Hit-test an annulus sector rather than the rect it is drawn in.
 *
 * A ring menu's wedge is a SQUARE widget the size of the whole ring, of which the shader keeps one
 * slice (see UDreamRingMenu). Every wedge therefore covers every other wedge, and under the default
 * Rect raycast the topmost one would answer for the entire circle -- one item permanently hovered,
 * the rest unreachable. The hit shape has to be the shape that is drawn, and this is it.
 *
 * The raycast pipeline hands a custom raycast the ray in the widget's LOCAL space and does no rect
 * test of its own (UDreamVisual::LineTraceUICustom), so what this class says is the whole answer:
 * a sector may claim less than the rect, and -- with no OuterRadius -- more.
 *
 * ANGLES ARE CLOCKWISE FROM TWELVE
 * --------------------------------
 * The convention the ring family speaks, and the one a designer means by "put Attack at the top".
 * Local space here is the UI plane: X is depth, Y runs right, Z runs up (the same axes
 * UUISlider::CalculateInputValue reads), so the angle of a point is atan2(Y, Z) -- zero straight up,
 * growing toward the right. The shader's own wedge maths speaks a different dialect (clockwise from
 * three, because UV Y runs down); the CONTROL translates once, at the one place it writes the rect's
 * RadialFillRotation, and nothing else in the family has to know.
 *
 * ZERO OUTER RADIUS MEANS UNBOUNDED
 * ---------------------------------
 * The weapon-wheel feel -- flick the mouse anywhere and the nearest slice lights up -- is a sector
 * with no far edge, and it is reachable here precisely because nothing culls a widget by its bounds
 * before the trace (FDreamBaseRaycaster traces every visual it collects). A clipping ancestor still
 * bounds it, via IsPointVisibleOnClip, which is the correct answer for a menu inside a panel.
 */
UCLASS(BlueprintType, EditInlineNew, DisplayName = "Dream Ring Sector Raycast")
class DREAMGUI_API UDreamRingSectorRaycast : public UDreamVisualCustomRaycast
{
	GENERATED_BODY()

public:
	/** Inside this, the ray misses -- the hub, or a dead zone bigger than it. In local units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Sector", meta = (ClampMin = "0.0"))
	float InnerRadius = 0.0f;

	/** The far edge, in local units. Zero or less is UNBOUNDED -- see the class header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Sector")
	float OuterRadius = 0.0f;

	/** Where the slice begins, in degrees clockwise from twelve o'clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Sector")
	float StartAngle = 0.0f;

	/** How far it runs from there. At or past 360 the sector is the whole annulus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Sector")
	float SweepAngle = 360.0f;

	virtual bool Raycast(const UDreamVisual* InVisual, const FVector& InLocalSpaceRayStart,
		const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal) const override;

	/**
	 * The angle of a point on the widget's plane, measured from the RING's centre: degrees
	 * clockwise from twelve, wrapped into [0, 360).
	 *
	 * InLocalPoint is (Y, Z) -- right and up -- which is what LinePlaneIntersection yields on the
	 * X = 0 plane. Exposed because the control needs the same answer for a gamepad stick, which
	 * arrives as a direction rather than as a ray, and two spellings of one convention is how the
	 * two would drift apart.
	 */
	UFUNCTION(BlueprintPure, Category = "Ring Sector")
	static float AngleOfLocalPoint(const FVector2D& InLocalPoint);

	/** True while a clockwise-from-twelve angle falls inside [InStart, InStart + InSweep). */
	UFUNCTION(BlueprintPure, Category = "Ring Sector")
	static bool IsAngleInSweep(float InAngle, float InStartAngle, float InSweepAngle);

	/**
	 * Where the widget's rect is centred, in its own local space.
	 *
	 * Not the origin: local space is measured from the PIVOT, so a wedge someone re-pivoted would
	 * otherwise be tested against a circle centred off its own middle. Four lines against a silent
	 * misalignment nobody would think to look for.
	 */
	static FVector2D LocalRectCentre(const UDreamVisual* InVisual);
};
