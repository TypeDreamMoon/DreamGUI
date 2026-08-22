// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FT_FaceRec_;

/** A glyph's multi-channel distance field, ready for the atlas. */
struct FDreamGlyphSdfResult
{
	int32 Width = 0;
	int32 Height = 0;
	/** Bitmap left edge relative to the glyph origin, in pixels (bearing minus spread). */
	float Left = 0.0f;
	/** Bitmap top edge above the baseline, in pixels. */
	float Top = 0.0f;
	/** Horizontal advance in pixels. */
	float Advance = 0.0f;
	/** Width * Height BGRA pixels: RGB the multi-channel field, A the true signed distance. 0.5 is the edge. */
	TArray<uint8> Pixels;
};

/**
 * Distance fields from outlines, through msdfgen (the engine's copy, the one Slate's own SDF text
 * uses). MTSDF rather than MSDF: the three colour channels give crisp corners through their median,
 * and the alpha channel carries the plain distance that effects -- blur, glow, shadows, outlines --
 * can widen into.
 */
class FDreamGlyphSdf
{
public:
	/**
	 * @param Face          FreeType face; the glyph's outline is loaded unscaled and unhinted.
	 * @param GlyphIndex    Which glyph.
	 * @param PixelsPerEm   Rasterization size in pixels per em.
	 * @param SpreadPixels  Distance range on each side of the edge, in pixels. 0.5 +/- this maps to 1/0.
	 * @param BoldPixels    Synthetic emboldening, in pixels; 0 for none.
	 */
	static bool GenerateMTSDF(FT_FaceRec_* Face, uint32 GlyphIndex, float PixelsPerEm, float SpreadPixels, float BoldPixels, FDreamGlyphSdfResult& Out);
};
