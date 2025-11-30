// Copyright 2019-Present LexLiu. All Rights Reserved.

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
 *		UV2: X- bold size of DistanceField font
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
	virtual void OnTransformChanged()override;
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
	/** Use a custom material to render this text */
    UPROPERTY(EditAnywhere, Category = "LexUI")
    UMaterialInterface* OverrideMaterial = nullptr;
	/**
	 * Use full rect area to generate char mesh, useful for effects of OverrideMaterial.
	 * Only valid for SDF font.
	 */
	UPROPERTY(EditAnywhere, Category = "LexUI")
	bool bMeshUseFullRect = false;
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
	/** created object for rich text image */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> CreatedRichTextImageObjectArray;
	/** created object for emoji */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> CreatedEmojiObjectArray;
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay)
	bool bListRichTextImageObjectInOutliner = true;
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay)
	bool bListEmojiObjectInOutliner = true;
#endif
private:
	bool bHasAddToFont = false;
	FVector2f PrevScale2DForUIText = FVector2f::One();

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
	virtual void OnFillWidgetPropertyDataForMaterial() override;
	virtual void OnFillWidgetPropertyDataForMaterial_FirstPixel() override;
	virtual void OnCultureChanged_Implementation()override;

public:
	void ApplyFontTextureScaleUp();
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
	UFUNCTION(BlueprintCallable, Category = "LGUI") ETextWrappingPolicy GetWrappingPolicy()const{return WrappingPolicy;}
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextFontStyle GetFontStyle()const { return FontStyle; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool GetRichText()const { return bRichText; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") int32 GetRichTextTagFilterFlags()const { return RichTextTagFilterFlags; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUIRichTextCustomStyleData* GetRichTextCustomStyleData()const { return RichTextCustomStyleData; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUIRichTextImageData_BaseObject* GetRichTextImageData()const { return RichTextImageData; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextParagraphHorizontalAlign GetParagraphHorizontalAlignment()const { return HAlign; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUITextParagraphVerticalAlign GetParagraphVerticalAlignment()const { return VAlign; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}

	/** indicating whether the text is Truncated or using Ellipsis */
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool IsTextTruncated()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetFont(ULexUIFontData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
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
		void SetFontStyle(ELexUITextFontStyle Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichText(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextTagFilterFlags(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextImageData(ULexUIRichTextImageData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRichTextCustomStyleData(ULexUIRichTextCustomStyleData* Value);
	UFUNCTION(BlueprintCallable, Category = "LexUI")
    	void SetOverrideMaterial(UMaterialInterface* Value);
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
