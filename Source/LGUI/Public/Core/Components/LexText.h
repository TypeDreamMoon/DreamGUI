// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "LexVisualBatchMesh.h"
#include "Core/ILexUICultureChangedInterface.h"
#include "Core/LexUITextData.h"
#include "LexText.generated.h"


class ULexUIFontData_BaseObject;
class ULexUIRichTextImageData_BaseObject;
class ULexUIRichTextCustomStyleData;

/**
 * UV channels-
 *		UV0: FontTexture coordinate
 *		UV1: Default LexCanvas use, check LexCanvas
 *		UV2: X- font-size * object-scale, Y- not used
 */
UCLASS(ClassGroup = (LGUI), Blueprintable)
class LGUI_API ULexText : public ULexVisualBatchMesh, public ILexUICultureChangedInterface
{
	GENERATED_BODY()

public:	
	ULexText(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void BeginDestroy() override;
	virtual void OnTransformChanged(bool InPositionChanged, bool InScaleChanged)override;
public:
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
protected:
#endif
	void RegisterOnRichTextImageDataChange();
	void UnregisterOnRichTextImageDataChange();
	FDelegateHandle RichTextImageDataChangedDelegateHandle;
	void RegisterOnRichTextCustomStyleDataChange();
	void UnregisterOnRichTextCustomStyleDataChange();
	FDelegateHandle RichTextCustomStyleDataChangedDelegateHandle;
#if WITH_EDITORONLY_DATA
	/** current using font. the default font when creating new UIText */
	static TWeakObjectPtr<ULexUIFontData_BaseObject> CurrentUsingFontData;
#endif
public:
	static FName GetPropertyName_Text()
	{
		return GET_MEMBER_NAME_CHECKED(ULexText, Text);
	}
	static FName GetPropertyName_OverrideMaterial()
	{
		return GET_MEMBER_NAME_CHECKED(ULexText, OverrideMaterial);
	}

protected:
	friend class FLexTextCustomization;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUIFontData_BaseObject> Font;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (MultiLine="true"))
		FText Text = FText::FromString(TEXT("New Text"));
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "2", ClampMax = "500"))
		float FontSize = 16;
	/** use font kerning for better text layout. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bUseKerning = true;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FVector2D FontSpace = FVector2D(0, 0);
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUITextParagraphHorizontalAlign HAlign = ELexUITextParagraphHorizontalAlign::Center;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUITextParagraphVerticalAlign VAlign = ELexUITextParagraphVerticalAlign::Middle;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUITextOverflowType OverflowType = ELexUITextOverflowType::VerticalOverflow;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	/**
	 * Padding between the widget's rect and the text laid out inside it, the way UMG's text Margin
	 * works. The text is wrapped, aligned and overflow-tested against the rect MINUS this, so a
	 * margin narrows the wrap width rather than just shifting the result.
	 *
	 * It also grows what this text reports as its preferred size, so a parent that sizes itself to
	 * its content leaves room for the padding instead of squeezing it back out.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Margin = FMargin(0.0f);
	/**
	 * Scales the distance from one line to the next, 1 being the font's own line height -- UMG's
	 * LineHeightPercentage. Only the gap between lines moves; the glyphs keep their size, so this
	 * tightens or opens up a paragraph without changing how big the letters are.
	 *
	 * FontSpace.Y is added on top and is a flat distance, which is the difference between the two:
	 * one scales with the font, the other does not.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0", UIMin = "0.5", UIMax = "3.0"))
	float LineHeightPercentage = 1.0f;
	/**
	 * Wrap the text at this width instead of at the content box's, UMG's WrapTextAt. Zero or less
	 * keeps the content box, which is the usual case.
	 *
	 * Worth having separately because the box is also what the text is ALIGNED in: a narrower wrap
	 * width gives a narrow column of text that still centres over the full widget, which the box
	 * alone cannot express. It does not truncate -- Truncate and Ellipsis still measure against the
	 * box, since what they are about is what fits on screen.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0"))
	float WrapTextAt = 0.0f;
	/**
	 * Shrink the font until the text fits the content box, uGUI's Best Fit. FontSize becomes the
	 * size the text is allowed to reach rather than the size it is drawn at, and BestFitMinSize is
	 * how small it may go before the text is simply allowed to overflow.
	 *
	 * Neither UMG nor Slate has an equivalent: their text is content-sized, so the box grows to the
	 * text instead. Here the box is authored, which is exactly the case that needs this.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", Getter = "GetBestFit", Setter = "SetBestFit", meta = (AllowPrivateAccess = true))
	bool bBestFit = false;
	UPROPERTY(EditAnywhere, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition = "bBestFit", ClampMin = "1.0"))
	float BestFitMinSize = 8.0f;
	/** What Best Fit settled on last layout. Not authored, so not a UPROPERTY the user can edit. */
	mutable float RenderedFontSize = 0.0f;
	/** Use a custom material to render this text */
    UPROPERTY(EditAnywhere, Category = "LexUI")
    UMaterialInterface* OverrideMaterial = nullptr;
	/**
	 * Expand character's rect area to generate bigger mesh, useful for effects of OverrideMaterial.
	 * Only valid for SDF font.
	 */
	UPROPERTY(EditAnywhere, Category = "LexUI")
	float ExpandMeshSize = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	ELexUITextFontStyle FontStyle = ELexUITextFontStyle::None;
	/**
	 * rich text support, eg:
	 * <b>Bold</b>
	 * <i>Italic</i>
	 * <u>Underline</u>
	 * <s>Strikethrough</s>
	 * <size=48>Point size 48</size>
	 * <size=+18>Point size increased by 18</size>
	 * <size=-18>Point size decreased by 18</size>
	 * <color=yellow>Yellow text</color> support color name: black, blue, green, orange, purple, red, white, and yellow
	 * <color=#00ff00>Green text</color>
	 * <sup>Superscript</sup>
	 * <sub>Subscript</sub>
	 * <MyTag>Custom tag</MyTag> use any string as custom tag. custom tag can use for char selection (check TextAnimation usage), and for custom style (check RichTextCustomStyleData)
	 * <img=smile/> display a image with key "smile" which defined in RichTextImageData property, can be used for emoji. @todo: image size option
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bRichText = false;
	/** Flags to enable/disable rich text tag. */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (Bitmask, BitmaskEnum = "/Script/LGUI.ELexUIText_RichTextTagFilterFlags", EditCondition = "bRichText"))
		int32 RichTextTagFilterFlags = 0xffffffff;
	/** rich text custom style data for custom tag and rendering custom style */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<ULexUIRichTextCustomStyleData> RichTextCustomStyleData = nullptr;
	/** rich text image data for rendering image inside UIText */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<ULexUIRichTextImageData_BaseObject> RichTextImageData = nullptr;
	/**
	 * The amount of pixels per unit to use for dynamically created bitmap texture, such as BitmapFont. 
	 * But!!! Do not set this value too large if you already have large font size of LexText, because that will result in extremely large texture! 
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay)
	float DynamicPixelsPerUnit = 1.0f;
	/** created object for rich text image */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> CreatedRichTextImageObjectArray;
	/** created object for emoji */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> CreatedEmojiObjectArray;
private:
	bool bHasAddToFont = false;

	mutable FLexUITextGeometryCache CacheTextGeometryData;
	void UpdateCacheTextGeometry()const;
	void ConditionalUpdateCacheTextGeometry()const;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<FLexUITextCharProperty>& GetCharPropertyArray()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		int32 GetVisibleCharCount()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<FLexUIText_RichTextCustomTag>& GetRichTextCustomTagArray()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<FLexUIText_RichTextImageTag>& GetRichTextImageTagArray()const;
public:
	virtual void MarkAllDirty()override;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual bool GetShouldAffectByPixelSnapping()const override;
	virtual void OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual uint8 GetFontMark_WidgetPropertyDataForMaterial() override;
	virtual void OnCultureChanged_Implementation()override;

	void CheckRequireNormalAndTangent();
public:
	void ApplyFontTextureChange();
	void ApplyFontMaterialChange();
	void ApplyRecreateText();
	void ApplyFontEmojiChange();

	virtual void MarkVerticesDirty(bool InTriangleDirty, bool InVertexPositionDirty, bool InVertexUVDirty, bool InVertexColorDirty)override;
	virtual void MarkTextureDirty()override;

	FORCEINLINE static bool IsVisibleChar(uint32 Codepoint)
	{
		if (Codepoint < 0x20) return false;    // C0
		if (Codepoint == 0x7F) return false;   // DEL

		// zero width
		if (Codepoint == 0x200B || Codepoint == 0x200C || Codepoint == 0x200D)
			return false;

		// 
		if (Codepoint == '\n' || Codepoint == '\r' || Codepoint == '\t' || Codepoint == ' ')
			return false;

		return true;
	}
	/** count visible char count of the string */
	static int VisibleCharCountInString(const FString& srcStr);

	void GenerateRichTextImageObject();
	void GenerateEmojiObject();

	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUIFontData_BaseObject* GetFont()const { return Font; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	const FText& GetText()const { return Text; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetFontSize()const { return FontSize; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool GetUseKerning()const { return bUseKerning; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") FVector2D GetFontSpace()const { return FontSpace; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextOverflowType GetOverflowType()const { return OverflowType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") const FMargin& GetMargin()const { return Margin; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetLineHeightPercentage()const { return LineHeightPercentage; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetWrapTextAt()const { return WrapTextAt; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool GetBestFit()const { return bBestFit; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetBestFitMinSize()const { return BestFitMinSize; }
	/** The size Best Fit actually drew at, which is GetFontSize() when Best Fit is off. */
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetRenderedFontSize()const { return RenderedFontSize; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ETextWrappingPolicy GetWrappingPolicy()const{return WrappingPolicy;}
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextFontStyle GetFontStyle()const { return FontStyle; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool GetRichText()const { return bRichText; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") int32 GetRichTextTagFilterFlags()const { return RichTextTagFilterFlags; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUIRichTextCustomStyleData* GetRichTextCustomStyleData()const { return RichTextCustomStyleData; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUIRichTextImageData_BaseObject* GetRichTextImageData()const { return RichTextImageData; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextParagraphHorizontalAlign GetParagraphHorizontalAlignment()const { return HAlign; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextParagraphVerticalAlign GetParagraphVerticalAlignment()const { return VAlign; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetExpandMeshSize()const{return ExpandMeshSize;}
	UFUNCTION(BlueprintCallable, Category = "LGUI") float GetDynamicPixelsPerUnit()const { return DynamicPixelsPerUnit; }

	/** indicating whether the text is Truncated or using Ellipsis */
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool IsTextTruncated()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetFont(ULexUIFontData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI", Meta = (AutoCreateRefTerm = "Value"))
		void SetText(const FText& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetFontSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetUseKerning(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetFontSpace(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetParagraphHorizontalAlignment(ELexUITextParagraphHorizontalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetParagraphVerticalAlignment(ELexUITextParagraphVerticalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetOverflowType(ELexUITextOverflowType Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWrappingPolicy(ETextWrappingPolicy Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetMargin(const FMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetLineHeightPercentage(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWrapTextAt(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetBestFit(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetBestFitMinSize(float Value);
	/**
	 * The largest whole size in [InMinSize, InMaxSize] whose measurement fits InBox, or InMinSize
	 * when even that does not. InMeasure returns the text size a given font size produces.
	 * Bisects, so it costs about eight measurements rather than one per size.
	 */
	static float FindBestFitFontSize(const FVector2f& InBox, float InMinSize, float InMaxSize,
		TFunctionRef<FVector2f(float)> InMeasure);
	/**
	 * The rect the text is actually laid out in: the widget's own, inset by Margin. Never negative
	 * on either axis -- a margin wider than the widget leaves no room rather than an inside-out box.
	 * Static so the arithmetic can be tested without a widget.
	 */
	static void GetContentBox(const FVector2f& InWidgetSize, const FVector2f& InPivot, const FMargin& InMargin,
		FVector2f& OutSize, FVector2f& OutPivot);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetFontStyle(ELexUITextFontStyle Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichText(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextTagFilterFlags(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextImageData(ULexUIRichTextImageData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextCustomStyleData(ULexUIRichTextCustomStyleData* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
    	void SetOverrideMaterial(UMaterialInterface* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetExpandMeshSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetDynamicPixelsPerUnit(float Value);
private:
	void ClearCreatedRichTextImageObject();
	void ClearEmojiObject();
	void RegisterFont();
	void UnregisterFont();
	FDelegateHandle EmojiDataChangedDelegateHandle;
protected:
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
public:
#pragma region UITextInputComponent
	/**
	 * .
	 * @param moveType 0-left, 1-right, 2-up, 3-down, 4-start, 5-end
	 * @return true- data changed
	 */
	bool MoveCaret(int32 moveType, int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, FVector2f& inOutCaretPosition);
	int GetCharIndexByCaretIndex(int32 inCaretPositionIndex);
	int GetLastCaret();
	/** get caret position and line index */
	void FindCaretByIndex(int32& inOutCaretPositionIndex, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outVisibleCaretStartIndex);
	/** find current caret position */
	void FindCaret(FVector2f& inOutCaretPosition, int32 inCaretPositionLineIndex, int32& outCaretPositionIndex);
	/** find caret index by position */
	void FindCaretByWorldPosition(FVector inWorldPosition, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outCaretPositionIndex);
	int GetCaretIndexByCharIndex(int32 inCharIndex);
	bool GetVisibleCharRangeForMultiLine(int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, int32& inOutVisibleCaretStartLineIndex, int32& inOutVisibleCaretStartIndex, int inMaxLineCount, int32& outVisibleCharStartIndex, int32& outVisibleCharCount);

	/** range selection */
	void GetSelectionProperty(int32 InSelectionStartCaretIndex, int32 InSelectionEndCaretIndex, TArray<FLexUITextSelectionProperty>& OutSelectionProeprtyArray);
	const FLexUITextGeometryCache& GetCacheTextGeometryData()const { UpdateCacheTextGeometry(); return CacheTextGeometryData; }
#pragma endregion UITextInputComponent
};
