// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/LexPerspective.h"

namespace LexPerspective
{
	namespace
	{
		/** Below this, an eye is in its own plane and the map would divide by nothing. */
		constexpr double DegenerateHeight = UE_KINDA_SMALL_NUMBER;
	}

	FMatrix MakeRemap(const FScope& Scope, const FVector& OuterEye)
	{
		const FVector Normal = Scope.PlaneNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return FMatrix::Identity;
		}
		// How far the inner eye stands off its own plane. This is the divisor, and it is the one
		// quantity that can legitimately be zero: an eye lying in the plane it looks along sees the
		// subtree edge-on and has no perspective to describe.
		const double EyeHeight = FVector::DotProduct(Normal, Scope.EyePosition - Scope.PlanePoint);
		if (FMath::Abs(EyeHeight) < DegenerateHeight)
		{
			return FMatrix::Identity;
		}
		const FVector EyeDelta = OuterEye - Scope.EyePosition;
		if (EyeDelta.IsNearlyZero())
		{
			return FMatrix::Identity;//already looking from the right place
		}

		// P(p) = p + EyeDelta * dot(Normal, p - PlanePoint) / EyeHeight
		//
		// A rank-one update, so only the depth direction is touched: a point sitting IN the plane
		// has a zero numerator and is returned bit for bit. That is what makes a subtree of flat
		// widgets cost nothing until something in it actually gains depth.
		const double PlaneOffset = FVector::DotProduct(Normal, Scope.PlanePoint);
		FMatrix Result = FMatrix::Identity;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				Result.M[Row][Column] += Normal[Row] * EyeDelta[Column] / EyeHeight;
			}
		}
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Result.M[3][Column] = -PlaneOffset * EyeDelta[Column] / EyeHeight;
		}
		return Result;
	}

	FMatrix ComposeRemap(TArrayView<const FScope> ScopesInnermostFirst, const FVector& CanvasEye)
	{
		FMatrix Result = FMatrix::Identity;
		for (int32 Index = 0; Index < ScopesInnermostFirst.Num(); ++Index)
		{
			// Each scope re-aims onto the one enclosing it, and only the last onto the canvas. The
			// hand-off is the whole of the nesting rule.
			const FVector OuterEye = ScopesInnermostFirst.IsValidIndex(Index + 1)
				? ScopesInnermostFirst[Index + 1].EyePosition
				: CanvasEye;
			Result = Result * MakeRemap(ScopesInnermostFirst[Index], OuterEye);
		}
		return Result;
	}

	bool ProjectOntoPlane(const FVector& Eye, const FVector& Point,
		const FVector& PlanePoint, const FVector& PlaneNormal, FVector& OutOnPlane)
	{
		const FVector Normal = PlaneNormal.GetSafeNormal();
		const FVector Direction = Point - Eye;
		const double Denominator = FVector::DotProduct(Normal, Direction);
		if (FMath::Abs(Denominator) < DegenerateHeight)
		{
			return false;//the sight line never reaches the plane
		}
		const double Alpha = FVector::DotProduct(Normal, PlanePoint - Eye) / Denominator;
		OutOnPlane = Eye + Direction * Alpha;
		return true;
	}
}
