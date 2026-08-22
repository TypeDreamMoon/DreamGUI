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
	/** Colour of every glyph that did not get one from a <color> tag. */
	FColor BaseColor = FColor::White;
	/**
	 * Lyric-style fill. Glyphs inside a segment carry that segment's progress and glow boost and
	 * their position across it (UV2.y 0..1, UV3.x progress, UV3.y glow boost); glyphs outside any
	 * segment are one run per line with FillProgress / GlowBoost.
	 */
	const TArray<struct FDreamTextFillSegment>* FillSegments = nullptr;
	float FillProgress = 1.0f;
	float GlowBoost = 0.0f;

	/**
	 * Multi-channel field (MTSDF) fonts. UV2.x carries DilateEm + 16 * Layer per glyph; quads grow into
	 * the field as far as the face / the effects reach; bold is a dilation of the regular glyph.
	 */
	bool bMultiChannelField = false;
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
 * look like: the glyph quad, the italic shear, the superscript lift, the underline and strikethrough
 * strips, which UV channel carries what. Runs from a cached display list, so it is cheap enough to
 * run every time the geometry is rebuilt.
 */
class DREAMGUI_API FDreamTextPainter
{
public:
	/**
	 * Appends nothing: the geometry is sized to exactly what the display list emits. OutCharProperties
	 * lists the emitted glyphs in order, which is the contract TextAnimation and pixel snapping read.
	 */
	static void Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
		FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties);
};
