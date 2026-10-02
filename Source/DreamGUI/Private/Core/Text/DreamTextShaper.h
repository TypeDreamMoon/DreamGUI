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
	float XOffset = 0.0f;
	float YOffset = 0.0f;
};

/**
 * A stretch of a paragraph shaped in one go: same face, same bidi level, same script, same size and
 * weight. Glyphs are in visual order -- for a right-to-left run, that means the array runs from
 * the left edge of the run rightwards, so clusters descend.
 */
struct FDreamShapedRun
{
	int32 ElementStart = 0;
	/** Exclusive. */
	int32 ElementEnd = 0;
	bool bRightToLeft = false;
	/** Bidi embedding level (UAX #9) of the run: odd runs right to left. A line orders its runs by these. */
	uint8 BidiLevel = 0;
	int32 FaceIndex = 0;
	float Size = 0.0f;
	bool bBold = false;
	/** Bold that the face does not have: the run's clusters carry the font's bold ratio in their advance, and its glyphs are drawn emboldened. */
	bool bSyntheticBold = false;
	TArray<FDreamShapedGlyph> Glyphs;
};

/** What the shaper needs to know about each element beyond its code point. */
struct FDreamShapeElement
{
	uint32 Codepoint = 0;
	float Size = 0.0f;
	bool bBold = false;
	/** Italic: a run that asks for it is drawn with the font's italic face when the font has one. */
	bool bItalic = false;
	/** Image placeholders and emoji are measured by the layout, never shaped; they end a run. */
	bool bUnshaped = false;
	/** A grapheme cluster (UAX #29) starts here. A cluster is drawn from one face and never split between runs. */
	bool bGraphemeStart = true;
	/** No run continues into this element even if nothing else differs: a rich-text tag edge while ligatures are on, so no ligature straddles a tag. The letters on either side still join, as context of each other's runs. */
	bool bRunBreakBefore = false;
};

/**
 * Itemizes a paragraph into runs and shapes each with HarfBuzz. Itemization is by bidi level (ICU's
 * UAX #9 implementation), script, face coverage (the first face that has every code point of a
 * grapheme cluster, punctuation following its neighbours, the font's bold or italic face first for a
 * styled cluster), size and weight -- the same cuts a browser's text itemizer makes before handing
 * runs to the shaper. Each run is shaped with the paragraph around it as context, so cursive joining
 * carries across a cut that is not the script's own (a size or weight that changes mid-word).
 */
class DREAMGUI_API FDreamTextShaper
{
public:
	/**
	 * @param Elements       The paragraph's elements, in logical order.
	 * @param Font           The font; its faces and shaping fonts are what runs are shaped with.
	 * @param bUseKerning    Whether the 'kern' feature is applied.
	 * @param FlowDirection  Auto asks the bidi algorithm; the other two force the paragraph's direction.
	 * @param OutRuns        Runs in logical order.
	 * @param OutBaseRightToLeft  The paragraph's base direction, which decides the visual order of its runs on a line.
	 * @param bLigatures     Whether the optional ligatures (liga, clig) and contextual alternates (calt) apply. Off, a code
	 *                       point keeps a glyph of its own, which is what per-character animation needs, except where its
	 *                       script requires otherwise (Arabic's lam-alef, Indic conjuncts). calt goes off with the ligatures
	 *                       in Latin, Greek, Cyrillic, Armenian, Georgian and runs of digits and punctuation, where it is
	 *                       only style; every other script keeps the font's default, since the fonts of the joining and
	 *                       Brahmic scripts can put required forms there.
	 * @param OutBidiLevels  When given, the bidi embedding level of every element, the unshaped ones included.
	 * @return false when the font cannot shape (no HarfBuzz font); the caller then measures per code point.
	 */
	static bool ShapeParagraph(const TArray<FDreamShapeElement>& Elements, UDreamUIFontData_BaseObject* Font, bool bUseKerning, EDreamTextFlowDirection FlowDirection, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft, bool bLigatures = false, TArray<uint8>* OutBidiLevels = nullptr);

	/** Whether the font can shape at all. */
	static bool CanShape(UDreamUIFontData_BaseObject* Font);
};
