// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextLayout.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIRichTextImageData_BaseObject.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/FRichTextParser.h"

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
	 * One layout pass. A class rather than a function so the state the old 900-line function kept in
	 * forty captured locals has names and a scope; the algorithm itself is unchanged.
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
		enum class ENewLineMode
		{
			None,//not new line
			LineBreak,//this new line come from line break
			Space,//this new line come from space char
			Overflow,//this new line come from overflow
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
		float VerticalOffset = 0.0f;
		float OriginLineHeight = 0.0f;
		float LineHeightScale = 1.0f;
		float WrapWidth = 0.0f;
		float HalfFontSpaceX = 0.0f;
		float ItalicSlope = 0.0f;

		FRichTextParser RichTextParser;
		FRichTextParseResult RichTextParseResult;
		TArray<FRichTextParseResult> RichTextPropertyArray;
		TArray<FDreamUIText_TextProcessingElement> TextProcessingArray;

		// Running state of the main loop.
		FVector2f CurrentLineOffset = FVector2f::ZeroVector;
		float CurrentLineWidth = 0.0f;
		float CurrentLineHeight = 0.0f;
		float ParagraphHeight = 0.0f;
		float FirstLineHeight = 0.0f;
		float MaxLineWidth = 0.0f;
		float CurrentPreferredWidth = 0.0f;
		float MaxPreferredWidth = 0.0f;
		int32 LineItemStart = 0;//first item of the current line
		int32 CurrentVisibleCharCount = 0;
		int32 ImageStartIndexInCurrentLine = 0;
		int32 EmojiStartIndexInCurrentLine = 0;
		FDreamUITextLineProperty LineProperty;
		FVector2f CaretPosition = FVector2f::ZeroVector;
		int32 LinesCount = 0;
		bool bHasClampContent = false;
		float CurrentLineWidth_ForClampContent = 0.0f;
		float ParagraphHeight_ForClampContent = 0.0f;
		bool bShouldSetParagraphHeightForClampContent = false;
		ENewLineMode NewLineMode = ENewLineMode::None;

		void Prepare();
		void Preprocess();
		void NewLine(int32 CharIndex, bool bWithCaret, ENewLineMode InNewLineMode, float ExtraSizeForPreferredWidth);
		void AlignLine(float LineWidthWithClamp);
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
		void ApplyEllipsis(int32 CharIndex);
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

		//some font may not render at vertical center, use this to modify it
		VerticalOffset = Font->GetVerticalOffset(FontSize);
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
		FirstLineHeight = CurrentLineHeight;
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
		const float CalculatedCharFixedOffset = In.bRichText ? Font->GetVerticalOffset(InFontSize) : VerticalOffset;

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
			OverrideCharData.YOffset = OverrideCharData.YOffset * OneDivideScale + CalculatedCharFixedOffset;
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
			OverrideCharData.YOffset += CalculatedCharFixedOffset;
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
		CharData.YOffset += Font->GetVerticalOffset(OverrideFontSize);

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

	void FLayoutRun::AlignLine(float LineWidthWithClamp)
	{
		float XOffset = 0.0f;
		switch (In.ParagraphHAlign)
		{
		case EDreamUITextParagraphHorizontalAlign::Center:
			XOffset = -LineWidthWithClamp * 0.5f;
			break;
		case EDreamUITextParagraphHorizontalAlign::Right:
			XOffset = -LineWidthWithClamp;
			break;
		default:
			break;
		}

		if (In.bRichText)
		{
			// Rich text centres a line of mixed sizes on its tallest glyph. Carets are deliberately not
			// shifted here: that is how the old pipeline behaved, and caret placement is UITextInput's
			// contract, so it moves with the baseline model in a later phase rather than here.
			const float YOffset = -(CurrentLineHeight - FontSize) * 0.5f;
			for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
			{
				Out.Items[i].Pen.X += XOffset;
				Out.Items[i].Pen.Y += YOffset;
			}
			for (int32 i = ImageStartIndexInCurrentLine; i < Out.Images.Num(); i++)
			{
				Out.Images[i].Position.X += XOffset;
				Out.Images[i].Position.Y += YOffset;
			}
			for (int32 i = EmojiStartIndexInCurrentLine; i < Out.Emojis.Num(); i++)
			{
				Out.Emojis[i].Position.X += XOffset;
				Out.Emojis[i].Position.Y += YOffset;
			}
			ImageStartIndexInCurrentLine = Out.Images.Num();
			EmojiStartIndexInCurrentLine = Out.Emojis.Num();
		}
		else
		{
			for (int32 i = LineItemStart; i < Out.Items.Num(); i++)
			{
				Out.Items[i].Pen.X += XOffset;
			}
			for (auto& Caret : LineProperty.CaretPropertyList)
			{
				Caret.CaretPosition.X += XOffset;
			}
			for (int32 i = EmojiStartIndexInCurrentLine; i < Out.Emojis.Num(); i++)
			{
				Out.Emojis[i].Position.X += XOffset;
			}
			EmojiStartIndexInCurrentLine = Out.Emojis.Num();
		}
	}

	void FLayoutRun::NewLine(int32 CharIndex, bool bWithCaret, ENewLineMode InNewLineMode, float ExtraSizeForPreferredWidth)
	{
		//add end caret position
		CurrentLineWidth -= In.FontSpace.X;//last char of a line don't need space
		const float CurrentLineWidthWithClamp = bHasClampContent ? CurrentLineWidth_ForClampContent : CurrentLineWidth;
		MaxLineWidth = FMath::Max(MaxLineWidth, CurrentLineWidth);
		if (InNewLineMode != ENewLineMode::None)
		{
			if (InNewLineMode == ENewLineMode::LineBreak)//if lineBreak then we should start a new preferredWidth
			{
				CurrentPreferredWidth += CurrentLineWidth;
				MaxPreferredWidth = FMath::Max(MaxPreferredWidth, CurrentPreferredWidth);
				CurrentPreferredWidth = 0;//lineBreak cause a newline and recalculation of preferredWidth
			}
			else
			{
				CurrentPreferredWidth += CurrentLineWidth + ExtraSizeForPreferredWidth;
			}
		}

		FDreamUITextCaretProperty CaretProperty;
		CaretProperty.CaretPosition = CaretPosition;
		CaretProperty.CharIndex = bWithCaret ? CharIndex : -1;
		LineProperty.CaretPropertyList.Add(CaretProperty);

		AlignLine(CurrentLineWidthWithClamp);

		Out.Lines.Add(LineProperty);
		LineProperty = FDreamUITextLineProperty();
		LineItemStart = Out.Items.Num();

		CurrentLineWidth = 0;
		CurrentLineOffset.X = 0;
		const float LineAdvance = (In.bRichText ? CurrentLineHeight : OriginLineHeight) * LineHeightScale + In.FontSpace.Y;
		CurrentLineOffset.Y -= LineAdvance;
		ParagraphHeight += LineAdvance;
		if (bHasClampContent && bShouldSetParagraphHeightForClampContent)
		{
			bShouldSetParagraphHeightForClampContent = false;
			ParagraphHeight_ForClampContent = ParagraphHeight;
		}
		LinesCount++;

		//set caret position for empty newline
		CaretPosition.X = CurrentLineOffset.X - HalfFontSpaceX;
		CaretPosition.Y = CurrentLineOffset.Y;
		//store first line height for paragraph align
		if (LinesCount == 1)
		{
			FirstLineHeight = In.bRichText ? CurrentLineHeight : OriginLineHeight;
		}
		//set line height to origin
		CurrentLineHeight = OriginLineHeight;

		NewLineMode = InNewLineMode;
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

	void FLayoutRun::ApplyEllipsis(int32 CharIndex)
	{
		//move back and replace chars by ...
		const uint32 CharCodeOfDots = 0x2026;//'…'
		const auto CharElementOfDots = FDreamUIText_TextProcessingElement{ CharCodeOfDots, CharIndex, 1, EDreamUIText_CodeType::Text };
		const auto CharGeoOfDots = GetCharGeo(CharCodeOfDots, CharElementOfDots, FontSize, false, RichTextParseResult);
		if (CurrentLineOffset.X < CharGeoOfDots.XAdvance)//remove all if it can't fit the char-of-dots
		{
			for (auto& Item : Out.Items)
			{
				Item.bEmit = false;
			}
			return;
		}

		const float LineOffsetPointToStripOff = CurrentLineOffset.X - CharGeoOfDots.XAdvance - HalfFontSpaceX;
		//remove char geometry on tail of data, if the char's vertex position greater than dots
		for (int32 ItemIndex = Out.Items.Num() - 1; ItemIndex >= 0; ItemIndex--)
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

		CurrentLineOffset.X = LineOffsetPointToStripOff;
		//push dots geometry to tail
		FDreamTextGlyphItem Dots;
		Dots.Kind = EDreamTextItemKind::Glyph;
		Dots.Codepoint = CharCodeOfDots;
		Dots.ElementIndex = CharIndex;
		Dots.SourceIndex = CharIndex;
		Dots.LineIndex = LinesCount;
		Dots.Pen = CurrentLineOffset;
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
	}

	void FLayoutRun::Run()
	{
		Out.Reset();
		Prepare();
		Preprocess();

		const int32 ContentLength = TextProcessingArray.Num();

		uint32 PrevCharCode = '\0';//prev char code (not space or tab)
		for (int32 CharIndex = 0; CharIndex < ContentLength; CharIndex++)
		{
			const auto CharElement = TextProcessingArray[CharIndex];
			const auto CharCode = CharElement.Unicode;
			int32 CaretCharIndex = CharIndex;
			if (In.bRichText)
			{
				RichTextParseResult = RichTextPropertyArray[CharIndex];
				CaretCharIndex = RichTextParseResult.CharIndex;
			}

			if (CharCode == '\n' || CharCode == '\r')//10 -- \n, 13 -- \r
			{
				NewLine(In.bRichText ? RichTextParseResult.CharIndex : CharIndex, true, ENewLineMode::LineBreak, 0);
				if (CharIndex + 1 < ContentLength)
				{
					const auto NextCharCode = TextProcessingArray[CharIndex + 1].Unicode;
					if ((CharCode == '\r' && NextCharCode == '\n') || (CharCode == '\n' && NextCharCode == '\r'))
					{
						CharIndex++;//\n\r or \r\n
					}
				}
				continue;
			}

			if (NewLineMode == ENewLineMode::Space || NewLineMode == ENewLineMode::Overflow)
			{
				if (IsSpace(CharCode, RichTextParseResult))//skip empty space at start of newline
				{
					if (NewLineMode == ENewLineMode::Overflow)
					{
						const auto TempCharGeo = GetCharGeo(CharIndex == 0 ? CharCode : PrevCharCode, CharElement
							, RichTextParseResult.Size, RichTextParseResult.Bold, RichTextParseResult);
						CurrentPreferredWidth += TempCharGeo.XAdvance;//newline is caused by space, so the space size should add to preferredWidth, because preferredWidth should ignore auto wrapping
						NewLineMode = ENewLineMode::None;
					}
					continue;
				}
				else
				{
					NewLineMode = ENewLineMode::None;
				}
			}

			const auto CharGeo = GetCharGeo(CharIndex == 0 ? CharCode : PrevCharCode, CharElement
				, RichTextParseResult.Size, RichTextParseResult.Bold, RichTextParseResult);
			//caret property
			CaretPosition.X = CurrentLineOffset.X - HalfFontSpaceX;
			CaretPosition.Y = CurrentLineOffset.Y;
			FDreamUITextCaretProperty CaretProperty;
			CaretProperty.CaretPosition = CaretPosition;
			CaretProperty.CharIndex = CaretCharIndex;
			LineProperty.CaretPropertyList.Add(CaretProperty);

			CaretPosition.X += In.FontSpace.X + CharGeo.XAdvance;//for line's last char's caret position

			if (IsSpace(CharCode, RichTextParseResult))//char is space
			{
				//char is space and text can have overflow line, then we need to calculate if the following words can fit the rest space, if not means new line
				if (In.OverflowType == EDreamUITextOverflowType::VerticalOverflow)
				{
					auto PrevCharCodeOfForwardChar = PrevCharCode;
					float SpaceNeeded = GetCharGeoXAdv(PrevCharCodeOfForwardChar, CharElement, RichTextParseResult);
					PrevCharCodeOfForwardChar = CharCode;
					SpaceNeeded += In.FontSpace.X;
					bool bNeedToRemoveLastFontSpace = false;
					for (int32 ForwardCharIndex = CharIndex + 1, ForwardVisibleCharIndex = CurrentVisibleCharCount; ForwardCharIndex < ContentLength && ForwardVisibleCharIndex < ContentLength; ForwardCharIndex++)
					{
						bNeedToRemoveLastFontSpace = false;
						const auto CharElementOfForwardChar = TextProcessingArray[ForwardCharIndex];
						const auto CharCodeOfForwardChar = CharElementOfForwardChar.Unicode;
						const auto& RichTextParseResultOfForwardChar = In.bRichText ? RichTextPropertyArray[ForwardCharIndex] : RichTextParseResult;
						if (IsSpace(CharCodeOfForwardChar, RichTextParseResultOfForwardChar))//space
						{
							break;
						}
						if (CharCodeOfForwardChar == '\n' || CharCodeOfForwardChar == '\r' || CharCodeOfForwardChar == '\t')//\n\r\t
						{
							break;
						}
						SpaceNeeded += GetCharGeoXAdv(PrevCharCodeOfForwardChar, CharElementOfForwardChar, RichTextParseResultOfForwardChar);
						SpaceNeeded += In.FontSpace.X;
						bNeedToRemoveLastFontSpace = true;
						ForwardVisibleCharIndex++;
						PrevCharCodeOfForwardChar = CharCodeOfForwardChar;
					}
					if (bNeedToRemoveLastFontSpace)
					{
						SpaceNeeded -= In.FontSpace.X;
					}
					if (CurrentLineOffset.X + SpaceNeeded > WrapWidth + UE_KINDA_SMALL_NUMBER)
					{
						NewLine(CaretCharIndex, false, ENewLineMode::Space,
							CharGeo.XAdvance
							+ In.FontSpace.X//this font-space is related to char
							+ In.FontSpace.X//because NewLine function remove font-space (currentLineWidth -= fontSpace.X to remove font-space), so we need add it back
						);
						continue;
					}
				}
			}

			PrevCharCode = CharCode;
			//char geometry
			if (IsRichTextImageSpace(CharCode, RichTextParseResult))
			{
				FDreamUIText_RichTextImageTag ImageTagData;
				ImageTagData.TagName = RichTextParseResult.ImageTag;
				ImageTagData.Position = FVector2D(CurrentLineOffset.X + CharGeo.XAdvance * 0.5f, CurrentLineOffset.Y);
				ImageTagData.Size = FVector2D(CharGeo.Width, CharGeo.Height);
				ImageTagData.TintColor = RichTextParseResult.HasColor ? RichTextParseResult.Color : FColor::White;
				Out.Images.Add(ImageTagData);
				CurrentLineHeight = FMath::Max(CurrentLineHeight, RichTextParseResult.Size);

				FDreamTextGlyphItem Item;
				Item.Kind = EDreamTextItemKind::Image;
				Item.Codepoint = CharCode;
				Item.ElementIndex = CharIndex;
				Item.SourceIndex = CharElement.StringIndex;
				Item.LineIndex = LinesCount;
				Item.Pen = CurrentLineOffset;
				Item.Glyph = CharGeo;
				Item.AdvanceWithSpace = CharGeo.XAdvance + In.FontSpace.X;
				Item.Style = MakeStyle(RichTextParseResult);
				Out.Items.Add(Item);
			}
			else if (CharElement.Type == EDreamUIText_CodeType::Emoji)
			{
				FDreamUIText_Emoji Emoji;
				Emoji.EmojiCode = CharElement.Unicode;
				Emoji.Position = FVector2D(CurrentLineOffset.X + CharGeo.XAdvance * 0.5f, CurrentLineOffset.Y);
				Emoji.Size = FVector2D(CharGeo.Width, CharGeo.Height);
				Out.Emojis.Add(Emoji);
				CurrentLineHeight = FMath::Max(CurrentLineHeight, RichTextParseResult.Size);

				FDreamTextGlyphItem Item;
				Item.Kind = EDreamTextItemKind::Emoji;
				Item.Codepoint = CharCode;
				Item.ElementIndex = CharIndex;
				Item.SourceIndex = CharElement.StringIndex;
				Item.LineIndex = LinesCount;
				Item.Pen = CurrentLineOffset;
				Item.Glyph = CharGeo;
				Item.AdvanceWithSpace = CharGeo.XAdvance + In.FontSpace.X;
				Item.Style = MakeStyle(RichTextParseResult);
				Out.Items.Add(Item);
			}
			else
			{
				FDreamTextGlyphItem Item;
				Item.Codepoint = CharCode;
				Item.ElementIndex = CharIndex;
				Item.SourceIndex = CharElement.StringIndex;
				Item.LineIndex = LinesCount;
				Item.Pen = CurrentLineOffset;
				Item.Glyph = CharGeo;
				Item.AdvanceWithSpace = CharGeo.XAdvance + In.FontSpace.X;
				Item.Style = MakeStyle(RichTextParseResult);
				if (CharCode != ' ' && CharCode != '\t')//skip invisible char
				{
					if (In.bRichText)
					{
						CurrentLineHeight = FMath::Max(CurrentLineHeight, RichTextParseResult.Size);
					}

					Item.Kind = EDreamTextItemKind::Glyph;
					if (!bHasClampContent)
					{
						Item.bEmit = true;
						Item.bCountsAsVisible = true;
						if (Item.Style.bUnderline)
						{
							Item.UnderlineGlyph = GetUnderlineOrStrikethroughCharGeo('_', RichTextParseResult.Size, RichTextParseResult.Bold);
						}
						if (Item.Style.bStrikethrough)
						{
							Item.StrikethroughGlyph = GetUnderlineOrStrikethroughCharGeo('-', RichTextParseResult.Size, RichTextParseResult.Bold);
						}
					}
					CurrentVisibleCharCount++;
				}
				else
				{
					Item.Kind = EDreamTextItemKind::Space;
				}
				Out.Items.Add(Item);
			}

			//collect rich text custom tag. custom tag use start/end mark, so put these code outside of visible-char-check.
			if (In.bRichText)
			{
				switch (RichTextParseResult.CustomTagMode)
				{
				case ECustomTagMode::Start:
				{
					FDreamUIText_RichTextCustomTag CustomTag;
					CustomTag.TagName = RichTextParseResult.CustomTag;
					CustomTag.CharIndexStart = CurrentVisibleCharCount - 1;//-1 as index
					CustomTag.CharIndexStart = FMath::Max(0, CustomTag.CharIndexStart);//incase first char is invisible char, that makes index == -1
					CustomTag.CharIndexEnd = -1;
					Out.CustomTags.Add(CustomTag);
				}
				break;
				case ECustomTagMode::End:
				{
					const FName TagName = RichTextParseResult.CustomTag;
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

			CurrentLineOffset.X += CharGeo.XAdvance + In.FontSpace.X;
			CurrentLineWidth += CharGeo.XAdvance + In.FontSpace.X;

			//overflow
			switch (In.OverflowType)
			{
			case EDreamUITextOverflowType::HorizontalOverflow:
			{
				//no need to do anything
			}
			break;
			case EDreamUITextOverflowType::VerticalOverflow:
			{
				if (CharIndex + 1 == ContentLength)continue;//last char
				float NextCharXAdv = GetCharGeoXAdv(TextProcessingArray[CharIndex].Unicode, TextProcessingArray[CharIndex + 1]
					, In.bRichText ? RichTextPropertyArray[CharIndex + 1] : RichTextParseResult);

				if (CharIndex + 2 < ContentLength//check size
					&& FChar::IsPunct(TextProcessingArray[CharIndex + 2].Unicode)//newline with punctuation
					&& CharIndex + 2 != ContentLength - 1//not last char
					)
				{
					NextCharXAdv += GetCharGeoXAdv(TextProcessingArray[CharIndex + 1].Unicode, TextProcessingArray[CharIndex + 2]
						, In.bRichText ? RichTextPropertyArray[CharIndex + 2] : RichTextParseResult);
					if (CurrentLineOffset.X + NextCharXAdv > WrapWidth + UE_KINDA_SMALL_NUMBER)//if next char cannot fit this line, then add new line
					{
						const auto NextChar = TextProcessingArray[CharIndex + 1].Unicode;
						if (NextChar == '\r' || NextChar == '\n')
						{
							//next char is new line, no need to add new line
						}
						else
						{
							NewLine(CaretCharIndex + 2, false, ENewLineMode::Overflow, 0);
							continue;
						}
					}
				}
				else
				{
					if (CurrentLineOffset.X + NextCharXAdv > WrapWidth + UE_KINDA_SMALL_NUMBER && In.WrappingPolicy == ETextWrappingPolicy::AllowPerCharacterWrapping)//if next char cannot fit this line, then add new line
					{
						const auto NextChar = TextProcessingArray[CharIndex + 1].Unicode;
						if (NextChar == '\r' || NextChar == '\n')
						{
							//next char is new line, no need to add new line
						}
						else
						{
							NewLine(CaretCharIndex + 1, false, ENewLineMode::Overflow, 0);
							continue;
						}
					}
				}
			}
			break;
			case EDreamUITextOverflowType::Truncate:
			case EDreamUITextOverflowType::Ellipsis:
			{
				if (CharIndex + 1 == ContentLength)continue;//last char
				if (bHasClampContent)continue;

				const float NextCharXAdv = GetCharGeoXAdv(TextProcessingArray[CharIndex].Unicode, TextProcessingArray[CharIndex + 1]
					, In.bRichText ? RichTextPropertyArray[CharIndex + 1] : RichTextParseResult);
				if (CurrentLineOffset.X + NextCharXAdv > In.Width)//horizontal cannot fit next char
				{
					bHasClampContent = true;
					Out.bTruncated = true;
					CurrentLineWidth_ForClampContent = CurrentLineWidth;
					bShouldSetParagraphHeightForClampContent = true;//paragraphHeight is set after NewLine, so we mark it and get clamp_ParagraphHeight later
					if (In.OverflowType == EDreamUITextOverflowType::Ellipsis)
					{
						ApplyEllipsis(CharIndex);
					}
				}
			}
			break;
			}
		}

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

		//last line
		NewLine(In.bRichText ? In.Content.Len() : TextProcessingArray.Num(), true, ENewLineMode::Overflow, 0);
		//remove last line's space Y
		ParagraphHeight -= In.FontSpace.Y;
		ParagraphHeight_ForClampContent -= In.FontSpace.Y;
		const float ParagraphHeightWithClamp = bHasClampContent ? ParagraphHeight_ForClampContent : ParagraphHeight;

		Out.PreferredSize.X = FMath::Max(CurrentPreferredWidth, MaxPreferredWidth);
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
		float YOffset = PivotOffsetY - FirstLineHeight * 0.5f;
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
		//caret property
		for (auto& LinePropertyItem : Out.Lines)
		{
			for (auto& CharItem : LinePropertyItem.CaretPropertyList)
			{
				CharItem.CaretPosition.X += XOffset;
				CharItem.CaretPosition.Y += YOffset;
			}
		}
		//image
		if (In.bRichText)
		{
			for (auto& ImageItem : Out.Images)
			{
				ImageItem.Position.X += XOffset;
				ImageItem.Position.Y += YOffset;
			}
		}
		//emoji
		for (auto& EmojiItem : Out.Emojis)
		{
			EmojiItem.Position.X += XOffset;
			EmojiItem.Position.Y += YOffset;
		}
		//items
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
