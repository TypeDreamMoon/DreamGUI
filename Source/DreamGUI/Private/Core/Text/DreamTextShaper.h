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
	/** Colour faces may draw clusters (FDreamFontFaceQuery::bAllowColorFaces); off, every cluster is itemized as if the font had none. */
	bool bAllowColorFaces = true;
};

/**
 * What the itemizer and the shape cache decided per element: what a later window over the same paragraph has to agree
 * with (FDreamTextShaper::ShapeWindow), and so what a layout keeps of a paragraph besides its glyphs.
 */
struct FDreamShapeAnalysis
{
	/**
	 * Per element: its script as the itemizer resolved it (an hb_script_t): its own, or for a neutral one (punctuation,
	 * spaces, marks) the script before it, and at a paragraph's start the first script after it. 0 for an unshaped element.
	 */
	TArray<uint32> Scripts;
	/** Per element: it has a script of its own, not Common, Inherited or Unknown (the whole grapheme cluster it starts, or is in). */
	TBitArray<> OwnScripts;
	/**
	 * Per element: a segment starts at it -- a run starts there, it is unshaped, or the shape cache cut the run before it
	 * (where HarfBuzz shapes the two sides apart exactly as together). Shaping in one window or in another never moves
	 * glyphs across one.
	 */
	TBitArray<> SegmentStarts;
};

/** A stretch of a paragraph to shape again on its own (FDreamTextShaper::ShapeWindow). */
struct FDreamShapeWindow
{
	/**
	 * The elements to shape, [Begin, End) of the span handed in; the span's other elements are context only (HarfBuzz and
	 * the shape cache read up to five code points on either side). Both ends must be where the paragraph's own shaping
	 * started a segment, and the span must hold the whole grapheme cluster that starts at End.
	 */
	int32 Begin = 0;
	int32 End = 0;
	/**
	 * The script the itemizer carries into Begin: the last script of its own (FDreamShapeAnalysis::OwnScripts) before the
	 * window -- not the resolved script of the element right before it, which is 0 for an unshaped one. 0 (and any value
	 * with bParagraphStart) starts from Common, as a paragraph's itemizer does.
	 */
	uint32 SeedScript = 0;
	/** Begin is its paragraph's first element: neutrals before the window's first script of its own take that script. */
	bool bParagraphStart = false;
};

/** What ShapeWindow found besides the glyphs. */
struct FDreamShapeWindowResult
{
	/** The window's elements, [Begin, End) of the span, indexed from Begin. */
	FDreamShapeAnalysis Analysis;
	/** The script an element after the window that has none of its own inherits: the window's last script of its own, else the seed. */
	uint32 LastScript = 0;
	/** An element of the window has a script of its own. */
	bool bAnyOwnScript = false;
	/** The span's element at End would continue the window's last run (same level, script, face, size, weight and language, no run break before it). */
	bool bLastRunContinues = false;
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
	 * @param OutAnalysis    When given, each element's resolved script and where the segments start (FDreamShapeAnalysis).
	 */
	static bool ShapeParagraph(const TArray<FDreamShapeElement>& Elements, const FDreamShapeParams& Params, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft, TArray<uint8>* OutBidiLevels = nullptr, FDreamShapeAnalysis* OutAnalysis = nullptr);

	/**
	 * Shapes a window of a left-to-right paragraph again on its own, coming out exactly as shaping the whole paragraph
	 * does for those elements: what an edit inside a long paragraph re-measures instead of all of it. The window is
	 * itemized from Window.SeedScript (every bidi level 0: the caller has made sure nothing in the paragraph can turn right
	 * to left), cut into runs and shaped through the shape cache, which has to be on; the span's elements outside it are
	 * context. Runs come back in logical order with their element ranges in the span's indices, starting at Window.Begin and
	 * ending at Window.End -- the caller numbers them and decides whether the first continues the run before the window.
	 * @return false when the font cannot shape, the shape cache is off, or this is not the game thread.
	 */
	static bool ShapeWindow(const TArray<FDreamShapeElement>& SpanElements, const FDreamShapeParams& Params, const FDreamShapeWindow& Window,
		TArray<FDreamShapedRun>& OutRuns, FDreamShapeWindowResult& OutResult);

	/** Whether the bidi algorithm can have anything to say about a code point (a right-to-left letter or digit, a direction control). */
	static bool CanTurnRightToLeft(uint32 Codepoint);
	/** Whether HarfBuzz reads the code points around a run in this script (an hb_script_t): the joining scripts, and any it does not know. */
	static bool ScriptReadsContext(uint32 Script);

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
