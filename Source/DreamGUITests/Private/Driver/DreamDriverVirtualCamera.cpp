// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverVirtualCamera.h"

#include "Engine/LocalPlayer.h"
#include "Math/InverseRotationMatrix.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace DreamDriverVirtualCameraLocal
{
	/**
	 * The projection data ULocalPlayer::GetProjectionData would hand UDreamWorldSpaceRaycaster::GenerateRay
	 * for this view on this viewport, for a single player who owns the whole of it. The header lists the
	 * steps and what is left out; each one below is the engine's line, not a rewrite of it.
	 */
	bool BuildProjectionData(const FDreamDriverVirtualCamera& InCamera, FSceneViewProjectionData& OutProjectionData)
	{
		const FIntPoint Size = InCamera.ViewportSize;
		// GetProjectionData refuses a viewport with no area before it computes anything.
		if (Size.X <= 0 || Size.Y <= 0)
		{
			return false;
		}

		// The unconstrained rect: Origin (0,0) and Size (1,1) of a viewport that starts at its own
		// top-left corner, which is every single-player game viewport.
		OutProjectionData.SetViewRectangle(FIntRect(0, 0, Size.X, Size.Y));

		// The view matrix, kept apart from the view origin exactly as GetProjectionData keeps it: the
		// origin is added back after deprojection (see Deproject), which is what keeps a camera far
		// from the world origin from bending every ray.
		OutProjectionData.ViewOrigin = InCamera.View.Location;
		OutProjectionData.ViewRotationMatrix = FInverseRotationMatrix(InCamera.View.Rotation) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		// FMinimalViewInfo::CalculateProjectionMatrixGivenView, with the one thing it asks the FViewport
		// for -- CalculateViewExtents -- answered by the engine's own static form of it. The desired
		// aspect ratio of an FViewport is its size's (no viewport in the engine overrides it), so that is
		// what is passed.
		FMinimalViewInfo ViewInfo = InCamera.View;
		const float CropAspectRatio = (ViewInfo.AsymmetricCropFraction.X + ViewInfo.AsymmetricCropFraction.Y)
			/ (ViewInfo.AsymmetricCropFraction.Z + ViewInfo.AsymmetricCropFraction.W);
		const float AspectRatio = ViewInfo.AspectRatio * CropAspectRatio;
		const float DesiredAspectRatio = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
		const FIntRect ViewExtents = FViewport::CalculateViewExtents(AspectRatio, DesiredAspectRatio,
			OutProjectionData.GetViewRect(), Size);

		// The local player's own constraint, which is config (BaseEngine.ini sets MaintainYFOV) and so is
		// whatever the class default object says unless a project changed it. A view that carries its own
		// constraint overrides it inside the engine function, as it would for a live player.
		const ULocalPlayer* LocalPlayerDefaults = GetDefault<ULocalPlayer>();
		const TEnumAsByte<EAspectRatioAxisConstraint> AxisConstraint = LocalPlayerDefaults != nullptr
			? LocalPlayerDefaults->AspectRatioAxisConstraint
			: TEnumAsByte<EAspectRatioAxisConstraint>(AspectRatio_MaintainYFOV);
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(ViewInfo, AxisConstraint, ViewExtents, OutProjectionData);
		return true;
	}
}

bool FDreamDriverVirtualCamera::Deproject(const FVector2D& InPixel, FVector& OutOrigin, FVector& OutDirection) const
{
	using namespace DreamDriverVirtualCameraLocal;
	FSceneViewProjectionData ProjectionData;
	if (!BuildProjectionData(*this, ProjectionData))
	{
		return false;
	}
	// UDreamWorldSpaceRaycaster::GenerateRay, line for line: the view-projection WITHOUT the position,
	// inverted, deprojected through the constrained rect, and the origin added back last.
	const FMatrix ViewProjectionWithoutPosition = ProjectionData.ViewRotationMatrix * ProjectionData.ProjectionMatrix;
	const FMatrix InverseViewProjection = ViewProjectionWithoutPosition.InverseFast();
	FSceneView::DeprojectScreenToWorld(InPixel, ProjectionData.GetConstrainedViewRect(), InverseViewProjection, OutOrigin, OutDirection);
	OutOrigin += ProjectionData.ViewOrigin;
	return true;
}

TOptional<FVector2D> FDreamDriverVirtualCamera::Project(const FVector& InWorldPoint) const
{
	using namespace DreamDriverVirtualCameraLocal;
	FSceneViewProjectionData ProjectionData;
	if (!BuildProjectionData(*this, ProjectionData))
	{
		return TOptional<FVector2D>();
	}
	// Relative to the view origin first, for the same reason Deproject adds it back last: the two halves
	// of the round trip should lose precision in the same place, not in two different ones.
	const FVector FromEye = InWorldPoint - ProjectionData.ViewOrigin;

	// Depth along the view direction. The rotation matrix carries no translation, so this is the view
	// space position, and after the axis swap its Z is "how far in front of the eye".
	const FVector ViewSpace = ProjectionData.ViewRotationMatrix.TransformPosition(FromEye);
	if (ViewSpace.Z <= UE_KINDA_SMALL_NUMBER)
	{
		return TOptional<FVector2D>();
	}

	const FVector4 Clip = (ProjectionData.ViewRotationMatrix * ProjectionData.ProjectionMatrix)
		.TransformFVector4(FVector4(FromEye, 1.0));
	// A perspective W is the depth just tested; an orthographic W is 1. Neither is zero here, but a
	// projection matrix is data a caller can hand in through the view, so the division is still guarded.
	if (FMath::Abs(Clip.W) <= UE_SMALL_NUMBER)
	{
		return TOptional<FVector2D>();
	}
	const double NDCX = Clip.X / Clip.W;
	const double NDCY = Clip.Y / Clip.W;

	// FSceneView::DeprojectScreenToWorld's first four lines, run backwards: normalised within the
	// CONSTRAINED rect, and Y flipped, because NDC's Y points up and a pixel's points down.
	const FIntRect& ConstrainedRect = ProjectionData.GetConstrainedViewRect();
	const double NormalizedX = NDCX * 0.5 + 0.5;
	const double NormalizedY = 0.5 - NDCY * 0.5;
	return FVector2D(
		ConstrainedRect.Min.X + NormalizedX * ConstrainedRect.Width(),
		ConstrainedRect.Min.Y + NormalizedY * ConstrainedRect.Height());
}
