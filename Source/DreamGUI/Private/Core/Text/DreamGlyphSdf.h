// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FT_FaceRec_;
struct FT_Outline_;

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
 * Distance fields from outlines, through msdfgen -- the plugin's own generated copy of it, since a
 * launcher engine ships none (ThirdParty/README.md). MTSDF rather than MSDF: the three colour channels
 * give crisp corners through their median, and the alpha channel carries the plain distance that
 * effects -- blur, glow, shadows, outlines -- can widen into.
 */
class DREAMGUI_API FDreamGlyphSdf
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

	/**
	 * The same field from an outline already loaded, in font units (FT_LOAD_NO_SCALE): what GenerateMTSDF does once it has
	 * loaded the glyph, for a caller -- a test, say -- that has an outline of its own. Emboldening modifies the outline in
	 * place. Advance is left at zero; it is the glyph's business, not the outline's.
	 * @param UnitsToPixels  Pixels per font unit at the atlas's sample size.
	 */
	static bool GenerateMTSDFFromOutline(FT_Outline_* Outline, double UnitsToPixels, float SpreadPixels, float BoldPixels, FDreamGlyphSdfResult& Out);
};
