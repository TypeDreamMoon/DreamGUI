// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamRichTextBlock.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"

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
	TextVisual->SetParagraphHorizontalAlignment(HorizontalAlignment);
	TextVisual->SetParagraphVerticalAlignment(VerticalAlignment);
	TextVisual->SetOverflowType(OverflowType);
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
