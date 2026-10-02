// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FT_FaceRec_;

/** What colour data a glyph has, as far as DreamGUI draws it. */
enum class EDreamGlyphColorKind : uint8
{
	/** None: an outline glyph like any other, drawn from the field (and, small, from a coverage glyph). */
	None,
	/** A bitmap in an embedded strike: CBDT/CBLC or sbix. */
	Bitmap,
	/** COLRv0 layers, which FreeType composites into one premultiplied image. */
	Layers,
	/**
	 * Colour data DreamGUI does not draw: COLRv1 paints with no v0 layers, or SVG alone. The face does not count as having
	 * the glyph's code point (UDreamUIFontData_BaseObject::FaceHasCodepoint answers false), so the next face or the emoji
	 * data answers instead.
	 */
	Unsupported,
};

/** What a colour glyph is rasterized with. */
struct FDreamGlyphColorParams
{
	/** Pixels per em to produce: a size bucket (GetSizeBucket). A larger strike is downscaled to it; a smaller one is stored as it is. */
	int32 TargetPixelSize = 0;
	/**
	 * How far outside the glyph's own bitmap anything drawn from it samples, in em -- the underlay's reach. The bitmap is
	 * padded by that in its own texels (ReachEm x TexelsPerEm), rounded up, plus one texel.
	 */
	float ReachEm = 0.0f;
};

/**
 * A colour glyph, ready for the atlas. Every length is in texels of the stored bitmap, whose scale is TexelsPerEm: a
 * length times Size / TexelsPerEm is pixels at Size.
 */
struct FDreamGlyphColorResult
{
	EDreamGlyphColorKind Kind = EDreamGlyphColorKind::None;
	/** The stored bitmap, padding included. */
	int32 Width = 0;
	int32 Height = 0;
	/** The bitmap's left edge right of the glyph origin, and its top edge above the baseline, padding included. */
	float Left = 0.0f;
	float Top = 0.0f;
	/** Horizontal advance: a bitmap face's strike advance (horiAdvance), hmtx for COLR. */
	float Advance = 0.0f;
	/** Texels per em of the stored bitmap: TargetPixelSize, unless a strike smaller than it was stored as it is (its ppem then). */
	float TexelsPerEm = 0.0f;
	/** Width * Height * 4 bytes, rows top first: premultiplied B, G, R, A as FreeType gives them (PF_B8G8R8A8's order); padding (0,0,0,0). */
	TArray<uint8> Pixels;
};

/**
 * Colour glyphs for emoji: CBDT/CBLC and sbix strikes, and COLRv0 through FreeType's own layer compositing. A strike is
 * chosen as Slate chooses it and downscaled on the CPU with an exact area filter on the premultiplied bytes, never
 * enlarged. Pure and thread-agnostic, like FDreamGlyphCoverage. The size buckets and the padding are decided here and
 * nowhere else: the font keys its colour cache by GetSizeBucket and passes the bucket in.
 */
class DREAMGUI_API FDreamGlyphColor
{
public:
	/**
	 * What colour data a glyph of a face has, first match wins: a bitmap in a colour strike, COLRv0 layers, then a COLRv1
	 * paint or an SVG document (Unsupported). Loads nothing it does not have to; the face's size may change.
	 */
	static EDreamGlyphColorKind GetColorKind(FT_FaceRec_* Face, uint32 GlyphIndex);
	/** Rasterize one colour glyph. False for a glyph of kind None or Unsupported, or one that does not load. */
	static bool Rasterize(FT_FaceRec_* Face, uint32 GlyphIndex, const FDreamGlyphColorParams& Params, FDreamGlyphColorResult& Out);
	/**
	 * The size a colour glyph wanted at DevicePixelSize pixels per em is rasterized at, rounded up to a step: 1 px steps up
	 * to 32, 2 px up to 64, 4 px up to 128, 8 px up to 256, capped at 256, at least 1. A size within 1/64 px above a step
	 * counts as on it. The GPU shrinks the rest, by a step at most: 6% or less from 16 px up to the cap.
	 */
	static int32 GetSizeBucket(float DevicePixelSize);
	/** The strike to use for PixelSize: an exact match, else the smallest above it, else the largest (Slate's rule). INDEX_NONE for a face with none. */
	static int32 ChooseStrike(FT_FaceRec_* Face, float PixelSize);
	/**
	 * A strike face's advance for a glyph at PixelSize: the chosen strike's horiAdvance times PixelSize / its y_ppem, which
	 * is what the shaping font answers for such a face instead of hmtx. A strike that lacks the glyph gives way to the
	 * largest one that has it. False for a face without colour strikes (CBDT/sbix), or a glyph none of them has.
	 */
	static bool GetStrikeAdvance(FT_FaceRec_* Face, uint32 GlyphIndex, float PixelSize, float& OutAdvancePixels);
};
