// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamFontFaceResolver.h"

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
	/** The elements' style size. The run was shaped -- and its glyphs are rasterized -- at Size * FaceScale. */
	float Size = 0.0f;
	/** The face's scale (FDreamFontFaceTable::GetScale): CSS size-adjust. Every advance and offset in Glyphs includes it already. */
	float FaceScale = 1.0f;
	/** The face is a colour face (UDreamUIFontData_BaseObject::IsColorFace): its glyphs are emoji, never emboldened. */
	bool bColorFace = false;
	bool bBold = false;
	/**
	 * Bold that the face does not have: the run's clusters carry the font's bold ratio in their advance, and its glyphs are
	 * drawn emboldened. Never on a colour face.
	 */
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
	/**
	 * The rest of the element's code points after Codepoint, as a range of FDreamShapeParams::SequenceCodepoints: what an
	 * emoji sequence holds beyond its base (U+FE0F, a keycap mark, the second regional indicator, skin tones, ZWJ and what
	 * it joins, tags), so HarfBuzz sees the whole sequence and a ligature over it can form inside the element. 0 for an
	 * element that is one code point -- an escaped one always is. Every glyph HarfBuzz makes of the sequence belongs to
	 * this element.
	 */
	int32 SequenceStart = 0;
	int32 SequenceCount = 0;
	/** Which of FDreamShapeParams::Languages the element is in: the faces its cultures prefer, and the language HarfBuzz shapes it with. */
	uint8 LanguageIndex = 0;
};

/** Everything ShapeParagraph reads besides the elements. */
struct FDreamShapeParams
{
	/** The font; its faces and shaping fonts are what runs are shaped with. */
	UDreamUIFontData_BaseObject* Font = nullptr;
	/** Whether the 'kern' feature is applied. */
	bool bUseKerning = false;
	/** Whether the optional ligatures (liga, clig) and contextual alternates (calt) apply; see ShapeParagraph. */
	bool bLigatures = false;
	/** Auto asks the bidi algorithm; the other two force the paragraph's direction. */
	EDreamTextFlowDirection FlowDirection = EDreamTextFlowDirection::Auto;
	/** What FDreamShapeElement::LanguageIndex indexes. Null or empty: every element is in the game's current language. */
	const TArray<FDreamTextLanguage>* Languages = nullptr;
	/** What FDreamShapeElement::SequenceStart/SequenceCount index. Null when no element has more than one code point. */
	const TArray<uint32>* SequenceCodepoints = nullptr;
};

/**
 * Itemizes a paragraph into runs and shapes each with HarfBuzz. Itemization is by bidi level (ICU's
 * UAX #9 implementation), script, language, face (FDreamFontFaceResolver, per grapheme cluster: the
 * font's bold or italic face first for a styled cluster, fallbacks by range and culture, colour faces
 * first for emoji presentation; punctuation following its neighbours), size and weight -- the same cuts
 * a browser's text itemizer makes before handing runs to the shaper. Each run is shaped with the
 * paragraph around it as context, so cursive joining carries across a cut that is not the script's own
 * (a size or weight that changes mid-word). Words are looked up in FDreamTextShapeCache first.
 */
class DREAMGUI_API FDreamTextShaper
{
public:
	/**
	 * @param Elements       The paragraph's elements, in logical order.
	 * @param Params         The font, the features, the direction, the languages and the sequences the elements index.
	 * @param OutRuns        Runs in logical order. An element's glyphs are contiguous and in one run. A grapheme cluster
	 *                       of several elements that no face covers whole is resolved element by element, so its elements
	 *                       can lie in neighbouring runs of different faces (same bidi level); an element is never split.
	 *                       A default-ignorable code point of a sequence that HarfBuzz hides (a selector the face has no
	 *                       variant for) leaves no glyph of its own in an element that has another.
	 * @param OutBaseRightToLeft  The paragraph's base direction, which decides the visual order of its runs on a line.
	 * @param OutBidiLevels  When given, the bidi embedding level of every element, the unshaped ones included.
	 * @return false when the font cannot shape (no HarfBuzz font); the caller then measures per code point.
	 *
	 * Params.bLigatures: whether the optional ligatures (liga, clig) and contextual alternates (calt) apply. Off, a code
	 * point keeps a glyph of its own, which is what per-character animation needs, except where its script requires
	 * otherwise (Arabic's lam-alef, Indic conjuncts). calt goes off with the ligatures in Latin, Greek, Cyrillic, Armenian,
	 * Georgian and runs of digits and punctuation, where it is only style; every other script keeps the font's default,
	 * since the fonts of the joining and Brahmic scripts can put required forms there. Ligatures inside an element (an
	 * emoji sequence) form whatever it says, through ccmp.
	 */
	static bool ShapeParagraph(const TArray<FDreamShapeElement>& Elements, const FDreamShapeParams& Params, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft, TArray<uint8>* OutBidiLevels = nullptr);

	/** The form before FDreamShapeParams: every element one code point, in the game's current language. */
	static bool ShapeParagraph(const TArray<FDreamShapeElement>& Elements, UDreamUIFontData_BaseObject* Font, bool bUseKerning, EDreamTextFlowDirection FlowDirection, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft, bool bLigatures = false, TArray<uint8>* OutBidiLevels = nullptr)
	{
		FDreamShapeParams Params;
		Params.Font = Font;
		Params.bUseKerning = bUseKerning;
		Params.bLigatures = bLigatures;
		Params.FlowDirection = FlowDirection;
		return ShapeParagraph(Elements, Params, OutRuns, OutBaseRightToLeft, OutBidiLevels);
	}

	/** Whether the font can shape at all. */
	static bool CanShape(UDreamUIFontData_BaseObject* Font);
};
