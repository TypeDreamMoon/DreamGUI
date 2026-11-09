// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "DreamMeshDeform.generated.h"

class FDreamUIGeometry;

/**
 * The warp styles of the image editors' text warp, by the names they have there and in that order, so that a document's
 * warp maps by name. Each bends the box the mesh sits in as its comment says; Bend runs -100 to 100 and turns each the
 * other way when negative. The formulas are this plugin's own (FDreamMeshDeform::WarpPoint): they look like the
 * editors' styles without being the same to the pixel.
 */
UENUM(BlueprintType)
enum class EDreamWarpStyle : uint8
{
	None,
	/** Bent along a circle: the top and bottom become arcs about one centre, the sides turn to point at it. */
	Arc,
	/** The bottom edge bows down, the top stays straight. */
	ArcLower,
	/** The top edge bows up, the bottom stays straight. */
	ArcUpper,
	/** Both edges bow up together; every column keeps its height and stays upright. */
	Arch,
	/** Taller in the middle: the top bows up and the bottom down. */
	Bulge,
	/** The bottom edge rises in the middle and draws in, like a shell's ribs meeting below. */
	ShellLower,
	/** The top edge sinks in the middle and draws in, like a shell's ribs meeting above. */
	ShellUpper,
	/** One wave along the length, the whole height moving together. Phase moves it. */
	Flag,
	/** A wave whose phase changes from the bottom to the top, so the height swells and narrows along it. Phase moves it. */
	Wave,
	/** A fish's outline: a swollen head on the left, a narrow waist, a tail at the right end. */
	Fish,
	/** Rises from the left end to the right in an S, columns upright. */
	Rise,
	/** Magnified in the middle, as through a lens, the ellipse inside the box holding the effect. */
	Fisheye,
	/** Puffed out: the middle of each side pushed outward, the corners kept. */
	Inflate,
	/** Pinched across in the middle rows and stretched up in the middle columns. */
	Squeeze,
	/** Swirled about the middle: turned most at the centre, the ends held, so the length runs through in an S. */
	Twist,
};

/** Which way along the box a warp style runs. */
UENUM(BlueprintType)
enum class EDreamWarpOrientation : uint8
{
	/** Along the box's width. */
	Horizontal,
	/** Along its height, as if the box were turned a quarter turn clockwise before the warp and back after it. */
	Vertical,
};

/** The box a warp bends. */
UENUM(BlueprintType)
enum class EDreamWarpBounds : uint8
{
	/** The box around what the visual draws: a text's glyphs, a rect block with its shadow. */
	Geometry,
	/** The widget's own rect. */
	Widget,
};

/**
 * The arithmetic of the deforming modifiers, apart from any object so that it can be tested by itself: the warp styles,
 * the Bezier lattice, and the subdivision that gives a mesh the vertices to bend. Points are in a widget's local plane:
 * x to the right, y up.
 */
class DREAMGUIEXTENSIONS_API FDreamMeshDeform
{
public:
	struct DREAMGUIEXTENSIONS_API FStyle
	{
		EDreamWarpStyle Style = EDreamWarpStyle::None;
		bool bVertical = false;
		/** -1 to 1: the editors' percentages over 100. */
		float Bend = 0.0f;
		/** -1 to 1. Positive makes the right end the nearer, larger one. */
		float HorizontalDistortion = 0.0f;
		/** -1 to 1. Positive makes the top the nearer, larger one. */
		float VerticalDistortion = 0.0f;
		/** Turns of the waves of Flag and Wave; nothing to the others. */
		float Phase = 0.0f;

		/** Moves no point at all, whatever the box. */
		bool IsIdentity() const;
	};

	/** InPoint bent by InStyle, for a box at InCentre of InHalfSize. */
	static FVector2f WarpPoint(const FStyle& InStyle, FVector2f InPoint, FVector2f InCentre, FVector2f InHalfSize);

	/** The Bernstein polynomial of InDegree (0 to 4) and InIndex at InT: the weight of one control point along one axis. */
	static float Bernstein(int32 InDegree, int32 InIndex, float InT);
	/**
	 * InPoint moved by a Bezier lattice of InColumns by InRows control points (2 to 5 each) over the box InMin..InMax.
	 * InPoints go row by row from the top, each row from the left, in the box's own measure: 0,0 its top left corner, 1,1
	 * its bottom right, as an image's pixels go. The rest lattice (MakeRestLattice) moves nothing.
	 */
	static FVector2f EvaluateLattice(TConstArrayView<FVector2D> InPoints, int32 InColumns, int32 InRows, FVector2f InPoint, FVector2f InMin, FVector2f InMax);
	/** InColumns by InRows points evenly over the box, in EvaluateLattice's order and measure. */
	static void MakeRestLattice(int32 InColumns, int32 InRows, TArray<FVector2D>& OutPoints);
	/** The lattice sizes EvaluateLattice takes. */
	static constexpr int32 MinLatticeSize = 2;
	static constexpr int32 MaxLatticeSize = 5;

	struct FSubdivision
	{
		/** The longest side a cut piece may have, in local units. */
		float CellLength = 8.0f;
		/** The most vertices the mesh may have once cut; the cells grow until it fits, and nothing is cut if they cannot. */
		int32 MaxVertices = 8192;
		/** Cut across the x axis only: columns, for a mesh bent along its length. */
		bool bAcrossOnly = false;
	};
	/**
	 * Cuts InOutGeometry finer, so that a deformation has vertices to move. Every vertex there was keeps its index (a
	 * text's character ranges name them) and the new ones go after them; the triangles are made anew. A quad that is an
	 * axis-aligned rectangle -- every rect writer's, every glyph's -- is cut along one grid of lines shared by the whole
	 * mesh, InCellLength apart, so neighbours cut their shared edge at the same points; any other triangle is cut into
	 * a regular triangular grid, with the same number of pieces for each such triangle of the mesh.
	 *
	 * A new vertex takes every attribute -- local position, normal and tangent, colour, the four texture coordinates and
	 * UV4 -- as the point's weights over its quad's or triangle's corners give it, which is what drawing the coarse mesh
	 * interpolated across its faces: so a rect block's distance field and a glyph's bend with the mesh. A value all the
	 * corners share is copied as it is rather than blended, since several are codes read back bit for bit (a text quad's
	 * code, a rect block's data row). The packed tangents of the vertex buffer come from the first corner.
	 *
	 * Returns the number of vertices added. OutSourceVertex, when given, gets for every vertex the index of the original
	 * vertex it was made from (itself, for an original).
	 */
	static int32 Subdivide(FDreamUIGeometry& InOutGeometry, const FSubdivision& InOptions, TArray<int32>* OutSourceVertex = nullptr);
	/** The box around InGeometry's local positions; an empty box when it has none. */
	static FBox2f MeasureBounds(const FDreamUIGeometry& InGeometry);
	/**
	 * Moves every local position of InOutGeometry through InWarp, and turns its tangent with the warp's local stretch
	 * (measured a short step along it), so that a lit or normal-mapped element shades along the bent surface.
	 */
	static void Deform(FDreamUIGeometry& InOutGeometry, TFunctionRef<FVector2f(FVector2f)> InWarp);
};
