// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUITextData.h"

class UDreamUIFontData_BaseObject;

/** One positioned glyph out of the shaper. Advances and offsets are in the text's units at the run's size. */
struct FDreamShapedGlyph
{
	int32 FaceIndex = 0;
	uint32 GlyphIndex = 0;
	/** The element this glyph belongs to: the first element of its cluster. */
	int32 ElementIndex = 0;
	float XAdvance = 0.0f;
	float YAdvance = 0.0f;
	float XOffset = 0.0f;
	float YOffset = 0.0f;
};

/**
 * A stretch of a paragraph shaped in one go: same face, same direction, same script, same size and
 * weight. Glyphs are in visual order -- for a right-to-left run, that means the array runs from
 * the left edge of the run rightwards, so clusters descend.
 */
struct FDreamShapedRun
{
	int32 ElementStart = 0;
	/** Exclusive. */
	int32 ElementEnd = 0;
	bool bRightToLeft = false;
	int32 FaceIndex = 0;
	float Size = 0.0f;
	bool bBold = false;
	TArray<FDreamShapedGlyph> Glyphs;
};

/** What the shaper needs to know about each element beyond its code point. */
struct FDreamShapeElement
{
	uint32 Codepoint = 0;
	float Size = 0.0f;
	bool bBold = false;
	/** Image placeholders and emoji are measured by the layout, never shaped; they end a run. */
	bool bUnshaped = false;
};

/**
 * Itemizes a paragraph into runs and shapes each with HarfBuzz. Itemization is by direction
 * (engine TextBiDi), script, face coverage (the first face that has the code point, marks and
 * punctuation following their neighbours), size and weight -- the same cuts a browser's text
 * itemizer makes before handing runs to the shaper.
 */
class FDreamTextShaper
{
public:
	/**
	 * @param Elements       The paragraph's elements, in logical order.
	 * @param Font           The font; its faces and shaping fonts are what runs are shaped with.
	 * @param bUseKerning    Whether the 'kern' feature is applied.
	 * @param OutRuns        Runs in logical order.
	 * @param OutBaseRightToLeft  The paragraph's base direction, which decides the visual order of its runs on a line.
	 * @return false when the font cannot shape (no HarfBuzz font); the caller then measures per code point.
	 */
	static bool ShapeParagraph(const TArray<FDreamShapeElement>& Elements, UDreamUIFontData_BaseObject* Font, bool bUseKerning, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft);

	/** Whether the font can shape at all. */
	static bool CanShape(UDreamUIFontData_BaseObject* Font);
};
