// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUITextData.h"
#include "DreamText.generated.h"


class UDreamUIFontData_BaseObject;
class UDreamUIRichTextImageData_BaseObject;
class UDreamUIRichTextCustomStyleData;
struct FDreamTextLayoutInput;
struct FDreamTextPaintParams;

/**
 * UV channels-
 *		UV0: FontTexture coordinate
 *		UV1: Default DreamCanvas use, check DreamCanvas
 *		UV2: X- font-size * object-scale, Y- not used
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable)
class DREAMGUI_API UDreamText : public UDreamVisualBatchMesh, public IDreamUICultureChangedInterface
{
	GENERATED_BODY()

public:	
	UDreamText(const FObjectInitializer& ObjectInitializer);

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
	static TWeakObjectPtr<UDreamUIFontData_BaseObject> CurrentUsingFontData;
#endif
public:
	static FName GetPropertyName_Text()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamText, Text);
	}
	static FName GetPropertyName_OverrideMaterial()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamText, OverrideMaterial);
	}

protected:
	friend class FDreamTextCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UDreamUIFontData_BaseObject> Font;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (MultiLine="true"))
		FText Text = FText::FromString(TEXT("New Text"));
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "2", ClampMax = "500"))
		float FontSize = 16;
	/** use font kerning for better text layout. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseKerning = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector2D FontSpace = FVector2D(0, 0);
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextParagraphHorizontalAlign HAlign = EDreamUITextParagraphHorizontalAlign::Center;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextParagraphVerticalAlign VAlign = EDreamUITextParagraphVerticalAlign::Middle;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	/**
	 * Keep CJK words together when wrapping, using ICU's dictionary -- CSS's `word-break: auto-phrase`.
	 * Only matters with VerticalOverflow. Needs the packaged ICU data to include the CJK dictionary
	 * (Project Settings > Packaging > Internationalization Support: CJK, EFIGSCJK or All); without it
	 * this quietly falls back to per-character breaks.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextPhraseWrap PhraseWrap = EDreamTextPhraseWrap::Off;
	/**
	 * Outline, underlay, glow and fill look, drawn by the built-in shader. Needs a distance-field font
	 * (OutlineMultiChannel source) and "Use Built-in UI Shader" on; has no effect with a custom material.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FDreamTextStyle TextStyle;
	/** Fill progress of the whole text (per line), 0..1, for lyric-style reveals. Segments override it. */
	UPROPERTY(Transient)
	float FillProgress = 1.0f;
	/** Glow boost of the whole text; segments override it. */
	UPROPERTY(Transient)
	float GlowBoost = 0.0f;
	/** Character runs with their own fill progress; see FDreamTextFillSegment. */
	UPROPERTY(Transient)
	TArray<FDreamTextFillSegment> FillSegments;
	/**
	 * Padding between the widget's rect and the text laid out inside it, the way UMG's text Margin
	 * works. The text is wrapped, aligned and overflow-tested against the rect MINUS this, so a
	 * margin narrows the wrap width rather than just shifting the result.
	 *
	 * It also grows what this text reports as its preferred size, so a parent that sizes itself to
	 * its content leaves room for the padding instead of squeezing it back out.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Margin = FMargin(0.0f);
	/**
	 * Scales the distance from one line to the next, 1 being the font's own line height -- UMG's
	 * LineHeightPercentage. Only the gap between lines moves; the glyphs keep their size, so this
	 * tightens or opens up a paragraph without changing how big the letters are.
	 *
	 * FontSpace.Y is added on top and is a flat distance, which is the difference between the two:
	 * one scales with the font, the other does not.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0", UIMin = "0.5", UIMax = "3.0"))
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
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0"))
	float WrapTextAt = 0.0f;
	/**
	 * Shrink the font until the text fits the content box, uGUI's Best Fit. FontSize becomes the
	 * size the text is allowed to reach rather than the size it is drawn at, and BestFitMinSize is
	 * how small it may go before the text is simply allowed to overflow.
	 *
	 * Neither UMG nor Slate has an equivalent: their text is content-sized, so the box grows to the
	 * text instead. Here the box is authored, which is exactly the case that needs this.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetBestFit", Setter = "SetBestFit", meta = (AllowPrivateAccess = true))
	bool bBestFit = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition = "bBestFit", ClampMin = "1.0"))
	float BestFitMinSize = 8.0f;
	/** What Best Fit settled on last layout. Not authored, so not a UPROPERTY the user can edit. */
	mutable float RenderedFontSize = 0.0f;
	/** Use a custom material to render this text */
    UPROPERTY(EditAnywhere, Category = "DreamUI")
    UMaterialInterface* OverrideMaterial = nullptr;
	/**
	 * Expand character's rect area to generate bigger mesh, useful for effects of OverrideMaterial.
	 * Only valid for SDF font.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	float ExpandMeshSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamUITextFontStyle FontStyle = EDreamUITextFontStyle::None;
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
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bRichText = false;
	/** Flags to enable/disable rich text tag. */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIText_RichTextTagFilterFlags", EditCondition = "bRichText"))
		int32 RichTextTagFilterFlags = 0xffffffff;
	/** rich text custom style data for custom tag and rendering custom style */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<UDreamUIRichTextCustomStyleData> RichTextCustomStyleData = nullptr;
	/** rich text image data for rendering image inside UIText */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<UDreamUIRichTextImageData_BaseObject> RichTextImageData = nullptr;
	/**
	 * The amount of pixels per unit to use for dynamically created bitmap texture, such as BitmapFont. 
	 * But!!! Do not set this value too large if you already have large font size of DreamText, because that will result in extremely large texture! 
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay)
	float DynamicPixelsPerUnit = 1.0f;
	/** created object for rich text image */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<UDreamWidget>> CreatedRichTextImageObjectArray;
	/** created object for emoji */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<UDreamWidget>> CreatedEmojiObjectArray;
private:
	bool bHasAddToFont = false;

	mutable FDreamUITextGeometryCache CacheTextGeometryData;
	void UpdateCacheTextGeometry()const;
	void ConditionalUpdateCacheTextGeometry()const;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUITextCharProperty>& GetCharPropertyArray()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int32 GetVisibleCharCount()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUIText_RichTextCustomTag>& GetRichTextCustomTagArray()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUIText_RichTextImageTag>& GetRichTextImageTagArray()const;
public:
	virtual void MarkAllDirty()override;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual bool GetShouldAffectByPixelSnapping()const override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual uint8 GetFontMark_WidgetPropertyDataForMaterial() override;
	virtual void FillWidgetPropertyDataForMaterial_Extra(class UDreamUIDataAsTexture* DataAsTexture) override;
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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIFontData_BaseObject* GetFont()const { return Font; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	const FText& GetText()const { return Text; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetFontSize()const { return FontSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetUseKerning()const { return bUseKerning; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") FVector2D GetFontSpace()const { return FontSpace; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextOverflowType GetOverflowType()const { return OverflowType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FMargin& GetMargin()const { return Margin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetLineHeightPercentage()const { return LineHeightPercentage; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetWrapTextAt()const { return WrapTextAt; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetBestFit()const { return bBestFit; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetBestFitMinSize()const { return BestFitMinSize; }
	/** The size Best Fit actually drew at, which is GetFontSize() when Best Fit is off. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetRenderedFontSize()const { return RenderedFontSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") ETextWrappingPolicy GetWrappingPolicy()const{return WrappingPolicy;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextPhraseWrap GetPhraseWrap()const{return PhraseWrap;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FDreamTextStyle& GetTextStyle()const{return TextStyle;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetFillProgress()const{return FillProgress;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetGlowBoost()const{return GlowBoost;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const TArray<FDreamTextFillSegment>& GetFillSegments()const{return FillSegments;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextFontStyle GetFontStyle()const { return FontStyle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetRichText()const { return bRichText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetRichTextTagFilterFlags()const { return RichTextTagFilterFlags; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIRichTextCustomStyleData* GetRichTextCustomStyleData()const { return RichTextCustomStyleData; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIRichTextImageData_BaseObject* GetRichTextImageData()const { return RichTextImageData; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextParagraphHorizontalAlign GetParagraphHorizontalAlignment()const { return HAlign; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextParagraphVerticalAlign GetParagraphVerticalAlignment()const { return VAlign; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetExpandMeshSize()const{return ExpandMeshSize;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetDynamicPixelsPerUnit()const { return DynamicPixelsPerUnit; }

	/** indicating whether the text is Truncated or using Ellipsis */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool IsTextTruncated()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFont(UDreamUIFontData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", Meta = (AutoCreateRefTerm = "Value"))
		void SetText(const FText& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUseKerning(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontSpace(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOverflowType(EDreamUITextOverflowType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetWrappingPolicy(ETextWrappingPolicy Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPhraseWrap(EDreamTextPhraseWrap Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTextStyle(const FDreamTextStyle& Value);
	/** Fill progress of every line, 0..1; glyphs inside a fill segment keep the segment's own value. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFillProgress(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGlowBoost(float Value);
	/** Character runs that fill independently (a lyric line's words or syllables). Replaces the previous set. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFillSegments(const TArray<FDreamTextFillSegment>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ClearFillSegments();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMargin(const FMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLineHeightPercentage(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetWrapTextAt(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBestFit(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
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
	/**
	 * The layout input this text would lay out with at the given font size: its own properties plus
	 * what the widget and canvas contribute. Public so the pipeline can be driven from outside the
	 * component -- tests, tools -- against the same numbers the component uses.
	 */
	static FDreamTextLayoutInput MakeLayoutInput(const UDreamText* Text, float InFontSize);
	/** The paint parameters this text would paint with: the font's quad style, the canvas's needs, the final colour. */
	static FDreamTextPaintParams MakePaintParams(const UDreamText* Text);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontStyle(EDreamUITextFontStyle Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichText(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextTagFilterFlags(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextImageData(UDreamUIRichTextImageData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextCustomStyleData(UDreamUIRichTextCustomStyleData* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
    	void SetOverrideMaterial(UMaterialInterface* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetExpandMeshSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
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
	void GetSelectionProperty(int32 InSelectionStartCaretIndex, int32 InSelectionEndCaretIndex, TArray<FDreamUITextSelectionProperty>& OutSelectionProeprtyArray);
	const FDreamUITextGeometryCache& GetCacheTextGeometryData()const { UpdateCacheTextGeometry(); return CacheTextGeometryData; }
#pragma endregion UITextInputComponent
};
