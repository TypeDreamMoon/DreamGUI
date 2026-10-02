// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Utils/DreamUIUtils.h"
#include "DreamUITextData.h"

//a set of helpers to parse rich text
namespace DreamUIRichTextParser
{
	enum class ESupOrSubMode
	{
		None, Sup, Sub,
	};
	enum class ECustomTagMode
	{
		None, Start, End,
	};
	/** Where an inline image sits on its line: `<img=Tag,...,baseline/>` and so on. */
	enum class EImageVerticalAlign : uint8
	{
		/** Centred on the line, which is where an image with no alignment has always gone. */
		Middle,
		/** Bottom edge on the baseline, like a letter. */
		Baseline,
		/** Top edge on the top of the line. */
		Top,
		/** Bottom edge on the bottom of the line. */
		Bottom,
	};
	struct FRichTextParseResult
	{
		bool Bold = false;
		bool Italic = false;
		bool Underline = false;
		bool Strikethrough = false;

		float Size = 0;

		FColor Color = FColor::Black;
		bool HasColor = false;
		/**
		 * A custom style's Multiply that found no colour of its own to multiply: the text's colour is a paint input, so the
		 * product is taken when the glyph is painted. MultiplyColor is what to multiply it by.
		 */
		bool bHasMultiplyColor = false;
		FColor MultiplyColor = FColor::White;

		ESupOrSubMode SupOrSubMode = ESupOrSubMode::None;
		/** How far a superscript (up, positive) or subscript (down, negative) moves its baseline, in the units of the font size. */
		float BaselineShift = 0.0f;

		/**
		 * Which open tag last set each property, as the order the tags were opened in (0: none, the text's own style). A
		 * custom style only overrides a property that no tag nested inside it has set, so `<warn><b>x</b></warn>` stays
		 * bold whatever the warn style says about bold.
		 */
		int32 BoldOrder = 0;
		int32 ItalicOrder = 0;
		int32 UnderlineOrder = 0;
		int32 StrikethroughOrder = 0;
		int32 SizeOrder = 0;
		int32 ColorOrder = 0;
		int32 SupOrSubOrder = 0;

		/** What the last Parse found: the start or the end of a custom tag, and its name. */
		ECustomTagMode CustomTagMode = ECustomTagMode::None;
		FName CustomTag;
		/** The custom tag above came from `<a=Id>`: it is a hyperlink, and something may be clicked on it. */
		bool bHyperlink = false;
		/** For a custom tag start, the order it was opened in, which is what its style is ranked by against nested tags. */
		int32 CustomTagOrder = 0;
		FName ImageTag;
		/** Size the `<img=Tag,W,H/>` asked for, in the same units as the font size; 0 means "the font size". */
		float ImageWidth = 0.0f;
		float ImageHeight = 0.0f;
		EImageVerticalAlign ImageVerticalAlign = EImageVerticalAlign::Middle;
		/**
		 * The culture name of the innermost open `<lang=xx>` ("ja", "zh-Hans"): the language the text is in from there,
		 * which decides its fallback faces and what HarfBuzz shapes it with. None outside every one: the text's own
		 * language (FDreamTextLayoutInput::Language).
		 */
		FName Language;

		int CharIndex = 0;
	};

	/**
	 * Superscript and subscript as Chrome's user-agent style draws them: `font-size: smaller`, which is the parent's size
	 * over 1.2, and `vertical-align: super` / `sub`, which Blink resolves to the baseline raised by a third of the PARENT's
	 * font size plus one pixel, or lowered by a fifth of it plus one pixel. InOutResult.Size is the parent's size on entry.
	 */
	inline void ApplySupOrSub(FRichTextParseResult& InOutResult, ESupOrSubMode InMode)
	{
		InOutResult.SupOrSubMode = InMode;
		if (InMode == ESupOrSubMode::None)
		{
			InOutResult.BaselineShift = 0.0f;
			return;
		}
		const float ParentSize = InOutResult.Size;
		InOutResult.Size = ParentSize / 1.2f;
		InOutResult.BaselineShift = InMode == ESupOrSubMode::Sup ? ParentSize / 3.0f + 1.0f : -(ParentSize / 5.0f + 1.0f);
	}

	/** What an open tag does to the font size: sets it, or makes the text a superscript or subscript of it. */
	struct FSizeEffect
	{
		/** The order the tag was opened in: effects fold outermost first. */
		int32 Order = 0;
		/** The tag sets the size to Size, or with bAdditional adds Size to the size around it. */
		bool bSetsSize = false;
		bool bAdditional = false;
		float Size = 0.0f;
		/** Otherwise the tag makes a superscript or subscript, or with None takes the text back to the baseline. */
		ESupOrSubMode SupOrSub = ESupOrSubMode::None;
	};

	/**
	 * The size and baseline shift of text inside every open tag that sets either, folded outermost first, the way CSS
	 * cascades font-size and vertical-align (see ApplySupOrSub for the steps): nested superscripts add up, and a size set
	 * inside a superscript replaces its smaller size while the text stays on the raised baseline. Writes Size,
	 * BaselineShift, SupOrSubMode and the orders of the innermost size and superscript tags. Effects of one tag keep the
	 * order they were added in.
	 */
	inline void FoldSizeEffects(float OriginSize, TArray<FSizeEffect>& Effects, FRichTextParseResult& InOutResult)
	{
		Effects.StableSort([](const FSizeEffect& A, const FSizeEffect& B) { return A.Order < B.Order; });
		float Size = OriginSize;
		float Shift = 0.0f;
		ESupOrSubMode Mode = ESupOrSubMode::None;
		int32 SizeOrder = 0;
		int32 SupOrSubOrder = 0;
		for (const FSizeEffect& Effect : Effects)
		{
			if (Effect.bSetsSize)
			{
				Size = Effect.bAdditional ? Size + Effect.Size : Effect.Size;
				SizeOrder = Effect.Order;
			}
			else if (Effect.SupOrSub == ESupOrSubMode::None)
			{
				Mode = ESupOrSubMode::None;
				Shift = 0.0f;
				SupOrSubOrder = Effect.Order;
			}
			else
			{
				const float ParentSize = Size;
				Size = ParentSize / 1.2f;
				Shift += Effect.SupOrSub == ESupOrSubMode::Sup ? ParentSize / 3.0f + 1.0f : -(ParentSize / 5.0f + 1.0f);
				Mode = Effect.SupOrSub;
				SupOrSubOrder = Effect.Order;
			}
		}
		InOutResult.Size = FMath::Max(Size, 0.0f);
		InOutResult.BaselineShift = Shift;
		InOutResult.SupOrSubMode = Mode;
		InOutResult.SizeOrder = SizeOrder;
		InOutResult.SupOrSubOrder = SupOrSubOrder;
	}

	struct FRichTextParser
	{
	private:
		int						BoldCount = 0;
		int						ItalicCount = 0;
		int						UnderlineCount = 0;
		int						StrikethroughCount = 0;
		TArray<float>			SizeArray;
		TArray<FColor>			ColorArray;
		TArray<ESupOrSubMode>	SupOrSubArray;
		TArray<FName>			CustomTagArray;
		/** Open `<a=Id>` tags, innermost last: `</a>` names no id, so it closes the one most recently opened. */
		TArray<FName>			HyperlinkTags;
		/** Open `<lang=xx>` tags, innermost last: `</lang>` closes the one most recently opened, as `</size>` does. */
		TArray<FName>			LanguageArray;
		/** How many tags have been opened so far: the order a tag is ranked by (see FRichTextParseResult::BoldOrder). */
		int32					OpenOrder = 0;
		/** The open order of every open tag of each kind, innermost last, parallel to the counts and stacks above. */
		TArray<int32>			BoldOrders;
		TArray<int32>			ItalicOrders;
		TArray<int32>			UnderlineOrders;
		TArray<int32>			StrikethroughOrders;
		TArray<int32>			SizeOrders;
		TArray<int32>			ColorOrders;
		TArray<int32>			SupOrSubOrders;
		FName ImageTag = NAME_None;
		float ImageWidth = 0.0f;
		float ImageHeight = 0.0f;
		EImageVerticalAlign ImageVerticalAlign = EImageVerticalAlign::Middle;

		/** Float, because font sizes are: an int here quantised every size a tag computed against it. */
		float OriginSize = 0.0f;
		FColor OriginColor = FColor::White;
		bool OriginBold = false;
		bool OriginItalic = false;
		/** The text's own underline / strike, which `<u>` and `<s>` nest on top of rather than replace. */
		bool OriginUnderline = false;
		bool OriginStrikethrough = false;

		bool
		bEnableBold = false
		, bEnableItalic = false
		, bEnableUnderline = false
		, bEnableStrikethrough = false
		, bEnableSize = false
		, bEnableColor = false
		, bEnableSuperscript = false
		, bEnableSubscript = false
		, bEnableCustomTag = false
		, bEnableImage = false
		, bEnableHyperlink = false
		, bEnableLanguage = false
		;
	public:
		void ClearImageTag()
		{
			ImageTag = NAME_None;
			ImageWidth = ImageHeight = 0.0f;
			ImageVerticalAlign = EImageVerticalAlign::Middle;
		}
		/** The size and superscript/subscript tags still open, for FoldSizeEffects. */
		void GetSizeEffects(TArray<FSizeEffect>& OutEffects) const
		{
			for (int32 i = 0; i < SizeArray.Num(); i++)
			{
				FSizeEffect& Effect = OutEffects.AddDefaulted_GetRef();
				Effect.Order = SizeOrders.IsValidIndex(i) ? SizeOrders[i] : 0;
				Effect.bSetsSize = true;
				Effect.Size = SizeArray[i];
			}
			for (int32 i = 0; i < SupOrSubArray.Num(); i++)
			{
				FSizeEffect& Effect = OutEffects.AddDefaulted_GetRef();
				Effect.Order = SupOrSubOrders.IsValidIndex(i) ? SupOrSubOrders[i] : 0;
				Effect.SupOrSub = SupOrSubArray[i];
			}
		}
		/** The text's own size: what every size tag folds from. */
		float GetOriginSize() const { return OriginSize; }
		/**
		 * Tag colours come out with the alpha the author wrote. Render opacity is applied by the painter
		 * (FDreamTextPaintParams::RichTextTagOpacity) instead of being baked in here, so that fading a
		 * rich text does not invalidate its layout.
		 */
		void Prepare(float inOriginSize, FColor inOriginColor, bool inBold, bool inItalic, bool inUnderline, bool inStrikethrough, int32 inFlags, FRichTextParseResult& result)
		{
			OriginSize = inOriginSize;
			OriginColor = inOriginColor;
			OriginBold = inBold;
			OriginItalic = inItalic;
			OriginUnderline = inUnderline;
			OriginStrikethrough = inStrikethrough;

			result.Bold = inBold;
			result.Italic = inItalic;
			result.Underline = inUnderline;
			result.Strikethrough = inStrikethrough;
			result.Size = inOriginSize;
			result.Color = inOriginColor;
			result.Language = NAME_None;

			bEnableBold = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Bold);
			bEnableItalic = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Italic);
			bEnableUnderline = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Underline);
			bEnableStrikethrough = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Strikethrough);
			bEnableSize = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Size);
			bEnableColor = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Color);
			bEnableSuperscript = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Superscript);
			bEnableSubscript = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Subscript);
			bEnableCustomTag = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::CustomTag);
			bEnableImage = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Image);
			bEnableHyperlink = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Hyperlink);
			bEnableLanguage = inFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Language);
		}
		void Clear()
		{
			BoldCount = 0;
			ItalicCount = 0;
			UnderlineCount = 0;
			StrikethroughCount = 0;
			SizeArray.Reset();
			ColorArray.Reset();
			SupOrSubArray.Reset();
			CustomTagArray.Reset();
			HyperlinkTags.Reset();
			LanguageArray.Reset();
			OpenOrder = 0;
			BoldOrders.Reset();
			ItalicOrders.Reset();
			UnderlineOrders.Reset();
			StrikethroughOrders.Reset();
			SizeOrders.Reset();
			ColorOrders.Reset();
			SupOrSubOrders.Reset();
			ImageTag = NAME_None;
			ImageWidth = ImageHeight = 0.0f;
			ImageVerticalAlign = EImageVerticalAlign::Middle;
		}
		/**
		 * A character reference, which is how a rich text writes a character the markup would otherwise
		 * eat: `&lt; &gt; &amp; &quot; &apos; &nbsp;`, and the numeric forms `&#1234;` / `&#x1F600;`.
		 * Same set a UMG RichTextBlock understands. Without one there was no way at all to show a
		 * literal '<' -- the only "escape" was to misspell the tag so that parsing failed.
		 *
		 * Only rich text unescapes: a plain text draws every character as itself, as UMG's TextBlock
		 * does, so `&amp;` in a plain text is four characters and means them.
		 *
		 * @return true when a reference starts at Index; OutLength is how many code units it spans.
		 */
		static bool ReadEscape(const FString& Text, int TextLength, int Index, uint32& OutCodepoint, int32& OutLength)
		{
			if (Index < 0 || Index >= TextLength || Text[Index] != '&')return false;
			// The longest thing accepted is "&#x10FFFF;", ten units; anything longer is not a reference.
			const int MaxEnd = FMath::Min(TextLength, Index + 12);
			int End = -1;
			for (int i = Index + 1; i < MaxEnd; i++)
			{
				const TCHAR c = Text[i];
				if (c == ';') { End = i; break; }
				if (c == '&' || c == '<' || c == '>' || c == ' ' || c == '\n' || c == '\t')break;
			}
			if (End == -1 || End == Index + 1)return false;
			const TCHAR* Body = Text.GetCharArray().GetData() + Index + 1;
			const int BodyLen = End - Index - 1;
			auto NameIs = [Body, BodyLen](const TCHAR* Lit) -> bool
			{
				const int LitLen = (int)FCString::Strlen(Lit);
				return BodyLen == LitLen && FCString::Strncmp(Body, Lit, LitLen) == 0;
			};
			uint32 Codepoint = 0;
			if (Body[0] == '#')
			{
				if (BodyLen < 2)return false;
				const bool bHex = Body[1] == 'x' || Body[1] == 'X';
				const int DigitStart = bHex ? 2 : 1;
				if (BodyLen <= DigitStart)return false;
				uint32 Value = 0;
				for (int i = DigitStart; i < BodyLen; i++)
				{
					const int Digit = bHex ? HexDigit(Body[i]) : ((Body[i] >= '0' && Body[i] <= '9') ? (int)(Body[i] - '0') : -1);
					if (Digit == -1)return false;
					Value = Value * (bHex ? 16u : 10u) + (uint32)Digit;
					if (Value > 0x10FFFF)return false;
				}
				// U+0000 and the surrogate range are not characters anyone can mean.
				if (Value == 0 || (Value >= 0xD800 && Value <= 0xDFFF))return false;
				Codepoint = Value;
			}
			else if (NameIs(TEXT("lt")))Codepoint = '<';
			else if (NameIs(TEXT("gt")))Codepoint = '>';
			else if (NameIs(TEXT("amp")))Codepoint = '&';
			else if (NameIs(TEXT("quot")))Codepoint = '"';
			else if (NameIs(TEXT("apos")))Codepoint = '\'';
			else if (NameIs(TEXT("nbsp")))Codepoint = 0x00A0;
			else return false;
			OutCodepoint = Codepoint;
			OutLength = End - Index + 1;
			return true;
		}
		/**
		 * Turns plain text into markup that renders as itself -- the inverse of ReadEscape, and what a
		 * caller needs before pushing user-typed text into a rich text.
		 */
		static FString EscapeText(const FString& InPlainText)
		{
			FString Out;
			Out.Reserve(InPlainText.Len());
			for (int32 i = 0; i < InPlainText.Len(); i++)
			{
				const TCHAR c = InPlainText[i];
				switch (c)
				{
				case '&': Out += TEXT("&amp;"); break;
				case '<': Out += TEXT("&lt;"); break;
				case '>': Out += TEXT("&gt;"); break;
				default: Out.AppendChar(c); break;
				}
			}
			return Out;
		}
		bool Parse(const FString& Text, int TextLength, int& InOutStartIndex, FRichTextParseResult& ParseResult)
		{
			bool bHaveSymbol = false;
			int CharIndex = InOutStartIndex;
			if (Text[CharIndex] == '<')
			{
				if (CharIndex + 2 < TextLength && Text[CharIndex + 2] == '>')
				{
					if (Text[CharIndex + 1] == 'b')//begin bold
					{
						if (bEnableBold)
						{
							InOutStartIndex += 3;
							BoldCount++;
							BoldOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
					else if (Text[CharIndex + 1] == 'i')//begin italic
					{
						if (bEnableItalic)
						{
							InOutStartIndex += 3;
							ItalicCount++;
							ItalicOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
					else if (Text[CharIndex + 1] == 'u')//begin underline
					{
						if (bEnableUnderline)
						{
							InOutStartIndex += 3;
							UnderlineCount++;
							UnderlineOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
					else if (Text[CharIndex + 1] == 's')//begin strikethough
					{
						if (bEnableStrikethrough)
						{
							InOutStartIndex += 3;
							StrikethroughCount++;
							StrikethroughOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
				}
				else if (CharIndex + 5 < TextLength
					&& Text[CharIndex + 1] == 's'
					&& Text[CharIndex + 2] == 'i'
					&& Text[CharIndex + 3] == 'z'
					&& Text[CharIndex + 4] == 'e'
					&& Text[CharIndex + 5] == '='
					)//being size=
				{
					if (bEnableSize)
					{
						int charEndIndex;
						float parsedSize;
						bool absoluteOrAdditional;
						if (GetSize(Text, TextLength, CharIndex + 6, charEndIndex, parsedSize, absoluteOrAdditional))
						{
							InOutStartIndex += charEndIndex - CharIndex + 1;
							if (absoluteOrAdditional)
							{
								SizeArray.Add(parsedSize);
							}
							else
							{
								SizeArray.Add(OriginSize + parsedSize);
							}
							SizeOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
				}
				else if (CharIndex + 6 < TextLength
					&& Text[CharIndex + 1] == 'c'
					&& Text[CharIndex + 2] == 'o'
					&& Text[CharIndex + 3] == 'l'
					&& Text[CharIndex + 4] == 'o'
					&& Text[CharIndex + 5] == 'r'
					&& Text[CharIndex + 6] == '='
					)//begin color=
				{
					if (bEnableColor)
					{
						int charEndIndex;
						FColor parsedColor;
						if (GetColor(Text, TextLength, CharIndex + 7, charEndIndex, parsedColor))
						{
							InOutStartIndex += charEndIndex - CharIndex + 1;
							ColorArray.Add(parsedColor);
							ColorOrders.Add(++OpenOrder);
							bHaveSymbol = true;
						}
					}
				}
				else if (CharIndex + 4 < TextLength
					&& Text[CharIndex + 1] == 's'
					&& Text[CharIndex + 2] == 'u'
					&& Text[CharIndex + 3] == 'p'
					&& Text[CharIndex + 4] == '>'
					)//begin sup
				{
					if (bEnableSuperscript)
					{
						InOutStartIndex += 5;
						SupOrSubArray.Add(ESupOrSubMode::Sup);
						SupOrSubOrders.Add(++OpenOrder);
						bHaveSymbol = true;
					}
				}
				else if (CharIndex + 4 < TextLength
					&& Text[CharIndex + 1] == 's'
					&& Text[CharIndex + 2] == 'u'
					&& Text[CharIndex + 3] == 'b'
					&& Text[CharIndex + 4] == '>'
					)//begin sub
				{
					if (bEnableSubscript)
					{
						InOutStartIndex += 5;
						SupOrSubArray.Add(ESupOrSubMode::Sub);
						SupOrSubOrders.Add(++OpenOrder);
						bHaveSymbol = true;
					}
				}
				else if (CharIndex + 6 < TextLength
					&& Text[CharIndex + 1] == 'i'
					&& Text[CharIndex + 2] == 'm'
					&& Text[CharIndex + 3] == 'g'
					&& Text[CharIndex + 4] == '='
					)//begin image=
				{
					if (bEnableImage)
					{
						int charEndIndex;
						if (GetImageTag(Text, TextLength, CharIndex + 5, charEndIndex, ImageTag, ImageWidth, ImageHeight, ImageVerticalAlign))
						{
							InOutStartIndex += charEndIndex - CharIndex + 1;
							bHaveSymbol = true;
						}
					}
				}
				else if (CharIndex + 3 < TextLength
					&& Text[CharIndex + 1] == 'a'
					&& Text[CharIndex + 2] == '='
					)//begin a= (hyperlink)
				{
					if (bEnableHyperlink)
					{
						int charEndIndex;
						FName tag;
						if (GetCustomTag(Text, TextLength, CharIndex + 3, charEndIndex, tag))
						{
							// A hyperlink IS a custom tag -- a named range of characters -- with one
							// thing added: something may be clicked on it. Sharing the machinery is what
							// makes `<a=Id>` work with TextAnimation and the custom style data for free.
							if (CustomTagArray.IndexOfByKey(tag) == -1)
							{
								InOutStartIndex += charEndIndex - CharIndex + 1;
								CustomTagArray.Add(tag);
								HyperlinkTags.Add(tag);
								ParseResult.CustomTag = tag;
								ParseResult.CustomTagMode = ECustomTagMode::Start;
								ParseResult.bHyperlink = true;
								ParseResult.CustomTagOrder = ++OpenOrder;
								bHaveSymbol = true;
							}
						}
					}
				}
				else if (CharIndex + 6 < TextLength
					&& Text[CharIndex + 1] == 'l'
					&& Text[CharIndex + 2] == 'a'
					&& Text[CharIndex + 3] == 'n'
					&& Text[CharIndex + 4] == 'g'
					&& Text[CharIndex + 5] == '='
					)//begin lang=
				{
					if (bEnableLanguage)
					{
						int charEndIndex;
						FName parsedLanguage;
						if (GetLanguage(Text, TextLength, CharIndex + 6, charEndIndex, parsedLanguage))
						{
							InOutStartIndex += charEndIndex - CharIndex + 1;
							LanguageArray.Add(parsedLanguage);
							bHaveSymbol = true;
						}
					}
				}
				else if (CharIndex + 1 < TextLength && Text[CharIndex + 1] == '/')//end
				{
					if (CharIndex + 3 < TextLength && Text[CharIndex + 3] == '>')
					{
						if (Text[CharIndex + 2] == 'b' && BoldCount > 0)//end bold
						{
							if (bEnableBold)
							{
								InOutStartIndex += 4;
								BoldCount--;
								PopOrder(BoldOrders);
								bHaveSymbol = true;
							}
						}
						else if (Text[CharIndex + 2] == 'i' && ItalicCount > 0)//end italic
						{
							if (bEnableItalic)
							{
								InOutStartIndex += 4;
								ItalicCount--;
								PopOrder(ItalicOrders);
								bHaveSymbol = true;
							}
						}
						else if (Text[CharIndex + 2] == 'u' && UnderlineCount > 0)//end underline
						{
							if (bEnableUnderline)
							{
								InOutStartIndex += 4;
								UnderlineCount--;
								PopOrder(UnderlineOrders);
								bHaveSymbol = true;
							}
						}
						else if (Text[CharIndex + 2] == 's' && StrikethroughCount > 0)//end strikethough
						{
							if (bEnableStrikethrough)
							{
								InOutStartIndex += 4;
								StrikethroughCount--;
								PopOrder(StrikethroughOrders);
								bHaveSymbol = true;
							}
						}
						else if (Text[CharIndex + 2] == 'a' && HyperlinkTags.Num() > 0)//end a (hyperlink)
						{
							if (bEnableHyperlink)
							{
								// `</a>` names no id, so it closes the innermost open one -- which is
								// also what lets one link nest inside another.
								const FName tag = HyperlinkTags.Pop();
								CustomTagArray.Remove(tag);
								InOutStartIndex += 4;
								ParseResult.CustomTag = tag;
								ParseResult.CustomTagMode = ECustomTagMode::End;
								ParseResult.bHyperlink = true;
								bHaveSymbol = true;
							}
						}
					}
					else if (CharIndex + 6 < TextLength
						&& Text[CharIndex + 2] == 's'
						&& Text[CharIndex + 3] == 'i'
						&& Text[CharIndex + 4] == 'z'
						&& Text[CharIndex + 5] == 'e'
						&& Text[CharIndex + 6] == '>'
						&& SizeArray.Num() > 0
						)//end size
					{
						if (bEnableSize)
						{
							InOutStartIndex += 7;
							SizeArray.Pop();
							PopOrder(SizeOrders);
							bHaveSymbol = true;
						}
					}
					else if (CharIndex + 7 < TextLength
						&& Text[CharIndex + 2] == 'c'
						&& Text[CharIndex + 3] == 'o'
						&& Text[CharIndex + 4] == 'l'
						&& Text[CharIndex + 5] == 'o'
						&& Text[CharIndex + 6] == 'r'
						&& Text[CharIndex + 7] == '>'
						&& ColorArray.Num() > 0
						)//end color
					{
						if (bEnableColor)
						{
							InOutStartIndex += 8;
							ColorArray.Pop();
							PopOrder(ColorOrders);
							bHaveSymbol = true;
						}
					}
					else if (CharIndex + 5 < TextLength
						&& Text[CharIndex + 2] == 's'
						&& Text[CharIndex + 3] == 'u'
						&& Text[CharIndex + 4] == 'p'
						&& Text[CharIndex + 5] == '>'
						&& SupOrSubArray.Num() > 0
						)//end sup
					{
						if (bEnableSuperscript)
						{
							InOutStartIndex += 6;
							SupOrSubArray.Pop();
							PopOrder(SupOrSubOrders);
							bHaveSymbol = true;
						}
					}
					else if (CharIndex + 5 < TextLength
						&& Text[CharIndex + 2] == 's'
						&& Text[CharIndex + 3] == 'u'
						&& Text[CharIndex + 4] == 'b'
						&& Text[CharIndex + 5] == '>'
						&& SupOrSubArray.Num() > 0
						)//end sub
					{
						if (bEnableSubscript)
						{
							InOutStartIndex += 6;
							SupOrSubArray.Pop();
							PopOrder(SupOrSubOrders);
							bHaveSymbol = true;
						}
					}
					else if (CharIndex + 6 < TextLength
						&& Text[CharIndex + 2] == 'l'
						&& Text[CharIndex + 3] == 'a'
						&& Text[CharIndex + 4] == 'n'
						&& Text[CharIndex + 5] == 'g'
						&& Text[CharIndex + 6] == '>'
						&& LanguageArray.Num() > 0
						)//end lang
					{
						if (bEnableLanguage)
						{
							InOutStartIndex += 7;
							LanguageArray.Pop();
							bHaveSymbol = true;
						}
					}
					else if (CustomTagArray.Num() > 0
						)//end custom tag
					{
						if (bEnableCustomTag)
						{
							int charEndIndex;
							FName tag;
							if (GetCustomTag(Text, TextLength, CharIndex + 2, charEndIndex, tag))
							{
								auto foundIndex = CustomTagArray.IndexOfByKey(tag);
								if (foundIndex != -1)
								{
									CustomTagArray.RemoveAt(foundIndex);
									InOutStartIndex += charEndIndex - CharIndex + 1;
									ParseResult.CustomTag = tag;
									ParseResult.CustomTagMode = ECustomTagMode::End;
									bHaveSymbol = true;
								}
							}
						}
					}
				}
				else if(CharIndex + 1 < TextLength
					)//check custom tag
				{
					if (bEnableCustomTag)
					{
						int charEndIndex;
						FName tag;
						if (GetCustomTag(Text, TextLength, CharIndex + 1, charEndIndex, tag))
						{
							auto foundIndex = CustomTagArray.IndexOfByKey(tag);
							if (foundIndex == -1)
							{
								InOutStartIndex += charEndIndex - CharIndex + 1;
								CustomTagArray.Add(tag);
								ParseResult.CustomTag = tag;
								ParseResult.CustomTagMode = ECustomTagMode::Start;
								ParseResult.CustomTagOrder = ++OpenOrder;
								bHaveSymbol = true;
							}
						}
					}
				}
			}
			if (bHaveSymbol)
			{
				ParseResult.Bold = BoldCount > 0 || OriginBold;
				ParseResult.Italic = ItalicCount > 0 || OriginItalic;
				ParseResult.Underline = UnderlineCount > 0 || OriginUnderline;
				ParseResult.Strikethrough = StrikethroughCount > 0 || OriginStrikethrough;
				ParseResult.HasColor = ColorArray.Num() > 0;
				ParseResult.Color = ParseResult.HasColor ? ColorArray[ColorArray.Num() - 1] : OriginColor;
				if (SizeArray.Num() == 0 && SupOrSubArray.Num() == 0)
				{
					ParseResult.Size = FMath::Max(OriginSize, 0.0f);
					ParseResult.BaselineShift = 0.0f;
					ParseResult.SupOrSubMode = ESupOrSubMode::None;
					ParseResult.SizeOrder = 0;
					ParseResult.SupOrSubOrder = 0;
				}
				else
				{
					// <size> and <sup>/<sub> fold in the order they were opened: `<sup><size=40>` is 40 on a raised
					// baseline, `<size=40><sup>` is 40/1.2 raised by a third of 40.
					TArray<FSizeEffect> SizeEffects;
					GetSizeEffects(SizeEffects);
					FoldSizeEffects(OriginSize, SizeEffects, ParseResult);
				}
				ParseResult.BoldOrder = TopOrder(BoldOrders);
				ParseResult.ItalicOrder = TopOrder(ItalicOrders);
				ParseResult.UnderlineOrder = TopOrder(UnderlineOrders);
				ParseResult.StrikethroughOrder = TopOrder(StrikethroughOrders);
				ParseResult.ColorOrder = TopOrder(ColorOrders);
				ParseResult.ImageTag = ImageTag;
				ParseResult.ImageWidth = ImageWidth;
				ParseResult.ImageHeight = ImageHeight;
				ParseResult.ImageVerticalAlign = ImageVerticalAlign;
				ParseResult.Language = LanguageArray.Num() > 0 ? LanguageArray.Last() : FName(NAME_None);
			}
			return bHaveSymbol;
		}
	private:
		/** The open order of the innermost open tag of a kind, or 0 when none is open. */
		static int32 TopOrder(const TArray<int32>& InOrders)
		{
			return InOrders.Num() > 0 ? InOrders.Last() : 0;
		}
		static void PopOrder(TArray<int32>& InOutOrders)
		{
			if (InOutOrders.Num() > 0)
			{
				InOutOrders.Pop();
			}
		}
		//scan from StartIndex for the first tag-value terminator ('>', '<', space, '\n' or '\t').
		//returns its index, or -1 if none found before TextLength.
		static int FindTokenEnd(const FString& Text, int TextLength, int StartIndex)
		{
			for (int i = StartIndex; i < TextLength; i++)
			{
				const TCHAR c = Text[i];
				if (c == '>' || c == '<' || c == ' ' || c == '\n' || c == '\t')
				{
					return i;
				}
			}
			return -1;
		}
		//parse a float (optional leading +/-, digits, at most one '.') straight from a TCHAR range.
		//matches FString::IsNumeric semantics so behaviour stays identical to the old Mid+IsNumeric+Atof path.
		static bool ParseFloat(const TCHAR* Str, int Len, float& OutValue)
		{
			if (Len <= 0)
			{
				return false;
			}
			int i = 0;
			bool bNegative = false;
			if (Str[0] == '+')
			{
				i = 1;
			}
			else if (Str[0] == '-')
			{
				i = 1;
				bNegative = true;
			}
			//note: a lone sign (e.g. "+"/"-") is treated as 0, matching FString::IsNumeric + FCString::Atof
			bool bHasDot = false;
			double IntegerPart = 0.0;
			double FractionPart = 0.0;
			double FractionScale = 0.1;
			for (; i < Len; i++)
			{
				const TCHAR c = Str[i];
				if (c == '.')
				{
					if (bHasDot)
					{
						return false;
					}
					bHasDot = true;
				}
				else if (c >= '0' && c <= '9')
				{
					const int32 Digit = c - '0';
					if (bHasDot)
					{
						FractionPart += Digit * FractionScale;
						FractionScale *= 0.1;
					}
					else
					{
						IntegerPart = IntegerPart * 10.0 + Digit;
					}
				}
				else
				{
					return false;
				}
			}
			const double Result = IntegerPart + FractionPart;
			OutValue = bNegative ? -(float)Result : (float)Result;
			return true;
		}
		//get size from 'size=' or 'size=+' or 'size=-', end with '>'
		//return true if is valid
		static bool GetSize(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, float& OutSize, bool& OutAbsoluteOrAdditional)
		{
			int EndIndex = FindTokenEnd(Text, TextLength, StartIndex);
			if (EndIndex != -1 && EndIndex > StartIndex && Text[EndIndex] == '>')//found end
			{
				const TCHAR* TokenPtr = Text.GetCharArray().GetData() + StartIndex;
				const int TokenLen = EndIndex - StartIndex;
				OutAbsoluteOrAdditional = TokenPtr[0] != '+' && TokenPtr[0] != '-';
				if (ParseFloat(TokenPtr, TokenLen, OutSize))
				{
					OutEndIndex = EndIndex;
					return true;
				}
			}
			return false;
		}
		static bool GetCustomTag(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, FName& OutTag)
		{
			int EndIndex = FindTokenEnd(Text, TextLength, StartIndex);
			if (EndIndex != -1 && EndIndex > StartIndex && Text[EndIndex] == '>')//found end
			{
				OutTag = FName(FStringView(Text.GetCharArray().GetData() + StartIndex, EndIndex - StartIndex));
				OutEndIndex = EndIndex;
				return true;
			}
			return false;
		}
		/**
		 * The culture name of a `<lang=xx>`, ending at '>': letters, digits, '-' and '_' ("ja", "zh-Hans", "pt_BR"), as a
		 * culture name is spelled. Anything else is no language, and the whole tag is then literal text.
		 */
		static bool GetLanguage(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, FName& OutLanguage)
		{
			const int EndIndex = FindTokenEnd(Text, TextLength, StartIndex);
			if (EndIndex == -1 || EndIndex <= StartIndex || Text[EndIndex] != '>')
			{
				return false;
			}
			for (int i = StartIndex; i < EndIndex; i++)
			{
				const TCHAR c = Text[i];
				const bool bNameChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
				if (!bNameChar)
				{
					return false;
				}
			}
			OutLanguage = FName(FStringView(Text.GetCharArray().GetData() + StartIndex, EndIndex - StartIndex));
			OutEndIndex = EndIndex;
			return true;
		}
		/** One of the alignment words an `<img>` tag may end with, case-insensitively. */
		static bool ParseImageAlign(const TCHAR* Str, int Len, EImageVerticalAlign& OutAlign)
		{
			struct FAlignName { const TCHAR* Name; EImageVerticalAlign Align; };
			static const FAlignName Names[] =
			{
				{ TEXT("middle"), EImageVerticalAlign::Middle },
				{ TEXT("baseline"), EImageVerticalAlign::Baseline },
				{ TEXT("top"), EImageVerticalAlign::Top },
				{ TEXT("bottom"), EImageVerticalAlign::Bottom },
			};
			for (const FAlignName& Name : Names)
			{
				const int NameLen = (int)FCString::Strlen(Name.Name);
				if (Len == NameLen && FCString::Strnicmp(Str, Name.Name, NameLen) == 0)
				{
					OutAlign = Name.Align;
					return true;
				}
			}
			return false;
		}
		/**
		 * `<img=Tag/>`, `<img=Tag,Size/>` or `<img=Tag,Width,Height/>`, each optionally followed by where the
		 * image sits on its line: `<img=Tag,baseline/>`, `<img=Tag,48,top/>`, `<img=Tag,30,60,bottom/>`.
		 * One size sets the height and lets the width follow the image's aspect ratio, which is what the
		 * default -- the font size -- does; two set both. Sizes are in the same units as the font size. The
		 * alignment is middle (centred on the line, the default), baseline (bottom edge on the baseline, like
		 * a letter), top or bottom (on that edge of the line); a tall image grows its line to fit in every
		 * case. A malformed size or an unknown word is not an argument, and the whole tag is then literal
		 * text rather than a silently mis-sized or mis-placed image.
		 */
		static bool GetImageTag(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, FName& OutTag, float& OutWidth, float& OutHeight, EImageVerticalAlign& OutAlign)
		{
			OutWidth = OutHeight = 0.0f;
			OutAlign = EImageVerticalAlign::Middle;
			//image is a self-closing tag, must end with '/>'; scan from the char after the first tag char
			int EndIndex = FindTokenEnd(Text, TextLength, StartIndex + 1);
			if (EndIndex == -1 || EndIndex <= StartIndex || Text[EndIndex] != '>' || Text[EndIndex - 1] != '/')//no valid end
			{
				return false;
			}
			const TCHAR* TokenPtr = Text.GetCharArray().GetData() + StartIndex;
			const int TokenLen = EndIndex - StartIndex - 1;//without the '/'
			int NameLen = TokenLen;
			for (int i = 0; i < TokenLen; i++)
			{
				if (TokenPtr[i] == ',') { NameLen = i; break; }
			}
			if (NameLen <= 0)return false;
			float Sizes[2] = { 0.0f, 0.0f };
			int SizeCount = 0;
			bool bHaveAlign = false;
			EImageVerticalAlign Align = EImageVerticalAlign::Middle;
			int Cursor = NameLen;
			while (Cursor < TokenLen)
			{
				if (TokenPtr[Cursor] != ',')return false;
				const int ArgumentStart = ++Cursor;
				while (Cursor < TokenLen && TokenPtr[Cursor] != ',')Cursor++;
				if (Cursor <= ArgumentStart)return false;//an empty argument: a trailing or doubled comma
				if (bHaveAlign)return false;//nothing may follow the alignment
				if (ParseImageAlign(TokenPtr + ArgumentStart, Cursor - ArgumentStart, Align))
				{
					bHaveAlign = true;
					continue;
				}
				if (SizeCount >= 2)return false;//a third size
				if (!ParseFloat(TokenPtr + ArgumentStart, Cursor - ArgumentStart, Sizes[SizeCount]))return false;
				if (Sizes[SizeCount] < 0.0f)return false;
				SizeCount++;
			}
			OutTag = FName(FStringView(TokenPtr, NameLen));
			OutAlign = Align;
			if (SizeCount == 1)
			{
				OutHeight = Sizes[0];
			}
			else if (SizeCount == 2)
			{
				OutWidth = Sizes[0];
				OutHeight = Sizes[1];
			}
			OutEndIndex = EndIndex;
			return true;
		}
		/** One entry of the colour-name table: the CSS name and what it means. */
		struct FNamedColor { const TCHAR* Name; uint8 R, G, B; };
		/**
		 * The colour names a tag may use. The HTML/CSS basic sixteen plus the handful this plugin
		 * already shipped, so nothing that used to parse stops parsing: `green` keeps the (0,255,0) it
		 * has always meant here -- CSS calls that `lime`, and `lime` is an alias for it rather than a
		 * second, conflicting definition.
		 */
		static const FNamedColor* GetNamedColorTable(int& OutCount)
		{
			static const FNamedColor Table[] =
			{
				{ TEXT("black"),       0,   0,   0 },
				{ TEXT("white"),     255, 255, 255 },
				{ TEXT("gray"),      128, 128, 128 },
				{ TEXT("grey"),      128, 128, 128 },
				{ TEXT("silver"),    192, 192, 192 },
				{ TEXT("red"),       255,   0,   0 },
				{ TEXT("green"),       0, 255,   0 },
				{ TEXT("lime"),        0, 255,   0 },
				{ TEXT("blue"),        0,   0, 255 },
				{ TEXT("orange"),    255, 165,   0 },
				{ TEXT("purple"),    128,   0, 128 },
				{ TEXT("yellow"),    255, 255,   0 },
				{ TEXT("cyan"),        0, 255, 255 },
				{ TEXT("aqua"),        0, 255, 255 },
				{ TEXT("magenta"),   255,   0, 255 },
				{ TEXT("fuchsia"),   255,   0, 255 },
				{ TEXT("maroon"),    128,   0,   0 },
				{ TEXT("navy"),        0,   0, 128 },
				{ TEXT("olive"),     128, 128,   0 },
				{ TEXT("teal"),        0, 128, 128 },
				{ TEXT("pink"),      255, 192, 203 },
				{ TEXT("brown"),     165,  42,  42 },
				{ TEXT("gold"),      255, 215,   0 },
			};
			OutCount = (int)UE_ARRAY_COUNT(Table);
			return Table;
		}

		static int HexDigit(TCHAR c)
		{
			if (c >= '0' && c <= '9')return c - '0';
			if (c >= 'a' && c <= 'f')return c - 'a' + 10;
			if (c >= 'A' && c <= 'F')return c - 'A' + 10;
			return -1;
		}

		/**
		 * `rgb(r,g,b)` / `rgba(r,g,b,a)`, ending at ')'. Channels are 0-255; the alpha is 0-255 too
		 * unless it is written with a decimal point, in which case it is CSS's 0..1. Whitespace around
		 * the commas is allowed -- which is why this scans for the ')' itself instead of going through
		 * FindTokenEnd, whose token stops at the first space.
		 * @return false when it is not an rgb() at all, or is malformed (the tag is then literal text).
		 */
		static bool ParseRgbFunction(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, FColor& OutColor)
		{
			const TCHAR* Ptr = Text.GetCharArray().GetData() + StartIndex;
			const int Remaining = TextLength - StartIndex;
			int Consumed = 0;
			bool bHasAlpha = false;
			if (Remaining >= 5 && FCString::Strnicmp(Ptr, TEXT("rgba("), 5) == 0)
			{
				Consumed = 5;
				bHasAlpha = true;
			}
			else if (Remaining >= 4 && FCString::Strnicmp(Ptr, TEXT("rgb("), 4) == 0)
			{
				Consumed = 4;
			}
			else
			{
				return false;
			}
			// The closing bracket, and then the tag's own end immediately after it.
			int Close = -1;
			for (int i = StartIndex + Consumed; i < TextLength; i++)
			{
				const TCHAR c = Text[i];
				if (c == ')') { Close = i; break; }
				if (c == '<' || c == '>' || c == '\n')break;
			}
			if (Close == -1 || Close + 1 >= TextLength || Text[Close + 1] != '>')
			{
				return false;
			}

			const int ExpectedCount = bHasAlpha ? 4 : 3;
			float Channels[4] = { 0.0f, 0.0f, 0.0f, 255.0f };
			bool bFraction[4] = { false, false, false, false };
			int Count = 0;
			int Cursor = StartIndex + Consumed;
			while (Count < ExpectedCount)
			{
				while (Cursor < Close && (Text[Cursor] == ' ' || Text[Cursor] == '\t'))Cursor++;
				const int NumberStart = Cursor;
				while (Cursor < Close && Text[Cursor] != ',')Cursor++;
				int NumberEnd = Cursor;
				while (NumberEnd > NumberStart && (Text[NumberEnd - 1] == ' ' || Text[NumberEnd - 1] == '\t'))NumberEnd--;
				if (NumberEnd <= NumberStart)return false;
				for (int i = NumberStart; i < NumberEnd; i++)
				{
					if (Text[i] == '.')bFraction[Count] = true;
				}
				if (!ParseFloat(Text.GetCharArray().GetData() + NumberStart, NumberEnd - NumberStart, Channels[Count]))
				{
					return false;
				}
				Count++;
				if (Cursor < Close && Text[Cursor] == ',')
				{
					Cursor++;
					continue;
				}
				break;
			}
			if (Count != ExpectedCount)return false;
			// A trailing comma, or a channel too many, is malformed rather than quietly ignored.
			while (Cursor < Close && (Text[Cursor] == ' ' || Text[Cursor] == '\t'))Cursor++;
			if (Cursor != Close)return false;

			auto ToByte = [](float Value) -> uint8
			{
				return (uint8)FMath::Clamp(FMath::RoundToInt(Value), 0, 255);
			};
			OutColor.R = ToByte(Channels[0]);
			OutColor.G = ToByte(Channels[1]);
			OutColor.B = ToByte(Channels[2]);
			OutColor.A = bHasAlpha
				? (bFraction[3] ? ToByte(Channels[3] * 255.0f) : ToByte(Channels[3]))
				: (uint8)255;
			OutEndIndex = Close + 1;
			return true;
		}

		/**
		 * Colour of a `<color=...>` tag, ending at '>'. Accepts:
		 *   a name from GetNamedColorTable, or `transparent`
		 *   `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`
		 *   `rgb(r,g,b)`, `rgba(r,g,b,a)`
		 * Anything else is not a colour, and the whole tag is then rendered as literal text -- write
		 * `&lt;` for a literal '<' if that is what you meant (see FRichTextParser::ReadEscape).
		 * The alpha is the author's; the hierarchy's fade is applied by the painter.
		 */
		bool GetColor(const FString& Text, int TextLength, int StartIndex, int& OutEndIndex, FColor& OutColor)
		{
			if (ParseRgbFunction(Text, TextLength, StartIndex, OutEndIndex, OutColor))
			{
				return true;
			}
			int EndIndex = FindTokenEnd(Text, TextLength, StartIndex);
			if (EndIndex == -1 || EndIndex <= StartIndex || Text[EndIndex] != '>')//no valid end
			{
				return false;
			}
			const TCHAR* TokenPtr = Text.GetCharArray().GetData() + StartIndex;
			const int TokenLen = EndIndex - StartIndex;

			if (TokenPtr[0] == '#')
			{
				// #rgb / #rgba double each digit, as CSS does; #rrggbb / #rrggbbaa take them as written.
				const int DigitCount = TokenLen - 1;
				if (DigitCount != 3 && DigitCount != 4 && DigitCount != 6 && DigitCount != 8)
				{
					return false;
				}
				const bool bShort = DigitCount <= 4;
				const int PerChannel = bShort ? 1 : 2;
				uint8 Channels[4] = { 0, 0, 0, 255 };
				for (int Channel = 0; Channel * PerChannel < DigitCount; Channel++)
				{
					const int First = HexDigit(TokenPtr[1 + Channel * PerChannel]);
					if (First == -1)return false;
					if (bShort)
					{
						Channels[Channel] = (uint8)(First * 16 + First);
					}
					else
					{
						const int Second = HexDigit(TokenPtr[2 + Channel * PerChannel]);
						if (Second == -1)return false;
						Channels[Channel] = (uint8)(First * 16 + Second);
					}
				}
				OutColor = FColor(Channels[0], Channels[1], Channels[2], Channels[3]);
				OutEndIndex = EndIndex;
				return true;
			}

			auto EqualsIC = [&TokenPtr, &TokenLen](const TCHAR* Lit) -> bool
			{
				const int LitLen = (int)FCString::Strlen(Lit);
				return TokenLen == LitLen && FCString::Strnicmp(TokenPtr, Lit, LitLen) == 0;
			};
			if (EqualsIC(TEXT("transparent")))
			{
				OutColor = FColor(0, 0, 0, 0);
				OutEndIndex = EndIndex;
				return true;
			}
			int NamedCount = 0;
			const FNamedColor* Named = GetNamedColorTable(NamedCount);
			for (int i = 0; i < NamedCount; i++)
			{
				if (EqualsIC(Named[i].Name))
				{
					OutColor = FColor(Named[i].R, Named[i].G, Named[i].B, 255);
					OutEndIndex = EndIndex;
					return true;
				}
			}
			return false;
		}
	};
}