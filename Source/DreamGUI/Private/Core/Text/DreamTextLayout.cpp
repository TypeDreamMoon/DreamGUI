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
		&& Font == Other.Font
		&& RichTextImageData == Other.RichTextImageData
		&& RichTextCustomStyleData == Other.RichTextCustomStyleData;
}

namespace DreamTextLayoutLocal
{
	using namespace DreamUIRichTextParser;

	/**
	 * One layout pass, in the order a browser's inline formatting context does it: measure every
	 * element, decide where the lines break, place the lines, then align the paragraph. Measuring
	 * first is what makes the breaker a pure function over widths, and what will let shaping slot in
	 * before it.
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
			/** XAdvance plus the horizontal font space: how far the pen moves. */
			float Advance = 0.0f;
			/** The element's glyph advances without the font space: what the breaker fits against. */
			float ClusterAdvance = 0.0f;
			/** Its glyphs, as a range of the run's glyph array; none for a cluster continuation. */
			int32 GlyphStart = 0;
			int32 GlyphCount = 0;
			/** Shaped run this element belongs to, or -1 when measured per code point. */
			int32 RunIndex = -1;
			/** Face the element's glyphs came from: 0 is the font, the rest are its fallbacks. */
			int32 FaceIndex = 0;
			/** Paragraph base direction: runs of a right-to-left paragraph are placed right to left. */
			bool bBaseRightToLeft = false;
			/** A newline: ends the paragraph, never placed. */
			bool bHardBreak = false;
			/** The second half of a CR LF pair: nothing at all. */
			bool bSkipped = false;
			/** Space or tab (not an image placeholder): advances, hangs past the wrap width, never emits. */
			bool bWhitespace = false;
			bool bImageSpace = false;
			bool bEmoji = false;
			/** A glyph the painter will draw. */
			bool bVisibleGlyph = false;
		};

		/** A line as the breaker decided it: a half-open element range and what ended it. */
		struct FLineRange
		{
			int32 Start = 0;
			int32 End = 0;
			/** The newline element that ended this line, or -1 for a soft break / the end of the text. */
			int32 HardBreakElement = -1;
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
		float OriginLineHeight = 0.0f;
		float LineHeightScale = 1.0f;
		float WrapWidth = 0.0f;
		float HalfFontSpaceX = 0.0f;
		float ItalicSlope = 0.0f;

		FRichTextParser RichTextParser;
		FRichTextParseResult RichTextParseResult;
		TArray<FRichTextParseResult> RichTextPropertyArray;
		TArray<FDreamUIText_TextProcessingElement> TextProcessingArray;

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
		TArray<uint32> ElementCodepoints;
		TBitArray<> CanBreakBefore;
		TArray<FLineRange> LineRanges;

		// Running state of placement.
		float CurrentLineHeight = 0.0f;
		float ParagraphHeight = 0.0f;
		/**
		 * Custom tag ranges as ELEMENT indices while the lines are being placed, parallel to
		 * Out.CustomTags: X is the first tagged element, Y the last (INDEX_NONE until the closing tag).
		 * They become visible-character indices in Finish, off the one numbering built there -- counting
		 * them during placement gave a third, different answer, because placement counts elements that
		 * a later clamp then throws away.
		 */
		TArray<FIntPoint> CustomTagElements;
		bool bHasClampContent = false;
		/**
		 * False while a right-to-left line is being placed: that line is cut afterwards, from the other
		 * end (ApplyRightToLeftClamp), so the pen must be allowed to run past the box first.
		 */
		bool bClampWhilePlacing = true;
		/**
		 * The line being placed is the last one that fits the box vertically, so it ends in an ellipsis
		 * whether or not it also runs past the right edge -- what the ellipsis stands for is the text
		 * below it. Set by Place, consumed by PlaceLine.
		 */
		bool bEllipsizeThisLine = false;
		float ClampedLineWidth = 0.0f;
		float ParagraphHeight_ForClampContent = 0.0f;
		bool bShouldSetParagraphHeightForClampContent = false;

		void Prepare();
		void Preprocess();
		void Measure();
		void MeasureParagraphByCodepoint(int32 Start, int32 End);
		bool MeasureParagraphByShaping(int32 Start, int32 End);
		FDreamUICharData FetchGlyphQuad(int32 FaceIndex, uint32 GlyphIndex, float InFontSize, bool bInBold) const;
		void PlaceElement(int32 ElementIndex, int32 LineIndex, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent);
		void PlaceRightToLeftSegment(int32 Start, int32 End, int32 LineIndex, int32 LineItemStart, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent);
		/**
		 * Truncate and Ellipsis cut where the pen would leave the box -- measured against the box, not
		 * the wrap width, because they are about what fits on screen. Both directions share it: the pen
		 * runs left to right through a right-to-left segment as well, which is why living inside the
		 * left-to-right branch meant right-to-left text was never cut at all. True when it cut here.
		 */
		bool TryClampAt(int32 ElementIndex, float NextAdvance, int32 LineItemStart, float& InOutPenX, float Baseline, float& InOutContentRight, bool bAnyContent);
		void ComputeBreakOpportunities();
		void BreakLines();
		void Place();
		void PlaceLine(int32 LineIndex, float LineTop);
		/** Slides everything a line owns -- items, carets, inline objects -- by the same amount. */
		void ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty, float XOffset);
		void AlignLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty, float LineWidth);
		/**
		 * The clamp for a right-to-left paragraph, applied once the whole line is placed. A right-to-left
		 * line reads from its right edge, so what does not fit is at the LEFT of the pen run and the
		 * ellipsis belongs there -- which is where Slate puts it for a right-to-left line, and the
		 * opposite of where the left-to-right pass puts it. Cutting during placement cannot do this:
		 * the pen is still walking towards the end that survives.
		 */
		void ApplyRightToLeftClamp(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 StyleElementIndex,
			FDreamUITextLineProperty& LineProperty, float& InOutPenX, float Baseline, float& InOutContentRight, bool bAnyContent);
		void Finish();

		bool IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		bool IsSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		void GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FRichTextParseResult& RichTextResult) const;
		void GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, uint32 EmojiCode) const;
		FDreamUICharData GetCharGeo(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, float InFontSize, bool bInBold, const FRichTextParseResult& RichTextResult) const;
		FDreamUICharData GetUnderlineOrStrikethroughCharGeo(uint32 CharCode, float OverrideFontSize, bool bBold) const;
		static FDreamTextItemStyle MakeStyle(const FRichTextParseResult& Result);
		float ItemMaxX(const FDreamTextGlyphItem& Item) const;
		int32 CaretIndexOf(int32 ElementIndex) const;
		/**
		 * Replaces the tail of the line being placed with an ellipsis. bMayGrowLine is what the
		 * vertical clamp wants: the line ends inside the box and the ellipsis only has to follow it,
		 * saying "there is more below"; without it the ellipsis has to make room for itself.
		 */
		void ApplyEllipsis(int32 ElementIndex, int32 LineItemStart, float& InOutPenX, float Baseline, bool bMayGrowLine = false);
		/** True when lines wrap: the VerticalOverflow policy, or UMG-style AutoWrapText on top of another. */
		bool ShouldWrap() const { return In.OverflowType == EDreamUITextOverflowType::VerticalOverflow || In.bAutoWrapText; }
		/** True when the policy cuts what does not fit rather than letting it hang out. */
		bool IsClampMode() const { return In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis; }
		/** The line box a line would get: ascent, descent and height, over every face and size on it. */
		void ComputeLineBox(int32 LineIndex, float& OutAscent, float& OutDescent, float& OutLineHeight);
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
		// Scales the gap between lines without touching glyph size. FontSpace.Y stays outside it: that
		// one is a flat distance the author asked for, not something that should follow the font.
		LineHeightScale = FMath::Max(0.0f, In.LineHeightPercentage);
		// Wrapping and the box are separable: an author can ask for a narrow column that still centres
		// over the full widget. Truncate and Ellipsis deliberately keep measuring against the box,
		// because those are about what fits on screen rather than where lines break.
		WrapWidth = In.WrapTextAt > 0.0f ? In.WrapTextAt : In.Width;
		HalfFontSpaceX = In.FontSpace.X * 0.5f;

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
		// face needs the per-face question, and a font that cannot answer it says so.
		if (FaceIndex == 0 || !Font->GetFaceMetrics(FaceIndex, Size, M.Ascent, M.Descent, M.LineHeight))
		{
			M.Ascent = Font->GetAscent(Size);
			M.Descent = Font->GetDescent(Size);
			M.LineHeight = Font->GetLineHeight(Size);
		}
		return MetricsBySize.Add(Key, M);
	}

	bool FLayoutRun::IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const
	{
		return CharCode == ' ' && In.bRichText && !RichTextResult.ImageTag.IsNone();
	}

	bool FLayoutRun::IsSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const
	{
		return CharCode == ' ' && !(In.bRichText && !RichTextResult.ImageTag.IsNone());
	}

	void FLayoutRun::GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, const FRichTextParseResult& RichTextResult) const
	{
		// `<img=Tag/>` is as tall as the font and as wide as its aspect ratio makes it; `<img=Tag,H/>`
		// and `<img=Tag,W,H/>` say otherwise. An authored size is in the same units as the font size,
		// so it goes through the same canvas inverse the default does.
		const float AuthoredHeight = RichTextResult.ImageHeight;
		const float Height = (AuthoredHeight > 0.0f ? AuthoredHeight : InFontSize) * OneDivideRootCanvasScale;
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = Height;

		if (RichTextResult.ImageWidth > 0.0f)
		{
			OverrideCharData.Width = OverrideCharData.XAdvance = RichTextResult.ImageWidth * OneDivideRootCanvasScale;
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
		//emoji use font size as default width & height & xadvance
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = InFontSize * OneDivideRootCanvasScale;

		FIntVector2 ImageSize;
		if (IsValid(EmojiData) && EmojiData->GetImageSize(EmojiCode, ImageSize))
		{
			const float Ratio = (float)ImageSize.X / ImageSize.Y;
			OverrideCharData.Width = OverrideCharData.Width * Ratio;
			OverrideCharData.XAdvance = OverrideCharData.XAdvance * Ratio;
		}
		else
		{
			//default use font size as width & height & xadvance
			OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = InFontSize * OneDivideRootCanvasScale;
		}
	}

	FDreamUICharData FLayoutRun::GetCharGeo(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, float InFontSize, bool bInBold, const FRichTextParseResult& RichTextResult) const
	{
		auto CharData = Font->GetCharData(CharElement.Unicode, InFontSize, bInBold);

		auto OverrideCharData = CharData;
		/** Turns a length measured at the rasterized size back into text units; 1 when nothing was scaled. */
		float BackToTextUnits = 1.0f;
		if (bShouldScaleFontSizeWithRootCanvas)
		{
			// Three branches that differ only in which scale they apply; kept as three so the result
			// stays bit-for-bit what it was (the image/emoji sizes always use the canvas inverse).
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
			if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
			{
				// Inline objects are not rasterized, so the raster cap is not theirs to obey.
				GetRichTextImageCharData(OverrideCharData, ScaledFontSize, RichTextResult);
			}
			else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
			{
				GetEmojiCharData(OverrideCharData, ScaledFontSize, CharElement.Unicode);
			}
			else
			{
				OverrideCharData = Font->GetCharData(CharElement.Unicode, InFontSize, bInBold);

				OverrideCharData.Width = OverrideCharData.Width * OneDivideScale;
				OverrideCharData.Height = OverrideCharData.Height * OneDivideScale;
				OverrideCharData.XAdvance = OverrideCharData.XAdvance * OneDivideScale;
			}
			OverrideCharData.XOffset = OverrideCharData.XOffset * OneDivideScale;
			OverrideCharData.YOffset = OverrideCharData.YOffset * OneDivideScale;
			BackToTextUnits = OneDivideScale;
		}
		else
		{
			if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
			{
				GetRichTextImageCharData(OverrideCharData, InFontSize, RichTextResult);
			}
			else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
			{
				GetEmojiCharData(OverrideCharData, InFontSize, CharElement.Unicode);
			}
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

	FDreamUICharData FLayoutRun::GetUnderlineOrStrikethroughCharGeo(uint32 CharCode, float OverrideFontSize, bool bBold) const
	{
		auto CharData = Font->GetCharData(CharCode, OverrideFontSize, bBold);

		const float UVX = (CharData.MaxUV.X - CharData.MinUV.X) * 0.5f + CharData.MinUV.X;
		CharData.MinUV.X = CharData.MaxUV.X = UVX;
		return CharData;
	}

	FDreamTextItemStyle FLayoutRun::MakeStyle(const FRichTextParseResult& Result)
	{
		FDreamTextItemStyle Style;
		Style.Size = Result.Size;
		Style.Color = Result.Color;
		Style.bHasColor = Result.HasColor;
		Style.bBold = Result.Bold;
		Style.bItalic = Result.Italic;
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
		if (In.bRichText)
		{
			//pre parse rich text
			auto RichTextCustomStyleData = In.RichTextCustomStyleData.Get();
			const bool bUseCustomStyle = IsValid(RichTextCustomStyleData);
			for (int32 CharIndex = 0; CharIndex < ContentLength; CharIndex++)
			{
				RichTextParseResult.CustomTag = NAME_None;
				RichTextParseResult.CustomTagMode = ECustomTagMode::None;
				RichTextParseResult.bHyperlink = false;
				RichTextParseResult.CharIndex = CharIndex;
				RichTextParser.ClearImageTag();
				while (RichTextParser.Parse(In.Content, ContentLength, CharIndex, RichTextParseResult))
				{
					if (!RichTextParseResult.ImageTag.IsNone())//get image, append a blank placeholder
					{
						TextProcessingArray.Add(FDreamUIText_TextProcessingElement{ ' ', CharIndex, 1, EDreamUIText_CodeType::Text });
						RichTextPropertyArray.Add(RichTextParseResult);
						RichTextParseResult.ImageTag = NAME_None;//clear it
						RichTextParser.ClearImageTag();
					}
					if (CharIndex >= ContentLength)
					{
						break;
					}
				}
				//if find end symbol, then mark the prev one as end
				if (RichTextParseResult.CustomTagMode == ECustomTagMode::End)
				{
					if (RichTextPropertyArray.Num() > 0)
					{
						auto& Last = RichTextPropertyArray[RichTextPropertyArray.Num() - 1];
						Last.CustomTag = RichTextParseResult.CustomTag;
						Last.CustomTagMode = RichTextParseResult.CustomTagMode;
					}
					RichTextParseResult.CustomTag = NAME_None;
					RichTextParseResult.CustomTagMode = ECustomTagMode::None;
					RichTextParseResult.bHyperlink = false;
				}

				if (CharIndex >= ContentLength)break;

				RichTextParseResult.CharIndex = CharIndex;
				//convert custom tag to style
				if (bUseCustomStyle)
				{
					if (auto CustomStyleItemDataPtr = RichTextCustomStyleData->GetDataMap().Find(RichTextParseResult.CustomTag))
					{
						CustomStyleItemDataPtr->ApplyToRichTextParseResult(RichTextParseResult);
					}
				}
				RichTextPropertyArray.Add(RichTextParseResult);

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
		}
	}

	int32 FLayoutRun::CaretIndexOf(int32 ElementIndex) const
	{
		// The caret contract: non-rich carets name the element (the code point), rich carets name the
		// source index, so UITextInput can walk the markup it was given.
		return In.bRichText ? RichTextPropertyArray[ElementIndex].CharIndex : ElementIndex;
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
			if (In.bRichText)
			{
				RichTextParseResult = RichTextPropertyArray[i];
			}
			M.Style = RichTextParseResult;
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
						if (In.bRichText)
						{
							Measured[i + 1].Style = RichTextPropertyArray[i + 1];
						}
						i++;
					}
				}
				continue;
			}
			M.bImageSpace = IsRichTextImageSpace(Code, RichTextParseResult);
			M.bEmoji = Element.Type == EDreamUIText_CodeType::Emoji;
			M.bWhitespace = !M.bImageSpace && (Code == ' ' || Code == '\t');
			M.bVisibleGlyph = !M.bImageSpace && !M.bEmoji && !M.bWhitespace;
		}

		const bool bCanShape = FDreamTextShaper::CanShape(Font);
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
				}
				ParagraphStart = i + 1;
			}
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
		uint32 PrevCharCode = 0;
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			const auto& Element = TextProcessingArray[i];
			RichTextParseResult = M.Style;
			M.Glyph = GetCharGeo(PrevCharCode, Element, M.Style.Size, M.Style.Bold, M.Style);
			M.ClusterAdvance = M.Glyph.XAdvance;
			M.Advance = M.ClusterAdvance + In.FontSpace.X;
			M.FaceIndex = M.Glyph.FaceIndex;
			M.RunIndex = -1;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 1;
			FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
			G.Quad = M.Glyph;
			G.XAdvance = M.Glyph.XAdvance;
			G.ElementIndex = i;
			PrevCharCode = Element.Unicode;
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
			E.bUnshaped = M.bSkipped || M.bHardBreak || M.bImageSpace || M.bEmoji;
			ShapeElements.Add(E);
		}
		TArray<FDreamShapedRun> ShapedRuns;
		bool bBaseRightToLeft = false;
		if (!FDreamTextShaper::ShapeParagraph(ShapeElements, Font, bUseKerning, In.FlowDirection, ShapedRuns, bBaseRightToLeft))
		{
			return false;
		}

		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			M.bBaseRightToLeft = bBaseRightToLeft;
			M.RunIndex = -1;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 0;
			M.ClusterAdvance = 0.0f;
			M.Advance = 0.0f;
			if (M.bImageSpace || M.bEmoji)
			{
				// Inline objects are measured by the layout, the way they always were. They never kern,
				// hence the zero left neighbour.
				RichTextParseResult = M.Style;
				M.Glyph = GetCharGeo(0, TextProcessingArray[i], M.Style.Size, M.Style.Bold, M.Style);
				M.ClusterAdvance = M.Glyph.XAdvance;
				M.Advance = M.ClusterAdvance + In.FontSpace.X;
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
				G.Quad = FetchGlyphQuad(Shaped.FaceIndex, Shaped.GlyphIndex, ShapedRun.Size, ShapedRun.bBold);
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
			for (int32 i = Run.ElementStart; i < Run.ElementEnd; i++)
			{
				FMeasured& M = Measured[i];
				if (M.RunIndex != RunIndex)
				{
					// A cluster continuation: no glyph of its own, so no advance and no run of its own,
					// but it still belongs to the run for placement.
					M.RunIndex = RunIndex;
				}
				if (M.GlyphCount > 0)
				{
					M.Advance = M.ClusterAdvance + In.FontSpace.X;
					M.Glyph = Glyphs[M.GlyphStart].Quad;
					M.Glyph.XAdvance = M.ClusterAdvance;
				}
			}
		}
		return true;
	}

	void FLayoutRun::ComputeBreakOpportunities()
	{
		const int32 Count = TextProcessingArray.Num();
		// The breaker sees the text as laid out: markup stripped, placeholders as spaces.
		FString PlainText;
		PlainText.Reserve(Count + 4);
		TArray<int32> PlainStart;
		TArray<uint32> Codepoints;
		PlainStart.SetNumUninitialized(Count);
		Codepoints.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; i++)
		{
			const auto& Element = TextProcessingArray[i];
			PlainStart[i] = PlainText.Len();
			Codepoints[i] = Element.Unicode;
			if (Measured[i].bImageSpace)
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
		FDreamTextBreaker::ComputeBreakOpportunities(PlainText, PlainStart, Codepoints, In.PhraseWrap, CanBreakBefore);
		ElementCodepoints = MoveTemp(Codepoints);
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
				if (i > LineStart && CanBreakBefore[i])
				{
					LastOpportunity = i;
					XAtLastOpportunity = X;
				}
				// Whitespace hangs: it may run past the wrap width and never forces a break itself.
				if (!M.bWhitespace && X + M.ClusterAdvance > WrapWidth + UE_KINDA_SMALL_NUMBER)
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
					// inside it if the policy allows, otherwise let it overflow. The cut avoids
					// stranding closing punctuation at a line start, as a browser's break-all does.
					if (bPerCharacter && i > LineStart && X + M.ClusterAdvance > WrapWidth + UE_KINDA_SMALL_NUMBER)
					{
						const int32 Cut = FDreamTextBreaker::FindKinsokuSafeFallback(ElementCodepoints, LineStart, i);
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

	void FLayoutRun::ShiftLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty, float XOffset)
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
	}

	void FLayoutRun::AlignLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty, float LineWidth)
	{
		float XOffset = 0.0f;
		switch (In.ParagraphHAlign)
		{
		case EDreamUITextParagraphHorizontalAlign::Center:
			XOffset = -LineWidth * 0.5f;
			break;
		case EDreamUITextParagraphHorizontalAlign::Right:
			XOffset = -LineWidth;
			break;
		default:
			break;
		}
		ShiftLine(LineItemStart, ImageStart, EmojiStart, LineProperty, XOffset);
	}

	float FLayoutRun::ItemMaxX(const FDreamTextGlyphItem& Item) const
	{
		// The right-most x the painter will write for this item: the glyph quad (sheared if italic)
		// and, if present, the decoration quads that span the advance.
		FVector2f Pen = Item.Pen;
		if (Item.Style.SupOrSub == 1)
		{
			Pen.Y += Item.Style.Size * 0.5f;
		}
		else if (Item.Style.SupOrSub == 2)
		{
			Pen.Y -= Item.Style.Size * 0.5f;
		}
		const float Left = Pen.X + Item.Glyph.XOffset;
		float MaxX = Left + Item.Glyph.Width;
		if (Item.Style.bItalic)
		{
			// Top edge (vert2/vert3) shears right by YOffset * slope; the bottom edge shears left.
			MaxX = FMath::Max(MaxX + Item.Glyph.YOffset * ItalicSlope, Left + Item.Glyph.Width - (Item.Glyph.Height - Item.Glyph.YOffset) * ItalicSlope);
		}
		if (Item.Style.bUnderline || Item.Style.bStrikethrough)
		{
			MaxX = FMath::Max(MaxX, Pen.X + Item.AdvanceWithSpace);
		}
		return MaxX;
	}

	void FLayoutRun::ApplyEllipsis(int32 ElementIndex, int32 LineItemStart, float& InOutPenX, float Baseline, bool bMayGrowLine)
	{
		//move back and replace chars by ...
		const uint32 CharCodeOfDots = 0x2026;//'…'
		const auto CharElementOfDots = FDreamUIText_TextProcessingElement{ CharCodeOfDots, ElementIndex, 1, EDreamUIText_CodeType::Text };
		// The ellipsis replaces whatever was there, so it has no left neighbour to kern against.
		const auto CharGeoOfDots = GetCharGeo(0, CharElementOfDots, FontSize, false, RichTextParseResult);
		// A wrapped line that ends inside the box keeps all of its glyphs: the ellipsis only has to
		// follow it, because what it stands for is the text BELOW, not text cut off the right edge.
		if (bMayGrowLine && InOutPenX + CharGeoOfDots.XAdvance + HalfFontSpaceX <= In.Width)
		{
			const float AppendAt = InOutPenX;
			FDreamTextGlyphItem AppendedDots;
			AppendedDots.Kind = EDreamTextItemKind::Glyph;
			AppendedDots.Codepoint = CharCodeOfDots;
			AppendedDots.ElementIndex = ElementIndex;
			AppendedDots.SourceIndex = TextProcessingArray.IsValidIndex(ElementIndex) ? TextProcessingArray[ElementIndex].StringIndex : 0;
			AppendedDots.LineIndex = Out.Lines.Num();
			AppendedDots.Pen = FVector2f(AppendAt, Baseline);
			AppendedDots.Glyph = CharGeoOfDots;
			AppendedDots.AdvanceWithSpace = CharGeoOfDots.XAdvance + In.FontSpace.X;
			AppendedDots.Style = MakeStyle(RichTextParseResult);
			AppendedDots.bEmit = true;
			AppendedDots.bCountsAsVisible = false;
			Out.Items.Add(AppendedDots);
			InOutPenX = AppendAt + AppendedDots.AdvanceWithSpace;
			return;
		}
		if (InOutPenX < CharGeoOfDots.XAdvance)//remove all if it can't fit the char-of-dots
		{
			for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
			{
				Out.Items[i].bEmit = false;
				Out.Items[i].bCountsAsVisible = false;
			}
			return;
		}

		const float LineOffsetPointToStripOff = InOutPenX - CharGeoOfDots.XAdvance - HalfFontSpaceX;
		//remove char geometry on tail of the line, if the char's vertex position greater than dots
		for (int32 ItemIndex = Out.Items.Num() - 1; ItemIndex >= LineItemStart; ItemIndex--)
		{
			auto& Item = Out.Items[ItemIndex];
			if (!Item.bEmit)continue;
			if (ItemMaxX(Item) > LineOffsetPointToStripOff)
			{
				Item.bEmit = false;
				Item.bCountsAsVisible = false;
			}
			else
			{
				break;
			}
		}

		FDreamTextGlyphItem Dots;
		Dots.Kind = EDreamTextItemKind::Glyph;
		Dots.Codepoint = CharCodeOfDots;
		Dots.ElementIndex = ElementIndex;
		Dots.SourceIndex = TextProcessingArray[ElementIndex].StringIndex;
		Dots.LineIndex = Out.Lines.Num();
		Dots.Pen = FVector2f(LineOffsetPointToStripOff, Baseline);
		Dots.Glyph = CharGeoOfDots;
		Dots.AdvanceWithSpace = CharGeoOfDots.XAdvance + In.FontSpace.X;
		Dots.Style = MakeStyle(RichTextParseResult);
		if (Dots.Style.bUnderline)
		{
			Dots.UnderlineGlyph = GetUnderlineOrStrikethroughCharGeo('_', RichTextParseResult.Size, RichTextParseResult.Bold);
		}
		if (Dots.Style.bStrikethrough)
		{
			Dots.StrikethroughGlyph = GetUnderlineOrStrikethroughCharGeo('-', RichTextParseResult.Size, RichTextParseResult.Bold);
		}
		Dots.bEmit = true;
		Dots.bCountsAsVisible = false;
		Out.Items.Add(Dots);
		InOutPenX = LineOffsetPointToStripOff + Dots.AdvanceWithSpace;
	}

	void FLayoutRun::PlaceElement(int32 i, int32 LineIndex, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent)
	{
		const FMeasured& M = Measured[i];
		const auto& Element = TextProcessingArray[i];
		RichTextParseResult = M.Style;

		//caret property: the caret sits on the left of its char
		FDreamUITextCaretProperty CaretProperty;
		CaretProperty.CaretPosition = FVector2f(PenX - HalfFontSpaceX, LineCentre);
		CaretProperty.CharIndex = CaretIndexOf(i);
		LineProperty.CaretPropertyList.Add(CaretProperty);

		FDreamTextGlyphItem Item;
		Item.Codepoint = Element.Unicode;
		Item.ElementIndex = i;
		Item.SourceIndex = Element.StringIndex;
		Item.LineIndex = LineIndex;
		Item.Pen = FVector2f(PenX, Baseline);
		Item.Glyph = M.Glyph;
		Item.AdvanceWithSpace = M.Advance;
		Item.Style = MakeStyle(M.Style);

		if (M.bImageSpace)
		{
			Item.Kind = EDreamTextItemKind::Image;
			FDreamUIText_RichTextImageTag ImageTagData;
			ImageTagData.TagName = M.Style.ImageTag;
			ImageTagData.Position = FVector2D(PenX + M.Glyph.XAdvance * 0.5f, LineCentre);
			ImageTagData.Size = FVector2D(M.Glyph.Width, M.Glyph.Height);
			ImageTagData.TintColor = M.Style.HasColor ? M.Style.Color : FColor::White;
			Out.Images.Add(ImageTagData);
			Out.Items.Add(Item);
		}
		else if (M.bEmoji)
		{
			Item.Kind = EDreamTextItemKind::Emoji;
			FDreamUIText_Emoji Emoji;
			Emoji.EmojiCode = Element.Unicode;
			Emoji.Position = FVector2D(PenX + M.Glyph.XAdvance * 0.5f, LineCentre);
			Emoji.Size = FVector2D(M.Glyph.Width, M.Glyph.Height);
			Out.Emojis.Add(Emoji);
			Out.Items.Add(Item);
		}
		else if (M.bWhitespace)
		{
			Item.Kind = EDreamTextItemKind::Space;
			Out.Items.Add(Item);
		}
		else
		{
			// One item per glyph. A cluster continuation has no glyphs and adds nothing, but it
			// still counted as a visible character for the tag and animation indices.
			Item.Kind = EDreamTextItemKind::Glyph;
			const bool bEmit = !bHasClampContent;
			float GlyphPenX = PenX;
			for (int32 g = 0; g < M.GlyphCount; g++)
			{
				const FGlyphSource& G = Glyphs[M.GlyphStart + g];
				FDreamTextGlyphItem GlyphItem = Item;
				GlyphItem.Pen = FVector2f(GlyphPenX + G.XOffset, Baseline + G.YOffset);
				GlyphItem.Glyph = G.Quad;
				GlyphItem.AdvanceWithSpace = G.XAdvance + (g == M.GlyphCount - 1 ? In.FontSpace.X : 0.0f);
				if (G.Quad.bPending)
				{
					Out.bHasPendingGlyphs = true;
				}
				// Counting and emitting are separate: a glyph still on the rasterizer's worker occupies
				// its place in the visible-character numbering even though it has no quad yet, so a tag
				// range or a TextAnimation index does not shift the moment OnGlyphsReady lands.
				GlyphItem.bCountsAsVisible = bEmit;
				if (bEmit && !G.Quad.bPending)
				{
					GlyphItem.bEmit = true;
					if (GlyphItem.Style.bUnderline)
					{
						GlyphItem.UnderlineGlyph = GetUnderlineOrStrikethroughCharGeo('_', M.Style.Size, M.Style.Bold);
					}
					if (GlyphItem.Style.bStrikethrough)
					{
						GlyphItem.StrikethroughGlyph = GetUnderlineOrStrikethroughCharGeo('-', M.Style.Size, M.Style.Bold);
					}
				}
				Out.Items.Add(GlyphItem);
				GlyphPenX += G.XAdvance;
			}
		}

		//collect rich text custom tag. custom tag use start/end mark, so put these code outside of visible-char-check.
		if (In.bRichText)
		{
			switch (M.Style.CustomTagMode)
			{
			case ECustomTagMode::Start:
			{
				FDreamUIText_RichTextCustomTag CustomTag;
				CustomTag.TagName = M.Style.CustomTag;
				CustomTag.bHyperlink = M.Style.bHyperlink;
				CustomTag.CharIndexStart = 0;
				CustomTag.CharIndexEnd = -1;
				Out.CustomTags.Add(CustomTag);
				CustomTagElements.Add(FIntPoint(i, INDEX_NONE));
			}
			break;
			case ECustomTagMode::End:
			{
				const FName TagName = M.Style.CustomTag;
				const int32 FoundIndex = Out.CustomTags.IndexOfByPredicate([TagName](const FDreamUIText_RichTextCustomTag& A) {
					return A.TagName == TagName;
					});
				if (CustomTagElements.IsValidIndex(FoundIndex))
				{
					// The element carrying the End mark is the LAST tagged one: Preprocess marks the
					// element before the closing tag.
					CustomTagElements[FoundIndex].Y = i;
				}
			}
			break;
			default:
				break;
			}
		}

		PenX += M.Advance;
		if (!M.bWhitespace && M.Advance > 0.0f)
		{
			ContentRight = PenX;
			bAnyContent = true;
		}
	}

	bool FLayoutRun::TryClampAt(int32 ElementIndex, float NextAdvance, int32 LineItemStart, float& InOutPenX, float Baseline, float& InOutContentRight, bool bAnyContent)
	{
		const bool bClampMode = In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis;
		if (!bClampMode || bHasClampContent || !bClampWhilePlacing)return false;
		if (InOutPenX + NextAdvance <= In.Width)return false;
		bHasClampContent = true;
		Out.bTruncated = true;
		ClampedLineWidth = bAnyContent ? InOutContentRight - In.FontSpace.X : 0.0f;
		bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after the line, so we mark it and read it later
		if (In.OverflowType == EDreamUITextOverflowType::Ellipsis)
		{
			RichTextParseResult = Measured[ElementIndex].Style;
			ApplyEllipsis(ElementIndex, LineItemStart, InOutPenX, Baseline);
			InOutContentRight = InOutPenX;
			ClampedLineWidth = InOutContentRight - In.FontSpace.X;
		}
		return true;
	}

	void FLayoutRun::ApplyRightToLeftClamp(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, int32 StyleElementIndex,
		FDreamUITextLineProperty& LineProperty, float& InOutPenX, float Baseline, float& InOutContentRight, bool bAnyContent)
	{
		const bool bClampMode = In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis;
		if (!bClampMode || bHasClampContent)return;
		const float LineWidth = bAnyContent ? InOutContentRight - In.FontSpace.X : 0.0f;
		if (LineWidth <= In.Width)return;

		bHasClampContent = true;
		Out.bTruncated = true;
		bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after the line, so we mark it and read it later

		const uint32 CharCodeOfDots = 0x2026;//'…'
		const bool bEllipsis = In.OverflowType == EDreamUITextOverflowType::Ellipsis;
		FDreamUICharData DotsGeo;
		float EllipsisAdvance = 0.0f;
		if (bEllipsis)
		{
			RichTextParseResult = Measured[FMath::Clamp(StyleElementIndex, 0, FMath::Max(0, Measured.Num() - 1))].Style;
			const auto DotsElement = FDreamUIText_TextProcessingElement{ CharCodeOfDots, 0, 1, EDreamUIText_CodeType::Text };
			DotsGeo = GetCharGeo(0, DotsElement, FontSize, false, RichTextParseResult);
			EllipsisAdvance = DotsGeo.XAdvance + In.FontSpace.X;
		}

		auto DropWholeLine = [&]()
		{
			for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
			{
				Out.Items[i].bEmit = false;
				Out.Items[i].bCountsAsVisible = false;
			}
			ClampedLineWidth = 0.0f;
			InOutContentRight = 0.0f;
			InOutPenX = 0.0f;
		};

		const float Keep = In.Width - EllipsisAdvance;
		if (Keep <= 0.0f)
		{
			DropWholeLine();//not even the ellipsis fits, which is what the left-to-right pass does too
			return;
		}
		// Everything left of the cut goes; the pen run's right end is the start of the text.
		const float Cut = LineWidth - Keep;
		float FirstKeptLeft = LineWidth;
		for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
		{
			FDreamTextGlyphItem& Item = Out.Items[i];
			if (Item.Pen.X < Cut - KINDA_SMALL_NUMBER)
			{
				Item.bEmit = false;
				Item.bCountsAsVisible = false;
			}
			else if (Item.bEmit)
			{
				FirstKeptLeft = FMath::Min(FirstKeptLeft, Item.Pen.X);
			}
		}
		if (FirstKeptLeft >= LineWidth)
		{
			DropWholeLine();
			return;
		}
		// Slide what survived back to the line's origin, leaving room for the ellipsis in front of it.
		ShiftLine(LineItemStart, ImageStart, EmojiStart, LineProperty, EllipsisAdvance - FirstKeptLeft);
		const float NewWidth = (LineWidth - FirstKeptLeft) + EllipsisAdvance;
		if (bEllipsis)
		{
			FDreamTextGlyphItem Dots;
			Dots.Kind = EDreamTextItemKind::Glyph;
			Dots.Codepoint = CharCodeOfDots;
			Dots.ElementIndex = FMath::Clamp(StyleElementIndex, 0, FMath::Max(0, Measured.Num() - 1));
			Dots.SourceIndex = TextProcessingArray.IsValidIndex(Dots.ElementIndex) ? TextProcessingArray[Dots.ElementIndex].StringIndex : 0;
			Dots.LineIndex = Out.Lines.Num();
			Dots.Pen = FVector2f(0.0f, Baseline);
			Dots.Glyph = DotsGeo;
			Dots.AdvanceWithSpace = EllipsisAdvance;
			Dots.Style = MakeStyle(RichTextParseResult);
			if (Dots.Style.bUnderline)
			{
				Dots.UnderlineGlyph = GetUnderlineOrStrikethroughCharGeo('_', RichTextParseResult.Size, RichTextParseResult.Bold);
			}
			if (Dots.Style.bStrikethrough)
			{
				Dots.StrikethroughGlyph = GetUnderlineOrStrikethroughCharGeo('-', RichTextParseResult.Size, RichTextParseResult.Bold);
			}
			Dots.bEmit = true;
			Dots.bCountsAsVisible = false;
			Out.Items.Add(Dots);
		}
		ClampedLineWidth = NewWidth;
		InOutContentRight = NewWidth + In.FontSpace.X;
		InOutPenX = NewWidth + In.FontSpace.X;
	}

	void FLayoutRun::PlaceRightToLeftSegment(int32 Start, int32 End, int32 LineIndex, int32 LineItemStart, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent)
	{
		// The run's glyphs are already in visual order; take the ones on this line and lay them out
		// left to right from the pen. Carets and per-character bookkeeping then follow in logical
		// order, each caret at the left edge of its element's glyphs.
		const int32 RunIndex = Measured[Start].RunIndex;
		const FRunInfo& Run = Runs[RunIndex];
		TMap<int32, float> ElementLeft;
		float X = PenX;
		bool bAnyPlaced = false;
		for (int32 g = Run.GlyphStart; g < Run.GlyphEnd; g++)
		{
			const FGlyphSource& G = Glyphs[g];
			if (G.ElementIndex < Start || G.ElementIndex >= End)continue;
			const FMeasured& M = Measured[G.ElementIndex];
			if (!ElementLeft.Contains(G.ElementIndex))
			{
				ElementLeft.Add(G.ElementIndex, X);
			}
			if (!M.bWhitespace)
			{
				FDreamTextGlyphItem Item;
				Item.Kind = EDreamTextItemKind::Glyph;
				Item.Codepoint = TextProcessingArray[G.ElementIndex].Unicode;
				Item.ElementIndex = G.ElementIndex;
				Item.SourceIndex = TextProcessingArray[G.ElementIndex].StringIndex;
				Item.LineIndex = LineIndex;
				Item.Pen = FVector2f(X + G.XOffset, Baseline + G.YOffset);
				Item.Glyph = G.Quad;
				Item.AdvanceWithSpace = G.XAdvance;
				Item.Style = MakeStyle(M.Style);
				if (G.Quad.bPending)
				{
					Out.bHasPendingGlyphs = true;
				}
				Item.bCountsAsVisible = !bHasClampContent;//see PlaceElement: pending glyphs keep their place
				if (!bHasClampContent && !G.Quad.bPending)
				{
					Item.bEmit = true;
					if (Item.Style.bUnderline)
					{
						Item.UnderlineGlyph = GetUnderlineOrStrikethroughCharGeo('_', M.Style.Size, M.Style.Bold);
					}
					if (Item.Style.bStrikethrough)
					{
						Item.StrikethroughGlyph = GetUnderlineOrStrikethroughCharGeo('-', M.Style.Size, M.Style.Bold);
					}
				}
				Out.Items.Add(Item);
			}
			else
			{
				FDreamTextGlyphItem Item;
				Item.Kind = EDreamTextItemKind::Space;
				Item.Codepoint = TextProcessingArray[G.ElementIndex].Unicode;
				Item.ElementIndex = G.ElementIndex;
				Item.SourceIndex = TextProcessingArray[G.ElementIndex].StringIndex;
				Item.LineIndex = LineIndex;
				Item.Pen = FVector2f(X, Baseline);
				Item.Glyph = G.Quad;
				Item.AdvanceWithSpace = G.XAdvance;
				Item.Style = MakeStyle(M.Style);
				Out.Items.Add(Item);
			}
			X += G.XAdvance + (M.GlyphCount > 0 && g == M.GlyphStart + M.GlyphCount - 1 ? In.FontSpace.X : 0.0f);
			if (!M.bWhitespace)
			{
				bAnyPlaced = true;
			}
			// Cut inside the segment, in visual order: the next glyph to the right is what has to fit.
			if (g + 1 < Run.GlyphEnd)
			{
				const FGlyphSource& NextGlyph = Glyphs[g + 1];
				if (NextGlyph.ElementIndex >= Start && NextGlyph.ElementIndex < End)
				{
					float ClampPen = X;
					float ClampContentRight = FMath::Max(ContentRight, X);
					if (TryClampAt(G.ElementIndex, NextGlyph.XAdvance, LineItemStart, ClampPen, Baseline, ClampContentRight, bAnyContent || bAnyPlaced))
					{
						X = ClampPen;
						ContentRight = ClampContentRight;
						bAnyContent = bAnyContent || bAnyPlaced;
					}
				}
			}
		}
		const float SegmentEnd = X;
		for (int32 i = Start; i < End; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			FDreamUITextCaretProperty CaretProperty;
			const float* Left = ElementLeft.Find(i);
			CaretProperty.CaretPosition = FVector2f((Left ? *Left : SegmentEnd) - HalfFontSpaceX, LineCentre);
			CaretProperty.CharIndex = CaretIndexOf(i);
			LineProperty.CaretPropertyList.Add(CaretProperty);
			if (!M.bWhitespace && M.Advance > 0.0f)
			{
				bAnyContent = true;
			}
		}
		PenX = SegmentEnd;
		if (bAnyContent)
		{
			ContentRight = FMath::Max(ContentRight, SegmentEnd);
		}
	}

	void FLayoutRun::ComputeLineBox(int32 LineIndex, float& OutAscent, float& OutDescent, float& OutLineHeight)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		const FSizeMetrics& Base = MetricsFor(FontSize);
		OutAscent = Base.Ascent;
		OutDescent = Base.Descent;
		OutLineHeight = OriginLineHeight;
		// The strut is the primary face at the text's own size; anything on the line with a taller box
		// grows it -- a rich-text <size>, or a glyph that a fallback face supplied (CJK under a Latin
		// primary), which used to be laid out against the primary's ascent and hang out of the line.
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			if (!In.bRichText && M.FaceIndex == 0)continue;
			const FSizeMetrics& Metrics = MetricsFor(M.Style.Size, M.FaceIndex);
			OutAscent = FMath::Max(OutAscent, Metrics.Ascent);
			OutDescent = FMath::Max(OutDescent, Metrics.Descent);
			OutLineHeight = FMath::Max(OutLineHeight, Metrics.LineHeight);
		}
	}

	void FLayoutRun::PlaceLine(int32 LineIndex, float LineTop)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		const int32 LineItemStart = Out.Items.Num();
		const int32 ImageStart = Out.Images.Num();
		const int32 EmojiStart = Out.Emojis.Num();
		FDreamUITextLineProperty LineProperty;

		// The line box: as tall as the tallest font box on the line, every glyph sitting on one
		// baseline. Extra height beyond ascent + descent is split above and below (CSS half-leading).
		// Carets and inline objects anchor on the line's centre, which is the contract they had.
		float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
		ComputeLineBox(LineIndex, Ascent, Descent, LineHeight);
		CurrentLineHeight = LineHeight;
		const float Baseline = LineTop - (LineHeight - (Ascent + Descent)) * 0.5f - Ascent;
		const float LineCentre = LineTop - LineHeight * 0.5f;

		float PenX = 0.0f;
		float ContentRight = 0.0f;//pen after the last non-whitespace element: trailing spaces hang outside the line's width
		bool bAnyContent = false;

		// Segments: maximal stretches of one run (or of unshaped elements). A left-to-right paragraph
		// places them in logical order with right-to-left ones reversed inside; a right-to-left
		// paragraph places the segments themselves from right to left.
		struct FSegment { int32 Start; int32 End; bool bRightToLeft; };
		TArray<FSegment> Segments;
		bool bBaseRightToLeft = false;
		for (int32 i = Range.Start; i < Range.End; i++)
		{
			const FMeasured& M = Measured[i];
			if (M.bSkipped)continue;
			bBaseRightToLeft |= M.bBaseRightToLeft;
			const bool bRTL = M.RunIndex >= 0 && Runs[M.RunIndex].bRightToLeft;
			const int32 RunIndex = M.RunIndex;
			if (Segments.Num() > 0 && Segments.Last().bRightToLeft == bRTL
				&& (RunIndex < 0 || Measured[Segments.Last().End - 1].RunIndex == RunIndex || !bRTL))
			{
				Segments.Last().End = i + 1;
			}
			else
			{
				Segments.Add({ i, i + 1, bRTL });
			}
		}
		if (bBaseRightToLeft)
		{
			Algo::Reverse(Segments);
		}
		// A right-to-left line is cut after it is placed, from the other end: see ApplyRightToLeftClamp.
		bClampWhilePlacing = !bBaseRightToLeft;
		for (const FSegment& Segment : Segments)
		{
			if (Segment.bRightToLeft)
			{
				PlaceRightToLeftSegment(Segment.Start, Segment.End, LineIndex, LineItemStart, PenX, Baseline, LineCentre, LineProperty, ContentRight, bAnyContent);
			}
			else
			{
				for (int32 i = Segment.Start; i < Segment.End; i++)
				{
					if (Measured[i].bSkipped)continue;
					PlaceElement(i, LineIndex, PenX, Baseline, LineCentre, LineProperty, ContentRight, bAnyContent);

					// The cut is decided by whether the NEXT element would fit.
					if (i + 1 < Range.End && !Measured[i + 1].bSkipped)
					{
						TryClampAt(i, Measured[i + 1].ClusterAdvance, LineItemStart, PenX, Baseline, ContentRight, bAnyContent);
					}
				}
			}
		}

		if (bBaseRightToLeft)
		{
			ApplyRightToLeftClamp(LineItemStart, ImageStart, EmojiStart, Range.Start, LineProperty, PenX, Baseline, ContentRight, bAnyContent);
		}
		bClampWhilePlacing = true;

		// The last line that fits the box: it is elided at its end even if it fits the width, because
		// the text it stands in for is the lines below, not characters off the right edge.
		if (bEllipsizeThisLine && !bHasClampContent)
		{
			bHasClampContent = true;
			Out.bTruncated = true;
			ClampedLineWidth = bAnyContent ? ContentRight - In.FontSpace.X : 0.0f;
			bShouldSetParagraphHeightForClampContent = true;
			if (In.OverflowType == EDreamUITextOverflowType::Ellipsis)
			{
				const int32 StyleElement = FMath::Clamp(Range.End - 1, 0, FMath::Max(0, Measured.Num() - 1));
				RichTextParseResult = Measured.IsValidIndex(StyleElement) ? Measured[StyleElement].Style : RichTextParseResult;
				ApplyEllipsis(StyleElement, LineItemStart, PenX, Baseline, /*bMayGrowLine*/true);
				ContentRight = PenX;
				ClampedLineWidth = ContentRight - In.FontSpace.X;
			}
		}

		//end caret: the newline's own for a hard break, the string's end for the last line, nameless for a soft wrap
		{
			FDreamUITextCaretProperty CaretProperty;
			CaretProperty.CaretPosition = FVector2f(PenX - HalfFontSpaceX, LineCentre);
			if (Range.HardBreakElement != -1)
			{
				CaretProperty.CharIndex = CaretIndexOf(Range.HardBreakElement);
			}
			else if (LineIndex == LineRanges.Num() - 1)
			{
				CaretProperty.CharIndex = In.bRichText ? In.Content.Len() : TextProcessingArray.Num();
			}
			else
			{
				CaretProperty.CharIndex = -1;
			}
			LineProperty.CaretPropertyList.Add(CaretProperty);
		}

		const float LineWidth = bAnyContent ? ContentRight - In.FontSpace.X : 0.0f;
		AlignLine(LineItemStart, ImageStart, EmojiStart, LineProperty, bHasClampContent ? ClampedLineWidth : LineWidth);
		Out.Lines.Add(LineProperty);
	}

	void FLayoutRun::Place()
	{
		// Lines stack down from the paragraph's top edge at y = 0. The line-height scale stretches
		// the gap below each line, as it always has; the glyphs keep their place inside the box.
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
				float NextAscent = 0.0f, NextDescent = 0.0f, NextLineHeight = 0.0f;
				ComputeLineBox(LineIndex + 1, NextAscent, NextDescent, NextLineHeight);
				float ThisAscent = 0.0f, ThisDescent = 0.0f, ThisLineHeight = 0.0f;
				ComputeLineBox(LineIndex, ThisAscent, ThisDescent, ThisLineHeight);
				const float AfterThis = ParagraphHeight + ThisLineHeight * LineHeightScale + In.FontSpace.Y;
				if (AfterThis + NextLineHeight > In.Height + KINDA_SMALL_NUMBER)
				{
					bEllipsizeThisLine = true;
				}
			}
			const bool bWasLastVisibleLine = bEllipsizeThisLine;
			PlaceLine(LineIndex, LineTop);
			bEllipsizeThisLine = false;
			const float LineAdvance = CurrentLineHeight * LineHeightScale + In.FontSpace.Y;
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
		Out.Reset();
		Prepare();
		Preprocess();
		Measure();
		if (ShouldWrap())
		{
			ComputeBreakOpportunities();
		}
		BreakLines();
		Place();
		Finish();
	}

	void FLayoutRun::Finish()
	{
		// Custom tag ranges are resolved at the end of this function, once the one visible-character
		// numbering exists; nothing may read Out.CustomTags before then.

		//remove last line's space Y
		ParagraphHeight -= In.FontSpace.Y;
		ParagraphHeight_ForClampContent -= In.FontSpace.Y;
		const float ParagraphHeightWithClamp = bHasClampContent ? ParagraphHeight_ForClampContent : ParagraphHeight;

		// Preferred width is the unwrapped width: the widest paragraph as it would be on one line.
		float PreferredWidth = 0.0f;
		{
			float Width = 0.0f;
			bool bAny = false;
			for (int32 i = 0; i < Measured.Num(); i++)
			{
				const FMeasured& M = Measured[i];
				if (M.bSkipped)continue;
				if (M.bHardBreak)
				{
					PreferredWidth = FMath::Max(PreferredWidth, bAny ? Width - In.FontSpace.X : 0.0f);
					Width = 0.0f;
					bAny = false;
					continue;
				}
				Width += M.Advance;
				bAny = true;
			}
			PreferredWidth = FMath::Max(PreferredWidth, bAny ? Width - In.FontSpace.X : 0.0f);
		}
		Out.PreferredSize.X = PreferredWidth;
		Out.PreferredSize.Y = ParagraphHeight;

		const float PivotOffsetX = In.Width * (0.5f - In.Pivot.X);
		const float PivotOffsetY = In.Height * (0.5f - In.Pivot.Y);
		float XOffset = PivotOffsetX;
		switch (In.ParagraphHAlign)
		{
		case EDreamUITextParagraphHorizontalAlign::Left:
			XOffset += -In.Width * 0.5f;
			break;
		case EDreamUITextParagraphHorizontalAlign::Center:
			break;
		case EDreamUITextParagraphHorizontalAlign::Right:
			XOffset += In.Width * 0.5f;
			break;
		}
		float YOffset = PivotOffsetY;
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

		// THE numbering of visible characters, built once, here, from the items the painter will walk.
		// One per ELEMENT, in the order they are painted: a code point that shaped into two glyphs is
		// one character, a character whose glyphs have not landed yet is still one character, and a
		// character a clamp threw away is none. Everything that addresses characters -- the display
		// list's count, the painter's FDreamUITextCharProperty list that TextAnimation walks, and the
		// rich-text custom tag ranges resolved below -- reads this one answer. They used to be three
		// separate counts that agreed only when nothing was truncated and nothing was still rasterizing.
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
			const int32 LastVisible = FMath::Max(0, Out.VisibleCharCount - 1);
			// A tag can open or close on an element that is not a visible character at all -- a space,
			// an inline image, or one a clamp removed -- so each end walks to the nearest one that is.
			auto VisibleAtOrAfter = [&VisibleIndexOfElement, LastVisible](int32 Element)
			{
				for (int32 e = FMath::Max(0, Element); e < VisibleIndexOfElement.Num(); e++)
				{
					if (VisibleIndexOfElement[e] != INDEX_NONE)return VisibleIndexOfElement[e];
				}
				return LastVisible;
			};
			auto VisibleAtOrBefore = [&VisibleIndexOfElement](int32 Element)
			{
				for (int32 e = FMath::Min(Element, VisibleIndexOfElement.Num() - 1); e >= 0; e--)
				{
					if (VisibleIndexOfElement[e] != INDEX_NONE)return VisibleIndexOfElement[e];
				}
				return 0;
			};
			for (int32 TagIndex = 0; TagIndex < Out.CustomTags.Num(); TagIndex++)
			{
				if (!CustomTagElements.IsValidIndex(TagIndex))continue;
				const FIntPoint Range = CustomTagElements[TagIndex];
				FDreamUIText_RichTextCustomTag& Tag = Out.CustomTags[TagIndex];
				Tag.CharIndexStart = VisibleAtOrAfter(Range.X);
				// An unclosed tag runs to the end of the text, which is what it used to do too.
				Tag.CharIndexEnd = Range.Y == INDEX_NONE ? LastVisible : VisibleAtOrBefore(Range.Y);
				Tag.CharIndexEnd = FMath::Max(Tag.CharIndexEnd, Tag.CharIndexStart);
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
