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

struct FDreamTextLayoutInput;
struct FDreamTextDisplayList;
struct FDreamTextPaintParams;

/**
 * The text component's layout cache: the last layout input, the display list it produced, and the
 * char properties the last paint wrote. Layout and paint are separate steps. A layout is re-run only
 * when its input changes (or the font says its glyphs did); a paint runs every time the geometry is
 * rebuilt, from the cached display list, which is cheap.
 */
struct DREAMGUI_API FDreamUITextGeometryCache
{
public:
	FDreamUITextGeometryCache();
	~FDreamUITextGeometryCache();
	FDreamUITextGeometryCache(const FDreamUITextGeometryCache&) = delete;
	FDreamUITextGeometryCache& operator=(const FDreamUITextGeometryCache&) = delete;

	/** Replace the layout input. Returns true when the layout is now stale. */
	bool SetLayoutInput(const FDreamTextLayoutInput& InInput);
	const FDreamTextLayoutInput& GetLayoutInput() const;
	/** Force the next EnsureLayout to run, for when the glyphs changed underneath an unchanged input. */
	void MarkDirty();
	bool IsLayoutDirty() const { return bIsDirty; }
	/** Lays out if stale. Measures only -- no geometry is touched. Returns true if a layout ran. */
	bool EnsureLayout();
	/** Paints the display list into the geometry, laying out first if stale. */
	void Paint(FDreamUIGeometry& Geometry, const FDreamTextPaintParams& Params);

	const FDreamTextDisplayList& GetDisplayList() const;
	/** Emitted glyphs in order, as written by the last Paint. */
	const TArray<FDreamUITextCharProperty>& GetCharPropertyArray() const { return CharPropertyArray; }
	bool IsTextTruncated() const;
	FVector2f GetPreferredSize() const;
	const TArray<FDreamUITextLineProperty>& GetLines() const;
	const TArray<FDreamUIText_RichTextCustomTag>& GetCustomTags() const;
	const TArray<FDreamUIText_RichTextImageTag>& GetImageTags() const;
	const TArray<FDreamUIText_Emoji>& GetEmojis() const;

	/**
	 * Best Fit memo. The search probes several sizes, each a layout; remembering the answer for the
	 * input it was found against (with FontSize at the ceiling) makes the common call -- nothing
	 * changed -- free. MarkDirty forgets it, since a glyph change can move the answer.
	 */
	bool TryGetBestFit(const FDreamTextLayoutInput& InCeilingInput, float& OutSize) const;
	void SetBestFit(const FDreamTextLayoutInput& InCeilingInput, float InSize);
private:
	TUniquePtr<FDreamTextLayoutInput> Input;
	TUniquePtr<FDreamTextDisplayList> DisplayList;
	TArray<FDreamUITextCharProperty> CharPropertyArray;
	TUniquePtr<FDreamTextLayoutInput> BestFitKey;
	float BestFitSize = 0.0f;
	bool bIsDirty = true;
};
