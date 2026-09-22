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
#include "Core/DreamUIFontData_BaseObject.h"
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
			if (Active.Font != nullptr)
			{
				// Only when the style NAMES one. Pushing null would take the project's default font
				// away from every spin box that has never stated one, which is all of them. The two
				// step glyphs take it as well: a minus sign in a different typeface from the number
				// beside it is the one thing a shared style must not produce.
				GlyphVisual->SetFont(Active.Font);
			}
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

	if (InputBehaviour != nullptr)
	{
		// The typed-entry half of a spin box is a text field, and the two knobs it shares with one
		// are pushed the same way everything else here is.
		InputBehaviour->SetKeyboardType(KeyboardType);
		InputBehaviour->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
	}
	// Where the number sits in the field. Written on every push rather than only at build, for the
	// template road's reason: a node somebody else's tree supplied carries the library's defaults.
	if (UDreamText* ValueVisual = ValueTextNode != nullptr ? Cast<UDreamText>(ValueTextNode->GetVisual()) : nullptr)
	{
		ValueVisual->SetParagraphHorizontalAlignment(Justification);
	}

	// The authored value, clamped against the authored range and mirrored, so the property and the
	// field never show two different numbers. Eventless: pushing authored state is not a change.
	Value = FMath::Clamp(Value, GetMinValue(), GetMaxValue());
	PushValueToParts();

	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SizeControlHeight(Active.Height);
	PushMinDesiredWidth();
}

void UDreamSpinBox::PushMinDesiredWidth()
{
	if (MinDesiredWidth <= 0.0f)
	{
		// No opinion, which is every spin box that existed before this knob.
		return;
	}
	if (UDreamText* ValueVisual = ValueTextNode != nullptr ? Cast<UDreamText>(ValueTextNode->GetVisual()) : nullptr)
	{
		ValueVisual->SetMinDesiredWidth(MinDesiredWidth);
	}
	if (GetWidth() < MinDesiredWidth)
	{
		// A FLOOR, never a size: a box already wider keeps the width whoever placed it gave it.
		SetWidth(MinDesiredWidth);
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}
}

void UDreamSpinBox::SetStyle(const FDreamSpinBoxStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

float UDreamSpinBox::GetMinValue() const
{
	// Float's own floor when there is no minimum, because that is what every caller of this does
	// with the answer: clamp against it. A stored number returned for a bound that is switched off
	// would clamp to a limit the author explicitly removed.
	return bOverride_MinValue ? MinValue : TNumericLimits<float>::Lowest();
}

float UDreamSpinBox::GetMaxValue() const
{
	return bOverride_MaxValue ? MaxValue : TNumericLimits<float>::Max();
}

void UDreamSpinBox::SetMinValue(float InMinValue)
{
	bOverride_MinValue = true;
	MinValue = InMinValue;
	// Through the ordinary road, so a value that has just become illegal is clamped and reported --
	// a range that moved under a value and left it outside is the bug this prevents.
	ApplyValueChange(Value);
}

void UDreamSpinBox::ClearMinValue()
{
	// No clamp afterwards: taking a bound away can only ever widen what is legal, so the value the
	// control is holding was legal before and still is.
	bOverride_MinValue = false;
}

void UDreamSpinBox::SetMaxValue(float InMaxValue)
{
	bOverride_MaxValue = true;
	MaxValue = InMaxValue;
	ApplyValueChange(Value);
}

void UDreamSpinBox::ClearMaxValue()
{
	bOverride_MaxValue = false;
}

float UDreamSpinBox::GetMinSliderValue() const
{
	return bOverride_MinSliderValue ? MinSliderValue : TNumericLimits<float>::Lowest();
}

float UDreamSpinBox::GetMaxSliderValue() const
{
	return bOverride_MaxSliderValue ? MaxSliderValue : TNumericLimits<float>::Max();
}

void UDreamSpinBox::SetMinSliderValue(float InMinSliderValue)
{
	bOverride_MinSliderValue = true;
	MinSliderValue = InMinSliderValue;
}

void UDreamSpinBox::ClearMinSliderValue()
{
	bOverride_MinSliderValue = false;
}

void UDreamSpinBox::SetMaxSliderValue(float InMaxSliderValue)
{
	bOverride_MaxSliderValue = true;
	MaxSliderValue = InMaxSliderValue;
}

void UDreamSpinBox::ClearMaxSliderValue()
{
	bOverride_MaxSliderValue = false;
}

bool UDreamSpinBox::HasFiniteSliderRange() const
{
	// An open end is asked for by name rather than left to the arithmetic. With BOTH ends open the
	// span is float's floor to float's ceiling and overflows to infinity, which a finiteness test
	// catches -- but with ONE end open it is merely 3e38, perfectly finite and perfectly useless: a
	// drag across the whole control would move the value by a number with thirty-eight digits.
	const bool bHasBottom = bOverride_MinSliderValue || bOverride_MinValue;
	const bool bHasTop = bOverride_MaxSliderValue || bOverride_MaxValue;
	if (!bHasBottom || !bHasTop)
	{
		return false;
	}
	// Everything downstream of a scrub divides by this number, so a degenerate span is a NaN the
	// value never comes back from.
	const float Span = GetSliderMaxValue() - GetSliderMinValue();
	return FMath::IsFinite(Span) && FMath::Abs(Span) > KINDA_SMALL_NUMBER;
}

void UDreamSpinBox::SetStepSize(float InStepSize)
{
	StepSize = InStepSize;
	if (bAlwaysUsesDeltaSnap)
	{
		// The step IS the snap grid while that is on, so a new step has to re-snap what is showing.
		ApplyValueChange(Value);
	}
}

void UDreamSpinBox::SetSliderExponent(float InSliderExponent)
{
	// Only read while a scrub is in flight, so there is nothing to re-push; clamped away from zero
	// for the same reason the scrub arithmetic clamps it.
	SliderExponent = FMath::Max(InSliderExponent, 0.01f);
}

void UDreamSpinBox::SetEnableSlider(bool bInEnableSlider)
{
	bEnableSlider = bInEnableSlider;
	if (!bEnableSlider && bSliderMoving)
	{
		// A scrub in flight when the feature is switched off has to end, or the control stays in a
		// gesture nothing will ever finish -- and the end of a scrub is a commit, so it is reported.
		bSliderMoving = false;
		OnEndSliderMovement.Broadcast(Value);
		CommitValue(Value);
	}
}

void UDreamSpinBox::SetAlwaysUsesDeltaSnap(bool bInAlwaysUsesDeltaSnap)
{
	bAlwaysUsesDeltaSnap = bInAlwaysUsesDeltaSnap;
	if (bAlwaysUsesDeltaSnap)
	{
		// Turning it on applies it to the value already held. A snap that only bound future entries
		// would leave the box showing a number it says cannot exist.
		ApplyValueChange(Value);
	}
}

void UDreamSpinBox::SetMinFractionalDigits(int32 InMinFractionalDigits)
{
	MinFractionalDigits = FMath::Clamp(InMinFractionalDigits, 0, 9);
	// Eventless: how a number is SPELLED is not the number changing.
	PushValueToParts();
}

void UDreamSpinBox::SetMaxFractionalDigits(int32 InMaxFractionalDigits)
{
	MaxFractionalDigits = FMath::Clamp(InMaxFractionalDigits, 0, 9);
	PushValueToParts();
}

void UDreamSpinBox::SetClearKeyboardFocusOnCommit(bool bInClearKeyboardFocusOnCommit)
{
	// Read by CommitValue, so there is nothing to push: the next commit asks.
	bClearKeyboardFocusOnCommit = bInClearKeyboardFocusOnCommit;
}

void UDreamSpinBox::SetSelectAllTextOnCommit(bool bInSelectAllTextOnCommit)
{
	bSelectAllTextOnCommit = bInSelectAllTextOnCommit;
}

void UDreamSpinBox::SetJustification(EDreamUITextParagraphHorizontalAlign InJustification)
{
	Justification = InJustification;
	if (UDreamText* ValueVisual = ValueTextNode != nullptr ? Cast<UDreamText>(ValueTextNode->GetVisual()) : nullptr)
	{
		ValueVisual->SetParagraphHorizontalAlignment(InJustification);
	}
}

void UDreamSpinBox::SetMinDesiredWidth(float InMinDesiredWidth)
{
	MinDesiredWidth = FMath::Max(0.0f, InMinDesiredWidth);
	PushMinDesiredWidth();
}

void UDreamSpinBox::SetFont(UDreamUIFontData_BaseObject* InFont)
{
	Style.Font = InFont;
	// The whole push: a typeface is part of the look, and it has to come back out of ResolveStyle
	// with everything else or a box on the project sheet would mix the two sources.
	ApplyStyle();
}

void UDreamSpinBox::SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> InKeyboardType)
{
	KeyboardType = InKeyboardType;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetKeyboardType(InKeyboardType);
	}
}

void UDreamSpinBox::SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction InDismissAction)
{
	VirtualKeyboardDismissAction = InDismissAction;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetVirtualKeyboardDismissAction(InDismissAction);
	}
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
	// The VALUE range when the scrub does not state its own -- which is now a range that may have no
	// bottom at all, and HasFiniteSliderRange is what keeps that out of the scrub arithmetic.
	return bOverride_MinSliderValue ? MinSliderValue : GetMinValue();
}

float UDreamSpinBox::GetSliderMaxValue() const
{
	return bOverride_MaxSliderValue ? MaxSliderValue : GetMaxValue();
}

float UDreamSpinBox::SnapToStep(float InValue) const
{
	if (!bAlwaysUsesDeltaSnap || StepSize <= KINDA_SMALL_NUMBER)
	{
		return InValue;
	}
	// Measured FROM MinValue rather than from zero, for the reason the slider's mouse step is: a
	// range of 3..10 snapped by 2 must offer the ends the author stated, not 4, 6, 8, 10.
	// With no bottom stated there is no such end to honour, and zero is the only origin a snap can
	// mean then -- float's floor as an origin would put the grid nowhere near the value.
	const float SnapOrigin = bOverride_MinValue ? MinValue : 0.0f;
	return SnapOrigin + FMath::RoundToFloat((InValue - SnapOrigin) / StepSize) * StepSize;
}

float UDreamSpinBox::ValueToSliderFraction(float InValue) const
{
	const float SliderMin = GetSliderMinValue();
	const float SliderMax = GetSliderMaxValue();
	const float Span = SliderMax - SliderMin;
	if (!FMath::IsFinite(Span) || FMath::Abs(Span) <= KINDA_SMALL_NUMBER)
	{
		// An empty scrub range is a scrub that can go nowhere -- and, unguarded, a division that
		// hands a NaN to the drag arithmetic the way the slider's range once did. An OPEN range
		// (a value bound switched off) is the same problem from the other end: its span overflows
		// to infinity, and infinity times a fraction is a NaN just the same.
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
		|| EventData->InputType != EDreamUIPointerInputType::Pointer
		// An open range has no travel to sweep: a value that may be anything cannot be mapped onto
		// two hundred pixels, and pretending otherwise is where the NaN comes from. Typing still
		// works, which is the only way such a value was ever going to be entered.
		|| !HasFiniteSliderRange())
	{
		return bBubble;
	}
	bSliderMoving = true;
	// Where the value already is, in the scrub's own coordinates. Every drag frame is an OFFSET from
	// this rather than an absolute read of the pointer, which is what keeps the number from jumping
	// to wherever in the field the drag happened to start.
	SliderPressFraction = ValueToSliderFraction(Value);
	// And the travel that turned the press into a drag, which moves nothing: SSpinBox::OnMouseMove adds
	// the pointer's moves up only to decide that this IS a drag, and the move that decides it changes
	// no value -- only the ones after it do. Measured here, at the moment the drag is recognised, so
	// the scrub counts from this point and the number does not leap by the drag threshold as it starts.
	ScrubStartTravel = static_cast<float>(EventData->PressWorldToLocalTransform.TransformVector(
		EventData->GetWorldPointInPlane() - EventData->PressWorldPoint).Y);
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
	// Y (the UI plane is YZ) -- less the stretch that only decided this was a drag (ScrubStartTravel).
	const FVector LocalDelta = EventData->PressWorldToLocalTransform.TransformVector(
		EventData->GetWorldPointInPlane() - EventData->PressWorldPoint);
	const float Fraction = SliderPressFraction + (static_cast<float>(LocalDelta.Y) - ScrubStartTravel) / ScrubWidth;
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
	const float Clamped = FMath::Clamp(SnapToStep(InValue), GetMinValue(), GetMaxValue());
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
