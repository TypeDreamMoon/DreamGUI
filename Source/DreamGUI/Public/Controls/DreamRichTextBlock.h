// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamText.h"
#include "DreamRichTextBlock.generated.h"

class UDreamWidget;
class UDreamUIRichTextImageData_BaseObject;
class UDreamUIRichTextCustomStyleData;
class UMaterialInstanceDynamic;
class UDreamUIFontData_BaseObject;

/**
 * A rich text block whose hierarchy is code, not an asset: one paragraph that reads markup.
 *
 * UMG's RichTextBlock resolves `<Tag>text</Tag>` against a decorator set; this library's text stack
 * has had the same idea since it was written -- UDreamText::bRichText plus two data assets, one
 * mapping tags to styles and one mapping tags to images -- and what was missing was a CONTROL that
 * turns it on. That is all this class is: a text node with the flag set, the two assets exposed, and
 * the colour and size coming from the project sheet like every other control's.
 *
 * WHY IT IS A CONTROL AND NOT A FLAG ON A TEXT NODE
 * -------------------------------------------------
 * The flag already exists on the node, and an author who wants it there can still set it. What a
 * control adds is the thing a flag cannot: a `Native.RichText` tag that reads the project's text
 * colour and size, so a project restyles its prose in one place, and a place for the two data assets
 * to be authored per instance without anyone reaching into a visual.
 *
 * WHAT THE MARKUP MEANS is not this control's business and is not restated here -- the tag
 * vocabulary, the filter flags and the two asset formats belong to UDreamText and its parser, and a
 * second description of them in a control header is a second thing to keep true.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Rich Text Block")
class DREAMGUI_API UDreamRichTextBlock : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Rich Text")
	FDreamRichTextStyle Style;

	/** The prose, markup and all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetText", BlueprintSetter = "SetText", Category = "Rich Text", meta = (MultiLine = "true"))
	FText Text;

	/** Which tags the parser is allowed to act on. The default is all of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTagFilterFlags", BlueprintSetter = "SetTagFilterFlags", Category = "Rich Text",
		meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIText_RichTextTagFilterFlags"))
	int32 TagFilterFlags = 0xffffffff;

	/** What `<MyTag>` means, as a style. Null leaves custom tags to the char-selection consumers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetCustomStyleData", BlueprintSetter = "SetCustomStyleData", Category = "Rich Text")
	TObjectPtr<UDreamUIRichTextCustomStyleData> CustomStyleData = nullptr;

	/** What `<img=key/>` means. Null draws nothing for an image tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetImageData", BlueprintSetter = "SetImageData", Category = "Rich Text")
	TObjectPtr<UDreamUIRichTextImageData_BaseObject> ImageData = nullptr;

	/** How the paragraph sits in the control's rect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetHorizontalAlignment", BlueprintSetter = "SetHorizontalAlignment", Category = "Rich Text")
	EDreamUITextParagraphHorizontalAlign HorizontalAlignment = EDreamUITextParagraphHorizontalAlign::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVerticalAlignment", BlueprintSetter = "SetVerticalAlignment", Category = "Rich Text")
	EDreamUITextParagraphVerticalAlign VerticalAlignment = EDreamUITextParagraphVerticalAlign::Top;

	/**
	 * What a line too long for the control does. VerticalOverflow is the one that WRAPS, which is
	 * what prose usually wants and what a rich text block almost always is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetOverflowType", BlueprintSetter = "SetOverflowType", Category = "Rich Text")
	EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::VerticalOverflow;

	/**
	 * Break a line that does not fit rather than letting it run on -- UMG's AutoWrapText.
	 *
	 * Off by default, which is the paragraph's own default and therefore what every existing block
	 * does. It only bites under the overflow types that do NOT already wrap: VerticalOverflow always
	 * wraps, so a block left on the shipped overflow will not notice this either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAutoWrapText", BlueprintSetter = "SetAutoWrapText", Category = "Rich Text")
	bool bAutoWrapText = false;

	/** Case the prose is DRAWN in -- UMG's TextTransformPolicy. The authored string is untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTextTransformPolicy", BlueprintSetter = "SetTextTransformPolicy", Category = "Rich Text")
	EDreamUITextTransformPolicy TextTransformPolicy = EDreamUITextTransformPolicy::None;

	/**
	 * A floor under the width the paragraph asks a content-sized parent for -- UMG's MinDesiredWidth.
	 * Zero means no opinion, which is every block that exists today.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinDesiredWidth", BlueprintSetter = "SetMinDesiredWidth", Category = "Rich Text", meta = (ClampMin = "0.0"))
	float MinDesiredWidth = 0.0f;

	/** The paragraph itself. Public because everything under a control is reachable by name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rich Text")
	TObjectPtr<UDreamWidget> TextNode = nullptr;

	UFUNCTION(BlueprintPure, Category = "Rich Text")
	FText GetText() const { return Text; }

	/** Replace the prose. Re-parsed on the way in, which is what the markup costs. */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetText(const FText& InText);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	FDreamRichTextStyle GetStyle() const { return Style; }

	/** The whole look at once, re-pushed. */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetStyle(const FDreamRichTextStyle& InStyle);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	int32 GetTagFilterFlags() const { return TagFilterFlags; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetTagFilterFlags(int32 InTagFilterFlags);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	UDreamUIRichTextCustomStyleData* GetCustomStyleData() const { return CustomStyleData; }

	/**
	 * What the custom tags mean. UMG's TextStyleSet under the name this library gives it: a data
	 * asset mapping tag to style, which is the same job its RichTextStyleRow table does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetCustomStyleData(UDreamUIRichTextCustomStyleData* InCustomStyleData);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	UDreamUIRichTextImageData_BaseObject* GetImageData() const { return ImageData; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetImageData(UDreamUIRichTextImageData_BaseObject* InImageData);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	EDreamUITextParagraphHorizontalAlign GetHorizontalAlignment() const { return HorizontalAlignment; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetHorizontalAlignment(EDreamUITextParagraphHorizontalAlign InHorizontalAlignment);

	/** UMG's Justification, which on a paragraph is the horizontal alignment. One implementation. */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetJustification(EDreamUITextParagraphHorizontalAlign InJustification) { SetHorizontalAlignment(InJustification); }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	EDreamUITextParagraphVerticalAlign GetVerticalAlignment() const { return VerticalAlignment; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetVerticalAlignment(EDreamUITextParagraphVerticalAlign InVerticalAlignment);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	EDreamUITextOverflowType GetOverflowType() const { return OverflowType; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetOverflowType(EDreamUITextOverflowType InOverflowType);

	/** UMG's name for the same decision. Forwards, so there is one writer and one truth. */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetTextOverflowPolicy(EDreamUITextOverflowType InOverflowPolicy) { SetOverflowType(InOverflowPolicy); }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	bool GetAutoWrapText() const { return bAutoWrapText; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetAutoWrapText(bool bInAutoWrapText);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	EDreamUITextTransformPolicy GetTextTransformPolicy() const { return TextTransformPolicy; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetTextTransformPolicy(EDreamUITextTransformPolicy InTransformPolicy);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	float GetMinDesiredWidth() const { return MinDesiredWidth; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetMinDesiredWidth(float InMinDesiredWidth);

	/**
	 * Re-parse the markup and lay the paragraph out again -- UMG's RefreshTextLayout.
	 *
	 * The prose has not changed, so this is not SetText: what may have changed is what the markup
	 * RESOLVES against (a style asset edited in place, an image data asset reloaded), and the parser
	 * only reads those while it is parsing.
	 */
	/**
	 * The DEFAULT typeface for undecorated prose -- UMG's SetDefaultFont, minus what FSlateFontInfo
	 * packs in beside it: the size is Style.FontSize and the outline is Style.TextStyle. Markup may
	 * still switch the face per run; this is what a run that says nothing gets.
	 *
	 * Reads and writes Style.Font, so it obeys StyleSource like the rest of the look. Null means
	 * "leave the paragraph on the font it has", which is every block that exists today.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	UDreamUIFontData_BaseObject* GetDefaultFont() const { return Style.Font; }

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetDefaultFont(UDreamUIFontData_BaseObject* InFont);

	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void RefreshTextLayout();

	/**
	 * A material instance of this block's own, created over the style's material the first time it
	 * is asked for -- UMG's GetDefaultDynamicMaterial.
	 *
	 * Returns null when the style names no material: there is nothing to instance, and handing back
	 * an instance of the library's built-in text material would let a caller edit every other
	 * paragraph in the project through it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	UMaterialInstanceDynamic* GetDefaultDynamicMaterial();

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
};
