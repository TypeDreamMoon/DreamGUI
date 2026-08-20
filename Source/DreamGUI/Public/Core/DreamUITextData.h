// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"
#include "DreamUITextData.generated.h"

class FDreamUIGeometry;
class UDreamText;
class UDreamUIFontData_BaseObject;
class UDreamUIRichTextImageData;


UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextParagraphHorizontalAlign : uint8
{
	Left,
	Center,
	Right,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextParagraphVerticalAlign : uint8
{
	Top,
	Middle,
	Bottom,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextFontStyle :uint8
{
	None,
	Bold,
	Italic,
	BoldAndItalic,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUITextOverflowType :uint8
{
	/** chars will go out of rect range horizontally */
	HorizontalOverflow = 0,
	/** chars will go out of rect range vertically */
	VerticalOverflow = 1,
	/** remove chars on right if out of range */
	Truncate = 2,
	/** replace chars with ... if out of range */
	Ellipsis = 3,
};

/** single char property */
struct FDreamUITextCaretProperty
{
	/** caret position. caret is on left side of char */
	FVector2f CaretPosition = FVector2f::ZeroVector;
	/** char index in text, -1 means line end caret */
	int32 CharIndex = 0;
};
/** a line of text property */
struct FDreamUITextLineProperty
{
	TArray<FDreamUITextCaretProperty> CaretPropertyList;
};
/** for range selection in TextInputComponent */
struct FDreamUITextSelectionProperty
{
	FVector2f Pos = FVector2f::ZeroVector;
	int32 Size = 0;
};
/** char property */
USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUITextCharProperty
{
	GENERATED_BODY()
	/** char index in string */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndex = 0;
	/** vertex index in UIGeometry::vertices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 StartVertIndex = 0;
	/** vertex count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 VertCount = 0;
	/** triangle index in UIGeometry::triangles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 StartTriangleIndex = 0;
	/** triangle indices count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 IndicesCount = 0;

	/** center position of the char, in UIText's local space */
	//FVector2D CenterPosition;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_RichTextCustomTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FName TagName;
	/** start char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndexStart = 0;
	/** end char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 CharIndexEnd = 0;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_RichTextImageTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FName TagName;
	/** image object position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Size = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FColor TintColor = FColor::White;
};

USTRUCT(BlueprintType, Category = DreamGUI)
struct FDreamUIText_Emoji
{
	GENERATED_BODY()
	/** Emoji char */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) int32 EmojiCode = 0;
	/** image object position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI) FVector2D Size = FVector2D::ZeroVector;
};

UENUM(BlueprintType, meta = (Bitflags), Category = DreamGUI)
enum class EDreamUIText_RichTextTagFilterFlags : uint8
{
	Bold, Italic, Underline, Strikethrough, Size, Color, Superscript, Subscript, CustomTag, Image
};
ENUM_CLASS_FLAGS(EDreamUIText_RichTextTagFilterFlags);

enum class EDreamUIText_CodeType
{
	Text = 0,
	Emoji = 1,
};
struct FDreamUIText_TextProcessingElement
{
	uint32 Unicode;
	int StringIndex;
	int Length;
	EDreamUIText_CodeType Type;
};

/// <summary>
/// Commonly referenced Unicode characters in the text generation process.
/// </summary>
namespace FDreamUIText_CodePoint
{
	// constexpr uint32 SPACE = 0x20;
	// constexpr uint32 DOUBLE_QUOTE = 0x22;
	// constexpr uint32 NUMBER_SIGN = 0x23;
	// constexpr uint32 PERCENTAGE = 0x25;
	// constexpr uint32 PLUS = 0x2B;
	// constexpr uint32 MINUS = 0x2D;
	// constexpr uint32 PERIOD = 0x2E;
	//
	// constexpr uint32 HYPHEN_MINUS = 0x2D;
	// constexpr uint32 SOFT_HYPHEN = 0xAD;
	// constexpr uint32 HYPHEN = 0x2010;
	// constexpr uint32 NON_BREAKING_HYPHEN = 0x2011;
	// constexpr uint32 ZERO_WIDTH_SPACE = 0x200B;
	// constexpr uint32 RIGHT_SINGLE_QUOTATION = 0x2019;
	// constexpr uint32 APOSTROPHE = 0x27;
	// constexpr uint32 WORD_JOINER = 0x2060;
	constexpr uint32 HIGH_SURROGATE_START = 0xD800;
	constexpr uint32 HIGH_SURROGATE_END = 0xDBFF;
	constexpr uint32 LOW_SURROGATE_START = 0xDC00;
	constexpr uint32 LOW_SURROGATE_END = 0xDFFF;
	constexpr uint32 UNICODE_PLANE01_START = 0x10000;
	constexpr uint32 UNICODE_VS_BLACK = 0xFE0E;
	constexpr uint32 UNICODE_VS_COLOR = 0xFE0F;

	inline uint32 ConvertToUTF32(uint32 highSurrogate, uint32 lowSurrogate)
	{
		return (((highSurrogate - FDreamUIText_CodePoint::HIGH_SURROGATE_START) << 10) | (lowSurrogate - FDreamUIText_CodePoint::LOW_SURROGATE_START)) + FDreamUIText_CodePoint::UNICODE_PLANE01_START;
	}
	inline bool IsEmoji(uint32 Codepoint)
	{
		// Emoji Block 1
		if (Codepoint >= 0x1F300 && Codepoint <= 0x1F5FF) return true;

		// Emoticons
		if (Codepoint >= 0x1F600 && Codepoint <= 0x1F64F) return true;

		// Transport & Map Symbols
		if (Codepoint >= 0x1F680 && Codepoint <= 0x1F6FF) return true;

		// Supplemental Symbols and Pictographs
		if (Codepoint >= 0x1F900 && Codepoint <= 0x1F9FF) return true;

		// Symbols and Pictographs Extended-A
		if (Codepoint >= 0x1FA70 && Codepoint <= 0x1FAFF) return true;

		// Flags (Regional Indicator Symbols)
		if (Codepoint >= 0x1F1E6 && Codepoint <= 0x1F1FF) return true;

		return false;
	}
	inline FDreamUIText_TextProcessingElement ReadCodePoint(const FString& InString, int InStringLen, int& InOutCharIndex)
	{
		auto charCode = InString[InOutCharIndex];
		if (charCode >= FDreamUIText_CodePoint::HIGH_SURROGATE_START && charCode <= FDreamUIText_CodePoint::HIGH_SURROGATE_END
				&& InOutCharIndex + 1 < InStringLen
				&& InString[InOutCharIndex + 1] >= FDreamUIText_CodePoint::LOW_SURROGATE_START && InString[InOutCharIndex + 1] <= FDreamUIText_CodePoint::LOW_SURROGATE_END)
		{
			FDreamUIText_TextProcessingElement Element;
			Element.Unicode = FDreamUIText_CodePoint::ConvertToUTF32(charCode, InString[InOutCharIndex + 1]);
			Element.StringIndex = InOutCharIndex;
			Element.Type = IsEmoji(Element.Unicode) ? EDreamUIText_CodeType::Emoji : EDreamUIText_CodeType::Text;
			if (InOutCharIndex + 2 < InStringLen
				&& (InString[InOutCharIndex + 2] == FDreamUIText_CodePoint::UNICODE_VS_BLACK || InString[InOutCharIndex + 2] == FDreamUIText_CodePoint::UNICODE_VS_COLOR))
			{
				Element.Length = 3;
				InOutCharIndex+=2;
			}
			else
			{
				Element.Length = 2;
				InOutCharIndex+=1;
			}
			return Element;
		}
		else
		{
			return FDreamUIText_TextProcessingElement{charCode, InOutCharIndex, 1, EDreamUIText_CodeType::Text};
		}
	}
};

struct DREAMGUI_API FDreamUITextGeometryCache
{
public:
	FDreamUITextGeometryCache() {}
	FDreamUITextGeometryCache(UDreamText* InUIText);
	/**
	 * @return true - anything change
	 */
	bool SetInputParameters(
		const FString& InContent,
		float InWidth,
		float InHeight,
		FVector2f InPivot,
		FColor InColor,
		float InRenderOpacityForRichText,
		FVector2f InFontSpace,
		float InFontSize,
		EDreamUITextParagraphHorizontalAlign InParagraphHAlign,
		EDreamUITextParagraphVerticalAlign InParagraphVAlign,
		EDreamUITextOverflowType InOverflowType,
		ETextWrappingPolicy InWrappingPolicy,
		bool InUseKerning,
		EDreamUITextFontStyle InFontStyle,
		bool InRichText,
		int32 InRichTextFilterFlags,
		UDreamUIFontData_BaseObject* InFont
	);
private:
#pragma region InputParameters
	TArray<FDreamUIText_TextProcessingElement> TextProcessingArray;
	FString Content = TEXT("");
	float Width = 0;
	float Height = 0;
	FVector2f Pivot = FVector2f::ZeroVector;
	FColor Color = FColor::White;
	float RenderOpacityForRichText = 1.0f;
	FVector2f FontSpace = FVector2f::ZeroVector;
	float FontSize = 0;
	EDreamUITextParagraphHorizontalAlign ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	EDreamUITextParagraphVerticalAlign ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
	EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	bool bUseKerning = false;
	EDreamUITextFontStyle FontStyle = EDreamUITextFontStyle::None;
	bool bRichText = false;
	int32 RichTextFilterFlags = 0xffffffff;
	TWeakObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;
#pragma endregion InputParameters

	bool bIsDirty = true;//vertex or triangle data is dirty
	bool bIsColorDirty = true;//only color data is dirty (no include rich text's color)
	TWeakObjectPtr<UDreamText> TextComp = nullptr;

public:
#pragma region OutputResults
	/** indicating whether the text is Truncated or using Ellipsis */
	bool textTruncated = false;
	FVector2f textPreferredSize = FVector2f::ZeroVector;
	/** line properties, from first line to last one in array */
	TArray<FDreamUITextLineProperty> cacheLinePropertyArray;
	/** char properties, from first char to last one in array */
	TArray<FDreamUITextCharProperty> cacheCharPropertyArray;
	TArray<FDreamUIText_RichTextCustomTag> cacheRichTextCustomTagArray;
	TArray<FDreamUIText_RichTextImageTag> cacheRichTextImageTagArray;
	TArray<FDreamUIText_Emoji> cacheEmojiArray;
#pragma endregion OutputResults
	void MarkDirty();
	/** check if dirty before calculate geometry */
	void ConditionalCalculateGeometry();
};
