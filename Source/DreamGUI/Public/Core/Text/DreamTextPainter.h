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
	/** SDF fonts store fontSize * objectScale in UV2.x for the material's anti-aliasing. Bitmap fonts leave UV2 alone. */
	bool bWriteFontScaleToUV2 = false;
	float FontScaleMultiplier = 0.0f;
	/** The canvas asks for normals and tangents (SDF fonts do, for the tilt-aware smoothing). */
	bool bRequireNormalAndTangent = false;
	/** Colour of every glyph that did not get one from a <color> tag. */
	FColor BaseColor = FColor::White;
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
