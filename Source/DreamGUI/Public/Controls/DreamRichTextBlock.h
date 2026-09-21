// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamText.h"
#include "DreamRichTextBlock.generated.h"

class UDreamWidget;
class UDreamUIRichTextImageData_BaseObject;
class UDreamUIRichTextCustomStyleData;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	FDreamRichTextStyle Style;

	/** The prose, markup and all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text", meta = (MultiLine = "true"))
	FText Text;

	/** Which tags the parser is allowed to act on. The default is all of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text",
		meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIText_RichTextTagFilterFlags"))
	int32 TagFilterFlags = 0xffffffff;

	/** What `<MyTag>` means, as a style. Null leaves custom tags to the char-selection consumers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	TObjectPtr<UDreamUIRichTextCustomStyleData> CustomStyleData = nullptr;

	/** What `<img=key/>` means. Null draws nothing for an image tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	TObjectPtr<UDreamUIRichTextImageData_BaseObject> ImageData = nullptr;

	/** How the paragraph sits in the control's rect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	EDreamUITextParagraphHorizontalAlign HorizontalAlignment = EDreamUITextParagraphHorizontalAlign::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	EDreamUITextParagraphVerticalAlign VerticalAlignment = EDreamUITextParagraphVerticalAlign::Top;

	/**
	 * What a line too long for the control does. VerticalOverflow is the one that WRAPS, which is
	 * what prose usually wants and what a rich text block almost always is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text")
	EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::VerticalOverflow;

	/** The paragraph itself. Public because everything under a control is reachable by name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rich Text")
	TObjectPtr<UDreamWidget> TextNode = nullptr;

	UFUNCTION(BlueprintPure, Category = "Rich Text")
	FText GetText() const { return Text; }

	/** Replace the prose. Re-parsed on the way in, which is what the markup costs. */
	UFUNCTION(BlueprintCallable, Category = "Rich Text")
	void SetText(const FText& InText);

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
};
