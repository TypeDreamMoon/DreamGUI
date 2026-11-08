// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamNiagaraParticles.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "DreamUIRender/DreamUIMeshVertex.h"

/**
 * Where a segment's vertices are written: arrays kept from frame to frame (a section's), grown by tiers and never shrunk,
 * so that a segment whose particle count stays in its tier writes in place and its buffers on the GPU are updated rather
 * than made again.
 */
struct FDreamNiagaraMeshTarget
{
	TArray<FDreamUIMeshVertex>* Vertices = nullptr;
	TArray<FDreamUIMeshIndex>* Indices = nullptr;
	/** Written: how many of each the segment draws. */
	int32 NumVertices = 0;
	int32 NumIndices = 0;
	/** Written: the arrays grew, so the section's buffers must be made again. */
	bool bGrew = false;
	/** Written: what the vertices cover, on the canvas. Invalid when nothing is drawn. */
	FBox Bounds = FBox(ForceInit);
	/** Written: particles left out because one draw call cannot index more vertices. */
	int32 NumDropped = 0;
};

/** What every vertex of a segment shares. */
struct FDreamNiagaraVertexCommon
{
	/** The widget property record's row (UV1.x). */
	float WidgetRecord = 0.0f;
	/** The widget's colour, every parent's included (linear): each particle's is multiplied by it. */
	FLinearColor Tint = FLinearColor::White;
};

/**
 * Particles into vertices: sprites as quads, ribbons as strips, written into a segment's arrays (DreamNiagaraVertexLayout.h
 * says how a vertex is laid out). The work over the particles runs across worker threads when there are enough of them.
 * Nothing here reads a UObject.
 */
class DREAMGUINIAGARA_API FDreamNiagaraMeshBuilder
{
public:
	/** The capacity an array is grown to, to hold InNeeded: 64 at the least, then the next power of two. */
	static int32 GrowCapacity(int32 InNeeded);

	/** The order sprites are drawn in under InMode: indices into InParticles, first drawn first. */
	static void SortSprites(TConstArrayView<FDreamNiagaraSpriteParticle> InParticles, EDreamNiagaraSortMode InMode, TArray<int32>& OutOrder);

	/** A frame's texture coordinates on a sheet of InSubImageSize, for a coordinate InUV (0 to 1) across the sprite, as Niagara works them out. */
	static FVector2f SubImageUV(float InFrame, const FIntPoint& InSubImageSize, const FVector2f& InUV);

	static void BuildSprites(TConstArrayView<FDreamNiagaraSpriteParticle> InParticles, const FDreamNiagaraSpriteSettings& InSettings,
		const FDreamNiagaraBuildSpace& InSpace, const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget);

	/** The points of each ribbon, in drawing order: grouped by key (ascending), each group by link order. */
	static void GroupRibbons(TConstArrayView<FDreamNiagaraRibbonParticle> InParticles, int32 InMaxNumRibbons, TArray<TArray<int32>>& OutRibbons);

	/** How many pieces the segment from point InIndex to the next is cut into (1 for a straight one). */
	static int32 TessellationOf(TConstArrayView<FVector2f> InPoints, int32 InIndex, const FDreamNiagaraRibbonSettings& InSettings);

	/** A point of the curve through InPoints between point InIndex and the next, InT of the way (cardinal spline of the settings' tension). */
	static FVector2f CurvePoint(TConstArrayView<FVector2f> InPoints, int32 InIndex, float InT, float InTension);

	static void BuildRibbons(TConstArrayView<FDreamNiagaraRibbonParticle> InParticles, const FDreamNiagaraRibbonSettings& InSettings,
		const FDreamNiagaraBuildSpace& InSpace, const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget);

	/** A segment with nothing to draw: one triangle of no area, so that the section keeps its buffers. */
	static void BuildEmpty(const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget);

	/** The most sprites one segment draws: the vertices one draw call can index, four to a sprite. */
	static int32 MaxSprites();
};
