// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextLayout.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIRichTextImageData_BaseObject.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/FRichTextParser.h"
#include "Core/Text/DreamTextBreaker.h"
#include "Core/Text/DreamTextShaper.h"
#include "Algo/Reverse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	 * One layout pass, in the order a browser's inline formatting context does it: measure every
	 * element, decide where the lines break, place the lines, then align the paragraph. Measuring
	 * first is what makes the breaker a pure function over widths.
	 */
	class FLayoutRun
	{
	public:
		FLayoutRun(const FDreamTextLayoutInput& InInput, FDreamTextDisplayList& InOut)
			: In(InInput), Out(InOut)
		{
		}

		void Run();

	private:
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
			bool bImageSpace = false;
			/** An emoji drawn by the font's emoji data as an inline object. One the emoji data lacks but a face has is a glyph. */
			bool bEmoji = false;
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
		};

		/** A line as the breaker decided it: a half-open element range and what ended it. */
		struct FLineRange
		{
			int32 Start = 0;
			int32 End = 0;
			/** The newline element that ended this line, or -1 for a soft break / the end of the text. */
			int32 HardBreakElement = -1;
		};

		/** A rich-text custom tag as the source has it: where it opened and where it closed, in elements. */
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
		};

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
		FDreamTextDisplayList& Out;

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

		/** A glyph ready to place: its atlas quad and how the shaper positioned it. */
		struct FGlyphSource
		{
			FDreamUICharData Quad;
			float XAdvance = 0.0f;
			float XOffset = 0.0f;
			float YOffset = 0.0f;
			int32 ElementIndex = 0;
		};
		/** A shaped run: a range of Glyphs in visual order, and the elements it covers. */
		struct FRunInfo
		{
			int32 GlyphStart = 0;
			int32 GlyphEnd = 0;
			int32 ElementStart = 0;
			int32 ElementEnd = 0;
			bool bRightToLeft = false;
		};
		TArray<FGlyphSource> Glyphs;
		TArray<FRunInfo> Runs;

		TArray<FMeasured> Measured;
		/** The text as laid out -- markup stripped, placeholders as spaces -- and where each element starts in it. */
		FString PlainText;
		TArray<int32> PlainStart;
		TArray<uint32> ElementCodepoints;
		TBitArray<> GraphemeStart;
		TBitArray<> ClusterStarts;
		TBitArray<> CanBreakBefore;
		TArray<FLineRange> LineRanges;

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
		/** The line being placed: its clusters in visual order, each element's caret, and its ellipsis item. */
		TArray<FPlaced> LinePlaced;
		TArray<float> LineCaretX;
		TBitArray<> LineHasCaret;
		int32 LineDotsItem = INDEX_NONE;

		void Prepare();
		void Preprocess();
		void BuildPlainText();
		void Measure();
		void MeasureParagraphByCodepoint(int32 Start, int32 End);
		bool MeasureParagraphByShaping(int32 Start, int32 End);
		/** Groups a paragraph's elements into clusters and gives each its letter spacing and width. */
		void FinishClusters(int32 Start, int32 End);
		float LetterSpacingFor(int32 ElementIndex) const;
		FDreamUICharData FetchGlyphQuad(int32 FaceIndex, uint32 GlyphIndex, float InFontSize, bool bInBold) const;
		bool CanDrawEmojiAsGlyph(uint32 Codepoint);
		void ComputeBreakOpportunities();
		void BreakLines();
		void Place();
		void PlaceLine(int32 LineIndex, float LineTop);
		/** Places one cluster at the pen: its items, its inline object, the carets of its grapheme clusters. */
		void PlaceCluster(int32 Start, int32 End, bool bRightToLeft, uint8 Level, bool bTrailing, int32 LineIndex, int32 RangeStart,
			float& InOutPenX, float Baseline, const FLineBox& Box, float LineCentre);
		/**
		 * Truncate and Ellipsis on a placed line, measured against the box rather than the wrap width, because they are
		 * about what fits on screen. A line is cut at the end it reads towards -- the right of a left-to-right line, the
		 * left of a right-to-left one, where Slate puts the ellipsis too -- and bForceEllipsis ends the line with one
		 * whether or not it overflows (the last line that fits the box vertically).
		 */
		void ClampLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline, bool bForceEllipsis,
			int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty);
		/** Hides a cluster a clamp removed: its glyphs stop counting, its strokes and its inline object go. */
		void CutCluster(FPlaced& Placed, TArray<int32>& InOutImagesToRemove, TArray<int32>& InOutEmojisToRemove);
		void RemoveInlineObjects(TArray<int32>& ImagesToRemove, TArray<int32>& EmojisToRemove);
		/**
		 * The ellipsis in the style of the text it ends: that text's size and weight, from the face that style chooses for
		 * it. Says whether the glyph is still rasterizing, and what of bold and italic that face lacks and has to be made up.
		 */
		FDreamUICharData MakeEllipsisGlyph(const FMeasured& StyleElement, bool& bOutPending, bool& bOutSyntheticBold, bool& bOutSyntheticItalic);
		/** Slides everything a line owns -- items, carets, inline objects, visual runs -- by the same amount. */
		void ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 VisualRunStart, FDreamUITextLineProperty& LineProperty, float XOffset);
		void Finish();
		float ComputePreferredWidth() const;
		bool IsLineBaseRightToLeft(const FLineRange& Range) const;

		bool IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		void GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FRichTextParseResult& RichTextResult) const;
		void GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, uint32 EmojiCode) const;
		FDreamUICharData GetCharGeo(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, float InFontSize, bool bInBold, const FRichTextParseResult& RichTextResult, bool bEmojiPlaceholder) const;
		/** The underline ('_') or strikethrough ('-') an item of this element is drawn with, measured from a pen raised by GlyphYOffset. */
		FDreamUICharData GetDecorationGlyph(bool bStrikethrough, const FMeasured& M, float GlyphYOffset, bool& bOutPending);
		/** Gives an item the strokes its style asks for, or takes them away while their glyph is still rasterizing. */
		void AssignDecorations(FDreamTextGlyphItem& Item, const FMeasured& M, float GlyphYOffset);
		static FDreamTextItemStyle MakeStyle(const FMeasured& M);
		/** The left-most and right-most x the painter will write for an item's glyph, sheared if italic. */
		void ItemInkExtent(const FDreamTextGlyphItem& Item, float& OutLeft, float& OutRight) const;
		int32 CaretIndexOf(int32 ElementIndex) const;
		/** True when lines wrap: the VerticalOverflow policy, or UMG-style AutoWrapText on top of another. */
		bool ShouldWrap() const { return In.OverflowType == EDreamUITextOverflowType::VerticalOverflow || In.bAutoWrapText; }
		/** True when the policy cuts what does not fit rather than letting it hang out. */
		bool IsClampMode() const { return In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis; }
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

	void FLayoutRun::GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, uint32 EmojiCode) const
	{
		// As tall as the font, in text units, and as wide as the emoji's own aspect ratio makes it.
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = InFontSize;

		FIntVector2 ImageSize;
		if (IsValid(EmojiData) && EmojiData->GetImageSize(EmojiCode, ImageSize) && ImageSize.Y != 0)
		{
			const float Ratio = (float)ImageSize.X / ImageSize.Y;
			OverrideCharData.Width = OverrideCharData.Width * Ratio;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * Ratio;
		}
	}

	FDreamUICharData FLayoutRun::GetCharGeo(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, float InFontSize, bool bInBold, const FRichTextParseResult& RichTextResult, bool bEmojiPlaceholder) const
	{
		// Inline objects are not glyphs. They are sized in text units, from the font size or the size the tag asked
		// for, so no raster scale applies to them -- passing them the rasterized size made them DynamicPixelsPerUnit
		// times too big in world space -- and they never kern.
		if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
		{
			FDreamUICharData ImageData;
			GetRichTextImageCharData(ImageData, InFontSize, RichTextResult);
			return ImageData;
		}
		if (bEmojiPlaceholder)
		{
			FDreamUICharData EmojiCharData;
			GetEmojiCharData(EmojiCharData, InFontSize, CharElement.Unicode);
			return EmojiCharData;
		}

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
			OverrideCharData = Font->GetCharData(CharElement.Unicode, InFontSize, bInBold);
			OverrideCharData.Width = OverrideCharData.Width * OneDivideScale;
			OverrideCharData.Height = OverrideCharData.Height * OneDivideScale;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * OneDivideScale;
			OverrideCharData.XOffset = OverrideCharData.XOffset * OneDivideScale;
			OverrideCharData.YOffset = OverrideCharData.YOffset * OneDivideScale;
			BackToTextUnits = OneDivideScale;
		}
		else
		{
			OverrideCharData = Font->GetCharData(CharElement.Unicode, InFontSize, bInBold);
		}
		// PrevCharCode == 0 is "no left neighbour": the caller says so explicitly rather than passing the
		// character itself, which used to mean every doubled pair ("TT", "ll", "//") lost its kerning.
		if (bUseKerning && PrevCharCode != 0)
		{
			// Kerning comes back at the size the glyph was measured at, so it converts back the same way.
			const float KerningValue = Font->GetKerning(PrevCharCode, CharElement.Unicode, InFontSize) * BackToTextUnits;
			OverrideCharData.XAdvance += KerningValue;
			OverrideCharData.XOffset += KerningValue;
		}
		return OverrideCharData;
	}

	FDreamUICharData FLayoutRun::GetDecorationGlyph(bool bStrikethrough, const FMeasured& M, float GlyphYOffset, bool& bOutPending)
	{
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
				Out.bHasPendingGlyphs = true;
				Item.Style.bUnderline = false;
			}
		}
		if (Item.Style.bStrikethrough)
		{
			bool bPending = false;
			Item.StrikethroughGlyph = GetDecorationGlyph(true, M, GlyphYOffset, bPending);
			if (bPending)
			{
				Out.bHasPendingGlyphs = true;
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
		return Style;
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
							StyleItem->ApplyToRichTextParseResult(Style, Record.Order);
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
				if (CharIndex >= ContentLength)break;

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
					Element.Type = FDreamUIText_CodePoint::IsEmoji(EscapedCodepoint)
						? EDreamUIText_CodeType::Emoji : EDreamUIText_CodeType::Text;
					Element.bEscaped = true;
					TextProcessingArray.Add(Element);
					CharIndex += EscapeLength - 1;//the loop's own step takes the last unit
					continue;
				}
				TextProcessingArray.Add(FDreamUIText_CodePoint::ReadCodePoint(In.Content, ContentLength, CharIndex));
			}
		}
		else
		{
			for (int32 CharIndex = 0; CharIndex < ContentLength; CharIndex++)
			{
				TextProcessingArray.Add(FDreamUIText_CodePoint::ReadCodePoint(In.Content, ContentLength, CharIndex));
			}
			FollowsMarkup.Init(false, TextProcessingArray.Num());
		}
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
		// a unit or more short of its character, and an edit there landed inside the emoji.
		return In.bRichText ? RichTextPropertyArray[ElementIndex].CharIndex : TextProcessingArray[ElementIndex].StringIndex;
	}

	bool FLayoutRun::CanDrawEmojiAsGlyph(uint32 Codepoint)
	{
		// The emoji data draws an emoji it has. One it does not have is drawn by a face that has the code point, as
		// a glyph: monochrome, but a blank where the emoji should be tells the reader nothing at all.
		if (IsValid(EmojiData))
		{
			FIntVector2 ImageSize;
			if (EmojiData->GetImageSize(Codepoint, ImageSize))
			{
				return false;
			}
		}
		const int32 FaceCount = Font->GetFaceCount();
		for (int32 F = 0; F < FaceCount; F++)
		{
			if (Font->FaceHasCodepoint(F, Codepoint))
			{
				return true;
			}
		}
		return false;
	}

	void FLayoutRun::Measure()
	{
		const int32 Count = TextProcessingArray.Num();
		Measured.SetNum(Count);
		Glyphs.Reset();
		Runs.Reset();
		// Classification first, then metrics paragraph by paragraph: a paragraph is the unit the
		// shaper sees, so nothing kerns or forms across a hard break.
		for (int32 i = 0; i < Count; i++)
		{
			const auto& Element = TextProcessingArray[i];
			FMeasured& M = Measured[i];
			M.Style = In.bRichText ? RichTextPropertyArray[i] : RichTextParseResult;
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
						i++;
					}
				}
				continue;
			}
			M.bImageSpace = IsRichTextImageSpace(Code, M.Style);
			M.bEmoji = Element.Type == EDreamUIText_CodeType::Emoji && !CanDrawEmojiAsGlyph(Code);
			M.bWhitespace = !M.bImageSpace && (Code == ' ' || Code == '\t');
			M.bVisibleGlyph = !M.bImageSpace && !M.bEmoji && !M.bWhitespace;
		}

		bCanShape = FDreamTextShaper::CanShape(Font);
		int32 ParagraphStart = 0;
		for (int32 i = 0; i <= Count; i++)
		{
			if (i == Count || Measured[i].bHardBreak)
			{
				if (i > ParagraphStart)
				{
					if (!bCanShape || !MeasureParagraphByShaping(ParagraphStart, i))
					{
						MeasureParagraphByCodepoint(ParagraphStart, i);
					}
					FinishClusters(ParagraphStart, i);
				}
				ParagraphStart = i + 1;
			}
		}
		ClusterStarts.Init(false, Count);
		for (int32 i = 0; i < Count; i++)
		{
			ClusterStarts[i] = Measured[i].bClusterStart;
		}
	}

	void FLayoutRun::MeasureParagraphByCodepoint(int32 Start, int32 End)
	{
		// One glyph per code point, metrics straight from the font: the path for fonts that cannot
		// shape. Kerning pairs with the previous character of the paragraph; the first has none, which
		// GetCharGeo spells as a zero left neighbour.
		//
		// Direction is the shaper's job, so this path lays every paragraph out left to right and
		// FlowDirection does nothing here: without HarfBuzz there is no bidi and no reordering to
		// force. A font that cannot shape cannot draw right-to-left text correctly in the first place.
		const bool bFaceBold = EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Bold);
		const bool bFaceItalic = EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Italic);
		uint32 PrevCharCode = 0;
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			const auto& Element = TextProcessingArray[i];
			M.bSyntheticBold = M.Style.Bold && !bFaceBold;
			M.bSyntheticItalic = M.Style.Italic && !bFaceItalic;
			M.Glyph = GetCharGeo(PrevCharCode, Element, M.Style.Size, M.bSyntheticBold, M.Style, M.bEmoji);
			M.ClusterAdvance = M.Glyph.XAdvance;
			M.FaceIndex = M.Glyph.FaceIndex;
			M.RunIndex = -1;
			M.BidiLevel = 0;
			M.bBaseRightToLeft = false;
			M.bShapeClusterStart = true;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 1;
			FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
			G.Quad = M.Glyph;
			G.XAdvance = M.Glyph.XAdvance;
			G.ElementIndex = i;
			// An inline object is no character to kern the next one against.
			PrevCharCode = (M.bImageSpace || M.bEmoji) ? 0 : Element.Unicode;
		}
	}

	FDreamUICharData FLayoutRun::FetchGlyphQuad(int32 FaceIndex, uint32 GlyphIndex, float InFontSize, bool bInBold) const
	{
		// The same canvas-scale dance GetCharGeo does for code points: rasterize at the device size and
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
			const float ScaledSize = FMath::Clamp(WantedSize, 0.0f, MaxFontSize);
			// Same as GetCharGeo: once the font's raster cap clamps the size, the measurement has to be
			// divided by the ratio that was achieved, not by the one that was asked for.
			if (ScaledSize > 0.0f && WantedSize > ScaledSize)
			{
				OneDivideScale = OneDivideScale * (WantedSize / ScaledSize);
			}
			FDreamUICharData Data = Font->GetGlyphData(FaceIndex, GlyphIndex, ScaledSize, bInBold);
			Data.Width *= OneDivideScale;
			Data.Height *= OneDivideScale;
			Data.XAdvance *= OneDivideScale;
			Data.XOffset *= OneDivideScale;
			Data.YOffset *= OneDivideScale;
			return Data;
		}
		return Font->GetGlyphData(FaceIndex, GlyphIndex, InFontSize, bInBold);
	}

	bool FLayoutRun::MeasureParagraphByShaping(int32 Start, int32 End)
	{
		TArray<FDreamShapeElement> ShapeElements;
		ShapeElements.Reserve(End - Start);
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
			E.bRunBreakBefore = bLigatures && i > Start && E.bGraphemeStart && FollowsMarkup.IsValidIndex(i) && FollowsMarkup[i];
			ShapeElements.Add(E);
		}
		TArray<FDreamShapedRun> ShapedRuns;
		TArray<uint8> Levels;
		bool bBaseRightToLeft = false;
		if (!FDreamTextShaper::ShapeParagraph(ShapeElements, Font, bUseKerning, In.FlowDirection, ShapedRuns, bBaseRightToLeft, bLigatures, &Levels))
		{
			return false;
		}

		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			M.bBaseRightToLeft = bBaseRightToLeft;
			M.BidiLevel = Levels.IsValidIndex(i - Start) ? Levels[i - Start] : (bBaseRightToLeft ? 1 : 0);
			M.RunIndex = -1;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 0;
			M.ClusterAdvance = 0.0f;
			M.Advance = 0.0f;
			M.bShapeClusterStart = true;
			if (M.bImageSpace || M.bEmoji)
			{
				// Inline objects are measured by the layout, the way they always were, and never kern.
				M.Glyph = GetCharGeo(0, TextProcessingArray[i], M.Style.Size, false, M.Style, M.bEmoji);
				M.ClusterAdvance = M.Glyph.XAdvance;
			}
		}

		for (const FDreamShapedRun& ShapedRun : ShapedRuns)
		{
			FRunInfo Run;
			Run.GlyphStart = Glyphs.Num();
			Run.ElementStart = Start + ShapedRun.ElementStart;
			Run.ElementEnd = Start + ShapedRun.ElementEnd;
			Run.bRightToLeft = ShapedRun.bRightToLeft;
			const int32 RunIndex = Runs.Num();
			// Glyphs stay in the shaper's visual order; an element's glyphs are contiguous within it.
			int32 CurrentElement = -1;
			for (const FDreamShapedGlyph& Shaped : ShapedRun.Glyphs)
			{
				const int32 ElementIndex = Start + Shaped.ElementIndex;
				FMeasured& M = Measured[ElementIndex];
				if (ElementIndex != CurrentElement)
				{
					M.GlyphStart = Glyphs.Num();
					M.GlyphCount = 0;
					M.FaceIndex = Shaped.FaceIndex;
					CurrentElement = ElementIndex;
				}
				FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
				G.Quad = FetchGlyphQuad(Shaped.FaceIndex, Shaped.GlyphIndex, ShapedRun.Size, ShapedRun.bSyntheticBold);
				G.XAdvance = Shaped.XAdvance;
				G.XOffset = Shaped.XOffset;
				G.YOffset = Shaped.YOffset;
				G.ElementIndex = ElementIndex;
				M.GlyphCount++;
				M.ClusterAdvance += Shaped.XAdvance;
				M.RunIndex = RunIndex;
			}
			Run.GlyphEnd = Glyphs.Num();
			Runs.Add(Run);
			const bool bRunHasGlyphs = ShapedRun.Glyphs.Num() > 0;
			for (int32 i = Run.ElementStart; i < Run.ElementEnd; i++)
			{
				FMeasured& M = Measured[i];
				// A shaped-cluster continuation -- a combining mark, the second letter of a ligature -- has no glyph of
				// its own, so no advance, but it still belongs to the run for placement.
				M.RunIndex = RunIndex;
				if (M.GlyphCount > 0)
				{
					M.Glyph = Glyphs[M.GlyphStart].Quad;
					M.Glyph.XAdvance = M.ClusterAdvance;
				}
				else
				{
					M.FaceIndex = ShapedRun.FaceIndex;
					M.bShapeClusterStart = !bRunHasGlyphs;
				}
				M.bSyntheticBold = ShapedRun.bSyntheticBold;
				M.bSyntheticItalic = M.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(M.FaceIndex), EDreamUIFontFaceStyle::Italic);
			}
		}
		return true;
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
		// A cluster is what is never pulled apart: an extended grapheme cluster (UAX #29: a base and its marks, an
		// emoji sequence, a conjunct) joined with whatever the shaper made one glyph cluster of (a ligature). Lines
		// break only between clusters and letter spacing goes only between them, never between a letter and its
		// accent; a caret still stands at every grapheme cluster, inside a ligature too.
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			const bool bUnshaped = M.bSkipped || M.bHardBreak || M.bImageSpace || M.bEmoji;
			const FMeasured* Prev = i > Start ? &Measured[i - 1] : nullptr;
			const bool bPrevUnshaped = Prev != nullptr && (Prev->bSkipped || Prev->bHardBreak || Prev->bImageSpace || Prev->bEmoji);
			const bool bGrapheme = GraphemeStart.IsValidIndex(i) ? (bool)GraphemeStart[i] : true;
			M.bClusterStart = Prev == nullptr || bUnshaped || bPrevUnshaped || Prev->RunIndex != M.RunIndex
				|| (bGrapheme && M.bShapeClusterStart);
			M.bCaretStop = M.bClusterStart || bGrapheme;
			M.LetterSpacing = 0.0f;
			M.ClusterFitWidth = 0.0f;
		}
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (!M.bClusterStart)continue;
			float Width = M.ClusterAdvance;
			for (int32 j = i + 1; j < End && !Measured[j].bClusterStart; j++)
			{
				Width += Measured[j].ClusterAdvance;
			}
			M.ClusterFitWidth = Width;
			M.LetterSpacing = LetterSpacingFor(i);
		}
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			M.Advance = M.ClusterAdvance + (M.bClusterStart ? M.LetterSpacing : 0.0f);
		}
	}

	void FLayoutRun::ComputeBreakOpportunities()
	{
		FDreamTextBreaker::ComputeBreakOpportunities(PlainText, PlainStart, ElementCodepoints, In.PhraseWrap, CanBreakBefore);
		// A line never ends inside a cluster, whatever the line-break rules allow.
		for (int32 i = 0; i < CanBreakBefore.Num() && i < Measured.Num(); i++)
		{
			if (!Measured[i].bClusterStart)
			{
				CanBreakBefore[i] = false;
			}
		}
	}

	void FLayoutRun::BreakLines()
	{
		const int32 Count = TextProcessingArray.Num();
		const bool bWrap = ShouldWrap();
		const bool bPerCharacter = In.WrappingPolicy == ETextWrappingPolicy::AllowPerCharacterWrapping;
		LineRanges.Reset();

		int32 LineStart = 0;
		float X = 0.0f;
		int32 LastOpportunity = -1;
		float XAtLastOpportunity = 0.0f;
		for (int32 i = 0; i < Count; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			if (M.bHardBreak)
			{
				FLineRange Range;
				Range.Start = LineStart;
				Range.End = i;
				Range.HardBreakElement = i;
				LineRanges.Add(Range);
				LineStart = i + 1;
				if (LineStart < Count && Measured[LineStart].bSkipped)
				{
					LineStart++;
				}
				X = 0.0f;
				LastOpportunity = -1;
				continue;
			}

			if (bWrap)
			{
				if (i > LineStart && CanBreakBefore.IsValidIndex(i) && CanBreakBefore[i])
				{
					LastOpportunity = i;
					XAtLastOpportunity = X;
				}
				// A cluster is fitted whole, at its first element. Whitespace hangs: it may run past the wrap width
				// and never forces a break itself.
				if (M.bClusterStart && !M.bWhitespace && X + M.ClusterFitWidth > WrapWidth + UE_KINDA_SMALL_NUMBER)
				{
					if (LastOpportunity > LineStart)
					{
						FLineRange Range;
						Range.Start = LineStart;
						Range.End = LastOpportunity;
						LineRanges.Add(Range);
						LineStart = LastOpportunity;
						X -= XAtLastOpportunity;
						LastOpportunity = -1;
					}
					// Still too wide on a line of its own start: a word longer than the box. Break
					// inside it if the policy allows, otherwise let it overflow. The cut lands between clusters
					// and avoids stranding closing punctuation at a line start, as a browser's break-all does.
					if (bPerCharacter && i > LineStart && X + M.ClusterFitWidth > WrapWidth + UE_KINDA_SMALL_NUMBER)
					{
						const int32 Cut = FDreamTextBreaker::FindKinsokuSafeFallback(ElementCodepoints, LineStart, i, &ClusterStarts);
						if (Cut != INDEX_NONE)
						{
							FLineRange Range;
							Range.Start = LineStart;
							Range.End = Cut;
							LineRanges.Add(Range);
							float XAtCut = 0.0f;
							for (int32 k = Cut; k < i; k++)
							{
								if (!Measured[k].bSkipped)XAtCut += Measured[k].Advance;
							}
							LineStart = Cut;
							X = XAtCut;
							LastOpportunity = -1;
						}
					}
				}
			}
			X += M.Advance;
		}
		FLineRange Last;
		Last.Start = LineStart;
		Last.End = Count;
		LineRanges.Add(Last);
	}

	void FLayoutRun::ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 VisualRunStart, FDreamUITextLineProperty& LineProperty, float XOffset)
	{
		if (XOffset == 0.0f)return;
		for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
		{
			Out.Items[i].Pen.X += XOffset;
		}
		for (auto& Caret : LineProperty.CaretPropertyList)
		{
			Caret.CaretPosition.X += XOffset;
		}
		for (int32 i = ImageStart; i < Out.Images.Num(); i++)
		{
			Out.Images[i].Position.X += XOffset;
		}
		for (int32 i = EmojiStart; i < Out.Emojis.Num(); i++)
		{
			Out.Emojis[i].Position.X += XOffset;
		}
		for (int32 i = VisualRunStart; i < Out.VisualRuns.Num(); i++)
		{
			Out.VisualRuns[i].Left += XOffset;
			Out.VisualRuns[i].Right += XOffset;
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

	FDreamUICharData FLayoutRun::MakeEllipsisGlyph(const FMeasured& StyleElement, bool& bOutPending, bool& bOutSyntheticBold, bool& bOutSyntheticItalic)
	{
		const uint32 CharCodeOfDots = 0x2026;//'…'
		const float Size = StyleElement.Style.Size;
		FDreamUICharData Glyph;
		bool bShaped = false;
		// Without shaping the glyph comes from the font's own face, as every glyph of that path does.
		bOutSyntheticBold = StyleElement.Style.Bold && !EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Bold);
		bOutSyntheticItalic = StyleElement.Style.Italic && !EnumHasAnyFlags(FaceStyleOf(0), EDreamUIFontFaceStyle::Italic);
		bool bAnyFaceHasDots = false;
		const int32 FaceCount = Font->GetFaceCount();
		for (int32 F = 0; F < FaceCount && !bAnyFaceHasDots; F++)
		{
			bAnyFaceHasDots = Font->FaceHasCodepoint(F, CharCodeOfDots);
		}
		if (bCanShape && bAnyFaceHasDots)
		{
			// Shaped like any character of the run it ends, so it comes from the face that run is drawn from -- a bold
			// face for a bold run -- at the run's size.
			TArray<FDreamShapeElement> DotsElements;
			FDreamShapeElement& Dots = DotsElements.AddDefaulted_GetRef();
			Dots.Codepoint = CharCodeOfDots;
			Dots.Size = Size;
			Dots.bBold = StyleElement.Style.Bold;
			Dots.bItalic = StyleElement.Style.Italic;
			TArray<FDreamShapedRun> DotsRuns;
			bool bDotsRightToLeft = false;
			if (FDreamTextShaper::ShapeParagraph(DotsElements, Font, false, EDreamTextFlowDirection::LeftToRight, DotsRuns, bDotsRightToLeft)
				&& DotsRuns.Num() > 0 && DotsRuns[0].Glyphs.Num() > 0)
			{
				const FDreamShapedRun& DotsRun = DotsRuns[0];
				Glyph = FetchGlyphQuad(DotsRun.Glyphs[0].FaceIndex, DotsRun.Glyphs[0].GlyphIndex, Size, DotsRun.bSyntheticBold);
				float Advance = 0.0f;
				for (const FDreamShapedGlyph& Shaped : DotsRun.Glyphs)
				{
					Advance += Shaped.XAdvance;
				}
				Glyph.XAdvance = Advance;
				bOutSyntheticBold = DotsRun.bSyntheticBold;
				bOutSyntheticItalic = StyleElement.Style.Italic
					&& !EnumHasAnyFlags(FaceStyleOf(DotsRun.Glyphs[0].FaceIndex), EDreamUIFontFaceStyle::Italic);
				bShaped = true;
			}
		}
		if (!bShaped)
		{
			const FDreamUIText_TextProcessingElement DotsElement{ CharCodeOfDots, 0, 1, EDreamUIText_CodeType::Text };
			// The ellipsis replaces whatever was there, so it has no left neighbour to kern against.
			Glyph = GetCharGeo(0, DotsElement, Size, bOutSyntheticBold, StyleElement.Style, false);
		}
		bOutPending = Glyph.bPending;
		return Glyph;
	}

	void FLayoutRun::CutCluster(FPlaced& Placed, TArray<int32>& InOutImagesToRemove, TArray<int32>& InOutEmojisToRemove)
	{
		Placed.bCut = true;
		for (int32 i = Placed.ItemStart; i < Placed.ItemEnd; i++)
		{
			FDreamTextGlyphItem& Item = Out.Items[i];
			Item.bEmit = false;
			Item.bCountsAsVisible = false;
			Item.Style.bUnderline = false;
			Item.Style.bStrikethrough = false;
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
			if (Out.Images.IsValidIndex(Index))
			{
				Out.Images.RemoveAt(Index);
			}
		}
		EmojisToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (const int32 Index : EmojisToRemove)
		{
			if (Out.Emojis.IsValidIndex(Index))
			{
				Out.Emojis.RemoveAt(Index);
			}
		}
	}

	void FLayoutRun::ClampLine(int32 LineIndex, bool bBaseRightToLeft, float PenEnd, float Baseline, bool bForceEllipsis,
		int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty)
	{
		if (!IsClampMode() || bHasClampContent)return;
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
		Out.bTruncated = true;
		bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after the line, so we mark it and read it later
		const bool bEllipsis = In.OverflowType == EDreamUITextOverflowType::Ellipsis;
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

		FDreamUICharData DotsGlyph;
		float DotsAdvance = 0.0f;
		bool bDotsPending = false;
		bool bDotsSyntheticBold = false;
		bool bDotsSyntheticItalic = false;
		bool bDots = false;
		if (bEllipsis && Measured.IsValidIndex(StyleElement))
		{
			DotsGlyph = MakeEllipsisGlyph(Measured[StyleElement], bDotsPending, bDotsSyntheticBold, bDotsSyntheticItalic);
			DotsAdvance = DotsGlyph.XAdvance;
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
				Out.Items[ItemIndex].Style.bUnderline = false;
				Out.Items[ItemIndex].Style.bStrikethrough = false;
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
			const FMeasured& StyleMeasured = Measured[StyleElement];
			const float DotsSpacing = In.FontSpace.X;
			const float DotsLeft = bBaseRightToLeft ? KeptEnd - DotsAdvance : KeptEnd;
			FDreamTextGlyphItem Dots;
			Dots.Kind = EDreamTextItemKind::Glyph;
			Dots.Codepoint = 0x2026;
			Dots.ElementIndex = StyleElement;
			Dots.SourceIndex = TextProcessingArray.IsValidIndex(StyleElement) ? TextProcessingArray[StyleElement].StringIndex : 0;
			Dots.LineIndex = Out.Lines.Num();
			Dots.Pen = FVector2f(DotsLeft, Baseline + StyleMeasured.Style.BaselineShift);
			Dots.Glyph = DotsGlyph;
			Dots.AdvanceWithSpace = DotsAdvance + DotsSpacing;
			Dots.DecorationOffset = bBaseRightToLeft ? -DotsSpacing : 0.0f;
			Dots.Style = MakeStyle(StyleMeasured);
			// What has to be made up depends on the face the ellipsis itself came from, not on the text's.
			Dots.Style.bSyntheticBold = bDotsSyntheticBold;
			Dots.Style.bSyntheticItalic = bDotsSyntheticItalic;
			AssignDecorations(Dots, StyleMeasured, 0.0f);
			// Laid out either way; drawn once its glyph has landed, when the font lays the text out again.
			Dots.bEmit = !bDotsPending;
			Dots.bCountsAsVisible = false;
			if (bDotsPending)
			{
				Out.bHasPendingGlyphs = true;
			}
			LineDotsItem = Out.Items.Add(Dots);
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
		if (Shift != 0.0f)
		{
			ShiftLine(LineItemStart, ImageStart, EmojiStart, Out.VisualRuns.Num(), LineProperty, Shift);
			for (FPlaced& P : LinePlaced)
			{
				P.X0 += Shift;
			}
		}
	}

	void FLayoutRun::PlaceCluster(int32 Start, int32 End, bool bRightToLeft, uint8 Level, bool bTrailing, int32 LineIndex, int32 RangeStart,
		float& InOutPenX, float Baseline, const FLineBox& Box, float LineCentre)
	{
		const FMeasured& Lead = Measured[Start];
		FPlaced Placed;
		Placed.Start = Start;
		Placed.End = End;
		Placed.ItemStart = Out.Items.Num();
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
				Placed.ImageIndex = Out.Images.Add(ImageTagData);
			}
			else
			{
				AssignDecorations(Item, Lead, 0.0f);
				FDreamUIText_Emoji Emoji;
				Emoji.EmojiCode = TextProcessingArray[Start].Unicode;
				Emoji.Position = FVector2D(CentreX, LineCentre);
				Emoji.Size = FVector2D(Lead.Glyph.Width, ObjectHeight);
				Placed.EmojiIndex = Out.Emojis.Add(Emoji);
			}
			Out.Items.Add(Item);
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
				AssignDecorations(Item, Lead, 0.0f);
				Out.Items.Add(Item);
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
					// The cluster's letter spacing rides on its last glyph in reading order -- the right-most left to
					// right, the left-most right to left -- so the glyphs' stretches together cover its whole pen box.
					const bool bCarriesSpacing = bRightToLeft ? GlyphOrdinal == 0 : GlyphOrdinal == TotalGlyphs - 1;
					Item.AdvanceWithSpace = G.XAdvance + (bCarriesSpacing ? Placed.Spacing : 0.0f);
					Item.DecorationOffset = -G.XOffset - ((bCarriesSpacing && bRightToLeft) ? Placed.Spacing : 0.0f);
					if (G.Quad.bPending)
					{
						Out.bHasPendingGlyphs = true;
					}
					// Counting and emitting are separate: a glyph still on the rasterizer's worker occupies its place
					// in the visible-character numbering even though it has no quad yet, so a tag range or a
					// TextAnimation index does not shift the moment OnGlyphsReady lands.
					Item.bCountsAsVisible = true;
					Item.bEmit = !G.Quad.bPending;
					AssignDecorations(Item, M, G.YOffset);
					Out.Items.Add(Item);
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
		Placed.ItemEnd = Out.Items.Num();
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
			AddBox(MetricsFor(M.Style.Size, M.FaceIndex), Shift);
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

	void FLayoutRun::PlaceLine(int32 LineIndex, float LineTop)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		const int32 LineItemStart = Out.Items.Num();
		const int32 ImageStart = Out.Images.Num();
		const int32 EmojiStart = Out.Emojis.Num();
		const int32 VisualRunStart = Out.VisualRuns.Num();
		const bool bAfterClamp = bHasClampContent;
		FDreamUITextLineProperty LineProperty;

		// The line box: every box on the line sitting on one baseline (ComputeLineBox). Carets and line-relative
		// inline objects anchor on the line's centre, which is the contract they had.
		const FLineBox Box = ComputeLineBox(LineIndex);
		CurrentLineHeight = Box.Top + Box.Bottom;
		const float Baseline = LineTop - Box.Top;
		const float LineCentre = LineTop - CurrentLineHeight * 0.5f;

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

		// The line's end caret: the trailing edge of its last cluster in logical order -- the right of one read left to
		// right, the left of one read right to left.
		float EndCaretX = 0.0f;
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

		// Spaces hanging off the line's end are not underlined, as in CSS; the ones between words are.
		for (const FPlaced& P : LinePlaced)
		{
			if (!P.bTrailing)continue;
			for (int32 ItemIndex = P.ItemStart; ItemIndex < P.ItemEnd; ItemIndex++)
			{
				Out.Items[ItemIndex].Style.bUnderline = false;
				Out.Items[ItemIndex].Style.bStrikethrough = false;
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
			ClampLine(LineIndex, bBaseRightToLeft, PenEnd, Baseline, bEllipsizeThisLine, LineItemStart, ImageStart, EmojiStart, LineProperty);
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
			if (Out.VisualRuns.Num() > VisualRunStart && k > 0 && !LinePlaced[k - 1].bCut)
			{
				const FPlaced& Prev = LinePlaced[k - 1];
				const bool bContiguous = P.bRightToLeft ? P.End == Prev.Start : P.Start == Prev.End;
				if (Prev.Level == P.Level && bContiguous)
				{
					FDreamTextVisualRun& VisualRun = Out.VisualRuns.Last();
					VisualRun.SourceStart = FMath::Min(VisualRun.SourceStart, SourceStart);
					VisualRun.SourceEnd = FMath::Max(VisualRun.SourceEnd, SourceEnd);
					VisualRun.Left = FMath::Min(VisualRun.Left, Left);
					VisualRun.Right = FMath::Max(VisualRun.Right, Right);
					bJoined = true;
				}
			}
			if (!bJoined)
			{
				FDreamTextVisualRun& VisualRun = Out.VisualRuns.AddDefaulted_GetRef();
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
				const FDreamTextGlyphItem& Item = Out.Items[ItemIndex];
				if (!Item.bEmit)continue;
				float InkLeft = 0.0f, InkRight = 0.0f;
				ItemInkExtent(Item, InkLeft, InkRight);
				AddExtent(InkLeft, InkRight);
			}
		}
		if (LineDotsItem != INDEX_NONE)
		{
			const FDreamTextGlyphItem& Dots = Out.Items[LineDotsItem];
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
		Out.Lines.Add(LineProperty);
	}

	void FLayoutRun::Place()
	{
		// Lines stack down from the paragraph's top edge at y = 0, each as tall as its line box.
		//
		// A wrapped paragraph under a clamp policy also has a BOTTOM: a line that does not fit the box
		// is not placed at all, and the last one that does carries the ellipsis -- Slate's "overflow
		// line". Without wrapping there is only ever one line, which is why Ellipsis used to be a
		// single-line feature.
		const bool bVerticalClamp = IsClampMode() && ShouldWrap() && In.Height > 0.0f;
		float LineTop = 0.0f;
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
			PlaceLine(LineIndex, LineTop);
			bEllipsizeThisLine = false;
			const float LineAdvance = CurrentLineHeight + In.FontSpace.Y;
			LineTop -= LineAdvance;
			ParagraphHeight += LineAdvance;
			if (bHasClampContent && bShouldSetParagraphHeightForClampContent)
			{
				bShouldSetParagraphHeightForClampContent = false;
				ParagraphHeight_ForClampContent = ParagraphHeight;
			}
			if (bWasLastVisibleLine)
			{
				break;//every line after this one is below the box
			}
		}
	}

	void FLayoutRun::Run()
	{
		// A scope a step, so that Insights says which of them a slow layout spent its time in.
		Out.Reset();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Prepare);
			Prepare();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Preprocess);
			Preprocess();
			BuildPlainText();
			FDreamTextBreaker::ComputeGraphemeStarts(PlainText, PlainStart, ElementCodepoints, GraphemeStart);
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Measure);
			Measure();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_BreakLines);
			if (ShouldWrap())
			{
				ComputeBreakOpportunities();
			}
			BreakLines();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Place);
			Place();
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextLayout_Finish);
			Finish();
		}
	}

	float FLayoutRun::ComputePreferredWidth() const
	{
		// The unwrapped width: the widest paragraph as it would be on one line. Under negative letter spacing it also
		// reaches the paragraph's ink, as a placed line does.
		const bool bInkCounts = In.FontSpace.X < 0.0f;
		const float TrailingSpacingFactor = bTrailingLetterSpacingCounts ? 1.0f : 0.0f;
		float PreferredWidth = 0.0f;
		float PenWidth = 0.0f;
		float InkRight = -MAX_FLT;
		float LastSpacing = 0.0f;
		bool bAny = false;
		auto EndParagraph = [&]()
		{
			if (bAny)
			{
				const float Width = PenWidth - LastSpacing * (1.0f - TrailingSpacingFactor);
				PreferredWidth = FMath::Max(PreferredWidth, bInkCounts ? FMath::Max(Width, InkRight) : Width);
			}
			PenWidth = 0.0f;
			InkRight = -MAX_FLT;
			LastSpacing = 0.0f;
			bAny = false;
		};
		for (int32 i = 0; i < Measured.Num(); i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			if (M.bHardBreak)
			{
				EndParagraph();
				continue;
			}
			if (M.bClusterStart)
			{
				LastSpacing = M.LetterSpacing;
				if (bInkCounts)
				{
					// The cluster's glyphs walked from its pen, in logical order: near enough for the ink's reach.
					float GlyphPenX = PenWidth;
					for (int32 e = i; e < Measured.Num() && (e == i || !Measured[e].bClusterStart); e++)
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
			bAny = true;
		}
		EndParagraph();
		return PreferredWidth;
	}

	void FLayoutRun::Finish()
	{
		//remove last line's space Y
		ParagraphHeight -= In.FontSpace.Y;
		ParagraphHeight_ForClampContent -= In.FontSpace.Y;
		const float ParagraphHeightWithClamp = bHasClampContent ? ParagraphHeight_ForClampContent : ParagraphHeight;

		Out.PreferredSize.X = ComputePreferredWidth();
		Out.PreferredSize.Y = ParagraphHeight;

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
		for (auto& LinePropertyItem : Out.Lines)
		{
			for (auto& CharItem : LinePropertyItem.CaretPropertyList)
			{
				CharItem.CaretPosition.X += XOffset;
				CharItem.CaretPosition.Y += YOffset;
			}
		}
		for (auto& ImageItem : Out.Images)
		{
			ImageItem.Position.X += XOffset;
			ImageItem.Position.Y += YOffset;
		}
		for (auto& EmojiItem : Out.Emojis)
		{
			EmojiItem.Position.X += XOffset;
			EmojiItem.Position.Y += YOffset;
		}
		for (auto& Item : Out.Items)
		{
			Item.Pen.X += XOffset;
			Item.Pen.Y += YOffset;
		}
		for (auto& VisualRun : Out.VisualRuns)
		{
			VisualRun.Left += XOffset;
			VisualRun.Right += XOffset;
		}

		// THE numbering of visible characters, built once, here, from the items the painter will walk.
		// One per ELEMENT, in the order they are painted: a code point that shaped into two glyphs is
		// one character, a character whose glyphs have not landed yet is still one character, and a
		// character a clamp threw away is none. Everything that addresses characters -- the display
		// list's count, the painter's FDreamUITextCharProperty list that TextAnimation walks, and the
		// rich-text custom tag ranges resolved below -- reads this one answer.
		TArray<int32> VisibleIndexOfElement;
		VisibleIndexOfElement.Init(INDEX_NONE, Measured.Num());
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
}

void FDreamTextLayoutEngine::Layout(const FDreamTextLayoutInput& Input, FDreamTextDisplayList& Out)
{
	Out.Reset();
	if (!Input.Font.IsValid())
	{
		return;
	}
	DreamTextLayoutLocal::FLayoutRun Run(Input, Out);
	Run.Run();
}
