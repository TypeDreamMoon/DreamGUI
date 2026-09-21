// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamRichTextBlock.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_BaseObject.h"
//GetDefaultDynamicMaterial instances one over whatever the style named
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void UDreamRichTextBlock::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Text"), TextNode);
}

void UDreamRichTextBlock::RealizeBuiltIn()
{
	using namespace DreamUI;

	// One node. There is no face, no padding and no states, because a rich text block is a paragraph
	// -- put it in a Native.Border when it needs a box, which is the composition this library prefers
	// to a control that carries a box nine users out of ten switch off.
	Realize(this,
		DreamUI::Text("Text")
			.Stretch()
			.Visual([](UDreamText& InText)
			{
				// The flag IS the control: everything else here is what any text node can do.
				InText.SetRichText(true);
			}));
}

void UDreamRichTextBlock::ApplyStyle()
{
	const FDreamRichTextStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RichTextStyle);

	UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr;
	if (TextVisual == nullptr)
	{
		// A template road whose author drew something that is not a text node. Nothing to push and
		// nothing to crash on -- the same null-check every other control's style push makes.
		return;
	}
	// Re-stated on every push rather than only at build, for the template road's reason: a node a
	// template supplied carries the library's defaults, and a flag only the code tree ever set is a
	// flag the template road silently does without.
	TextVisual->SetRichText(true);
	TextVisual->SetRichTextTagFilterFlags(TagFilterFlags);
	TextVisual->SetRichTextCustomStyleData(CustomStyleData);
	TextVisual->SetRichTextImageData(ImageData);

	TextVisual->SetText(Text);
	TextVisual->SetColor(Active.TextColor);
	TextVisual->SetFontSize(Active.FontSize);
	if (Active.Font != nullptr)
	{
		// Only when the style NAMES one -- the DEFAULT typeface, which markup may still switch per
		// run. Pushing null would take the project's default font away from every block that has
		// never stated one, which is all of them.
		TextVisual->SetFont(Active.Font);
	}
	// Outline, drop shadow and glow, which the sheet now describes. Default-constructed means every
	// effect switched off (each colour ships with alpha zero), so a block that states nothing looks
	// exactly as it did before the style carried them.
	TextVisual->SetTextStyle(Active.TextStyle);
	TextVisual->SetOverrideMaterial(Active.OverrideMaterial);
	TextVisual->SetParagraphHorizontalAlignment(HorizontalAlignment);
	TextVisual->SetParagraphVerticalAlignment(VerticalAlignment);
	TextVisual->SetOverflowType(OverflowType);
	TextVisual->SetAutoWrapText(bAutoWrapText);
	TextVisual->SetTextTransform(TextTransformPolicy);
	TextVisual->SetMinDesiredWidth(MinDesiredWidth);
}

/*
 * The setters, which are all one shape: write the control's field, then push that ONE thing onto the
 * paragraph.
 *
 * Not ApplyStyle, deliberately. A rich text block is the one control whose push is expensive -- every
 * one of them re-parses the markup through SetText -- so re-deriving two data assets and eight style
 * fields to change a case policy is work nobody asked for on a control whose whole job is to be
 * written to. SetText makes the same argument in its own comment.
 */

void UDreamRichTextBlock::SetStyle(const FDreamRichTextStyle& InStyle)
{
	Style = InStyle;
	// The exception: a whole style really did move, so the whole push is the right size of answer.
	ApplyStyle();
}

void UDreamRichTextBlock::SetTagFilterFlags(int32 InTagFilterFlags)
{
	TagFilterFlags = InTagFilterFlags;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetRichTextTagFilterFlags(InTagFilterFlags);
		// Which tags are acted on is a PARSE decision, so the prose has to go through the parser
		// again -- the flags alone change nothing about text that has already been parsed.
		TextVisual->SetText(Text);
	}
}

void UDreamRichTextBlock::SetCustomStyleData(UDreamUIRichTextCustomStyleData* InCustomStyleData)
{
	CustomStyleData = InCustomStyleData;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetRichTextCustomStyleData(InCustomStyleData);
		TextVisual->SetText(Text);
	}
}

void UDreamRichTextBlock::SetImageData(UDreamUIRichTextImageData_BaseObject* InImageData)
{
	ImageData = InImageData;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetRichTextImageData(InImageData);
		TextVisual->SetText(Text);
	}
}

void UDreamRichTextBlock::SetHorizontalAlignment(EDreamUITextParagraphHorizontalAlign InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetParagraphHorizontalAlignment(InHorizontalAlignment);
	}
}

void UDreamRichTextBlock::SetVerticalAlignment(EDreamUITextParagraphVerticalAlign InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetParagraphVerticalAlignment(InVerticalAlignment);
	}
}

void UDreamRichTextBlock::SetOverflowType(EDreamUITextOverflowType InOverflowType)
{
	OverflowType = InOverflowType;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetOverflowType(InOverflowType);
	}
}

void UDreamRichTextBlock::SetAutoWrapText(bool bInAutoWrapText)
{
	bAutoWrapText = bInAutoWrapText;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetAutoWrapText(bInAutoWrapText);
	}
}

void UDreamRichTextBlock::SetTextTransformPolicy(EDreamUITextTransformPolicy InTransformPolicy)
{
	TextTransformPolicy = InTransformPolicy;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetTextTransform(InTransformPolicy);
	}
}

void UDreamRichTextBlock::SetMinDesiredWidth(float InMinDesiredWidth)
{
	MinDesiredWidth = FMath::Max(0.0f, InMinDesiredWidth);
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		// Onto the paragraph and nowhere else: a rich text block IS its paragraph, stretched to the
		// control's rect, so the number a content-sized parent reads is the paragraph's.
		TextVisual->SetMinDesiredWidth(MinDesiredWidth);
	}
}

void UDreamRichTextBlock::SetDefaultFont(UDreamUIFontData_BaseObject* InFont)
{
	Style.Font = InFont;
	// The one place the expensive push is right: a typeface has to come back out of ResolveStyle
	// with the rest of the look, and changing it re-lays the prose anyway.
	ApplyStyle();
}

void UDreamRichTextBlock::RefreshTextLayout()
{
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		// Pushing the SAME prose is what re-runs the parser, which is the whole of a refresh here:
		// the markup is resolved during parsing, so a style asset edited in place after the last
		// parse is not seen until the prose goes through again.
		TextVisual->SetText(Text);
	}
}

UMaterialInstanceDynamic* UDreamRichTextBlock::GetDefaultDynamicMaterial()
{
	UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr;
	if (TextVisual == nullptr)
	{
		return nullptr;
	}
	UMaterialInterface* Current = TextVisual->GetOverrideMaterial();
	if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Current))
	{
		// Already this block's own. Asked twice, the same instance comes back -- UMG's contract, and
		// the reason a caller can hold on to it.
		return Existing;
	}
	if (Current == nullptr)
	{
		// Nothing to instance. Handing back an instance of the library's built-in text material
		// would let a caller edit every other paragraph in the project through it.
		return nullptr;
	}
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Current, this);
	if (Dynamic != nullptr)
	{
		TextVisual->SetOverrideMaterial(Dynamic);
	}
	return Dynamic;
}

void UDreamRichTextBlock::SetText(const FText& InText)
{
	Text = InText;
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		// Straight onto the visual rather than through ApplyStyle: setting the prose is not a restyle,
		// and re-pushing two data assets and six style fields to change a string is work nobody asked
		// for on a control whose whole job is to be written to.
		TextVisual->SetText(InText);
	}
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "RichText", UDreamRichTextBlock)
