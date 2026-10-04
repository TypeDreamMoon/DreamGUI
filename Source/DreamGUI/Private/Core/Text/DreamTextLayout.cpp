// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextLayout.h"
#include "DreamGUI.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIRichTextImageData_BaseObject.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/FRichTextParser.h"
#include "Core/Text/DreamTextBreaker.h"
#include "Core/Text/DreamTextShaper.h"
#include "Core/Text/DreamTextShapeCache.h"
#include "Algo/Reverse.h"
#include "Engine/Texture2DArray.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Hash/CityHash.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/ObjectKey.h"
#include <atomic>

static int32 GDreamTextIncrementalLayout = 1;
static FAutoConsoleVariableRef CVarDreamTextIncrementalLayout(
	TEXT("DreamGUI.Text.IncrementalLayout"),
	GDreamTextIncrementalLayout,
	TEXT("1: a text that asks for it (a field being typed into), or a long one (256 elements or more) whose content changed ")
	TEXT("in two layouts in a row, keeps what its layout found and lays out again only what an edit touched. 0: every layout ")
	TEXT("starts from nothing, and nothing is kept."),
	ECVF_Default);

static int32 GDreamTextIncrementalParse = 1;
static FAutoConsoleVariableRef CVarDreamTextIncrementalParse(
	TEXT("DreamGUI.Text.IncrementalParse"),
	GDreamTextIncrementalParse,
	TEXT("1: an incremental layout finds the edit from the text itself (what the old and the new content start and end with in ")
	TEXT("common), takes every paragraph the edit did not touch where it stands -- no hash, no comparison -- and reads only the ")
	TEXT("window around the edit, splicing it into the elements, records and boundary bits it kept. 0: every element is read again ")
	TEXT("and paragraphs are found by their content, as round-4 layouts did."),
	ECVF_Default);

static int32 GDreamTextIncrementalMeasure = 1;
static FAutoConsoleVariableRef CVarDreamTextIncrementalMeasure(
	TEXT("DreamGUI.Text.IncrementalMeasure"),
	GDreamTextIncrementalMeasure,
	TEXT("1: inside a long paragraph an edit touched, only a window around the edit -- a segment of untouched text on either ")
	TEXT("side -- is shaped and measured again and spliced into the rest, which stays as it was. Needs DreamGUI.Text.IncrementalParse. ")
	TEXT("0: an edited paragraph is measured whole, as round-4 layouts did."),
	ECVF_Default);

static int32 GDreamTextInPlaceDisplayList = 1;
static FAutoConsoleVariableRef CVarDreamTextInPlaceDisplayList(
	TEXT("DreamGUI.Text.InPlaceDisplayList"),
	GDreamTextInPlaceDisplayList,
	TEXT("1: an incremental layout edits the display list it wrote last time where it is: the lines the edit changed are placed ")
	TEXT("again and spliced in, the lines after them rebased, and a line is moved only when its place changed. 0: the display ")
	TEXT("list is written whole, kept lines copied from a copy the layout kept, as round-4 layouts did."),
	ECVF_Default);

#if !UE_BUILD_SHIPPING
static int32 GDreamTextVerifyIncremental = 0;
static FAutoConsoleVariableRef CVarDreamTextVerifyIncremental(
	TEXT("DreamGUI.Text.VerifyIncremental"),
	GDreamTextVerifyIncremental,
	TEXT("1: every layout that built on a kept one is laid out again from nothing and the two compared, display list and kept ")
	TEXT("state; the first difference is logged, the fresh layout is used and what was kept is dropped. For finding a fault in ")
	TEXT("incremental layout: it costs a whole layout more each time. 0: off."),
	ECVF_Default);
#endif

namespace DreamTextLayoutGlobals
{
	/** What FDreamTextDisplayList::Generation and LineStamps are drawn from: process-wide, so no two lists or lines ever share one. */
	std::atomic<uint64> DisplayListGeneration{ 0 };
	std::atomic<uint64> LineStamp{ 0 };

	uint64 NextGeneration()
	{
		return DisplayListGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	uint64 NextLineStamp()
	{
		return LineStamp.fetch_add(1, std::memory_order_relaxed) + 1;
	}
}

bool FDreamTextLayoutInput::operator==(const FDreamTextLayoutInput& Other) const
{
	// Colour is deliberately absent, and so is the render opacity that used to ride with it. Both are
	// paint inputs: untagged glyphs take FDreamTextPaintParams::BaseColor, and a <color> tag's colour is
	// stored with its authored alpha and multiplied by FDreamTextPaintParams::RichTextTagOpacity when the
	// quad is written. Nothing the layout emits depends on either. Comparing them here meant every fade
	// -- GetFinalColor() folds the whole hierarchy's render opacity into the alpha -- re-ran rich-text
	// parsing, shaping and line breaking on every frame of the fade.
	return Content.Equals(Other.Content)
		&& Width == Other.Width
		&& Height == Other.Height
		&& Pivot == Other.Pivot
		&& FontSpace == Other.FontSpace
		&& FontSize == Other.FontSize
		&& ParagraphHAlign == Other.ParagraphHAlign
		&& ParagraphVAlign == Other.ParagraphVAlign
		&& OverflowType == Other.OverflowType
		&& WrappingPolicy == Other.WrappingPolicy
		&& PhraseWrap == Other.PhraseWrap
		&& bUseKerning == Other.bUseKerning
		&& FontStyle == Other.FontStyle
		&& bUnderline == Other.bUnderline
		&& bStrikethrough == Other.bStrikethrough
		&& TextTransform == Other.TextTransform
		&& FlowDirection == Other.FlowDirection
		&& Language.Equals(Other.Language, ESearchCase::CaseSensitive)
		&& TabSize == Other.TabSize
		&& TextJustify == Other.TextJustify
		&& LastLineAlign == Other.LastLineAlign
		&& bAutoWrapText == Other.bAutoWrapText
		&& bRichText == Other.bRichText
		&& RichTextFilterFlags == Other.RichTextFilterFlags
		&& LineHeightPercentage == Other.LineHeightPercentage
		&& WrapTextAt == Other.WrapTextAt
		&& ExpandMeshSize == Other.ExpandMeshSize
		&& DynamicPixelsPerUnit == Other.DynamicPixelsPerUnit
		&& RootCanvasScale == Other.RootCanvasScale
		&& bRenderToWorldSpace == Other.bRenderToWorldSpace
		&& bPixelPerfect == Other.bPixelPerfect
		&& bAllowLigatures == Other.bAllowLigatures
		&& bAllowColorFaces == Other.bAllowColorFaces
		&& Font == Other.Font
		&& RichTextImageData == Other.RichTextImageData
		&& RichTextCustomStyleData == Other.RichTextCustomStyleData;
}

namespace DreamTextLayoutLocal
{
	using namespace DreamUIRichTextParser;

	/**
	 * Whether the letter spacing after a line's last cluster is part of the line: of its width, of what it is aligned
	 * by, and of the text's preferred width. Browsers add letter-spacing after every typographic character unit, the
	 * last one on a line included -- which is why a centred, letter-spaced heading in Chrome sits half a spacing left of
	 * centre -- and this follows them. The parity harness measures it against Chrome.
	 */
	constexpr bool bTrailingLetterSpacingCounts = true;

	/**
	 * Scripts whose letters join, so a gap between them breaks the word apart: Arabic, Syriac, N'Ko, Mandaic, Mongolian,
	 * Phags-pa and the joining scripts of the supplementary planes. CSS Text asks that letter-spacing not be applied to
	 * them, and Slate does not space right-to-left text either. The digits of the Arabic blocks do not join.
	 */
	bool IsCursiveScript(uint32 C)
	{
		if ((C >= 0x0660 && C <= 0x066C) || (C >= 0x06F0 && C <= 0x06F9))
		{
			return false;
		}
		return (C >= 0x0600 && C <= 0x077F)   // Arabic, Syriac, Arabic Supplement
			|| (C >= 0x07C0 && C <= 0x07FF)   // N'Ko
			|| (C >= 0x0840 && C <= 0x08FF)   // Mandaic, Syriac Supplement, Arabic Extended-B and -A
			|| (C >= 0x1800 && C <= 0x18AF)   // Mongolian
			|| (C >= 0xA840 && C <= 0xA87F)   // Phags-pa
			|| (C >= 0xFB50 && C <= 0xFDFF)   // Arabic Presentation Forms-A
			|| (C >= 0xFE70 && C <= 0xFEFC)   // Arabic Presentation Forms-B
			|| (C >= 0x10AC0 && C <= 0x10AFF) // Manichaean
			|| (C >= 0x10B80 && C <= 0x10BAF) // Psalter Pahlavi
			|| (C >= 0x10D00 && C <= 0x10D3F) // Hanifi Rohingya
			|| (C >= 0x10EC0 && C <= 0x10EFF) // Arabic Extended-C
			|| (C >= 0x10F30 && C <= 0x10FDF) // Sogdian, Old Uyghur, Chorasmian
			|| (C >= 0x11660 && C <= 0x1167F) // Mongolian Supplement
			|| (C >= 0x1E900 && C <= 0x1E95F);// Adlam
	}

	/**
	 * What a justified line may widen on either side of under text-justify auto, as Blink does it: ideographs, kana,
	 * Bopomofo, CJK symbols and punctuation, and the fullwidth forms. Hangul is not among them: Korean is written with
	 * spaces, and justifies at them.
	 */
	bool IsJustifiedLikeCJK(uint32 C)
	{
		return (C >= 0x2E80 && C <= 0x2FFF)   // CJK radicals, Kangxi radicals, ideographic description
			|| (C >= 0x3000 && C <= 0x303F)   // CJK symbols and punctuation
			|| (C >= 0x3040 && C <= 0x30FF)   // Hiragana, Katakana
			|| (C >= 0x3100 && C <= 0x312F)   // Bopomofo
			|| (C >= 0x3190 && C <= 0x31FF)   // Kanbun, Bopomofo extended, CJK strokes, Katakana phonetic extensions
			|| (C >= 0x3200 && C <= 0x33FF)   // enclosed CJK letters and months, CJK compatibility
			|| (C >= 0x3400 && C <= 0x4DBF)   // CJK extension A
			|| (C >= 0x4E00 && C <= 0x9FFF)   // CJK unified ideographs
			|| (C >= 0xF900 && C <= 0xFAFF)   // CJK compatibility ideographs
			|| (C >= 0xFE30 && C <= 0xFE4F)   // CJK compatibility forms
			|| (C >= 0xFF00 && C <= 0xFF60)   // fullwidth forms
			|| (C >= 0xFFE0 && C <= 0xFFE6)   // fullwidth signs
			|| (C >= 0x20000 && C <= 0x3134F);// CJK extensions B to G
	}

	/** What measuring an element found out. */
	struct FMeasured
	{
		FDreamUICharData Glyph;
		FRichTextParseResult Style;
		/** How far the pen moves for this element: its glyph advance, plus, on a cluster's first element, the cluster's letter spacing. */
		float Advance = 0.0f;
		/** The element's own glyph advances, without letter spacing. */
		float ClusterAdvance = 0.0f;
		/** On a cluster's first element: the letter spacing after the cluster, and the cluster's whole glyph advance, which is what the breaker fits. */
		float LetterSpacing = 0.0f;
		float ClusterFitWidth = 0.0f;
		/** Its glyphs, as a range of the run's glyph array; none for a cluster continuation. */
		int32 GlyphStart = 0;
		int32 GlyphCount = 0;
		/** Shaped run this element belongs to, or -1 when measured per code point. */
		int32 RunIndex = -1;
		/** Face the element's glyphs came from: 0 is the font, then its fallbacks, then its styled faces. */
		int32 FaceIndex = 0;
		/** Bidi embedding level (UAX #9): odd reads right to left. */
		uint8 BidiLevel = 0;
		/** Paragraph base direction: a right-to-left paragraph starts at the right and is aligned from there. */
		bool bBaseRightToLeft = false;
		/** A newline: ends the paragraph, never placed. */
		bool bHardBreak = false;
		/** The second half of a CR LF pair: nothing at all. */
		bool bSkipped = false;
		/** Space or tab (not an image placeholder): advances, hangs past the wrap width, never emits. */
		bool bWhitespace = false;
		/** A tab: whitespace as wide as the distance to the next tab stop from where it stands on its line. */
		bool bTab = false;
		bool bImageSpace = false;
		/**
		 * An emoji drawn by the font's emoji data as an inline object: the entry for its exact sequence, or, when no
		 * colour face has the whole cluster, the one for its base (EmojiItem). Every other emoji is a glyph.
		 */
		bool bEmoji = false;
		const FDreamUIFontEmojiDataItem* EmojiItem = nullptr;
		/** Which of Languages the element is in: 0 for the text's own language, else the <lang=xx> around it. */
		uint8 LanguageIndex = 0;
		/** A glyph the painter will draw. */
		bool bVisibleGlyph = false;
		/** The shaper gave this element glyphs of its own, or it is not shaped at all: no shaped cluster continues into it. */
		bool bShapeClusterStart = true;
		/**
		 * A cluster starts here: an extended grapheme cluster that is not inside a shaped cluster (a ligature, a
		 * conjunct). Lines break only before one, letter spacing follows one, and its glyphs are placed together.
		 */
		bool bClusterStart = true;
		/** A caret stands before this element: a grapheme cluster starts here, inside a ligature or not. */
		bool bCaretStop = true;
		/** Bold or italic that the element's face does not have, and the painter makes up. */
		bool bSyntheticBold = false;
		bool bSyntheticItalic = false;

		/*
		 * What its shaping found besides glyphs (FDreamShapeAnalysis), so an edit beside it can be shaped as a window that
		 * agrees with the rest: its resolved script (an hb_script_t, 0 unshaped), whether that script is its own, and whether
		 * a segment starts here. None of them is set by the path that measures per code point.
		 */
		uint32 Script = 0;
		bool bOwnScript = false;
		bool bSegmentStart = false;
		/** The paragraph's preferred-width pen after this element, from the paragraph's start, unwrapped: where a window's sum resumes. */
		float PenAfter = 0.0f;
	};

	/** A line as the breaker decided it: a half-open element range, what ended it, and what decided where it ends. */
	struct FLineRange
	{
		int32 Start = 0;
		int32 End = 0;
		/** The newline element that ended this line, or -1 for a soft break / the end of the text. */
		int32 HardBreakElement = -1;
		/**
		 * The last element the breaker read to decide where the line ends: the one that did not fit, or the paragraph's end.
		 * A line is a function of the elements from its start to here alone, which is what lets an edit keep the lines before it.
		 */
		int32 DecidedAt = 0;
	};

	/** A glyph ready to place: its atlas quad and how the shaper positioned it. */
	struct FGlyphSource
	{
		FDreamUICharData Quad;
		float XAdvance = 0.0f;
		float XOffset = 0.0f;
		float YOffset = 0.0f;
		int32 ElementIndex = 0;
		/** The size it was shaped and rasterized at, in text units: its style size times its face's scale. */
		float GlyphSize = 0.0f;
		/**
		 * What the quad was asked of the font with (FetchGlyphQuad): a quad kept for the same glyph, size and style is the one
		 * asking again would give. RasterFace is -1 for a glyph measured per code point, whose quad also carries kerning.
		 */
		int32 RasterFace = -1;
		uint32 RasterGlyph = 0;
		bool bRasterBold = false;
		bool bRasterColorFace = false;
	};

	/** A paragraph: the elements between two hard breaks, the unit the shaper, the breakers and a kept layout work in. */
	struct FParagraph
	{
		/** Its elements, [Start, End); End is its newline, or the text's end. */
		int32 Start = 0;
		int32 End = 0;
		/** The next paragraph's first element: past the newline, past both halves of a CR LF. */
		int32 Next = 0;
		/** Its glyphs and shaped runs, half-open. */
		int32 GlyphStart = 0;
		int32 GlyphEnd = 0;
		int32 RunStart = 0;
		int32 RunEnd = 0;
		/** Its lines, half-open. */
		int32 LineStart = 0;
		int32 LineEnd = 0;
		/**
		 * What its measurement depends on -- its text, and each element's size, weight, slant, language, kind, run break and
		 * inline object size -- and what else of its style its placement reads: what a kept paragraph is found by.
		 */
		uint64 MeasureHash = 0;
		uint64 PlaceHash = 0;
		/** Its width on one line (the preferred width's part), and whether it has anything to measure. */
		float PreferredWidth = 0.0f;
		bool bHasPreferredWidth = false;
		bool bHasTab = false;
		/** A glyph of it was still rasterizing: its quads are asked for again rather than kept. */
		bool bPendingQuads = false;
		/**
		 * Counts kept up to date as edits splice elements in and out, so a paragraph's state is known without a walk over it:
		 * its tabs (bHasTab), the code points that can turn it right to left (FDreamTextShaper::CanTurnRightToLeft, its
		 * elements' bases), its inline objects (images and emoji pictures), and its glyphs still rasterizing (bPendingQuads).
		 */
		int32 TabCount = 0;
		int32 RtlCount = 0;
		int32 ObjectCount = 0;
		int32 PendingGlyphs = 0;
		/** Its hashes have been worked out: hashes are worked out only when a lookup needs them, and kept with the paragraph. */
		bool bHashed = false;

		/*
		 * This layout's own bookkeeping against the kept layout.
		 */

		/** The kept paragraph it is, measured as it was; INDEX_NONE when it was measured. */
		int32 Source = INDEX_NONE;
		/** Taken where it stood: the edit did not touch it, and its elements, records and bits are already in place. */
		bool bPositional = false;
		/** Its style is the kept paragraph's in everything placement reads: its lines may keep their placement. */
		bool bPlaceSame = false;
		/** For a measured paragraph: the kept paragraphs it was edited from, whose start and whose end it may share. */
		int32 PrefixDonor = INDEX_NONE;
		int32 SuffixDonor = INDEX_NONE;
		/** Elements at its start and its end whose text is the donors' (TextPrefix, TextSuffix), and of those, how many came out the same. */
		int32 TextPrefix = 0;
		int32 TextSuffix = 0;
		int32 SamePrefix = 0;
		int32 SameSuffix = 0;
	};

	/** Where a placed line is in the display list's arrays, its box, and whether it may be kept. */
	struct FLineSpan
	{
		int32 ItemStart = 0;
		int32 ItemEnd = 0;
		int32 ImageStart = 0;
		int32 ImageEnd = 0;
		int32 EmojiStart = 0;
		int32 EmojiEnd = 0;
		int32 VisualRunStart = 0;
		int32 VisualRunEnd = 0;
		/** Its carets in the kept layout's flat array of line-local caret positions. */
		int32 CaretStart = 0;
		int32 CaretEnd = 0;
		/** The line's top in the paragraph, 0 for the first and going down, and its height. It is placed at 0; Finish moves it. */
		float Top = 0.0f;
		float Height = 0.0f;
		/** Something on it still waited for its glyph: it is placed again next time. */
		bool bPending = false;
		/** It was the text's last line, whose end caret names the text's length. */
		bool bLastLine = false;
		/** Visible characters on it (FDreamTextDisplayList::VisibleCharCount is their sum). */
		int32 VisibleCount = 0;
		/** Where Finish moved it: the box's offset across and the line's top in the box. A line moved alike needs no moving again. */
		float FinishX = 0.0f;
		float FinishY = 0.0f;
	};

	/**
	 * Everything outside the text itself that measuring a paragraph reads: the font and each face it drew from, the atlas
	 * its quads point into, the raster scale, the shaping switches, the languages. A kept layout is built on only under the
	 * same key -- a field missing here would be a stale glyph on screen, so when in doubt a field is in.
	 */
	struct FMeasureKey
	{
		FObjectKey Font;
		/** The atlas texture: a flush, or a slice added, makes another one, and every kept quad may point at nothing. */
		FObjectKey Atlas;
		FObjectKey EmojiData;
		FObjectKey RichTextImageData;
		uint32 LayoutEpoch = 0;
		int32 FaceCount = 0;
		/** The face table: each fallback entry's ranges, cultures, scale and preference. */
		uint64 FaceTableHash = 0;
		float FontSize = 0.0f;
		float MaxFontSize = 0.0f;
		float LetterSpacing = 0.0f;
		float TabSize = 0.0f;
		float ExpandMeshSize = 0.0f;
		float RootCanvasScale = 1.0f;
		float DynamicPixelsPerUnit = 1.0f;
		float ItalicSlope = 0.0f;
		bool bPixelPerfect = false;
		bool bRenderToWorldSpace = false;
		bool bSupportDynamicPixelsPerUnit = false;
		bool bUseKerning = false;
		bool bLigatures = false;
		bool bCanShape = false;
		bool bRichText = false;
		bool bAllowColorFaces = true;
		int32 RichTextFilterFlags = 0;
		EDreamTextFlowDirection FlowDirection = EDreamTextFlowDirection::Auto;
		EDreamTextPhraseWrap PhraseWrap = EDreamTextPhraseWrap::Off;
		EDreamUITextParagraphHorizontalAlign ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		EDreamTextJustify TextJustify = EDreamTextJustify::Auto;
		EDreamTextLastLineAlign LastLineAlign = EDreamTextLastLineAlign::Auto;
		/** The text's own language as it resolved, and the game's culture, which line breaking follows. */
		FString Language;
		FString Culture;

		bool operator==(const FMeasureKey& Other) const
		{
			return Font == Other.Font && Atlas == Other.Atlas && EmojiData == Other.EmojiData && RichTextImageData == Other.RichTextImageData
				&& LayoutEpoch == Other.LayoutEpoch && FaceCount == Other.FaceCount && FaceTableHash == Other.FaceTableHash
				&& FontSize == Other.FontSize && MaxFontSize == Other.MaxFontSize && LetterSpacing == Other.LetterSpacing
				&& TabSize == Other.TabSize && ExpandMeshSize == Other.ExpandMeshSize && RootCanvasScale == Other.RootCanvasScale
				&& DynamicPixelsPerUnit == Other.DynamicPixelsPerUnit && ItalicSlope == Other.ItalicSlope
				&& bPixelPerfect == Other.bPixelPerfect && bRenderToWorldSpace == Other.bRenderToWorldSpace
				&& bSupportDynamicPixelsPerUnit == Other.bSupportDynamicPixelsPerUnit && bUseKerning == Other.bUseKerning
				&& bLigatures == Other.bLigatures && bCanShape == Other.bCanShape && bRichText == Other.bRichText
				&& bAllowColorFaces == Other.bAllowColorFaces
				&& RichTextFilterFlags == Other.RichTextFilterFlags && FlowDirection == Other.FlowDirection && PhraseWrap == Other.PhraseWrap
				&& ParagraphHAlign == Other.ParagraphHAlign && TextJustify == Other.TextJustify && LastLineAlign == Other.LastLineAlign
				&& Language.Equals(Other.Language, ESearchCase::CaseSensitive) && Culture.Equals(Other.Culture, ESearchCase::CaseSensitive);
		}
	};

	/**
	 * Whether two inputs place a line alike once its content is the same: every field but the content, its colour (a paint
	 * input), and what only Finish reads -- the pivot, the vertical alignment, the box's height and the space between lines --
	 * none of which a line's own placement sees outside a Truncate, Ellipsis or MiddleEllipsis, which never keep a line.
	 */
	bool SameLinePlacementInputs(const FDreamTextLayoutInput& A, const FDreamTextLayoutInput& B)
	{
		return A.Width == B.Width && A.FontSpace.X == B.FontSpace.X && A.FontSize == B.FontSize && A.ParagraphHAlign == B.ParagraphHAlign
			&& A.OverflowType == B.OverflowType && A.WrappingPolicy == B.WrappingPolicy && A.PhraseWrap == B.PhraseWrap
			&& A.bUseKerning == B.bUseKerning && A.FontStyle == B.FontStyle && A.bUnderline == B.bUnderline
			&& A.bStrikethrough == B.bStrikethrough && A.TextTransform == B.TextTransform && A.FlowDirection == B.FlowDirection
			&& A.Language.Equals(B.Language, ESearchCase::CaseSensitive) && A.TabSize == B.TabSize && A.TextJustify == B.TextJustify
			&& A.LastLineAlign == B.LastLineAlign && A.bAutoWrapText == B.bAutoWrapText && A.bRichText == B.bRichText
			&& A.RichTextFilterFlags == B.RichTextFilterFlags && A.LineHeightPercentage == B.LineHeightPercentage
			&& A.WrapTextAt == B.WrapTextAt && A.ExpandMeshSize == B.ExpandMeshSize && A.DynamicPixelsPerUnit == B.DynamicPixelsPerUnit
			&& A.RootCanvasScale == B.RootCanvasScale && A.bRenderToWorldSpace == B.bRenderToWorldSpace && A.bPixelPerfect == B.bPixelPerfect
			&& A.bAllowLigatures == B.bAllowLigatures && A.bAllowColorFaces == B.bAllowColorFaces && A.Font == B.Font
			&& A.RichTextImageData == B.RichTextImageData && A.RichTextCustomStyleData == B.RichTextCustomStyleData;
	}

	/**
	 * Whether two inputs give the same elements the same styles once the content is the same: every input the parser and the
	 * styles read besides the content itself. Positional paragraphs (an edit seen from the content) need them all alike.
	 */
	bool SameParseInputs(const FDreamTextLayoutInput& A, const FDreamTextLayoutInput& B)
	{
		return A.Color == B.Color && A.FontStyle == B.FontStyle && A.bUnderline == B.bUnderline && A.bStrikethrough == B.bStrikethrough
			&& A.FontSize == B.FontSize && A.bRichText == B.bRichText && A.RichTextFilterFlags == B.RichTextFilterFlags
			&& A.TextTransform == B.TextTransform && A.Language.Equals(B.Language, ESearchCase::CaseSensitive)
			&& A.RichTextCustomStyleData == B.RichTextCustomStyleData && A.RichTextImageData == B.RichTextImageData && A.Font == B.Font;
	}

	/** Two elements' styles agree on everything measuring or placing an element reads of them (where they stand in the source aside). */
	bool SameLayoutStyle(const FRichTextParseResult& A, const FRichTextParseResult& B)
	{
		return A.Bold == B.Bold && A.Italic == B.Italic && A.Underline == B.Underline && A.Strikethrough == B.Strikethrough
			&& A.Size == B.Size && A.Color == B.Color && A.HasColor == B.HasColor && A.bHasMultiplyColor == B.bHasMultiplyColor
			&& A.MultiplyColor == B.MultiplyColor && A.SupOrSubMode == B.SupOrSubMode && A.BaselineShift == B.BaselineShift
			&& A.ImageTag == B.ImageTag && A.ImageWidth == B.ImageWidth && A.ImageHeight == B.ImageHeight
			&& A.ImageVerticalAlign == B.ImageVerticalAlign && A.Language == B.Language
			&& A.PaintName == B.PaintName && A.PaintOrder == B.PaintOrder && A.bPaintRemoved == B.bPaintRemoved;
	}

	/** Every field of two parse results, where each stands in the source included: what the debug comparison of two states reads. */
	bool SameParseResult(const FRichTextParseResult& A, const FRichTextParseResult& B)
	{
		return SameLayoutStyle(A, B) && A.BoldOrder == B.BoldOrder && A.ItalicOrder == B.ItalicOrder && A.UnderlineOrder == B.UnderlineOrder
			&& A.StrikethroughOrder == B.StrikethroughOrder && A.SizeOrder == B.SizeOrder && A.ColorOrder == B.ColorOrder
			&& A.SupOrSubOrder == B.SupOrSubOrder && A.CustomTagMode == B.CustomTagMode && A.CustomTag == B.CustomTag
			&& A.bHyperlink == B.bHyperlink && A.CustomTagOrder == B.CustomTagOrder && A.CharIndex == B.CharIndex;
	}

	/** A rich-text custom tag as the source has it: where it opened and where it closed, in elements and in the source. */
	struct FTagRecord
	{
		FName Name;
		bool bHyperlink = false;
		/** The first element inside the tag, and the last; an unclosed tag runs to the end of the text. */
		int32 FirstElement = 0;
		int32 LastElement = INDEX_NONE;
		bool bClosed = false;
		/** The order the tag was opened in, which ranks its custom style against the tags nested inside it. */
		int32 Order = 0;
		/** Where its opening and its closing markup start in the source: an edit that moves the text after them moves them. */
		int32 OpenAt = 0;
		int32 CloseAt = INDEX_NONE;

		bool operator==(const FTagRecord& Other) const
		{
			return Name == Other.Name && bHyperlink == Other.bHyperlink && FirstElement == Other.FirstElement && LastElement == Other.LastElement
				&& bClosed == Other.bClosed && Order == Other.Order && OpenAt == Other.OpenAt && CloseAt == Other.CloseAt;
		}
	};

	/** What a glyph's quad is asked of the font with: the same glyph, size, weight and kind of face is the same quad while the measure key holds. */
	struct FQuadKey
	{
		int32 Face = 0;
		uint32 Glyph = 0;
		float Size = 0.0f;
		bool bBold = false;
		bool bColorFace = false;

		bool operator==(const FQuadKey& Other) const
		{
			return Face == Other.Face && Glyph == Other.Glyph && Size == Other.Size && bBold == Other.bBold && bColorFace == Other.bColorFace;
		}
		friend uint32 GetTypeHash(const FQuadKey& Key)
		{
			return HashCombineFast(HashCombineFast(::GetTypeHash(Key.Face), ::GetTypeHash(Key.Glyph)),
				HashCombineFast(::GetTypeHash(Key.Size), (uint32)(Key.bBold ? 1 : 0) | (Key.bColorFace ? 2u : 0u)));
		}
	};

	/**
	 * Makes [Start, Start + OldCount) of an array NewCount long, moving what follows once: the hole an edit's new records are
	 * written into. The hole's records are left as they were where it shrank or kept its size, and default where it grew.
	 */
	template <typename T, typename AllocatorType>
	void SpliceRange(TArray<T, AllocatorType>& Array, int32 Start, int32 OldCount, int32 NewCount)
	{
		if (NewCount > OldCount)
		{
			Array.InsertDefaulted(Start + OldCount, NewCount - OldCount);
		}
		else if (NewCount < OldCount)
		{
			Array.RemoveAt(Start + NewCount, OldCount - NewCount, EAllowShrinking::No);
		}
	}

	/** The same for a bit array; bits it adds are false. */
	void SpliceBits(TBitArray<>& Bits, int32 Start, int32 OldCount, int32 NewCount)
	{
		if (NewCount > OldCount)
		{
			Bits.Insert(false, Start + OldCount, NewCount - OldCount);
		}
		else if (NewCount < OldCount)
		{
			Bits.RemoveAt(Start + NewCount, OldCount - NewCount);
		}
	}

	/** Every field of two glyph entries, the advance aside when asked to (a tab's is where its line put it). */
	bool SameCharData(const FDreamUICharData& A, const FDreamUICharData& B, bool bIgnoreAdvance = false)
	{
		return A.Width == B.Width && A.Height == B.Height && A.XOffset == B.XOffset && A.YOffset == B.YOffset
			&& (bIgnoreAdvance || A.XAdvance == B.XAdvance) && A.MinUV == B.MinUV && A.MaxUV == B.MaxUV && A.SliceIndex == B.SliceIndex
			&& A.FaceIndex == B.FaceIndex && A.GlyphIndex == B.GlyphIndex && A.bPending == B.bPending && A.bColor == B.bColor
			&& A.ColorTexelsPerEm == B.ColorTexelsPerEm;
	}

	bool SameGlyphSource(const FGlyphSource& A, const FGlyphSource& B)
	{
		return SameCharData(A.Quad, B.Quad) && A.XAdvance == B.XAdvance && A.XOffset == B.XOffset && A.YOffset == B.YOffset
			&& A.GlyphSize == B.GlyphSize;
	}

	/** One step of a running 64-bit hash. */
	uint64 MixHash(uint64 Hash, uint64 Value)
	{
		Hash ^= Value + 0x9E3779B97F4A7C15ull + (Hash << 6) + (Hash >> 2);
		Hash ^= Hash >> 31;
		Hash *= 0xBF58476D1CE4E5B9ull;
		return Hash ^ (Hash >> 27);
	}

	uint64 FloatPairBits(float A, float B)
	{
		return ((uint64)GetTypeHash(A) << 32) | (uint64)GetTypeHash(B);
	}

	/**
	 * An element of an edited paragraph stays under this many code units of an edit before its boundaries are taken from
	 * the kept layout: what ICU's rules read past a boundary to decide it is a few characters, a run of combining marks or
	 * of digits and separators at most.
	 */
	constexpr int32 BoundaryWindowMargin = 32;
	/** A paragraph shorter than this is analysed whole after an edit: a window would save nothing. */
	constexpr int32 BoundaryWindowMinElements = 64;

	FDreamTextLayoutStats Stats;

	/** Adds the time from its making to its end to one stage of the stats. */
	struct FStageTimer
	{
		explicit FStageTimer(EDreamTextLayoutStage InStage)
			: Stage(InStage), Start(FPlatformTime::Cycles64())
		{
		}
		~FStageTimer()
		{
			Stats.Cycles[(int32)Stage] += FPlatformTime::Cycles64() - Start;
		}
		EDreamTextLayoutStage Stage;
		uint64 Start;
	};
}

/**
 * The layout a FDreamTextLayoutState keeps: the arrays of its last run, moved in whole when it ended, and what they were
 * made under. An edit seen from the content moves them out again, splices the edit's window into them and moves them back.
 * What its lines were placed as is kept in one of two forms: line-local coordinates of the display list it wrote (before
 * Finish moved each line to its place), which that list is edited in place from; or, with DreamGUI.Text.InPlaceDisplayList
 * 0, line-local copies of the display list's arrays, which kept lines are copied from.
 */
struct FDreamTextLayoutStateData
{
	bool bValid = false;
	DreamTextLayoutLocal::FMeasureKey MeasureKey;
	/** The faces the kept layout drew from, and who each of them was (UDreamUIFontData_BaseObject::GetFaceIdentity). An edit only adds to them. */
	TArray<int32> UsedFaces;
	TArray<FDreamUIFontFaceIdentity> UsedFaceIdentities;
	/** The input it was laid out from, and its content's length (the last line's end caret). */
	FDreamTextLayoutInput Input;
	int32 ContentLength = 0;

	TArray<FDreamUIText_TextProcessingElement> Elements;
	TBitArray<> FollowsMarkup;
	FString PlainText;
	TArray<int32> PlainStart;
	TArray<uint32> ElementCodepoints;
	/** As the layout left them: a tab's advance is where its line put it. */
	TArray<DreamTextLayoutLocal::FMeasured> Measured;
	TArray<DreamTextLayoutLocal::FGlyphSource> Glyphs;
	TBitArray<> GraphemeStart;
	TBitArray<> ClusterStarts;
	/** Line-break analysis, when the kept layout wrapped: ICU's raw line and word boundaries, and the opportunities made of them. */
	bool bHasBreakBits = false;
	bool bHasWordBits = false;
	TBitArray<> LineBreakRaw;
	TBitArray<> WordBreakRaw;
	TBitArray<> CanBreakBefore;
	TArray<DreamTextLayoutLocal::FParagraph> Paragraphs;
	TArray<DreamTextLayoutLocal::FLineRange> LineRanges;
	/** Paragraphs with a glyph still rasterizing: while there is one, an edit is not spliced in. */
	int32 PendingParagraphs = 0;

	/** Rich text: its custom tags, the style an element added at the text's end would take, and whether markup ends the text. */
	TArray<DreamTextLayoutLocal::FTagRecord> TagRecords;
	DreamUIRichTextParser::FRichTextParseResult EndStyle;
	bool bEndFollowsMarkup = false;
	/** An element has a paint (a <gradient> or a custom style that sets or removes one): its display list has paint runs. */
	bool bAnyPaint = false;
	/** The languages its elements are in, as FMeasured::LanguageIndex indexes them, and the <lang> tags that named them. */
	TArray<FDreamTextLanguage> Languages;
	TMap<FName, uint8> LanguageIndexByTag;
	/** The elements that are inline objects (images, emoji pictures), in order: what is checked against the asset before a paragraph is taken where it stands. */
	TArray<int32> InlineObjects;
	/** Glyph quads already asked of the font, by face, glyph, size, weight and kind of face: valid while the measure key holds. */
	TMap<DreamTextLayoutLocal::FQuadKey, FDreamUICharData> Quads;

	/** Its lines as they were placed, before Finish moved them; false when nothing of the placement may be kept (a clamp). */
	bool bLinesKept = false;
	TArray<DreamTextLayoutLocal::FLineSpan> LineSpans;
	TArray<FDreamTextGlyphItem> Items;
	TArray<FDreamUITextLineProperty> Lines;
	TArray<FDreamUIText_RichTextImageTag> Images;
	TArray<FDreamUIText_Emoji> Emojis;
	TArray<FDreamTextVisualRun> VisualRuns;

	/**
	 * The display list it wrote and that list's generation then: the list an in-place edit may start from. Its lines'
	 * line-local coordinates -- item pens, carets, images, emojis, visual-run edges, parallel to the list's arrays -- are
	 * what a line that has to move is moved from, one addition each as Finish does it.
	 */
	bool bLocalLinesKept = false;
	const FDreamTextDisplayList* WrittenList = nullptr;
	uint64 WrittenGeneration = 0;
	TArray<FVector2f> LocalPens;
	TArray<FVector2f> LocalCarets;
	TArray<FVector2D> LocalImages;
	TArray<FVector2D> LocalEmojis;
	TArray<FVector2f> LocalRuns;

	void Reset()
	{
		*this = FDreamTextLayoutStateData();
	}

	SIZE_T GetAllocatedSize() const
	{
		SIZE_T Size = UsedFaces.GetAllocatedSize() + UsedFaceIdentities.GetAllocatedSize() + Input.Content.GetAllocatedSize()
			+ Elements.GetAllocatedSize() + FollowsMarkup.GetAllocatedSize() + PlainText.GetAllocatedSize() + PlainStart.GetAllocatedSize()
			+ ElementCodepoints.GetAllocatedSize() + Measured.GetAllocatedSize() + Glyphs.GetAllocatedSize() + GraphemeStart.GetAllocatedSize()
			+ ClusterStarts.GetAllocatedSize() + LineBreakRaw.GetAllocatedSize() + WordBreakRaw.GetAllocatedSize()
			+ CanBreakBefore.GetAllocatedSize() + Paragraphs.GetAllocatedSize() + LineRanges.GetAllocatedSize() + TagRecords.GetAllocatedSize()
			+ Languages.GetAllocatedSize() + LanguageIndexByTag.GetAllocatedSize() + InlineObjects.GetAllocatedSize() + Quads.GetAllocatedSize()
			+ LineSpans.GetAllocatedSize() + Items.GetAllocatedSize() + Lines.GetAllocatedSize() + Images.GetAllocatedSize()
			+ Emojis.GetAllocatedSize() + VisualRuns.GetAllocatedSize() + LocalPens.GetAllocatedSize() + LocalCarets.GetAllocatedSize()
			+ LocalImages.GetAllocatedSize() + LocalEmojis.GetAllocatedSize() + LocalRuns.GetAllocatedSize();
		for (const FDreamUITextLineProperty& Line : Lines)
		{
			Size += Line.CaretPropertyList.GetAllocatedSize();
		}
		return Size;
	}
};

namespace DreamTextLayoutLocal
{
	using namespace DreamUIRichTextParser;

	/**
	 * One layout pass, in the order a browser's inline formatting context does it: measure every
	 * element, decide where the lines break, place the lines, then align the paragraph. Measuring
	 * first is what makes the breaker a pure function over widths.
	 *
	 * With a kept layout (FDreamTextLayoutStateData) the same pass takes from it what is provably the same: a paragraph whose
	 * text and measured style match is copied rather than measured, an edited one keeps its lines before and after the edit,
	 * and a line whose content and inputs match keeps its placement. Every line is placed with its top at 0 and moved into
	 * the box by Finish, so a kept line comes out bit for bit as placing it again would.
	 */
	class FLayoutRun
	{
	public:
		FLayoutRun(const FDreamTextLayoutInput& InInput, FDreamTextDisplayList& InOut, FDreamTextLayoutStateData* InState)
			: In(InInput), Out(InOut), Dest(&InOut), State(InState)
		{
		}

		void Run();

		/** The layout built on what its state had kept (and how): read by the verify switch. */
		bool BuiltOnKeptLayout() const { return bUseState; }

	private:
		/** A line box: how far it reaches above and below its baseline, half-leading and shifted boxes included. */
		struct FLineBox
		{
			float Top = 0.0f;
			float Bottom = 0.0f;
		};

		/** A cluster as placed on its line. A line's clusters are kept in visual order, left to right. */
		struct FPlaced
		{
			/** Its elements, half-open, in logical order. */
			int32 Start = 0;
			int32 End = 0;
			/** Its items in Out.Items, half-open. */
			int32 ItemStart = 0;
			int32 ItemEnd = 0;
			/** Left edge of its pen box: its glyph advance plus its letter spacing. */
			float X0 = 0.0f;
			float Advance = 0.0f;
			float Spacing = 0.0f;
			uint8 Level = 0;
			bool bRightToLeft = false;
			bool bWhitespace = false;
			bool bImage = false;
			/** Whitespace at the line's logical end, which hangs outside the line. */
			bool bTrailing = false;
			/** A Truncate or Ellipsis cut removed it. */
			bool bCut = false;
			/** Its inline object in Out.Images / Out.Emojis. */
			int32 ImageIndex = INDEX_NONE;
			int32 EmojiIndex = INDEX_NONE;

			/** Letter spacing follows a cluster in reading order: right of it left to right, left of it right to left. */
			float GlyphLeft() const { return bRightToLeft ? X0 + Spacing : X0; }
			float BoxRight() const { return X0 + Advance + Spacing; }
		};

		const FDreamTextLayoutInput& In;
		/** The display list the layout is for. */
		FDreamTextDisplayList& Out;
		/**
		 * The list lines are placed into: Out, or the scratch list an in-place edit places its lines into before they are
		 * spliced into Out. Everything placing a line writes through it.
		 */
		FDreamTextDisplayList* Dest = nullptr;

		/** What this text's last layout kept, to build on and to keep this one in; null for a layout that keeps nothing. */
		FDreamTextLayoutStateData* State = nullptr;
		/** State holds a layout made under this one's measure key: its paragraphs may be taken. */
		bool bUseState = false;
		/** ...and its lines were broken at the same width under the same policy: a paragraph taken keeps its lines. */
		bool bBreakKeySame = false;
		/** ...and every input a line's placement reads is the same: a line whose content is the same keeps its placement. */
		bool bLinePlacementSame = false;
		/** ...and every input the parser and the styles read is the same: an edit may be seen from the content alone. */
		bool bParseInputsSame = false;
		FMeasureKey MeasureKey;
		/** The three switches, read once: DreamGUI.Text.IncrementalParse, IncrementalMeasure and InPlaceDisplayList. */
		bool bParseSwitch = false;
		bool bMeasureSwitch = false;
		bool bInPlaceSwitch = false;

		/*
		 * AN EDIT SEEN FROM THE CONTENT (TryParseInPlace). The kept arrays were moved out of the state, the edit's window
		 * spliced into them; every paragraph but the edited one is taken where it stands.
		 */
		bool bInPlaceParse = false;
		/** The new elements are [EditStart, EditStart + EditCount); the kept ones they replace were [EditStart, EditKeptEnd). */
		int32 EditStart = 0;
		int32 EditCount = 0;
		int32 EditKeptEnd = 0;
		/** How far everything after the window moved: in elements, in source code units, in plain-text code units. */
		int32 EditElementDelta = 0;
		int32 EditSourceDelta = 0;
		int32 EditPlainDelta = 0;
		/** The paragraph the window is in, the same index in this layout and the kept one; INDEX_NONE when the content is the same. */
		int32 EditParagraph = INDEX_NONE;
		/** The kept elements the window replaced, and of their records what a kept line's checks and the script search read. */
		TArray<FDreamUIText_TextProcessingElement> ReplacedElements;
		TArray<int32> ReplacedCaretIndices;
		TArray<uint32> ReplacedScripts;
		TBitArray<> ReplacedOwnScripts;
		/** The edited paragraph is measured through a window (MeasureWindowed): [MeasureStart, MeasureEnd) was measured again. */
		bool bMeasuredInWindow = false;
		int32 MeasureStart = 0;
		int32 MeasureEnd = 0;
		/** Where the edited paragraph's grapheme bits came out other than they were (INDEX_NONE: nowhere), the window aside. */
		int32 GraphemeChangeFirst = INDEX_NONE;
		int32 GraphemeChangeLast = INDEX_NONE;
		/** How far the glyphs and runs after the edited paragraph's window moved once it was measured. */
		int32 EditGlyphDelta = 0;
		int32 EditRunDelta = 0;
		/** Elements the in-place edit's rebases rewrote, counted once (FDreamTextLayoutStats::ElementsRebased). */
		bool bTailRebased = false;

		/*
		 * THE DISPLAY LIST. With DreamGUI.Text.InPlaceDisplayList on, every layout keeps its lines' line-local coordinates,
		 * and one that may edits the list it wrote last time where it is (EditDisplayListInPlace).
		 */
		bool bDisplayListInPlace = false;
		/** The lines placed by this layout; the others kept where they were, and moved only if their place changed. */
		TBitArray<> PlacedLines;
		/** Line-local coordinates, parallel to the display list's arrays: what Finish moves each line from, and what is kept. */
		TArray<FVector2f> LocalPens;
		TArray<FVector2f> LocalCarets;
		TArray<FVector2D> LocalImages;
		TArray<FVector2D> LocalEmojis;
		TArray<FVector2f> LocalRuns;
		/** The scratch list the lines an in-place edit changes are placed into. */
		FDreamTextDisplayList Scratch;

		/*
		 * PAINTS (FDreamTextItemStyle::PaintIndex, FDreamTextDisplayList::PaintFragments).
		 */
		/** Some element has a paint name or a removed paint: the display list has paint runs to measure. */
		bool bAnyPaint = false;
		/** PaintNames by name, as items name them. */
		TMap<FName, int32> PaintIndexByName;
		/** Items a paint run's box leaves out: what a clamp cut, and whitespace hanging off a line's end. */
		TBitArray<> ItemsOutsideRunBox;
		/** The last item metrics asked for: consecutive items are mostly in one face at one size. */
		float LastItemMetricsSize = -1.0f;
		int32 LastItemMetricsFace = INDEX_NONE;
		float LastItemAscent = 0.0f;
		float LastItemDescent = 0.0f;

		/** Rich text: the style an element added at the text's end takes, and whether markup stands right before the end. */
		FRichTextParseResult EndStyle;
		bool bEndFollowsMarkup = false;
		/** The elements that are inline objects, in order (FDreamTextLayoutStateData::InlineObjects). */
		TArray<int32> InlineObjects;

		UDreamUIFontData_BaseObject* Font = nullptr;
		UDreamUIRichTextImageData_BaseObject* RichTextImageData = nullptr;
		UDreamUIFontEmojiData* EmojiData = nullptr;

		// Resolved once up front, exactly as the old function derived them from the canvas and widget.
		float FontSize = 0.0f;
		float MaxFontSize = 0.0f;
		bool bPixelPerfect = false;
		float RootCanvasScale = 1.0f;
		float DynamicPixelsPerUnit = 1.0f;
		float OneDivideRootCanvasScale = 1.0f;
		float OneDivideDynamicPixelsPerUnit = 1.0f;
		bool bShouldScaleFontSizeWithRootCanvas = false;
		bool bUseKerning = false;
		/** Optional ligatures: asked for, and no letter spacing to keep them apart (CSS Text). */
		bool bLigatures = false;
		bool bCanShape = false;
		float OriginLineHeight = 0.0f;
		float LineHeightScale = 1.0f;
		float WrapWidth = 0.0f;
		float ItalicSlope = 0.0f;

		FRichTextParser RichTextParser;
		FRichTextParseResult RichTextParseResult;
		TArray<FRichTextParseResult> RichTextPropertyArray;
		TArray<FDreamUIText_TextProcessingElement> TextProcessingArray;
		/** One bit per element: rich-text markup stood right before it in the source. */
		TBitArray<> FollowsMarkup;
		TArray<FTagRecord> TagRecords;

		/** Font box at one size: what a line's height and baseline are built from. */
		struct FSizeMetrics
		{
			float Ascent = 0.0f;
			float Descent = 0.0f;
			float LineHeight = 0.0f;
		};
		/** One font box per size AND face: a fallback's box is not the primary's. */
		struct FMetricsKey
		{
			float Size = 0.0f;
			int32 FaceIndex = 0;
			bool operator==(const FMetricsKey& Other) const { return Size == Other.Size && FaceIndex == Other.FaceIndex; }
			friend uint32 GetTypeHash(const FMetricsKey& Key) { return HashCombine(::GetTypeHash(Key.Size), ::GetTypeHash(Key.FaceIndex)); }
		};
		TMap<FMetricsKey, FSizeMetrics> MetricsBySize;
		const FSizeMetrics& MetricsFor(float Size, int32 FaceIndex = 0);

		/** The font's underline and strikethrough at one size, from its primary face; bValid is false when the font has none to give. */
		struct FDecorationMetrics
		{
			bool bValid = false;
			float UnderlinePosition = 0.0f;
			float UnderlineThickness = 0.0f;
			float StrikethroughPosition = 0.0f;
			float StrikethroughThickness = 0.0f;
		};
		TMap<float, FDecorationMetrics> DecorationMetricsBySize;
		const FDecorationMetrics& DecorationMetricsFor(float Size);

		TMap<int32, EDreamUIFontFaceStyle> FaceStyles;
		/** What a face itself is -- bold, italic -- which decides what of a style has to be made up. */
		EDreamUIFontFaceStyle FaceStyleOf(int32 FaceIndex);
		TMap<int32, bool> FaceColors;
		/** Whether a face is a colour (emoji) face, asked once per face: its glyphs are never emboldened. */
		bool IsColorFaceOf(int32 FaceIndex);
		/** Every regular face's scale (FDreamFontFaceTable::GetScale), copied once: a scaled face's glyphs and line box are at its size. */
		TArray<float> FaceScales;
		float FaceScaleOf(int32 FaceIndex) const { return FaceScales.IsValidIndex(FaceIndex) ? FaceScales[FaceIndex] : 1.0f; }

		/**
		 * The languages the text's elements are in, what FMeasured::LanguageIndex and FDreamShapeElement::LanguageIndex
		 * index: 0 is the text's own (FDreamTextLayoutInput::Language, the game's when empty), then one per distinct
		 * <lang=xx>. Past 255 of them a tag's text is taken to be in the text's own.
		 */
		TArray<FDreamTextLanguage> Languages;
		TMap<FName, uint8> LanguageIndexByTag;
		uint8 LanguageIndexFor(FName Tag);
		/** The face a cluster of code points resolves to in an element's style and language (FDreamFontFaceResolver). */
		FDreamFontFaceChoice ResolveFace(TConstArrayView<uint32> Cluster, const FRichTextParseResult& Style, uint8 LanguageIndex) const;
		/** Appends the element's code points after its base -- the rest of an emoji sequence, a variation selector -- from its source span. None for an escaped element. */
		void AppendSequenceCodepoints(int32 ElementIndex, TArray<uint32>& OutCodepoints) const;
		/** The element as the text spells it: its source span, or the one character an escaped element stands for. */
		FString ElementText(int32 ElementIndex) const;
		/** Scratch for the code points of the cluster being resolved. */
		TArray<uint32> ClusterCodepoints;

		/** Tab stops (FDreamTextLayoutInput::TabSize), from the primary face's space at the text's size; measured when a tab first asks. */
		bool bTabMetricsReady = false;
		float TabSpaceWidth = 0.0f;
		float TabInterval = 0.0f;
		/** How wide a tab is that starts X from its line's start edge: to the next stop at least half a space away. */
		float TabAdvanceAt(float X);
		void SetTabAdvance(FMeasured& M, float X);

		TArray<FGlyphSource> Glyphs;
		/** How many shaped runs the paragraphs so far were cut into: what FMeasured::RunIndex counts. */
		int32 RunCount = 0;

		TArray<FMeasured> Measured;
		/** The text as laid out -- markup stripped, placeholders as spaces -- and where each element starts in it. */
		FString PlainText;
		TArray<int32> PlainStart;
		TArray<uint32> ElementCodepoints;
		TBitArray<> GraphemeStart;
		TBitArray<> ClusterStarts;
		/** ICU's raw line and word boundaries, and the break opportunities they make once clusters have their say. */
		TBitArray<> LineBreakRaw;
		TBitArray<> WordBreakRaw;
		TBitArray<> CanBreakBefore;
		TArray<FParagraph> Paragraphs;
		TArray<FLineRange> LineRanges;
		/** For each line, the kept line it repeats -- a kept placement it may take -- or INDEX_NONE. */
		TArray<int32> LineSources;
		/** Each placed line's stretch of the display list, its top and its height. */
		TArray<FLineSpan> LineSpans;
		/** The text's width ignoring automatic wrapping, taken before the line breaker moves the tabs to where their lines put them. */
		float UnwrappedPreferredWidth = 0.0f;

		// Running state of placement.
		float CurrentLineHeight = 0.0f;
		float ParagraphHeight = 0.0f;
		bool bHasClampContent = false;
		/**
		 * The line being placed is the last one that fits the box vertically, so it ends in an ellipsis
		 * whether or not it also runs past the right edge -- what the ellipsis stands for is the text
		 * below it. Set by Place, consumed by PlaceLine.
		 */
		bool bEllipsizeThisLine = false;
		float ParagraphHeight_ForClampContent = 0.0f;
		bool bShouldSetParagraphHeightForClampContent = false;
		/**
		 * The lines up to and with the one a clamp cut (0 without a cut): what the vertical alignment measures, and the text
		 * block. A text that does not wrap places the lines after the cut too, with nothing on them drawn.
		 */
		int32 ClampedLineCount = 0;
		/** The line being placed: its clusters in visual order, each element's caret, and its ellipsis item. */
		TArray<FPlaced> LinePlaced;
		TArray<float> LineCaretX;
		TBitArray<> LineHasCaret;
		int32 LineDotsItem = INDEX_NONE;
		/** Per element of the line being placed: the room justification adds after the cluster it leads, in its Spacing. */
		TArray<float> LineJustify;
		/** The end caret of the line being placed, when a middle ellipsis took the cluster it stands after: the ellipsis's end. */
		TOptional<float> LineEndCaretX;

		void Prepare();
		/** The measure key, and whether the kept layout was made under it: what may be taken from it at all. */
		void PrepareKept();
		FMeasureKey MakeMeasureKey() const;
		void Preprocess();
		void BuildPlainText();
		/** Sorts every element into its kind -- newline, space, tab, image, emoji, glyph -- and the text into paragraphs. */
		void Classify();
		/**
		 * Sorts one element that is not a newline into its kind, its style already in M: language, inline object, emoji,
		 * whitespace, tab, visible glyph. What Classify does for every such element, and an edit for the ones it reads.
		 */
		void ClassifyElement(int32 ElementIndex, FMeasured& M);
		/** A paragraph's tabs, right-to-left code points and inline objects counted, its bHasTab set. */
		void CountParagraph(FParagraph& P) const;

		/**
		 * An edit seen from the content: the common start and end of the kept text and this one give the kept elements the
		 * edit replaced, the window of new elements is read from the source alone and spliced into the kept arrays, and every
		 * paragraph but the one the window is in is taken where it stands. False -- with nothing changed -- for an edit it
		 * cannot see that way: one that adds, removes or rejoins a paragraph, that touches rich-text markup or an image, or
		 * whose paragraph would have to be measured whole when it is long (right to left, or with the window or the shape
		 * cache switched off). False too, with what was kept dropped, when an inline object's asset changed under the kept
		 * layout: the layout then starts from nothing.
		 */
		bool TryParseInPlace();
		/** For rich text: the window's text and what stands around it cannot change what any markup parses as. */
		bool IsMarkupSafeEdit(int32 Prefix, int32 OldEnd, int32 NewEnd, int32 ReadStart, int32 ReadEnd) const;
		/** A kept element as it was: what the kept layout's line checks read. Works whether the kept arrays were spliced or not. */
		FDreamUIText_TextProcessingElement KeptElementAt(int32 KeptIndex) const;
		/** Finds the kept paragraph each paragraph is, and for each one that is not, the kept ones it was edited from. */
		void Lookup();
		uint64 MeasureHashOf(const FParagraph& P) const;
		uint64 PlaceHashOf(const FParagraph& P) const;
		/** A kept paragraph's hashes, worked out the first time a lookup needs them and kept on it. */
		uint64 KeptMeasureHashOf(int32 KeptIndex);
		uint64 KeptPlaceHashOf(int32 KeptIndex);
		/** Whether a kept paragraph measures exactly as P would: its text, its elements' kinds, sizes, weights, languages, objects. */
		bool MatchesKept(const FParagraph& P, const FParagraph& K) const;
		/** How many of P's elements at its start (or its end) are written exactly as the kept paragraph's are. */
		int32 CountSameText(const FParagraph& P, const FParagraph& K, bool bFromEnd) const;
		/** Where an element of this text, or of the kept one, starts in its plain text; its length at the end. */
		int32 PlainAt(int32 ElementIndex) const { return ElementIndex < PlainStart.Num() ? PlainStart[ElementIndex] : PlainText.Len(); }
		int32 KeptPlainAt(int32 ElementIndex) const { return ElementIndex < State->PlainStart.Num() ? State->PlainStart[ElementIndex] : State->PlainText.Len(); }
		/** The span a boundary analysis of paragraphs [First, Last] runs over: their elements and text, newlines included. */
		FDreamTextBoundarySpan SpanOf(int32 FirstParagraph, int32 LastParagraph) const;
		/**
		 * Boundaries of one kind for every paragraph: a paragraph taken from the kept layout takes its bits, an edited one is
		 * analysed again around the edit (AnalyseWindow), and the rest are analysed whole, consecutive ones in one go.
		 */
		void AnalyseBoundaries(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, const TBitArray<>* KeptBits);
		/**
		 * An edited paragraph's boundaries: its donors' at its unchanged start and end, and ICU's from two boundaries and a
		 * margin before the edit (before any dictionary run it is in) to the first boundary after it that the kept analysis
		 * has too -- from a boundary on, what follows depends only on the text that follows, and that is the kept text.
		 */
		void AnalyseWindow(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, const TBitArray<>& KeptBits, int32 ParagraphIndex);
		/**
		 * The edited paragraph's boundaries after an edit seen from the content: its kept bits are in place but for the
		 * window's, so ICU walks from before the window to the first boundary after it that the kept bits have too (the kept
		 * ones saved first, since the walk writes over them); a short paragraph is analysed whole. Where a bit came out other
		 * than it was is reported, the window's own elements aside (INDEX_NONE: nowhere).
		 */
		void AnalyseEditedParagraph(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, bool bHaveKeptBits, int32& OutChangeFirst, int32& OutChangeLast);
		void AnalyseGraphemes();
		/** Each paragraph measured, or taken from the kept layout; then the clusters and the unwrapped width. */
		void MeasureParagraphs();
		/**
		 * The edited paragraph after an edit seen from the content: through a window (MeasureWindowed) when it is long, else
		 * whole; its glyphs spliced in where its kept glyphs were, and everything after it rebased.
		 */
		void MeasureEditedParagraph();
		/**
		 * Measures [MeasureStart, MeasureEnd) of the edited paragraph again -- the edit and a segment of untouched text on
		 * either side -- and splices it into the paragraph's kept measurement. False, with nothing changed, when the window's
		 * edges would not hold (its scripts or its runs do not join the rest as they did): the paragraph is then measured whole.
		 */
		bool MeasureWindowed(FParagraph& P);
		/** The same for a font that does not shape: the window is the edit's grapheme clusters and the one on either side. */
		bool MeasureWindowedByCodepoint(FParagraph& P);
		/** An element's measurement as Classify leaves it: its style and kind kept, everything measuring writes set back. */
		void ResetMeasurement(int32 ElementIndex);
		/** Moves what follows the edited paragraph's new glyphs by the glyph, run and element deltas. */
		void RebaseAfterEdit(int32 FirstElement, int32 FirstGlyph);
		/** Copies a kept paragraph's measurement over P's elements, its glyphs and runs renumbered from here. */
		void ReuseParagraph(FParagraph& P, const FParagraph& K);
		void MeasureParagraphByCodepoint(int32 Start, int32 End);
		/**
		 * The elements [From, To) of the paragraph starting at ParagraphStart measured per code point, as a walk over the
		 * whole paragraph measures them: the kerning partner and face before From are taken from element From - 1.
		 */
		void MeasureCodepointRange(int32 ParagraphStart, int32 From, int32 To);
		bool MeasureParagraphByShaping(FParagraph& P);
		/** The shaping input of the elements [Start, End) of a paragraph, and the code points their sequences add. */
		void MakeShapeElements(int32 ParagraphStart, int32 Start, int32 End, TArray<FDreamShapeElement>& OutElements, TArray<uint32>& OutSequences) const;
		FDreamShapeParams MakeShapeParams(const TArray<uint32>& SequenceCodepoints) const;
		/**
		 * Shaped runs into the elements' records and Glyphs, appended at the end of Glyphs: what MeasureParagraphByShaping
		 * and a window both do. ElementBase is the paragraph index of the runs' element 0; runs are numbered from FirstRun.
		 */
		void ApplyShapedRuns(const TArray<FDreamShapedRun>& ShapedRuns, int32 ElementBase, int32 FirstRun);
		/** Every shaped glyph's quad in [Begin, End): the kept one for the same glyph where P's donors had it, else the font's. */
		void FillGlyphQuads(const FParagraph& P, int32 Begin, int32 End);
		/** The same from the quads the state remembers by glyph, asking the font only for the ones it does not. */
		void FillGlyphQuadsFromTable(int32 Begin, int32 End);
		/** Groups a paragraph's elements into clusters and gives each its letter spacing and width. */
		void FinishClusters(int32 Start, int32 End);
		/**
		 * FinishClusters for the elements [From, To] of the paragraph [ParagraphStart, ParagraphEnd), the clusters around them
		 * read but not written; the unwrapped tab advances and pens go on to the paragraph's end when it has a tab.
		 */
		void FinishClustersRange(int32 ParagraphStart, int32 ParagraphEnd, int32 From, int32 To, bool bTabsToEnd);
		/**
		 * Each element of [From, Last] of the paragraph starting at ParagraphStart given its advance on one unwrapped line --
		 * its glyphs and the letter spacing its cluster carries, a tab the distance to its stop -- the pen taken up where
		 * PenAfter[From - 1] left it. What a measured paragraph has before the line breaker moves its tabs to its lines.
		 */
		void SetUnwrappedAdvances(int32 ParagraphStart, int32 From, int32 Last);
		float LetterSpacingFor(int32 ElementIndex) const;
		/**
		 * A paragraph's width on one line, the ink of negative letter spacing included; bOutAny is false for one with nothing in
		 * it. Each element's PenAfter is written on the way; From > Start resumes the sum from PenAfter[From - 1] (the ink walk,
		 * when there is one, still walks the whole paragraph).
		 */
		float ParagraphPreferredWidth(int32 Start, int32 End, bool& bOutAny, int32 From = INDEX_NONE);
		/** A shaped glyph's atlas quad at InFontSize (its run's size times its face's scale), measured back in text units. */
		FDreamUICharData FetchGlyphQuad(int32 FaceIndex, uint32 GlyphIndex, float InFontSize, bool bInBold, bool bColorFace) const;
		/**
		 * The size a glyph is asked of the font at, given the size it would be: a colour glyph in world space, where there is no
		 * device size to raster at, never below 64 px; InOutBackScale is multiplied by what measures it back.
		 */
		float RasterSizeFor(bool bColorFace, float InSize, float& InOutBackScale) const;
		/**
		 * How an emoji element is drawn, in this order: the emoji data's picture for its exact sequence; a colour face that
		 * has the whole cluster; the emoji data's picture for its base; else a glyph, from whatever face the shaper finds.
		 * True for a picture, with M.EmojiItem set.
		 */
		bool DrawsEmojiAsImage(int32 ElementIndex, FMeasured& M);
		void AnalyseBreaks();
		/** Break opportunities from the raw bits and the clusters for the elements [Begin, End). */
		void CombineBreaks(int32 Begin, int32 End);
		/** For each edited paragraph, how many of its elements at its start and its end came out exactly as its donors' did. */
		void CompareWithDonors();
		/** Element i of this layout and element j of the kept one agree in everything breaking and placing a line reads. */
		bool SameAsKept(int32 i, int32 j, const FParagraph& P, const FParagraph& K, bool bFromEnd) const;
		void BreakLines();
		/**
		 * P's lines from From, a line start, to its end: greedy, each line decided from its own start alone. Once a line
		 * starts inside P's unchanged end, where a line of its suffix donor started too, the donor's lines are P's lines.
		 */
		void BreakParagraphFrom(const FParagraph& P, int32 From, int32 HardBreak);
		void AddLine(int32 Start, int32 End, int32 HardBreak, int32 DecidedAt, int32 Source);
		/**
		 * The kept paragraph K's lines from FirstLine to its last, moved by Delta elements, the last one ended at HardBreak;
		 * with bKeepPlacement each may keep its placement too.
		 */
		void CopyKeptLines(const FParagraph& K, int32 FirstLine, int32 Delta, int32 HardBreak, bool bKeepPlacement);
		/** Every tab where its own line puts it: what the lines' placement reads. */
		void FixTabs();
		void Place();
		void PlaceLine(int32 LineIndex);
		/** Whether a kept line's placement is this line's, and by how much its source positions moved. */
		bool CanReuseLine(int32 LineIndex, int32 KeptIndex, int32& OutSourceDelta) const;
		void ReuseLine(int32 LineIndex, int32 KeptIndex, int32 SourceDelta, FLineSpan& Span);
		/**
		 * The display list the state wrote, edited where it is (DreamGUI.Text.InPlaceDisplayList): the lines before the first
		 * changed one and after the last are kept, the ones between placed into the scratch list and spliced in, those after
		 * rebased. False -- with nothing touched -- when the list is not the one the state wrote, or its lines cannot be kept.
		 */
		bool EditDisplayListInPlace();
		/** Whether kept line KeptIndex can stand for line LineIndex where it is in the list, and how far its elements and source moved. */
		bool CanKeepLineInPlace(int32 LineIndex, int32 KeptIndex, int32& OutElementDelta, int32& OutSourceDelta) const;
		/** Places line LineIndex at its top into Dest, its span relative to Dest's arrays, and counts its visible characters. */
		void PlaceOneLine(int32 LineIndex, FLineSpan& Span);
		/** The lines' line-local coordinates, copied from the list as placed (before Finish): what the state keeps. */
		void KeepLocalLines();
		/** A line's items, carets, inline objects and runs set to their line-local coordinates plus the line's offsets: what Finish does. */
		void FinishLine(int32 LineIndex, float XOffset, float LineY);
		/** Places one cluster at the pen: its items, its inline object, the carets of its grapheme clusters. */
		void PlaceCluster(int32 Start, int32 End, bool bRightToLeft, uint8 Level, bool bTrailing, int32 LineIndex, int32 RangeStart,
			float& InOutPenX, float Baseline, const FLineBox& Box, float LineCentre);
		/**
		 * Justification of the line about to be placed (ParagraphHAlign Justify): what it lacks of its target width, spread
		 * evenly over its opportunities into LineJustify. True when the line was stretched.
		 */
		bool JustifyLine(int32 LineIndex, int32 TrailingStart);
		/** Whether a justified line may widen between two neighbouring clusters, led by these elements, under TextJustify. */
		bool IsJustifyOpportunity(int32 BeforeElement, int32 AfterElement) const;
		/**
		 * Truncate and Ellipsis on a placed line, measured against the box rather than the wrap width, because they are
		 * about what fits on screen. A line is cut at the end it reads towards -- the right of a left-to-right line, the
		 * left of a right-to-left one, where Slate puts the ellipsis too -- and bForceEllipsis ends the line with one
		 * whether or not it overflows (the last line that fits the box vertically). MiddleEllipsis without wrapping elides
		 * each line in its middle (MiddleEllipsizeLine); wrapped, it is an end ellipsis on the last line that fits.
		 */
		void ClampLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline, bool bForceEllipsis,
			int32 LineItemStart, int32 ImageStart, int32 EmojiStart);
		/**
		 * A line that does not fit the box keeps its start and its end and loses its middle to an ellipsis: the gap grows
		 * from the line's middle by width, on whichever side keeps the two halves closest, until the rest and the ellipsis
		 * fit; whitespace beside the gap goes with it; the end slides back against the ellipsis, and carets in the gap stand
		 * on it. Every line on its own; nothing after it is cut.
		 */
		void MiddleEllipsizeLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline);
		/** Hides a cluster a clamp removed: its glyphs stop counting, its strokes and its inline object go. */
		void CutCluster(FPlaced& Placed, TArray<int32>& InOutImagesToRemove, TArray<int32>& InOutEmojisToRemove);
		void RemoveInlineObjects(TArray<int32>& ImagesToRemove, TArray<int32>& EmojisToRemove);
		/** An ellipsis glyph as MakeEllipsisGlyph made it. */
		struct FEllipsisGlyph
		{
			FDreamUICharData Glyph;
			/** The size it was shaped and rasterized at: its style size times its face's scale. */
			float GlyphSize = 0.0f;
			bool bPending = false;
			bool bSyntheticBold = false;
			bool bSyntheticItalic = false;
		};
		/**
		 * The ellipsis in the style of the text it ends: that text's size and weight, from the face that style and that
		 * text's language choose for it. Says whether the glyph is still rasterizing, and what of bold and italic that face
		 * lacks and has to be made up.
		 */
		FEllipsisGlyph MakeEllipsisGlyph(const FMeasured& StyleElement);
		/** Adds a line's ellipsis item, its glyph box starting at DotsLeft, in the style of StyleElement; LineDotsItem is it. */
		void AddEllipsisItem(int32 LineIndex, int32 StyleElement, const FEllipsisGlyph& Dots, float DotsLeft, bool bBaseRightToLeft, float Baseline);
		/** Slides everything a line owns -- items, carets, inline objects, visual runs -- by the same amount. */
		void ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 VisualRunStart, FDreamUITextLineProperty& LineProperty, float XOffset);
		/** The same while a line is still being clamped: its items and inline objects, its element carets and its placed clusters. */
		void ShiftPlacedLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, float XOffset);
		void Finish();
		/** The painter's boxes (FDreamTextDisplayList's ContentBox, TextBlockBox and LineBoxes) from the finished lines. */
		void BuildBoxes(float XOffset, float YOffset);
		/** The paint runs' pieces, line by line, and each item's piece (FDreamTextDisplayList::PaintFragments). */
		void BuildPaintFragments();
		/** Copies the lines as placed, before Finish moves them, into the kept layout. */
		void KeepLines();
		/** Moves this layout's arrays into the kept layout, or empties it when the atlas changed under the layout. */
		void KeepState();
		bool IsLineBaseRightToLeft(const FLineRange& Range) const;
		/** An item's box up and down from its pen: its face's ascent and descent at Size, the shaper's YOffset taken back out. */
		void SetItemMetrics(FDreamTextGlyphItem& Item, float Size, int32 FaceIndex, float YOffset);

		bool IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		void GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FRichTextParseResult& RichTextResult) const;
		void GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FDreamUIFontEmojiDataItem* EmojiItem) const;
		/** An inline object's box -- an <img> placeholder or an emoji picture -- in text units, as the element's style sizes it. */
		FDreamUICharData GetInlineObjectGeo(int32 ElementIndex) const;
		FDreamUICharData GetInlineObjectGeo(const FMeasured& M) const;
		/**
		 * One code point's glyph from one face at InFontSize (the style size times the face's scale), for a layout that does
		 * not shape: rasterized at the device size and measured back, and kerned against PrevCharCode (0: no left neighbour).
		 */
		FDreamUICharData GetCodepointGlyph(uint32 PrevCharCode, uint32 CharCode, int32 FaceIndex, float InFontSize, bool bInBold, bool bColorFace) const;
		/** The underline ('_') or strikethrough ('-') an item of this element is drawn with, measured from a pen raised by GlyphYOffset. */
		FDreamUICharData GetDecorationGlyph(bool bStrikethrough, const FMeasured& M, float GlyphYOffset, bool& bOutPending);
		/** Gives an item the strokes its style asks for, or takes them away while their glyph is still rasterizing. */
		void AssignDecorations(FDreamTextGlyphItem& Item, const FMeasured& M, float GlyphYOffset);
		/**
		 * The item style of an element: its parse result, with the paint the innermost of its paint and its colour gives it
		 * (FDreamTextItemStyle::PaintIndex, a name added to Out.PaintNames the first time an item names it, or bPaintRemoved).
		 */
		FDreamTextItemStyle MakeStyle(const FMeasured& M);
		/** The left-most and right-most x the painter will write for an item's glyph, sheared if italic. */
		void ItemInkExtent(const FDreamTextGlyphItem& Item, float& OutLeft, float& OutRight) const;
		int32 CaretIndexOf(int32 ElementIndex) const;
		/** CaretIndexOf for an element of the kept layout. */
		int32 KeptCaretIndexOf(int32 ElementIndex) const;
		/** True when lines wrap: the VerticalOverflow policy, or UMG-style AutoWrapText on top of another. */
		bool ShouldWrap() const { return In.OverflowType == EDreamUITextOverflowType::VerticalOverflow || In.bAutoWrapText; }
		/** True when the policy cuts what does not fit rather than letting it hang out. */
		bool IsClampMode() const
		{
			return In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis
				|| In.OverflowType == EDreamUITextOverflowType::MiddleEllipsis;
		}
		/** The line box a line gets: every face and size on it, superscripts and subscripts where they sit, tall inline objects. */
		FLineBox ComputeLineBox(int32 LineIndex);
	};

	void FLayoutRun::Prepare()
	{
		Font = In.Font.Get();
		RichTextImageData = In.RichTextImageData.Get();
		EmojiData = Font->GetEmojiData();

		MaxFontSize = Font->GetFontSizeLimit();
		FontSize = FMath::Clamp(In.FontSize, 0.0f, MaxFontSize);
		bPixelPerfect = In.bPixelPerfect;
		RootCanvasScale = In.RootCanvasScale;
		DynamicPixelsPerUnit = In.DynamicPixelsPerUnit * RootCanvasScale;
		OneDivideRootCanvasScale = 1.0f / RootCanvasScale;
		OneDivideDynamicPixelsPerUnit = 1.0f / DynamicPixelsPerUnit;
		bShouldScaleFontSizeWithRootCanvas = false;

		if (In.bRenderToWorldSpace)
		{
			bPixelPerfect = false;
			if (DynamicPixelsPerUnit != 1.0f && Font->GetSupportDynamicPixelsPerUnit())
			{
				bShouldScaleFontSizeWithRootCanvas = true;
			}
		}
		else
		{
			if (RootCanvasScale != 1.0f)
			{
				bShouldScaleFontSizeWithRootCanvas = true;
			}
			else
			{
				if (DynamicPixelsPerUnit != 1.0f && Font->GetSupportDynamicPixelsPerUnit())
				{
					bShouldScaleFontSizeWithRootCanvas = true;
				}
			}
		}

		Font->PrepareForLayout(In.ExpandMeshSize);
		ItalicSlope = Font->GetGlyphPaintStyle(FVector2f(1.0f, 1.0f), In.ExpandMeshSize).ItalicSlope;
		bUseKerning = In.bUseKerning && Font->HasKerning();
		// CSS Text: when the spacing between characters is not the font's own, optional ligatures are not used.
		bLigatures = In.bAllowLigatures && In.FontSpace.X == 0.0f;

		const bool bUseBold = In.FontStyle == EDreamUITextFontStyle::Bold || In.FontStyle == EDreamUITextFontStyle::BoldAndItalic;
		const bool bUseItalic = In.FontStyle == EDreamUITextFontStyle::Italic || In.FontStyle == EDreamUITextFontStyle::BoldAndItalic;

		if (In.bRichText)
		{
			RichTextParser.Clear();
			RichTextParser.Prepare(FontSize, In.Color, bUseBold, bUseItalic, In.bUnderline, In.bStrikethrough, In.RichTextFilterFlags, RichTextParseResult);
		}
		else
		{
			RichTextParseResult.Color = In.Color;
			RichTextParseResult.Bold = bUseBold;
			RichTextParseResult.Italic = bUseItalic;
			RichTextParseResult.Underline = In.bUnderline;
			RichTextParseResult.Strikethrough = In.bStrikethrough;
			RichTextParseResult.Size = FontSize;
		}

		OriginLineHeight = Font->GetLineHeight(FontSize);
		// Scales each line's height without touching glyph size. FontSpace.Y stays outside it: that
		// one is a flat distance the author asked for, not something that should follow the font.
		LineHeightScale = FMath::Max(0.0f, In.LineHeightPercentage);
		// Wrapping and the box are separable: an author can ask for a narrow column that still centres
		// over the full widget. Truncate and Ellipsis deliberately keep measuring against the box,
		// because those are about what fits on screen rather than where lines break.
		WrapWidth = In.WrapTextAt > 0.0f ? In.WrapTextAt : In.Width;

		CurrentLineHeight = OriginLineHeight;

		// The text's own language is entry 0; a <lang=xx> met while measuring adds its own. The kept layout's has what HarfBuzz
		// was told for it, which is kept with it so it is not asked again.
		Languages.Reset();
		Languages.Add(FDreamTextLanguage::Make(In.Language));
		if (State != nullptr && State->Languages.Num() > 0 && State->Languages[0].Name.Equals(Languages[0].Name, ESearchCase::CaseSensitive))
		{
			Languages[0].ShapingLanguage = State->Languages[0].ShapingLanguage;
		}
		LanguageIndexByTag.Reset();
		bParseSwitch = GDreamTextIncrementalParse != 0;
		bMeasureSwitch = GDreamTextIncrementalMeasure != 0;
		bInPlaceSwitch = GDreamTextInPlaceDisplayList != 0;
		// Copied rather than held: a font may rebuild its table whenever it is asked for it again.
		const FDreamFontFaceTable& FaceTable = Font->GetFaceTable();
		FaceScales.Reset(FaceTable.Faces.Num());
		for (int32 FaceIndex = 0; FaceIndex < FaceTable.Faces.Num(); FaceIndex++)
		{
			FaceScales.Add(FaceTable.GetScale(FaceIndex));
		}
		bCanShape = FDreamTextShaper::CanShape(Font);
		PrepareKept();
	}

	FMeasureKey FLayoutRun::MakeMeasureKey() const
	{
		FMeasureKey Key;
		Key.Font = FObjectKey(Font);
		Key.Atlas = FObjectKey(Font->GetFontTexture());
		Key.EmojiData = FObjectKey(EmojiData);
		Key.RichTextImageData = FObjectKey(RichTextImageData);
		Key.LayoutEpoch = Font->GetLayoutEpoch();
		Key.FaceCount = Font->GetFaceCount();
		// The face table, whole: a font may change a fallback's ranges, cultures or scale without a layout epoch to say so.
		const FDreamFontFaceTable& FaceTable = Font->GetFaceTable();
		uint64 TableHash = FaceTable.bPreferColorEmoji ? 1 : 0;
		for (const FDreamFontFaceInfo& Face : FaceTable.Faces)
		{
			TableHash = MixHash(TableHash, ((uint64)GetTypeHash(Face.Scale) << 1) | (Face.bPreferOverPrimary ? 1 : 0));
			TableHash = MixHash(TableHash, ((uint64)Face.Ranges.Num() << 32) | (uint32)Face.Cultures.Num());
			for (const FInt32Interval& Range : Face.Ranges)
			{
				TableHash = MixHash(TableHash, ((uint64)(uint32)Range.Min << 32) | (uint32)Range.Max);
			}
			for (const FString& Culture : Face.Cultures)
			{
				TableHash = MixHash(TableHash, GetTypeHash(Culture));
			}
		}
		Key.FaceTableHash = TableHash;
		Key.FontSize = FontSize;
		Key.MaxFontSize = MaxFontSize;
		Key.LetterSpacing = In.FontSpace.X;
		Key.TabSize = In.TabSize;
		Key.ExpandMeshSize = In.ExpandMeshSize;
		Key.RootCanvasScale = In.RootCanvasScale;
		Key.DynamicPixelsPerUnit = In.DynamicPixelsPerUnit;
		Key.ItalicSlope = ItalicSlope;
		Key.bPixelPerfect = In.bPixelPerfect;
		Key.bRenderToWorldSpace = In.bRenderToWorldSpace;
		Key.bSupportDynamicPixelsPerUnit = Font->GetSupportDynamicPixelsPerUnit();
		Key.bUseKerning = bUseKerning;
		Key.bLigatures = bLigatures;
		Key.bCanShape = bCanShape;
		Key.bRichText = In.bRichText;
		Key.bAllowColorFaces = In.bAllowColorFaces;
		Key.RichTextFilterFlags = In.RichTextFilterFlags;
		Key.FlowDirection = In.FlowDirection;
		Key.PhraseWrap = In.PhraseWrap;
		Key.ParagraphHAlign = In.ParagraphHAlign;
		Key.TextJustify = In.TextJustify;
		Key.LastLineAlign = In.LastLineAlign;
		Key.Language = Languages[0].Name;
		Key.Culture = FInternationalization::Get().GetCurrentCulture()->GetName();
		return Key;
	}

	void FLayoutRun::PrepareKept()
	{
		if (State == nullptr)
		{
			return;
		}
		MeasureKey = MakeMeasureKey();
		// A face reloaded since -- a new file, a culture swap -- has another epoch; its glyphs are not the kept ones.
		bool bFacesSame = true;
		for (int32 k = 0; k < State->UsedFaces.Num() && bFacesSame; k++)
		{
			bFacesSame = Font->GetFaceIdentity(State->UsedFaces[k]) == State->UsedFaceIdentities[k];
		}
		bUseState = State->bValid && bFacesSame && State->MeasureKey == MeasureKey;
		if (!bUseState)
		{
			// Made under something else, so of no use: it goes now rather than with the end of this layout.
			State->Reset();
			return;
		}
		Stats.IncrementalLayouts++;
		const FDreamTextLayoutInput& Kept = State->Input;
		const bool bKeptWrap = Kept.OverflowType == EDreamUITextOverflowType::VerticalOverflow || Kept.bAutoWrapText;
		const float KeptWrapWidth = Kept.WrapTextAt > 0.0f ? Kept.WrapTextAt : Kept.Width;
		// A paragraph's lines are a function of its elements, the wrap width and the policy alone -- and of break bits, which
		// the kept layout has only when it wrapped.
		bBreakKeySame = bKeptWrap == ShouldWrap()
			&& (!ShouldWrap() || (KeptWrapWidth == WrapWidth && Kept.WrappingPolicy == In.WrappingPolicy && State->bHasBreakBits));
		// The kept lines' placement is there to take in the form this layout takes it in: line-local coordinates of the list it
		// wrote, or (the switch off) copies of the list.
		bLinePlacementSame = (bInPlaceSwitch ? State->bLocalLinesKept : State->bLinesKept) && SameLinePlacementInputs(Kept, In);
		bParseInputsSame = SameParseInputs(Kept, In);
	}

	bool FLayoutRun::IsColorFaceOf(int32 FaceIndex)
	{
		if (const bool* Found = FaceColors.Find(FaceIndex))
		{
			return *Found;
		}
		return FaceColors.Add(FaceIndex, Font->IsColorFace(FaceIndex));
	}

	uint8 FLayoutRun::LanguageIndexFor(FName Tag)
	{
		if (Tag.IsNone())
		{
			return 0;
		}
		if (const uint8* Found = LanguageIndexByTag.Find(Tag))
		{
			return *Found;
		}
		// A tag naming the text's own language is the text's own language: no run is cut at its edges.
		const FString Name = Tag.ToString();
		uint8 Index = 0;
		if (!Name.Equals(Languages[0].Name, ESearchCase::IgnoreCase) && Languages.Num() <= MAX_uint8)
		{
			Index = (uint8)Languages.Num();
			FDreamTextLanguage& Language = Languages.Add_GetRef(FDreamTextLanguage::Make(Name));
			// What HarfBuzz was told for it before, when the kept layout had it too.
			if (State != nullptr)
			{
				for (const FDreamTextLanguage& KeptLanguage : State->Languages)
				{
					if (KeptLanguage.Name.Equals(Language.Name, ESearchCase::CaseSensitive))
					{
						Language.ShapingLanguage = KeptLanguage.ShapingLanguage;
						break;
					}
				}
			}
		}
		LanguageIndexByTag.Add(Tag, Index);
		return Index;
	}

	FDreamFontFaceChoice FLayoutRun::ResolveFace(TConstArrayView<uint32> Cluster, const FRichTextParseResult& Style, uint8 LanguageIndex) const
	{
		FDreamFontFaceQuery Query;
		Query.Cluster = Cluster;
		Query.StyledFace = (Style.Bold || Style.Italic) ? Font->GetStyledFace(Style.Bold, Style.Italic) : 0;
		const FDreamTextLanguage& Language = Languages.IsValidIndex(LanguageIndex) ? Languages[LanguageIndex] : Languages[0];
		Query.Cultures = Language.PrioritizedCultureNames;
		Query.Presentation = FDreamFontFaceResolver::GetPresentation(Cluster);
		Query.bAllowColorFaces = In.bAllowColorFaces;
		return FDreamFontFaceResolver::Resolve(Font, Query);
	}

	void FLayoutRun::AppendSequenceCodepoints(int32 ElementIndex, TArray<uint32>& OutCodepoints) const
	{
		const FDreamUIText_TextProcessingElement& Element = TextProcessingArray[ElementIndex];
		// An escaped element is the one character it stands for. Every other element's span starts with its base -- an
		// <img> placeholder's one unit is no character of its own and holds nothing more.
		const int32 BaseUnits = Element.Unicode >= FDreamUIText_CodePoint::UNICODE_PLANE01_START ? 2 : 1;
		if (Element.bEscaped || Element.Length <= BaseUnits)
		{
			return;
		}
		const int32 End = FMath::Min(Element.StringIndex + Element.Length, In.Content.Len());
		for (int32 Cursor = Element.StringIndex + BaseUnits; Cursor < End;)
		{
			int Units = 1;
			OutCodepoints.Add(FDreamUIText_CodePoint::DecodeCodePointAt(In.Content, End, Cursor, Units));
			Cursor += Units;
		}
	}

	FString FLayoutRun::ElementText(int32 ElementIndex) const
	{
		const FDreamUIText_TextProcessingElement& Element = TextProcessingArray[ElementIndex];
		if (!Element.bEscaped)
		{
			return In.Content.Mid(Element.StringIndex, Element.Length);
		}
		FString Text;
		if (Element.Unicode >= FDreamUIText_CodePoint::UNICODE_PLANE01_START)
		{
			const uint32 Value = Element.Unicode - FDreamUIText_CodePoint::UNICODE_PLANE01_START;
			Text.AppendChar((TCHAR)(FDreamUIText_CodePoint::HIGH_SURROGATE_START + (Value >> 10)));
			Text.AppendChar((TCHAR)(FDreamUIText_CodePoint::LOW_SURROGATE_START + (Value & 0x3FF)));
		}
		else
		{
			Text.AppendChar((TCHAR)Element.Unicode);
		}
		return Text;
	}

	float FLayoutRun::TabAdvanceAt(float X)
	{
		if (!bTabMetricsReady)
		{
			bTabMetricsReady = true;
			// The primary face's space at the text's own size, plus the letter spacing every character gets: TabSize of
			// them, set in a row, end exactly on the first stop. Measured as a space in the text is -- rasterized at the
			// device size under a scaled canvas and measured back -- so the stop is where TabSize of them really end.
			TabSpaceWidth = FontSize > 0.0f ? GetCodepointGlyph(0, ' ', 0, FontSize, false, false).XAdvance : 0.0f;
			TabInterval = FMath::Max(In.TabSize, 0.0f) * (TabSpaceWidth + In.FontSpace.X);
		}
		if (TabInterval <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		float Distance = TabInterval - FMath::Fmod(FMath::Max(X, 0.0f), TabInterval);
		// A tab is never narrower than half a space (Blink's rule; CSS asks for 0.5ch): one that would be jumps a stop.
		if (Distance < TabSpaceWidth * 0.5f)
		{
			Distance += TabInterval;
		}
		return Distance;
	}

	void FLayoutRun::SetTabAdvance(FMeasured& M, float X)
	{
		// The whole advance is the distance to the stop: no letter spacing after it, so what follows starts on the stop.
		const float Width = TabAdvanceAt(X);
		M.ClusterAdvance = Width;
		M.LetterSpacing = 0.0f;
		M.Advance = Width;
		M.ClusterFitWidth = Width;
		M.Glyph.XAdvance = Width;
	}

	const FLayoutRun::FSizeMetrics& FLayoutRun::MetricsFor(float Size, int32 FaceIndex)
	{
		const FMetricsKey Key{ Size, FaceIndex };
		if (const FSizeMetrics* Found = MetricsBySize.Find(Key))
		{
			return *Found;
		}
		FSizeMetrics M;
		// Face 0 is the font itself, and GetAscent/GetDescent/GetLineHeight ARE its metrics -- going
		// through GetFaceMetrics for it would walk past whatever a subclass overrode. Only a fallback
		// or styled face needs the per-face question, and a font that cannot answer it says so.
		if (FaceIndex == 0 || !Font->GetFaceMetrics(FaceIndex, Size, M.Ascent, M.Descent, M.LineHeight))
		{
			M.Ascent = Font->GetAscent(Size);
			M.Descent = Font->GetDescent(Size);
			M.LineHeight = Font->GetLineHeight(Size);
		}
		return MetricsBySize.Add(Key, M);
	}

	const FLayoutRun::FDecorationMetrics& FLayoutRun::DecorationMetricsFor(float Size)
	{
		if (const FDecorationMetrics* Found = DecorationMetricsBySize.Find(Size))
		{
			return *Found;
		}
		// From the primary face, the way CSS takes a decoration from the decorating box's first available font: one
		// stroke under a line that mixes a Latin face and a CJK fallback, not two at two heights. Asked at the size in
		// text units, like every other font metric the layout reads.
		FDecorationMetrics Metrics;
		Metrics.bValid = Font->GetDecorationMetrics(0, Size, Metrics.UnderlinePosition, Metrics.UnderlineThickness,
			Metrics.StrikethroughPosition, Metrics.StrikethroughThickness);
		return DecorationMetricsBySize.Add(Size, Metrics);
	}

	EDreamUIFontFaceStyle FLayoutRun::FaceStyleOf(int32 FaceIndex)
	{
		if (const EDreamUIFontFaceStyle* Found = FaceStyles.Find(FaceIndex))
		{
			return *Found;
		}
		return FaceStyles.Add(FaceIndex, Font->GetFaceStyleFlags(FaceIndex));
	}

	bool FLayoutRun::IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const
	{
		return CharCode == ' ' && In.bRichText && !RichTextResult.ImageTag.IsNone();
	}

	void FLayoutRun::GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FRichTextParseResult& RichTextResult) const
	{
		// `<img=Tag/>` is as tall as the font and as wide as its aspect ratio makes it; `<img=Tag,H/>`
		// and `<img=Tag,W,H/>` say otherwise. Both are in text units already: the size handed in is the
		// text's own, never the one a glyph is rasterized at.
		const float AuthoredHeight = RichTextResult.ImageHeight;
		const float Height = AuthoredHeight > 0.0f ? AuthoredHeight : InFontSize;
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = Height;

		if (RichTextResult.ImageWidth > 0.0f)
		{
			OverrideCharData.Width = OverrideCharData.XAdvance = RichTextResult.ImageWidth;
			return;
		}
		FIntVector2 ImageSize;
		if (IsValid(RichTextImageData) && RichTextImageData->GetImageSize(RichTextResult.ImageTag, ImageSize) && ImageSize.Y != 0)
		{
			const float Ratio = (float)ImageSize.X / ImageSize.Y;
			OverrideCharData.Width = OverrideCharData.Width * Ratio;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * Ratio;
		}
	}

	void FLayoutRun::GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FDreamUIFontEmojiDataItem* EmojiItem) const
	{
		// As tall as the font, in text units, and as wide as the picture the emoji data chose makes it: the one for the
		// exact sequence, or the one for its base.
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = InFontSize;

		FIntVector2 ImageSize;
		if (EmojiItem != nullptr && UDreamUIFontEmojiData::GetItemImageSize(*EmojiItem, ImageSize) && ImageSize.Y != 0)
		{
			const float Ratio = (float)ImageSize.X / ImageSize.Y;
			OverrideCharData.Width = OverrideCharData.Width * Ratio;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * Ratio;
		}
	}

	FDreamUICharData FLayoutRun::GetInlineObjectGeo(int32 ElementIndex) const
	{
		return GetInlineObjectGeo(Measured[ElementIndex]);
	}

	FDreamUICharData FLayoutRun::GetInlineObjectGeo(const FMeasured& M) const
	{
		// Inline objects are not glyphs. They are sized in text units, from the font size or the size the tag asked
		// for, so no raster scale applies to them -- passing them the rasterized size made them DynamicPixelsPerUnit
		// times too big in world space -- and they never kern.
		FDreamUICharData ObjectData;
		if (M.bImageSpace)
		{
			GetRichTextImageCharData(ObjectData, M.Style.Size, M.Style);
		}
		else
		{
			GetEmojiCharData(ObjectData, M.Style.Size, M.EmojiItem);
		}
		return ObjectData;
	}

	float FLayoutRun::RasterSizeFor(bool bColorFace, float InSize, float& InOutBackScale) const
	{
		if (!bColorFace || !In.bRenderToWorldSpace || InSize <= 0.0f)
		{
			return InSize;
		}
		// World space has no device pixels to match: a colour glyph is rasterized at 64 px at least and minified by the
		// sampler, since a bitmap rasterized at the size in text units would be magnified into a blur.
		const float Floor = FMath::Min(64.0f, MaxFontSize);
		if (InSize >= Floor)
		{
			return InSize;
		}
		InOutBackScale *= InSize / Floor;
		return Floor;
	}

	FDreamUICharData FLayoutRun::GetCodepointGlyph(uint32 PrevCharCode, uint32 CharCode, int32 FaceIndex, float InFontSize, bool bInBold, bool bColorFace) const
	{
		Stats.QuadFetches++;
		FDreamUICharData OverrideCharData;
		/** Turns a length measured at the rasterized size back into text units; 1 when nothing was scaled. */
		float BackToTextUnits = 1.0f;
		if (bShouldScaleFontSizeWithRootCanvas)
		{
			// Three branches that differ only in which scale they apply; kept as three so the result
			// stays bit-for-bit what it was.
			float Scale, OneDivideScale;
			if (bPixelPerfect)
			{
				Scale = RootCanvasScale;
				OneDivideScale = OneDivideRootCanvasScale;
			}
			else if (DynamicPixelsPerUnit != 1.0f)
			{
				Scale = DynamicPixelsPerUnit;
				OneDivideScale = OneDivideDynamicPixelsPerUnit;
			}
			else
			{
				Scale = RootCanvasScale;
				OneDivideScale = OneDivideRootCanvasScale;
			}
			const float ScaledFontSize = InFontSize * Scale;
			InFontSize = FMath::Clamp(ScaledFontSize, 0.0f, MaxFontSize);
			// The font caps the size it will rasterize (GetFontSizeLimit). When that cap bites, the glyph
			// comes back smaller than the scale asked for, so measuring it back has to use the ratio that
			// was actually achieved: dividing by the nominal scale is what made a large font silently
			// shrink inside a scaled canvas.
			if (InFontSize > 0.0f && ScaledFontSize > InFontSize)
			{
				OneDivideScale = OneDivideScale * (ScaledFontSize / InFontSize);
			}
			InFontSize = RasterSizeFor(bColorFace, InFontSize, OneDivideScale);
			OverrideCharData = Font->GetFaceCharData(FaceIndex, CharCode, InFontSize, bInBold);
			OverrideCharData.Width = OverrideCharData.Width * OneDivideScale;
			OverrideCharData.Height = OverrideCharData.Height * OneDivideScale;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * OneDivideScale;
			OverrideCharData.XOffset = OverrideCharData.XOffset * OneDivideScale;
			OverrideCharData.YOffset = OverrideCharData.YOffset * OneDivideScale;
			BackToTextUnits = OneDivideScale;
		}
		else
		{
			InFontSize = RasterSizeFor(bColorFace, InFontSize, BackToTextUnits);
			OverrideCharData = Font->GetFaceCharData(FaceIndex, CharCode, InFontSize, bInBold);
			if (BackToTextUnits != 1.0f)
			{
				OverrideCharData.Width *= BackToTextUnits;
				OverrideCharData.Height *= BackToTextUnits;
				OverrideCharData.XAdvance *= BackToTextUnits;
				OverrideCharData.XOffset *= BackToTextUnits;
				OverrideCharData.YOffset *= BackToTextUnits;
			}
		}
		// PrevCharCode == 0 is "no left neighbour": the caller says so explicitly rather than passing the
		// character itself, which used to mean every doubled pair ("TT", "ll", "//") lost its kerning.
		if (bUseKerning && PrevCharCode != 0)
		{
			// Kerning comes back at the size the glyph was measured at, so it converts back the same way.
			const float KerningValue = Font->GetKerning(PrevCharCode, CharCode, InFontSize) * BackToTextUnits;
			OverrideCharData.XAdvance += KerningValue;
			OverrideCharData.XOffset += KerningValue;
		}
		return OverrideCharData;
	}

	FDreamUICharData FLayoutRun::GetDecorationGlyph(bool bStrikethrough, const FMeasured& M, float GlyphYOffset, bool& bOutPending)
	{
		Stats.QuadFetches++;
		const float Size = M.Style.Size;
		FDreamUICharData Glyph = Font->GetCharData(bStrikethrough ? '-' : '_', Size, M.bSyntheticBold);
		bOutPending = Glyph.bPending;
		const FDecorationMetrics& Metrics = DecorationMetricsFor(Size);
		if (Metrics.bValid)
		{
			// Where the font says the stroke goes and how thick it is (its post and OS/2 tables), drawn as a solid
			// strip: the atlas has no white texel of its own, so it samples one texel from inside the font's '_' or '-'.
			// At least one pixel of a screen canvas thick, so a small size does not lose its underline altogether.
			const float Position = bStrikethrough ? Metrics.StrikethroughPosition : Metrics.UnderlinePosition;
			const float Thickness = FMath::Max(bStrikethrough ? Metrics.StrikethroughThickness : Metrics.UnderlineThickness, OneDivideRootCanvasScale);
			const FVector2f Interior((Glyph.MinUV.X + Glyph.MaxUV.X) * 0.5f, (Glyph.MinUV.Y + Glyph.MaxUV.Y) * 0.5f);
			Glyph.MinUV = Interior;
			Glyph.MaxUV = Interior;
			Glyph.YOffset = Position + Thickness * 0.5f;
			Glyph.Height = Thickness;
		}
		else
		{
			// The glyph's own placement and thickness, its texels collapsed to their centre column so they stretch.
			const float UVX = (Glyph.MaxUV.X - Glyph.MinUV.X) * 0.5f + Glyph.MinUV.X;
			Glyph.MinUV.X = Glyph.MaxUV.X = UVX;
		}
		// Measured from the item's pen, which carries its glyph's vertical offset from the shaper: a stroke stays on
		// the line's baseline under a raised mark.
		Glyph.YOffset -= GlyphYOffset;
		return Glyph;
	}

	void FLayoutRun::AssignDecorations(FDreamTextGlyphItem& Item, const FMeasured& M, float GlyphYOffset)
	{
		// A stroke whose '_' or '-' is still on the rasterizer has no texel to be drawn from yet: it is left out and
		// the layout says it is waiting, so the font's OnGlyphsReady lays the text out again with it.
		if (Item.Style.bUnderline)
		{
			bool bPending = false;
			Item.UnderlineGlyph = GetDecorationGlyph(false, M, GlyphYOffset, bPending);
			if (bPending)
			{
				Dest->bHasPendingGlyphs = true;
				Item.Style.bUnderline = false;
			}
		}
		if (Item.Style.bStrikethrough)
		{
			bool bPending = false;
			Item.StrikethroughGlyph = GetDecorationGlyph(true, M, GlyphYOffset, bPending);
			if (bPending)
			{
				Dest->bHasPendingGlyphs = true;
				Item.Style.bStrikethrough = false;
			}
		}
	}

	FDreamTextItemStyle FLayoutRun::MakeStyle(const FMeasured& M)
	{
		const FRichTextParseResult& Result = M.Style;
		FDreamTextItemStyle Style;
		Style.Size = Result.Size;
		Style.Color = Result.Color;
		Style.bHasColor = Result.HasColor;
		Style.bHasMultiplyColor = Result.bHasMultiplyColor;
		Style.MultiplyColor = Result.MultiplyColor;
		Style.bBold = Result.Bold;
		Style.bItalic = Result.Italic;
		Style.bSyntheticBold = M.bSyntheticBold;
		Style.bSyntheticItalic = M.bSyntheticItalic;
		Style.bUnderline = Result.Underline;
		Style.bStrikethrough = Result.Strikethrough;
		Style.SupOrSub = Result.SupOrSubMode == ESupOrSubMode::Sup ? 1 : (Result.SupOrSubMode == ESupOrSubMode::Sub ? 2 : 0);
		// Whichever of a paint and a colour was set by the inner tag wins (CSS: a <color> inside a gradient run is solid, a
		// gradient inside a coloured run paints). A colour that wins keeps bHasColor as it always was. A custom style's
		// Multiply sets no colour of its own (it waits in MultiplyColor for whatever the glyph is painted with): it multiplies
		// the paint rather than ending it, and so does an entry that both multiplies and sets a paint, whose orders tie.
		if (Result.PaintOrder > Result.ColorOrder || (!Result.HasColor && Result.PaintOrder > 0))
		{
			if (Result.bPaintRemoved)
			{
				Style.bPaintRemoved = true;
			}
			else if (!Result.PaintName.IsNone())
			{
				if (const int32* Found = PaintIndexByName.Find(Result.PaintName))
				{
					Style.PaintIndex = *Found;
				}
				else
				{
					// Names are listed in the order items first name them.
					Style.PaintIndex = Out.PaintNames.Add(Result.PaintName);
					PaintIndexByName.Add(Result.PaintName, Style.PaintIndex);
				}
			}
		}
		return Style;
	}

	void FLayoutRun::SetItemMetrics(FDreamTextGlyphItem& Item, float Size, int32 FaceIndex, float YOffset)
	{
		if (Size != LastItemMetricsSize || FaceIndex != LastItemMetricsFace)
		{
			const FSizeMetrics& Metrics = MetricsFor(Size, FaceIndex);
			LastItemMetricsSize = Size;
			LastItemMetricsFace = FaceIndex;
			LastItemAscent = Metrics.Ascent;
			LastItemDescent = Metrics.Descent;
		}
		// Measured from the pen, which carries the shaper's vertical offset: a mark raised over its letter sits in its letter's box.
		Item.Ascent = LastItemAscent - YOffset;
		Item.Descent = LastItemDescent + YOffset;
	}

	void FLayoutRun::Preprocess()
	{
		const int32 ContentLength = In.Content.Len();
		RichTextPropertyArray.Reset();
		TextProcessingArray.Reset(ContentLength);
		TagRecords.Reset();
		FollowsMarkup.Empty();
		if (In.bRichText)
		{
			UDreamUIRichTextCustomStyleData* CustomStyleData = In.RichTextCustomStyleData.Get();
			const TMap<FName, FDreamUIRichTextCustomStyleItemData>* StyleMap = IsValid(CustomStyleData) ? &CustomStyleData->GetDataMap() : nullptr;
			// Open custom tags, as indices into TagRecords, outermost first.
			TArray<int32> OpenTags;
			// An element's style is the parser's state with the style of every open custom tag on top, outermost
			// first, composed again for every element. A style applied once, on the element where its tag was
			// parsed, was gone after the next tag the parser rebuilt its state for: a <b> nested inside a styled tag
			// took the style off everything after it.
			auto ComposeStyle = [this, StyleMap, &OpenTags]()
			{
				FRichTextParseResult Style = RichTextParseResult;
				if (StyleMap != nullptr && OpenTags.Num() > 0)
				{
					TArray<FSizeEffect> SizeEffects;
					for (const int32 RecordIndex : OpenTags)
					{
						const FTagRecord& Record = TagRecords[RecordIndex];
						if (const FDreamUIRichTextCustomStyleItemData* StyleItem = StyleMap->Find(Record.Name))
						{
							// The style's name is the paint's name when it sets one: the text resolves it to the entry's paint.
							StyleItem->ApplyToRichTextParseResult(Style, Record.Order, Record.Name);
							StyleItem->AppendSizeEffects(Record.Order, SizeEffects);
						}
					}
					// A style's size and the superscripts around it or inside it are folded together with the parser's own
					// size tags, in the order all of them were opened, rather than the style overwriting a size the parser
					// had already stepped down for a superscript.
					if (SizeEffects.Num() > 0)
					{
						RichTextParser.GetSizeEffects(SizeEffects);
						FoldSizeEffects(RichTextParser.GetOriginSize(), SizeEffects, Style);
					}
				}
				return Style;
			};
			bEndFollowsMarkup = false;
			for (int32 CharIndex = 0; CharIndex < ContentLength; CharIndex++)
			{
				const int32 MarkupStart = CharIndex;
				bool bAfterMarkup = false;
				RichTextParser.ClearImageTag();
				while (true)
				{
					// Every tag is acted on as it is parsed. A closing tag and the next opening tag can stand back to
					// back with no character between them, and the second parse used to overwrite what the first found.
					RichTextParseResult.CustomTagMode = ECustomTagMode::None;
					RichTextParseResult.bHyperlink = false;
					const int32 TagStart = CharIndex;
					if (!RichTextParser.Parse(In.Content, ContentLength, CharIndex, RichTextParseResult))
					{
						break;
					}
					bAfterMarkup = true;
					if (RichTextParseResult.CustomTagMode == ECustomTagMode::Start)
					{
						FTagRecord& Record = TagRecords.AddDefaulted_GetRef();
						Record.Name = RichTextParseResult.CustomTag;
						Record.bHyperlink = RichTextParseResult.bHyperlink;
						Record.FirstElement = TextProcessingArray.Num();
						Record.Order = RichTextParseResult.CustomTagOrder;
						Record.OpenAt = TagStart;
						OpenTags.Add(TagRecords.Num() - 1);
					}
					else if (RichTextParseResult.CustomTagMode == ECustomTagMode::End)
					{
						// The most recent tag of that name still open: a name can be used again once it has closed, and
						// closing the first tag of the name instead left the second one running to the end of the text.
						for (int32 OpenIndex = OpenTags.Num() - 1; OpenIndex >= 0; OpenIndex--)
						{
							FTagRecord& Record = TagRecords[OpenTags[OpenIndex]];
							if (Record.Name == RichTextParseResult.CustomTag)
							{
								Record.LastElement = TextProcessingArray.Num() - 1;
								Record.bClosed = true;
								Record.CloseAt = TagStart;
								OpenTags.RemoveAt(OpenIndex);
								break;
							}
						}
					}
					if (!RichTextParseResult.ImageTag.IsNone())//get image, append a blank placeholder
					{
						TextProcessingArray.Add(FDreamUIText_TextProcessingElement{ ' ', CharIndex, 1, EDreamUIText_CodeType::Text });
						FRichTextParseResult ImageStyle = ComposeStyle();
						ImageStyle.CharIndex = MarkupStart;
						RichTextPropertyArray.Add(ImageStyle);
						FollowsMarkup.Add(true);
						RichTextParseResult.ImageTag = NAME_None;//clear it
						RichTextParser.ClearImageTag();
					}
					if (CharIndex >= ContentLength)
					{
						break;
					}
				}
				if (CharIndex >= ContentLength)
				{
					// Markup ends the text: what is typed after it follows markup.
					bEndFollowsMarkup = bAfterMarkup;
					break;
				}

				FRichTextParseResult Style = ComposeStyle();
				Style.CharIndex = CharIndex;
				RichTextPropertyArray.Add(Style);
				FollowsMarkup.Add(bAfterMarkup);

				// A character reference is one element spelled with several code units: `&lt;` is the
				// one character '<'. Only rich text unescapes, exactly as UMG's RichTextBlock does.
				uint32 EscapedCodepoint = 0;
				int32 EscapeLength = 0;
				if (FRichTextParser::ReadEscape(In.Content, ContentLength, CharIndex, EscapedCodepoint, EscapeLength))
				{
					FDreamUIText_TextProcessingElement Element;
					Element.Unicode = EscapedCodepoint;
					Element.StringIndex = CharIndex;
					Element.Length = EscapeLength;
					// One code point, so its presentation is its own default, as segmentation decides it for any other.
					Element.Type = FDreamUIText_CodePoint::HasEmojiPresentation(EscapedCodepoint)
						? EDreamUIText_CodeType::Emoji : EDreamUIText_CodeType::Text;
					Element.bEscaped = true;
					TextProcessingArray.Add(Element);
					CharIndex += EscapeLength - 1;//the loop's own step takes the last unit
					continue;
				}
				TextProcessingArray.Add(FDreamUIText_CodePoint::ReadCodePoint(In.Content, ContentLength, CharIndex));
			}
			// The style of an element added at the text's end, after every tag the text closes or leaves open there: what a
			// character typed at the end takes when the next layout reads only the edit.
			EndStyle = ComposeStyle();
		}
		else
		{
			for (int32 CharIndex = 0; CharIndex < ContentLength; CharIndex++)
			{
				TextProcessingArray.Add(FDreamUIText_CodePoint::ReadCodePoint(In.Content, ContentLength, CharIndex));
			}
			FollowsMarkup.Init(false, TextProcessingArray.Num());
		}
		Stats.ElementsParsed += TextProcessingArray.Num();
	}

	void FLayoutRun::BuildPlainText()
	{
		const int32 Count = TextProcessingArray.Num();
		// The breakers see the text as laid out: markup stripped, placeholders as spaces.
		PlainText.Reset(Count + 4);
		PlainStart.SetNumUninitialized(Count);
		ElementCodepoints.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; i++)
		{
			const auto& Element = TextProcessingArray[i];
			PlainStart[i] = PlainText.Len();
			ElementCodepoints[i] = Element.Unicode;
			if (In.bRichText && IsRichTextImageSpace(Element.Unicode, RichTextPropertyArray[i]))
			{
				PlainText.AppendChar(TEXT(' '));
			}
			else if (Element.bEscaped)
			{
				// The source spells this `&lt;`; the breaker has to see the '<' it stands for.
				if (Element.Unicode >= 0x10000)
				{
					const uint32 Value = Element.Unicode - 0x10000;
					PlainText.AppendChar((TCHAR)(0xD800 + (Value >> 10)));
					PlainText.AppendChar((TCHAR)(0xDC00 + (Value & 0x3FF)));
				}
				else
				{
					PlainText.AppendChar((TCHAR)Element.Unicode);
				}
			}
			else
			{
				PlainText.Append(*In.Content + Element.StringIndex, Element.Length);
			}
		}
	}

	int32 FLayoutRun::CaretIndexOf(int32 ElementIndex) const
	{
		// The caret contract: every caret names its element's offset in the source string, in UTF-16
		// code units -- rich carets so UITextInput can walk the markup it was given, plain ones because an
		// element is a whole cluster (a surrogate pair, an emoji sequence) and the text is edited in code
		// units. Naming the element's position in the element list instead put every caret after an emoji
		// a unit or more short of its character, and an edit there landed inside the emoji. A rich element's style is its
		// parse result, CharIndex and all (Classify).
		return In.bRichText ? Measured[ElementIndex].Style.CharIndex : TextProcessingArray[ElementIndex].StringIndex;
	}

	bool FLayoutRun::DrawsEmojiAsImage(int32 ElementIndex, FMeasured& M)
	{
		M.EmojiItem = nullptr;
		// Without emoji data every emoji is a glyph: a colour face's, else a monochrome one, else the missing-glyph box --
		// monochrome, but a blank where the emoji should be tells the reader nothing at all.
		if (!IsValid(EmojiData))
		{
			return false;
		}
		// 1. An author's own picture for exactly this sequence beats any font.
		if (const FDreamUIFontEmojiDataItem* Item = EmojiData->FindBySequence(ElementText(ElementIndex)))
		{
			M.EmojiItem = Item;
			return true;
		}
		// 2. A colour face that has the whole cluster draws it, sequence and all: the picture for the base alone would
		// drop its skin tone or the rest of its ZWJ sequence.
		ClusterCodepoints.Reset();
		ClusterCodepoints.Add(TextProcessingArray[ElementIndex].Unicode);
		AppendSequenceCodepoints(ElementIndex, ClusterCodepoints);
		const FDreamFontFaceChoice Choice = ResolveFace(ClusterCodepoints, M.Style, M.LanguageIndex);
		if (Choice.bColor && Choice.bCoversCluster)
		{
			return false;
		}
		// 3. The picture for its base, which is what an asset made before sequences has.
		if (const FDreamUIFontEmojiDataItem* Item = EmojiData->FindByCodepoint(TextProcessingArray[ElementIndex].Unicode))
		{
			M.EmojiItem = Item;
			return true;
		}
		// 4. A monochrome face's glyph, or the missing-glyph box: the shaper's choice.
		return false;
	}

	int32 FLayoutRun::KeptCaretIndexOf(int32 ElementIndex) const
	{
		// A rich element's style is its parse result, CharIndex and all (Classify).
		if (!bInPlaceParse)
		{
			return State->Input.bRichText ? State->Measured[ElementIndex].Style.CharIndex : State->Elements[ElementIndex].StringIndex;
		}
		// The kept arrays were spliced: a kept element the edit replaced is as it was saved, one after the window is where the
		// edit moved it, its source position moved back.
		if (ElementIndex >= EditStart && ElementIndex < EditKeptEnd)
		{
			return ReplacedCaretIndices[ElementIndex - EditStart];
		}
		const bool bAfter = ElementIndex >= EditKeptEnd;
		const int32 Now = bAfter ? ElementIndex + EditElementDelta : ElementIndex;
		return (In.bRichText ? Measured[Now].Style.CharIndex : TextProcessingArray[Now].StringIndex) - (bAfter ? EditSourceDelta : 0);
	}

	FDreamUIText_TextProcessingElement FLayoutRun::KeptElementAt(int32 KeptIndex) const
	{
		if (!bInPlaceParse)
		{
			return State->Elements[KeptIndex];
		}
		if (KeptIndex >= EditStart && KeptIndex < EditKeptEnd)
		{
			return ReplacedElements[KeptIndex - EditStart];
		}
		if (KeptIndex < EditStart)
		{
			return TextProcessingArray[KeptIndex];
		}
		FDreamUIText_TextProcessingElement Element = TextProcessingArray[KeptIndex + EditElementDelta];
		Element.StringIndex -= EditSourceDelta;
		return Element;
	}

	/**
	 * Where what FDreamUIText_CodePoint::ReadCodePoint looked at to decide an element ending at ElementEnd stops: the code
	 * point after the element is always looked at -- whether there is one, and whether it joins -- and past a joiner the one
	 * after that. An element whose read stops at or before the first unit an edit changed reads the same in the edited text.
	 */
	int32 DreamTextLayoutElementReadEnd(const FString& Text, int32 ElementEnd)
	{
		const int32 Length = Text.Len();
		if (ElementEnd >= Length)
		{
			return ElementEnd + 1;
		}
		auto IsHighSurrogate = [](uint32 Unit)
		{
			return Unit >= FDreamUIText_CodePoint::HIGH_SURROGATE_START && Unit <= FDreamUIText_CodePoint::HIGH_SURROGATE_END;
		};
		const uint32 Next = (uint32)Text[ElementEnd];
		if (IsHighSurrogate(Next))
		{
			return ElementEnd + 2;
		}
		if (Next == FDreamUIText_CodePoint::UNICODE_ZWJ)
		{
			return ElementEnd + 1 < Length && IsHighSurrogate((uint32)Text[ElementEnd + 1]) ? ElementEnd + 3 : ElementEnd + 2;
		}
		return ElementEnd + 1;
	}

	bool FLayoutRun::IsMarkupSafeEdit(int32 Prefix, int32 OldEnd, int32 NewEnd, int32 ReadStart, int32 ReadEnd) const
	{
		const FString& OldText = State->Input.Content;
		const FString& NewText = In.Content;
		const int32 NewLength = NewText.Len();
		auto IsMarkupCharacter = [](TCHAR C) { return C == '<' || C == '>' || C == '&' || C == ';'; };
		// What the edit took out and what it put in are text through and through.
		for (int32 i = Prefix; i < OldEnd; i++)
		{
			if (IsMarkupCharacter(OldText[i]))
			{
				return false;
			}
		}
		for (int32 i = Prefix; i < NewEnd; i++)
		{
			if (IsMarkupCharacter(NewText[i]))
			{
				return false;
			}
		}
		// What is read again holds no tag and no character reference: it is read as plain text is.
		for (int32 i = ReadStart; i < ReadEnd; i++)
		{
			const TCHAR C = NewText[i];
			if (C == '<' || C == '>' || C == '&')
			{
				return false;
			}
		}
		// Not inside a tag: the nearest angle bracket before what is read is not a '<' still open, the nearest after it no '>'.
		for (int32 i = FMath::Min(ReadStart, Prefix) - 1; i >= 0; i--)
		{
			if (NewText[i] == '>')
			{
				break;
			}
			if (NewText[i] == '<')
			{
				return false;
			}
		}
		for (int32 i = FMath::Max(ReadEnd, NewEnd); i < NewLength; i++)
		{
			if (NewText[i] == '<')
			{
				break;
			}
			if (NewText[i] == '>')
			{
				return false;
			}
		}
		// No character reference the edit could make or break: the longest one, "&#x10FFFF;", is ten units after its '&'.
		constexpr int32 EscapeReach = 16;
		for (int32 i = FMath::Max(Prefix - EscapeReach, 0); i < Prefix; i++)
		{
			if (NewText[i] == '&')
			{
				return false;
			}
		}
		for (int32 i = NewEnd; i < FMath::Min(NewEnd + EscapeReach, NewLength); i++)
		{
			if (NewText[i] == ';')
			{
				return false;
			}
		}
		// Not strictly inside the markup between two elements: text put there would follow some of it and stand before the
		// rest, a style no kept element has.
		const TArray<FDreamUIText_TextProcessingElement>& Kept = State->Elements;
		int32 Low = 0;
		int32 High = Kept.Num();
		while (Low < High)
		{
			const int32 Mid = (Low + High) / 2;
			if (Kept[Mid].StringIndex + Kept[Mid].Length > Prefix)
			{
				High = Mid;
			}
			else
			{
				Low = Mid + 1;
			}
		}
		if (Low >= Kept.Num() || Kept[Low].StringIndex > Prefix)
		{
			const int32 GapStart = Low > 0 ? Kept[Low - 1].StringIndex + Kept[Low - 1].Length : 0;
			const int32 GapEnd = Low < Kept.Num() ? Kept[Low].StringIndex : OldText.Len();
			if (Prefix > GapStart && Prefix < GapEnd)
			{
				return false;
			}
		}
		return true;
	}

	bool FLayoutRun::TryParseInPlace()
	{
		bInPlaceParse = false;
		EditParagraph = INDEX_NONE;
		if (State == nullptr || !bUseState || !bParseSwitch || !bParseInputsSame || State->PendingParagraphs > 0)
		{
			return false;
		}
		const int32 KeptCount = State->Elements.Num();
		if (State->Measured.Num() != KeptCount || State->FollowsMarkup.Num() != KeptCount || State->PlainStart.Num() != KeptCount
			|| State->ElementCodepoints.Num() != KeptCount || State->GraphemeStart.Num() != KeptCount
			|| State->ClusterStarts.Num() != KeptCount || State->Paragraphs.Num() == 0)
		{
			return false;
		}
		const FString& OldText = State->Input.Content;
		const FString& NewText = In.Content;
		const int32 OldLength = OldText.Len();
		const int32 NewLength = NewText.Len();
		const TArray<FDreamUIText_TextProcessingElement>& Kept = State->Elements;
		const TArray<FMeasured>& KeptMeasured = State->Measured;
		auto EndOf = [&Kept](int32 k) { return Kept[k].StringIndex + Kept[k].Length; };
		auto IsNewline = [](uint32 Code) { return Code == '\n' || Code == '\r'; };

		// The window: kept elements [WindowStart, WindowKeptEnd) are replaced by Read[Front, Front + NewCount).
		TArray<FDreamUIText_TextProcessingElement, TInlineAllocator<16>> Read;
		int32 Front = 0;
		int32 NewCount = 0;
		int32 WindowStart = KeptCount;
		int32 WindowKeptEnd = KeptCount;
		int32 SourceDelta = 0;
		int32 ResyncAt = OldLength;
		int32 WindowParagraph = INDEX_NONE;
		const FRichTextParseResult* DonorStyle = nullptr;
		FRichTextParseResult EndDonor;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Diff);
			FStageTimer Timer(EDreamTextLayoutStage::Diff);
			// The edit, from the two texts alone: their common start, then their common end in what is left.
			const TCHAR* OldChars = *OldText;
			const TCHAR* NewChars = *NewText;
			const int32 MinLength = FMath::Min(OldLength, NewLength);
			int32 Prefix = 0;
			while (Prefix < MinLength && OldChars[Prefix] == NewChars[Prefix])
			{
				Prefix++;
			}
			int32 Suffix = 0;
			while (Suffix < MinLength - Prefix && OldChars[OldLength - 1 - Suffix] == NewChars[NewLength - 1 - Suffix])
			{
				Suffix++;
			}
			SourceDelta = NewLength - OldLength;
			if (Prefix < OldLength || Prefix < NewLength)
			{
				const int32 OldEnd = OldLength - Suffix;
				const int32 NewEnd = NewLength - Suffix;
				// The first kept element whose read reached the edit: every one before it reads the same in the new text. An
				// element's read reaches at most three units past its end, so the search starts three units short of the edit.
				int32 Low = 0;
				int32 High = KeptCount;
				while (Low < High)
				{
					const int32 Mid = (Low + High) / 2;
					if (EndOf(Mid) + 3 <= Prefix)
					{
						Low = Mid + 1;
					}
					else
					{
						High = Mid;
					}
				}
				int32 ReadFrom = Low;
				while (ReadFrom < KeptCount && DreamTextLayoutElementReadEnd(OldText, EndOf(ReadFrom)) <= Prefix)
				{
					ReadFrom++;
				}
				// A long paragraph that cannot be measured through a window -- the window or the shape cache it splices at switched
				// off, a flow forced right to left -- is laid out as round-4 layouts did. That is known from the paragraph the edit
				// is in before anything is read, so no window is read only to be thrown away.
				const bool bWindowable = bMeasureSwitch && (!bCanShape || (In.FlowDirection != EDreamTextFlowDirection::RightToLeft
					&& FDreamTextShapeCache::IsEnabled() && IsInGameThread()));
				if (!bWindowable)
				{
					int32 AtEdit = ReadFrom;
					while (AtEdit < KeptCount && EndOf(AtEdit) <= Prefix)
					{
						AtEdit++;
					}
					const TArray<FParagraph>& ProbeParagraphs = State->Paragraphs;
					int32 Containing = 0;
					while (Containing + 1 < ProbeParagraphs.Num() && ProbeParagraphs[Containing + 1].Start <= AtEdit)
					{
						Containing++;
					}
					if (ProbeParagraphs[Containing].End - ProbeParagraphs[Containing].Start >= BoundaryWindowMinElements)
					{
						return false;
					}
				}
				// Read again from there, element by element as Preprocess reads, until the reader stands past the edit where the
				// old text was read from too -- a kept element's start or end, or the text's end: from there on, the same text
				// read in the same state, the rest is the kept rest.
				const int32 ReadStart = ReadFrom < KeptCount ? Kept[ReadFrom].StringIndex : Prefix;
				int32 ReadAt = ReadStart;
				int32 KeptAfter = ReadFrom;
				while (true)
				{
					if (ReadAt >= NewEnd)
					{
						const int32 OldAt = ReadAt - SourceDelta;
						while (KeptAfter < KeptCount && Kept[KeptAfter].StringIndex < OldAt)
						{
							KeptAfter++;
						}
						if (OldAt >= OldLength || (KeptAfter < KeptCount && Kept[KeptAfter].StringIndex == OldAt)
							|| (KeptAfter > 0 && EndOf(KeptAfter - 1) == OldAt))
						{
							ResyncAt = OldAt;
							break;
						}
					}
					if (ReadAt >= NewLength)
					{
						return false;
					}
					// Markup or a character reference is read by the parser, not here.
					const TCHAR Unit = NewChars[ReadAt];
					if (In.bRichText && (Unit == '<' || Unit == '>' || Unit == '&'))
					{
						return false;
					}
					int32 Cursor = ReadAt;
					Read.Add(FDreamUIText_CodePoint::ReadCodePoint(NewText, NewLength, Cursor));
					ReadAt = Cursor + 1;
				}
				// Read.Num() goes to ElementsParsed only once the window is spliced in (the end of this function): a window read and
				// then refused is not counted on top of the layout from nothing that follows it.
				WindowStart = ReadFrom;
				WindowKeptEnd = KeptAfter;
				// An image placeholder is made by its tag, not read: nothing next to one is spliced.
				if (In.bRichText)
				{
					for (int32 k = FMath::Max(WindowStart - 1, 0); k <= FMath::Min(WindowKeptEnd, KeptCount - 1); k++)
					{
						if (!KeptMeasured[k].Style.ImageTag.IsNone())
						{
							return false;
						}
					}
				}
				// The elements read again that came out as they were, at either end, stay kept: the same span of the same text.
				auto SameElement = [](const FDreamUIText_TextProcessingElement& A, const FDreamUIText_TextProcessingElement& B, int32 Shift)
				{
					return A.Unicode == B.Unicode && A.StringIndex == B.StringIndex + Shift && A.Length == B.Length && A.Type == B.Type
						&& A.bEscaped == B.bEscaped;
				};
				while (Front < Read.Num() && WindowStart < WindowKeptEnd && Read[Front].StringIndex + Read[Front].Length <= Prefix
					&& SameElement(Read[Front], Kept[WindowStart], 0))
				{
					Front++;
					WindowStart++;
				}
				int32 Back = 0;
				while (Back < Read.Num() - Front && WindowKeptEnd > WindowStart && Read[Read.Num() - 1 - Back].StringIndex >= NewEnd
					&& SameElement(Read[Read.Num() - 1 - Back], Kept[WindowKeptEnd - 1], SourceDelta))
				{
					Back++;
					WindowKeptEnd--;
				}
				NewCount = Read.Num() - Front - Back;
				const int32 RemovedCount = WindowKeptEnd - WindowStart;
				if (NewCount == 0 && RemovedCount == 0)
				{
					return false;
				}
				// The paragraphs stay the paragraphs they were: no newline put in or taken out, no CR LF pair made or split.
				for (int32 k = Front; k < Front + NewCount; k++)
				{
					if (IsNewline(Read[k].Unicode))
					{
						return false;
					}
				}
				for (int32 k = WindowStart; k < WindowKeptEnd; k++)
				{
					if (IsNewline(Kept[k].Unicode))
					{
						return false;
					}
				}
				if (WindowKeptEnd < KeptCount && KeptMeasured[WindowKeptEnd].bSkipped)
				{
					return false;
				}
				if (NewCount == 0 && WindowStart > 0 && WindowKeptEnd < KeptCount && IsNewline(Kept[WindowStart - 1].Unicode)
					&& IsNewline(Kept[WindowKeptEnd].Unicode))
				{
					return false;
				}
				// The paragraph the window is in: the last one starting at or before it.
				const TArray<FParagraph>& KeptParagraphs = State->Paragraphs;
				int32 First = 0;
				int32 Last = KeptParagraphs.Num() - 1;
				while (First < Last)
				{
					const int32 Mid = (First + Last + 1) / 2;
					if (KeptParagraphs[Mid].Start <= WindowStart)
					{
						First = Mid;
					}
					else
					{
						Last = Mid - 1;
					}
				}
				WindowParagraph = First;
				const FParagraph& K = KeptParagraphs[WindowParagraph];
				if (WindowStart < K.Start || WindowKeptEnd > K.End)
				{
					return false;
				}
				// A long paragraph is measured through a window; one that could not be -- right to left, or with the window or
				// the shape cache it splices at switched off -- is better laid out as round-4 layouts did, from its donors.
				if ((K.End - K.Start) + NewCount - RemovedCount >= BoundaryWindowMinElements)
				{
					int32 Rtl = K.RtlCount;
					for (int32 k = Front; k < Front + NewCount; k++)
					{
						Rtl += FDreamTextShaper::CanTurnRightToLeft(Read[k].Unicode) ? 1 : 0;
					}
					for (int32 k = WindowStart; k < WindowKeptEnd; k++)
					{
						Rtl -= FDreamTextShaper::CanTurnRightToLeft(Kept[k].Unicode) ? 1 : 0;
					}
					if (!bMeasureSwitch || (bCanShape && (Rtl > 0 || In.FlowDirection == EDreamTextFlowDirection::RightToLeft
						|| !FDreamTextShapeCache::IsEnabled() || !IsInGameThread())))
					{
						return false;
					}
				}
				if (In.bRichText)
				{
					if (!IsMarkupSafeEdit(Prefix, OldEnd, NewEnd, ReadStart, ReadAt))
					{
						return false;
					}
					// The languages are numbered as their <lang> tags first name an element: one whose elements the edit takes out
					// has to name the element right before the window too, so it is still named, and named first where it was.
					const FName LanguageBefore = WindowStart > 0 && !KeptMeasured[WindowStart - 1].bHardBreak ? KeptMeasured[WindowStart - 1].Style.Language : NAME_None;
					for (int32 k = WindowStart; k < WindowKeptEnd; k++)
					{
						if (!KeptMeasured[k].Style.Language.IsNone() && KeptMeasured[k].Style.Language != LanguageBefore)
						{
							return false;
						}
					}
					// The style the new elements take: the parser's state where they stand, which every element read in the same
					// run of text has -- a kept element beside the window with no markup between, or the one that stood at the
					// window's start after the same markup, or the text's end.
					if (NewCount > 0)
					{
						const int32 FirstStart = Read[Front].StringIndex;
						const int32 PreviousEnd = WindowStart > 0 ? EndOf(WindowStart - 1) : 0;
						if (FirstStart > PreviousEnd)
						{
							if (WindowStart < KeptCount && Kept[WindowStart].StringIndex == FirstStart && State->FollowsMarkup[WindowStart])
							{
								DonorStyle = &KeptMeasured[WindowStart].Style;
							}
							else if (WindowStart == KeptCount && FirstStart == OldLength)
							{
								DonorStyle = &EndDonor;
							}
						}
						else if (WindowStart > 0)
						{
							DonorStyle = &KeptMeasured[WindowStart - 1].Style;
						}
						else if (WindowStart < WindowKeptEnd)
						{
							DonorStyle = &KeptMeasured[WindowStart].Style;
						}
						else if (WindowKeptEnd < KeptCount && !State->FollowsMarkup[WindowKeptEnd])
						{
							DonorStyle = &KeptMeasured[WindowKeptEnd].Style;
						}
						else if (OldLength == 0)
						{
							DonorStyle = &EndDonor;
						}
						if (DonorStyle == nullptr)
						{
							return false;
						}
						if (DonorStyle == &EndDonor)
						{
							// The end's style as the parser left it after the last tag, which says what that tag was; an element
							// after it is read once the parser has been reset for the next character.
							EndDonor = State->EndStyle;
							EndDonor.CustomTagMode = ECustomTagMode::None;
							EndDonor.bHyperlink = false;
							EndDonor.ImageTag = NAME_None;
						}
						// A <lang> no element named yet would be numbered where the new elements first name it, which the kept
						// numbering cannot say.
						if (!DonorStyle->Language.IsNone() && !State->LanguageIndexByTag.Contains(DonorStyle->Language))
						{
							return false;
						}
					}
				}
			}
		}

		// From here the kept arrays are this layout's: moved out of the state, the window spliced in where its kept elements
		// were, what follows moved by the edit's lengths.
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Preprocess);
		FStageTimer Timer(EDreamTextLayoutStage::Preprocess);
		const int32 RemovedCount = WindowKeptEnd - WindowStart;
		ReplacedElements.Reset(RemovedCount);
		ReplacedCaretIndices.Reset(RemovedCount);
		ReplacedScripts.Reset(RemovedCount);
		ReplacedOwnScripts.Init(false, RemovedCount);
		for (int32 k = WindowStart; k < WindowKeptEnd; k++)
		{
			ReplacedElements.Add(Kept[k]);
			ReplacedCaretIndices.Add(In.bRichText ? KeptMeasured[k].Style.CharIndex : Kept[k].StringIndex);
			ReplacedScripts.Add(KeptMeasured[k].Script);
			ReplacedOwnScripts[k - WindowStart] = KeptMeasured[k].bOwnScript;
		}
		// What the paragraph's counts lose with the elements the window replaced, read before the arrays move.
		int32 RemovedTabs = 0;
		int32 RemovedRtl = 0;
		int32 RemovedObjects = 0;
		for (int32 k = WindowStart; k < WindowKeptEnd; k++)
		{
			RemovedTabs += KeptMeasured[k].bTab ? 1 : 0;
			RemovedRtl += FDreamTextShaper::CanTurnRightToLeft(Kept[k].Unicode) ? 1 : 0;
			RemovedObjects += (KeptMeasured[k].bImageSpace || KeptMeasured[k].bEmoji) ? 1 : 0;
		}
		const int32 KeptPlainBegin = WindowStart < KeptCount ? State->PlainStart[WindowStart] : State->PlainText.Len();
		const int32 KeptPlainEnd = WindowKeptEnd < KeptCount ? State->PlainStart[WindowKeptEnd] : State->PlainText.Len();
		if (DonorStyle != nullptr && DonorStyle != &EndDonor)
		{
			// The donor is a kept record that is about to move.
			EndDonor = *DonorStyle;
			DonorStyle = &EndDonor;
		}

		bInPlaceParse = true;
		EditStart = WindowStart;
		EditCount = NewCount;
		EditKeptEnd = WindowKeptEnd;
		EditElementDelta = NewCount - RemovedCount;
		EditSourceDelta = SourceDelta;
		EditParagraph = WindowParagraph;
		TextProcessingArray = MoveTemp(State->Elements);
		FollowsMarkup = MoveTemp(State->FollowsMarkup);
		PlainText = MoveTemp(State->PlainText);
		PlainStart = MoveTemp(State->PlainStart);
		ElementCodepoints = MoveTemp(State->ElementCodepoints);
		Measured = MoveTemp(State->Measured);
		Glyphs = MoveTemp(State->Glyphs);
		GraphemeStart = MoveTemp(State->GraphemeStart);
		ClusterStarts = MoveTemp(State->ClusterStarts);
		LineBreakRaw = MoveTemp(State->LineBreakRaw);
		WordBreakRaw = MoveTemp(State->WordBreakRaw);
		CanBreakBefore = MoveTemp(State->CanBreakBefore);
		TagRecords = MoveTemp(State->TagRecords);
		InlineObjects = MoveTemp(State->InlineObjects);
		Languages = MoveTemp(State->Languages);
		LanguageIndexByTag = MoveTemp(State->LanguageIndexByTag);
		EndStyle = State->EndStyle;
		bEndFollowsMarkup = State->bEndFollowsMarkup;
		bAnyPaint = State->bAnyPaint;
		if (Languages.Num() == 0)
		{
			Languages.Add(FDreamTextLanguage::Make(In.Language));
		}

		if (NewCount > 0 || RemovedCount > 0)
		{
			const int32 TailStart = WindowStart + NewCount;
			SpliceRange(TextProcessingArray, WindowStart, RemovedCount, NewCount);
			SpliceRange(ElementCodepoints, WindowStart, RemovedCount, NewCount);
			SpliceRange(PlainStart, WindowStart, RemovedCount, NewCount);
			SpliceRange(Measured, WindowStart, RemovedCount, NewCount);
			SpliceBits(FollowsMarkup, WindowStart, RemovedCount, NewCount);
			SpliceBits(GraphemeStart, WindowStart, RemovedCount, NewCount);
			SpliceBits(ClusterStarts, WindowStart, RemovedCount, NewCount);
			// The break bits are there only when the kept layout wrapped; without them the analysis runs whole.
			for (TBitArray<>* Bits : { &LineBreakRaw, &WordBreakRaw, &CanBreakBefore })
			{
				if (Bits->Num() == KeptCount)
				{
					SpliceBits(*Bits, WindowStart, RemovedCount, NewCount);
				}
			}
			// The window's plain text: no tag and no reference in it, so each element as the source spells it.
			FString WindowPlain;
			for (int32 k = 0; k < NewCount; k++)
			{
				const FDreamUIText_TextProcessingElement& Element = Read[Front + k];
				WindowPlain.AppendChars(*NewText + Element.StringIndex, Element.Length);
			}
			EditPlainDelta = WindowPlain.Len() - (KeptPlainEnd - KeptPlainBegin);
			PlainText.RemoveAt(KeptPlainBegin, KeptPlainEnd - KeptPlainBegin, EAllowShrinking::No);
			PlainText.InsertAt(KeptPlainBegin, WindowPlain);

			int32 PlainCursor = KeptPlainBegin;
			int32 AddedTabs = 0;
			int32 AddedRtl = 0;
			int32 AddedObjects = 0;
			for (int32 k = 0; k < NewCount; k++)
			{
				const int32 i = WindowStart + k;
				const FDreamUIText_TextProcessingElement& Element = Read[Front + k];
				TextProcessingArray[i] = Element;
				ElementCodepoints[i] = Element.Unicode;
				PlainStart[i] = PlainCursor;
				PlainCursor += Element.Length;
				FollowsMarkup[i] = false;
				GraphemeStart[i] = false;
				ClusterStarts[i] = false;
				FMeasured& M = Measured[i];
				M = FMeasured();
				M.Style = In.bRichText ? *DonorStyle : RichTextParseResult;
				if (In.bRichText)
				{
					M.Style.CharIndex = Element.StringIndex;
				}
				bAnyPaint |= M.Style.PaintOrder > 0;
				ClassifyElement(i, M);
				AddedTabs += M.bTab ? 1 : 0;
				AddedRtl += FDreamTextShaper::CanTurnRightToLeft(Element.Unicode) ? 1 : 0;
				AddedObjects += (M.bImageSpace || M.bEmoji) ? 1 : 0;
			}
			for (TBitArray<>* Bits : { &LineBreakRaw, &WordBreakRaw, &CanBreakBefore })
			{
				if (Bits->Num() == TextProcessingArray.Num())
				{
					for (int32 k = 0; k < NewCount; k++)
					{
						(*Bits)[WindowStart + k] = false;
					}
				}
			}

			// Everything after the window moved by the edit's lengths.
			const int32 Count = TextProcessingArray.Num();
			if (SourceDelta != 0)
			{
				for (int32 i = TailStart; i < Count; i++)
				{
					TextProcessingArray[i].StringIndex += SourceDelta;
				}
				if (In.bRichText)
				{
					for (int32 i = TailStart; i < Count; i++)
					{
						Measured[i].Style.CharIndex += SourceDelta;
					}
				}
				bTailRebased = true;
			}
			if (EditPlainDelta != 0)
			{
				for (int32 i = TailStart; i < Count; i++)
				{
					PlainStart[i] += EditPlainDelta;
				}
				bTailRebased = true;
			}

			if (In.bRichText)
			{
				// Markup stands before an element where there is a gap between it and the element before: the window's first
				// element, and the one after the window, which now follows the window's last.
				auto FollowsMarkupAt = [this](int32 ElementIndex)
				{
					const int32 PreviousEnd = ElementIndex > 0 ? TextProcessingArray[ElementIndex - 1].StringIndex + TextProcessingArray[ElementIndex - 1].Length : 0;
					return TextProcessingArray[ElementIndex].StringIndex > PreviousEnd;
				};
				if (NewCount > 0)
				{
					FollowsMarkup[WindowStart] = FollowsMarkupAt(WindowStart);
				}
				if (TailStart < Count)
				{
					FollowsMarkup[TailStart] = FollowsMarkupAt(TailStart);
				}
				else
				{
					// The window ends the text: markup ends it when the last element does not.
					const int32 LastEnd = Count > 0 ? TextProcessingArray[Count - 1].StringIndex + TextProcessingArray[Count - 1].Length : 0;
					bEndFollowsMarkup = NewLength > LastEnd;
				}
				// The tags after the window moved with it; one that spans it now ends further on.
				for (FTagRecord& Record : TagRecords)
				{
					if (Record.OpenAt >= ResyncAt)
					{
						Record.FirstElement += EditElementDelta;
						Record.OpenAt += SourceDelta;
					}
					if (Record.bClosed && Record.CloseAt >= ResyncAt)
					{
						Record.LastElement += EditElementDelta;
						Record.CloseAt += SourceDelta;
					}
				}
			}

			// The inline objects: the kept ones before the window, the window's own, the kept ones after it moved.
			TArray<int32> KeptObjects = MoveTemp(InlineObjects);
			InlineObjects.Reset(KeptObjects.Num() + AddedObjects);
			int32 Next = 0;
			while (Next < KeptObjects.Num() && KeptObjects[Next] < WindowStart)
			{
				InlineObjects.Add(KeptObjects[Next++]);
			}
			for (int32 i = WindowStart; i < TailStart; i++)
			{
				if (Measured[i].bImageSpace || Measured[i].bEmoji)
				{
					InlineObjects.Add(i);
				}
			}
			while (Next < KeptObjects.Num() && KeptObjects[Next] < WindowKeptEnd)
			{
				Next++;
			}
			while (Next < KeptObjects.Num())
			{
				InlineObjects.Add(KeptObjects[Next++] + EditElementDelta);
			}

			// Paragraphs: every one but the window's is taken where it stands; the window's is measured again from the kept one.
			Paragraphs = State->Paragraphs;
			for (int32 p = 0; p < Paragraphs.Num(); p++)
			{
				FParagraph& P = Paragraphs[p];
				if (p > WindowParagraph)
				{
					P.Start += EditElementDelta;
				}
				if (p >= WindowParagraph)
				{
					P.End += EditElementDelta;
					P.Next += EditElementDelta;
				}
				if (p == WindowParagraph)
				{
					P.Source = INDEX_NONE;
					P.bPositional = false;
					P.bPlaceSame = false;
					P.bHashed = false;
					P.PrefixDonor = WindowParagraph;
					P.SuffixDonor = WindowParagraph;
					P.TextPrefix = WindowStart - P.Start;
					P.TextSuffix = P.End - TailStart;
					P.SamePrefix = 0;
					P.SameSuffix = 0;
					P.TabCount += AddedTabs - RemovedTabs;
					P.RtlCount += AddedRtl - RemovedRtl;
					P.ObjectCount += AddedObjects - RemovedObjects;
					P.bHasTab = P.TabCount > 0;
				}
				else
				{
					P.Source = p;
					P.bPositional = true;
					P.bPlaceSame = true;
					P.PrefixDonor = INDEX_NONE;
					P.SuffixDonor = INDEX_NONE;
					P.TextPrefix = 0;
					P.TextSuffix = 0;
					P.SamePrefix = 0;
					P.SameSuffix = 0;
				}
			}
		}
		else
		{
			// The same text: every paragraph is taken where it stands.
			EditPlainDelta = 0;
			Paragraphs = State->Paragraphs;
			for (int32 p = 0; p < Paragraphs.Num(); p++)
			{
				FParagraph& P = Paragraphs[p];
				P.Source = p;
				P.bPositional = true;
				P.bPlaceSame = true;
				P.PrefixDonor = INDEX_NONE;
				P.SuffixDonor = INDEX_NONE;
				P.TextPrefix = 0;
				P.TextSuffix = 0;
				P.SamePrefix = 0;
				P.SameSuffix = 0;
			}
		}

		// An inline object is as big as its picture says, and an emoji is a picture only while the emoji data has one for it:
		// either may have changed in its asset since. One that did makes this a layout from nothing.
		bool bObjectsSame = true;
		for (const int32 Object : InlineObjects)
		{
			if (Object >= EditStart && Object < EditStart + EditCount)
			{
				continue;
			}
			const FMeasured& M = Measured[Object];
			FMeasured Probe = M;
			if (M.bEmoji && (!DrawsEmojiAsImage(Object, Probe) || Probe.EmojiItem != M.EmojiItem))
			{
				bObjectsSame = false;
				break;
			}
			const FDreamUICharData Geo = GetInlineObjectGeo(Probe);
			if (Geo.Width != M.Glyph.Width || Geo.Height != M.Glyph.Height || Geo.XAdvance != M.Glyph.XAdvance)
			{
				bObjectsSame = false;
				break;
			}
		}
		if (!bObjectsSame)
		{
			State->Reset();
			bUseState = false;
			bBreakKeySame = false;
			bLinePlacementSame = false;
			bParseInputsSame = false;
			bInPlaceParse = false;
			EditParagraph = INDEX_NONE;
			bTailRebased = false;
			Measured.Reset();
			Glyphs.Reset();
			Paragraphs.Reset();
			InlineObjects.Reset();
			Languages.SetNum(1);
			LanguageIndexByTag.Reset();
			return false;
		}
		// Counted only now that the edit is spliced in: every paragraph but the edited one (none, for the same text) stands
		// where it stood.
		Stats.ElementsParsed += Read.Num();
		Stats.ElementsSpliced += NewCount;
		Stats.ParagraphsPositional += Paragraphs.Num() - (EditParagraph != INDEX_NONE ? 1 : 0);
		return true;
	}

	void FLayoutRun::ClassifyElement(int32 ElementIndex, FMeasured& M)
	{
		const FDreamUIText_TextProcessingElement& Element = TextProcessingArray[ElementIndex];
		const uint32 Code = Element.Unicode;
		M.LanguageIndex = LanguageIndexFor(M.Style.Language);
		M.bImageSpace = IsRichTextImageSpace(Code, M.Style);
		// Text-type elements are always glyphs: a bare U+2764 is drawn in colour when only a colour face has it.
		M.bEmoji = !M.bImageSpace && Element.Type == EDreamUIText_CodeType::Emoji && DrawsEmojiAsImage(ElementIndex, M);
		M.bWhitespace = !M.bImageSpace && (Code == ' ' || Code == '\t');
		M.bTab = M.bWhitespace && Code == '\t';
		M.bVisibleGlyph = !M.bImageSpace && !M.bEmoji && !M.bWhitespace;
	}

	void FLayoutRun::CountParagraph(FParagraph& P) const
	{
		P.TabCount = 0;
		P.RtlCount = 0;
		P.ObjectCount = 0;
		for (int32 k = P.Start; k < P.End; k++)
		{
			const FMeasured& M = Measured[k];
			P.TabCount += M.bTab ? 1 : 0;
			P.RtlCount += FDreamTextShaper::CanTurnRightToLeft(TextProcessingArray[k].Unicode) ? 1 : 0;
			P.ObjectCount += (M.bImageSpace || M.bEmoji) ? 1 : 0;
		}
		P.bHasTab = P.TabCount > 0;
	}

	void FLayoutRun::Classify()
	{
		const int32 Count = TextProcessingArray.Num();
		Measured.SetNum(Count);
		InlineObjects.Reset();
		bAnyPaint = false;
		// Classification first, then metrics paragraph by paragraph: a paragraph is the unit the
		// shaper sees, so nothing kerns or forms across a hard break.
		for (int32 i = 0; i < Count; i++)
		{
			const auto& Element = TextProcessingArray[i];
			FMeasured& M = Measured[i];
			M.Style = In.bRichText ? RichTextPropertyArray[i] : RichTextParseResult;
			bAnyPaint |= M.Style.PaintOrder > 0;
			const uint32 Code = Element.Unicode;
			if (Code == '\n' || Code == '\r')
			{
				M.bHardBreak = true;
				if (i + 1 < Count)
				{
					const uint32 Next = TextProcessingArray[i + 1].Unicode;
					if ((Code == '\r' && Next == '\n') || (Code == '\n' && Next == '\r'))
					{
						Measured[i + 1].bSkipped = true;
						Measured[i + 1].bHardBreak = true;
						Measured[i + 1].Style = In.bRichText ? RichTextPropertyArray[i + 1] : RichTextParseResult;
						bAnyPaint |= Measured[i + 1].Style.PaintOrder > 0;
						i++;
					}
				}
				continue;
			}
			ClassifyElement(i, M);
			if (M.bImageSpace || M.bEmoji)
			{
				InlineObjects.Add(i);
			}
		}

		// The paragraphs, each with the newline that ends it; the two halves of a CR LF are one newline.
		Paragraphs.Reset();
		int32 ParagraphStart = 0;
		for (int32 i = 0; i <= Count; i++)
		{
			if (i < Count && (!Measured[i].bHardBreak || Measured[i].bSkipped))
			{
				continue;
			}
			FParagraph& P = Paragraphs.AddDefaulted_GetRef();
			P.Start = ParagraphStart;
			P.End = i;
			int32 Next = FMath::Min(i + 1, Count);
			if (Next < Count && Measured[Next].bSkipped)
			{
				Next++;
			}
			P.Next = Next;
			CountParagraph(P);
			ParagraphStart = Next;
		}
	}

	/**
	 * A paragraph's measure hash over one layout's arrays -- this layout's or the kept one's: its text, and each element's
	 * kind, size, weight, language and inline object size.
	 */
	uint64 HashForMeasure(const FParagraph& P, const FString& InPlainText, const TArray<int32>& InPlainStart, const TArray<FMeasured>& InMeasured,
		const TArray<FDreamUIText_TextProcessingElement>& InElements, const TBitArray<>& InFollowsMarkup, TFunctionRef<FDreamUICharData(const FMeasured&)> InlineGeo)
	{
		auto PlainAtIn = [&InPlainText, &InPlainStart](int32 ElementIndex) { return ElementIndex < InPlainStart.Num() ? InPlainStart[ElementIndex] : InPlainText.Len(); };
		const int32 PlainBegin = PlainAtIn(P.Start);
		const int32 PlainLength = PlainAtIn(P.End) - PlainBegin;
		uint64 Hash = CityHash64(reinterpret_cast<const char*>(*InPlainText + PlainBegin), (uint32)(PlainLength * sizeof(TCHAR)));
		for (int32 i = P.Start; i < P.End; i++)
		{
			const FMeasured& M = InMeasured[i];
			const FDreamUIText_TextProcessingElement& Element = InElements[i];
			const uint32 Kinds = (M.Style.Bold ? 1u : 0u) | (M.Style.Italic ? 2u : 0u) | (M.bImageSpace ? 4u : 0u) | (M.bEmoji ? 8u : 0u)
				| (M.bWhitespace ? 16u : 0u) | (M.bTab ? 32u : 0u) | (Element.bEscaped ? 64u : 0u)
				| (Element.Type == EDreamUIText_CodeType::Emoji ? 128u : 0u) | ((bool)InFollowsMarkup[i] ? 256u : 0u);
			Hash = MixHash(Hash, ((uint64)(uint32)(InPlainStart[i] - PlainBegin) << 32) | Kinds);
			Hash = MixHash(Hash, ((uint64)GetTypeHash(M.Style.Size) << 32) | GetTypeHash(M.Style.Language));
			if (M.bImageSpace || M.bEmoji)
			{
				const FDreamUICharData Geo = InlineGeo(M);
				Hash = MixHash(Hash, FloatPairBits(Geo.Width, Geo.Height));
				Hash = MixHash(Hash, ((uint64)GetTypeHash(Geo.XAdvance) << 32) | GetTypeHash(M.EmojiItem));
			}
		}
		return Hash;
	}

	/** A paragraph's place hash over one layout's records: what else of its style its placement reads, paint included. */
	uint64 HashForPlace(const FParagraph& P, const TArray<FMeasured>& InMeasured)
	{
		uint64 Hash = 0;
		for (int32 i = P.Start; i < P.End; i++)
		{
			const FRichTextParseResult& Style = InMeasured[i].Style;
			const uint32 Flags = (Style.HasColor ? 1u : 0u) | (Style.bHasMultiplyColor ? 2u : 0u) | (Style.Underline ? 4u : 0u)
				| (Style.Strikethrough ? 8u : 0u) | ((uint32)Style.SupOrSubMode << 4) | ((uint32)Style.ImageVerticalAlign << 8)
				| (Style.bPaintRemoved ? (1u << 12) : 0u);
			Hash = MixHash(Hash, ((uint64)Style.Color.DWColor() << 32) | Style.MultiplyColor.DWColor());
			Hash = MixHash(Hash, ((uint64)GetTypeHash(Style.BaselineShift) << 32) | Flags);
			if (!Style.ImageTag.IsNone())
			{
				Hash = MixHash(Hash, GetTypeHash(Style.ImageTag));
				Hash = MixHash(Hash, FloatPairBits(Style.ImageWidth, Style.ImageHeight));
			}
			if (Style.PaintOrder != 0)
			{
				// Two runs of one gradient are told apart by their order, so a run that moves is placed again with its own boxes.
				Hash = MixHash(Hash, ((uint64)GetTypeHash(Style.PaintName) << 32) | (uint32)Style.PaintOrder);
			}
		}
		return Hash;
	}

	uint64 FLayoutRun::MeasureHashOf(const FParagraph& P) const
	{
		return HashForMeasure(P, PlainText, PlainStart, Measured, TextProcessingArray, FollowsMarkup,
			[this](const FMeasured& M) { return GetInlineObjectGeo(M); });
	}

	uint64 FLayoutRun::PlaceHashOf(const FParagraph& P) const
	{
		return HashForPlace(P, Measured);
	}

	uint64 FLayoutRun::KeptMeasureHashOf(int32 KeptIndex)
	{
		FParagraph& K = State->Paragraphs[KeptIndex];
		if (!K.bHashed)
		{
			// Worked out from the kept arrays, which a layout that reads every element (this one) leaves where they are.
			K.MeasureHash = HashForMeasure(K, State->PlainText, State->PlainStart, State->Measured, State->Elements, State->FollowsMarkup,
				[this](const FMeasured& M) { return GetInlineObjectGeo(M); });
			K.PlaceHash = HashForPlace(K, State->Measured);
			K.bHashed = true;
			Stats.ParagraphsHashed++;
		}
		return K.MeasureHash;
	}

	uint64 FLayoutRun::KeptPlaceHashOf(int32 KeptIndex)
	{
		KeptMeasureHashOf(KeptIndex);
		return State->Paragraphs[KeptIndex].PlaceHash;
	}

	bool FLayoutRun::MatchesKept(const FParagraph& P, const FParagraph& K) const
	{
		const int32 Count = P.End - P.Start;
		if (K.bPendingQuads || Count != K.End - K.Start)
		{
			return false;
		}
		// The same text, element for element: the code points the shaper and the breakers see, sequences and all.
		const int32 PlainBegin = PlainAt(P.Start);
		const int32 PlainLength = PlainAt(P.End) - PlainBegin;
		const int32 KeptPlainBegin = KeptPlainAt(K.Start);
		if (PlainLength != KeptPlainAt(K.End) - KeptPlainBegin
			|| FMemory::Memcmp(*PlainText + PlainBegin, *State->PlainText + KeptPlainBegin, PlainLength * sizeof(TCHAR)) != 0)
		{
			return false;
		}
		for (int32 k = 0; k < Count; k++)
		{
			const int32 i = P.Start + k;
			const int32 j = K.Start + k;
			const FMeasured& M = Measured[i];
			const FMeasured& O = State->Measured[j];
			const FDreamUIText_TextProcessingElement& Element = TextProcessingArray[i];
			const FDreamUIText_TextProcessingElement& KeptElement = State->Elements[j];
			// What the element is, and everything of its style measuring reads: size, weight, slant, language. A tag edge
			// before it can end a shaped run, so that is the same too.
			if (PlainStart[i] - PlainBegin != State->PlainStart[j] - KeptPlainBegin || Element.Type != KeptElement.Type
				|| Element.bEscaped != KeptElement.bEscaped || (bool)FollowsMarkup[i] != (bool)State->FollowsMarkup[j]
				|| M.bImageSpace != O.bImageSpace || M.bEmoji != O.bEmoji || M.EmojiItem != O.EmojiItem || M.bWhitespace != O.bWhitespace
				|| M.bTab != O.bTab || M.bVisibleGlyph != O.bVisibleGlyph || M.Style.Size != O.Style.Size || M.Style.Bold != O.Style.Bold
				|| M.Style.Italic != O.Style.Italic || M.Style.Language != O.Style.Language)
			{
				return false;
			}
			// An inline object is as big as its picture says, and the picture may have been changed in its asset.
			if (M.bImageSpace || M.bEmoji)
			{
				const FDreamUICharData Geo = GetInlineObjectGeo(i);
				if (Geo.Width != O.Glyph.Width || Geo.Height != O.Glyph.Height || Geo.XAdvance != O.Glyph.XAdvance)
				{
					return false;
				}
			}
		}
		return true;
	}

	int32 FLayoutRun::CountSameText(const FParagraph& P, const FParagraph& K, bool bFromEnd) const
	{
		const int32 Limit = FMath::Min(P.End - P.Start, K.End - K.Start);
		const int32 PlainBegin = PlainAt(P.Start);
		const int32 PlainEnd = PlainAt(P.End);
		const int32 KeptPlainBegin = KeptPlainAt(K.Start);
		const int32 KeptPlainEnd = KeptPlainAt(K.End);
		int32 Same = 0;
		for (; Same < Limit; Same++)
		{
			const int32 i = bFromEnd ? P.End - 1 - Same : P.Start + Same;
			const int32 j = bFromEnd ? K.End - 1 - Same : K.Start + Same;
			const int32 Start = PlainStart[i];
			const int32 Length = PlainAt(i + 1) - Start;
			const int32 KeptStart = State->PlainStart[j];
			const int32 KeptLength = KeptPlainAt(j + 1) - KeptStart;
			// The same characters at the same distance from the paragraph's start -- or end -- cut into the same elements.
			const bool bSamePlace = bFromEnd ? PlainEnd - Start == KeptPlainEnd - KeptStart : Start - PlainBegin == KeptStart - KeptPlainBegin;
			if (!bSamePlace || Length != KeptLength
				|| FMemory::Memcmp(*PlainText + Start, *State->PlainText + KeptStart, Length * sizeof(TCHAR)) != 0)
			{
				break;
			}
		}
		return Same;
	}

	void FLayoutRun::Lookup()
	{
		// Nothing kept to look in: no hash is worked out now -- a later layout that looks in this one works out the hashes it
		// needs. An edit seen from the content needs none: its paragraphs stand where they stood (TryParseInPlace).
		if (State == nullptr || !bUseState || bInPlaceParse)
		{
			return;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Lookup);
		FStageTimer Timer(EDreamTextLayoutStage::Lookup);
		for (FParagraph& P : Paragraphs)
		{
			P.MeasureHash = MeasureHashOf(P);
			P.PlaceHash = PlaceHashOf(P);
			P.bHashed = true;
			Stats.ParagraphsHashed++;
		}
		const TArray<FParagraph>& Kept = State->Paragraphs;
		// The kept paragraphs by measure hash, those that share one chained in order.
		TMap<uint64, int32> FirstByHash;
		FirstByHash.Reserve(Kept.Num());
		TArray<int32> NextSameHash;
		NextSameHash.Init(INDEX_NONE, Kept.Num());
		for (int32 k = Kept.Num() - 1; k >= 0; k--)
		{
			const uint64 KeptHash = KeptMeasureHashOf(k);
			if (const int32* First = FirstByHash.Find(KeptHash))
			{
				NextSameHash[k] = *First;
			}
			FirstByHash.Add(KeptHash, k);
		}
		TBitArray<> Taken;
		Taken.Init(false, Kept.Num());
		for (FParagraph& P : Paragraphs)
		{
			int32* Head = FirstByHash.Find(P.MeasureHash);
			if (Head == nullptr)
			{
				continue;
			}
			for (int32 k = *Head; k != INDEX_NONE; k = NextSameHash[k])
			{
				if (Taken[k] || !MatchesKept(P, Kept[k]))
				{
					continue;
				}
				P.Source = k;
				Taken[k] = true;
				// Its lines may keep their placement only if every element is styled as it was, colours, strokes and paints included.
				P.bPlaceSame = P.PlaceHash == KeptPlaceHashOf(k);
				for (int32 e = 0; e < P.End - P.Start && P.bPlaceSame; e++)
				{
					P.bPlaceSame = SameLayoutStyle(Measured[P.Start + e].Style, State->Measured[Kept[k].Start + e].Style);
				}
				break;
			}
			// The chain's head moves past what is taken, so a long run of equal paragraphs pairs up in one pass.
			while (*Head != INDEX_NONE && Taken[*Head])
			{
				*Head = NextSameHash[*Head];
			}
		}

		// A paragraph not found was measured afresh. The kept paragraphs nothing took, between the kept ones its neighbours
		// were found as, are what it was edited from: the first of them may share its start, the last its end -- one paragraph
		// edited, split in two, or two joined.
		TArray<int32> NextSource;
		NextSource.SetNumUninitialized(Paragraphs.Num());
		int32 Following = Kept.Num();
		for (int32 p = Paragraphs.Num() - 1; p >= 0; p--)
		{
			NextSource[p] = Following;
			if (Paragraphs[p].Source != INDEX_NONE)
			{
				Following = Paragraphs[p].Source;
			}
		}
		int32 PreviousSource = INDEX_NONE;
		for (int32 p = 0; p < Paragraphs.Num(); p++)
		{
			FParagraph& P = Paragraphs[p];
			if (P.Source != INDEX_NONE)
			{
				PreviousSource = P.Source;
				continue;
			}
			int32 First = PreviousSource + 1;
			while (First < NextSource[p] && Taken[First])
			{
				First++;
			}
			int32 Last = NextSource[p] - 1;
			while (Last >= First && Taken[Last])
			{
				Last--;
			}
			if (First >= NextSource[p] || Last < First)
			{
				continue;
			}
			P.PrefixDonor = First;
			P.SuffixDonor = Last;
			const int32 Count = P.End - P.Start;
			P.TextPrefix = CountSameText(P, Kept[First], false);
			// The start and the end never claim the same element twice.
			int32 SuffixLimit = Count - P.TextPrefix;
			if (First == Last)
			{
				SuffixLimit = FMath::Min(SuffixLimit, (Kept[Last].End - Kept[Last].Start) - P.TextPrefix);
			}
			P.TextSuffix = FMath::Clamp(CountSameText(P, Kept[Last], true), 0, FMath::Max(SuffixLimit, 0));
		}
	}

	FDreamTextBoundarySpan FLayoutRun::SpanOf(int32 FirstParagraph, int32 LastParagraph) const
	{
		FDreamTextBoundarySpan Span;
		Span.PlainText = &PlainText;
		Span.ElementPlainStart = &PlainStart;
		Span.ElementCodepoints = &ElementCodepoints;
		Span.FirstElement = Paragraphs[FirstParagraph].Start;
		// Each paragraph with its newline: what ICU's rules see after the last element is what they saw in the whole text.
		Span.EndElement = Paragraphs[LastParagraph].Next;
		Span.PlainBegin = PlainAt(Span.FirstElement);
		Span.PlainEnd = PlainAt(Span.EndElement);
		return Span;
	}

	void FLayoutRun::AnalyseBoundaries(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, const TBitArray<>* KeptBits)
	{
		auto NoStop = [](int32, bool) { return false; };
		// Paragraphs analysed whole go together while they come one after another: with nothing kept, the whole text in one
		// run, as it always was.
		int32 RunFirst = INDEX_NONE;
		auto AnalyseRun = [&](int32 EndParagraph)
		{
			if (RunFirst != INDEX_NONE)
			{
				const FDreamTextBoundarySpan Span = SpanOf(RunFirst, EndParagraph - 1);
				int32 Last = 0;
				Stats.IcuCodeUnits += FDreamTextBreaker::ComputeBoundaries(Kind, Span, Span.FirstElement, Span.EndElement, Bits, NoStop, Last);
				RunFirst = INDEX_NONE;
			}
		};
		for (int32 p = 0; p < Paragraphs.Num(); p++)
		{
			const FParagraph& P = Paragraphs[p];
			if (KeptBits != nullptr && P.Source != INDEX_NONE)
			{
				AnalyseRun(p);
				if (P.End > P.Start)
				{
					Bits.SetRangeFromRange(P.Start, P.End - P.Start, *KeptBits, State->Paragraphs[P.Source].Start);
				}
				continue;
			}
			if (KeptBits != nullptr && (P.PrefixDonor != INDEX_NONE || P.SuffixDonor != INDEX_NONE)
				&& P.End - P.Start >= BoundaryWindowMinElements)
			{
				AnalyseRun(p);
				AnalyseWindow(Kind, Bits, *KeptBits, p);
				continue;
			}
			if (RunFirst == INDEX_NONE)
			{
				RunFirst = p;
			}
		}
		AnalyseRun(Paragraphs.Num());
		// A paragraph's first element starts a cluster and has no line before it to break from, whatever stands before it.
		for (const FParagraph& P : Paragraphs)
		{
			if (P.End > P.Start)
			{
				Bits[P.Start] = Kind == EDreamTextBoundaryKind::Grapheme;
			}
		}
	}

	void FLayoutRun::AnalyseWindow(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, const TBitArray<>& KeptBits, int32 ParagraphIndex)
	{
		const FParagraph& P = Paragraphs[ParagraphIndex];
		const FParagraph* Prefix = P.PrefixDonor != INDEX_NONE ? &State->Paragraphs[P.PrefixDonor] : nullptr;
		const FParagraph* Suffix = P.SuffixDonor != INDEX_NONE ? &State->Paragraphs[P.SuffixDonor] : nullptr;
		const int32 TextPrefix = Prefix != nullptr ? P.TextPrefix : 0;
		const int32 TextSuffix = Suffix != nullptr ? P.TextSuffix : 0;
		// The text the edit left alone has the boundaries it had.
		if (TextPrefix > 0)
		{
			Bits.SetRangeFromRange(P.Start, TextPrefix, KeptBits, Prefix->Start);
		}
		if (TextSuffix > 0)
		{
			Bits.SetRangeFromRange(P.End - TextSuffix, TextSuffix, KeptBits, Suffix->End - TextSuffix);
		}
		const int32 ChangedBegin = P.Start + TextPrefix;
		const int32 ChangedEnd = P.End - TextSuffix;
		// A dictionary reads a run of its script whole -- Thai and its neighbours for lines, CJK for words -- so an edit in one,
		// or against one, has the whole run read again.
		auto IsDictionaryAt = [this, Kind](int32 i)
		{
			const uint32 C = ElementCodepoints[i];
			return Kind != EDreamTextBoundaryKind::Grapheme && (FDreamTextBreaker::IsDictionaryLineBreakCodepoint(C)
				|| (Kind == EDreamTextBoundaryKind::Word && FDreamTextBreaker::IsCJKCodepoint(C)));
		};
		int32 From = ChangedBegin;
		while (From > P.Start && IsDictionaryAt(From - 1))
		{
			From--;
		}
		// From the word before the edit: two kept boundaries back, and at least as far as ICU's rules could have read past a
		// boundary to decide the ones before the edit.
		int32 WindowStart = P.Start;
		int32 BoundariesBack = 0;
		for (int32 i = From - 1; i > P.Start; i--)
		{
			if (!Bits[i])
			{
				continue;
			}
			BoundariesBack++;
			if (BoundariesBack >= 2 && PlainAt(ChangedBegin) - PlainStart[i] >= BoundaryWindowMargin)
			{
				WindowStart = i;
				break;
			}
		}
		int32 ResyncFrom = ChangedEnd;
		while (ResyncFrom < P.End && IsDictionaryAt(ResyncFrom))
		{
			ResyncFrom++;
		}
		const int32 SuffixShift = Suffix != nullptr ? Suffix->End - P.End : 0;
		// The walk resumes from WindowStart as a walk over the whole paragraph goes on from a boundary: WindowStart keeps the
		// boundary it has, and ICU is asked for the ones after it. Asked to find WindowStart itself, from the code unit before
		// it, the engine's line iterator -- which skips a Hangul word whole from wherever it is asked -- steps over a boundary
		// the whole walk found inside one ("(" then a Hangul word breaks after its first syllable).
		const int32 WalkFrom = WindowStart > P.Start ? WindowStart + 1 : P.Start;
		int32 Last = 0;
		Stats.IcuCodeUnits += FDreamTextBreaker::ComputeBoundaries(Kind, SpanOf(ParagraphIndex, ParagraphIndex), WalkFrom, P.End, Bits,
			[&](int32 Element, bool bBoundary)
			{
				// What ICU finds after a boundary depends only on the text after it: at the first boundary past the edit that the
				// kept text has too, the rest of the paragraph is the kept rest, boundaries included.
				return Suffix != nullptr && bBoundary && Element >= ResyncFrom && Element > WindowStart && KeptBits[Element + SuffixShift];
			}, Last);
	}

	void FLayoutRun::AnalyseEditedParagraph(EDreamTextBoundaryKind Kind, TBitArray<>& Bits, bool bHaveKeptBits, int32& OutChangeFirst, int32& OutChangeLast)
	{
		OutChangeFirst = INDEX_NONE;
		OutChangeLast = INDEX_NONE;
		if (EditParagraph == INDEX_NONE)
		{
			return;
		}
		const FParagraph& P = Paragraphs[EditParagraph];
		auto Note = [&OutChangeFirst, &OutChangeLast](int32 Element)
		{
			OutChangeFirst = OutChangeFirst == INDEX_NONE ? Element : FMath::Min(OutChangeFirst, Element);
			OutChangeLast = FMath::Max(OutChangeLast, Element);
		};
		const int32 ChangedBegin = FMath::Clamp(EditStart, P.Start, P.End);
		const int32 ChangedEnd = FMath::Clamp(EditStart + EditCount, ChangedBegin, P.End);
		if (!bHaveKeptBits || P.End - P.Start < BoundaryWindowMinElements)
		{
			// Short, or nothing kept to resync against: analysed whole, newline included, as a layout from nothing analyses it.
			const FDreamTextBoundarySpan Span = SpanOf(EditParagraph, EditParagraph);
			int32 Last = 0;
			Stats.IcuCodeUnits += FDreamTextBreaker::ComputeBoundaries(Kind, Span, Span.FirstElement, Span.EndElement, Bits,
				[](int32, bool) { return false; }, Last);
			Stats.BitsRecomputed += FMath::Max(Last - Span.FirstElement + 1, 0);
			if (P.End > P.Start)
			{
				Note(P.Start);
				Note(P.End - 1);
			}
		}
		else
		{
			// As AnalyseWindow, with the kept bits already in place: a dictionary reads a run of its script whole, so an edit in
			// one or against one has the whole run read again; the walk starts two kept boundaries and a margin back.
			auto IsDictionaryAt = [this, Kind](int32 i)
			{
				const uint32 C = ElementCodepoints[i];
				return Kind != EDreamTextBoundaryKind::Grapheme && (FDreamTextBreaker::IsDictionaryLineBreakCodepoint(C)
					|| (Kind == EDreamTextBoundaryKind::Word && FDreamTextBreaker::IsCJKCodepoint(C)));
			};
			int32 From = ChangedBegin;
			while (From > P.Start && IsDictionaryAt(From - 1))
			{
				From--;
			}
			int32 WindowStart = P.Start;
			int32 BoundariesBack = 0;
			for (int32 i = From - 1; i > P.Start; i--)
			{
				if (!Bits[i])
				{
					continue;
				}
				BoundariesBack++;
				if (BoundariesBack >= 2 && PlainAt(ChangedBegin) - PlainStart[i] >= BoundaryWindowMargin)
				{
					WindowStart = i;
					break;
				}
			}
			int32 ResyncFrom = ChangedEnd;
			while (ResyncFrom < P.End && IsDictionaryAt(ResyncFrom))
			{
				ResyncFrom++;
			}
			const int32 WalkFrom = WindowStart > P.Start ? WindowStart + 1 : P.Start;
			// The walk writes over the kept bits as it goes: what they were is what it resyncs against, and what tells a change.
			TBitArray<> Saved;
			Saved.Init(false, P.End - WalkFrom);
			Saved.SetRangeFromRange(0, P.End - WalkFrom, Bits, WalkFrom);
			int32 Last = 0;
			Stats.IcuCodeUnits += FDreamTextBreaker::ComputeBoundaries(Kind, SpanOf(EditParagraph, EditParagraph), WalkFrom, P.End, Bits,
				[&Saved, ResyncFrom, WindowStart, WalkFrom](int32 Element, bool bBoundary)
				{
					return bBoundary && Element >= ResyncFrom && Element > WindowStart && (bool)Saved[Element - WalkFrom];
				}, Last);
			Stats.BitsRecomputed += FMath::Max(Last - WalkFrom + 1, 0);
			for (int32 i = WalkFrom; i <= Last; i++)
			{
				if ((i < ChangedBegin || i >= ChangedEnd) && (bool)Bits[i] != (bool)Saved[i - WalkFrom])
				{
					Note(i);
				}
			}
		}
		if (P.End > P.Start)
		{
			Bits[P.Start] = Kind == EDreamTextBoundaryKind::Grapheme;
		}
	}

	void FLayoutRun::AnalyseGraphemes()
	{
		if (bInPlaceParse)
		{
			// Every paragraph but the edited one has its bits where they were, spliced with its elements.
			AnalyseEditedParagraph(EDreamTextBoundaryKind::Grapheme, GraphemeStart, true, GraphemeChangeFirst, GraphemeChangeLast);
			return;
		}
		GraphemeStart.Init(true, TextProcessingArray.Num());
		AnalyseBoundaries(EDreamTextBoundaryKind::Grapheme, GraphemeStart, bUseState ? &State->GraphemeStart : nullptr);
	}

	void FLayoutRun::MeasureParagraphs()
	{
		const int32 Count = TextProcessingArray.Num();
		if (bInPlaceParse)
		{
			// Every paragraph but the edited one is measured as it was and already in place; the edited one is measured here,
			// and what follows it moved.
			if (EditParagraph != INDEX_NONE)
			{
				MeasureEditedParagraph();
			}
			UnwrappedPreferredWidth = 0.0f;
			for (const FParagraph& P : Paragraphs)
			{
				if (P.bHasPreferredWidth)
				{
					UnwrappedPreferredWidth = FMath::Max(UnwrappedPreferredWidth, P.PreferredWidth);
				}
			}
			return;
		}
		Glyphs.Reset();
		RunCount = 0;
		UnwrappedPreferredWidth = 0.0f;
		for (FParagraph& P : Paragraphs)
		{
			P.GlyphStart = Glyphs.Num();
			P.RunStart = RunCount;
			if (P.Source != INDEX_NONE)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Reuse);
				FStageTimer Timer(EDreamTextLayoutStage::Reuse);
				ReuseParagraph(P, State->Paragraphs[P.Source]);
				Stats.ParagraphsReused++;
			}
			else
			{
				FStageTimer Timer(EDreamTextLayoutStage::Measure);
				if (P.End > P.Start)
				{
					if (!bCanShape || !MeasureParagraphByShaping(P))
					{
						MeasureParagraphByCodepoint(P.Start, P.End);
					}
					FinishClusters(P.Start, P.End);
				}
				// Before the line breaker sets each tab where its own line puts it: the preferred width is the unwrapped one.
				P.PreferredWidth = ParagraphPreferredWidth(P.Start, P.End, P.bHasPreferredWidth);
				Stats.ParagraphsMeasured++;
				Stats.WindowElements += P.End - P.Start;
			}
			P.GlyphEnd = Glyphs.Num();
			P.RunEnd = RunCount;
			P.PendingGlyphs = 0;
			for (int32 g = P.GlyphStart; g < P.GlyphEnd; g++)
			{
				P.PendingGlyphs += Glyphs[g].Quad.bPending ? 1 : 0;
			}
			P.bPendingQuads = P.PendingGlyphs > 0;
			if (P.bHasPreferredWidth)
			{
				UnwrappedPreferredWidth = FMath::Max(UnwrappedPreferredWidth, P.PreferredWidth);
			}
		}
		FStageTimer Timer(EDreamTextLayoutStage::Measure);
		ClusterStarts.Init(false, Count);
		for (int32 i = 0; i < Count; i++)
		{
			ClusterStarts[i] = Measured[i].bClusterStart;
		}
	}

	void FLayoutRun::ResetMeasurement(int32 ElementIndex)
	{
		// What Classify gave the element: its style and its kind. Everything measuring writes goes back to how a layout from
		// nothing finds it, so a record measured again comes out as one measured for the first time, field for field.
		FMeasured& M = Measured[ElementIndex];
		FMeasured Classified;
		Classified.Style = M.Style;
		Classified.bHardBreak = M.bHardBreak;
		Classified.bSkipped = M.bSkipped;
		Classified.bWhitespace = M.bWhitespace;
		Classified.bTab = M.bTab;
		Classified.bImageSpace = M.bImageSpace;
		Classified.bEmoji = M.bEmoji;
		Classified.EmojiItem = M.EmojiItem;
		Classified.LanguageIndex = M.LanguageIndex;
		Classified.bVisibleGlyph = M.bVisibleGlyph;
		M = Classified;
	}

	void FLayoutRun::RebaseAfterEdit(int32 FirstElement, int32 FirstGlyph)
	{
		const int32 Count = TextProcessingArray.Num();
		if (EditGlyphDelta != 0 || EditRunDelta != 0)
		{
			for (int32 i = FirstElement; i < Count; i++)
			{
				FMeasured& M = Measured[i];
				// A newline is never measured: its records stay as Classify left them.
				if (M.bHardBreak)
				{
					continue;
				}
				M.GlyphStart += EditGlyphDelta;
				if (M.RunIndex >= 0)
				{
					M.RunIndex += EditRunDelta;
				}
			}
			bTailRebased = true;
		}
		if (EditElementDelta != 0)
		{
			for (int32 g = FirstGlyph; g < Glyphs.Num(); g++)
			{
				Glyphs[g].ElementIndex += EditElementDelta;
			}
		}
		for (int32 p = EditParagraph + 1; p < Paragraphs.Num(); p++)
		{
			FParagraph& Q = Paragraphs[p];
			Q.GlyphStart += EditGlyphDelta;
			Q.GlyphEnd += EditGlyphDelta;
			Q.RunStart += EditRunDelta;
			Q.RunEnd += EditRunDelta;
		}
	}

	void FLayoutRun::MeasureEditedParagraph()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_MeasureWindow);
		FStageTimer Timer(EDreamTextLayoutStage::Measure);
		FParagraph& P = Paragraphs[EditParagraph];
		const FParagraph& K = State->Paragraphs[EditParagraph];
		Stats.ParagraphsMeasured++;
		EditGlyphDelta = 0;
		EditRunDelta = 0;
		bool bMeasured = false;
		if (P.End - P.Start >= BoundaryWindowMinElements && bMeasureSwitch)
		{
			bMeasured = bCanShape ? MeasureWindowed(P) : MeasureWindowedByCodepoint(P);
		}
		if (!bMeasured)
		{
			// Whole: its records set back to what Classify gave them, its glyphs measured on their own and spliced in where the
			// kept paragraph's were, its runs numbered from where the kept paragraph's started.
			for (int32 i = P.Start; i < P.End; i++)
			{
				ResetMeasurement(i);
			}
			TArray<FGlyphSource> KeptGlyphs = MoveTemp(Glyphs);
			Glyphs.Reset();
			RunCount = K.RunStart;
			P.PrefixDonor = INDEX_NONE;
			P.SuffixDonor = INDEX_NONE;
			P.SamePrefix = 0;
			P.SameSuffix = 0;
			if (P.End > P.Start)
			{
				if (!bCanShape || !MeasureParagraphByShaping(P))
				{
					MeasureCodepointRange(P.Start, P.Start, P.End);
				}
				FinishClusters(P.Start, P.End);
			}
			P.PreferredWidth = ParagraphPreferredWidth(P.Start, P.End, P.bHasPreferredWidth);
			Stats.WindowElements += P.End - P.Start;
			TArray<FGlyphSource> NewGlyphs = MoveTemp(Glyphs);
			Glyphs = MoveTemp(KeptGlyphs);
			const int32 KeptGlyphCount = K.GlyphEnd - K.GlyphStart;
			SpliceRange(Glyphs, K.GlyphStart, KeptGlyphCount, NewGlyphs.Num());
			for (int32 g = 0; g < NewGlyphs.Num(); g++)
			{
				Glyphs[K.GlyphStart + g] = NewGlyphs[g];
			}
			for (int32 i = P.Start; i < P.End; i++)
			{
				Measured[i].GlyphStart += K.GlyphStart;
				ClusterStarts[i] = Measured[i].bClusterStart;
			}
			P.GlyphStart = K.GlyphStart;
			P.GlyphEnd = K.GlyphStart + NewGlyphs.Num();
			P.RunStart = K.RunStart;
			P.RunEnd = RunCount;
			EditGlyphDelta = NewGlyphs.Num() - KeptGlyphCount;
			EditRunDelta = P.RunEnd - K.RunEnd;
			P.PendingGlyphs = 0;
			for (const FGlyphSource& G : NewGlyphs)
			{
				P.PendingGlyphs += G.Quad.bPending ? 1 : 0;
			}
			bMeasuredInWindow = false;
			MeasureStart = P.Start;
			MeasureEnd = P.End;
			RebaseAfterEdit(P.End, P.GlyphEnd);
		}
		P.bPendingQuads = P.PendingGlyphs > 0;
		if (bTailRebased)
		{
			Stats.ElementsRebased += FMath::Max(TextProcessingArray.Num() - (EditStart + EditCount), 0);
		}
	}

	bool FLayoutRun::MeasureWindowed(FParagraph& P)
	{
		const FParagraph& K = State->Paragraphs[EditParagraph];
		const int32 Start = P.Start;
		const int32 End = P.End;
		if (End <= Start)
		{
			return false;
		}
		// What changed: the window's elements, and the one on either side, whose clusters and cuts read them; then whatever
		// grapheme boundary came out other than it was.
		const int32 Join = FMath::Clamp(EditStart, Start, End);
		const int32 JoinEnd = FMath::Clamp(EditStart + EditCount, Join, End);
		int32 FirstChange = FMath::Max(Join - 1, Start);
		int32 LastChange = FMath::Clamp(JoinEnd, Start, End - 1);
		if (GraphemeChangeFirst != INDEX_NONE)
		{
			FirstChange = FMath::Clamp(FMath::Min(FirstChange, GraphemeChangeFirst), Start, End - 1);
			LastChange = FMath::Clamp(FMath::Max(LastChange, GraphemeChangeLast), Start, End - 1);
		}
		// The segments the kept shaping cut the paragraph into: the cache's words, every run's start, every unshaped element.
		auto IsSegmentStart = [this, Start](int32 i) { return i <= Start || Measured[i].bSegmentStart; };
		auto NextSegmentStart = [End, &IsSegmentStart](int32 From)
		{
			int32 i = From;
			while (i < End && !IsSegmentStart(i))
			{
				i++;
			}
			return i;
		};
		// A joining script's shaper reads past a segment's ends: a window edge inside a run of one goes to the run's edge.
		auto ReadsContextAcross = [this](int32 Left, int32 Right)
		{
			return Measured[Right].RunIndex >= 0 && Measured[Left].RunIndex == Measured[Right].RunIndex
				&& FDreamTextShaper::ScriptReadsContext(Measured[Right].Script);
		};
		// L: the start of the segment before the one holding the first change, so one untouched segment stands between the
		// change and L, and the cut at L is decided by what it always was decided by.
		int32 Held = FirstChange;
		while (Held > Start && !IsSegmentStart(Held))
		{
			Held--;
		}
		int32 L = Start;
		if (Held > Start)
		{
			L = Held - 1;
			while (L > Start && !IsSegmentStart(L))
			{
				L--;
			}
		}
		while (L > Start && ReadsContextAcross(L - 1, L))
		{
			L--;
		}
		// Neutrals take the script before them: with no script of its own before L, L's are the paragraph's leading neutrals,
		// which take a script from after them, and the window starts at the paragraph.
		int32 OwnScriptBefore = INDEX_NONE;
		for (int32 i = L - 1; i >= Start; i--)
		{
			if (Measured[i].bOwnScript)
			{
				OwnScriptBefore = i;
				break;
			}
		}
		if (OwnScriptBefore == INDEX_NONE)
		{
			L = Start;
		}
		// What the itemizer carries into L: the last script of its own before it. Not L - 1's resolved script, which is 0 for an
		// inline object (the itemizer carries its script past one); at the paragraph's start, nothing (the shaper seeds Common).
		const uint32 Seed = L > Start ? Measured[OwnScriptBefore].Script : 0;
		// The script the kept elements after a point inherited: the kept layout's last script of its own before it.
		auto KeptLastScriptBefore = [this, Start, Join, JoinEnd](int32 Before)
		{
			for (int32 i = Before - 1; i >= JoinEnd; i--)
			{
				if (Measured[i].bOwnScript)
				{
					return Measured[i].Script;
				}
			}
			for (int32 k = ReplacedOwnScripts.Num() - 1; k >= 0; k--)
			{
				if (ReplacedOwnScripts[k])
				{
					return ReplacedScripts[k];
				}
			}
			for (int32 i = Join - 1; i >= Start; i--)
			{
				if (Measured[i].bOwnScript)
				{
					return Measured[i].Script;
				}
			}
			return 0u;
		};

		TArray<FDreamShapeElement> SpanElements;
		TArray<uint32> SequenceCodepoints;
		TArray<FDreamShapedRun> Runs;
		FDreamShapeWindowResult Result;
		int32 R = End;
		int32 SpanStart = Start;
		bool bAccepted = false;
		for (int32 Attempt = 0; Attempt < 2 && !bAccepted; Attempt++)
		{
			// R: the start of the segment after the one following the last change: one untouched segment on the right too.
			const int32 Following = NextSegmentStart(LastChange + 1);
			R = Following < End ? NextSegmentStart(Following + 1) : End;
			while (R < End && ReadsContextAcross(R - 1, R))
			{
				R++;
			}
			// The span: eight elements of context on either side (HarfBuzz and the cache read five code points), and the whole
			// grapheme cluster at R, which the itemizer reads to say whether R continues the window's last run.
			SpanStart = FMath::Max(Start, L - 8);
			int32 SpanEnd = FMath::Min(End, R + 8);
			if (R < End)
			{
				int32 ClusterEnd = R + 1;
				while (ClusterEnd < End && !GraphemeStart[ClusterEnd])
				{
					ClusterEnd++;
				}
				SpanEnd = FMath::Max(SpanEnd, ClusterEnd);
			}
			MakeShapeElements(Start, SpanStart, SpanEnd, SpanElements, SequenceCodepoints);
			const FDreamShapeParams Params = MakeShapeParams(SequenceCodepoints);
			FDreamShapeWindow Window;
			Window.Begin = L - SpanStart;
			Window.End = R - SpanStart;
			Window.SeedScript = Seed;
			Window.bParagraphStart = L == Start;
			bool bShaped = false;
			{
				FStageTimer ShapeTimer(EDreamTextLayoutStage::Shape);
				bShaped = FDreamTextShaper::ShapeWindow(SpanElements, Params, Window, Runs, Result);
			}
			if (!bShaped || (L == Start && !Result.bAnyOwnScript))
			{
				return false;
			}
			// The elements after the window inherit the script they inherited, or they are itemized differently now: the window
			// then reaches over them to the next element with a script of its own, once.
			const uint32 KeptLast = R < End ? KeptLastScriptBefore(R) : 0u;
			if (R >= End || (KeptLast != 0 && Result.LastScript == KeptLast))
			{
				bAccepted = true;
				break;
			}
			if (KeptLast == 0 || Result.LastScript == 0)
			{
				return false;
			}
			int32 NextOwn = R;
			while (NextOwn < End && !Measured[NextOwn].bOwnScript)
			{
				NextOwn++;
			}
			LastChange = FMath::Max(LastChange, FMath::Min(NextOwn, End - 1));
		}
		if (!bAccepted)
		{
			return false;
		}

		// Runs: the window's first continues the run before it when the kept layout had them as one -- the items on either side
		// of L are the kept ones -- and the run R is in becomes the window's last when the itemizer says R joins it.
		const bool bFirstContinues = L > Start && Measured[L - 1].RunIndex >= 0 && Measured[L - 1].RunIndex == Measured[L].RunIndex;
		int32 PreviousRun = K.RunStart - 1;
		for (int32 i = L - 1; i >= Start; i--)
		{
			if (Measured[i].RunIndex >= 0)
			{
				PreviousRun = Measured[i].RunIndex;
				break;
			}
		}
		const int32 FirstRun = bFirstContinues ? Measured[L - 1].RunIndex : PreviousRun + 1;
		const int32 LastRun = FirstRun + Runs.Num() - 1;
		int32 KeptNextRun = K.RunEnd;
		bool bNextIsAtR = false;
		for (int32 i = R; i < End; i++)
		{
			if (Measured[i].RunIndex >= 0)
			{
				KeptNextRun = Measured[i].RunIndex;
				bNextIsAtR = i == R;
				break;
			}
		}
		const int32 NewNextRun = Runs.Num() > 0 && Result.bLastRunContinues && bNextIsAtR ? LastRun : LastRun + 1;
		// The kept glyphs the window replaces: from where L's glyphs start to where R's do.
		const int32 KeptG0 = L == Start ? K.GlyphStart : Measured[L].GlyphStart;
		const int32 KeptG1 = R < End ? Measured[R].GlyphStart : K.GlyphEnd;

		// The window's records as Classify left them, then measured as MeasureParagraphByShaping measures a paragraph.
		for (int32 i = L; i < R; i++)
		{
			ResetMeasurement(i);
			FMeasured& M = Measured[i];
			const int32 w = i - L;
			M.Script = Result.Analysis.Scripts.IsValidIndex(w) ? Result.Analysis.Scripts[w] : 0u;
			M.bOwnScript = Result.Analysis.OwnScripts.IsValidIndex(w) && Result.Analysis.OwnScripts[w];
			M.bSegmentStart = Result.Analysis.SegmentStarts.IsValidIndex(w) && Result.Analysis.SegmentStarts[w];
			if (M.bImageSpace || M.bEmoji)
			{
				M.Glyph = GetInlineObjectGeo(i);
				M.ClusterAdvance = M.Glyph.XAdvance;
			}
		}
		TArray<FGlyphSource> KeptGlyphs = MoveTemp(Glyphs);
		Glyphs.Reset();
		ApplyShapedRuns(Runs, SpanStart, FirstRun);
		FillGlyphQuadsFromTable(0, Glyphs.Num());
		int32 Cursor = 0;
		for (int32 i = L; i < R; i++)
		{
			FMeasured& M = Measured[i];
			if (M.RunIndex >= 0 && M.GlyphCount > 0)
			{
				M.Glyph = Glyphs[M.GlyphStart].Quad;
				M.Glyph.XAdvance = M.ClusterAdvance;
			}
			if (M.GlyphCount > 0)
			{
				Cursor = M.GlyphStart + M.GlyphCount;
			}
			else
			{
				M.GlyphStart = Cursor;
			}
		}
		TArray<FGlyphSource> NewGlyphs = MoveTemp(Glyphs);
		Glyphs = MoveTemp(KeptGlyphs);
		SpliceRange(Glyphs, KeptG0, KeptG1 - KeptG0, NewGlyphs.Num());
		for (int32 g = 0; g < NewGlyphs.Num(); g++)
		{
			Glyphs[KeptG0 + g] = NewGlyphs[g];
		}
		for (int32 i = L; i < R; i++)
		{
			Measured[i].GlyphStart += KeptG0;
		}
		EditGlyphDelta = NewGlyphs.Num() - (KeptG1 - KeptG0);
		EditRunDelta = NewNextRun - KeptNextRun;
		// What follows R moves before the clusters and the widths are worked out, so the records they read are this layout's.
		RebaseAfterEdit(R, KeptG0 + NewGlyphs.Num());

		FinishClustersRange(Start, End, L, R, P.TabCount > 0);
		// Under negative letter spacing the preferred width walks the whole paragraph, every advance read again (the ink walk
		// of ParagraphPreferredWidth), and the tabs before the window still have the advances the kept lines put them at:
		// they go back to where one unwrapped line has them, as the window's and those after it just did.
		if (P.TabCount > 0 && In.FontSpace.X < 0.0f && L > Start)
		{
			SetUnwrappedAdvances(Start, Start, L - 1);
		}
		for (int32 i = L; i <= FMath::Min(R, End - 1); i++)
		{
			ClusterStarts[i] = Measured[i].bClusterStart;
		}
		P.PreferredWidth = ParagraphPreferredWidth(Start, End, P.bHasPreferredWidth, L);
		P.GlyphStart = K.GlyphStart;
		P.GlyphEnd = K.GlyphEnd + EditGlyphDelta;
		P.RunStart = K.RunStart;
		P.RunEnd = K.RunEnd + EditRunDelta;
		P.PendingGlyphs = 0;
		for (const FGlyphSource& G : NewGlyphs)
		{
			P.PendingGlyphs += G.Quad.bPending ? 1 : 0;
		}
		// The lines before L and after R break as they did: what decides them is what it was (AnalyseBreaks lowers both to the
		// first and the last break opportunity that came out other than it was).
		P.SamePrefix = L - Start;
		P.SameSuffix = FMath::Max(End - (R + 1), 0);
		bMeasuredInWindow = true;
		MeasureStart = L;
		MeasureEnd = R;
		Stats.WindowElements += R - L;
		return true;
	}

	bool FLayoutRun::MeasureWindowedByCodepoint(FParagraph& P)
	{
		const FParagraph& K = State->Paragraphs[EditParagraph];
		const int32 Start = P.Start;
		const int32 End = P.End;
		if (End <= Start)
		{
			return false;
		}
		const int32 Join = FMath::Clamp(EditStart, Start, End);
		const int32 JoinEnd = FMath::Clamp(EditStart + EditCount, Join, End);
		int32 FirstChange = FMath::Max(Join - 1, Start);
		int32 LastChange = FMath::Clamp(JoinEnd, Start, End - 1);
		if (GraphemeChangeFirst != INDEX_NONE)
		{
			FirstChange = FMath::Clamp(FMath::Min(FirstChange, GraphemeChangeFirst), Start, End - 1);
			LastChange = FMath::Clamp(FMath::Max(LastChange, GraphemeChangeLast), Start, End - 1);
		}
		// Per code point a face is chosen per grapheme cluster, and a glyph kerns against the one before it in the primary
		// face: the window is the change's clusters from the one before it, to the end of the cluster after the one it ends
		// in -- the first element past the window kerns against a cluster nothing in the edit could change.
		auto IsClusterStart = [this, Start](int32 i) { return i <= Start || (bool)GraphemeStart[i]; };
		int32 L = FirstChange;
		while (L > Start && !IsClusterStart(L))
		{
			L--;
		}
		int32 R = LastChange + 1;
		while (R < End && !IsClusterStart(R))
		{
			R++;
		}
		if (R < End)
		{
			R++;
			while (R < End && !IsClusterStart(R))
			{
				R++;
			}
		}
		const int32 KeptG0 = L == Start ? K.GlyphStart : Measured[L].GlyphStart;
		const int32 KeptG1 = R < End ? Measured[R].GlyphStart : K.GlyphEnd;
		for (int32 i = L; i < R; i++)
		{
			ResetMeasurement(i);
		}
		TArray<FGlyphSource> KeptGlyphs = MoveTemp(Glyphs);
		Glyphs.Reset();
		MeasureCodepointRange(Start, L, R);
		TArray<FGlyphSource> NewGlyphs = MoveTemp(Glyphs);
		Glyphs = MoveTemp(KeptGlyphs);
		SpliceRange(Glyphs, KeptG0, KeptG1 - KeptG0, NewGlyphs.Num());
		for (int32 g = 0; g < NewGlyphs.Num(); g++)
		{
			Glyphs[KeptG0 + g] = NewGlyphs[g];
		}
		for (int32 i = L; i < R; i++)
		{
			Measured[i].GlyphStart += KeptG0;
		}
		EditGlyphDelta = NewGlyphs.Num() - (KeptG1 - KeptG0);
		EditRunDelta = 0;
		RebaseAfterEdit(R, KeptG0 + NewGlyphs.Num());

		FinishClustersRange(Start, End, L, R, P.TabCount > 0);
		// Under negative letter spacing the preferred width walks the whole paragraph, every advance read again (the ink walk
		// of ParagraphPreferredWidth), and the tabs before the window still have the advances the kept lines put them at:
		// they go back to where one unwrapped line has them, as the window's and those after it just did.
		if (P.TabCount > 0 && In.FontSpace.X < 0.0f && L > Start)
		{
			SetUnwrappedAdvances(Start, Start, L - 1);
		}
		for (int32 i = L; i <= FMath::Min(R, End - 1); i++)
		{
			ClusterStarts[i] = Measured[i].bClusterStart;
		}
		P.PreferredWidth = ParagraphPreferredWidth(Start, End, P.bHasPreferredWidth, L);
		P.GlyphStart = K.GlyphStart;
		P.GlyphEnd = K.GlyphEnd + EditGlyphDelta;
		P.RunStart = K.RunStart;
		P.RunEnd = K.RunEnd;
		P.PendingGlyphs = 0;
		for (const FGlyphSource& G : NewGlyphs)
		{
			P.PendingGlyphs += G.Quad.bPending ? 1 : 0;
		}
		P.SamePrefix = L - Start;
		P.SameSuffix = FMath::Max(End - (R + 1), 0);
		bMeasuredInWindow = true;
		MeasureStart = L;
		MeasureEnd = R;
		Stats.WindowElements += R - L;
		return true;
	}

	void FLayoutRun::ReuseParagraph(FParagraph& P, const FParagraph& K)
	{
		const int32 ElementDelta = P.Start - K.Start;
		const int32 GlyphDelta = Glyphs.Num() - K.GlyphStart;
		const int32 RunDelta = RunCount - K.RunStart;
		const int32 FirstGlyph = Glyphs.Num();
		Glyphs.Append(State->Glyphs.GetData() + K.GlyphStart, K.GlyphEnd - K.GlyphStart);
		for (int32 g = FirstGlyph; g < Glyphs.Num(); g++)
		{
			Glyphs[g].ElementIndex += ElementDelta;
		}
		RunCount += K.RunEnd - K.RunStart;
		for (int32 k = 0; k < P.End - P.Start; k++)
		{
			// What measuring found. What the element is, its style and its language were found to be the kept element's
			// (MatchesKept), and stay this layout's: the style's CharIndex is where it stands now.
			FMeasured& M = Measured[P.Start + k];
			const FMeasured& Kept = State->Measured[K.Start + k];
			M.Glyph = Kept.Glyph;
			M.Advance = Kept.Advance;
			M.ClusterAdvance = Kept.ClusterAdvance;
			M.LetterSpacing = Kept.LetterSpacing;
			M.ClusterFitWidth = Kept.ClusterFitWidth;
			M.GlyphStart = Kept.GlyphStart + GlyphDelta;
			M.GlyphCount = Kept.GlyphCount;
			M.RunIndex = Kept.RunIndex >= 0 ? Kept.RunIndex + RunDelta : Kept.RunIndex;
			M.FaceIndex = Kept.FaceIndex;
			M.BidiLevel = Kept.BidiLevel;
			M.bBaseRightToLeft = Kept.bBaseRightToLeft;
			M.bShapeClusterStart = Kept.bShapeClusterStart;
			M.bClusterStart = Kept.bClusterStart;
			M.bCaretStop = Kept.bCaretStop;
			M.bSyntheticBold = Kept.bSyntheticBold;
			M.bSyntheticItalic = Kept.bSyntheticItalic;
			M.Script = Kept.Script;
			M.bOwnScript = Kept.bOwnScript;
			M.bSegmentStart = Kept.bSegmentStart;
			M.PenAfter = Kept.PenAfter;
		}
		// Its tabs are where the kept lines put them; the line breaker and FixTabs put them where this layout's lines do. Its
		// width on one line was taken before any of that, and is kept.
		P.PreferredWidth = K.PreferredWidth;
		P.bHasPreferredWidth = K.bHasPreferredWidth;
	}

	void FLayoutRun::MeasureParagraphByCodepoint(int32 Start, int32 End)
	{
		MeasureCodepointRange(Start, Start, End);
	}

	void FLayoutRun::MeasureCodepointRange(int32 ParagraphStart, int32 From, int32 To)
	{
		// One glyph per code point, metrics straight from the font: the path for fonts that cannot
		// shape. Kerning pairs with the previous character of the paragraph; the first has none, which
		// GetCodepointGlyph spells as a zero left neighbour.
		//
		// Direction is the shaper's job, so this path lays every paragraph out left to right and
		// FlowDirection does nothing here: without HarfBuzz there is no bidi and no reordering to
		// force. A font that cannot shape cannot draw right-to-left text correctly in the first place.
		//
		// Faces are chosen as the shaper chooses them (FDreamFontFaceResolver): per grapheme cluster, in its style and
		// its language, element by element when no face has the whole cluster. A face's own scale sizes its glyphs.
		uint32 PrevCharCode = 0;
		int32 PrevFace = INDEX_NONE;
		// Inside the paragraph, the walk is taken up where a walk from its start would stand: kerning against the element
		// before From, in the face it was drawn from -- none after an inline object, which is no character to kern against.
		if (From > ParagraphStart)
		{
			const FMeasured& Before = Measured[From - 1];
			if (!Before.bImageSpace && !Before.bEmoji && !Before.bSkipped && !Before.bHardBreak)
			{
				PrevCharCode = TextProcessingArray[From - 1].Unicode;
				PrevFace = Before.FaceIndex;
			}
		}
		int32 ClusterFace = 0;
		bool bResolvePerElement = false;
		for (int32 i = From; i < To; i++)
		{
			FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			const auto& Element = TextProcessingArray[i];
			M.RunIndex = -1;
			M.BidiLevel = 0;
			M.bBaseRightToLeft = false;
			M.bShapeClusterStart = true;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 1;
			FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
			G.ElementIndex = i;
			if (M.bImageSpace || M.bEmoji)
			{
				// What the primary face lacks of the style still decides the strokes drawn under the object.
				M.bSyntheticBold = M.Style.Bold && !EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Bold);
				M.bSyntheticItalic = M.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Italic);
				M.Glyph = GetInlineObjectGeo(i);
				M.ClusterAdvance = M.Glyph.XAdvance;
				M.FaceIndex = 0;
				G.Quad = M.Glyph;
				G.XAdvance = M.Glyph.XAdvance;
				// An inline object is no character to kern the next one against.
				PrevCharCode = 0;
				PrevFace = INDEX_NONE;
				continue;
			}
			// A tab is measured as the space it stands for, then widened to its tab stop.
			const uint32 Codepoint = M.bTab ? (uint32)' ' : Element.Unicode;
			// A window starts on a cluster's first element, so it resolves its face there as the walk from the start does.
			const bool bGraphemeStart = i == ParagraphStart || i == From || !GraphemeStart.IsValidIndex(i) || GraphemeStart[i];
			if (bGraphemeStart)
			{
				int32 ClusterEnd = i + 1;
				while (ClusterEnd < To && GraphemeStart.IsValidIndex(ClusterEnd) && !GraphemeStart[ClusterEnd]
					&& !Measured[ClusterEnd].bHardBreak && !Measured[ClusterEnd].bImageSpace && !Measured[ClusterEnd].bEmoji)
				{
					ClusterEnd++;
				}
				ClusterCodepoints.Reset();
				for (int32 k = i; k < ClusterEnd; k++)
				{
					ClusterCodepoints.Add(k == i ? Codepoint : TextProcessingArray[k].Unicode);
					AppendSequenceCodepoints(k, ClusterCodepoints);
				}
				const FDreamFontFaceChoice Choice = ResolveFace(ClusterCodepoints, M.Style, M.LanguageIndex);
				ClusterFace = Choice.FaceIndex;
				bResolvePerElement = !Choice.bCoversCluster && ClusterEnd - i > 1;
			}
			int32 Face = ClusterFace;
			if (bResolvePerElement)
			{
				// No face has the whole cluster ("a" and a skin tone): each part from a face that has it, still one cluster.
				ClusterCodepoints.Reset();
				ClusterCodepoints.Add(Codepoint);
				AppendSequenceCodepoints(i, ClusterCodepoints);
				Face = ResolveFace(ClusterCodepoints, M.Style, M.LanguageIndex).FaceIndex;
			}
			const bool bColorFace = IsColorFaceOf(Face);
			M.FaceIndex = Face;
			M.bSyntheticBold = M.Style.Bold && !bColorFace && !EnumHasAnyFlags(FaceStyleOf(Face), EDreamUIFontFaceStyle::Bold);
			M.bSyntheticItalic = M.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(Face), EDreamUIFontFaceStyle::Italic);
			// Kerning is the primary face's, so it pairs two of its own glyphs only.
			const uint32 KernLeft = (Face == 0 && PrevFace == 0) ? PrevCharCode : 0;
			const float GlyphSize = M.Style.Size * FaceScaleOf(Face);
			M.Glyph = GetCodepointGlyph(KernLeft, Codepoint, Face, GlyphSize, M.bSyntheticBold, bColorFace);
			M.ClusterAdvance = M.Glyph.XAdvance;
			G.Quad = M.Glyph;
			G.XAdvance = M.Glyph.XAdvance;
			G.GlyphSize = GlyphSize;
			PrevCharCode = Element.Unicode;
			PrevFace = Face;
		}
	}

	FDreamUICharData FLayoutRun::FetchGlyphQuad(int32 FaceIndex, uint32 GlyphIndex, float InFontSize, bool bInBold, bool bColorFace) const
	{
		Stats.QuadFetches++;
		// The same canvas-scale dance GetCodepointGlyph does for code points: rasterize at the device size and
		// measure back in text units, so a bitmap font stays crisp under a scaled canvas.
		if (bShouldScaleFontSizeWithRootCanvas)
		{
			float Scale, OneDivideScale;
			if (bPixelPerfect || DynamicPixelsPerUnit == 1.0f)
			{
				Scale = RootCanvasScale;
				OneDivideScale = OneDivideRootCanvasScale;
			}
			else
			{
				Scale = DynamicPixelsPerUnit;
				OneDivideScale = OneDivideDynamicPixelsPerUnit;
			}
			const float WantedSize = InFontSize * Scale;
			float ScaledSize = FMath::Clamp(WantedSize, 0.0f, MaxFontSize);
			// Same as GetCodepointGlyph: once the font's raster cap clamps the size, the measurement has to be
			// divided by the ratio that was achieved, not by the one that was asked for.
			if (ScaledSize > 0.0f && WantedSize > ScaledSize)
			{
				OneDivideScale = OneDivideScale * (WantedSize / ScaledSize);
			}
			ScaledSize = RasterSizeFor(bColorFace, ScaledSize, OneDivideScale);
			FDreamUICharData Data = Font->GetGlyphData(FaceIndex, GlyphIndex, ScaledSize, bInBold);
			Data.Width *= OneDivideScale;
			Data.Height *= OneDivideScale;
			Data.XAdvance *= OneDivideScale;
			Data.XOffset *= OneDivideScale;
			Data.YOffset *= OneDivideScale;
			return Data;
		}
		float BackToTextUnits = 1.0f;
		const float RasterSize = RasterSizeFor(bColorFace, InFontSize, BackToTextUnits);
		FDreamUICharData Data = Font->GetGlyphData(FaceIndex, GlyphIndex, RasterSize, bInBold);
		if (BackToTextUnits != 1.0f)
		{
			// A colour glyph's ColorTexelsPerEm is per em of its cell whatever size it came at: only lengths scale back.
			Data.Width *= BackToTextUnits;
			Data.Height *= BackToTextUnits;
			Data.XAdvance *= BackToTextUnits;
			Data.XOffset *= BackToTextUnits;
			Data.YOffset *= BackToTextUnits;
		}
		return Data;
	}

	void FLayoutRun::MakeShapeElements(int32 ParagraphStart, int32 Start, int32 End, TArray<FDreamShapeElement>& OutElements, TArray<uint32>& OutSequences) const
	{
		OutElements.Reset(End - Start);
		// The code points of each element after its base, so HarfBuzz sees an emoji sequence whole and a ligature over it
		// forms inside its element.
		OutSequences.Reset();
		for (int32 i = Start; i < End; i++)
		{
			const FMeasured& M = Measured[i];
			FDreamShapeElement E;
			E.Codepoint = TextProcessingArray[i].Unicode;
			E.Size = M.Style.Size;
			E.bBold = M.Style.Bold;
			E.bItalic = M.Style.Italic;
			E.bUnshaped = M.bSkipped || M.bHardBreak || M.bImageSpace || M.bEmoji;
			E.bGraphemeStart = GraphemeStart.IsValidIndex(i) ? (bool)GraphemeStart[i] : true;
			// With ligatures on, a tag edge ends the run, so a ligature never straddles one: a tag's range, its
			// colour and its hyperlink all address whole glyphs.
			E.bRunBreakBefore = bLigatures && i > ParagraphStart && E.bGraphemeStart && FollowsMarkup.IsValidIndex(i) && FollowsMarkup[i];
			E.LanguageIndex = M.LanguageIndex;
			if (!E.bUnshaped)
			{
				E.SequenceStart = OutSequences.Num();
				AppendSequenceCodepoints(i, OutSequences);
				E.SequenceCount = OutSequences.Num() - E.SequenceStart;
			}
			OutElements.Add(E);
		}
	}

	FDreamShapeParams FLayoutRun::MakeShapeParams(const TArray<uint32>& SequenceCodepoints) const
	{
		FDreamShapeParams Params;
		Params.Font = Font;
		Params.bUseKerning = bUseKerning;
		Params.bLigatures = bLigatures;
		Params.FlowDirection = In.FlowDirection;
		Params.Languages = &Languages;
		Params.SequenceCodepoints = SequenceCodepoints.Num() > 0 ? &SequenceCodepoints : nullptr;
		Params.bAllowColorFaces = In.bAllowColorFaces;
		return Params;
	}

	bool FLayoutRun::MeasureParagraphByShaping(FParagraph& P)
	{
		const int32 Start = P.Start;
		const int32 End = P.End;
		TArray<FDreamShapeElement> ShapeElements;
		TArray<uint32> SequenceCodepoints;
		MakeShapeElements(Start, Start, End, ShapeElements, SequenceCodepoints);
		const FDreamShapeParams Params = MakeShapeParams(SequenceCodepoints);
		TArray<FDreamShapedRun> ShapedRuns;
		TArray<uint8> Levels;
		FDreamShapeAnalysis Analysis;
		bool bBaseRightToLeft = false;
		bool bShaped = false;
		{
			FStageTimer ShapeTimer(EDreamTextLayoutStage::Shape);
			bShaped = FDreamTextShaper::ShapeParagraph(ShapeElements, Params, ShapedRuns, bBaseRightToLeft, &Levels, &Analysis);
		}
		if (!bShaped)
		{
			return false;
		}

		const int32 FirstGlyph = Glyphs.Num();
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			const int32 k = i - Start;
			M.bBaseRightToLeft = bBaseRightToLeft;
			M.BidiLevel = Levels.IsValidIndex(k) ? Levels[k] : (bBaseRightToLeft ? 1 : 0);
			M.RunIndex = -1;
			M.GlyphStart = FirstGlyph;
			M.GlyphCount = 0;
			M.ClusterAdvance = 0.0f;
			M.Advance = 0.0f;
			M.bShapeClusterStart = true;
			// What a window over the paragraph later has to agree with: each element's script, and where the segments start.
			M.Script = Analysis.Scripts.IsValidIndex(k) ? Analysis.Scripts[k] : 0u;
			M.bOwnScript = Analysis.OwnScripts.IsValidIndex(k) && Analysis.OwnScripts[k];
			M.bSegmentStart = Analysis.SegmentStarts.IsValidIndex(k) && Analysis.SegmentStarts[k];
			if (M.bImageSpace || M.bEmoji)
			{
				// Inline objects are measured by the layout, the way they always were, and never kern.
				M.Glyph = GetInlineObjectGeo(i);
				M.ClusterAdvance = M.Glyph.XAdvance;
			}
		}
		ApplyShapedRuns(ShapedRuns, Start, RunCount);
		if (bInPlaceParse)
		{
			FillGlyphQuadsFromTable(FirstGlyph, Glyphs.Num());
		}
		else
		{
			FillGlyphQuads(P, FirstGlyph, Glyphs.Num());
		}
		int32 Cursor = FirstGlyph;
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (M.RunIndex >= 0 && M.GlyphCount > 0)
			{
				M.Glyph = Glyphs[M.GlyphStart].Quad;
				M.Glyph.XAdvance = M.ClusterAdvance;
			}
			// An element with no glyph of its own -- a mark a ligature took in, an inline object -- stands where its glyphs
			// would: after the glyphs of the element before it. A window spliced into the paragraph numbers them the same.
			if (M.GlyphCount > 0)
			{
				Cursor = M.GlyphStart + M.GlyphCount;
			}
			else
			{
				M.GlyphStart = Cursor;
			}
		}
		return true;
	}

	void FLayoutRun::ApplyShapedRuns(const TArray<FDreamShapedRun>& ShapedRuns, int32 ElementBase, int32 FirstRun)
	{
		RunCount = FirstRun;
		for (const FDreamShapedRun& ShapedRun : ShapedRuns)
		{
			const int32 RunElementStart = ElementBase + ShapedRun.ElementStart;
			const int32 RunElementEnd = ElementBase + ShapedRun.ElementEnd;
			const int32 RunIndex = RunCount++;
			// A scaled face's run was shaped at its style size times the scale, and its glyphs are rasterized there too.
			const float GlyphSize = ShapedRun.Size * ShapedRun.FaceScale;
			// Glyphs stay in the shaper's visual order; an element's glyphs are contiguous within it.
			int32 CurrentElement = -1;
			for (const FDreamShapedGlyph& Shaped : ShapedRun.Glyphs)
			{
				const int32 ElementIndex = ElementBase + Shaped.ElementIndex;
				FMeasured& M = Measured[ElementIndex];
				if (ElementIndex != CurrentElement)
				{
					M.GlyphStart = Glyphs.Num();
					M.GlyphCount = 0;
					M.FaceIndex = Shaped.FaceIndex;
					CurrentElement = ElementIndex;
				}
				// Its quad is asked for once the paragraph's every glyph is known (FillGlyphQuads).
				FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
				G.RasterFace = Shaped.FaceIndex;
				G.RasterGlyph = Shaped.GlyphIndex;
				G.bRasterBold = ShapedRun.bSyntheticBold;
				G.bRasterColorFace = ShapedRun.bColorFace;
				G.XAdvance = Shaped.XAdvance;
				G.XOffset = Shaped.XOffset;
				G.YOffset = Shaped.YOffset;
				G.ElementIndex = ElementIndex;
				G.GlyphSize = GlyphSize;
				M.GlyphCount++;
				M.ClusterAdvance += Shaped.XAdvance;
				M.RunIndex = RunIndex;
			}
			const bool bRunHasGlyphs = ShapedRun.Glyphs.Num() > 0;
			for (int32 i = RunElementStart; i < RunElementEnd; i++)
			{
				FMeasured& M = Measured[i];
				// A shaped-cluster continuation -- a combining mark, the second letter of a ligature -- has no glyph of
				// its own, so no advance, but it still belongs to the run for placement.
				M.RunIndex = RunIndex;
				if (M.GlyphCount == 0)
				{
					M.FaceIndex = ShapedRun.FaceIndex;
					M.bShapeClusterStart = !bRunHasGlyphs;
				}
				M.bSyntheticBold = ShapedRun.bSyntheticBold;
				M.bSyntheticItalic = M.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(M.FaceIndex), EDreamUIFontFaceStyle::Italic);
			}
		}
	}

	void FLayoutRun::FillGlyphQuads(const FParagraph& P, int32 Begin, int32 End)
	{
		const FParagraph* Prefix = bUseState && P.PrefixDonor != INDEX_NONE ? &State->Paragraphs[P.PrefixDonor] : nullptr;
		const FParagraph* Suffix = bUseState && P.SuffixDonor != INDEX_NONE ? &State->Paragraphs[P.SuffixDonor] : nullptr;
		// The same glyph of the same face at the same size and weight is the same quad while the atlas is the same (the measure
		// key): asking the font again would hand it back. An edited paragraph's glyphs before and after the edit are where its
		// donors had them, and are taken from there rather than looked up one by one.
		auto Donated = [this](const FGlyphSource& G, int32 KeptIndex) -> const FGlyphSource*
		{
			const FGlyphSource& K = State->Glyphs[KeptIndex];
			const bool bSame = !K.Quad.bPending && K.RasterFace >= 0 && K.RasterFace == G.RasterFace && K.RasterGlyph == G.RasterGlyph
				&& K.GlyphSize == G.GlyphSize && K.bRasterBold == G.bRasterBold && K.bRasterColorFace == G.bRasterColorFace;
			return bSame ? &K : nullptr;
		};
		for (int32 g = Begin; g < End; g++)
		{
			FGlyphSource& G = Glyphs[g];
			const FGlyphSource* Kept = nullptr;
			if (Prefix != nullptr && Prefix->GlyphStart + (g - Begin) < Prefix->GlyphEnd)
			{
				Kept = Donated(G, Prefix->GlyphStart + (g - Begin));
			}
			if (Kept == nullptr && Suffix != nullptr && Suffix->GlyphEnd - (End - g) >= Suffix->GlyphStart)
			{
				Kept = Donated(G, Suffix->GlyphEnd - (End - g));
			}
			G.Quad = Kept != nullptr ? Kept->Quad : FetchGlyphQuad(G.RasterFace, G.RasterGlyph, G.GlyphSize, G.bRasterBold, G.bRasterColorFace);
		}
	}

	void FLayoutRun::FillGlyphQuadsFromTable(int32 Begin, int32 End)
	{
		// The quads the state was handed before, by what they were asked with: the same while the measure key holds. A glyph
		// still rasterizing is asked for again next time, so it is not remembered; nor are more than a few thousand glyphs.
		constexpr int32 MaxRememberedQuads = 8192;
		for (int32 g = Begin; g < End; g++)
		{
			FGlyphSource& G = Glyphs[g];
			FQuadKey Key;
			Key.Face = G.RasterFace;
			Key.Glyph = G.RasterGlyph;
			Key.Size = G.GlyphSize;
			Key.bBold = G.bRasterBold;
			Key.bColorFace = G.bRasterColorFace;
			if (const FDreamUICharData* Found = State->Quads.Find(Key))
			{
				G.Quad = *Found;
				continue;
			}
			G.Quad = FetchGlyphQuad(G.RasterFace, G.RasterGlyph, G.GlyphSize, G.bRasterBold, G.bRasterColorFace);
			if (!G.Quad.bPending)
			{
				if (State->Quads.Num() >= MaxRememberedQuads)
				{
					State->Quads.Reset();
				}
				State->Quads.Add(Key, G.Quad);
			}
		}
	}

	float FLayoutRun::LetterSpacingFor(int32 ElementIndex) const
	{
		if (In.FontSpace.X == 0.0f)
		{
			return 0.0f;
		}
		const FMeasured& M = Measured[ElementIndex];
		// An inline image is not a character, and a cursive script is not spaced at all (see IsCursiveScript).
		if (M.bImageSpace || IsCursiveScript(TextProcessingArray[ElementIndex].Unicode))
		{
			return 0.0f;
		}
		return In.FontSpace.X;
	}

	void FLayoutRun::FinishClusters(int32 Start, int32 End)
	{
		FinishClustersRange(Start, End, Start, End - 1, true);
	}

	void FLayoutRun::FinishClustersRange(int32 ParagraphStart, int32 ParagraphEnd, int32 From, int32 To, bool bTabsToEnd)
	{
		To = FMath::Min(To, ParagraphEnd - 1);
		if (From > To)
		{
			return;
		}
		// A cluster is what is never pulled apart: an extended grapheme cluster (UAX #29: a base and its marks, an
		// emoji sequence, a conjunct) joined with whatever the shaper made one glyph cluster of (a ligature). Lines
		// break only between clusters and letter spacing goes only between them, never between a letter and its
		// accent; a caret still stands at every grapheme cluster, inside a ligature too.
		for (int32 i = From; i <= To; i++)
		{
			FMeasured& M = Measured[i];
			const bool bUnshaped = M.bSkipped || M.bHardBreak || M.bImageSpace || M.bEmoji;
			const FMeasured* Prev = i > ParagraphStart ? &Measured[i - 1] : nullptr;
			const bool bPrevUnshaped = Prev != nullptr && (Prev->bSkipped || Prev->bHardBreak || Prev->bImageSpace || Prev->bEmoji);
			const bool bGrapheme = GraphemeStart.IsValidIndex(i) ? (bool)GraphemeStart[i] : true;
			// A new run starts a cluster only where a grapheme cluster starts too: one no face has whole ("a" and a skin
			// tone) has its parts in runs of their own faces, and is still one cluster -- one caret, never split.
			M.bClusterStart = Prev == nullptr || bUnshaped || bPrevUnshaped || (bGrapheme && Prev->RunIndex != M.RunIndex)
				|| (bGrapheme && M.bShapeClusterStart);
			M.bCaretStop = M.bClusterStart || bGrapheme;
			M.LetterSpacing = 0.0f;
			M.ClusterFitWidth = 0.0f;
		}
		for (int32 i = From; i <= To; i++)
		{
			FMeasured& M = Measured[i];
			if (!M.bClusterStart)continue;
			float Width = M.ClusterAdvance;
			for (int32 j = i + 1; j < ParagraphEnd && !Measured[j].bClusterStart; j++)
			{
				Width += Measured[j].ClusterAdvance;
			}
			M.ClusterFitWidth = Width;
			M.LetterSpacing = LetterSpacingFor(i);
		}
		Stats.ClustersFinished += To - From + 1;
		// A tab reaches to the next stop from where it stands; on one unwrapped line that is from the paragraph's start.
		// The line breaker measures it again where its own line starts. Taken up inside the paragraph, the pen stands where
		// the preferred width's sum left it; past the range only a tab can change, so a paragraph without one stops there.
		SetUnwrappedAdvances(ParagraphStart, From, bTabsToEnd ? ParagraphEnd - 1 : To);
	}

	void FLayoutRun::SetUnwrappedAdvances(int32 ParagraphStart, int32 From, int32 Last)
	{
		float X = From > ParagraphStart ? Measured[From - 1].PenAfter : 0.0f;
		for (int32 i = From; i <= Last; i++)
		{
			FMeasured& M = Measured[i];
			M.Advance = M.ClusterAdvance + (M.bClusterStart ? M.LetterSpacing : 0.0f);
			if (M.bTab && M.bClusterStart)
			{
				SetTabAdvance(M, X);
			}
			if (!M.bSkipped)
			{
				X += M.Advance;
			}
		}
	}

	void FLayoutRun::CombineBreaks(int32 Begin, int32 End)
	{
		Begin = FMath::Max(Begin, 0);
		End = FMath::Min(End, TextProcessingArray.Num());
		if (Begin >= End)
		{
			return;
		}
		const bool bPhrase = In.PhraseWrap != EDreamTextPhraseWrap::Off;
		FDreamTextBreaker::CombineBreakOpportunities(ElementCodepoints, LineBreakRaw, bPhrase ? &WordBreakRaw : nullptr, In.PhraseWrap,
			Begin, End, CanBreakBefore);
		// A line never ends inside a cluster, whatever the line-break rules allow.
		for (int32 i = Begin; i < End; i++)
		{
			if (!Measured[i].bClusterStart)
			{
				CanBreakBefore[i] = false;
			}
		}
		Stats.BitsRecomputed += End - Begin;
	}

	void FLayoutRun::AnalyseBreaks()
	{
		const int32 Count = TextProcessingArray.Num();
		const bool bPhrase = In.PhraseWrap != EDreamTextPhraseWrap::Off;
		if (bInPlaceParse && State->bHasBreakBits && (!bPhrase || State->bHasWordBits) && LineBreakRaw.Num() == Count
			&& CanBreakBefore.Num() == Count && (!bPhrase || WordBreakRaw.Num() == Count))
		{
			// Every paragraph but the edited one keeps its bits, spliced with its elements.
			if (EditParagraph == INDEX_NONE)
			{
				return;
			}
			FParagraph& P = Paragraphs[EditParagraph];
			int32 LineFirst = INDEX_NONE;
			int32 LineLast = INDEX_NONE;
			AnalyseEditedParagraph(EDreamTextBoundaryKind::Line, LineBreakRaw, true, LineFirst, LineLast);
			int32 WordFirst = INDEX_NONE;
			int32 WordLast = INDEX_NONE;
			if (bPhrase)
			{
				AnalyseEditedParagraph(EDreamTextBoundaryKind::Word, WordBreakRaw, true, WordFirst, WordLast);
			}
			// The opportunities again over what was measured, where a raw bit changed, and one element either side: an
			// opportunity reads its element's cluster and the code point before it.
			int32 Low = bMeasuredInWindow ? MeasureStart : P.Start;
			int32 High = bMeasuredInWindow ? MeasureEnd : P.End;
			for (const int32 First : { LineFirst, WordFirst })
			{
				Low = First != INDEX_NONE ? FMath::Min(Low, First) : Low;
			}
			for (const int32 Last : { LineLast, WordLast })
			{
				High = Last != INDEX_NONE ? FMath::Max(High, Last + 1) : High;
			}
			Low = FMath::Max(Low - 1, P.Start);
			High = FMath::Min(High + 1, P.End);
			TBitArray<> Before;
			Before.Init(false, FMath::Max(High - Low, 0));
			if (High > Low)
			{
				Before.SetRangeFromRange(0, High - Low, CanBreakBefore, Low);
			}
			CombineBreaks(Low, High);
			if (bMeasuredInWindow)
			{
				// The lines before the first opportunity that came out other than it was, and after the last, still break as they did.
				for (int32 i = Low; i < High; i++)
				{
					if ((bool)CanBreakBefore[i] != (bool)Before[i - Low])
					{
						P.SamePrefix = FMath::Min(P.SamePrefix, FMath::Max(i - P.Start, 0));
						P.SameSuffix = FMath::Min(P.SameSuffix, FMath::Max(P.End - (i + 1), 0));
					}
				}
			}
			return;
		}
		// A layout that read every element, or one whose kept layout had no break bits to splice (it did not wrap).
		LineBreakRaw.Init(false, Count);
		AnalyseBoundaries(EDreamTextBoundaryKind::Line, LineBreakRaw, bUseState && !bInPlaceParse && State->bHasBreakBits ? &State->LineBreakRaw : nullptr);
		if (bPhrase)
		{
			WordBreakRaw.Init(false, Count);
			AnalyseBoundaries(EDreamTextBoundaryKind::Word, WordBreakRaw, bUseState && !bInPlaceParse && State->bHasWordBits ? &State->WordBreakRaw : nullptr);
		}
		CanBreakBefore.Init(false, Count);
		CombineBreaks(0, Count);
	}

	bool FLayoutRun::SameAsKept(int32 i, int32 j, const FParagraph& P, const FParagraph& K, bool bFromEnd) const
	{
		const FMeasured& A = Measured[i];
		const FMeasured& B = State->Measured[j];
		const FDreamUIText_TextProcessingElement& ElementA = TextProcessingArray[i];
		const FDreamUIText_TextProcessingElement& ElementB = State->Elements[j];
		// A tab is as wide as where its line puts it, and the kept one was put by the kept lines: the rest of it has to agree.
		const bool bTab = A.bTab && A.bClusterStart;
		if (ElementA.Unicode != ElementB.Unicode || ElementA.Type != ElementB.Type || ElementA.bEscaped != ElementB.bEscaped
			|| !SameCharData(A.Glyph, B.Glyph, bTab) || A.GlyphCount != B.GlyphCount || A.FaceIndex != B.FaceIndex
			|| A.BidiLevel != B.BidiLevel || A.bBaseRightToLeft != B.bBaseRightToLeft || A.bHardBreak != B.bHardBreak
			|| A.bSkipped != B.bSkipped || A.bWhitespace != B.bWhitespace || A.bTab != B.bTab || A.bImageSpace != B.bImageSpace
			|| A.bEmoji != B.bEmoji || A.EmojiItem != B.EmojiItem || A.bVisibleGlyph != B.bVisibleGlyph
			|| A.bShapeClusterStart != B.bShapeClusterStart || A.bClusterStart != B.bClusterStart || A.bCaretStop != B.bCaretStop
			|| A.bSyntheticBold != B.bSyntheticBold || A.bSyntheticItalic != B.bSyntheticItalic || !SameLayoutStyle(A.Style, B.Style))
		{
			return false;
		}
		if (!bTab && (A.Advance != B.Advance || A.ClusterAdvance != B.ClusterAdvance || A.LetterSpacing != B.LetterSpacing
			|| A.ClusterFitWidth != B.ClusterFitWidth))
		{
			return false;
		}
		// Glyphs and runs are numbered from the paragraph's start, or at its end from its end: a line orders a cluster's glyphs
		// by where they start and cuts pieces where the run changes.
		const int32 GlyphA = bFromEnd ? A.GlyphStart - P.GlyphEnd : A.GlyphStart - P.GlyphStart;
		const int32 GlyphB = bFromEnd ? B.GlyphStart - K.GlyphEnd : B.GlyphStart - K.GlyphStart;
		if (GlyphA != GlyphB || (A.RunIndex < 0) != (B.RunIndex < 0))
		{
			return false;
		}
		if (A.RunIndex >= 0 && (bFromEnd ? A.RunIndex - P.RunEnd != B.RunIndex - K.RunEnd : A.RunIndex - P.RunStart != B.RunIndex - K.RunStart))
		{
			return false;
		}
		for (int32 g = 0; g < A.GlyphCount; g++)
		{
			if (!SameGlyphSource(Glyphs[A.GlyphStart + g], State->Glyphs[B.GlyphStart + g]))
			{
				return false;
			}
		}
		if ((bool)GraphemeStart[i] != (bool)State->GraphemeStart[j])
		{
			return false;
		}
		return !(ShouldWrap() && State->bHasBreakBits) || (bool)CanBreakBefore[i] == (bool)State->CanBreakBefore[j];
	}

	void FLayoutRun::CompareWithDonors()
	{
		// An edit seen from the content knows where its paragraph came out the same from its window (MeasureWindowed), and
		// a short paragraph it measured whole keeps nothing: nothing to compare element by element.
		if (!bUseState || bInPlaceParse)
		{
			return;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Compare);
		FStageTimer Timer(EDreamTextLayoutStage::Compare);
		for (FParagraph& P : Paragraphs)
		{
			if (P.Source != INDEX_NONE)
			{
				continue;
			}
			if (P.PrefixDonor != INDEX_NONE)
			{
				const FParagraph& K = State->Paragraphs[P.PrefixDonor];
				while (P.SamePrefix < P.TextPrefix && SameAsKept(P.Start + P.SamePrefix, K.Start + P.SamePrefix, P, K, false))
				{
					P.SamePrefix++;
				}
			}
			if (P.SuffixDonor != INDEX_NONE)
			{
				const FParagraph& K = State->Paragraphs[P.SuffixDonor];
				int32 Limit = FMath::Min(P.TextSuffix, (P.End - P.Start) - P.SamePrefix);
				if (P.SuffixDonor == P.PrefixDonor)
				{
					Limit = FMath::Min(Limit, (K.End - K.Start) - P.SamePrefix);
				}
				while (P.SameSuffix < Limit && SameAsKept(P.End - 1 - P.SameSuffix, K.End - 1 - P.SameSuffix, P, K, true))
				{
					P.SameSuffix++;
				}
			}
		}
	}

	void FLayoutRun::AddLine(int32 Start, int32 End, int32 HardBreak, int32 DecidedAt, int32 Source)
	{
		FLineRange& Range = LineRanges.AddDefaulted_GetRef();
		Range.Start = Start;
		Range.End = End;
		Range.HardBreakElement = HardBreak;
		Range.DecidedAt = DecidedAt;
		LineSources.Add(Source);
	}

	void FLayoutRun::CopyKeptLines(const FParagraph& K, int32 FirstLine, int32 Delta, int32 HardBreak, bool bKeepPlacement)
	{
		for (int32 Line = FirstLine; Line < K.LineEnd; Line++)
		{
			const FLineRange& Kept = State->LineRanges[Line];
			AddLine(Kept.Start + Delta, Kept.End + Delta, Line == K.LineEnd - 1 ? HardBreak : -1, Kept.DecidedAt + Delta,
				bKeepPlacement ? Line : INDEX_NONE);
		}
	}

	void FLayoutRun::BreakLines()
	{
		const int32 Count = TextProcessingArray.Num();
		LineRanges.Reset();
		LineSources.Reset();
		for (FParagraph& P : Paragraphs)
		{
			P.LineStart = LineRanges.Num();
			const int32 HardBreak = P.End < Count ? P.End : -1;
			if (bBreakKeySame && P.Source != INDEX_NONE)
			{
				// A paragraph measured as it was breaks as it did; its lines keep their placement too if its style is the same.
				const FParagraph& K = State->Paragraphs[P.Source];
				CopyKeptLines(K, K.LineStart, P.Start - K.Start, HardBreak, P.bPlaceSame);
			}
			else
			{
				int32 From = P.Start;
				if (bBreakKeySame && P.PrefixDonor != INDEX_NONE)
				{
					// An edited paragraph keeps its lines before the edit: each one whose every deciding element came out as it was.
					const FParagraph& K = State->Paragraphs[P.PrefixDonor];
					const int32 Delta = P.Start - K.Start;
					for (int32 Line = K.LineStart; Line < K.LineEnd; Line++)
					{
						const FLineRange& Kept = State->LineRanges[Line];
						if (Kept.DecidedAt - K.Start >= P.SamePrefix)
						{
							break;
						}
						AddLine(Kept.Start + Delta, Kept.End + Delta, -1, Kept.DecidedAt + Delta, Line);
						From = Kept.End + Delta;
					}
				}
				BreakParagraphFrom(P, From, HardBreak);
			}
			P.LineEnd = LineRanges.Num();
		}
	}

	void FLayoutRun::BreakParagraphFrom(const FParagraph& P, int32 From, int32 HardBreak)
	{
		const bool bWrap = ShouldWrap();
		const bool bPerCharacter = In.WrappingPolicy == ETextWrappingPolicy::AllowPerCharacterWrapping;
		const FParagraph* Suffix = bBreakKeySame && P.SuffixDonor != INDEX_NONE && P.SameSuffix > 0 ? &State->Paragraphs[P.SuffixDonor] : nullptr;
		int32 LineStart = From;
		while (true)
		{
			// Inside the paragraph's unchanged end, a line that starts where a line of the donor started is that line, and so is
			// every line after it: a line is decided from its own start and what follows alone.
			if (Suffix != nullptr && LineStart >= P.End - P.SameSuffix)
			{
				const int32 KeptStart = LineStart - P.End + Suffix->End;
				for (int32 Line = Suffix->LineStart; Line < Suffix->LineEnd && State->LineRanges[Line].Start <= KeptStart; Line++)
				{
					if (State->LineRanges[Line].Start == KeptStart)
					{
						CopyKeptLines(*Suffix, Line, P.End - Suffix->End, HardBreak, true);
						return;
					}
				}
			}
			// One line, greedily, from its own start: nothing carries over from the line before, so where a line ends depends
			// on the elements from its start to the one that did not fit, and on nothing else.
			int32 Break = INDEX_NONE;
			int32 DecidedAt = P.End;
			if (bWrap)
			{
				float X = 0.0f;
				int32 LastOpportunity = -1;
				for (int32 i = LineStart; i < P.End; i++)
				{
					FMeasured& M = Measured[i];
					if (M.bSkipped)continue;
					if (i > LineStart && CanBreakBefore[i])
					{
						LastOpportunity = i;
					}
					// A cluster is fitted whole, at its first element. Whitespace hangs: it may run past the wrap width
					// and never forces a break itself.
					if (M.bClusterStart && !M.bWhitespace && X + M.ClusterFitWidth > WrapWidth + UE_KINDA_SMALL_NUMBER)
					{
						if (LastOpportunity > LineStart)
						{
							Break = LastOpportunity;
						}
						else if (bPerCharacter && i > LineStart)
						{
							// Too wide on a line of its own start: a word longer than the box. Break inside it if the policy
							// allows, otherwise let it overflow. The cut lands between clusters and avoids stranding
							// closing punctuation at a line start, as a browser's break-all does.
							Break = FDreamTextBreaker::FindKinsokuSafeFallback(ElementCodepoints, LineStart, i, &ClusterStarts);
						}
						if (Break != INDEX_NONE)
						{
							DecidedAt = i;
							break;
						}
					}
					// A tab reaches from where it stands on its own line to the next stop.
					if (M.bTab && M.bClusterStart)
					{
						SetTabAdvance(M, X);
					}
					X += M.Advance;
				}
			}
			if (Break == INDEX_NONE)
			{
				AddLine(LineStart, P.End, HardBreak, P.End, INDEX_NONE);
				return;
			}
			AddLine(LineStart, Break, -1, DecidedAt, INDEX_NONE);
			LineStart = Break;
		}
	}

	void FLayoutRun::FixTabs()
	{
		// A tab reaches from where it stands on its own line to the next stop: walked again line by line, kept lines and lines
		// broken now alike, so each tab is what its line makes it however the line came to be.
		for (const FParagraph& P : Paragraphs)
		{
			// A paragraph taken where it stood, with its kept lines, has its tabs where those lines put them already.
			if (!P.bHasTab || (P.bPositional && bBreakKeySame))
			{
				continue;
			}
			for (int32 Line = P.LineStart; Line < P.LineEnd; Line++)
			{
				const FLineRange& Range = LineRanges[Line];
				float X = 0.0f;
				for (int32 i = Range.Start; i < Range.End; i++)
				{
					FMeasured& M = Measured[i];
					if (M.bSkipped)continue;
					if (M.bTab && M.bClusterStart)
					{
						SetTabAdvance(M, X);
					}
					X += M.Advance;
				}
			}
		}
	}

	void FLayoutRun::ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 VisualRunStart, FDreamUITextLineProperty& LineProperty, float XOffset)
	{
		if (XOffset == 0.0f)return;
		for (int32 i = LineItemStart; i < Dest->Items.Num(); i++)
		{
			Dest->Items[i].Pen.X += XOffset;
		}
		for (auto& Caret : LineProperty.CaretPropertyList)
		{
			Caret.CaretPosition.X += XOffset;
		}
		for (int32 i = ImageStart; i < Dest->Images.Num(); i++)
		{
			Dest->Images[i].Position.X += XOffset;
		}
		for (int32 i = EmojiStart; i < Dest->Emojis.Num(); i++)
		{
			Dest->Emojis[i].Position.X += XOffset;
		}
		for (int32 i = VisualRunStart; i < Dest->VisualRuns.Num(); i++)
		{
			Dest->VisualRuns[i].Left += XOffset;
			Dest->VisualRuns[i].Right += XOffset;
		}
	}

	void FLayoutRun::ShiftPlacedLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, float XOffset)
	{
		if (XOffset == 0.0f)return;
		for (int32 i = LineItemStart; i < Dest->Items.Num(); i++)
		{
			Dest->Items[i].Pen.X += XOffset;
		}
		for (int32 i = ImageStart; i < Dest->Images.Num(); i++)
		{
			Dest->Images[i].Position.X += XOffset;
		}
		for (int32 i = EmojiStart; i < Dest->Emojis.Num(); i++)
		{
			Dest->Emojis[i].Position.X += XOffset;
		}
		for (float& CaretX : LineCaretX)
		{
			CaretX += XOffset;
		}
		for (FPlaced& P : LinePlaced)
		{
			P.X0 += XOffset;
		}
	}

	void FLayoutRun::ItemInkExtent(const FDreamTextGlyphItem& Item, float& OutLeft, float& OutRight) const
	{
		const float Left = Item.Pen.X + Item.Glyph.XOffset;
		const float Right = Left + Item.Glyph.Width;
		OutLeft = Left;
		OutRight = Right;
		if (Item.Style.bSyntheticItalic)
		{
			// The top edge shears right by YOffset * slope, the bottom edge left by its depth below the pen.
			const float TopShift = Item.Glyph.YOffset * ItalicSlope;
			const float BottomShift = -(Item.Glyph.Height - Item.Glyph.YOffset) * ItalicSlope;
			OutLeft = Left + FMath::Min(TopShift, BottomShift);
			OutRight = Right + FMath::Max(TopShift, BottomShift);
		}
	}

	FLayoutRun::FEllipsisGlyph FLayoutRun::MakeEllipsisGlyph(const FMeasured& StyleElement)
	{
		const uint32 CharCodeOfDots = 0x2026;//'…'
		const float Size = StyleElement.Style.Size;
		FEllipsisGlyph Dots;
		bool bShaped = false;
		bool bAnyFaceHasDots = false;
		const int32 FaceCount = Font->GetFaceCount();
		for (int32 F = 0; F < FaceCount && !bAnyFaceHasDots; F++)
		{
			bAnyFaceHasDots = Font->FaceHasCodepoint(F, CharCodeOfDots);
		}
		if (bCanShape && bAnyFaceHasDots)
		{
			// Shaped like any character of the run it ends, so it comes from the face that run is drawn from -- a bold
			// face for a bold run, the fallback its language prefers -- at the run's size, in its language.
			TArray<FDreamShapeElement> DotsElements;
			FDreamShapeElement& DotsElement = DotsElements.AddDefaulted_GetRef();
			DotsElement.Codepoint = CharCodeOfDots;
			DotsElement.Size = Size;
			DotsElement.bBold = StyleElement.Style.Bold;
			DotsElement.bItalic = StyleElement.Style.Italic;
			DotsElement.LanguageIndex = StyleElement.LanguageIndex;
			FDreamShapeParams Params;
			Params.Font = Font;
			Params.FlowDirection = EDreamTextFlowDirection::LeftToRight;
			Params.Languages = &Languages;
			TArray<FDreamShapedRun> DotsRuns;
			bool bDotsRightToLeft = false;
			bool bDotsShaped = false;
			{
				FStageTimer ShapeTimer(EDreamTextLayoutStage::Shape);
				bDotsShaped = FDreamTextShaper::ShapeParagraph(DotsElements, Params, DotsRuns, bDotsRightToLeft);
			}
			if (bDotsShaped && DotsRuns.Num() > 0 && DotsRuns[0].Glyphs.Num() > 0)
			{
				const FDreamShapedRun& DotsRun = DotsRuns[0];
				Dots.GlyphSize = Size * DotsRun.FaceScale;
				Dots.Glyph = FetchGlyphQuad(DotsRun.Glyphs[0].FaceIndex, DotsRun.Glyphs[0].GlyphIndex, Dots.GlyphSize, DotsRun.bSyntheticBold, DotsRun.bColorFace);
				float Advance = 0.0f;
				for (const FDreamShapedGlyph& Shaped : DotsRun.Glyphs)
				{
					Advance += Shaped.XAdvance;
				}
				Dots.Glyph.XAdvance = Advance;
				Dots.bSyntheticBold = DotsRun.bSyntheticBold;
				Dots.bSyntheticItalic = StyleElement.Style.Italic
					&& !EnumHasAnyFlags(FaceStyleOf(DotsRun.Glyphs[0].FaceIndex), EDreamUIFontFaceStyle::Italic);
				bShaped = true;
			}
		}
		if (!bShaped)
		{
			// Without shaping the glyph comes from the face the text's style and language resolve it to, as every glyph of
			// that path does. The ellipsis replaces whatever was there, so it has no left neighbour to kern against.
			const int32 Face = ResolveFace(MakeArrayView(&CharCodeOfDots, 1), StyleElement.Style, StyleElement.LanguageIndex).FaceIndex;
			const bool bColorFace = IsColorFaceOf(Face);
			Dots.bSyntheticBold = StyleElement.Style.Bold && !bColorFace && !EnumHasAnyFlags(FaceStyleOf(Face), EDreamUIFontFaceStyle::Bold);
			Dots.bSyntheticItalic = StyleElement.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(Face), EDreamUIFontFaceStyle::Italic);
			Dots.GlyphSize = Size * FaceScaleOf(Face);
			Dots.Glyph = GetCodepointGlyph(0, CharCodeOfDots, Face, Dots.GlyphSize, Dots.bSyntheticBold, bColorFace);
		}
		Dots.bPending = Dots.Glyph.bPending;
		return Dots;
	}

	void FLayoutRun::AddEllipsisItem(int32 LineIndex, int32 StyleElement, const FEllipsisGlyph& Dots, float DotsLeft, bool bBaseRightToLeft, float Baseline)
	{
		const FMeasured& StyleMeasured = Measured[StyleElement];
		const float DotsSpacing = In.FontSpace.X;
		FDreamTextGlyphItem Item;
		Item.Kind = EDreamTextItemKind::Glyph;
		Item.Codepoint = 0x2026;
		Item.ElementIndex = StyleElement;
		Item.SourceIndex = TextProcessingArray.IsValidIndex(StyleElement) ? TextProcessingArray[StyleElement].StringIndex : 0;
		Item.LineIndex = LineIndex;
		Item.Pen = FVector2f(DotsLeft, Baseline + StyleMeasured.Style.BaselineShift);
		Item.Glyph = Dots.Glyph;
		Item.GlyphSize = Dots.GlyphSize;
		Item.AdvanceWithSpace = Dots.Glyph.XAdvance + DotsSpacing;
		Item.DecorationOffset = bBaseRightToLeft ? -DotsSpacing : 0.0f;
		Item.Style = MakeStyle(StyleMeasured);
		// What has to be made up depends on the face the ellipsis itself came from, not on the text's.
		Item.Style.bSyntheticBold = Dots.bSyntheticBold;
		Item.Style.bSyntheticItalic = Dots.bSyntheticItalic;
		SetItemMetrics(Item, Dots.GlyphSize, Dots.Glyph.FaceIndex, 0.0f);
		AssignDecorations(Item, StyleMeasured, 0.0f);
		// Laid out either way; drawn once its glyph has landed, when the font lays the text out again.
		Item.bEmit = !Dots.bPending;
		Item.bCountsAsVisible = false;
		if (Dots.bPending)
		{
			Dest->bHasPendingGlyphs = true;
		}
		LineDotsItem = Dest->Items.Add(Item);
	}

	void FLayoutRun::CutCluster(FPlaced& Placed, TArray<int32>& InOutImagesToRemove, TArray<int32>& InOutEmojisToRemove)
	{
		Placed.bCut = true;
		for (int32 i = Placed.ItemStart; i < Placed.ItemEnd; i++)
		{
			FDreamTextGlyphItem& Item = Dest->Items[i];
			Item.bEmit = false;
			Item.bCountsAsVisible = false;
			Item.Style.bUnderline = false;
			Item.Style.bStrikethrough = false;
			// Not on screen, so no part of a paint run's box.
			if (bAnyPaint && Dest == &Out)
			{
				ItemsOutsideRunBox.PadToNum(i + 1, false);
				ItemsOutsideRunBox[i] = true;
			}
		}
		// What was cut is not on screen, and neither is an image or emoji standing in it.
		if (Placed.ImageIndex != INDEX_NONE)
		{
			InOutImagesToRemove.Add(Placed.ImageIndex);
			Placed.ImageIndex = INDEX_NONE;
		}
		if (Placed.EmojiIndex != INDEX_NONE)
		{
			InOutEmojisToRemove.Add(Placed.EmojiIndex);
			Placed.EmojiIndex = INDEX_NONE;
		}
	}

	void FLayoutRun::RemoveInlineObjects(TArray<int32>& ImagesToRemove, TArray<int32>& EmojisToRemove)
	{
		// Highest index first, so the ones still to go keep theirs.
		ImagesToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (const int32 Index : ImagesToRemove)
		{
			if (Dest->Images.IsValidIndex(Index))
			{
				Dest->Images.RemoveAt(Index);
			}
		}
		EmojisToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (const int32 Index : EmojisToRemove)
		{
			if (Dest->Emojis.IsValidIndex(Index))
			{
				Dest->Emojis.RemoveAt(Index);
			}
		}
	}

	void FLayoutRun::ClampLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline, bool bForceEllipsis,
		int32 LineItemStart, int32 ImageStart, int32 EmojiStart)
	{
		if (!IsClampMode() || bHasClampContent)return;
		const bool bMiddleEllipsis = In.OverflowType == EDreamUITextOverflowType::MiddleEllipsis;
		if (bMiddleEllipsis && !ShouldWrap())
		{
			MiddleEllipsizeLine(LineIndex, bBaseRightToLeft, PenEnd, Baseline);
			return;
		}
		const int32 Count = LinePlaced.Num();
		// How far a cluster's glyph reaches from the line's start, in the line's own direction: what has to fit the box.
		auto Extent = [bBaseRightToLeft, PenEnd](const FPlaced& P)
		{
			return bBaseRightToLeft ? PenEnd - P.GlyphLeft() : P.GlyphLeft() + P.Advance;
		};
		auto ReadingIndex = [bBaseRightToLeft, Count](int32 k)
		{
			return bBaseRightToLeft ? Count - 1 - k : k;
		};
		// The first cluster in reading order that reaches past Limit, and everything after it, is what goes: Count when
		// everything fits. Whitespace never decides it, it hangs.
		auto FindCut = [this, &Extent, &ReadingIndex, Count](float Limit)
		{
			for (int32 k = 0; k < Count; k++)
			{
				const FPlaced& P = LinePlaced[ReadingIndex(k)];
				if (!P.bWhitespace && Extent(P) > Limit + KINDA_SMALL_NUMBER)
				{
					return k;
				}
			}
			return Count;
		};
		int32 Cut = FindCut(In.Width);
		if (Cut == Count && !bForceEllipsis)return;
		bHasClampContent = true;
		Dest->bTruncated = true;
		bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after the line, so we mark it and read it later
		// A wrapped middle ellipsis ends the last line that fits, as an end ellipsis does: Slate's has no multi-line form either.
		const bool bEllipsis = In.OverflowType == EDreamUITextOverflowType::Ellipsis || bMiddleEllipsis;
		if (!bEllipsis && Cut == Count)return;//Truncate on the last line that fits: nothing of the line itself goes

		// The ellipsis takes the style of the text it ends: the last cluster kept in reading order, else the first one.
		int32 StyleElement = INDEX_NONE;
		for (int32 k = Cut - 1; k >= 0 && StyleElement == INDEX_NONE; k--)
		{
			const FPlaced& P = LinePlaced[ReadingIndex(k)];
			if (!P.bWhitespace)
			{
				StyleElement = P.Start;
			}
		}
		if (StyleElement == INDEX_NONE && Count > 0)
		{
			StyleElement = LinePlaced[ReadingIndex(0)].Start;
		}
		if (StyleElement == INDEX_NONE && Measured.Num() > 0)
		{
			StyleElement = FMath::Clamp(LineRanges[LineIndex].Start, 0, Measured.Num() - 1);
		}

		FEllipsisGlyph Dots;
		float DotsAdvance = 0.0f;
		bool bDots = false;
		if (bEllipsis && Measured.IsValidIndex(StyleElement))
		{
			Dots = MakeEllipsisGlyph(Measured[StyleElement]);
			DotsAdvance = Dots.Glyph.XAdvance;
			// Not even the ellipsis fits: the line draws nothing, which is all the box can hold.
			bDots = DotsAdvance <= In.Width + KINDA_SMALL_NUMBER;
			Cut = bDots ? FindCut(In.Width - DotsAdvance) : 0;
		}
		TArray<int32> ImagesToRemove;
		TArray<int32> EmojisToRemove;
		for (int32 k = Cut; k < Count; k++)
		{
			CutCluster(LinePlaced[ReadingIndex(k)], ImagesToRemove, EmojisToRemove);
		}
		RemoveInlineObjects(ImagesToRemove, EmojisToRemove);
		// Spaces the cut leaves at the end of what is kept hang there like the spaces at a line's end: an underline stops
		// at the last character kept rather than running on under nothing, or under the ellipsis a second time.
		for (int32 k = Cut - 1; k >= 0; k--)
		{
			const FPlaced& P = LinePlaced[ReadingIndex(k)];
			if (!P.bWhitespace)break;
			for (int32 ItemIndex = P.ItemStart; ItemIndex < P.ItemEnd; ItemIndex++)
			{
				Dest->Items[ItemIndex].Style.bUnderline = false;
				Dest->Items[ItemIndex].Style.bStrikethrough = false;
			}
		}

		// Where the kept text ends, in reading order: its right edge left to right, its left edge right to left.
		float KeptEnd = bBaseRightToLeft ? PenEnd : 0.0f;
		for (int32 k = 0; k < Cut; k++)
		{
			const FPlaced& P = LinePlaced[ReadingIndex(k)];
			if (P.bWhitespace)continue;
			KeptEnd = bBaseRightToLeft ? FMath::Min(KeptEnd, P.X0) : FMath::Max(KeptEnd, P.BoxRight());
		}
		float Shift = 0.0f;
		if (bDots)
		{
			const float DotsSpacing = In.FontSpace.X;
			const float DotsLeft = bBaseRightToLeft ? KeptEnd - DotsAdvance : KeptEnd;
			AddEllipsisItem(LineIndex, StyleElement, Dots, DotsLeft, bBaseRightToLeft, Baseline);
			if (bBaseRightToLeft)
			{
				// The ellipsis's own box, letter spacing included, starts the line.
				Shift = -(DotsLeft - DotsSpacing);
			}
		}
		else if (bBaseRightToLeft)
		{
			Shift = -KeptEnd;//what survived slides back to the line's origin
		}
		ShiftPlacedLine(LineItemStart, ImageStart, EmojiStart, Shift);
	}

	void FLayoutRun::MiddleEllipsizeLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline)
	{
		const int32 Count = LinePlaced.Num();
		auto ReadingIndex = [bBaseRightToLeft, Count](int32 k)
		{
			return bBaseRightToLeft ? Count - 1 - k : k;
		};
		// Whether the line fits is decided as the end ellipsis decides it: by how far each glyph reaches, whitespace hanging.
		bool bOverflows = false;
		for (const FPlaced& P : LinePlaced)
		{
			const float Extent = bBaseRightToLeft ? PenEnd - P.GlyphLeft() : P.GlyphLeft() + P.Advance;
			if (!P.bWhitespace && Extent > In.Width + KINDA_SMALL_NUMBER)
			{
				bOverflows = true;
				break;
			}
		}
		if (!bOverflows)return;
		Dest->bTruncated = true;

		// What the line holds, in reading order and by width, whitespace hanging off its end left out.
		int32 ContentCount = Count;
		while (ContentCount > 0 && LinePlaced[ReadingIndex(ContentCount - 1)].bTrailing)
		{
			ContentCount--;
		}
		if (ContentCount == 0)return;
		TArray<float, TInlineAllocator<64>> Widths;
		Widths.SetNumUninitialized(ContentCount);
		float Total = 0.0f;
		for (int32 k = 0; k < ContentCount; k++)
		{
			const FPlaced& P = LinePlaced[ReadingIndex(k)];
			Widths[k] = P.Advance + P.Spacing;
			Total += Widths[k];
		}
		// The gap opens on the cluster the line's middle falls in: [GapStart, GapEnd) in reading order.
		int32 GapStart = 0;
		float HeadWidth = 0.0f;
		while (GapStart + 1 < ContentCount && HeadWidth + Widths[GapStart] <= Total * 0.5f)
		{
			HeadWidth += Widths[GapStart];
			GapStart++;
		}
		int32 GapEnd = GapStart + 1;
		float TailWidth = Total - HeadWidth - Widths[GapStart];

		// The ellipsis is set like the last cluster kept before it -- with nothing kept before it, like the first kept after
		// it -- and is made again only when that style changes as the gap grows.
		auto StyleElementFor = [this, &ReadingIndex, ContentCount](int32 InGapStart, int32 InGapEnd)
		{
			for (int32 k = InGapStart - 1; k >= 0; k--)
			{
				const FPlaced& P = LinePlaced[ReadingIndex(k)];
				if (!P.bWhitespace)return P.Start;
			}
			for (int32 k = InGapEnd; k < ContentCount; k++)
			{
				const FPlaced& P = LinePlaced[ReadingIndex(k)];
				if (!P.bWhitespace)return P.Start;
			}
			return LinePlaced[ReadingIndex(0)].Start;
		};
		auto SameEllipsis = [this](int32 A, int32 B)
		{
			const FRichTextParseResult& StyleA = Measured[A].Style;
			const FRichTextParseResult& StyleB = Measured[B].Style;
			return StyleA.Size == StyleB.Size && StyleA.Bold == StyleB.Bold && StyleA.Italic == StyleB.Italic
				&& Measured[A].LanguageIndex == Measured[B].LanguageIndex;
		};
		const float DotsSpacing = In.FontSpace.X;
		int32 StyleElement = INDEX_NONE;
		FEllipsisGlyph Dots;
		float DotsWidth = 0.0f;
		while (true)
		{
			const int32 WantedStyle = StyleElementFor(GapStart, GapEnd);
			if (StyleElement == INDEX_NONE || !SameEllipsis(StyleElement, WantedStyle))
			{
				Dots = MakeEllipsisGlyph(Measured[WantedStyle]);
				DotsWidth = Dots.Glyph.XAdvance + DotsSpacing;
			}
			StyleElement = WantedStyle;
			if (HeadWidth + DotsWidth + TailWidth <= In.Width + KINDA_SMALL_NUMBER || (GapStart == 0 && GapEnd >= ContentCount))
			{
				break;
			}
			// One more cluster into the gap, from whichever side leaves the head and the tail closest in width.
			bool bFromHead = false;
			if (GapEnd >= ContentCount)
			{
				bFromHead = true;
			}
			else if (GapStart > 0)
			{
				const float IfHead = FMath::Abs((HeadWidth - Widths[GapStart - 1]) - TailWidth);
				const float IfTail = FMath::Abs(HeadWidth - (TailWidth - Widths[GapEnd]));
				bFromHead = IfHead < IfTail;
			}
			if (bFromHead)
			{
				GapStart--;
				HeadWidth -= Widths[GapStart];
			}
			else
			{
				TailWidth -= Widths[GapEnd];
				GapEnd++;
			}
		}
		// Whitespace touching the gap goes with it: the ellipsis stands against the text on either side.
		while (GapStart > 0 && LinePlaced[ReadingIndex(GapStart - 1)].bWhitespace)
		{
			GapStart--;
		}
		while (GapEnd < ContentCount && LinePlaced[ReadingIndex(GapEnd)].bWhitespace)
		{
			GapEnd++;
		}
		// Not even the ellipsis fits: the line draws nothing, which is all the box can hold.
		const bool bDots = Dots.Glyph.XAdvance <= In.Width + KINDA_SMALL_NUMBER;
		if (!bDots)
		{
			GapStart = 0;
			GapEnd = ContentCount;
		}

		// The ellipsis stands where the head ends, in the line's own direction; its box, letter spacing included, ends where
		// the tail now begins.
		const FPlaced& GapFirst = LinePlaced[ReadingIndex(GapStart)];
		const float HeadEnd = bBaseRightToLeft ? GapFirst.BoxRight() : GapFirst.X0;
		const float DotsAdvance = bDots ? Dots.Glyph.XAdvance : 0.0f;
		const float DotsBox = bDots ? DotsAdvance + DotsSpacing : 0.0f;
		const float DotsLeft = bBaseRightToLeft ? HeadEnd - DotsAdvance : HeadEnd;
		const float DotsFarEdge = bBaseRightToLeft ? HeadEnd - DotsBox : HeadEnd + DotsBox;
		const int32 RangeStart = LineRanges[LineIndex].Start;

		// The tail slides back against the ellipsis -- its items, its inline objects, its carets -- before anything in the
		// gap is removed, while the tail's inline objects still have their indices.
		if (GapEnd < Count)
		{
			const FPlaced& TailFirst = LinePlaced[ReadingIndex(GapEnd)];
			const float TailShift = bBaseRightToLeft ? DotsFarEdge - TailFirst.BoxRight() : DotsFarEdge - TailFirst.X0;
			for (int32 k = GapEnd; k < Count && TailShift != 0.0f; k++)
			{
				FPlaced& P = LinePlaced[ReadingIndex(k)];
				for (int32 ItemIndex = P.ItemStart; ItemIndex < P.ItemEnd; ItemIndex++)
				{
					Dest->Items[ItemIndex].Pen.X += TailShift;
				}
				if (Dest->Images.IsValidIndex(P.ImageIndex))
				{
					Dest->Images[P.ImageIndex].Position.X += TailShift;
				}
				if (Dest->Emojis.IsValidIndex(P.EmojiIndex))
				{
					Dest->Emojis[P.EmojiIndex].Position.X += TailShift;
				}
				for (int32 e = P.Start; e < P.End; e++)
				{
					LineCaretX[e - RangeStart] += TailShift;
				}
				P.X0 += TailShift;
			}
		}

		// The gap is not on screen: its glyphs are neither drawn nor counted, its inline objects go, and a caret inside it
		// stands on the ellipsis's leading edge. The line's end caret, when the gap took the cluster it follows, stands on
		// the ellipsis's far edge.
		int32 LastElement = INDEX_NONE;
		for (int32 i = LineRanges[LineIndex].End - 1; i >= RangeStart; i--)
		{
			if (!Measured[i].bSkipped)
			{
				LastElement = i;
				break;
			}
		}
		TArray<int32> ImagesToRemove;
		TArray<int32> EmojisToRemove;
		for (int32 k = GapStart; k < GapEnd; k++)
		{
			FPlaced& P = LinePlaced[ReadingIndex(k)];
			for (int32 e = P.Start; e < P.End; e++)
			{
				LineCaretX[e - RangeStart] = HeadEnd;
			}
			if (LastElement >= P.Start && LastElement < P.End)
			{
				LineEndCaretX = DotsFarEdge;
			}
			CutCluster(P, ImagesToRemove, EmojisToRemove);
		}
		RemoveInlineObjects(ImagesToRemove, EmojisToRemove);
		if (bDots)
		{
			AddEllipsisItem(LineIndex, StyleElement, Dots, DotsLeft, bBaseRightToLeft, Baseline);
		}
	}

	void FLayoutRun::PlaceCluster(int32 Start, int32 End, bool bRightToLeft, uint8 Level, bool bTrailing, int32 LineIndex, int32 RangeStart,
		float& InOutPenX, float Baseline, const FLineBox& Box, float LineCentre)
	{
		const FMeasured& Lead = Measured[Start];
		FPlaced Placed;
		Placed.Start = Start;
		Placed.End = End;
		Placed.ItemStart = Dest->Items.Num();
		Placed.X0 = InOutPenX;
		Placed.Level = Level;
		Placed.bRightToLeft = bRightToLeft;
		Placed.bWhitespace = Lead.bWhitespace;
		Placed.bImage = Lead.bImageSpace;
		Placed.bTrailing = bTrailing;
		for (int32 e = Start; e < End; e++)
		{
			Placed.Advance += Measured[e].ClusterAdvance;
		}
		Placed.Spacing = Lead.bClusterStart ? Lead.LetterSpacing : 0.0f;
		// Justification widens the same slot letter spacing fills, so strokes, carets and runs follow it as they do spacing.
		if (LineJustify.IsValidIndex(Start - RangeStart))
		{
			Placed.Spacing += LineJustify[Start - RangeStart];
		}
		const float GlyphLeft = Placed.GlyphLeft();
		const float ItemBaseline = Baseline + Lead.Style.BaselineShift;

		auto MakeItem = [this, LineIndex](int32 ElementIndex, EDreamTextItemKind Kind)
		{
			FDreamTextGlyphItem Item;
			Item.Kind = Kind;
			Item.Codepoint = TextProcessingArray[ElementIndex].Unicode;
			Item.ElementIndex = ElementIndex;
			Item.SourceIndex = TextProcessingArray[ElementIndex].StringIndex;
			Item.LineIndex = LineIndex;
			Item.Style = MakeStyle(Measured[ElementIndex]);
			return Item;
		};

		if (Lead.bImageSpace || Lead.bEmoji)
		{
			FDreamTextGlyphItem Item = MakeItem(Start, Lead.bImageSpace ? EDreamTextItemKind::Image : EDreamTextItemKind::Emoji);
			Item.Pen = FVector2f(GlyphLeft, Baseline);
			Item.Glyph = Lead.Glyph;
			Item.AdvanceWithSpace = Placed.Advance + Placed.Spacing;
			Item.DecorationOffset = bRightToLeft ? -Placed.Spacing : 0.0f;
			// An inline object's box is its style's size on the primary face, as a letter in its place would have.
			SetItemMetrics(Item, Lead.Style.Size, 0, 0.0f);
			const float CentreX = GlyphLeft + Placed.Advance * 0.5f;
			const float ObjectHeight = Lead.Glyph.Height;
			if (Lead.bImageSpace)
			{
				// A decoration does not run through an atomic inline (CSS): an underline stops at an image.
				Item.Style.bUnderline = false;
				Item.Style.bStrikethrough = false;
				float CentreY = LineCentre;
				switch (Lead.Style.ImageVerticalAlign)
				{
				case EImageVerticalAlign::Baseline:
					CentreY = ItemBaseline + ObjectHeight * 0.5f;
					break;
				case EImageVerticalAlign::Top:
					CentreY = Baseline + Box.Top - ObjectHeight * 0.5f;
					break;
				case EImageVerticalAlign::Bottom:
					CentreY = Baseline - Box.Bottom + ObjectHeight * 0.5f;
					break;
				default:
					break;
				}
				FDreamUIText_RichTextImageTag ImageTagData;
				ImageTagData.TagName = Lead.Style.ImageTag;
				ImageTagData.Position = FVector2D(CentreX, CentreY);
				ImageTagData.Size = FVector2D(Lead.Glyph.Width, ObjectHeight);
				ImageTagData.TintColor = Lead.Style.HasColor ? Lead.Style.Color : FColor::White;
				Placed.ImageIndex = Dest->Images.Add(ImageTagData);
			}
			else
			{
				AssignDecorations(Item, Lead, 0.0f);
				FDreamUIText_Emoji Emoji;
				Emoji.EmojiCode = TextProcessingArray[Start].Unicode;
				// What the emoji data's picture is looked up by again when the object is made: the whole sequence first.
				Emoji.Sequence = ElementText(Start);
				Emoji.Position = FVector2D(CentreX, LineCentre);
				Emoji.Size = FVector2D(Lead.Glyph.Width, ObjectHeight);
				Placed.EmojiIndex = Dest->Emojis.Add(Emoji);
			}
			Dest->Items.Add(Item);
		}
		else
		{
			// A cluster led by a space or a tab is a Space item, which draws nothing; a combining mark that sits on the
			// space is still a glyph of its own. The space's own glyph is the lead element's first glyph that advances
			// (a mark has no advance), or its first glyph when none does.
			int32 SpaceGlyph = INDEX_NONE;
			if (Lead.bWhitespace)
			{
				FDreamTextGlyphItem Item = MakeItem(Start, EDreamTextItemKind::Space);
				Item.Pen = FVector2f(GlyphLeft, ItemBaseline);
				Item.Glyph = Lead.Glyph;
				Item.AdvanceWithSpace = Placed.Advance + Placed.Spacing;
				Item.DecorationOffset = bRightToLeft ? -Placed.Spacing : 0.0f;
				SetItemMetrics(Item, Lead.Style.Size, 0, 0.0f);
				AssignDecorations(Item, Lead, 0.0f);
				Dest->Items.Add(Item);
				if (Lead.GlyphCount > 0)
				{
					SpaceGlyph = Lead.GlyphStart;
					for (int32 g = Lead.GlyphStart; g < Lead.GlyphStart + Lead.GlyphCount; g++)
					{
						if (Glyphs[g].XAdvance != 0.0f)
						{
							SpaceGlyph = g;
							break;
						}
					}
				}
			}
			// The cluster's glyphs in visual order. An element's glyphs were appended in the shaper's visual order, so
			// ordering the elements by where their glyphs start is visual order in either direction.
			TArray<int32, TInlineAllocator<8>> GlyphElements;
			int32 TotalGlyphs = 0;
			for (int32 e = Start; e < End; e++)
			{
				if (Measured[e].GlyphCount > 0)
				{
					GlyphElements.Add(e);
					TotalGlyphs += Measured[e].GlyphCount;
				}
			}
			GlyphElements.Sort([this](int32 A, int32 B) { return Measured[A].GlyphStart < Measured[B].GlyphStart; });
			float GlyphPenX = GlyphLeft;
			int32 GlyphOrdinal = 0;
			for (const int32 e : GlyphElements)
			{
				const FMeasured& M = Measured[e];
				for (int32 g = 0; g < M.GlyphCount; g++, GlyphOrdinal++)
				{
					const FGlyphSource& G = Glyphs[M.GlyphStart + g];
					if (M.GlyphStart + g == SpaceGlyph)
					{
						GlyphPenX += G.XAdvance;
						continue;
					}
					FDreamTextGlyphItem Item = MakeItem(e, EDreamTextItemKind::Glyph);
					Item.Pen = FVector2f(GlyphPenX + G.XOffset, ItemBaseline + G.YOffset);
					Item.Glyph = G.Quad;
					Item.GlyphSize = G.GlyphSize;
					SetItemMetrics(Item, G.GlyphSize, M.FaceIndex, G.YOffset);
					// The cluster's letter spacing rides on its last glyph in reading order -- the right-most left to
					// right, the left-most right to left -- so the glyphs' stretches together cover its whole pen box.
					const bool bCarriesSpacing = bRightToLeft ? GlyphOrdinal == 0 : GlyphOrdinal == TotalGlyphs - 1;
					Item.AdvanceWithSpace = G.XAdvance + (bCarriesSpacing ? Placed.Spacing : 0.0f);
					Item.DecorationOffset = -G.XOffset - ((bCarriesSpacing && bRightToLeft) ? Placed.Spacing : 0.0f);
					if (G.Quad.bPending)
					{
						Dest->bHasPendingGlyphs = true;
					}
					// Counting and emitting are separate: a glyph still on the rasterizer's worker occupies its place
					// in the visible-character numbering even though it has no quad yet, so a tag range or a
					// TextAnimation index does not shift the moment OnGlyphsReady lands.
					Item.bCountsAsVisible = true;
					Item.bEmit = !G.Quad.bPending;
					AssignDecorations(Item, M, G.YOffset);
					Dest->Items.Add(Item);
					GlyphPenX += G.XAdvance;
				}
			}
		}

		// A caret at the leading edge of every grapheme cluster: the left of one read left to right, the right of one
		// read right to left. Inside a ligature the clusters share the glyph's advance evenly, as browsers and Slate
		// split it.
		int32 StopCount = 0;
		for (int32 e = Start; e < End; e++)
		{
			if (e == Start || Measured[e].bCaretStop)StopCount++;
		}
		int32 StopIndex = 0;
		for (int32 e = Start; e < End; e++)
		{
			if (e != Start && !Measured[e].bCaretStop)continue;
			const float Fraction = (float)StopIndex / (float)StopCount;
			LineCaretX[e - RangeStart] = bRightToLeft ? GlyphLeft + Placed.Advance * (1.0f - Fraction) : GlyphLeft + Placed.Advance * Fraction;
			LineHasCaret[e - RangeStart] = true;
			StopIndex++;
		}

		InOutPenX += Placed.Advance + Placed.Spacing;
		Placed.ItemEnd = Dest->Items.Num();
		LinePlaced.Add(Placed);
	}

	FLayoutRun::FLineBox FLayoutRun::ComputeLineBox(int32 LineIndex)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		FLineBox Box;
		// Every box on the line reaches its ascent and half its leading above its baseline, its descent and the other
		// half below (CSS half-leading), raised or lowered by its baseline shift; the line box is their union. The
		// line-height scale stretches each box's height, so the extra space goes half above and half below rather than
		// all under the line, and a single line in a tall box sits in its middle.
		auto AddBox = [&Box, this](const FSizeMetrics& Metrics, float Shift)
		{
			const float HalfLeading = (Metrics.LineHeight * LineHeightScale - Metrics.Ascent - Metrics.Descent) * 0.5f;
			Box.Top = FMath::Max(Box.Top, Metrics.Ascent + HalfLeading + Shift);
			Box.Bottom = FMath::Max(Box.Bottom, Metrics.Descent + HalfLeading - Shift);
		};
		// The strut is the primary face at the text's own size; anything on the line with a taller box grows it -- a
		// rich-text <size>, a superscript, or a glyph that a fallback face supplied (CJK under a Latin primary).
		{
			const FSizeMetrics& Strut = MetricsFor(FontSize);
			const float HalfLeading = (Strut.LineHeight * LineHeightScale - Strut.Ascent - Strut.Descent) * 0.5f;
			Box.Top = Strut.Ascent + HalfLeading;
			Box.Bottom = Strut.Descent + HalfLeading;
		}
		TArray<int32, TInlineAllocator<8>> LineRelative;
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			const float Shift = M.Style.BaselineShift;
			if (M.bImageSpace && M.Style.ImageVerticalAlign == EImageVerticalAlign::Baseline)
			{
				// Bottom edge on the (shifted) baseline, like a letter.
				Box.Top = FMath::Max(Box.Top, M.Glyph.Height + Shift);
				Box.Bottom = FMath::Max(Box.Bottom, -Shift);
				continue;
			}
			if (M.bImageSpace || M.bEmoji)
			{
				LineRelative.Add(i);
				continue;
			}
			if (!In.bRichText && M.FaceIndex == 0)continue;//the strut is this box already
			// A scaled face's box is its size times its scale: a face drawn larger grows its line, as CSS size-adjust does.
			AddBox(MetricsFor(M.Style.Size * FaceScaleOf(M.FaceIndex), M.FaceIndex), Shift);
		}
		// Objects placed against the line rather than the baseline -- centred on it, on its top or its bottom edge --
		// once the text has set it: one taller than the line grows it, so it never overlaps the lines around it.
		for (const int32 i : LineRelative)
		{
			const FMeasured& M = Measured[i];
			const float ObjectHeight = M.Glyph.Height;
			const float Deficit = ObjectHeight - (Box.Top + Box.Bottom);
			if (Deficit <= 0.0f)continue;
			const EImageVerticalAlign Align = M.bEmoji ? EImageVerticalAlign::Middle : M.Style.ImageVerticalAlign;
			if (Align == EImageVerticalAlign::Top)
			{
				Box.Bottom += Deficit;
			}
			else if (Align == EImageVerticalAlign::Bottom)
			{
				Box.Top += Deficit;
			}
			else
			{
				Box.Top += Deficit * 0.5f;
				Box.Bottom += Deficit * 0.5f;
			}
		}
		return Box;
	}

	bool FLayoutRun::IsLineBaseRightToLeft(const FLineRange& Range) const
	{
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			if (!Measured[i].bSkipped)
			{
				return Measured[i].bBaseRightToLeft;
			}
		}
		// An empty line has no character to ask; a forced direction still holds for it.
		return bCanShape && In.FlowDirection == EDreamTextFlowDirection::RightToLeft;
	}

	bool FLayoutRun::IsJustifyOpportunity(int32 BeforeElement, int32 AfterElement) const
	{
		const FMeasured& Before = Measured[BeforeElement];
		const FMeasured& After = Measured[AfterElement];
		const uint32 BeforeCode = TextProcessingArray[BeforeElement].Unicode;
		const uint32 AfterCode = TextProcessingArray[AfterElement].Unicode;
		// Every mode widens after a word separator: a space, a no-break space, a tab (as Blink treats them).
		if (Before.bWhitespace || BeforeCode == 0x00A0)
		{
			return true;
		}
		if (In.TextJustify == EDreamTextJustify::InterWord)
		{
			return false;
		}
		// Auto and InterCharacter: on either side of an ideograph, kana or fullwidth symbol.
		if (IsJustifiedLikeCJK(BeforeCode) || IsJustifiedLikeCJK(AfterCode))
		{
			return true;
		}
		if (In.TextJustify != EDreamTextJustify::InterCharacter)
		{
			return false;
		}
		// InterCharacter: every other boundary, but never inside a cursive word -- room there tears the joined letters
		// apart -- and never beside an inline object.
		if (Before.bImageSpace || Before.bEmoji || After.bImageSpace || After.bEmoji)
		{
			return false;
		}
		return !(IsCursiveScript(BeforeCode) && IsCursiveScript(AfterCode));
	}

	bool FLayoutRun::JustifyLine(int32 LineIndex, int32 TrailingStart)
	{
		if (In.ParagraphHAlign != EDreamUITextParagraphHorizontalAlign::Justify || In.TextJustify == EDreamTextJustify::None)
		{
			return false;
		}
		const FLineRange& Range = LineRanges[LineIndex];
		// A line the text wrapped is justified; the paragraph's last line, and one a newline ends, only when LastLineAlign
		// asks for it. The last line that fits a clamped box ends in an ellipsis instead.
		const bool bEndsParagraph = Range.HardBreakElement != -1 || LineIndex == LineRanges.Num() - 1;
		if ((bEndsParagraph && In.LastLineAlign != EDreamTextLastLineAlign::Justify) || bEllipsizeThisLine)
		{
			return false;
		}
		const float Target = ShouldWrap() ? FMath::Min(WrapWidth, In.Width) : In.Width;
		// The line's natural width -- its clusters' pen boxes, the whitespace hanging off its end left out -- and the
		// boundaries between its clusters where it may widen, in logical order. The line's end is never one.
		float Natural = 0.0f;
		TArray<int32, TInlineAllocator<64>> Opportunities;
		int32 PreviousLead = INDEX_NONE;
		for (int32 i = Range.Start; i < TrailingStart; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			Natural += M.Advance;
			if (!M.bClusterStart)continue;
			if (PreviousLead != INDEX_NONE && IsJustifyOpportunity(PreviousLead, i))
			{
				Opportunities.Add(PreviousLead);
			}
			PreviousLead = i;
		}
		// A line too wide already is not shrunk, and a line with nowhere to widen starts at its start edge.
		const float Room = Target - Natural;
		if (Opportunities.Num() == 0 || Room <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		// The same room at every opportunity, after the cluster before it: no kashida stretches an Arabic word instead.
		const float Extra = Room / (float)Opportunities.Num();
		for (const int32 Lead : Opportunities)
		{
			LineJustify[Lead - Range.Start] += Extra;
		}
		return true;
	}

	void FLayoutRun::PlaceLine(int32 LineIndex)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		const int32 LineItemStart = Dest->Items.Num();
		const int32 ImageStart = Dest->Images.Num();
		const int32 EmojiStart = Dest->Emojis.Num();
		const int32 VisualRunStart = Dest->VisualRuns.Num();
		const bool bAfterClamp = bHasClampContent;
		FDreamUITextLineProperty LineProperty;

		// The line box: every box on the line sitting on one baseline (ComputeLineBox). Carets and line-relative
		// inline objects anchor on the line's centre, which is the contract they had.
		const FLineBox Box = ComputeLineBox(LineIndex);
		CurrentLineHeight = Box.Top + Box.Bottom;
		// Placed with its own top at 0: Finish moves the line to where it stands, so a line placed alike comes out alike.
		const float Baseline = -Box.Top;
		const float LineCentre = -CurrentLineHeight * 0.5f;

		const bool bBaseRightToLeft = IsLineBaseRightToLeft(Range);
		const uint8 ParagraphLevel = bBaseRightToLeft ? 1 : 0;
		const int32 LineLength = FMath::Max(Range.End - Range.Start, 0);

		// Levels on this line, with UAX #9 rule L1: whitespace at the end of a line goes back to the paragraph's
		// level, so a space that wrapped after a right-to-left word hangs off the line's end, not inside the word.
		TArray<uint8, TInlineAllocator<128>> Levels;
		Levels.SetNumUninitialized(LineLength);
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			Levels[i - Range.Start] = Measured[i].BidiLevel;
		}
		int32 TrailingStart = Range.End;
		while (TrailingStart > Range.Start && (Measured[TrailingStart - 1].bWhitespace || Measured[TrailingStart - 1].bSkipped))
		{
			TrailingStart--;
			Levels[TrailingStart - Range.Start] = ParagraphLevel;
		}

		// Pieces: maximal stretches of one level and one shaped run (or of unshaped elements). A cluster is never
		// split between two.
		struct FPiece
		{
			int32 Start = 0;
			int32 End = 0;
			uint8 Level = 0;
			int32 RunIndex = INDEX_NONE;
		};
		TArray<FPiece, TInlineAllocator<16>> Pieces;
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			const uint8 Level = Levels[i - Range.Start];
			if (Pieces.Num() > 0 && (!M.bClusterStart || (Pieces.Last().Level == Level && Pieces.Last().RunIndex == M.RunIndex)))
			{
				Pieces.Last().End = i + 1;
			}
			else
			{
				FPiece& Piece = Pieces.AddDefaulted_GetRef();
				Piece.Start = i;
				Piece.End = i + 1;
				Piece.Level = Level;
				Piece.RunIndex = M.RunIndex;
			}
		}

		// UAX #9 rule L2: from the highest level on the line down to the lowest odd one, reverse every maximal stretch
		// of pieces at that level or above. A right-to-left phrase that spans several runs -- a number in it, a bold
		// word -- reads in its own order whatever the paragraph's direction; reversing only within one run put its
		// words in logical order inside a left-to-right paragraph, so the second word was read first.
		TArray<int32, TInlineAllocator<16>> Order;
		Order.SetNumUninitialized(Pieces.Num());
		int32 HighestLevel = 0;
		int32 LowestLevel = MAX_int32;
		for (int32 k = 0; k < Pieces.Num(); k++)
		{
			Order[k] = k;
			HighestLevel = FMath::Max(HighestLevel, (int32)Pieces[k].Level);
			LowestLevel = FMath::Min(LowestLevel, (int32)Pieces[k].Level);
		}
		// The lowest odd level, counting levels not present on the line, as ICU's own reordering does.
		const int32 LowestOddLevel = Pieces.Num() > 0 ? (LowestLevel | 1) : MAX_int32;
		for (int32 Level = HighestLevel; Level >= LowestOddLevel; Level--)
		{
			for (int32 k = 0; k < Order.Num();)
			{
				if ((int32)Pieces[Order[k]].Level < Level)
				{
					k++;
					continue;
				}
				int32 StretchEnd = k + 1;
				while (StretchEnd < Order.Num() && (int32)Pieces[Order[StretchEnd]].Level >= Level)
				{
					StretchEnd++;
				}
				Algo::Reverse(Order.GetData() + k, StretchEnd - k);
				k = StretchEnd;
			}
		}

		// Justification decides each cluster's extra room before anything is placed: it rides in the clusters' Spacing.
		LineJustify.Init(0.0f, LineLength);
		LineEndCaretX.Reset();
		const bool bJustified = !bAfterClamp && JustifyLine(LineIndex, TrailingStart);

		// Place the pieces left to right, each one's clusters in its own direction.
		LinePlaced.Reset();
		LineCaretX.Init(0.0f, LineLength);
		LineHasCaret.Init(false, LineLength);
		LineDotsItem = INDEX_NONE;
		float PenX = 0.0f;
		for (const int32 PieceIndex : Order)
		{
			const FPiece& Piece = Pieces[PieceIndex];
			const bool bRightToLeft = (Piece.Level & 1) != 0;
			TArray<FIntPoint, TInlineAllocator<64>> Clusters;
			for (int32 i = Piece.Start; i < Piece.End;)
			{
				int32 j = i + 1;
				while (j < Piece.End && (Measured[j].bSkipped || !Measured[j].bClusterStart))
				{
					j++;
				}
				Clusters.Add(FIntPoint(i, j));
				i = j;
			}
			for (int32 k = 0; k < Clusters.Num(); k++)
			{
				const FIntPoint& Cluster = Clusters[bRightToLeft ? Clusters.Num() - 1 - k : k];
				PlaceCluster(Cluster.X, Cluster.Y, bRightToLeft, Piece.Level, Cluster.X >= TrailingStart, LineIndex, Range.Start,
					PenX, Baseline, Box, LineCentre);
			}
		}
		const float PenEnd = PenX;

		// Spaces hanging off the line's end are not underlined, as in CSS; the ones between words are. Nor are they part of a
		// paint run's box: a gradient runs across what is drawn.
		for (const FPlaced& P : LinePlaced)
		{
			if (!P.bTrailing)continue;
			for (int32 ItemIndex = P.ItemStart; ItemIndex < P.ItemEnd; ItemIndex++)
			{
				Dest->Items[ItemIndex].Style.bUnderline = false;
				Dest->Items[ItemIndex].Style.bStrikethrough = false;
				if (bAnyPaint && Dest == &Out)
				{
					ItemsOutsideRunBox.PadToNum(ItemIndex + 1, false);
					ItemsOutsideRunBox[ItemIndex] = true;
				}
			}
		}

		if (bAfterClamp)
		{
			// An earlier line was cut: nothing after the cut is drawn, as it never was.
			TArray<int32> ImagesToRemove;
			TArray<int32> EmojisToRemove;
			for (FPlaced& P : LinePlaced)
			{
				CutCluster(P, ImagesToRemove, EmojisToRemove);
			}
			RemoveInlineObjects(ImagesToRemove, EmojisToRemove);
		}
		else
		{
			ClampLine(LineIndex, bBaseRightToLeft, PenEnd, Baseline, bEllipsizeThisLine, LineItemStart, ImageStart, EmojiStart);
		}

		// The carets are read off the line as the clamp left it: a middle ellipsis slid the line's end back and stood the
		// carets of what it took on itself. The line's end caret is the trailing edge of its last cluster in logical order
		// -- the right of one read left to right, the left of one read right to left.
		float EndCaretX = 0.0f;
		if (LineEndCaretX.IsSet())
		{
			EndCaretX = LineEndCaretX.GetValue();
		}
		else
		{
			int32 LastElement = INDEX_NONE;
			for (int32 i = Range.End - 1; i >= Range.Start; i--)
			{
				if (!Measured[i].bSkipped)
				{
					LastElement = i;
					break;
				}
			}
			for (const FPlaced& P : LinePlaced)
			{
				if (LastElement >= P.Start && LastElement < P.End)
				{
					EndCaretX = P.bRightToLeft ? P.X0 : P.BoxRight();
					break;
				}
			}
		}
		// Carets in logical order, one per grapheme cluster, then the end caret: the newline's own for a hard break,
		// the string's end for the last line, nameless for a soft wrap.
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			if (!LineHasCaret[i - Range.Start])continue;
			FDreamUITextCaretProperty CaretProperty;
			CaretProperty.CaretPosition = FVector2f(LineCaretX[i - Range.Start], LineCentre);
			CaretProperty.CharIndex = CaretIndexOf(i);
			LineProperty.CaretPropertyList.Add(CaretProperty);
		}
		{
			FDreamUITextCaretProperty CaretProperty;
			CaretProperty.CaretPosition = FVector2f(EndCaretX, LineCentre);
			if (Range.HardBreakElement != -1)
			{
				CaretProperty.CharIndex = CaretIndexOf(Range.HardBreakElement);
			}
			else if (LineIndex == LineRanges.Num() - 1)
			{
				//the string's length in code units, the offset every other caret is counted in
				CaretProperty.CharIndex = In.Content.Len();
			}
			else
			{
				CaretProperty.CharIndex = -1;
			}
			LineProperty.CaretPropertyList.Add(CaretProperty);
		}

		// Visual runs, left to right: neighbouring clusters that run the same way at the same level over contiguous
		// source are one run. What a clamp cut is not on screen and is in no run.
		for (int32 k = 0; k < LinePlaced.Num(); k++)
		{
			const FPlaced& P = LinePlaced[k];
			if (P.bCut)continue;
			const int32 SourceStart = TextProcessingArray[P.Start].StringIndex;
			const int32 SourceEnd = TextProcessingArray[P.End - 1].StringIndex + TextProcessingArray[P.End - 1].Length;
			const float Left = P.X0;
			const float Right = P.BoxRight();
			bool bJoined = false;
			if (Dest->VisualRuns.Num() > VisualRunStart && k > 0 && !LinePlaced[k - 1].bCut)
			{
				const FPlaced& Prev = LinePlaced[k - 1];
				const bool bContiguous = P.bRightToLeft ? P.End == Prev.Start : P.Start == Prev.End;
				if (Prev.Level == P.Level && bContiguous)
				{
					FDreamTextVisualRun& VisualRun = Dest->VisualRuns.Last();
					VisualRun.SourceStart = FMath::Min(VisualRun.SourceStart, SourceStart);
					VisualRun.SourceEnd = FMath::Max(VisualRun.SourceEnd, SourceEnd);
					VisualRun.Left = FMath::Min(VisualRun.Left, Left);
					VisualRun.Right = FMath::Max(VisualRun.Right, Right);
					bJoined = true;
				}
			}
			if (!bJoined)
			{
				FDreamTextVisualRun& VisualRun = Dest->VisualRuns.AddDefaulted_GetRef();
				VisualRun.LineIndex = LineIndex;
				VisualRun.SourceStart = SourceStart;
				VisualRun.SourceEnd = SourceEnd;
				VisualRun.Left = Left;
				VisualRun.Right = Right;
				VisualRun.bRightToLeft = P.bRightToLeft;
			}
		}

		// The line's extent: from its start edge to the far edge of its content, hanging whitespace left out. Negative
		// letter spacing pulls the pen back over ink it has already drawn, and the line then has to reach the ink, or the
		// overlap would make the line narrower than what it shows. With zero or positive spacing it stays the pen box,
		// as CSS has it, and widths are what they always were.
		const bool bInkCounts = In.FontSpace.X < 0.0f;
		const float TrailingSpacingFactor = bTrailingLetterSpacingCounts ? 1.0f : 0.0f;
		int32 LastContentStart = INDEX_NONE;
		for (const FPlaced& P : LinePlaced)
		{
			if (!P.bTrailing && !P.bCut)
			{
				LastContentStart = FMath::Max(LastContentStart, P.Start);
			}
		}
		bool bAnyContent = false;
		float ContentLeft = MAX_FLT;
		float ContentRight = -MAX_FLT;
		auto AddExtent = [&ContentLeft, &ContentRight, &bAnyContent](float Left, float Right)
		{
			ContentLeft = FMath::Min(ContentLeft, Left);
			ContentRight = FMath::Max(ContentRight, Right);
			bAnyContent = true;
		};
		for (const FPlaced& P : LinePlaced)
		{
			if (P.bTrailing || P.bCut)continue;
			float BoxLeft = P.X0;
			float BoxRight = P.BoxRight();
			if (P.Start == LastContentStart)
			{
				// The line's last cluster: its letter spacing counts or not as bTrailingLetterSpacingCounts says.
				const float Dropped = P.Spacing * (1.0f - TrailingSpacingFactor);
				if (P.bRightToLeft)
				{
					BoxLeft += Dropped;
				}
				else
				{
					BoxRight -= Dropped;
				}
			}
			AddExtent(BoxLeft, BoxRight);
			if (!bInkCounts)continue;
			for (int32 ItemIndex = P.ItemStart; ItemIndex < P.ItemEnd; ItemIndex++)
			{
				const FDreamTextGlyphItem& Item = Dest->Items[ItemIndex];
				if (!Item.bEmit)continue;
				float InkLeft = 0.0f, InkRight = 0.0f;
				ItemInkExtent(Item, InkLeft, InkRight);
				AddExtent(InkLeft, InkRight);
			}
		}
		if (LineDotsItem != INDEX_NONE)
		{
			const FDreamTextGlyphItem& Dots = Dest->Items[LineDotsItem];
			AddExtent(Dots.Pen.X + Dots.DecorationOffset, Dots.Pen.X + Dots.DecorationOffset + Dots.AdvanceWithSpace);
		}
		float LineLeft = 0.0f;
		float LineRight = 0.0f;
		if (bAnyContent)
		{
			// The start edge is where the pen started -- the left of a left-to-right line, the right of a right-to-left one.
			LineLeft = bBaseRightToLeft ? ContentLeft : 0.0f;
			LineRight = ContentRight;
		}

		// Horizontal alignment against the box, whose left edge is 0 here. In a right-to-left paragraph Left and Right
		// mean start and end, as they do for UMG's text (Left aligns to the start of the flow) and for CSS's
		// text-align: start; Center is Center either way.
		EDreamUITextParagraphHorizontalAlign Align = In.ParagraphHAlign;
		if (Align == EDreamUITextParagraphHorizontalAlign::Justify)
		{
			// A justified line already spans its width from the start edge. One that was not stretched -- it has no
			// opportunity, or it is cut -- starts there too, unless it ends its paragraph: that one is set as
			// LastLineAlign says (CSS text-align-last).
			Align = EDreamUITextParagraphHorizontalAlign::Left;
			const bool bEndsParagraph = Range.HardBreakElement != -1 || LineIndex == LineRanges.Num() - 1;
			if (!bJustified && bEndsParagraph)
			{
				if (In.LastLineAlign == EDreamTextLastLineAlign::Center)
				{
					Align = EDreamUITextParagraphHorizontalAlign::Center;
				}
				else if (In.LastLineAlign == EDreamTextLastLineAlign::End)
				{
					Align = EDreamUITextParagraphHorizontalAlign::Right;
				}
			}
		}
		if (bBaseRightToLeft)
		{
			if (Align == EDreamUITextParagraphHorizontalAlign::Left)
			{
				Align = EDreamUITextParagraphHorizontalAlign::Right;
			}
			else if (Align == EDreamUITextParagraphHorizontalAlign::Right)
			{
				Align = EDreamUITextParagraphHorizontalAlign::Left;
			}
		}
		float XOffset = 0.0f;
		switch (Align)
		{
		case EDreamUITextParagraphHorizontalAlign::Center:
			XOffset = In.Width * 0.5f - (LineLeft + LineRight) * 0.5f;
			break;
		case EDreamUITextParagraphHorizontalAlign::Right:
			XOffset = In.Width - LineRight;
			break;
		default:
			XOffset = -LineLeft;
			break;
		}
		ShiftLine(LineItemStart, ImageStart, EmojiStart, VisualRunStart, LineProperty, XOffset);
		Dest->Lines.Add(LineProperty);
	}

	void FLayoutRun::Place()
	{
		// Lines stack down from the paragraph's top edge at y = 0, each as tall as its line box. Each is placed with its own
		// top at 0, and Finish moves it down to its place: a line placed alike anywhere comes out alike, which is what lets
		// a kept line stand for placing it again, bit for bit.
		//
		// A wrapped paragraph under a clamp policy also has a BOTTOM: a line that does not fit the box
		// is not placed at all, and the last one that does carries the ellipsis -- Slate's "overflow
		// line". Without wrapping there is only ever one line, which is why Ellipsis used to be a
		// single-line feature.
		const bool bVerticalClamp = IsClampMode() && ShouldWrap() && In.Height > 0.0f;
		// A clamp reads the lines around a line and what was cut before it: under one, every line is placed again. A line is
		// copied from the kept layout only from its copy of the list (DreamGUI.Text.InPlaceDisplayList 0); with the switch on,
		// a list that could not be edited in place is placed whole. Paint runs are measured over what placing marks (the
		// whitespace hanging off a line's end), so a text with paints places every line.
		const bool bMayKeepPlacement = bUseState && bLinePlacementSame && !IsClampMode() && !bInPlaceSwitch && !bAnyPaint;
		float LineTop = 0.0f;
		LineSpans.Reset(LineRanges.Num());
		for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); LineIndex++)
		{
			if (bVerticalClamp && LineIndex + 1 < LineRanges.Num())
			{
				const FLineBox ThisBox = ComputeLineBox(LineIndex);
				const FLineBox NextBox = ComputeLineBox(LineIndex + 1);
				const float AfterThis = ParagraphHeight + ThisBox.Top + ThisBox.Bottom + In.FontSpace.Y;
				if (AfterThis + NextBox.Top + NextBox.Bottom > In.Height + KINDA_SMALL_NUMBER)
				{
					bEllipsizeThisLine = true;
				}
			}
			const bool bWasLastVisibleLine = bEllipsizeThisLine;
			FLineSpan Span;
			Span.Top = LineTop;
			Span.bLastLine = LineIndex == LineRanges.Num() - 1;
			const int32 Source = bMayKeepPlacement ? LineSources[LineIndex] : INDEX_NONE;
			int32 SourceDelta = 0;
			if (Source != INDEX_NONE && CanReuseLine(LineIndex, Source, SourceDelta))
			{
				ReuseLine(LineIndex, Source, SourceDelta, Span);
			}
			else
			{
				PlaceOneLine(LineIndex, Span);
			}
			LineSpans.Add(Span);
			bEllipsizeThisLine = false;
			const float LineAdvance = CurrentLineHeight + In.FontSpace.Y;
			LineTop -= LineAdvance;
			ParagraphHeight += LineAdvance;
			if (bHasClampContent && bShouldSetParagraphHeightForClampContent)
			{
				bShouldSetParagraphHeightForClampContent = false;
				ParagraphHeight_ForClampContent = ParagraphHeight;
				ClampedLineCount = LineSpans.Num();
			}
			if (bWasLastVisibleLine)
			{
				break;//every line after this one is below the box
			}
		}
	}

	void FLayoutRun::PlaceOneLine(int32 LineIndex, FLineSpan& Span)
	{
		FStageTimer Timer(EDreamTextLayoutStage::Place);
		Span.ItemStart = Dest->Items.Num();
		Span.ImageStart = Dest->Images.Num();
		Span.EmojiStart = Dest->Emojis.Num();
		Span.VisualRunStart = Dest->VisualRuns.Num();
		// Whether this line itself waits for a glyph: such a line is placed again next time rather than kept.
		const bool bPendingBefore = Dest->bHasPendingGlyphs;
		Dest->bHasPendingGlyphs = false;
		PlaceLine(LineIndex);
		Span.bPending = Dest->bHasPendingGlyphs;
		Dest->bHasPendingGlyphs |= bPendingBefore;
		Span.ItemEnd = Dest->Items.Num();
		Span.ImageEnd = Dest->Images.Num();
		Span.EmojiEnd = Dest->Emojis.Num();
		Span.VisualRunEnd = Dest->VisualRuns.Num();
		Span.Height = CurrentLineHeight;
		Span.bLastLine = LineIndex == LineRanges.Num() - 1;
		// Its share of the visible-character numbering, counted as Finish counts it: one per element, in painted order.
		Span.VisibleCount = 0;
		int32 LastCounted = INDEX_NONE;
		for (int32 i = Span.ItemStart; i < Span.ItemEnd; i++)
		{
			const FDreamTextGlyphItem& Item = Dest->Items[i];
			if (Item.bCountsAsVisible && Item.ElementIndex != LastCounted)
			{
				Span.VisibleCount++;
				LastCounted = Item.ElementIndex;
			}
		}
		Stats.LinesPlaced++;
		Stats.ItemsPlaced += Span.ItemEnd - Span.ItemStart;
	}

	bool FLayoutRun::CanReuseLine(int32 LineIndex, int32 KeptIndex, int32& OutSourceDelta) const
	{
		if (!State->LineSpans.IsValidIndex(KeptIndex) || !State->LineRanges.IsValidIndex(KeptIndex))
		{
			return false;
		}
		const FLineSpan& Kept = State->LineSpans[KeptIndex];
		const FLineRange& Range = LineRanges[LineIndex];
		const FLineRange& KeptRange = State->LineRanges[KeptIndex];
		// The text's last line ends in a caret naming the text's length, and a paragraph's last line is set as LastLineAlign
		// says: a line keeps its placement only where it ends as it did.
		if (Kept.bPending || Kept.bLastLine != (LineIndex == LineRanges.Num() - 1)
			|| (KeptRange.HardBreakElement != -1) != (Range.HardBreakElement != -1) || Range.End - Range.Start != KeptRange.End - KeptRange.Start)
		{
			return false;
		}
		// Every source position on the line -- its items', its carets', its runs' -- moved by one amount, the edit's length.
		bool bHaveDelta = false;
		int32 SourceDelta = 0;
		auto Moved = [&](int32 Element, int32 KeptElement)
		{
			const FDreamUIText_TextProcessingElement& Now = TextProcessingArray[Element];
			const FDreamUIText_TextProcessingElement Then = KeptElementAt(KeptElement);
			const int32 Delta = Now.StringIndex - Then.StringIndex;
			if (!bHaveDelta)
			{
				SourceDelta = Delta;
				bHaveDelta = true;
			}
			return Now.Length == Then.Length && Delta == SourceDelta && CaretIndexOf(Element) - KeptCaretIndexOf(KeptElement) == SourceDelta;
		};
		for (int32 k = 0; k < Range.End - Range.Start; k++)
		{
			if (!Moved(Range.Start + k, KeptRange.Start + k))
			{
				return false;
			}
		}
		if (Range.HardBreakElement != -1 && !Moved(Range.HardBreakElement, KeptRange.HardBreakElement))
		{
			return false;
		}
		if (Kept.bLastLine)
		{
			const int32 Delta = In.Content.Len() - State->ContentLength;
			if (bHaveDelta && Delta != SourceDelta)
			{
				return false;
			}
			SourceDelta = Delta;
		}
		OutSourceDelta = SourceDelta;
		return true;
	}

	void FLayoutRun::ReuseLine(int32 LineIndex, int32 KeptIndex, int32 SourceDelta, FLineSpan& Span)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Reuse);
		FStageTimer Timer(EDreamTextLayoutStage::Reuse);
		const FLineSpan& Kept = State->LineSpans[KeptIndex];
		const int32 ElementDelta = LineRanges[LineIndex].Start - State->LineRanges[KeptIndex].Start;
		// The line as it was placed, at its own top: only what names an element, a source offset or a line moves.
		Span.ItemStart = Out.Items.Num();
		Out.Items.Append(State->Items.GetData() + Kept.ItemStart, Kept.ItemEnd - Kept.ItemStart);
		Span.ItemEnd = Out.Items.Num();
		for (int32 i = Span.ItemStart; i < Span.ItemEnd; i++)
		{
			FDreamTextGlyphItem& Item = Out.Items[i];
			Item.ElementIndex += ElementDelta;
			Item.SourceIndex += SourceDelta;
			Item.LineIndex = LineIndex;
		}
		Span.ImageStart = Out.Images.Num();
		Out.Images.Append(State->Images.GetData() + Kept.ImageStart, Kept.ImageEnd - Kept.ImageStart);
		Span.ImageEnd = Out.Images.Num();
		Span.EmojiStart = Out.Emojis.Num();
		Out.Emojis.Append(State->Emojis.GetData() + Kept.EmojiStart, Kept.EmojiEnd - Kept.EmojiStart);
		Span.EmojiEnd = Out.Emojis.Num();
		Span.VisualRunStart = Out.VisualRuns.Num();
		Out.VisualRuns.Append(State->VisualRuns.GetData() + Kept.VisualRunStart, Kept.VisualRunEnd - Kept.VisualRunStart);
		Span.VisualRunEnd = Out.VisualRuns.Num();
		for (int32 i = Span.VisualRunStart; i < Span.VisualRunEnd; i++)
		{
			FDreamTextVisualRun& VisualRun = Out.VisualRuns[i];
			VisualRun.LineIndex = LineIndex;
			VisualRun.SourceStart += SourceDelta;
			VisualRun.SourceEnd += SourceDelta;
		}
		FDreamUITextLineProperty& LineProperty = Out.Lines.Add_GetRef(State->Lines[KeptIndex]);
		for (FDreamUITextCaretProperty& Caret : LineProperty.CaretPropertyList)
		{
			// A soft-wrapped line's end caret names no offset (-1).
			if (Caret.CharIndex >= 0)
			{
				Caret.CharIndex += SourceDelta;
			}
		}
		Span.Height = Kept.Height;
		Span.bPending = false;
		Span.VisibleCount = Kept.VisibleCount;
		CurrentLineHeight = Kept.Height;
		Stats.LinesReused++;
		Stats.ItemsCopied += Span.ItemEnd - Span.ItemStart;
	}

	bool FLayoutRun::CanKeepLineInPlace(int32 LineIndex, int32 KeptIndex, int32& OutElementDelta, int32& OutSourceDelta) const
	{
		if (!State->LineSpans.IsValidIndex(KeptIndex) || !State->LineRanges.IsValidIndex(KeptIndex))
		{
			return false;
		}
		const FLineSpan& Kept = State->LineSpans[KeptIndex];
		const FLineRange& Range = LineRanges[LineIndex];
		const FLineRange& KeptRange = State->LineRanges[KeptIndex];
		// As CanReuseLine: a line keeps its placement only where it ends as it did.
		if (Kept.bPending || Kept.bLastLine != (LineIndex == LineRanges.Num() - 1)
			|| (KeptRange.HardBreakElement != -1) != (Range.HardBreakElement != -1) || Range.End - Range.Start != KeptRange.End - KeptRange.Start)
		{
			return false;
		}
		OutElementDelta = Range.Start - KeptRange.Start;
		if (!bInPlaceParse)
		{
			// Nothing says what the edit was: every element's source position is checked, as for a line copied.
			return CanReuseLine(LineIndex, KeptIndex, OutSourceDelta);
		}
		// The edit is known (TryParseInPlace): a kept line wholly before its window is the same line, element for element; one
		// wholly after it moved by the window's deltas, every source position with it, the text's end caret included.
		const int32 KeptLast = KeptRange.HardBreakElement != -1 ? KeptRange.HardBreakElement : KeptRange.End - 1;
		if (KeptLast < EditStart && OutElementDelta == 0 && (!Kept.bLastLine || EditSourceDelta == 0))
		{
			OutSourceDelta = 0;
			return true;
		}
		if (KeptRange.Start >= EditKeptEnd && OutElementDelta == EditElementDelta)
		{
			OutSourceDelta = EditSourceDelta;
			return true;
		}
		return false;
	}

	bool FLayoutRun::EditDisplayListInPlace()
	{
		// The list has to be the one the state wrote, unchanged since, and its lines kept in the form this edits; a clamp reads
		// the lines around a line, and paint runs are numbered across the list: those are placed whole.
		if (State == nullptr || !bUseState || !bLinePlacementSame || IsClampMode() || !State->bLocalLinesKept || bAnyPaint || State->bAnyPaint
			|| State->WrittenList != &Out || State->WrittenGeneration != Out.Generation)
		{
			return false;
		}
		const int32 KeptLineCount = State->LineSpans.Num();
		const int32 LineCount = LineRanges.Num();
		if (KeptLineCount == 0 || LineCount == 0 || Out.Lines.Num() != KeptLineCount || Out.LineStamps.Num() != KeptLineCount
			|| Out.Items.Num() != State->LineSpans.Last().ItemEnd || Out.Images.Num() != State->LineSpans.Last().ImageEnd
			|| Out.Emojis.Num() != State->LineSpans.Last().EmojiEnd || Out.VisualRuns.Num() != State->LineSpans.Last().VisualRunEnd
			|| State->LocalPens.Num() != Out.Items.Num() || State->LocalImages.Num() != Out.Images.Num()
			|| State->LocalEmojis.Num() != Out.Emojis.Num() || State->LocalRuns.Num() != Out.VisualRuns.Num()
			|| State->LocalCarets.Num() != State->LineSpans.Last().CaretEnd)
		{
			return false;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Patch);
		// The kept lines at the start that are this layout's lines where they are, and those at the end, each moved by what
		// the edit moved its elements and its source by; the lines between are placed again.
		int32 First = 0;
		while (First < LineCount && First < KeptLineCount && LineSources[First] == First)
		{
			int32 ElementShift = 0;
			int32 SourceShift = 0;
			if (!CanKeepLineInPlace(First, First, ElementShift, SourceShift) || ElementShift != 0 || SourceShift != 0)
			{
				break;
			}
			First++;
		}
		int32 TailStart = LineCount;
		int32 KeptTailStart = KeptLineCount;
		TArray<FIntPoint, TInlineAllocator<64>> TailShifts;
		while (TailStart > First && KeptTailStart > First && LineSources[TailStart - 1] == KeptTailStart - 1)
		{
			int32 ElementShift = 0;
			int32 SourceShift = 0;
			if (!CanKeepLineInPlace(TailStart - 1, KeptTailStart - 1, ElementShift, SourceShift))
			{
				break;
			}
			TailShifts.Add(FIntPoint(ElementShift, SourceShift));
			TailStart--;
			KeptTailStart--;
		}
		Algo::Reverse(TailShifts);

		// The lines between, placed into the scratch list at their own tops, as Place places them.
		Scratch.Reset();
		Dest = &Scratch;
		TArray<FLineSpan, TInlineAllocator<8>> WindowSpans;
		for (int32 LineIndex = First; LineIndex < TailStart; LineIndex++)
		{
			FLineSpan& Span = WindowSpans.AddDefaulted_GetRef();
			PlaceOneLine(LineIndex, Span);
		}
		Dest = &Out;
		FStageTimer Timer(EDreamTextLayoutStage::Patch);

		// Their line-local carets, before the lines move into the list.
		TArray<FVector2f> WindowCarets;
		for (int32 k = 0; k < WindowSpans.Num(); k++)
		{
			WindowSpans[k].CaretStart = WindowCarets.Num();
			for (const FDreamUITextCaretProperty& Caret : Scratch.Lines[k].CaretPropertyList)
			{
				WindowCarets.Add(Caret.CaretPosition);
			}
			WindowSpans[k].CaretEnd = WindowCarets.Num();
		}

		// Spliced in where the kept lines they replace were, every array moved once.
		const TArray<FLineSpan>& KeptSpans = State->LineSpans;
		auto KeptAt = [&KeptSpans, KeptLineCount](int32 KeptLine, int32 FLineSpan::* Start, int32 EndOfList)
		{
			return KeptLine < KeptLineCount ? KeptSpans[KeptLine].*Start : EndOfList;
		};
		const int32 ItemFrom = KeptAt(First, &FLineSpan::ItemStart, Out.Items.Num());
		const int32 ItemTo = KeptAt(KeptTailStart, &FLineSpan::ItemStart, Out.Items.Num());
		const int32 ImageFrom = KeptAt(First, &FLineSpan::ImageStart, Out.Images.Num());
		const int32 ImageTo = KeptAt(KeptTailStart, &FLineSpan::ImageStart, Out.Images.Num());
		const int32 EmojiFrom = KeptAt(First, &FLineSpan::EmojiStart, Out.Emojis.Num());
		const int32 EmojiTo = KeptAt(KeptTailStart, &FLineSpan::EmojiStart, Out.Emojis.Num());
		const int32 RunFrom = KeptAt(First, &FLineSpan::VisualRunStart, Out.VisualRuns.Num());
		const int32 RunTo = KeptAt(KeptTailStart, &FLineSpan::VisualRunStart, Out.VisualRuns.Num());
		const int32 CaretFrom = KeptAt(First, &FLineSpan::CaretStart, State->LocalCarets.Num());
		const int32 CaretTo = KeptAt(KeptTailStart, &FLineSpan::CaretStart, State->LocalCarets.Num());

		LocalPens = MoveTemp(State->LocalPens);
		LocalCarets = MoveTemp(State->LocalCarets);
		LocalImages = MoveTemp(State->LocalImages);
		LocalEmojis = MoveTemp(State->LocalEmojis);
		LocalRuns = MoveTemp(State->LocalRuns);

		SpliceRange(Out.Items, ItemFrom, ItemTo - ItemFrom, Scratch.Items.Num());
		SpliceRange(LocalPens, ItemFrom, ItemTo - ItemFrom, Scratch.Items.Num());
		for (int32 k = 0; k < Scratch.Items.Num(); k++)
		{
			LocalPens[ItemFrom + k] = Scratch.Items[k].Pen;
			Out.Items[ItemFrom + k] = MoveTemp(Scratch.Items[k]);
		}
		SpliceRange(Out.Images, ImageFrom, ImageTo - ImageFrom, Scratch.Images.Num());
		SpliceRange(LocalImages, ImageFrom, ImageTo - ImageFrom, Scratch.Images.Num());
		for (int32 k = 0; k < Scratch.Images.Num(); k++)
		{
			LocalImages[ImageFrom + k] = Scratch.Images[k].Position;
			Out.Images[ImageFrom + k] = MoveTemp(Scratch.Images[k]);
		}
		SpliceRange(Out.Emojis, EmojiFrom, EmojiTo - EmojiFrom, Scratch.Emojis.Num());
		SpliceRange(LocalEmojis, EmojiFrom, EmojiTo - EmojiFrom, Scratch.Emojis.Num());
		for (int32 k = 0; k < Scratch.Emojis.Num(); k++)
		{
			LocalEmojis[EmojiFrom + k] = Scratch.Emojis[k].Position;
			Out.Emojis[EmojiFrom + k] = MoveTemp(Scratch.Emojis[k]);
		}
		SpliceRange(Out.VisualRuns, RunFrom, RunTo - RunFrom, Scratch.VisualRuns.Num());
		SpliceRange(LocalRuns, RunFrom, RunTo - RunFrom, Scratch.VisualRuns.Num());
		for (int32 k = 0; k < Scratch.VisualRuns.Num(); k++)
		{
			LocalRuns[RunFrom + k] = FVector2f(Scratch.VisualRuns[k].Left, Scratch.VisualRuns[k].Right);
			Out.VisualRuns[RunFrom + k] = Scratch.VisualRuns[k];
		}
		SpliceRange(LocalCarets, CaretFrom, CaretTo - CaretFrom, WindowCarets.Num());
		for (int32 k = 0; k < WindowCarets.Num(); k++)
		{
			LocalCarets[CaretFrom + k] = WindowCarets[k];
		}
		SpliceRange(Out.Lines, First, KeptTailStart - First, WindowSpans.Num());
		SpliceRange(Out.LineStamps, First, KeptTailStart - First, WindowSpans.Num());
		for (int32 k = 0; k < WindowSpans.Num(); k++)
		{
			Out.Lines[First + k] = MoveTemp(Scratch.Lines[k]);
			Out.LineStamps[First + k] = DreamTextLayoutGlobals::NextLineStamp();
		}

		// The spans: the kept ones before, the window's at their place in the list, the kept ones after moved by the splice.
		const int32 ItemShift = Scratch.Items.Num() - (ItemTo - ItemFrom);
		const int32 ImageShift = Scratch.Images.Num() - (ImageTo - ImageFrom);
		const int32 EmojiShift = Scratch.Emojis.Num() - (EmojiTo - EmojiFrom);
		const int32 RunShift = Scratch.VisualRuns.Num() - (RunTo - RunFrom);
		const int32 CaretShift = WindowCarets.Num() - (CaretTo - CaretFrom);
		LineSpans.Reset(LineCount);
		for (int32 LineIndex = 0; LineIndex < First; LineIndex++)
		{
			LineSpans.Add(KeptSpans[LineIndex]);
		}
		for (const FLineSpan& WindowSpan : WindowSpans)
		{
			FLineSpan& Span = LineSpans.Add_GetRef(WindowSpan);
			Span.ItemStart += ItemFrom;
			Span.ItemEnd += ItemFrom;
			Span.ImageStart += ImageFrom;
			Span.ImageEnd += ImageFrom;
			Span.EmojiStart += EmojiFrom;
			Span.EmojiEnd += EmojiFrom;
			Span.VisualRunStart += RunFrom;
			Span.VisualRunEnd += RunFrom;
			Span.CaretStart += CaretFrom;
			Span.CaretEnd += CaretFrom;
		}
		for (int32 KeptLine = KeptTailStart; KeptLine < KeptLineCount; KeptLine++)
		{
			FLineSpan& Span = LineSpans.Add_GetRef(KeptSpans[KeptLine]);
			Span.ItemStart += ItemShift;
			Span.ItemEnd += ItemShift;
			Span.ImageStart += ImageShift;
			Span.ImageEnd += ImageShift;
			Span.EmojiStart += EmojiShift;
			Span.EmojiEnd += EmojiShift;
			Span.VisualRunStart += RunShift;
			Span.VisualRunEnd += RunShift;
			Span.CaretStart += CaretShift;
			Span.CaretEnd += CaretShift;
		}
		if (ItemShift != 0)
		{
			Stats.ItemsMoved += Out.Items.Num() - (ItemFrom + Scratch.Items.Num());
		}

		// The lines after the window rebased: the elements, source positions and line index the edit moved them by.
		for (int32 LineIndex = TailStart; LineIndex < LineCount; LineIndex++)
		{
			const FIntPoint& Shift = TailShifts[LineIndex - TailStart];
			// A line whose elements, source and index all stayed is the line it was.
			if (Shift.X == 0 && Shift.Y == 0 && TailStart == KeptTailStart)
			{
				continue;
			}
			const FLineSpan& Span = LineSpans[LineIndex];
			for (int32 i = Span.ItemStart; i < Span.ItemEnd; i++)
			{
				FDreamTextGlyphItem& Item = Out.Items[i];
				Item.ElementIndex += Shift.X;
				Item.SourceIndex += Shift.Y;
				Item.LineIndex = LineIndex;
			}
			for (FDreamUITextCaretProperty& Caret : Out.Lines[LineIndex].CaretPropertyList)
			{
				// A soft-wrapped line's end caret names no offset (-1).
				if (Caret.CharIndex >= 0)
				{
					Caret.CharIndex += Shift.Y;
				}
			}
			for (int32 i = Span.VisualRunStart; i < Span.VisualRunEnd; i++)
			{
				FDreamTextVisualRun& VisualRun = Out.VisualRuns[i];
				VisualRun.LineIndex = LineIndex;
				VisualRun.SourceStart += Shift.Y;
				VisualRun.SourceEnd += Shift.Y;
			}
			Out.LineStamps[LineIndex] = DreamTextLayoutGlobals::NextLineStamp();
			Stats.ItemsRebased += Span.ItemEnd - Span.ItemStart;
		}

		// Every line's top again, in Place's order of additions, so each comes out bit for bit as placing every line would.
		float LineTop = 0.0f;
		ParagraphHeight = 0.0f;
		for (FLineSpan& Span : LineSpans)
		{
			Span.Top = LineTop;
			const float LineAdvance = Span.Height + In.FontSpace.Y;
			LineTop -= LineAdvance;
			ParagraphHeight += LineAdvance;
		}
		PlacedLines.Init(false, LineCount);
		for (int32 LineIndex = First; LineIndex < TailStart; LineIndex++)
		{
			PlacedLines[LineIndex] = true;
		}
		Out.bHasPendingGlyphs = false;
		for (const FLineSpan& Span : LineSpans)
		{
			Out.bHasPendingGlyphs |= Span.bPending;
		}
		Stats.LinesReused += First + (LineCount - TailStart);
		Scratch.Reset();
		return true;
	}

	void FLayoutRun::KeepLocalLines()
	{
		// A clamp places every line against the lines around it: nothing of its placement is kept to edit.
		if (State == nullptr || IsClampMode())
		{
			return;
		}
		FStageTimer Timer(EDreamTextLayoutStage::Reuse);
		// What Finish moves, as each line was placed at its own top: a later in-place edit moves a line from here.
		LocalPens.SetNumUninitialized(Out.Items.Num());
		for (int32 i = 0; i < Out.Items.Num(); i++)
		{
			LocalPens[i] = Out.Items[i].Pen;
		}
		LocalImages.SetNumUninitialized(Out.Images.Num());
		for (int32 i = 0; i < Out.Images.Num(); i++)
		{
			LocalImages[i] = Out.Images[i].Position;
		}
		LocalEmojis.SetNumUninitialized(Out.Emojis.Num());
		for (int32 i = 0; i < Out.Emojis.Num(); i++)
		{
			LocalEmojis[i] = Out.Emojis[i].Position;
		}
		LocalRuns.SetNumUninitialized(Out.VisualRuns.Num());
		for (int32 i = 0; i < Out.VisualRuns.Num(); i++)
		{
			LocalRuns[i] = FVector2f(Out.VisualRuns[i].Left, Out.VisualRuns[i].Right);
		}
		LocalCarets.Reset();
		for (int32 LineIndex = 0; LineIndex < LineSpans.Num() && LineIndex < Out.Lines.Num(); LineIndex++)
		{
			FLineSpan& Span = LineSpans[LineIndex];
			Span.CaretStart = LocalCarets.Num();
			for (const FDreamUITextCaretProperty& Caret : Out.Lines[LineIndex].CaretPropertyList)
			{
				LocalCarets.Add(Caret.CaretPosition);
			}
			Span.CaretEnd = LocalCarets.Num();
		}
	}

	void FLayoutRun::FinishLine(int32 LineIndex, float XOffset, float LineY)
	{
		// Each value its line-local one plus the line's offsets: the one addition Finish makes, so a line moved again comes out
		// bit for bit as one finished for the first time.
		const FLineSpan& Span = LineSpans[LineIndex];
		TArray<FDreamUITextCaretProperty>& Carets = Out.Lines[LineIndex].CaretPropertyList;
		for (int32 c = 0; c < Carets.Num() && Span.CaretStart + c < Span.CaretEnd; c++)
		{
			const FVector2f& Local = LocalCarets[Span.CaretStart + c];
			Carets[c].CaretPosition.X = Local.X + XOffset;
			Carets[c].CaretPosition.Y = Local.Y + LineY;
		}
		for (int32 i = Span.ImageStart; i < Span.ImageEnd; i++)
		{
			Out.Images[i].Position.X = LocalImages[i].X + XOffset;
			Out.Images[i].Position.Y = LocalImages[i].Y + LineY;
		}
		for (int32 i = Span.EmojiStart; i < Span.EmojiEnd; i++)
		{
			Out.Emojis[i].Position.X = LocalEmojis[i].X + XOffset;
			Out.Emojis[i].Position.Y = LocalEmojis[i].Y + LineY;
		}
		for (int32 i = Span.ItemStart; i < Span.ItemEnd; i++)
		{
			Out.Items[i].Pen.X = LocalPens[i].X + XOffset;
			Out.Items[i].Pen.Y = LocalPens[i].Y + LineY;
		}
		for (int32 i = Span.VisualRunStart; i < Span.VisualRunEnd; i++)
		{
			Out.VisualRuns[i].Left = LocalRuns[i].X + XOffset;
			Out.VisualRuns[i].Right = LocalRuns[i].Y + XOffset;
		}
	}

	void FLayoutRun::Run()
	{
		Stats.Layouts++;
		// A scope a step, so that Insights says which of them a slow layout spent its time in. The display list is reset only
		// once it is known it will not be edited in place.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Prepare);
			FStageTimer Timer(EDreamTextLayoutStage::Prepare);
			Prepare();
		}
		TryParseInPlace();
		if (!bInPlaceParse)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Preprocess);
			FStageTimer Timer(EDreamTextLayoutStage::Preprocess);
			Preprocess();
			BuildPlainText();
			Classify();
		}
		Lookup();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Measure);
			{
				FStageTimer Timer(EDreamTextLayoutStage::Measure);
				AnalyseGraphemes();
			}
			MeasureParagraphs();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_BreakLines);
			if (ShouldWrap())
			{
				FStageTimer Timer(EDreamTextLayoutStage::BreakLines);
				AnalyseBreaks();
			}
			CompareWithDonors();
			FStageTimer Timer(EDreamTextLayoutStage::BreakLines);
			BreakLines();
			FixTabs();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Place);
			bDisplayListInPlace = bInPlaceSwitch && EditDisplayListInPlace();
			if (!bDisplayListInPlace)
			{
				Out.Reset();
				Place();
				if (bInPlaceSwitch)
				{
					KeepLocalLines();
				}
				else
				{
					KeepLines();
				}
			}
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Finish);
			{
				FStageTimer Timer(EDreamTextLayoutStage::Finish);
				Finish();
			}
			KeepState();
		}
	}

	float FLayoutRun::ParagraphPreferredWidth(int32 Start, int32 End, bool& bOutAny, int32 From)
	{
		// The paragraph as it would be on one line. Under negative letter spacing it also reaches the paragraph's ink, as a
		// placed line does.
		const bool bInkCounts = In.FontSpace.X < 0.0f;
		const float TrailingSpacingFactor = bTrailingLetterSpacingCounts ? 1.0f : 0.0f;
		// Taken up where the running pen stood before From: the sum is the one a walk from the start makes, in its order, so
		// it comes out bit for bit. The ink walk reads every cluster, and walks the whole paragraph.
		const int32 SumFrom = (From > Start && From <= End && !bInkCounts) ? From : Start;
		float PenWidth = SumFrom > Start ? Measured[SumFrom - 1].PenAfter : 0.0f;
		float InkRight = -MAX_FLT;
		float LastSpacing = 0.0f;
		bool bSawCluster = false;
		bOutAny = SumFrom > Start;
		Stats.PenSumElements += End - SumFrom;
		for (int32 i = SumFrom; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			if (M.bClusterStart)
			{
				bSawCluster = true;
				LastSpacing = M.LetterSpacing;
				if (bInkCounts)
				{
					// The cluster's glyphs walked from its pen, in logical order: near enough for the ink's reach.
					float GlyphPenX = PenWidth;
					for (int32 e = i; e < End && (e == i || !Measured[e].bClusterStart); e++)
					{
						const FMeasured& Part = Measured[e];
						if (Part.bSkipped || Part.bHardBreak)break;
						if (Part.bImageSpace || Part.bEmoji)
						{
							InkRight = FMath::Max(InkRight, GlyphPenX + Part.ClusterAdvance);
							GlyphPenX += Part.ClusterAdvance;
							continue;
						}
						for (int32 g = 0; g < Part.GlyphCount; g++)
						{
							const FGlyphSource& G = Glyphs[Part.GlyphStart + g];
							if (!Part.bWhitespace)
							{
								InkRight = FMath::Max(InkRight, GlyphPenX + G.XOffset + G.Quad.XOffset + G.Quad.Width);
							}
							GlyphPenX += G.XAdvance;
						}
					}
				}
			}
			PenWidth += M.Advance;
			M.PenAfter = PenWidth;
			bOutAny = true;
		}
		if (!bOutAny)
		{
			return 0.0f;
		}
		if (!bSawCluster)
		{
			// The paragraph's last cluster is before where the sum was taken up.
			for (int32 i = SumFrom - 1; i >= Start; i--)
			{
				if (!Measured[i].bSkipped && !Measured[i].bHardBreak && Measured[i].bClusterStart)
				{
					LastSpacing = Measured[i].LetterSpacing;
					break;
				}
			}
		}
		const float Width = PenWidth - LastSpacing * (1.0f - TrailingSpacingFactor);
		return bInkCounts ? FMath::Max(Width, InkRight) : Width;
	}

	void FLayoutRun::KeepLines()
	{
		if (State == nullptr)
		{
			return;
		}
		FStageTimer Timer(EDreamTextLayoutStage::Reuse);
		// The lines as they were placed, each at its own top, before Finish moves them: what a later layout copies a line from
		// (DreamGUI.Text.InPlaceDisplayList 0). A clamp placed every line against the lines around it, so nothing of that
		// placement is kept.
		State->bLinesKept = !IsClampMode();
		if (!State->bLinesKept)
		{
			State->LineSpans.Reset();
			State->Items.Reset();
			State->Lines.Reset();
			State->Images.Reset();
			State->Emojis.Reset();
			State->VisualRuns.Reset();
			return;
		}
		State->LineSpans = LineSpans;
		State->Items = Out.Items;
		State->Lines = Out.Lines;
		State->Images = Out.Images;
		State->Emojis = Out.Emojis;
		State->VisualRuns = Out.VisualRuns;
		Stats.ItemsCopied += Out.Items.Num();
	}

	void FLayoutRun::KeepState()
	{
		if (State == nullptr)
		{
			return;
		}
		FStageTimer Timer(EDreamTextLayoutStage::Reuse);
		// An atlas flushed or grown during the layout may have taken quads this layout handed out with it: nothing is kept,
		// and the text, which the font told of its new atlas, lays out again from nothing.
		if (FObjectKey(Font->GetFontTexture()) != MeasureKey.Atlas)
		{
			State->Reset();
			return;
		}
		State->bValid = true;
		State->MeasureKey = MeasureKey;
		// The faces this layout drew from, and who they were: a face reloaded since has other glyphs. An edit seen from the
		// content keeps the kept list and adds what its window drew from; a face no longer drawn from only has a later layout
		// check one face more.
		if (!bInPlaceParse)
		{
			State->UsedFaces.Reset();
			State->UsedFaceIdentities.Reset();
			if (bCanShape)
			{
				for (const FMeasured& M : Measured)
				{
					State->UsedFaces.AddUnique(M.FaceIndex);
				}
				for (const FGlyphSource& G : Glyphs)
				{
					if (G.RasterFace >= 0)
					{
						State->UsedFaces.AddUnique(G.RasterFace);
					}
				}
				for (const int32 FaceIndex : State->UsedFaces)
				{
					State->UsedFaceIdentities.Add(Font->GetFaceIdentity(FaceIndex));
				}
			}
		}
		else if (bCanShape && EditParagraph != INDEX_NONE)
		{
			auto AddFace = [this](int32 FaceIndex)
			{
				if (!State->UsedFaces.Contains(FaceIndex))
				{
					State->UsedFaces.Add(FaceIndex);
					State->UsedFaceIdentities.Add(Font->GetFaceIdentity(FaceIndex));
				}
			};
			for (int32 i = MeasureStart; i < MeasureEnd && i < Measured.Num(); i++)
			{
				const FMeasured& M = Measured[i];
				AddFace(M.FaceIndex);
				for (int32 g = M.GlyphStart; g < M.GlyphStart + M.GlyphCount && g < Glyphs.Num(); g++)
				{
					if (Glyphs[g].RasterFace >= 0)
					{
						AddFace(Glyphs[g].RasterFace);
					}
				}
			}
		}
		State->Input = In;
		State->ContentLength = In.Content.Len();
		State->Elements = MoveTemp(TextProcessingArray);
		State->FollowsMarkup = MoveTemp(FollowsMarkup);
		State->PlainText = MoveTemp(PlainText);
		State->PlainStart = MoveTemp(PlainStart);
		State->ElementCodepoints = MoveTemp(ElementCodepoints);
		State->Measured = MoveTemp(Measured);
		State->Glyphs = MoveTemp(Glyphs);
		State->GraphemeStart = MoveTemp(GraphemeStart);
		State->ClusterStarts = MoveTemp(ClusterStarts);
		// Break bits only when they are this layout's: a layout that did not wrap analysed none.
		const bool bWrap = ShouldWrap();
		const bool bPhrase = In.PhraseWrap != EDreamTextPhraseWrap::Off;
		State->bHasBreakBits = bWrap;
		State->bHasWordBits = bWrap && bPhrase;
		State->LineBreakRaw = bWrap ? MoveTemp(LineBreakRaw) : TBitArray<>();
		State->WordBreakRaw = bWrap && bPhrase ? MoveTemp(WordBreakRaw) : TBitArray<>();
		State->CanBreakBefore = bWrap ? MoveTemp(CanBreakBefore) : TBitArray<>();
		State->Paragraphs = MoveTemp(Paragraphs);
		State->PendingParagraphs = 0;
		for (const FParagraph& P : State->Paragraphs)
		{
			State->PendingParagraphs += P.bPendingQuads ? 1 : 0;
		}
		State->LineRanges = MoveTemp(LineRanges);
		State->TagRecords = MoveTemp(TagRecords);
		State->EndStyle = EndStyle;
		State->bEndFollowsMarkup = bEndFollowsMarkup;
		State->bAnyPaint = bAnyPaint;
		State->Languages = MoveTemp(Languages);
		State->LanguageIndexByTag = MoveTemp(LanguageIndexByTag);
		State->InlineObjects = MoveTemp(InlineObjects);

		if (bInPlaceSwitch)
		{
			// The list itself is what a later layout edits, with its lines' line-local coordinates; no copy of it is kept.
			State->bLinesKept = false;
			State->Items.Reset();
			State->Lines.Reset();
			State->Images.Reset();
			State->Emojis.Reset();
			State->VisualRuns.Reset();
			const int32 CaretCount = LineSpans.Num() > 0 ? LineSpans.Last().CaretEnd : 0;
			State->bLocalLinesKept = !IsClampMode() && LineSpans.Num() == Out.Lines.Num() && LocalPens.Num() == Out.Items.Num()
				&& LocalImages.Num() == Out.Images.Num() && LocalEmojis.Num() == Out.Emojis.Num() && LocalRuns.Num() == Out.VisualRuns.Num()
				&& LocalCarets.Num() == CaretCount && Out.LineStamps.Num() == Out.Lines.Num();
			State->LineSpans = MoveTemp(LineSpans);
			State->LocalPens = MoveTemp(LocalPens);
			State->LocalCarets = MoveTemp(LocalCarets);
			State->LocalImages = MoveTemp(LocalImages);
			State->LocalEmojis = MoveTemp(LocalEmojis);
			State->LocalRuns = MoveTemp(LocalRuns);
			State->WrittenList = &Out;
			State->WrittenGeneration = Out.Generation;
		}
		else
		{
			State->bLocalLinesKept = false;
			State->LocalPens.Reset();
			State->LocalCarets.Reset();
			State->LocalImages.Reset();
			State->LocalEmojis.Reset();
			State->LocalRuns.Reset();
			State->WrittenList = nullptr;
			State->WrittenGeneration = 0;
		}
	}

	void FLayoutRun::Finish()
	{
		//remove last line's space Y
		ParagraphHeight -= In.FontSpace.Y;
		ParagraphHeight_ForClampContent -= In.FontSpace.Y;
		const float ParagraphHeightWithClamp = bHasClampContent ? ParagraphHeight_ForClampContent : ParagraphHeight;

		Out.PreferredSize.X = UnwrappedPreferredWidth;
		Out.PreferredSize.Y = ParagraphHeight;
		Out.ElementCount = TextProcessingArray.Num();
		Out.Generation = DreamTextLayoutGlobals::NextGeneration();

		// Each line is already aligned within the box measured from its left edge; what is left is the box's own
		// place around the pivot.
		const float XOffset = In.Width * (0.5f - In.Pivot.X) - In.Width * 0.5f;
		float YOffset = In.Height * (0.5f - In.Pivot.Y);
		switch (In.ParagraphVAlign)
		{
		case EDreamUITextParagraphVerticalAlign::Top:
			YOffset += In.Height * 0.5f;
			break;
		case EDreamUITextParagraphVerticalAlign::Middle:
			YOffset += ParagraphHeightWithClamp * 0.5f;
			break;
		case EDreamUITextParagraphVerticalAlign::Bottom:
			YOffset += ParagraphHeightWithClamp - In.Height * 0.5f;
			break;
		}
		// Every line was placed with its own top at 0: it moves down to its place in the paragraph and, with the paragraph,
		// into the box, in one step. A line an in-place edit kept is where the list's last layout put it: it moves again only
		// when its place changed, from its line-local coordinates by the same one addition, so it comes out bit for bit as a
		// line placed and moved now.
		const int32 LineCount = FMath::Min(LineSpans.Num(), Out.Lines.Num());
		Out.LineStamps.SetNum(Out.Lines.Num());
		for (int32 LineIndex = 0; LineIndex < LineCount; LineIndex++)
		{
			FLineSpan& Span = LineSpans[LineIndex];
			const float LineY = Span.Top + YOffset;
			if (!bDisplayListInPlace || PlacedLines[LineIndex])
			{
				for (auto& CharItem : Out.Lines[LineIndex].CaretPropertyList)
				{
					CharItem.CaretPosition.X += XOffset;
					CharItem.CaretPosition.Y += LineY;
				}
				for (int32 i = Span.ImageStart; i < Span.ImageEnd; i++)
				{
					Out.Images[i].Position.X += XOffset;
					Out.Images[i].Position.Y += LineY;
				}
				for (int32 i = Span.EmojiStart; i < Span.EmojiEnd; i++)
				{
					Out.Emojis[i].Position.X += XOffset;
					Out.Emojis[i].Position.Y += LineY;
				}
				for (int32 i = Span.ItemStart; i < Span.ItemEnd; i++)
				{
					Out.Items[i].Pen.X += XOffset;
					Out.Items[i].Pen.Y += LineY;
				}
				for (int32 i = Span.VisualRunStart; i < Span.VisualRunEnd; i++)
				{
					Out.VisualRuns[i].Left += XOffset;
					Out.VisualRuns[i].Right += XOffset;
				}
				if (!bDisplayListInPlace)
				{
					Out.LineStamps[LineIndex] = DreamTextLayoutGlobals::NextLineStamp();
				}
			}
			else if (Span.FinishX != XOffset || Span.FinishY != LineY)
			{
				FinishLine(LineIndex, XOffset, LineY);
				Out.LineStamps[LineIndex] = DreamTextLayoutGlobals::NextLineStamp();
				Stats.ItemsRefinished += Span.ItemEnd - Span.ItemStart;
			}
			Span.FinishX = XOffset;
			Span.FinishY = LineY;
		}

		// THE numbering of visible characters, built once, here, from the items the painter will walk.
		// One per ELEMENT, in the order they are painted: a code point that shaped into two glyphs is
		// one character, a character whose glyphs have not landed yet is still one character, and a
		// character a clamp threw away is none. Everything that addresses characters -- the display
		// list's count, the painter's FDreamUITextCharProperty list that TextAnimation walks, and the
		// rich-text custom tag ranges resolved below -- reads this one answer. A list edited in place counts it line by
		// line: each line's share was counted as it was placed, and an element never spans two lines.
		Out.CustomTags.Reset();
		Out.CustomTagElementRanges.Reset();
		if (bDisplayListInPlace && !In.bRichText)
		{
			Out.VisibleCharCount = 0;
			for (int32 LineIndex = 0; LineIndex < LineCount; LineIndex++)
			{
				Out.VisibleCharCount += LineSpans[LineIndex].VisibleCount;
			}
		}
		else
		{
			TArray<int32> VisibleIndexOfElement;
			if (In.bRichText)
			{
				VisibleIndexOfElement.Init(INDEX_NONE, Measured.Num());
			}
			Out.VisibleCharCount = 0;
			int32 LastCountedElement = INDEX_NONE;
			for (const auto& Item : Out.Items)
			{
				if (!Item.bCountsAsVisible)continue;
				if (Item.ElementIndex != LastCountedElement)
				{
					if (VisibleIndexOfElement.IsValidIndex(Item.ElementIndex))
					{
						VisibleIndexOfElement[Item.ElementIndex] = Out.VisibleCharCount;
					}
					Out.VisibleCharCount++;
					LastCountedElement = Item.ElementIndex;
				}
			}

			if (In.bRichText)
			{
				for (const FTagRecord& Record : TagRecords)
				{
					FDreamUIText_RichTextCustomTag Tag;
					Tag.TagName = Record.Name;
					Tag.bHyperlink = Record.bHyperlink;
					// An unclosed tag runs to the end of the text, which is what it always did.
					const int32 First = Record.FirstElement;
					const int32 Last = Record.bClosed ? Record.LastElement : Measured.Num() - 1;
					// The visible characters painted for the tag's elements. Painted order is visual order, so inside a
					// right-to-left run the tag's first element is its highest index: the range is their lowest to their
					// highest, whichever ends of the tag they came from. Spaces, images and clamped characters are none.
					int32 MinVisible = MAX_int32;
					int32 MaxVisible = INDEX_NONE;
					for (int32 e = FMath::Max(First, 0); e <= Last && e < VisibleIndexOfElement.Num(); e++)
					{
						const int32 Visible = VisibleIndexOfElement[e];
						if (Visible == INDEX_NONE)continue;
						MinVisible = FMath::Min(MinVisible, Visible);
						MaxVisible = FMath::Max(MaxVisible, Visible);
					}
					if (MaxVisible == INDEX_NONE)
					{
						// Nothing visible inside -- an empty tag, one around spaces or an image, one a clamp removed: an
						// empty range (the end one before the start) where the next visible character is.
						int32 Next = Out.VisibleCharCount;
						for (int32 e = FMath::Max(Last + 1, 0); e < VisibleIndexOfElement.Num(); e++)
						{
							if (VisibleIndexOfElement[e] != INDEX_NONE)
							{
								Next = VisibleIndexOfElement[e];
								break;
							}
						}
						Tag.CharIndexStart = Next;
						Tag.CharIndexEnd = Next - 1;
					}
					else
					{
						Tag.CharIndexStart = MinVisible;
						Tag.CharIndexEnd = MaxVisible;
					}
					Out.CustomTags.Add(Tag);
					Out.CustomTagElementRanges.Add(FIntPoint(First, Last));
				}
			}
		}
		BuildBoxes(XOffset, YOffset);
		BuildPaintFragments();
	}

	void FLayoutRun::BuildBoxes(float XOffset, float YOffset)
	{
		// The content box: the rect the text was laid out in, around its pivot.
		FDreamTextBox& Content = Out.ContentBox;
		Content.Left = -In.Width * In.Pivot.X;
		Content.Right = Content.Left + In.Width;
		Content.Bottom = -In.Height * In.Pivot.Y;
		Content.Top = Content.Bottom + In.Height;
		// The text as a block, CSS's background box of a block: the content box across, from the first line's top to the last
		// placed line's bottom down -- under a clamp, the line it cut, as the vertical alignment has it: a text that does not
		// wrap places the lines after the cut too, and nothing of them is drawn.
		const int32 LineCount = FMath::Min(LineSpans.Num(), Out.Lines.Num());
		const int32 BlockLineCount = bHasClampContent && ClampedLineCount > 0 ? FMath::Min(ClampedLineCount, LineCount) : LineCount;
		FDreamTextBox& Block = Out.TextBlockBox;
		Block.Left = Content.Left;
		Block.Right = Content.Right;
		if (BlockLineCount > 0)
		{
			const FLineSpan& LastSpan = LineSpans[BlockLineCount - 1];
			Block.Top = LineSpans[0].Top + YOffset;
			Block.Bottom = LastSpan.Top + YOffset - LastSpan.Height;
		}
		else
		{
			Block.Top = Content.Top;
			Block.Bottom = Content.Top;
		}
		// Each line: across its visual runs -- an empty line where its caret stands -- and down its own line box, the space
		// between lines left out.
		Out.LineBoxes.SetNum(LineCount);
		for (int32 LineIndex = 0; LineIndex < LineCount; LineIndex++)
		{
			const FLineSpan& Span = LineSpans[LineIndex];
			FDreamTextBox& Box = Out.LineBoxes[LineIndex];
			Box.Top = Span.Top + YOffset;
			Box.Bottom = Box.Top - Span.Height;
			if (Span.VisualRunEnd > Span.VisualRunStart)
			{
				Box.Left = MAX_flt;
				Box.Right = -MAX_flt;
				for (int32 i = Span.VisualRunStart; i < Span.VisualRunEnd; i++)
				{
					Box.Left = FMath::Min(Box.Left, Out.VisualRuns[i].Left);
					Box.Right = FMath::Max(Box.Right, Out.VisualRuns[i].Right);
				}
			}
			else
			{
				const TArray<FDreamUITextCaretProperty>& Carets = Out.Lines[LineIndex].CaretPropertyList;
				const float CaretX = Carets.Num() > 0 ? Carets[0].CaretPosition.X : XOffset;
				Box.Left = CaretX;
				Box.Right = CaretX;
			}
		}
	}

	void FLayoutRun::BuildPaintFragments()
	{
		Out.PaintFragments.Reset();
		if (!bAnyPaint)
		{
			return;
		}
		// A run is the elements one paint tag occurrence is the innermost paint of -- one PaintOrder -- a colour inside it that
		// makes some of them solid included. Its piece on a line spans its items' pen boxes and vertical boxes there; laid end
		// to end in line order, the pieces are the run as if it were on one line (CSS box-decoration-break: slice). What a
		// clamp cut and whitespace hanging off a line's end are in no piece. A run none of whose items its paint draws (a
		// colour inside it covers it all) has no pieces, whether or not another run of the same paint named it in the list.
		TSet<int32> PaintedOrders;
		for (int32 ItemIndex = 0; ItemIndex < Out.Items.Num(); ItemIndex++)
		{
			const FDreamTextGlyphItem& Item = Out.Items[ItemIndex];
			if (Item.Style.PaintIndex != INDEX_NONE && Measured.IsValidIndex(Item.ElementIndex)
				&& !(ItemsOutsideRunBox.IsValidIndex(ItemIndex) && ItemsOutsideRunBox[ItemIndex]))
			{
				PaintedOrders.Add(Measured[Item.ElementIndex].Style.PaintOrder);
			}
		}
		struct FRunPieces
		{
			int32 PaintIndex = INDEX_NONE;
			int32 FirstFragment = 0;
			TArray<FDreamTextPaintFragment, TInlineAllocator<4>> Pieces;
		};
		TArray<FRunPieces> RunList;
		TMap<int32, int32> RunByOrder;
		TArray<FIntPoint> ItemPieces;
		ItemPieces.Init(FIntPoint(INDEX_NONE, INDEX_NONE), Out.Items.Num());
		for (int32 ItemIndex = 0; ItemIndex < Out.Items.Num(); ItemIndex++)
		{
			FDreamTextGlyphItem& Item = Out.Items[ItemIndex];
			Item.PaintFragment = INDEX_NONE;
			if (!Measured.IsValidIndex(Item.ElementIndex) || (ItemsOutsideRunBox.IsValidIndex(ItemIndex) && ItemsOutsideRunBox[ItemIndex]))
			{
				continue;
			}
			const FRichTextParseResult& Style = Measured[Item.ElementIndex].Style;
			if (Style.PaintOrder <= 0 || Style.bPaintRemoved || Style.PaintName.IsNone() || !PaintedOrders.Contains(Style.PaintOrder))
			{
				continue;
			}
			const int32* NameIndex = PaintIndexByName.Find(Style.PaintName);
			if (NameIndex == nullptr)
			{
				continue;
			}
			int32 RunIndex = INDEX_NONE;
			if (const int32* Found = RunByOrder.Find(Style.PaintOrder))
			{
				RunIndex = *Found;
			}
			else
			{
				RunIndex = RunList.Num();
				RunByOrder.Add(Style.PaintOrder, RunIndex);
				RunList.AddDefaulted_GetRef().PaintIndex = *NameIndex;
			}
			FRunPieces& RunPieces = RunList[RunIndex];
			const float Left = Item.Pen.X + Item.DecorationOffset;
			const float Right = Left + Item.AdvanceWithSpace;
			const float Bottom = Item.Pen.Y - Item.Descent;
			const float Top = Item.Pen.Y + Item.Ascent;
			if (RunPieces.Pieces.Num() == 0 || RunPieces.Pieces.Last().LineIndex != Item.LineIndex)
			{
				FDreamTextPaintFragment& Piece = RunPieces.Pieces.AddDefaulted_GetRef();
				Piece.PaintIndex = RunPieces.PaintIndex;
				Piece.LineIndex = Item.LineIndex;
				Piece.Box.Left = Left;
				Piece.Box.Right = Right;
				Piece.Box.Bottom = Bottom;
				Piece.Box.Top = Top;
			}
			else
			{
				FDreamTextBox& Box = RunPieces.Pieces.Last().Box;
				Box.Left = FMath::Min(Box.Left, Left);
				Box.Right = FMath::Max(Box.Right, Right);
				Box.Bottom = FMath::Min(Box.Bottom, Bottom);
				Box.Top = FMath::Max(Box.Top, Top);
			}
			ItemPieces[ItemIndex] = FIntPoint(RunIndex, RunPieces.Pieces.Num() - 1);
		}
		// Run by run, line by line within a run: each piece starts where the run's pieces before it end.
		for (FRunPieces& RunPieces : RunList)
		{
			RunPieces.FirstFragment = Out.PaintFragments.Num();
			float RunWidth = 0.0f;
			for (const FDreamTextPaintFragment& Piece : RunPieces.Pieces)
			{
				RunWidth += Piece.Box.GetWidth();
			}
			float RunOffset = 0.0f;
			for (FDreamTextPaintFragment& Piece : RunPieces.Pieces)
			{
				Piece.RunOffset = RunOffset;
				Piece.RunWidth = RunWidth;
				RunOffset += Piece.Box.GetWidth();
				Out.PaintFragments.Add(Piece);
			}
		}
		for (int32 ItemIndex = 0; ItemIndex < Out.Items.Num(); ItemIndex++)
		{
			const FIntPoint& Piece = ItemPieces[ItemIndex];
			if (Piece.X != INDEX_NONE)
			{
				Out.Items[ItemIndex].PaintFragment = RunList[Piece.X].FirstFragment + Piece.Y;
			}
		}
	}
}

FDreamTextLayoutState::FDreamTextLayoutState()
	: Data(MakeUnique<FDreamTextLayoutStateData>())
{
}

FDreamTextLayoutState::~FDreamTextLayoutState() = default;

void FDreamTextLayoutState::Reset()
{
	Data->Reset();
}

bool FDreamTextLayoutState::HasLayout() const
{
	return Data->bValid;
}

SIZE_T FDreamTextLayoutState::GetAllocatedSize() const
{
	return sizeof(FDreamTextLayoutStateData) + Data->GetAllocatedSize();
}

double FDreamTextLayoutStats::GetMilliseconds(EDreamTextLayoutStage Stage) const
{
	return FPlatformTime::ToMilliseconds64(Cycles[(int32)Stage]);
}

const TCHAR* FDreamTextLayoutStats::GetStageName(EDreamTextLayoutStage Stage)
{
	switch (Stage)
	{
	case EDreamTextLayoutStage::Prepare: return TEXT("Prepare");
	case EDreamTextLayoutStage::Preprocess: return TEXT("Preprocess");
	case EDreamTextLayoutStage::Lookup: return TEXT("Lookup");
	case EDreamTextLayoutStage::Reuse: return TEXT("Reuse");
	case EDreamTextLayoutStage::Measure: return TEXT("Measure");
	case EDreamTextLayoutStage::Shape: return TEXT("Shape");
	case EDreamTextLayoutStage::BreakLines: return TEXT("BreakLines");
	case EDreamTextLayoutStage::Place: return TEXT("Place");
	case EDreamTextLayoutStage::Finish: return TEXT("Finish");
	case EDreamTextLayoutStage::Diff: return TEXT("Diff");
	case EDreamTextLayoutStage::Compare: return TEXT("Compare");
	case EDreamTextLayoutStage::Patch: return TEXT("Patch");
	default: return TEXT("");
	}
}

void FDreamTextLayoutEngine::Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out)
{
	Layout(Input, Out, nullptr);
}

void FDreamTextLayoutEngine::Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out, FDreamTextLayoutState* State)
{
	if (!Input.Font.IsValid())
	{
		// Written all the same, emptied: a list a state wrote before is that list no more.
		Out.Reset();
		Out.Generation = DreamTextLayoutGlobals::NextGeneration();
		return;
	}
	FDreamTextLayoutStateData* Kept = nullptr;
	if (State != nullptr)
	{
		if (IsIncrementalLayoutEnabled())
		{
			Kept = State->Data.Get();
		}
		else
		{
			// Switched off: nothing is built on, and what was kept would only go stale.
			State->Reset();
		}
	}
	bool bBuiltOnKept = false;
	{
		DreamTextLayoutLocal::FLayoutRun Run(Input, Out, Kept);
		Run.Run();
		bBuiltOnKept = Run.BuiltOnKeptLayout();
	}
#if !UE_BUILD_SHIPPING
	if (GDreamTextVerifyIncremental != 0 && bBuiltOnKept && State != nullptr && State->HasLayout())
	{
		// The same layout from nothing, into a list and a state of its own, its work left out of the counters.
		const FDreamTextLayoutStats Before = DreamTextLayoutLocal::Stats;
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutState FreshState;
		{
			DreamTextLayoutLocal::FLayoutRun FreshRun(Input, Fresh, FreshState.Data.Get());
			FreshRun.Run();
		}
		DreamTextLayoutLocal::Stats = Before;
		DreamTextLayoutLocal::Stats.VerifiedLayouts++;
		FString Difference;
		const bool bSameList = DebugCompareDisplayLists(Out, Fresh, Difference);
		const bool bSameState = bSameList && DebugCompareStates(*State, FreshState, Difference);
		if (!bSameList || !bSameState)
		{
			DreamTextLayoutLocal::Stats.VerifyMismatches++;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d DreamGUI.Text.VerifyIncremental: a layout built on a kept one differs from a layout from nothing in its %s: %s. The fresh layout is used and what was kept is dropped. The text: \"%s\"")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, bSameList ? TEXT("kept state") : TEXT("display list"), *Difference,
				*Input.Content.ReplaceCharWithEscapedChar());
			Out = MoveTemp(Fresh);
			State->Reset();
		}
	}
#endif
}

bool FDreamTextLayoutEngine::IsIncrementalLayoutEnabled()
{
	return GDreamTextIncrementalLayout != 0;
}

bool FDreamTextLayoutEngine::IsIncrementalParseEnabled()
{
	return GDreamTextIncrementalParse != 0;
}

bool FDreamTextLayoutEngine::IsIncrementalMeasureEnabled()
{
	return GDreamTextIncrementalMeasure != 0;
}

bool FDreamTextLayoutEngine::IsInPlaceDisplayListEnabled()
{
	return GDreamTextInPlaceDisplayList != 0;
}

FDreamTextLayoutStats FDreamTextLayoutEngine::GetStats()
{
	return DreamTextLayoutLocal::Stats;
}

void FDreamTextLayoutEngine::ResetStats()
{
	DreamTextLayoutLocal::Stats = FDreamTextLayoutStats();
}

#if !UE_BUILD_SHIPPING
namespace DreamTextLayoutDebugLocal
{
	using namespace DreamTextLayoutLocal;

	FString Floats(double A, double B)
	{
		return FString::Printf(TEXT("%.9g vs %.9g"), A, B);
	}

	FString Ints(int64 A, int64 B)
	{
		return FString::Printf(TEXT("%lld vs %lld"), A, B);
	}

	/** The first field two glyph entries differ in, or an empty string. */
	FString CharDataDifference(const FDreamUICharData& A, const FDreamUICharData& B)
	{
		if (A.Width != B.Width) return TEXT("Width ") + Floats(A.Width, B.Width);
		if (A.Height != B.Height) return TEXT("Height ") + Floats(A.Height, B.Height);
		if (A.XOffset != B.XOffset) return TEXT("XOffset ") + Floats(A.XOffset, B.XOffset);
		if (A.YOffset != B.YOffset) return TEXT("YOffset ") + Floats(A.YOffset, B.YOffset);
		if (A.XAdvance != B.XAdvance) return TEXT("XAdvance ") + Floats(A.XAdvance, B.XAdvance);
		if (A.MinUV != B.MinUV) return TEXT("MinUV");
		if (A.MaxUV != B.MaxUV) return TEXT("MaxUV");
		if (A.SliceIndex != B.SliceIndex) return TEXT("SliceIndex ") + Ints(A.SliceIndex, B.SliceIndex);
		if (A.FaceIndex != B.FaceIndex) return TEXT("FaceIndex ") + Ints(A.FaceIndex, B.FaceIndex);
		if (A.GlyphIndex != B.GlyphIndex) return TEXT("GlyphIndex ") + Ints(A.GlyphIndex, B.GlyphIndex);
		if (A.bPending != B.bPending) return TEXT("bPending");
		if (A.bColor != B.bColor) return TEXT("bColor");
		if (A.ColorTexelsPerEm != B.ColorTexelsPerEm) return TEXT("ColorTexelsPerEm ") + Floats(A.ColorTexelsPerEm, B.ColorTexelsPerEm);
		return FString();
	}

	FString ItemStyleDifference(const FDreamTextItemStyle& A, const FDreamTextItemStyle& B)
	{
		if (A.Size != B.Size) return TEXT("Style.Size ") + Floats(A.Size, B.Size);
		if (A.Color != B.Color) return TEXT("Style.Color");
		if (A.bHasColor != B.bHasColor) return TEXT("Style.bHasColor");
		if (A.bHasMultiplyColor != B.bHasMultiplyColor) return TEXT("Style.bHasMultiplyColor");
		if (A.MultiplyColor != B.MultiplyColor) return TEXT("Style.MultiplyColor");
		if (A.bBold != B.bBold) return TEXT("Style.bBold");
		if (A.bItalic != B.bItalic) return TEXT("Style.bItalic");
		if (A.bSyntheticBold != B.bSyntheticBold) return TEXT("Style.bSyntheticBold");
		if (A.bSyntheticItalic != B.bSyntheticItalic) return TEXT("Style.bSyntheticItalic");
		if (A.bUnderline != B.bUnderline) return TEXT("Style.bUnderline");
		if (A.bStrikethrough != B.bStrikethrough) return TEXT("Style.bStrikethrough");
		if (A.SupOrSub != B.SupOrSub) return TEXT("Style.SupOrSub");
		if (A.PaintIndex != B.PaintIndex) return TEXT("Style.PaintIndex ") + Ints(A.PaintIndex, B.PaintIndex);
		if (A.bPaintRemoved != B.bPaintRemoved) return TEXT("Style.bPaintRemoved");
		return FString();
	}

	FString ItemDifference(const FDreamTextGlyphItem& X, const FDreamTextGlyphItem& Y)
	{
		if (X.Kind != Y.Kind) return TEXT("Kind");
		if (X.Codepoint != Y.Codepoint) return FString::Printf(TEXT("Codepoint U+%04X vs U+%04X"), X.Codepoint, Y.Codepoint);
		if (X.ElementIndex != Y.ElementIndex) return TEXT("ElementIndex ") + Ints(X.ElementIndex, Y.ElementIndex);
		if (X.SourceIndex != Y.SourceIndex) return TEXT("SourceIndex ") + Ints(X.SourceIndex, Y.SourceIndex);
		if (X.LineIndex != Y.LineIndex) return TEXT("LineIndex ") + Ints(X.LineIndex, Y.LineIndex);
		if (X.Pen.X != Y.Pen.X) return TEXT("Pen.X ") + Floats(X.Pen.X, Y.Pen.X);
		if (X.Pen.Y != Y.Pen.Y) return TEXT("Pen.Y ") + Floats(X.Pen.Y, Y.Pen.Y);
		if (X.GlyphSize != Y.GlyphSize) return TEXT("GlyphSize ") + Floats(X.GlyphSize, Y.GlyphSize);
		if (X.AdvanceWithSpace != Y.AdvanceWithSpace) return TEXT("AdvanceWithSpace ") + Floats(X.AdvanceWithSpace, Y.AdvanceWithSpace);
		if (X.DecorationOffset != Y.DecorationOffset) return TEXT("DecorationOffset ") + Floats(X.DecorationOffset, Y.DecorationOffset);
		if (X.Ascent != Y.Ascent) return TEXT("Ascent ") + Floats(X.Ascent, Y.Ascent);
		if (X.Descent != Y.Descent) return TEXT("Descent ") + Floats(X.Descent, Y.Descent);
		if (X.PaintFragment != Y.PaintFragment) return TEXT("PaintFragment ") + Ints(X.PaintFragment, Y.PaintFragment);
		if (X.bEmit != Y.bEmit) return TEXT("bEmit");
		if (X.bCountsAsVisible != Y.bCountsAsVisible) return TEXT("bCountsAsVisible");
		FString Field = CharDataDifference(X.Glyph, Y.Glyph);
		if (!Field.IsEmpty()) return TEXT("Glyph.") + Field;
		Field = ItemStyleDifference(X.Style, Y.Style);
		if (!Field.IsEmpty()) return Field;
		Field = CharDataDifference(X.UnderlineGlyph, Y.UnderlineGlyph);
		if (!Field.IsEmpty()) return TEXT("UnderlineGlyph.") + Field;
		Field = CharDataDifference(X.StrikethroughGlyph, Y.StrikethroughGlyph);
		if (!Field.IsEmpty()) return TEXT("StrikethroughGlyph.") + Field;
		return FString();
	}

	FString BoxDifference(const TCHAR* What, const FDreamTextBox& A, const FDreamTextBox& B)
	{
		if (A == B)
		{
			return FString();
		}
		return FString::Printf(TEXT("%s (%.9g, %.9g, %.9g, %.9g) vs (%.9g, %.9g, %.9g, %.9g) as left, right, bottom, top"), What, A.Left, A.Right,
			A.Bottom, A.Top, B.Left, B.Right, B.Bottom, B.Top);
	}

	FString ItemsDifference(const TArray<FDreamTextGlyphItem>& A, const TArray<FDreamTextGlyphItem>& B)
	{
		if (A.Num() != B.Num())
		{
			return TEXT("item count ") + Ints(A.Num(), B.Num());
		}
		for (int32 i = 0; i < A.Num(); i++)
		{
			const FString Field = ItemDifference(A[i], B[i]);
			if (!Field.IsEmpty())
			{
				return FString::Printf(TEXT("item %d (U+%04X, element %d, line %d): %s"), i, B[i].Codepoint, B[i].ElementIndex, B[i].LineIndex, *Field);
			}
		}
		return FString();
	}

	FString LinesDifference(const TArray<FDreamUITextLineProperty>& A, const TArray<FDreamUITextLineProperty>& B)
	{
		if (A.Num() != B.Num())
		{
			return TEXT("line count ") + Ints(A.Num(), B.Num());
		}
		for (int32 Line = 0; Line < A.Num(); Line++)
		{
			const TArray<FDreamUITextCaretProperty>& X = A[Line].CaretPropertyList;
			const TArray<FDreamUITextCaretProperty>& Y = B[Line].CaretPropertyList;
			if (X.Num() != Y.Num())
			{
				return FString::Printf(TEXT("line %d: caret count %d vs %d"), Line, X.Num(), Y.Num());
			}
			for (int32 k = 0; k < X.Num(); k++)
			{
				if (X[k].CaretPosition != Y[k].CaretPosition || X[k].CharIndex != Y[k].CharIndex)
				{
					return FString::Printf(TEXT("line %d caret %d: (%.9g, %.9g) %d vs (%.9g, %.9g) %d"), Line, k, X[k].CaretPosition.X,
						X[k].CaretPosition.Y, X[k].CharIndex, Y[k].CaretPosition.X, Y[k].CaretPosition.Y, Y[k].CharIndex);
				}
			}
		}
		return FString();
	}

	FString ImagesDifference(const TArray<FDreamUIText_RichTextImageTag>& A, const TArray<FDreamUIText_RichTextImageTag>& B)
	{
		if (A.Num() != B.Num())
		{
			return TEXT("image count ") + Ints(A.Num(), B.Num());
		}
		for (int32 k = 0; k < A.Num(); k++)
		{
			if (A[k].TagName != B[k].TagName || A[k].Position != B[k].Position || A[k].Size != B[k].Size || A[k].TintColor != B[k].TintColor)
			{
				return FString::Printf(TEXT("image %d"), k);
			}
		}
		return FString();
	}

	FString EmojisDifference(const TArray<FDreamUIText_Emoji>& A, const TArray<FDreamUIText_Emoji>& B)
	{
		if (A.Num() != B.Num())
		{
			return TEXT("emoji count ") + Ints(A.Num(), B.Num());
		}
		for (int32 k = 0; k < A.Num(); k++)
		{
			if (A[k].EmojiCode != B[k].EmojiCode || !A[k].Sequence.Equals(B[k].Sequence, ESearchCase::CaseSensitive) || A[k].Position != B[k].Position
				|| A[k].Size != B[k].Size)
			{
				return FString::Printf(TEXT("emoji %d"), k);
			}
		}
		return FString();
	}

	FString VisualRunsDifference(const TArray<FDreamTextVisualRun>& A, const TArray<FDreamTextVisualRun>& B)
	{
		if (A.Num() != B.Num())
		{
			return TEXT("visual run count ") + Ints(A.Num(), B.Num());
		}
		for (int32 k = 0; k < A.Num(); k++)
		{
			const FDreamTextVisualRun& X = A[k];
			const FDreamTextVisualRun& Y = B[k];
			if (X.LineIndex != Y.LineIndex || X.SourceStart != Y.SourceStart || X.SourceEnd != Y.SourceEnd || X.Left != Y.Left
				|| X.Right != Y.Right || X.bRightToLeft != Y.bRightToLeft)
			{
				return FString::Printf(TEXT("visual run %d: line %d [%d, %d) %.9g..%.9g vs line %d [%d, %d) %.9g..%.9g"), k, X.LineIndex,
					X.SourceStart, X.SourceEnd, X.Left, X.Right, Y.LineIndex, Y.SourceStart, Y.SourceEnd, Y.Left, Y.Right);
			}
		}
		return FString();
	}

	FString DisplayListDifference(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B)
	{
		FString Field = ItemsDifference(A.Items, B.Items);
		if (!Field.IsEmpty()) return Field;
		Field = LinesDifference(A.Lines, B.Lines);
		if (!Field.IsEmpty()) return Field;
		if (A.CustomTags.Num() != B.CustomTags.Num() || A.CustomTagElementRanges != B.CustomTagElementRanges)
		{
			return TEXT("custom tags (count or element ranges)");
		}
		for (int32 k = 0; k < A.CustomTags.Num(); k++)
		{
			const FDreamUIText_RichTextCustomTag& X = A.CustomTags[k];
			const FDreamUIText_RichTextCustomTag& Y = B.CustomTags[k];
			if (X.TagName != Y.TagName || X.CharIndexStart != Y.CharIndexStart || X.CharIndexEnd != Y.CharIndexEnd || X.bHyperlink != Y.bHyperlink)
			{
				return FString::Printf(TEXT("custom tag %d"), k);
			}
		}
		Field = ImagesDifference(A.Images, B.Images);
		if (!Field.IsEmpty()) return Field;
		Field = EmojisDifference(A.Emojis, B.Emojis);
		if (!Field.IsEmpty()) return Field;
		Field = VisualRunsDifference(A.VisualRuns, B.VisualRuns);
		if (!Field.IsEmpty()) return Field;
		if (A.PreferredSize != B.PreferredSize)
		{
			return FString::Printf(TEXT("preferred size (%.9g, %.9g) vs (%.9g, %.9g)"), A.PreferredSize.X, A.PreferredSize.Y, B.PreferredSize.X, B.PreferredSize.Y);
		}
		if (A.bTruncated != B.bTruncated) return TEXT("bTruncated");
		if (A.bHasPendingGlyphs != B.bHasPendingGlyphs) return TEXT("bHasPendingGlyphs");
		if (A.VisibleCharCount != B.VisibleCharCount) return TEXT("VisibleCharCount ") + Ints(A.VisibleCharCount, B.VisibleCharCount);
		if (A.ElementCount != B.ElementCount) return TEXT("ElementCount ") + Ints(A.ElementCount, B.ElementCount);
		Field = BoxDifference(TEXT("ContentBox"), A.ContentBox, B.ContentBox);
		if (!Field.IsEmpty()) return Field;
		Field = BoxDifference(TEXT("TextBlockBox"), A.TextBlockBox, B.TextBlockBox);
		if (!Field.IsEmpty()) return Field;
		if (A.LineBoxes.Num() != B.LineBoxes.Num()) return TEXT("line box count ") + Ints(A.LineBoxes.Num(), B.LineBoxes.Num());
		for (int32 k = 0; k < A.LineBoxes.Num(); k++)
		{
			Field = BoxDifference(*FString::Printf(TEXT("LineBoxes[%d]"), k), A.LineBoxes[k], B.LineBoxes[k]);
			if (!Field.IsEmpty()) return Field;
		}
		if (A.PaintNames != B.PaintNames) return TEXT("PaintNames");
		if (A.PaintFragments.Num() != B.PaintFragments.Num()) return TEXT("paint fragment count ") + Ints(A.PaintFragments.Num(), B.PaintFragments.Num());
		for (int32 k = 0; k < A.PaintFragments.Num(); k++)
		{
			const FDreamTextPaintFragment& X = A.PaintFragments[k];
			const FDreamTextPaintFragment& Y = B.PaintFragments[k];
			if (X.PaintIndex != Y.PaintIndex || X.LineIndex != Y.LineIndex || X.RunOffset != Y.RunOffset || X.RunWidth != Y.RunWidth)
			{
				return FString::Printf(TEXT("paint fragment %d: paint %d line %d offset %.9g width %.9g vs paint %d line %d offset %.9g width %.9g"), k,
					X.PaintIndex, X.LineIndex, X.RunOffset, X.RunWidth, Y.PaintIndex, Y.LineIndex, Y.RunOffset, Y.RunWidth);
			}
			Field = BoxDifference(*FString::Printf(TEXT("PaintFragments[%d].Box"), k), X.Box, Y.Box);
			if (!Field.IsEmpty()) return Field;
		}
		return FString();
	}

	FString MeasuredDifference(const FMeasured& A, const FMeasured& B)
	{
		FString Field = CharDataDifference(A.Glyph, B.Glyph);
		if (!Field.IsEmpty()) return TEXT("Glyph.") + Field;
		if (!SameParseResult(A.Style, B.Style)) return TEXT("Style");
		if (A.Advance != B.Advance) return TEXT("Advance ") + Floats(A.Advance, B.Advance);
		if (A.ClusterAdvance != B.ClusterAdvance) return TEXT("ClusterAdvance ") + Floats(A.ClusterAdvance, B.ClusterAdvance);
		if (A.LetterSpacing != B.LetterSpacing) return TEXT("LetterSpacing ") + Floats(A.LetterSpacing, B.LetterSpacing);
		if (A.ClusterFitWidth != B.ClusterFitWidth) return TEXT("ClusterFitWidth ") + Floats(A.ClusterFitWidth, B.ClusterFitWidth);
		if (A.GlyphStart != B.GlyphStart) return TEXT("GlyphStart ") + Ints(A.GlyphStart, B.GlyphStart);
		if (A.GlyphCount != B.GlyphCount) return TEXT("GlyphCount ") + Ints(A.GlyphCount, B.GlyphCount);
		if (A.RunIndex != B.RunIndex) return TEXT("RunIndex ") + Ints(A.RunIndex, B.RunIndex);
		if (A.FaceIndex != B.FaceIndex) return TEXT("FaceIndex ") + Ints(A.FaceIndex, B.FaceIndex);
		if (A.BidiLevel != B.BidiLevel) return TEXT("BidiLevel ") + Ints(A.BidiLevel, B.BidiLevel);
		if (A.bBaseRightToLeft != B.bBaseRightToLeft) return TEXT("bBaseRightToLeft");
		if (A.bHardBreak != B.bHardBreak) return TEXT("bHardBreak");
		if (A.bSkipped != B.bSkipped) return TEXT("bSkipped");
		if (A.bWhitespace != B.bWhitespace) return TEXT("bWhitespace");
		if (A.bTab != B.bTab) return TEXT("bTab");
		if (A.bImageSpace != B.bImageSpace) return TEXT("bImageSpace");
		if (A.bEmoji != B.bEmoji) return TEXT("bEmoji");
		if (A.EmojiItem != B.EmojiItem) return TEXT("EmojiItem");
		if (A.LanguageIndex != B.LanguageIndex) return TEXT("LanguageIndex ") + Ints(A.LanguageIndex, B.LanguageIndex);
		if (A.bVisibleGlyph != B.bVisibleGlyph) return TEXT("bVisibleGlyph");
		if (A.bShapeClusterStart != B.bShapeClusterStart) return TEXT("bShapeClusterStart");
		if (A.bClusterStart != B.bClusterStart) return TEXT("bClusterStart");
		if (A.bCaretStop != B.bCaretStop) return TEXT("bCaretStop");
		if (A.bSyntheticBold != B.bSyntheticBold) return TEXT("bSyntheticBold");
		if (A.bSyntheticItalic != B.bSyntheticItalic) return TEXT("bSyntheticItalic");
		if (A.Script != B.Script) return FString::Printf(TEXT("Script %08x vs %08x"), A.Script, B.Script);
		if (A.bOwnScript != B.bOwnScript) return TEXT("bOwnScript");
		if (A.bSegmentStart != B.bSegmentStart) return TEXT("bSegmentStart");
		if (A.PenAfter != B.PenAfter) return TEXT("PenAfter ") + Floats(A.PenAfter, B.PenAfter);
		return FString();
	}

	FString GlyphSourceDifference(const FGlyphSource& A, const FGlyphSource& B)
	{
		const FString Field = CharDataDifference(A.Quad, B.Quad);
		if (!Field.IsEmpty()) return TEXT("Quad.") + Field;
		if (A.XAdvance != B.XAdvance) return TEXT("XAdvance ") + Floats(A.XAdvance, B.XAdvance);
		if (A.XOffset != B.XOffset) return TEXT("XOffset ") + Floats(A.XOffset, B.XOffset);
		if (A.YOffset != B.YOffset) return TEXT("YOffset ") + Floats(A.YOffset, B.YOffset);
		if (A.ElementIndex != B.ElementIndex) return TEXT("ElementIndex ") + Ints(A.ElementIndex, B.ElementIndex);
		if (A.GlyphSize != B.GlyphSize) return TEXT("GlyphSize ") + Floats(A.GlyphSize, B.GlyphSize);
		if (A.RasterFace != B.RasterFace) return TEXT("RasterFace ") + Ints(A.RasterFace, B.RasterFace);
		if (A.RasterGlyph != B.RasterGlyph) return TEXT("RasterGlyph ") + Ints(A.RasterGlyph, B.RasterGlyph);
		if (A.bRasterBold != B.bRasterBold) return TEXT("bRasterBold");
		if (A.bRasterColorFace != B.bRasterColorFace) return TEXT("bRasterColorFace");
		return FString();
	}

	/**
	 * A bit array's first difference, the bits at newlines left out: what a boundary or a break before a newline is never
	 * read (a line ends at its newline), and an analysis of one paragraph sets it from that paragraph alone.
	 */
	FString BitsDifference(const TCHAR* What, const TBitArray<>& A, const TBitArray<>& B, const TArray<FMeasured>& MeasuredA)
	{
		if (A.Num() != B.Num())
		{
			return FString::Printf(TEXT("%s count %d vs %d"), What, A.Num(), B.Num());
		}
		for (int32 i = 0; i < A.Num(); i++)
		{
			if ((bool)A[i] != (bool)B[i] && !(MeasuredA.IsValidIndex(i) && MeasuredA[i].bHardBreak))
			{
				return FString::Printf(TEXT("%s[%d] %d vs %d"), What, i, (int32)(bool)A[i], (int32)(bool)B[i]);
			}
		}
		return FString();
	}

	FString ParagraphDifference(const FParagraph& A, const FParagraph& B)
	{
		if (A.Start != B.Start) return TEXT("Start ") + Ints(A.Start, B.Start);
		if (A.End != B.End) return TEXT("End ") + Ints(A.End, B.End);
		if (A.Next != B.Next) return TEXT("Next ") + Ints(A.Next, B.Next);
		if (A.GlyphStart != B.GlyphStart) return TEXT("GlyphStart ") + Ints(A.GlyphStart, B.GlyphStart);
		if (A.GlyphEnd != B.GlyphEnd) return TEXT("GlyphEnd ") + Ints(A.GlyphEnd, B.GlyphEnd);
		if (A.RunStart != B.RunStart) return TEXT("RunStart ") + Ints(A.RunStart, B.RunStart);
		if (A.RunEnd != B.RunEnd) return TEXT("RunEnd ") + Ints(A.RunEnd, B.RunEnd);
		if (A.LineStart != B.LineStart) return TEXT("LineStart ") + Ints(A.LineStart, B.LineStart);
		if (A.LineEnd != B.LineEnd) return TEXT("LineEnd ") + Ints(A.LineEnd, B.LineEnd);
		if (A.PreferredWidth != B.PreferredWidth) return TEXT("PreferredWidth ") + Floats(A.PreferredWidth, B.PreferredWidth);
		if (A.bHasPreferredWidth != B.bHasPreferredWidth) return TEXT("bHasPreferredWidth");
		if (A.bHasTab != B.bHasTab) return TEXT("bHasTab");
		if (A.bPendingQuads != B.bPendingQuads) return TEXT("bPendingQuads");
		if (A.TabCount != B.TabCount) return TEXT("TabCount ") + Ints(A.TabCount, B.TabCount);
		if (A.RtlCount != B.RtlCount) return TEXT("RtlCount ") + Ints(A.RtlCount, B.RtlCount);
		if (A.ObjectCount != B.ObjectCount) return TEXT("ObjectCount ") + Ints(A.ObjectCount, B.ObjectCount);
		if (A.PendingGlyphs != B.PendingGlyphs) return TEXT("PendingGlyphs ") + Ints(A.PendingGlyphs, B.PendingGlyphs);
		if (A.bHashed && B.bHashed && (A.MeasureHash != B.MeasureHash || A.PlaceHash != B.PlaceHash)) return TEXT("hashes");
		return FString();
	}

	FString LineSpanDifference(const FLineSpan& A, const FLineSpan& B)
	{
		if (A.ItemStart != B.ItemStart || A.ItemEnd != B.ItemEnd) return FString::Printf(TEXT("items [%d, %d) vs [%d, %d)"), A.ItemStart, A.ItemEnd, B.ItemStart, B.ItemEnd);
		if (A.ImageStart != B.ImageStart || A.ImageEnd != B.ImageEnd) return TEXT("images");
		if (A.EmojiStart != B.EmojiStart || A.EmojiEnd != B.EmojiEnd) return TEXT("emojis");
		if (A.VisualRunStart != B.VisualRunStart || A.VisualRunEnd != B.VisualRunEnd) return TEXT("visual runs");
		if (A.CaretStart != B.CaretStart || A.CaretEnd != B.CaretEnd) return FString::Printf(TEXT("carets [%d, %d) vs [%d, %d)"), A.CaretStart, A.CaretEnd, B.CaretStart, B.CaretEnd);
		if (A.Top != B.Top) return TEXT("Top ") + Floats(A.Top, B.Top);
		if (A.Height != B.Height) return TEXT("Height ") + Floats(A.Height, B.Height);
		if (A.bPending != B.bPending) return TEXT("bPending");
		if (A.bLastLine != B.bLastLine) return TEXT("bLastLine");
		if (A.VisibleCount != B.VisibleCount) return TEXT("VisibleCount ") + Ints(A.VisibleCount, B.VisibleCount);
		if (A.FinishX != B.FinishX) return TEXT("FinishX ") + Floats(A.FinishX, B.FinishX);
		if (A.FinishY != B.FinishY) return TEXT("FinishY ") + Floats(A.FinishY, B.FinishY);
		return FString();
	}

	template <typename T>
	FString VectorsDifference(const TCHAR* What, const TArray<T>& A, const TArray<T>& B)
	{
		if (A.Num() != B.Num())
		{
			return FString::Printf(TEXT("%s count %d vs %d"), What, A.Num(), B.Num());
		}
		for (int32 i = 0; i < A.Num(); i++)
		{
			if (A[i].X != B[i].X || A[i].Y != B[i].Y)
			{
				return FString::Printf(TEXT("%s[%d] (%.9g, %.9g) vs (%.9g, %.9g)"), What, i, (double)A[i].X, (double)A[i].Y, (double)B[i].X, (double)B[i].Y);
			}
		}
		return FString();
	}

	FString StateDifference(const FDreamTextLayoutStateData& A, const FDreamTextLayoutStateData& B)
	{
		if (A.bValid != B.bValid) return TEXT("bValid");
		if (!A.bValid) return FString();
		if (!(A.MeasureKey == B.MeasureKey)) return TEXT("measure key");
		if (!(A.Input == B.Input) || A.Input.Color != B.Input.Color) return TEXT("input");
		if (A.ContentLength != B.ContentLength) return TEXT("ContentLength ") + Ints(A.ContentLength, B.ContentLength);
		// The faces one remembers drawing from beyond the other's only make a later layout check a face more.
		const FDreamTextLayoutStateData& Fewer = A.UsedFaces.Num() <= B.UsedFaces.Num() ? A : B;
		const FDreamTextLayoutStateData& More = A.UsedFaces.Num() <= B.UsedFaces.Num() ? B : A;
		for (int32 k = 0; k < Fewer.UsedFaces.Num(); k++)
		{
			const int32 Found = More.UsedFaces.Find(Fewer.UsedFaces[k]);
			if (Found == INDEX_NONE || !(More.UsedFaceIdentities[Found] == Fewer.UsedFaceIdentities[k]))
			{
				return FString::Printf(TEXT("used face %d"), Fewer.UsedFaces[k]);
			}
		}
		if (A.Elements.Num() != B.Elements.Num()) return TEXT("element count ") + Ints(A.Elements.Num(), B.Elements.Num());
		for (int32 i = 0; i < A.Elements.Num(); i++)
		{
			const FDreamUIText_TextProcessingElement& X = A.Elements[i];
			const FDreamUIText_TextProcessingElement& Y = B.Elements[i];
			if (X.Unicode != Y.Unicode || X.StringIndex != Y.StringIndex || X.Length != Y.Length || X.Type != Y.Type || X.bEscaped != Y.bEscaped)
			{
				return FString::Printf(TEXT("element %d: U+%04X at %d, %d long vs U+%04X at %d, %d long"), i, X.Unicode, X.StringIndex, X.Length,
					Y.Unicode, Y.StringIndex, Y.Length);
			}
		}
		if (!A.PlainText.Equals(B.PlainText, ESearchCase::CaseSensitive)) return TEXT("plain text");
		if (A.PlainStart != B.PlainStart) return TEXT("plain text starts");
		if (A.ElementCodepoints != B.ElementCodepoints) return TEXT("element code points");
		FString Field = BitsDifference(TEXT("FollowsMarkup"), A.FollowsMarkup, B.FollowsMarkup, TArray<FMeasured>());
		if (!Field.IsEmpty()) return Field;
		if (A.Measured.Num() != B.Measured.Num()) return TEXT("measured count ") + Ints(A.Measured.Num(), B.Measured.Num());
		for (int32 i = 0; i < A.Measured.Num(); i++)
		{
			Field = MeasuredDifference(A.Measured[i], B.Measured[i]);
			if (!Field.IsEmpty())
			{
				return FString::Printf(TEXT("element %d (U+%04X): %s"), i, A.Elements[i].Unicode, *Field);
			}
		}
		if (A.Glyphs.Num() != B.Glyphs.Num()) return TEXT("glyph count ") + Ints(A.Glyphs.Num(), B.Glyphs.Num());
		for (int32 g = 0; g < A.Glyphs.Num(); g++)
		{
			Field = GlyphSourceDifference(A.Glyphs[g], B.Glyphs[g]);
			if (!Field.IsEmpty())
			{
				return FString::Printf(TEXT("glyph %d (element %d): %s"), g, B.Glyphs[g].ElementIndex, *Field);
			}
		}
		Field = BitsDifference(TEXT("GraphemeStart"), A.GraphemeStart, B.GraphemeStart, A.Measured);
		if (!Field.IsEmpty()) return Field;
		Field = BitsDifference(TEXT("ClusterStarts"), A.ClusterStarts, B.ClusterStarts, A.Measured);
		if (!Field.IsEmpty()) return Field;
		if (A.bHasBreakBits != B.bHasBreakBits || A.bHasWordBits != B.bHasWordBits) return TEXT("which break bits are kept");
		if (A.bHasBreakBits)
		{
			Field = BitsDifference(TEXT("LineBreakRaw"), A.LineBreakRaw, B.LineBreakRaw, A.Measured);
			if (!Field.IsEmpty()) return Field;
			Field = BitsDifference(TEXT("CanBreakBefore"), A.CanBreakBefore, B.CanBreakBefore, A.Measured);
			if (!Field.IsEmpty()) return Field;
		}
		if (A.bHasWordBits)
		{
			Field = BitsDifference(TEXT("WordBreakRaw"), A.WordBreakRaw, B.WordBreakRaw, A.Measured);
			if (!Field.IsEmpty()) return Field;
		}
		if (A.Paragraphs.Num() != B.Paragraphs.Num()) return TEXT("paragraph count ") + Ints(A.Paragraphs.Num(), B.Paragraphs.Num());
		for (int32 p = 0; p < A.Paragraphs.Num(); p++)
		{
			Field = ParagraphDifference(A.Paragraphs[p], B.Paragraphs[p]);
			if (!Field.IsEmpty()) return FString::Printf(TEXT("paragraph %d: %s"), p, *Field);
		}
		if (A.PendingParagraphs != B.PendingParagraphs) return TEXT("PendingParagraphs ") + Ints(A.PendingParagraphs, B.PendingParagraphs);
		if (A.LineRanges.Num() != B.LineRanges.Num()) return TEXT("line range count ") + Ints(A.LineRanges.Num(), B.LineRanges.Num());
		for (int32 l = 0; l < A.LineRanges.Num(); l++)
		{
			const FLineRange& X = A.LineRanges[l];
			const FLineRange& Y = B.LineRanges[l];
			if (X.Start != Y.Start || X.End != Y.End || X.HardBreakElement != Y.HardBreakElement || X.DecidedAt != Y.DecidedAt)
			{
				return FString::Printf(TEXT("line range %d: [%d, %d) break %d decided at %d vs [%d, %d) break %d decided at %d"), l, X.Start, X.End,
					X.HardBreakElement, X.DecidedAt, Y.Start, Y.End, Y.HardBreakElement, Y.DecidedAt);
			}
		}
		if (A.TagRecords != B.TagRecords) return TEXT("tag records");
		if (!SameParseResult(A.EndStyle, B.EndStyle)) return TEXT("EndStyle");
		if (A.bEndFollowsMarkup != B.bEndFollowsMarkup) return TEXT("bEndFollowsMarkup");
		if (A.Languages.Num() != B.Languages.Num()) return TEXT("language count ") + Ints(A.Languages.Num(), B.Languages.Num());
		for (int32 k = 0; k < A.Languages.Num(); k++)
		{
			if (!A.Languages[k].Name.Equals(B.Languages[k].Name, ESearchCase::CaseSensitive))
			{
				return FString::Printf(TEXT("language %d: %s vs %s"), k, *A.Languages[k].Name, *B.Languages[k].Name);
			}
		}
		if (A.LanguageIndexByTag.Num() != B.LanguageIndexByTag.Num()) return TEXT("language tags");
		for (const TPair<FName, uint8>& Pair : A.LanguageIndexByTag)
		{
			const uint8* Found = B.LanguageIndexByTag.Find(Pair.Key);
			if (Found == nullptr || *Found != Pair.Value)
			{
				return TEXT("language tag ") + Pair.Key.ToString();
			}
		}
		if (A.InlineObjects != B.InlineObjects) return TEXT("inline objects");
		// The lines as placed, in the form both kept them.
		if (A.bLinesKept != B.bLinesKept || A.bLocalLinesKept != B.bLocalLinesKept) return TEXT("which form of the lines is kept");
		if (A.LineSpans.Num() != B.LineSpans.Num()) return TEXT("line span count ") + Ints(A.LineSpans.Num(), B.LineSpans.Num());
		for (int32 l = 0; l < A.LineSpans.Num(); l++)
		{
			Field = LineSpanDifference(A.LineSpans[l], B.LineSpans[l]);
			if (!Field.IsEmpty()) return FString::Printf(TEXT("line span %d: %s"), l, *Field);
		}
		if (A.bLinesKept)
		{
			Field = ItemsDifference(A.Items, B.Items);
			if (!Field.IsEmpty()) return TEXT("kept copy: ") + Field;
			Field = LinesDifference(A.Lines, B.Lines);
			if (!Field.IsEmpty()) return TEXT("kept copy: ") + Field;
			Field = ImagesDifference(A.Images, B.Images);
			if (!Field.IsEmpty()) return TEXT("kept copy: ") + Field;
			Field = EmojisDifference(A.Emojis, B.Emojis);
			if (!Field.IsEmpty()) return TEXT("kept copy: ") + Field;
			Field = VisualRunsDifference(A.VisualRuns, B.VisualRuns);
			if (!Field.IsEmpty()) return TEXT("kept copy: ") + Field;
		}
		if (A.bLocalLinesKept)
		{
			Field = VectorsDifference(TEXT("LocalPens"), A.LocalPens, B.LocalPens);
			if (!Field.IsEmpty()) return Field;
			Field = VectorsDifference(TEXT("LocalCarets"), A.LocalCarets, B.LocalCarets);
			if (!Field.IsEmpty()) return Field;
			Field = VectorsDifference(TEXT("LocalImages"), A.LocalImages, B.LocalImages);
			if (!Field.IsEmpty()) return Field;
			Field = VectorsDifference(TEXT("LocalEmojis"), A.LocalEmojis, B.LocalEmojis);
			if (!Field.IsEmpty()) return Field;
			Field = VectorsDifference(TEXT("LocalRuns"), A.LocalRuns, B.LocalRuns);
			if (!Field.IsEmpty()) return Field;
		}
		return FString();
	}
}

bool FDreamTextLayoutEngine::DebugCompareStates(const FDreamTextLayoutState& A, const FDreamTextLayoutState& B, FString& OutDifference)
{
	OutDifference = DreamTextLayoutDebugLocal::StateDifference(*A.Data, *B.Data);
	return OutDifference.IsEmpty();
}

bool FDreamTextLayoutEngine::DebugCompareDisplayLists(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B, FString& OutDifference)
{
	OutDifference = DreamTextLayoutDebugLocal::DisplayListDifference(A, B);
	return OutDifference.IsEmpty();
}
#endif
