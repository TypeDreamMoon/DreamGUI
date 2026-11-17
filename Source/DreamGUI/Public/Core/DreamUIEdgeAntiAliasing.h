// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDreamUIGeometry;

/**
 * Anti-aliasing an element's outline through its own geometry: for the elements drawn as meshes, whose edges no shader
 * smooths -- polygons, shapes, rings, paths, lines, custom meshes, a round-cornered or radially filled image, and an
 * image, texture or sprite turned off the pixel grid. Text and RectBlock smooth theirs in the shader and do not come here.
 *
 * The outline is read off the mesh: an edge only one triangle uses, vertices at the same place taken as one, so a seam
 * of duplicated vertices inside the mesh is not an outline. Along it goes a strip one device pixel wide, centred on the
 * edge: the outline's vertices move half a pixel in, their texture coordinate and colour taken from the triangle they
 * now stand in, so the texture stays where it was on the surface; a transparent copy of each stands half a pixel out.
 * Coverage then runs from full to none across the pixel the edge crosses -- a pixel the edge cuts in half is drawn at
 * half -- while an edge lying on the pixel grid stays as sharp as it was: the pixel inside it is wholly within the
 * strip's opaque half and the pixel outside wholly beyond it. It is all vertex colour, so every material honours it.
 */
namespace DreamUIEdgeAntiAliasing
{
	struct FResult
	{
		/** The outline edges found, each of which got a strip. */
		int32 OutlineEdges = 0;
		/** Whether the geometry was changed. */
		bool bApplied = false;
	};

	/**
	 * Anti-aliases InOutGeometry's outline, InPixel being one device pixel in its local units. Leaves the geometry as it
	 * is when it cannot: no pixel, a mesh not flat on its local X = 0 plane, or one the strips would take past the
	 * vertex budget. A simple or nine-sliced quad stays one: its lines are kept first (FDreamUIGeometry::KeptQuadLines),
	 * which a clip underlay reads in place of the moved corners.
	 */
	DREAMGUI_API FResult Apply(FDreamUIGeometry& InOutGeometry, float InPixel);
}
