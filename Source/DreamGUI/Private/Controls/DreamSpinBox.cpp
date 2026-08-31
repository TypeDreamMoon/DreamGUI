// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamSpinBox.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UITextInput.h"
#include "Text/DreamUIValueFormat.h"

void UDreamSpinBox::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	Realize(this,
		Widget("SpinBox")
			.Stretch()
			.With<UDreamLayoutContainerHorizontalBox>()
			.Children(
				// The step faces are button-shaped the way DreamButton is: the face IS the node the
				// behaviour stands on, with a glyph centred in an overlay. Auto slots, so their
				// width comes from the brush (set in ApplyStyle); the field takes what remains.
				Image("Decrement").Out(DecrementNode)
					.With<UDreamLayoutContainerOverlay>()
					.With<UUIButton>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})
					.Children(
						DreamUI::Text("DecrementGlyph").Out(DecrementLabelNode)
							.Visual([](UDreamText& InText)
							{
								// ASCII hyphen-minus: U+2212 has no glyph in the default SDF font
								// and drew a tofu box. A hyphen sits slightly low at
								// button sizes.
								InText.SetText(FText::AsCultureInvariant(TEXT("-")));
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
							})
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
							})),
				Image("Field").Out(FieldNode)
					.With<UUITextInput>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
					})
					.Children(
						Widget("ClipArea").Out(ClipNode)
							// Stretch, explicitly: the field has no layout container, so this node
							// has no slot, and a node left to its anchor defaults is a 100x100 box
							// in the middle of the field -- .Anchors alone would not clear that
							// SizeDelta either, which is why this is Stretch and not Anchors.
							.Stretch()
							.Self([](UDreamWidget& InClip)
							{
								// A number is short until someone types into it; past the edge is
								// the step buttons' pixels.
								InClip.SetClipping(EDreamWidgetClipping::ClipToBounds);
							})
							.Children(
								DreamUI::Text("Value").Out(ValueTextNode).Stretch()
									.Visual([](UDreamText& InText)
									{
										InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
										InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
									}))),
				Image("Increment").Out(IncrementNode)
					.With<UDreamLayoutContainerOverlay>()
					.With<UUIButton>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})
					.Children(
						DreamUI::Text("IncrementGlyph").Out(IncrementLabelNode)
							.Visual([](UDreamText& InText)
							{
								InText.SetText(FText::AsCultureInvariant(TEXT("+")));
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
							})
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
							})))
			.Then([this](UDreamWidget& InRoot)
			{
				// Three behaviours on three nodes below the root -- none existed when the root was
				// built, which is what .Then is for.
				DecrementBehaviour = DecrementNode != nullptr ? DecrementNode->GetComponent<UUIButton>() : nullptr;
				IncrementBehaviour = IncrementNode != nullptr ? IncrementNode->GetComponent<UUIButton>() : nullptr;
				InputBehaviour = FieldNode != nullptr ? FieldNode->GetComponent<UUITextInput>() : nullptr;
				if (DecrementBehaviour != nullptr)
				{
					DecrementBehaviour->SetTransitionTarget(DecrementNode->GetVisual());
					DecrementBehaviour->GetOnClickEvent().AddUObject(this, &UDreamSpinBox::HandleDecrementClicked);
				}
				if (IncrementBehaviour != nullptr)
				{
					IncrementBehaviour->SetTransitionTarget(IncrementNode->GetVisual());
					IncrementBehaviour->GetOnClickEvent().AddUObject(this, &UDreamSpinBox::HandleIncrementClicked);
				}
				if (InputBehaviour != nullptr)
				{
					InputBehaviour->SetTextVisual(ValueTextNode != nullptr ? Cast<UDreamText>(ValueTextNode->GetVisual()) : nullptr);
					// Per-character rejection of everything a number cannot contain; the submit
					// parse below stays the backstop for shapes the filter cannot judge ("-", "1.2.").
					InputBehaviour->SetInputType(EUITextInputType::DecimalNumber);
					InputBehaviour->GetOnSubmitEvent().AddUObject(this, &UDreamSpinBox::HandleSubmitted);
				}
			}));

	ApplyStyle();
}

void UDreamSpinBox::ApplyStyle()
{
	const FDreamSpinBoxStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::SpinBoxStyle);

	ShapeFace(DecrementNode, Active.CornerRadius);
	ShapeFace(FieldNode, Active.CornerRadius);
	ShapeFace(IncrementNode, Active.CornerRadius);

	// The step faces' width goes through the BRUSH: they sit in Auto slots, and an Auto slot reads
	// the visual's preferred size -- SetWidth would be overwritten by the next arrange pass. Height
	// is the slot's (Fill), so only the X of the brush size is load-bearing.
	auto SizeFace = [&Active](UDreamWidget* InNode)
	{
		if (UDreamImage* FaceImage = InNode != nullptr ? Cast<UDreamImage>(InNode->GetVisual()) : nullptr)
		{
			FDreamUIImageBrush Brush = FaceImage->GetBrush();
			Brush.ImageSize = FVector2f(Active.ButtonWidth, Active.Height);
			FaceImage->SetBrush(Brush);
		}
	};
	SizeFace(DecrementNode);
	SizeFace(IncrementNode);

	auto StyleGlyph = [&Active](UDreamWidget* InNode)
	{
		if (UDreamText* GlyphVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			GlyphVisual->SetColor(Active.TextColor);
			GlyphVisual->SetFontSize(Active.FontSize);
		}
	};
	StyleGlyph(DecrementLabelNode);
	StyleGlyph(IncrementLabelNode);
	StyleGlyph(ValueTextNode);

	if (DecrementBehaviour != nullptr)
	{
		DecrementBehaviour->SetNormalColor(Active.ButtonNormal);
		DecrementBehaviour->SetHoveredColor(Active.ButtonHovered);
		DecrementBehaviour->SetPressedColor(Active.ButtonPressed);
	}
	if (IncrementBehaviour != nullptr)
	{
		IncrementBehaviour->SetNormalColor(Active.ButtonNormal);
		IncrementBehaviour->SetHoveredColor(Active.ButtonHovered);
		IncrementBehaviour->SetPressedColor(Active.ButtonPressed);
	}
	if (InputBehaviour != nullptr)
	{
		// The field's behaviour is a selectable and its pointer transition tints the field's own
		// visual -- left unset those colours are white, and the field ships as a white bar.
		InputBehaviour->SetNormalColor(Active.FieldBackground);
		InputBehaviour->SetHoveredColor(Active.FieldBackgroundHovered);
		InputBehaviour->SetPressedColor(Active.FieldBackground);
	}

	// The authored value, clamped against the authored range and mirrored, so the property and the
	// field never show two different numbers. Eventless: pushing authored state is not a change.
	Value = FMath::Clamp(Value, MinValue, MaxValue);
	PushValueToParts();

	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SetHeight(Active.Height);
}

float UDreamSpinBox::GetValue() const
{
	return Value;
}

void UDreamSpinBox::SetValue(float InValue)
{
	ApplyValueChange(InValue);
}

void UDreamSpinBox::Increment()
{
	ApplyValueChange(Value + StepSize);
}

void UDreamSpinBox::Decrement()
{
	ApplyValueChange(Value - StepSize);
}

void UDreamSpinBox::HandleDecrementClicked()
{
	Decrement();
}

void UDreamSpinBox::HandleIncrementClicked()
{
	Increment();
}

void UDreamSpinBox::HandleSubmitted(const FString& InText)
{
	float Parsed = 0.0f;
	if (LexTryParseString(Parsed, *InText))
	{
		ApplyValueChange(Parsed);
	}
	else
	{
		// Unparseable text does not become a value; the field snaps back to the number the control
		// still holds. (The DecimalNumber filter keeps letters out, but "-" or "1.2." get this far.)
		PushValueToParts();
	}
}

void UDreamSpinBox::ApplyValueChange(float InValue)
{
	const float Clamped = FMath::Clamp(InValue, MinValue, MaxValue);
	const bool bChanged = Clamped != Value;
	Value = Clamped;
	// Push even when nothing changed: a submit of "00100" or a step against the stop should still
	// snap the field back to the canonical spelling.
	PushValueToParts();
	if (bChanged)
	{
		OnValueChangedBP.Broadcast(Value), OnValueChanged.Broadcast(Value);
	}
}

void UDreamSpinBox::PushValueToParts()
{
	const FString Spelled = FormatValue();
	if (InputBehaviour != nullptr)
	{
		// Without notify: the field showing the control's value is not the user typing. Cleared
		// first, not as belt-and-braces: UUITextInput::SetText runs every character of the NEW
		// string through IsValidChar, and for DecimalNumber that check refuses a '.' (or a leading
		// '-') already present in the OLD text -- so "3.5" pushed over "2.5" arrives as "35".
		// Replacing through empty gives the filter nothing stale to refuse against.
		InputBehaviour->SetTextWithoutNotify(FString());
		InputBehaviour->SetTextWithoutNotify(Spelled);
	}
	// Straight onto the visual as well. The behaviour's own visual write sits behind a
	// render-canvas check (it needs geometry to clamp visible characters), so before this control
	// is registered anywhere -- including under a headless test -- the string above reaches the
	// behaviour's state and stops. The value text is this control's to show its own value on; once
	// a canvas exists the behaviour overwrites it with the same characters.
	if (UDreamText* ValueVisual = ValueTextNode != nullptr ? Cast<UDreamText>(ValueTextNode->GetVisual()) : nullptr)
	{
		ValueVisual->SetText(FText::AsCultureInvariant(Spelled));
	}
}

FString UDreamSpinBox::FormatValue() const
{
	// The project's one scalar printer: shortest spelling that reads back to exactly this float,
	// always '.' for the decimal point. FString::SanitizeFloat is six fixed decimals and drifts;
	// FText::AsNumber is culture-dependent, and a value the parser cannot read back is a value the
	// field destroys on the next submit.
	return DreamUIValueFormat::PrintScalar(Value, /*bSinglePrecision*/ true);
}
