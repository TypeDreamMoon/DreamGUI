// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextDisplayList.h"

class FDreamUIGeometry;

/** What the painter needs beyond the display list: the font's quad conventions and the canvas's. */
struct FDreamTextPaintParams
{
	/** tan(italic angle): how far the top edge of an italic quad leans right. */
	float ItalicSlope = 0.0f;
	/** The canvas asks for normals and tangents (SDF fonts do, for the tilt-aware smoothing). */
	bool bRequireNormalAndTangent = false;
	/** Colour of every glyph that did not get one from a <color> tag. Carries the render opacity already. */
	FColor BaseColor = FColor::White;
	/**
	 * Render opacity for glyphs that did get a colour from a <color> tag: the tag's alpha is what the
	 * author wrote, this is the hierarchy's fade. Keeping it here rather than baking it into the parsed
	 * colour is what lets a fade repaint without re-laying out.
	 */
	float RichTextTagOpacity = 1.0f;
	/**
	 * Lyric-style fill. Glyphs inside a segment carry that segment's progress and glow boost and
	 * their position across it (UV2.y 0..1, UV3.x progress, UV3.y glow boost); glyphs outside any
	 * segment are one run per line with FillProgress / GlowBoost.
	 */
	const TArray<struct FDreamTextFillSegment>* FillSegments = nullptr;
	float FillProgress = 1.0f;
	float GlowBoost = 0.0f;

	/**
	 * A drop shadow and an outline for a font that is NOT a distance field. A bitmap atlas holds the
	 * face and nothing else, so there is no field to dilate and no way to re-rasterize with an outline
	 * -- what there is, and what UMG's own ShadowOffset does, is to draw the glyphs again, offset, in
	 * another colour. The outline is that eight times around the glyph, which is the standard stand-in
	 * for a real one. Offsets and widths are in em, like every other length in FDreamTextStyle, and are
	 * ignored entirely when bDistanceField is set (the shader draws the real thing there). Every shadow
	 * is drawn before any outline and every outline before any face, as Slate draws them in whole-run
	 * passes, so one glyph's outline never lands on its neighbour's face.
	 */
	FColor BitmapShadowColor = FColor(0, 0, 0, 0);
	FVector2f BitmapShadowOffsetEm = FVector2f::ZeroVector;
	FColor BitmapOutlineColor = FColor(0, 0, 0, 0);
	float BitmapOutlineWidthEm = 0.0f;

	/** Colours that replace a rich-text tag's glyph colour at paint time (a hovered or pressed link): pairs of an index into DisplayList.CustomTags and the colour. Applied without laying out again. */
	const TArray<TPair<int32, FColor>>* TagColorOverrides = nullptr;

	/**
	 * Draw each character's underline and strikethrough as a piece of its own, inside that character's vertex range,
	 * so whatever animates the characters one by one moves, fades and reveals the strokes with them; a piece reaches
	 * across what follows its glyph with no piece of its own, a space or an emoji. Otherwise a stroke is one strip per
	 * run of matching decoration, across the spaces between words, as a browser draws it.
	 */
	bool bStrokesPerCharacter = false;

	/**
	 * Distance-field fonts (either kind). UV2.x carries DilateEm + 16 * Layer per glyph; quads grow into
	 * the field as far as the face / the effects reach; bold is a dilation of the regular glyph when
	 * BoldDilateEm is set.
	 */
	bool bDistanceField = false;
	/**
	 * Draw the effects (underlay, glow, outline) of every glyph in a first set of quads and the faces
	 * in a second, so a glyph's glow never lands on top of its neighbour's face. Doubles the quads, so
	 * only for texts whose style has effects.
	 */
	bool bSeparateEffectLayer = false;
	/** Synthetic bold as a dilation of the face, in em per side; 0 when the atlas bakes bold. */
	float BoldDilateEm = 0.0f;
	/** How far beyond the glyph's edge the face and the effects reach, in em (see FDreamTextStyle). */
	float FaceReachEm = 0.0f;
	float EffectReachEm = 0.0f;
	/** Field geometry for sizing the quads: texels per em, the spread, what the quads already include, a texel in UV. */
	float EmTexels = 0.0f;
	float FieldSpreadTexels = 0.0f;
	float QuadMarginTexels = 0.0f;
	float TexelToUV = 0.0f;
};

/**
 * Turns a display list into quads. The only place in the plugin that knows what a glyph's vertices
 * look like: the glyph quad, the italic shear, the underline and strikethrough strips, which UV
 * channel carries what. Runs from a cached display list, so it is cheap enough to run every time the
 * geometry is rebuilt.
 */
class DREAMGUI_API FDreamTextPainter
{
public:
	/**
	 * Appends nothing: the geometry is sized to exactly what the display list emits. OutCharProperties
	 * lists the emitted glyphs in order, which is the contract TextAnimation and pixel snapping read; a
	 * character's vertex range covers every copy of its quad, its triangle range the face copy. An
	 * underline or strikethrough is one strip per run of identical decoration, written after all the
	 * glyphs and belonging to no character -- or, with bStrokesPerCharacter, one piece per glyph inside
	 * its character's range.
	 */
	static void Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
		FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties);
};
