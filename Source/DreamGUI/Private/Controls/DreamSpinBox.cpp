// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamSpinBox.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIButton.h"
#include "Interaction/UITextInput.h"
#include "Text/DreamUIValueFormat.h"

void UDreamSpinBox::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Decrement"), DecrementNode);
	OutParts.Emplace(TEXT("DecrementGlyph"), DecrementLabelNode);
	OutParts.Emplace(TEXT("Field"), FieldNode);
	OutParts.Emplace(TEXT("ClipArea"), ClipNode);
	OutParts.Emplace(TEXT("Value"), ValueTextNode);
	OutParts.Emplace(TEXT("Increment"), IncrementNode);
	OutParts.Emplace(TEXT("IncrementGlyph"), IncrementLabelNode);
}

void UDreamSpinBox::RealizeBuiltIn()
{
	using namespace DreamUI;

	Realize(this,
		Widget("SpinBox")
			.Stretch()
			.With<UDreamLayoutContainerHorizontalBox>()
			.Children(
				// The step faces are button-shaped the way DreamButton is: the face IS the node the
				// behaviour stands on, with a glyph centred in an overlay. Auto slots, so their
				// width is the authored fallback (set in ApplyStyle); the field takes what remains.
				Node<UDreamRectBlock>("Decrement")
					.With<UDreamLayoutContainerOverlay>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})
					.Children(
						DreamUI::Text("DecrementGlyph")
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
				Node<UDreamRectBlock>("Field")
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
					})
					.Children(
						Widget("ClipArea")
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
								DreamUI::Text("Value").Stretch()
									.Visual([](UDreamText& InText)
									{
										InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
										InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
									}))),
				Node<UDreamRectBlock>("Increment")
					.With<UDreamLayoutContainerOverlay>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})
					.Children(
						DreamUI::Text("IncrementGlyph")
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
							}))));
}

void UDreamSpinBox::WireParts()
{
	// Three behaviours on three parts. Ensure rather than Get on every one of them: a template's
	// author draws two buttons and a field, and this is what makes them behave like any.
	DecrementBehaviour = EnsureComponent<UUIButton>(DecrementNode);
	IncrementBehaviour = EnsureComponent<UUIButton>(IncrementNode);
	InputBehaviour = EnsureComponent<UUITextInput>(FieldNode);
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
		// Per-character rejection of everything a number cannot contain; the submit parse below
		// stays the backstop for shapes the filter cannot judge ("-", "1.2.").
		InputBehaviour->SetInputType(EUITextInputType::DecimalNumber);
		InputBehaviour->GetOnSubmitEvent().AddUObject(this, &UDreamSpinBox::HandleSubmitted);
	}
}

void UDreamSpinBox::ApplyStyle()
{
	const FDreamSpinBoxStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::SpinBoxStyle);

	ShapeFace(DecrementNode, Active.CornerRadius);
	ShapeFace(FieldNode, Active.CornerRadius);
	ShapeFace(IncrementNode, Active.CornerRadius);

	SkinFace(DecrementNode, Active.ButtonBrush);
	SkinFace(IncrementNode, Active.ButtonBrush);
	SkinFace(FieldNode, Active.FieldBrush);

	// The step faces sit in Auto slots; a rect block states no size of its own, so the authored
	// width is what Auto measures. Height is the slot's (Fill) -- only the X is load-bearing.
	SizeFace(DecrementNode, FVector2D(Active.ButtonWidth, Active.Height));
	SizeFace(IncrementNode, FVector2D(Active.ButtonWidth, Active.Height));

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
		PushSelectableState(DecrementBehaviour, Active.ButtonNormal, Active.ButtonHovered,
			Active.ButtonPressed, Active.ButtonDisabled, Active.ButtonFocused, Active.TransitionDuration);
	}
	if (IncrementBehaviour != nullptr)
	{
		PushSelectableState(IncrementBehaviour, Active.ButtonNormal, Active.ButtonHovered,
			Active.ButtonPressed, Active.ButtonDisabled, Active.ButtonFocused, Active.TransitionDuration);
	}
	if (InputBehaviour != nullptr)
	{
		// The field's behaviour is a selectable and its pointer transition tints the field's own
		// visual -- left unset those colours are white, and the field ships as a white bar.
		// The field's pressed state is its resting one: clicking into a text field places a caret,
		// it does not push a button, and a face that darkened would say otherwise.
		PushSelectableState(InputBehaviour, Active.FieldBackground, Active.FieldBackgroundHovered,
			Active.FieldBackground, Active.ButtonDisabled, Active.ButtonFocused, Active.TransitionDuration);
	}

	// The authored value, clamped against the authored range and mirrored, so the property and the
	// field never show two different numbers. Eventless: pushing authored state is not a change.
	Value = FMath::Clamp(Value, MinValue, MaxValue);
	PushValueToParts();

	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SizeControlHeight(Active.Height);
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
	// A step is a COMMIT: one click, one settled value, which is what USpinBox's arrows do and what a
	// consumer writing to a setting needs to hear about exactly once.
	CommitValue(Value + StepSize);
}

void UDreamSpinBox::Decrement()
{
	CommitValue(Value - StepSize);
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
		// A typed entry is a COMMIT, not an intermediate value -- it is the moment USpinBox fires
		// OnValueCommitted from, and the moment the two focus knobs act on.
		CommitValue(Parsed);
	}
	else
	{
		// Unparseable text does not become a value; the field snaps back to the number the control
		// still holds. (The DecimalNumber filter keeps letters out, but "-" or "1.2." get this far.)
		PushValueToParts();
	}
}

float UDreamSpinBox::GetSliderMinValue() const
{
	return bOverride_MinSliderValue ? MinSliderValue : MinValue;
}

float UDreamSpinBox::GetSliderMaxValue() const
{
	return bOverride_MaxSliderValue ? MaxSliderValue : MaxValue;
}

float UDreamSpinBox::SnapToStep(float InValue) const
{
	if (!bAlwaysUsesDeltaSnap || StepSize <= KINDA_SMALL_NUMBER)
	{
		return InValue;
	}
	// Measured FROM MinValue rather than from zero, for the reason the slider's mouse step is: a
	// range of 3..10 snapped by 2 must offer the ends the author stated, not 4, 6, 8, 10.
	return MinValue + FMath::RoundToFloat((InValue - MinValue) / StepSize) * StepSize;
}

float UDreamSpinBox::ValueToSliderFraction(float InValue) const
{
	const float SliderMin = GetSliderMinValue();
	const float SliderMax = GetSliderMaxValue();
	const float Span = SliderMax - SliderMin;
	if (FMath::Abs(Span) <= KINDA_SMALL_NUMBER)
	{
		// An empty scrub range is a scrub that can go nowhere -- and, unguarded, a division that
		// hands a NaN to the drag arithmetic the way the slider's range once did.
		return 0.0f;
	}
	const float Linear = FMath::Clamp((InValue - SliderMin) / Span, 0.0f, 1.0f);
	const float Exponent = FMath::Max(SliderExponent, KINDA_SMALL_NUMBER);
	// The inverse of the bend the forward direction applies, so a fraction round-trips: a scrub that
	// starts where the value already is must not jump on its first frame.
	return FMath::Pow(Linear, 1.0f / Exponent);
}

float UDreamSpinBox::SliderFractionToValue(float InFraction) const
{
	const float SliderMin = GetSliderMinValue();
	const float SliderMax = GetSliderMaxValue();
	const float Exponent = FMath::Max(SliderExponent, KINDA_SMALL_NUMBER);
	const float Bent = FMath::Pow(FMath::Clamp(InFraction, 0.0f, 1.0f), Exponent);
	return SliderMin + (SliderMax - SliderMin) * Bent;
}

bool UDreamSpinBox::NativeOnBeginDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnBeginDrag(EventData);
	if (!bEnableSlider || EventData == nullptr
		|| EventData->InputType != EDreamUIPointerInputType::Pointer)
	{
		return bBubble;
	}
	bSliderMoving = true;
	// Where the value already is, in the scrub's own coordinates. Every drag frame is an OFFSET from
	// this rather than an absolute read of the pointer, which is what keeps the number from jumping
	// to wherever in the field the drag happened to start.
	SliderPressFraction = ValueToSliderFraction(Value);
	OnBeginSliderMovement.Broadcast(Value);
	return bBubble;
}

bool UDreamSpinBox::NativeOnDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnDrag(EventData);
	if (!bSliderMoving || EventData == nullptr)
	{
		return bBubble;
	}
	const float ScrubWidth = GetWidth();
	if (ScrubWidth <= KINDA_SMALL_NUMBER)
	{
		// A control with no width has no travel to measure against, and dividing by it would hand the
		// value a NaN it can never come back from.
		return bBubble;
	}
	// The cumulative travel since the press, in the PRESSED widget's local frame -- the same reading
	// UUIScrollbar takes for its handle, including the detail that a widget's local X is the engine's
	// Y (the UI plane is YZ).
	const FVector LocalDelta = EventData->PressWorldToLocalTransform.TransformVector(
		EventData->GetWorldPointInPlane() - EventData->PressWorldPoint);
	const float Fraction = SliderPressFraction + static_cast<float>(LocalDelta.Y) / ScrubWidth;
	// Through the ordinary road, which clamps to the HARD range: the scrub range decides how far the
	// travel reaches, never what the value is allowed to be.
	ApplyValueChange(SnapToStep(SliderFractionToValue(Fraction)));
	return bBubble;
}

bool UDreamSpinBox::NativeOnEndDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnEndDrag(EventData);
	if (!bSliderMoving)
	{
		return bBubble;
	}
	bSliderMoving = false;
	OnEndSliderMovement.Broadcast(Value);
	// Letting go is the COMMIT. Everything the drag passed through was OnValueChanged; this is the
	// one moment a consumer writing to a setting or a server should act on.
	CommitValue(Value);
	return bBubble;
}

void UDreamSpinBox::ApplyValueChange(float InValue)
{
	const float Clamped = FMath::Clamp(SnapToStep(InValue), MinValue, MaxValue);
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

void UDreamSpinBox::CommitValue(float InValue)
{
	ApplyValueChange(InValue);
	OnValueCommitted.Broadcast(Value);
	if (InputBehaviour == nullptr)
	{
		return;
	}
	if (bClearKeyboardFocusOnCommit)
	{
		// Without firing the behaviour's own submit again: the value has already been committed, and
		// a second submit would re-enter this function through HandleSubmitted.
		InputBehaviour->DeactivateInput(false);
	}
	else if (bSelectAllTextOnCommit)
	{
		// Ready to be typed over, which is what makes a spin box usable for a run of entries. Only
		// when focus was KEPT -- selecting the contents of a field nobody is editing shows a
		// highlight with no caret in it.
		InputBehaviour->SelectAll();
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
	// At most MaxFractionalDigits, then trailing zeros trimmed back to MinFractionalDigits -- UMG's
	// rule, and it is two knobs rather than one because the two cases it answers are opposite: a
	// currency field must show "2.50" (a MINIMUM) and a count field must not show "3.0000001" (a
	// MAXIMUM). Always '.' for the decimal point: FText::AsNumber is culture-dependent, and a value
	// the parser cannot read back is a value the field destroys on the next submit.
	const int32 MaxDigits = FMath::Clamp(MaxFractionalDigits, 0, 9);
	const int32 MinDigits = FMath::Clamp(MinFractionalDigits, 0, MaxDigits);
	FString Spelled = FString::Printf(TEXT("%.*f"), MaxDigits, Value);
	if (MaxDigits > MinDigits && Spelled.Contains(TEXT(".")))
	{
		int32 Last = Spelled.Len() - 1;
		// The floor is the point itself plus MinDigits after it, so a Min of zero can strip the point
		// as well and "3.000000" becomes "3" rather than "3.".
		const int32 Floor = Spelled.Find(TEXT(".")) + (MinDigits > 0 ? MinDigits : -1);
		while (Last > Floor && Spelled[Last] == TEXT('0'))
		{
			--Last;
		}
		if (Last >= 0 && Spelled[Last] == TEXT('.'))
		{
			--Last;
		}
		Spelled.LeftInline(Last + 1);
	}
	return Spelled;
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "SpinBox", UDreamSpinBox)
