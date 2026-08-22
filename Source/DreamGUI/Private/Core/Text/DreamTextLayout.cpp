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
	return Content.Equals(Other.Content)
		&& Width == Other.Width
		&& Height == Other.Height
		&& Pivot == Other.Pivot
		&& Color == Other.Color
		&& RenderOpacityForRichText == Other.RenderOpacityForRichText
		&& FontSpace == Other.FontSpace
		&& FontSize == Other.FontSize
		&& ParagraphHAlign == Other.ParagraphHAlign
		&& ParagraphVAlign == Other.ParagraphVAlign
		&& OverflowType == Other.OverflowType
		&& WrappingPolicy == Other.WrappingPolicy
		&& PhraseWrap == Other.PhraseWrap
		&& bUseKerning == Other.bUseKerning
		&& FontStyle == Other.FontStyle
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
		TMap<float, FSizeMetrics> MetricsBySize;
		const FSizeMetrics& MetricsFor(float Size);

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
		int32 CurrentVisibleCharCount = 0;
		bool bHasClampContent = false;
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
		void PlaceRightToLeftSegment(int32 Start, int32 End, int32 LineIndex, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent);
		void ComputeBreakOpportunities();
		void BreakLines();
		void Place();
		void PlaceLine(int32 LineIndex, float LineTop);
		void AlignLine(int32 LineItemStart, int32 ImageStart, int32 EmojiStart, FDreamUITextLineProperty& LineProperty, float LineWidth);
		void Finish();

		bool IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		bool IsSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const;
		void GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, FName ImageTag) const;
		void GetEmojiCharData(FDreamUICharData& OverrideCharData, float InFontSize, uint32 EmojiCode) const;
		FDreamUICharData GetCharGeo(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, float InFontSize, bool bInBold, const FRichTextParseResult& RichTextResult) const;
		float GetCharGeoXAdv(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, const FRichTextParseResult& RichTextResult) const;
		FDreamUICharData GetUnderlineOrStrikethroughCharGeo(uint32 CharCode, float OverrideFontSize, bool bBold) const;
		static FDreamTextItemStyle MakeStyle(const FRichTextParseResult& Result);
		float ItemMaxX(const FDreamTextGlyphItem& Item) const;
		int32 CaretIndexOf(int32 ElementIndex) const;
		void ApplyEllipsis(int32 ElementIndex, int32 LineItemStart, float& InOutPenX, float Baseline);
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
		ItalicSlope = Font->GetGlyphPaintStyle(FVector2f(1.0f, 1.0f)).ItalicSlope;
		bUseKerning = In.bUseKerning && Font->HasKerning();

		const bool bUseBold = In.FontStyle == EDreamUITextFontStyle::Bold || In.FontStyle == EDreamUITextFontStyle::BoldAndItalic;
		const bool bUseItalic = In.FontStyle == EDreamUITextFontStyle::Italic || In.FontStyle == EDreamUITextFontStyle::BoldAndItalic;

		if (In.bRichText)
		{
			RichTextParser.Clear();
			RichTextParser.Prepare(FontSize, In.Color, In.RenderOpacityForRichText, bUseBold, bUseItalic, In.RichTextFilterFlags, RichTextParseResult);
		}
		else
		{
			RichTextParseResult.Color = In.Color;
			RichTextParseResult.Bold = bUseBold;
			RichTextParseResult.Italic = bUseItalic;
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

	const FLayoutRun::FSizeMetrics& FLayoutRun::MetricsFor(float Size)
	{
		if (const FSizeMetrics* Found = MetricsBySize.Find(Size))
		{
			return *Found;
		}
		FSizeMetrics M;
		M.Ascent = Font->GetAscent(Size);
		M.Descent = Font->GetDescent(Size);
		M.LineHeight = Font->GetLineHeight(Size);
		return MetricsBySize.Add(Size, M);
	}

	bool FLayoutRun::IsRichTextImageSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const
	{
		return CharCode == ' ' && In.bRichText && !RichTextResult.ImageTag.IsNone();
	}

	bool FLayoutRun::IsSpace(uint32 CharCode, const FRichTextParseResult& RichTextResult) const
	{
		return CharCode == ' ' && !(In.bRichText && !RichTextResult.ImageTag.IsNone());
	}

	void FLayoutRun::GetRichTextImageCharData(FDreamUICharData& OverrideCharData, float InFontSize, FName ImageTag) const
	{
		//image use font size as default width & height & xadvance
		OverrideCharData.Width = OverrideCharData.Height = OverrideCharData.XAdvance = InFontSize * OneDivideRootCanvasScale;

		FIntVector2 ImageSize;
		if (IsValid(RichTextImageData) && RichTextImageData->GetImageSize(ImageTag, ImageSize))
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
			InFontSize = InFontSize * Scale;
			InFontSize = FMath::Clamp(InFontSize, 0.0f, MaxFontSize);
			if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
			{
				GetRichTextImageCharData(OverrideCharData, InFontSize, RichTextResult.ImageTag);
			}
			else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
			{
				GetEmojiCharData(OverrideCharData, InFontSize, CharElement.Unicode);
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
		}
		else
		{
			if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
			{
				GetRichTextImageCharData(OverrideCharData, InFontSize, RichTextResult.ImageTag);
			}
			else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
			{
				GetEmojiCharData(OverrideCharData, InFontSize, CharElement.Unicode);
			}
		}
		if (bUseKerning && PrevCharCode != CharElement.Unicode)
		{
			const float KerningValue = Font->GetKerning(PrevCharCode, CharElement.Unicode, InFontSize);
			OverrideCharData.XAdvance += KerningValue;
			OverrideCharData.XOffset += KerningValue;
		}
		return OverrideCharData;
	}

	float FLayoutRun::GetCharGeoXAdv(uint32 PrevCharCode, const FDreamUIText_TextProcessingElement& CharElement, const FRichTextParseResult& RichTextResult) const
	{
		if (IsRichTextImageSpace(CharElement.Unicode, RichTextResult))
		{
			return RichTextResult.Size;//image use font size as width & height & xadvance
		}
		else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
		{
			FIntVector2 Size;
			if (IsValid(EmojiData) && EmojiData->GetImageSize(CharElement.Unicode, Size))
			{
				return (float)Size.X;
			}
			return 0.0f;
		}
		else
		{
			auto CharData = Font->GetCharData(CharElement.Unicode, RichTextResult.Size, RichTextResult.Bold);
			if (bUseKerning && PrevCharCode != CharElement.Unicode)
			{
				const float KerningValue = Font->GetKerning(PrevCharCode, CharElement.Unicode, RichTextResult.Size);
				return CharData.XAdvance + KerningValue;
			}
			return CharData.XAdvance;
		}
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
		// shape. Kerning pairs with the previous character of the paragraph; the first is paired with
		// itself, which the font treats as "no pair".
		uint32 PrevCharCode = 0;
		bool bFirst = true;
		for (int32 i = Start; i < End; i++)
		{
			FMeasured& M = Measured[i];
			if (M.bSkipped || M.bHardBreak)continue;
			const auto& Element = TextProcessingArray[i];
			RichTextParseResult = M.Style;
			M.Glyph = GetCharGeo(bFirst ? Element.Unicode : PrevCharCode, Element, M.Style.Size, M.Style.Bold, M.Style);
			M.ClusterAdvance = M.Glyph.XAdvance;
			M.Advance = M.ClusterAdvance + In.FontSpace.X;
			M.RunIndex = -1;
			M.GlyphStart = Glyphs.Num();
			M.GlyphCount = 1;
			FGlyphSource& G = Glyphs.AddDefaulted_GetRef();
			G.Quad = M.Glyph;
			G.XAdvance = M.Glyph.XAdvance;
			G.ElementIndex = i;
			PrevCharCode = Element.Unicode;
			bFirst = false;
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
			const float ScaledSize = FMath::Clamp(InFontSize * Scale, 0.0f, MaxFontSize);
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
		if (!FDreamTextShaper::ShapeParagraph(ShapeElements, Font, bUseKerning, ShapedRuns, bBaseRightToLeft))
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
				// Inline objects are measured by the layout, the way they always were.
				RichTextParseResult = M.Style;
				M.Glyph = GetCharGeo(TextProcessingArray[i].Unicode, TextProcessingArray[i], M.Style.Size, M.Style.Bold, M.Style);
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
		const bool bWrap = In.OverflowType == EDreamUITextOverflowType::VerticalOverflow;
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

	void FLayoutRun::ApplyEllipsis(int32 ElementIndex, int32 LineItemStart, float& InOutPenX, float Baseline)
	{
		//move back and replace chars by ...
		const uint32 CharCodeOfDots = 0x2026;//'…'
		const auto CharElementOfDots = FDreamUIText_TextProcessingElement{ CharCodeOfDots, ElementIndex, 1, EDreamUIText_CodeType::Text };
		const auto CharGeoOfDots = GetCharGeo(CharCodeOfDots, CharElementOfDots, FontSize, false, RichTextParseResult);
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
				if (bEmit && !G.Quad.bPending)
				{
					GlyphItem.bEmit = true;
					GlyphItem.bCountsAsVisible = true;
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
			CurrentVisibleCharCount++;
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
				CustomTag.CharIndexStart = FMath::Max(0, CurrentVisibleCharCount - 1);//-1 as index; incase first char is invisible char
				CustomTag.CharIndexEnd = -1;
				Out.CustomTags.Add(CustomTag);
			}
			break;
			case ECustomTagMode::End:
			{
				const FName TagName = M.Style.CustomTag;
				const int32 FoundIndex = Out.CustomTags.IndexOfByPredicate([TagName](const FDreamUIText_RichTextCustomTag& A) {
					return A.TagName == TagName;
					});
				if (FoundIndex != -1)
				{
					Out.CustomTags[FoundIndex].CharIndexEnd = CurrentVisibleCharCount - 1;//-1 as index
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

	void FLayoutRun::PlaceRightToLeftSegment(int32 Start, int32 End, int32 LineIndex, float& PenX, float Baseline, float LineCentre, FDreamUITextLineProperty& LineProperty, float& ContentRight, bool& bAnyContent)
	{
		// The run's glyphs are already in visual order; take the ones on this line and lay them out
		// left to right from the pen. Carets and per-character bookkeeping then follow in logical
		// order, each caret at the left edge of its element's glyphs.
		const int32 RunIndex = Measured[Start].RunIndex;
		const FRunInfo& Run = Runs[RunIndex];
		TMap<int32, float> ElementLeft;
		float X = PenX;
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
				if (!bHasClampContent && !G.Quad.bPending)
				{
					Item.bEmit = true;
					Item.bCountsAsVisible = true;
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
			if (M.bVisibleGlyph)
			{
				CurrentVisibleCharCount++;
			}
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

	void FLayoutRun::PlaceLine(int32 LineIndex, float LineTop)
	{
		const FLineRange& Range = LineRanges[LineIndex];
		const int32 LineItemStart = Out.Items.Num();
		const int32 ImageStart = Out.Images.Num();
		const int32 EmojiStart = Out.Emojis.Num();
		FDreamUITextLineProperty LineProperty;
		const bool bClampMode = In.OverflowType == EDreamUITextOverflowType::Truncate || In.OverflowType == EDreamUITextOverflowType::Ellipsis;

		// The line box: as tall as the tallest font box on the line, every glyph sitting on one
		// baseline. Extra height beyond ascent + descent is split above and below (CSS half-leading).
		// Carets and inline objects anchor on the line's centre, which is the contract they had.
		const FSizeMetrics& Base = MetricsFor(FontSize);
		float Ascent = Base.Ascent;
		float Descent = Base.Descent;
		float LineHeight = OriginLineHeight;
		if (In.bRichText)
		{
			for (int32 i = Range.Start; i < Range.End; i++)
			{
				const FMeasured& M = Measured[i];
				if (M.bSkipped || M.bHardBreak)continue;
				const FSizeMetrics& Metrics = MetricsFor(M.Style.Size);
				Ascent = FMath::Max(Ascent, Metrics.Ascent);
				Descent = FMath::Max(Descent, Metrics.Descent);
				LineHeight = FMath::Max(LineHeight, Metrics.LineHeight);
			}
		}
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
		for (const FSegment& Segment : Segments)
		{
			if (Segment.bRightToLeft)
			{
				PlaceRightToLeftSegment(Segment.Start, Segment.End, LineIndex, PenX, Baseline, LineCentre, LineProperty, ContentRight, bAnyContent);
			}
			else
			{
				for (int32 i = Segment.Start; i < Segment.End; i++)
				{
					if (Measured[i].bSkipped)continue;
					PlaceElement(i, LineIndex, PenX, Baseline, LineCentre, LineProperty, ContentRight, bAnyContent);

					// Truncate and Ellipsis measure against the box, not the wrap width: they are about
					// what fits on screen. The cut is decided by whether the NEXT element would fit.
					if (bClampMode && !bHasClampContent && i + 1 < Range.End)
					{
						const FMeasured& Next = Measured[i + 1];
						if (!Next.bSkipped && PenX + Next.ClusterAdvance > In.Width)
						{
							bHasClampContent = true;
							Out.bTruncated = true;
							ClampedLineWidth = bAnyContent ? ContentRight - In.FontSpace.X : 0.0f;
							bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after the line, so we mark it and read it later
							if (In.OverflowType == EDreamUITextOverflowType::Ellipsis)
							{
								RichTextParseResult = Measured[i].Style;
								ApplyEllipsis(i, LineItemStart, PenX, Baseline);
								ContentRight = PenX;
								ClampedLineWidth = ContentRight - In.FontSpace.X;
							}
						}
					}
				}
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
		float LineTop = 0.0f;
		for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); LineIndex++)
		{
			PlaceLine(LineIndex, LineTop);
			const float LineAdvance = CurrentLineHeight * LineHeightScale + In.FontSpace.Y;
			LineTop -= LineAdvance;
			ParagraphHeight += LineAdvance;
			if (bHasClampContent && bShouldSetParagraphHeightForClampContent)
			{
				bShouldSetParagraphHeightForClampContent = false;
				ParagraphHeight_ForClampContent = ParagraphHeight;
			}
		}
	}

	void FLayoutRun::Run()
	{
		Out.Reset();
		Prepare();
		Preprocess();
		Measure();
		if (In.OverflowType == EDreamUITextOverflowType::VerticalOverflow)
		{
			ComputeBreakOpportunities();
		}
		BreakLines();
		Place();
		Finish();
	}

	void FLayoutRun::Finish()
	{
		//verify custom tag
		if (In.bRichText)
		{
			for (auto& Item : Out.CustomTags)
			{
				if (Item.CharIndexEnd == -1)
				{
					Item.CharIndexEnd = CurrentVisibleCharCount - 1;//-1 as index
				}
			}
		}

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

		Out.VisibleCharCount = 0;
		for (const auto& Item : Out.Items)
		{
			if (Item.bEmit && Item.bCountsAsVisible)
			{
				Out.VisibleCharCount++;
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
