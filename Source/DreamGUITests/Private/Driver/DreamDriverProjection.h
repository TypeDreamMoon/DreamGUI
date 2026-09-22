// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamCanvas;
class UDreamWidget;

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
 * v1 handles ScreenSpaceOverlay and RenderTarget canvases. A world-space canvas is projected through
 * the player's camera rather than through the canvas's own virtual one, so its pixel is a fact about a
 * viewpoint that a headless fixture has not got; those come back unset rather than wrong.
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
};
