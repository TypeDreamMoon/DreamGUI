// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Utils/LexUIUtils.h"
#include "LexUITextData.h"

//a set of helpers to parse rich text
namespace LexUIRichTextParser
{
	enum class ESupOrSubMode
	{
		None, Sup, Sub,
	};
	enum class ECustomTagMode
	{
		None, Start, End,
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

		ESupOrSubMode SupOrSubMode = ESupOrSubMode::None;

		ECustomTagMode CustomTagMode = ECustomTagMode::None;
		FName CustomTag;
		FName ImageTag;

		int CharIndex = 0;
	};
	
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
		FName ImageTag = NAME_None;

		int OriginSize = 0;
		FColor OriginColor = FColor::White;
		uint8 OriginRenderOpacity = 255;
		bool OriginBold = false;
		bool OriginItalic = false;

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
		;
	public:
		void ClearImageTag()
		{
			ImageTag = NAME_None;
		}
		void Prepare(float inOriginSize, FColor inOriginColor, uint8 inRenderOpacity, bool inBold, bool inItalic, int32 inFlags, FRichTextParseResult& result)
		{
			OriginSize = inOriginSize;
			OriginColor = inOriginColor;
			OriginRenderOpacity = inRenderOpacity;
			OriginBold = inBold;
			OriginItalic = inItalic;

			result.Bold = inBold;
			result.Italic = inItalic;
			result.Size = inOriginSize;
			result.Color = inOriginColor;

			bEnableBold = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Bold);
			bEnableItalic = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Italic);
			bEnableUnderline = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Underline);
			bEnableStrikethrough = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Strikethrough);
			bEnableSize = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Size);
			bEnableColor = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Color);
			bEnableSuperscript = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Superscript);
			bEnableSubscript = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Subscript);
			bEnableCustomTag = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::CustomTag);
			bEnableImage = inFlags & (1 << (int)EUIText_RichTextTagFilterFlags::Image);
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
			ImageTag = NAME_None;
		}
		bool Parse(const FString& text, const int& textLength, int& startIndex, FRichTextParseResult& parseResult)
		{
			bool haveSymbol = false;
			int charIndex = startIndex;
			if (text[charIndex] == '<')
			{
				if (charIndex + 2 < textLength && text[charIndex + 2] == '>')
				{
					if (text[charIndex + 1] == 'b')//begin bold
					{
						if (bEnableBold)
						{
							startIndex += 3;
							BoldCount++;
							haveSymbol = true;
						}
					}
					else if (text[charIndex + 1] == 'i')//begin italic
					{
						if (bEnableItalic)
						{
							startIndex += 3;
							ItalicCount++;
							haveSymbol = true;
						}
					}
					else if (text[charIndex + 1] == 'u')//begin underline
					{
						if (bEnableUnderline)
						{
							startIndex += 3;
							UnderlineCount++;
							haveSymbol = true;
						}
					}
					else if (text[charIndex + 1] == 's')//begin strikethough
					{
						if (bEnableStrikethrough)
						{
							startIndex += 3;
							StrikethroughCount++;
							haveSymbol = true;
						}
					}
				}
				else if (charIndex + 5 < textLength
					&& text[charIndex + 1] == 's'
					&& text[charIndex + 2] == 'i'
					&& text[charIndex + 3] == 'z'
					&& text[charIndex + 4] == 'e'
					&& text[charIndex + 5] == '='
					)//being size=
				{
					if (bEnableSize)
					{
						int charEndIndex;
						float parsedSize;
						bool absoluteOrAdditional;
						if (GetSize(text, textLength, charIndex + 6, charEndIndex, parsedSize, absoluteOrAdditional))
						{
							startIndex += charEndIndex - charIndex + 1;
							if (absoluteOrAdditional)
							{
								SizeArray.Add(parsedSize);
							}
							else
							{
								SizeArray.Add(OriginSize + parsedSize);
							}
							haveSymbol = true;
						}
					}
				}
				else if (charIndex + 6 < textLength
					&& text[charIndex + 1] == 'c'
					&& text[charIndex + 2] == 'o'
					&& text[charIndex + 3] == 'l'
					&& text[charIndex + 4] == 'o'
					&& text[charIndex + 5] == 'r'
					&& text[charIndex + 6] == '='
					)//begin color=
				{
					if (bEnableColor)
					{
						int charEndIndex;
						FColor parsedColor;
						if (GetColor(text, textLength, charIndex + 7, charEndIndex, parsedColor))
						{
							startIndex += charEndIndex - charIndex + 1;
							ColorArray.Add(parsedColor);
							haveSymbol = true;
						}
					}
				}
				else if (charIndex + 4 < textLength
					&& text[charIndex + 1] == 's'
					&& text[charIndex + 2] == 'u'
					&& text[charIndex + 3] == 'p'
					&& text[charIndex + 4] == '>'
					)//begin sup
				{
					if (bEnableSuperscript)
					{
						startIndex += 5;
						SupOrSubArray.Add(ESupOrSubMode::Sup);
						haveSymbol = true;
					}
				}
				else if (charIndex + 4 < textLength
					&& text[charIndex + 1] == 's'
					&& text[charIndex + 2] == 'u'
					&& text[charIndex + 3] == 'b'
					&& text[charIndex + 4] == '>'
					)//begin sub
				{
					if (bEnableSubscript)
					{
						startIndex += 5;
						SupOrSubArray.Add(ESupOrSubMode::Sub);
						haveSymbol = true;
					}
				}
				else if (charIndex + 6 < textLength
					&& text[charIndex + 1] == 'i'
					&& text[charIndex + 2] == 'm'
					&& text[charIndex + 3] == 'g'
					&& text[charIndex + 4] == '='
					)//begin image=
				{
					if (bEnableImage)
					{
						int charEndIndex;
						if (GetImageTag(text, textLength, charIndex + 5, charEndIndex, ImageTag))
						{
							startIndex += charEndIndex - charIndex + 1;
							haveSymbol = true;
						}
					}
				}
				else if (charIndex + 1 < textLength && text[charIndex + 1] == '/')//end
				{
					if (charIndex + 3 < textLength && text[charIndex + 3] == '>')
					{
						if (text[charIndex + 2] == 'b' && BoldCount > 0)//end bold
						{
							if (bEnableBold)
							{
								startIndex += 4;
								BoldCount--;
								haveSymbol = true;
							}
						}
						else if (text[charIndex + 2] == 'i' && ItalicCount > 0)//end italic
						{
							if (bEnableItalic)
							{
								startIndex += 4;
								ItalicCount--;
								haveSymbol = true;
							}
						}
						else if (text[charIndex + 2] == 'u' && UnderlineCount > 0)//end underline
						{
							if (bEnableUnderline)
							{
								startIndex += 4;
								UnderlineCount--;
								haveSymbol = true;
							}
						}
						else if (text[charIndex + 2] == 's' && StrikethroughCount > 0)//end strikethough
						{
							if (bEnableStrikethrough)
							{
								startIndex += 4;
								StrikethroughCount--;
								haveSymbol = true;
							}
						}
					}
					else if (charIndex + 6 < textLength
						&& text[charIndex + 2] == 's'
						&& text[charIndex + 3] == 'i'
						&& text[charIndex + 4] == 'z'
						&& text[charIndex + 5] == 'e'
						&& text[charIndex + 6] == '>'
						&& SizeArray.Num() > 0
						)//end size
					{
						if (bEnableSize)
						{
							startIndex += 7;
							SizeArray.RemoveAt(SizeArray.Num() - 1);
							haveSymbol = true;
						}
					}
					else if (charIndex + 7 < textLength
						&& text[charIndex + 2] == 'c'
						&& text[charIndex + 3] == 'o'
						&& text[charIndex + 4] == 'l'
						&& text[charIndex + 5] == 'o'
						&& text[charIndex + 6] == 'r'
						&& text[charIndex + 7] == '>'
						&& ColorArray.Num() > 0
						)//end color
					{
						if (bEnableColor)
						{
							startIndex += 8;
							ColorArray.RemoveAt(ColorArray.Num() - 1);
							haveSymbol = true;
						}
					}
					else if (charIndex + 5 < textLength
						&& text[charIndex + 2] == 's'
						&& text[charIndex + 3] == 'u'
						&& text[charIndex + 4] == 'p'
						&& text[charIndex + 5] == '>'
						&& SupOrSubArray.Num() > 0
						)//end sup
					{
						if (bEnableSuperscript)
						{
							startIndex += 6;
							SupOrSubArray.RemoveAt(SupOrSubArray.Num() - 1);
							haveSymbol = true;
						}
					}
					else if (charIndex + 5 < textLength
						&& text[charIndex + 2] == 's'
						&& text[charIndex + 3] == 'u'
						&& text[charIndex + 4] == 'b'
						&& text[charIndex + 5] == '>'
						&& SupOrSubArray.Num() > 0
						)//end sub
					{
						if (bEnableSubscript)
						{
							startIndex += 6;
							SupOrSubArray.RemoveAt(SupOrSubArray.Num() - 1);
							haveSymbol = true;
						}
					}
					else if (CustomTagArray.Num() > 0
						)//end custom tag
					{
						if (bEnableCustomTag)
						{
							int charEndIndex;
							FName tag;
							if (GetCustomTag(text, textLength, charIndex + 2, charEndIndex, tag))
							{
								auto foundIndex = CustomTagArray.IndexOfByKey(tag);
								if (foundIndex != -1)
								{
									CustomTagArray.RemoveAt(foundIndex);
									startIndex += charEndIndex - charIndex + 1;
									parseResult.CustomTag = tag;
									parseResult.CustomTagMode = ECustomTagMode::End;
									haveSymbol = true;
								}
							}
						}
					}
				}
				else if(charIndex + 1 < textLength
					)//check custom tag
				{
					if (bEnableCustomTag)
					{
						int charEndIndex;
						FName tag;
						if (GetCustomTag(text, textLength, charIndex + 1, charEndIndex, tag))
						{
							auto foundIndex = CustomTagArray.IndexOfByKey(tag);
							if (foundIndex == -1)
							{
								startIndex += charEndIndex - charIndex + 1;
								CustomTagArray.Add(tag);
								parseResult.CustomTag = tag;
								parseResult.CustomTagMode = ECustomTagMode::Start;
								haveSymbol = true;
							}
						}
					}
				}
			}
			if (haveSymbol)
			{
				parseResult.Bold = BoldCount > 0 || OriginBold;
				parseResult.Italic = ItalicCount > 0 || OriginItalic;
				parseResult.Underline = UnderlineCount > 0;
				parseResult.Strikethrough = StrikethroughCount > 0;
				parseResult.Size = SizeArray.Num() > 0 ? SizeArray[SizeArray.Num() - 1] : OriginSize;
				parseResult.Size = FMath::Max(parseResult.Size, 0.0f);
				parseResult.HasColor = ColorArray.Num() > 0;
				parseResult.Color = parseResult.HasColor ? ColorArray[ColorArray.Num() - 1] : OriginColor;
				parseResult.SupOrSubMode = SupOrSubArray.Num() > 0 ? SupOrSubArray[SupOrSubArray.Num() - 1] : ESupOrSubMode::None;
				if (parseResult.SupOrSubMode != ESupOrSubMode::None)
				{
					parseResult.Size *= 0.8f;//sup or sub size
				}
				parseResult.ImageTag = ImageTag;
			}
			return haveSymbol;
		}
	private:
		//get size from 'size=' or 'size=+' or 'size=-', end with '>'
		//return true if is valid
		static bool GetSize(const FString& text, int textLength, int startIndex, int& outEndIndex, float& outSize, bool& outAbsoluteOrAdditional)
		{
			int endIndex = -1;
			for (int i = startIndex; i < textLength; i++)
			{
				if (text[i] == '>')
				{
					endIndex = i;
					break;
				}
				else if (text[i] == '<'//reach another tag
					|| text[i] == ' '//reach space
					|| text[i] == '\n'//reach new line
					|| text[i] == '\t'//reach new line
					)
				{
					break;
				}
			}
			if (endIndex != -1 && endIndex > startIndex)//found end
			{
				FString sizeStr = text.Mid(startIndex, endIndex - startIndex);
				outAbsoluteOrAdditional = sizeStr[0] != '+' && sizeStr[0] != '-';
				if (sizeStr.IsNumeric())
				{
					outSize = FCString::Atof(*sizeStr);
					outEndIndex = endIndex;
					return true;
				}
			}
			return false;
		}
		static bool GetCustomTag(const FString& text, int textLength, int startIndex, int& outEndIndex, FName& outTag)
		{
			int endIndex = -1;
			for (int i = startIndex; i < textLength; i++)
			{
				if (text[i] == '>')
				{
					endIndex = i;
					break;
				}
				else if (text[i] == '<'//reach another tag
					|| text[i] == ' '//reach space
					|| text[i] == '\n'//reach new line
					|| text[i] == '\t'//reach new line
					)
				{
					break;
				}
			}
			if (endIndex != -1 && endIndex > startIndex)//found end
			{
				outTag = FName(*text.Mid(startIndex, endIndex - startIndex));
				outEndIndex = endIndex;
				return true;
			}
			return false;
		}
		static bool GetImageTag(const FString& text, int textLength, int startIndex, int& outEndIndex, FName& outTag)
		{
			int endIndex = -1;
			for (int i = startIndex + 1; i < textLength; i++)
			{
				if (text[i] == '>')
				{
					if (text[i - 1] == '/')//image is self-close tag, must end with '/>'
					{
						endIndex = i;
						break;
					}
					else
					{
						break;
					}
				}
				else if (text[i] == '<'//reach another tag
					|| text[i] == ' '//reach space
					|| text[i] == '\n'//reach new line
					|| text[i] == '\t'//reach new line
					)
				{
					break;
				}
			}
			if (endIndex != -1 && endIndex > startIndex)//found end
			{
				outTag = FName(*text.Mid(startIndex, endIndex - startIndex - 1));
				outEndIndex = endIndex;
				return true;
			}
			return false;
		}
		//get color from 'color=red' or 'color=#ffffff', end with '>'
		//return true if is valid
		bool GetColor(const FString& text, int textLength, int startIndex, int& outEndIndex, FColor& outColor)
		{
			int endIndex = -1;
			for (int i = startIndex; i < textLength; i++)
			{
				if (text[i] == '>')
				{
					endIndex = i;
					break;
				}
				else if (text[i] == '<'//reach another tag
					|| text[i] == ' '//reach space
					|| text[i] == '\n'//reach new line
					|| text[i] == '\t'//reach new line
					)
				{
					break;
				}
			}
			if (endIndex != -1 && endIndex > startIndex)//found end
			{
				outEndIndex = endIndex;
				FString colorString = text.Mid(startIndex, endIndex - startIndex);
				colorString.ToLowerInline();
				if (colorString == TEXT("black"))
				{
					outColor = FColor::Black;
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("white"))
				{
					outColor = FColor::White;
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("gray"))
				{
					outColor = FColor(128, 128, 128);
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("silver"))
				{
					outColor = FColor(192, 192, 192);
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("red"))
				{
					outColor = FColor::Red;
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("green"))
				{
					outColor = FColor::Green;
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("blue"))
				{
					outColor = FColor::Blue;
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("orange"))
				{
					outColor = FColor(255, 165, 0);
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("purple"))
				{
					outColor = FColor(128, 0, 128);
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString == TEXT("yellow"))
				{
					outColor = FColor(255, 255, 0);
					outColor.A = OriginRenderOpacity;
					return true;
				}
				else if (colorString[0] == '#')
				{
					const static TArray<TCHAR> hexTable = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
					if (colorString.Len() == 7 || colorString.Len() == 9)//#ffffff/#ffffff00
					{
						outColor.A = OriginRenderOpacity;
						int colorStringLength = colorString.Len();
						for (int i = 1; i < colorStringLength; i += 2)
						{
							int firstIndex = hexTable.IndexOfByKey(colorString[i]);
							int secondIndex = hexTable.IndexOfByKey(colorString[i + 1]);
							if (firstIndex != -1 && secondIndex != -1)//valid
							{
								uint8 value = (uint8)(firstIndex * 16 + secondIndex);
								switch (i)
								{
								case 1:outColor.R = value; break;
								case 3:outColor.G = value; break;
								case 5:outColor.B = value; break;
								case 7:
								{
									outColor.A = (uint8)(FLexUIUtils::Color255To1_Table[OriginRenderOpacity] * value);
								}
								break;
								}
							}
							else
							{
								return false;
							}
						}
						return true;
					}
				}
			}
			return false;
		}
	};
}