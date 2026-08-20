// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamCanvasViewFit.h"

namespace DreamCanvasViewFit
{
	namespace
	{
		struct FSample
		{
			FVector2D NDC = FVector2D::ZeroVector;
			double W = 0.0;
		};

		bool ProjectSample(const FMatrix& InWorldToClip, const FVector& InPoint, FSample& OutSample)
		{
			const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(InPoint, 1.0));
			if (FMath::Abs(Clip.W) <= UE_KINDA_SMALL_NUMBER)
			{
				return false;//on the eye plane; no image exists
			}
			if (Clip.W < 0.0)
			{
				// Behind the eye. Refused rather than divided through, because the division would
				// produce a mirrored position that looks like a real answer.
				return false;
			}
			OutSample.NDC = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
			OutSample.W = Clip.W;
			return true;
		}
	}

	bool BuildClipCorrection(
		const FMatrix& InCanvasWorldToClip,
		const FMatrix& InReferenceWorldToClip,
		const FVector& InPlaneOrigin,
		const FVector& InPlaneRight,
		const FVector& InPlaneUp,
		double InSampleLength,
		FMatrix& OutCorrection)
	{
		if (InSampleLength <= UE_KINDA_SMALL_NUMBER
			|| InPlaneRight.IsNearlyZero()
			|| InPlaneUp.IsNearlyZero())
		{
			return false;
		}

		const FVector Points[3] = {
			InPlaneOrigin,
			InPlaneOrigin + InPlaneRight.GetSafeNormal() * InSampleLength,
			InPlaneOrigin + InPlaneUp.GetSafeNormal() * InSampleLength,
		};
		FSample Canvas[3];
		FSample Reference[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!ProjectSample(InCanvasWorldToClip, Points[Index], Canvas[Index]))return false;
			if (!ProjectSample(InReferenceWorldToClip, Points[Index], Reference[Index]))return false;
		}

		// An eye on the plane's normal gives every in-plane point the same w, which is what makes
		// the plane map affinely into clip space and so what makes a scale-and-offset able to pin
		// it. A tilted eye -- bOverrideViewRotation can produce one -- breaks that, and no
		// correction of this shape exists. Detected relatively, since w is a distance and its
		// magnitude depends entirely on how big the canvas is.
		for (int32 Index = 1; Index < 3; ++Index)
		{
			if (!FMath::IsNearlyEqual(Canvas[Index].W, Canvas[0].W, FMath::Abs(Canvas[0].W) * 1e-4))
			{
				return false;
			}
		}

		const FVector2D CanvasSpan(Canvas[1].NDC.X - Canvas[0].NDC.X, Canvas[2].NDC.Y - Canvas[0].NDC.Y);
		const FVector2D ReferenceSpan(Reference[1].NDC.X - Reference[0].NDC.X, Reference[2].NDC.Y - Reference[0].NDC.Y);
		if (FMath::Abs(CanvasSpan.X) <= UE_SMALL_NUMBER || FMath::Abs(CanvasSpan.Y) <= UE_SMALL_NUMBER)
		{
			return false;//the plane projects to a line or a point under the canvas
		}

		// Independently per axis. One shared scalar would be wrong whenever the panel's aspect
		// differs from the canvas's -- the canvas bakes its own aspect into the projection while
		// the orthographic path applies the same zoom to both axes -- and that is the normal case,
		// not an edge case.
		const double ScaleX = ReferenceSpan.X / CanvasSpan.X;
		const double ScaleY = ReferenceSpan.Y / CanvasSpan.Y;
		const double OffsetX = Reference[0].NDC.X - ScaleX * Canvas[0].NDC.X;
		const double OffsetY = Reference[0].NDC.Y - ScaleY * Canvas[0].NDC.Y;

		OutCorrection = FMatrix(
			FPlane(ScaleX, 0.0, 0.0, 0.0),
			FPlane(0.0, ScaleY, 0.0, 0.0),
			FPlane(0.0, 0.0, 1.0, 0.0),
			FPlane(OffsetX, OffsetY, 0.0, 1.0));
		return true;
	}

	bool IsCanvasViewUsable(double InEyeToPlaneDistance, double InNearClipPlane, double InFarClipPlane)
	{
		return InEyeToPlaneDistance > InNearClipPlane && InEyeToPlaneDistance < InFarClipPlane;
	}
}
