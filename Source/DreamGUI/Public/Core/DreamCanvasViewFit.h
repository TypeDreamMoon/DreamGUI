// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

/**
 * Fitting a canvas's own projection into an editor viewport that is zoomed and panned.
 *
 * The designer's 2D view is orthographic, and an orthographic projection has no perspective
 * divide -- so a widget pushed away in depth keeps exactly its laid-out size, and a declared
 * Perspective cannot show itself there however the camera is placed. Showing it means the viewport
 * has to project through the canvas's own virtual camera, which is what play projects through.
 *
 * Doing that naively throws away the viewport's zoom and pan, because they live in the editor
 * camera that has just been replaced. Putting them back by MOVING the substituted eye would be
 * wrong twice over: the foreshortening would then belong to neither the author's intent nor the
 * shipped image, and the whole point was to show the shipped one. So zoom and pan move to where
 * they belong for a design surface -- into image space, as a scale and offset applied to clip
 * coordinates AFTER the canvas has projected.
 *
 * The invariant that makes this safe to switch on: EVERY POINT IN THE CANVAS PLANE KEEPS EXACTLY
 * THE PIXEL IT HAS TODAY. Only content off that plane moves. That holds because at the canvas's
 * own eye distance the canvas rect maps to the NDC square on both axes, so an in-plane point
 * projects to the same normalized position under either projection; the correction below then
 * lines that square up with wherever the orthographic view was already putting it. Alignment work,
 * the resize handles, the guides and the resolution rects are all in-plane, so none of them move.
 *
 * The correction is SOLVED BY SAMPLING rather than derived in closed form, which is a deliberate
 * choice and not laziness. A closed form has to name the eye distance, and the canvas has three
 * ways to make any named distance a lie: bOverrideFovAngle (which GetProjectionMatrix honours and
 * CalculateDistanceToCamera does not), bOverrideViewLocation, and bOverrideProjectionMatrix.
 * Sampling asks the two matrices what they actually do, so it is right in all of those cases, and
 * it works unchanged when the canvas is orthographic. It also never touches pixels, only
 * normalized coordinates, which keeps DPI scaling out of the arithmetic entirely.
 */
namespace DreamCanvasViewFit
{
	/**
	 * Solve the clip-space correction that makes InCanvasWorldToClip agree with InReferenceWorldToClip
	 * for every point in the sampled plane, and disagree everywhere else exactly as perspective
	 * requires.
	 *
	 * The result is meant to be applied on the RIGHT of the canvas projection -- CanvasProj *
	 * Correction -- because UE composes row vectors, so this must act on clip coordinates rather
	 * than on view space. Its offsets sit in the last row, where they multiply w; putting them in
	 * the third row would multiply them by z instead, which under a reversed-Z projection is not w
	 * and goes wrong in a way that only depth-bearing content reveals.
	 *
	 * Returns false, and leaves OutCorrection untouched, when no such correction exists: a sample
	 * at or behind either eye, a degenerate plane basis, or an eye aimed off the plane normal. That
	 * last case is real -- bOverrideViewRotation can do it -- and it is not an approximation
	 * failure: with a tilted eye the plane no longer maps affinely into clip space, so no scale and
	 * offset can pin it, and the caller must fall back rather than ship something plausible.
	 *
	 * @param InSampleLength	How far along each basis vector to sample. Any non-trivial length
	 *							works; the plane's own half-extents are the natural choice.
	 */
	DREAMGUI_API bool BuildClipCorrection(
		const FMatrix& InCanvasWorldToClip,
		const FMatrix& InReferenceWorldToClip,
		const FVector& InPlaneOrigin,
		const FVector& InPlaneRight,
		const FVector& InPlaneUp,
		double InSampleLength,
		FMatrix& OutCorrection);

	/**
	 * Whether a canvas's own projection can see its own plane at all.
	 *
	 * The canvas builds a FINITE far plane, unlike the editor's perspective path, and its eye
	 * distance grows with the canvas width: Width * 0.5 / tan(FOV/2). A wide enough canvas puts its
	 * own contents past its own far plane, at which point the viewport goes black with nothing
	 * logged. Cheap to check, and the failure it prevents is silent.
	 */
	DREAMGUI_API bool IsCanvasViewUsable(double InEyeToPlaneDistance, double InNearClipPlane, double InFarClipPlane);
}
