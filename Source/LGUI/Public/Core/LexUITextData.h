// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"
#include "LexUITextData.generated.h"

class FLexUIGeometry;
class ULexText;
class ULexUIFontData_BaseObject;
class ULexUIRichTextImageData;


UENUM(BlueprintType, Category = LGUI)
enum class ELexUITextParagraphHorizontalAlign : uint8
{
	Left,
	Center,
	Right,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexUITextParagraphVerticalAlign : uint8
{
	Top,
	Middle,
	Bottom,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexUITextFontStyle :uint8
{
	None,
	Bold,
	Italic,
	BoldAndItalic,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexUITextOverflowType :uint8
{
	/** chars will go out of rect range horizontally */
	HorizontalOverflow = 0,
	/** chars will go out of rect range vertically */
	VerticalOverflow = 1,
	/** if with less than maxHorizontalWidth then use HorizontalOverlow, if grater than maxHorizontalWidth then use VerticalOverflow */
	HorizontalAndVerticalOverflow = 3,
	/** remove chars on right if out of range */
	ClampContent = 2,
};

/** single char property */
struct FLexUITextCaretProperty
{
	/** caret position. caret is on left side of char */
	FVector2f CaretPosition = FVector2f::ZeroVector;
	/** char index in text, -1 means line end caret */
	int32 CharIndex = 0;
};
/** a line of text property */
struct FLexUITextLineProperty
{
	TArray<FLexUITextCaretProperty> CaretPropertyList;
};
/** for range selection in TextInputComponent */
struct FLexUITextSelectionProperty
{
	FVector2f Pos = FVector2f::ZeroVector;
	int32 Size = 0;
};
/** char property */
USTRUCT(BlueprintType, Category = LGUI)
struct FLexUITextCharProperty
{
	GENERATED_BODY()
	/** char index in string */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 CharIndex = 0;
	/** vertex index in UIGeometry::vertices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 StartVertIndex = 0;
	/** vertex count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 VertCount = 0;
	/** triangle index in UIGeometry::triangles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 StartTriangleIndex = 0;
	/** triangle indices count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 IndicesCount = 0;

	/** center position of the char, in UIText's local space */
	//FVector2D CenterPosition;
};

USTRUCT(BlueprintType, Category = LGUI)
struct FLexUIText_RichTextCustomTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) FName TagName;
	/** start char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 CharIndexStart = 0;
	/** end char index in cacheCharPropertyArray */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) int32 CharIndexEnd = 0;
};

USTRUCT(BlueprintType, Category = LGUI)
struct FLexUIText_RichTextImageTag
{
	GENERATED_BODY()
	/** Tag name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) FName TagName;
	/** image object position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) FVector2D Size = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI) FColor TintColor = FColor::White;
};

UENUM(BlueprintType, meta = (Bitflags), Category = LGUI)
enum class ELexUIText_RichTextTagFilterFlags : uint8
{
	Bold, Italic, Underline, Strikethrough, Size, Color, Superscript, Subscript, CustomTag, Image
};
ENUM_CLASS_FLAGS(ELexUIText_RichTextTagFilterFlags);

struct LGUI_API FLexUITextGeometryCache
{
public:
	FLexUITextGeometryCache() {}
	FLexUITextGeometryCache(ULexText* InUIText);
	/**
	 * @return true - anything change
	 */
	bool SetInputParameters(
		const FString& InContent,
		int32 InVisibleCharCount,
		float InWidth,
		float InHeight,
		FVector2f InPivot,
		FColor InColor,
		float InCanvasGroupAlpha,
		FVector2f InFontSpace,
		float InFontSize,
		ELexUITextParagraphHorizontalAlign InParagraphHAlign,
		ELexUITextParagraphVerticalAlign InParagraphVAlign,
		ELexUITextOverflowType InOverflowType,
		ETextWrappingPolicy InWrappingPolicy,
		float InMaxHorizontalWidth,
		bool InUseKerning,
		ELexUITextFontStyle InFontStyle,
		bool InRichText,
		int32 InRichTextFilterFlags,
		ULexUIFontData_BaseObject* InFont
	);
private:
#pragma region InputParameters
	FString content = TEXT("");
	int32 visibleCharCount = -1;
	float width = 0;
	float height = 0;
	FVector2f pivot = FVector2f::ZeroVector;
	FColor color = FColor::White;
	float canvasGroupAlpha = 1.0f;
	FVector2f fontSpace = FVector2f::ZeroVector;
	float fontSize = 0;
	ELexUITextParagraphHorizontalAlign paragraphHAlign = ELexUITextParagraphHorizontalAlign::Left;
	ELexUITextParagraphVerticalAlign paragraphVAlign = ELexUITextParagraphVerticalAlign::Bottom;
	ELexUITextOverflowType overflowType = ELexUITextOverflowType::HorizontalOverflow;
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	float maxHorizontalWidth = 100;
	bool useKerning = false;
	ELexUITextFontStyle fontStyle = ELexUITextFontStyle::None;
	bool richText = false;
	int32 richTextFilterFlags = 0xffffffff;
	TWeakObjectPtr<ULexUIFontData_BaseObject> font = nullptr;
#pragma endregion InputParameters

	bool bIsDirty = true;//vertex or triangle data is dirty
	bool bIsColorDirty = true;//only color data is dirty (no include rich text's color)
	TWeakObjectPtr<ULexText> UIText = nullptr;

public:
#pragma region OutputResults
	FVector2f textRealSize = FVector2f::ZeroVector;
	/** line properties, from first line to last one in array */
	TArray<FLexUITextLineProperty> cacheLinePropertyArray;
	/** char properties, from first char to last one in array */
	TArray<FLexUITextCharProperty> cacheCharPropertyArray;
	TArray<FLexUIText_RichTextCustomTag> cacheRichTextCustomTagArray;
	TArray<FLexUIText_RichTextImageTag> cacheRichTextImageTagArray;
#pragma endregion OutputResults
public:
	void MarkDirty();
	/** check if dirty before calculate geometry */
	void ConditionalCalculateGeometry();
};
