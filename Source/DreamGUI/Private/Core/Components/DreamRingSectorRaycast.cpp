// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamRingSectorRaycast.h"

#include "Core/Components/DreamWidget.h"

float UDreamRingSectorRaycast::AngleOfLocalPoint(const FVector2D& InLocalPoint)
{
	// atan2(right, up): zero straight up, growing toward the right -- clockwise from twelve, which
	// is the family's convention. Swapping the arguments of the usual atan2(y, x) IS the rotation;
	// there is no separate correction anywhere, and there should not be one.
	const float Degrees = FMath::RadiansToDegrees(
		FMath::Atan2(static_cast<float>(InLocalPoint.X), static_cast<float>(InLocalPoint.Y)));
	// Into [0, 360) so a sweep test never has to reason about a negative angle.
	return FMath::Fmod(Degrees + 360.0f, 360.0f);
}

bool UDreamRingSectorRaycast::IsAngleInSweep(float InAngle, float InStartAngle, float InSweepAngle)
{
	if (InSweepAngle <= 0.0f)
	{
		return false;
	}
	if (InSweepAngle >= 360.0f)
	{
		return true;
	}
	// The distance travelled clockwise from the start, which wraps for free: a slice running from
	// 350 to 20 is one comparison here and two special cases in every other spelling of this test.
	const float Delta = FMath::Fmod(InAngle - InStartAngle + 720.0f, 360.0f);
	return Delta < InSweepAngle;
}

FVector2D UDreamRingSectorRaycast::LocalRectCentre(const UDreamVisual* InVisual)
{
	const UDreamWidget* Widget = InVisual != nullptr ? InVisual->GetWidget() : nullptr;
	if (Widget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	// Local space runs from -Pivot*Size to (1 - Pivot)*Size on each axis, so the middle sits this far
	// from the origin. Zero for the centred pivot the ring menu builds its wedges with.
	const FVector2D Pivot = Widget->GetPivot();
	return FVector2D(
		(0.5 - Pivot.X) * Widget->GetWidth(),
		(0.5 - Pivot.Y) * Widget->GetHeight());
}

bool UDreamRingSectorRaycast::Raycast(const UDreamVisual* InVisual, const FVector& InLocalSpaceRayStart,
	const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal) const
{
	// The caller has already established that the two ends straddle the widget's plane, so this
	// intersection exists. Same call the rect trace makes, for the same reason: the plane is X = 0.
	const FVector Plane = FMath::LinePlaneIntersection(
		InLocalSpaceRayStart, InLocalSpaceRayEnd, FVector::ZeroVector, FVector(1, 0, 0));

	const FVector2D Centre = LocalRectCentre(InVisual);
	const FVector2D FromCentre(Plane.Y - Centre.X, Plane.Z - Centre.Y);
	const float Radius = static_cast<float>(FromCentre.Size());

	if (Radius < InnerRadius)
	{
		return false;
	}
	// Zero or less is unbounded: a slice with no far edge, which is what makes a flick of the mouse
	// far outside the ring still pick an item.
	if (OuterRadius > 0.0f && Radius > OuterRadius)
	{
		return false;
	}
	// Dead centre has no angle at all -- atan2(0,0) is zero, an answer that would hand the wedge
	// containing twelve o'clock a hit it did not earn. Only reachable with InnerRadius at zero.
	if (Radius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	if (!IsAngleInSweep(AngleOfLocalPoint(FromCentre), StartAngle, SweepAngle))
	{
		return false;
	}

	OutHitPoint = Plane;
	// The plane's own normal, in local space. The caller transforms it out to world.
	OutHitNormal = FVector(1, 0, 0);
	return true;
}
