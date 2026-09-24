// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"   // FMinimalViewInfo

/**
 * A camera a headless world does not have: the view a player would see through, and the viewport it
 * is seen on.
 *
 * WHY IT EXISTS. A world-space pointer's ray comes from the player: UDreamWorldSpaceRaycaster::GenerateRay
 * asks the pointer's PlayerController for its LocalPlayer, the LocalPlayer for its projection data
 * (ULocalPlayer::GetProjectionData, which reads the camera manager's view and the viewport client's
 * FViewport), and deprojects the pointer's pixel through that. A headless rig has a PlayerController at
 * best and never a viewport, so the production ray is never produced and nothing world-space can be
 * pointed at. This struct is the missing half, held in the open: a view and a viewport size. The test
 * raycaster (UDreamDriverWorldSpaceRaycaster) makes its ray from it, and the driver's projection
 * (FDreamDriverProjection's camera overloads) turns world points into pixels through it. One object,
 * read by both ends, so "the pixel the driver aims at" and "the ray that pixel produces" are inverses
 * by construction -- the same trick the screen-space projection plays with the canvas's own matrix.
 *
 * THE SAME ARITHMETIC AS THE LOCAL PLAYER, STEP BY STEP. Deproject builds its FSceneViewProjectionData
 * the way ULocalPlayer::GetProjectionData does for a single, full-viewport player, then finishes the
 * way GenerateRay does:
 *   1. the unconstrained view rect is (0, 0, ViewportSize) -- a single player's Origin (0,0) and Size
 *      (1,1), on a viewport whose initial position is the origin;
 *   2. ViewOrigin = View.Location, ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) times the
 *      engine's axis swap (forward, right, up) -> (right, up, forward);
 *   3. the constrained rect is FViewport::CalculateViewExtents for the view's aspect ratio (times its
 *      asymmetric crop, as CalculateProjectionMatrixGivenView multiplies it) on a viewport whose
 *      desired aspect ratio is its own size's -- which is FViewport's, as no viewport overrides it;
 *   4. the projection matrix is FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle, the
 *      engine's own function, called with ULocalPlayer's configured AspectRatioAxisConstraint (read
 *      from its class default object; View.AspectRatioAxisConstraint overrides it, exactly as it does
 *      for a live player). Orthographic views get the engine's automatic planes the same way;
 *   5. the ray is FSceneView::DeprojectScreenToWorld through the constrained rect and the inverse of
 *      ViewRotationMatrix * ProjectionMatrix, with ViewOrigin added afterwards -- GenerateRay's own
 *      precision trick, reproduced rather than improved on.
 * What is NOT reproduced: stereo, split screen (a LocalPlayer whose Origin/Size is not the whole
 * viewport), r.ViewportTest, the editor's preview-platform aspect constraint, and scene view
 * extensions that rewrite the projection. None of those exist in a headless rig; a PIE test that
 * needs them should aim through the production raycaster instead.
 *
 * PIXELS. Y grows DOWNWARD from the top of the viewport, the convention the input module speaks and
 * FDreamDriverProjection documents. FSceneView::DeprojectScreenToWorld truncates the pixel to its
 * integer corner before it deprojects, so the ray for (640.7, 360.2) is the ray for (640, 360):
 * Project is continuous, Deproject is not, and a pixel handed from one to the other lands within one
 * pixel of where it was aimed -- which is also what a real mouse, whose positions are whole pixels,
 * gets.
 *
 * THE VIEW'S ASPECT RATIO. FMinimalViewInfo defaults AspectRatio to 4:3. Under the engine's default
 * axis constraint (MaintainYFOV) the vertical field of view is worked out from the horizontal FOV at
 * THAT aspect ratio, and the horizontal one then follows from the viewport's shape -- so a default
 * view with FOV 90 on a 16:9 viewport sees about 106 degrees across. That is what a PIE player with a
 * default camera manager sees too, which is why it is left alone here; a test that wants FOV to mean
 * "degrees across this viewport" sets AspectRatio to the viewport's, which DreamDriverWorld::MakeView
 * does.
 */
struct FDreamDriverVirtualCamera
{
	FMinimalViewInfo View;
	FIntPoint ViewportSize = FIntPoint(1280, 720);

	/**
	 * The ray under a viewport pixel (Y down from the top), built exactly as
	 * ULocalPlayer::GetProjectionData + UDreamWorldSpaceRaycaster::GenerateRay would build it.
	 *
	 * False only for a viewport with no area, which is the one case GetProjectionData refuses too.
	 * OutOrigin lies on the near plane, not at the eye, because that is where the engine starts it.
	 */
	bool Deproject(const FVector2D& InPixel, FVector& OutOrigin, FVector& OutDirection) const;

	/**
	 * Where a world point lands on the viewport, or unset if it is at or behind the eye.
	 *
	 * "Behind the eye" is measured along the view direction from View.Location, in both projection
	 * modes: a point there has no pixel a ray from this camera could come back through, only a
	 * mirrored one. A point in front of the eye but nearer than the near plane still gets its pixel;
	 * the ray through that pixel starts beyond it, so it cannot be hit -- which is the engine's
	 * behaviour as well, and the reason a panel should never be placed closer than ten centimetres.
	 */
	TOptional<FVector2D> Project(const FVector& InWorldPoint) const;
};
