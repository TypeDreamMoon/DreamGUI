// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTextInput.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UITextInput.h"

void UDreamTextInput::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Field"), BackgroundNode);
	OutParts.Emplace(TEXT("Placeholder"), PlaceholderNode);
	OutParts.Emplace(TEXT("ClipArea"), ClipNode);
	OutParts.Emplace(TEXT("Text"), TextNode);
}

void UDreamTextInput::RealizeBuiltIn()
{
	using namespace DreamUI;

	Realize(this,
		Node<UDreamRectBlock>("Field")
			.Stretch()
			.Children(
				DreamUI::Text("Placeholder").Stretch(),
				Widget("ClipArea")
					.Self([](UDreamWidget& InClip)
					{
						// The one structural fact of a text field: its content is regularly wider
						// than it is, and everything past the edge is someone else's pixels.
						InClip.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					.Children(
						DreamUI::Text("Text").Stretch())));
}

void UDreamTextInput::WireParts()
{
	// On the background, which is the control's own face: the input owns the whole box, not the
	// scrolled text inside it.
	InputBehaviour = EnsureComponent<UUITextInput>(BackgroundNode);
	if (InputBehaviour == nullptr)
	{
		return;
	}
	InputBehaviour->SetTextVisual(TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr);
	InputBehaviour->SetPlaceHolder(PlaceholderNode);
	InputBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamTextInput::HandleTextChanged);
	InputBehaviour->GetOnSubmitEvent().AddUObject(this, &UDreamTextInput::HandleSubmitted);
}

void UDreamTextInput::ApplyStyle()
{
	const FDreamTextInputStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::TextInputStyle);
	ShapeFace(BackgroundNode, Active.CornerRadius);
	SkinFace(BackgroundNode, Active.BackgroundBrush);

	// The padding is geometry, not a text property: the clip area is inset from the field, and the
	// placeholder is inset the same amount so the hint sits exactly where typing will.
	auto Inset = [&Active](UDreamWidget* InNode)
	{
		if (InNode != nullptr)
		{
			InNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
			InNode->SetAnchorOffset(Active.Padding);
		}
	};
	Inset(ClipNode);
	Inset(PlaceholderNode);

	auto StyleText = [&Active](UDreamWidget* InNode, const FColor& InColor)
	{
		if (UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			TextVisual->SetColor(InColor);
			TextVisual->SetFontSize(Active.FontSize);
			TextVisual->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
			TextVisual->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
		}
	};
	StyleText(TextNode, Active.TextColor);
	StyleText(PlaceholderNode, Active.PlaceholderColor);

	if (UDreamText* PlaceholderVisual = PlaceholderNode != nullptr ? Cast<UDreamText>(PlaceholderNode->GetVisual()) : nullptr)
	{
		PlaceholderVisual->SetText(Placeholder);
	}
	if (InputBehaviour != nullptr)
	{
		// The behaviour is a selectable and its pointer transition tints the field's own visual --
		// UISelectable defaults the target to it, in white. These are what actually colour the box.
		PushSelectableState(InputBehaviour, Active.Background, Active.BackgroundHovered, Active.Background,
			Active.BackgroundDisabled, Active.BackgroundFocused, Active.TransitionDuration);
		InputBehaviour->SetAllowMultiLine(bMultiLine);
		// Without an event: pushing the authored text in is not the user typing.
		InputBehaviour->SetTextWithoutNotify(Text);
	}
	SizeControlHeight(Active.Height);
}

FString UDreamTextInput::GetText() const
{
	return InputBehaviour != nullptr ? InputBehaviour->GetText() : Text;
}

void UDreamTextInput::SetText(const FString& InText)
{
	Text = InText;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetText(InText);
	}
}

void UDreamTextInput::HandleTextChanged(const FString& InText)
{
	Text = InText;
	OnTextChanged.Broadcast(InText);
	OnValueChangedBP.Broadcast(InText);
}

void UDreamTextInput::HandleSubmitted(const FString& InText)
{
	// Both spellings of the same moment, same payload: OnSubmitted is the compatibility name,
	// OnTextCommitted the UMG one. Wherever one fires the other must, so they broadcast from the
	// one place the behaviour reports a submit.
	OnSubmitted.Broadcast(InText);
	OnTextCommitted.Broadcast(InText);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TextInput", UDreamTextInput)
