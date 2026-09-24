// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamCanvas;
class UDreamUIRenderTargetGeometrySource;
class UDreamWidget;
struct FDreamDriverVirtualCamera;

/**
 * Where a widget is, in the pixels the pointer is measured in.
 *
 * This is the inverse of UDreamScreenSpaceRaycaster::GenerateRay, and it is written as the inverse on
 * purpose: it reads the SAME matrix (UDreamCanvas::GetViewProjectionMatrix) and the SAME viewport size
 * (UDreamCanvas::GetViewportSize) that the raycaster deprojects with, so a pixel this hands back is by
 * construction a pixel whose ray comes back to the point it came from. Anything that recomputed the
 * projection from the canvas's parts instead would be a second implementation of the same arithmetic,
 * and the two would drift.
 *
 * WITH A CAMERA. The overloads that take an FDreamDriverVirtualCamera answer for the two kinds of
 * canvas that are seen through a player's eye rather than through their own:
 *  - a WORLD-SPACE canvas (WorldSpace or WorldSpace_DreamUI): the widget's point is already a world
 *    point, and the camera projects it. The camera is the one UDreamDriverWorldSpaceRaycaster makes its
 *    rays from, so this is the same "inverse of the raycaster" as above, one raycaster over.
 *  - a RENDER-TARGET canvas that a UDreamUIRenderTargetGeometrySource in the same world is showing: the
 *    widget's point goes through the canvas's own view-projection to the UV that
 *    UDreamUIRenderTargetInteraction would deproject it back from, the UV to the point of the surface's
 *    mesh that carries it, and that world point through the camera. Plane and cylinder surfaces are
 *    mapped (both are the geometry source's own triangle list, searched by texture coordinate); a
 *    StaticMesh surface has no pixel here, because its UVs live in render data this module cannot read.
 * Everything else is answered exactly as the camera-less versions answer it: a screen-space canvas by
 * its own matrix, a render-target canvas that no surface shows by its own matrix in the target's
 * pixels, and a world-space canvas with a null camera not at all. Passing null is the camera-less call.
 *
 * THE Y AXIS. The raycaster turns a pointer position into a view point by dividing by the viewport
 * size and then flipping Y -- `mousePos01.Y = 1.0f - mousePos01.Y` -- before deprojecting. So the
 * pointer's Y is measured DOWNWARD FROM THE TOP of the viewport, while NDC's Y points up. Every
 * function here ends with that same flip, which is why a pixel from here can be handed straight to
 * UDreamDriverInputModule::MoveTo.
 *
 * Note this is NOT the convention of UDreamCanvas::Project3DToScreen, which returns Y upward from the
 * bottom and additionally divides by the canvas scale to answer in canvas units. That function answers
 * "where on the canvas", this one answers "which pixel does the pointer have to be at"; they are
 * different questions and mixing them up puts the cursor at 720 minus where it should be.
 *
 * Without a camera only ScreenSpaceOverlay and RenderTarget canvases have pixels. A world-space canvas
 * is projected through the player's camera rather than through the canvas's own virtual one, so its
 * pixel is a fact about a viewpoint; with no camera to say what that viewpoint is, those come back
 * unset rather than wrong.
 */
class FDreamDriverProjection
{
public:
	/** Whether this root canvas is one whose own view-projection matrix decides where its pixels are. */
	static bool IsProjectableCanvas(const UDreamCanvas* InRootCanvas);

	/** A world point through a canvas's view-projection, as a viewport pixel. Unset if it is behind the eye. */
	static TOptional<FVector2D> WorldPointToPixel(const UDreamCanvas* InRootCanvas, const FVector& InWorldPoint);

	/**
	 * A point in the widget's own 2D space -- X rightward, Y upward, origin at the pivot, the units
	 * GetLocalSpaceLeft and friends answer in -- as a viewport pixel.
	 */
	static TOptional<FVector2D> WidgetLocalPointToPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint);

	/**
	 * The axis-aligned pixel box that contains the widget's four corners.
	 *
	 * Axis-aligned, so for a rotated widget it is a bound and not the shape: its corners are not on
	 * the widget. Use WidgetCentrePixel for a pixel that is guaranteed to be on it.
	 */
	static TOptional<FBox2D> WidgetToPixelRect(const UDreamWidget* InWidget);

	/**
	 * The pixel the widget's own centre lands on -- the projection of the centre, not the centre of
	 * the projected rect. The two agree for a widget lying flat in the canvas plane and disagree for
	 * one that is rotated under a perspective canvas (the default projection is Perspective), and it
	 * is this one that a ray comes back to the widget from, which is the whole point of aiming here.
	 */
	static TOptional<FVector2D> WidgetCentrePixel(const UDreamWidget* InWidget);

	/** WidgetLocalPointToPixel through a player's eye when there is one; see the class comment. InCamera null is the overload above. */
	static TOptional<FVector2D> WidgetLocalPointToPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint, const FDreamDriverVirtualCamera* InCamera);
	/** WidgetToPixelRect through a player's eye when there is one. A corner at or behind the eye leaves the whole rect unset, as it does without one. */
	static TOptional<FBox2D> WidgetToPixelRect(const UDreamWidget* InWidget, const FDreamDriverVirtualCamera* InCamera);
	/** WidgetCentrePixel through a player's eye when there is one. */
	static TOptional<FVector2D> WidgetCentrePixel(const UDreamWidget* InWidget, const FDreamDriverVirtualCamera* InCamera);

	/**
	 * A world point through a canvas's view-projection, as the canvas's VIEW POINT: NDC scaled to 0..1,
	 * with Y UP. Unset behind the canvas's eye.
	 *
	 * Not a pixel, and deliberately not flipped: this is the space UDreamScreenSpaceRaycaster::
	 * DeprojectViewPointToWorld deprojects from, and so the space UDreamUIRenderTargetInteraction hands
	 * it a surface's hit UV in -- (0,0) the bottom left of the canvas, (1,1) its top right.
	 */
	static TOptional<FVector2D> WorldPointToViewPoint01(const UDreamCanvas* InRootCanvas, const FVector& InWorldPoint);

	/**
	 * The render-target geometry source in the canvas's world that is showing this canvas, or null.
	 *
	 * Asked of every registered source in that world, through its own GetCanvas -- which is the answer
	 * UDreamUIRenderTargetInteraction gets too. A source with neither a canvas nor a presenter logs its
	 * usual warning when asked; the rig's own never do, because they are given their canvas first.
	 */
	static const UDreamUIRenderTargetGeometrySource* FindSurfaceShowing(const UDreamCanvas* InRenderTargetCanvas);

	/**
	 * The world point on a render-target surface that shows the target's UV InUV, in the convention
	 * UDreamUIRenderTargetGeometrySource::LineTraceHitUV answers in (V up from the bottom edge), or
	 * unset for a UV the surface does not carry or a surface kind that cannot be mapped (StaticMesh).
	 *
	 * Found on the surface's own triangles: the one whose texture coordinates contain (U, 1 - V) --
	 * the mesh stores V down the texture -- and the position interpolated there. For a plane that is
	 * exactly LineTraceHitUV run backwards; for a cylinder it is the chord LineTraceHitUV measures along.
	 */
	static TOptional<FVector> RenderTargetUVToSurfacePoint(const UDreamUIRenderTargetGeometrySource* InSurface, const FVector2D& InUV);
};
