// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTextInput.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
//the style may name a typeface; the paragraph is what loads it
#include "Core/DreamUIFontData_BaseObject.h"
#include "Interaction/UITextInput.h"
#include "Materials/MaterialInterface.h"

void UDreamTextInput::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Field"), BackgroundNode);
	OutParts.Emplace(TEXT("Placeholder"), PlaceholderNode);
	OutParts.Emplace(TEXT("ClipArea"), ClipNode);
	OutParts.Emplace(TEXT("Text"), TextNode);
	// Optional: a template written before the field could report an error has no node by this name,
	// and a required part would turn an addition into a warning on every asset already out there.
	OutParts.Emplace(TEXT("Error"), ErrorNode, false);
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
						DreamUI::Text("Text").Stretch()),
				// The error message, over the field's right end -- where SEditableTextBox packs its
				// error-reporting widget, and the one place in a one-line box that is not where the
				// player is typing. Asleep from the start: a field with nothing wrong with it must
				// not reserve a single pixel for the possibility.
				DreamUI::Text("Error")
					.Stretch()
					.Visual([](UDreamText& InError)
					{
						InError.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Right);
						InError.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Self([](UDreamWidget& InError)
					{
						InError.SetWidgetActive(false);
					})));
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
	// The error sits in the same inset box as the text it is complaining about, so its right edge is
	// the field's right edge minus the padding rather than the raw rect.
	Inset(ErrorNode);

	auto StyleText = [this, &Active](UDreamWidget* InNode, const FColor& InColor, EDreamUITextParagraphHorizontalAlign InAlign)
	{
		if (UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			TextVisual->SetColor(InColor);
			TextVisual->SetFontSize(Active.FontSize);
			if (Active.Font != nullptr)
			{
				// Only when the style NAMES one. Pushing null would take the project's default font
				// away from every field that has never stated one, which is all of them.
				TextVisual->SetFont(Active.Font);
			}
			TextVisual->SetParagraphHorizontalAlignment(InAlign);
			TextVisual->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
			// Which way the paragraph reads. Auto is the default and asks the bidi algorithm, which is
			// what the field always did; the other two state it outright for a screen that knows.
			TextVisual->SetFlowDirection(TextFlowDirection);
		}
	};
	// The placeholder takes the SAME alignment as the typed text: a hint that sat at the other end of
	// the box would be a hint about a different field than the one about to be typed into.
	StyleText(TextNode, Active.TextColor, Justification);
	StyleText(PlaceholderNode, Active.PlaceholderColor, Justification);
	// The error keeps the right end whatever the value does -- it is not part of the value, and a
	// message that moved under the text it annotates would collide with it on a full field.
	StyleText(ErrorNode, Active.ErrorColor, EDreamUITextParagraphHorizontalAlign::Right);
	PushError();

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
		// The caret and the selection highlight, which the sheet now describes. Left alone they came
		// from the behaviour's own defaults and had no way to follow a theme -- a caret four values
		// away from this style's own Background.
		InputBehaviour->SetCaretColor(Active.CaretColor);
		InputBehaviour->SetSelectionColor(Active.SelectionColor);
		InputBehaviour->SetCaretWidth(Active.CaretWidth);
		InputBehaviour->SetCaretBlinkRate(Active.CaretBlinkRate);
		// The behaviour's own settings, authored on the control. Order matters for exactly one pair:
		// the validation rules go in before the text does, or the text is checked against the rules
		// the field had a moment ago.
		InputBehaviour->SetPasswordChar(PasswordChar);
		InputBehaviour->SetDisplayType(DisplayType);
		InputBehaviour->SetCustomValidation(CustomValidation);
		InputBehaviour->SetInputType(InputType);
		InputBehaviour->SetMaxLength(MaxLength);
		InputBehaviour->SetReadOnly(bReadOnly);
		InputBehaviour->SetIgnoreKeys(IgnoreKeys);
		InputBehaviour->SetMultiLineSubmitFunctionKeys(MultiLineSubmitFunctionKeys);
		InputBehaviour->SetSelectAllWhenActivateInput(bSelectAllWhenActivateInput);
		InputBehaviour->SetAutoActivateInputWhenNavigateIn(bAutoActivateInputWhenNavigateIn);
		InputBehaviour->SetSubmitWhenDeactivate(bSubmitWhenDeactivate);
		InputBehaviour->SetAllowContextMenu(bAllowContextMenu);
		InputBehaviour->SetRevertTextOnEscape(bRevertTextOnEscape);
		InputBehaviour->SetClearKeyboardFocusOnCommit(bClearKeyboardFocusOnCommit);
		InputBehaviour->SetSelectAllTextOnCommit(bSelectAllTextOnCommit);
		InputBehaviour->SetIsCaretMovedWhenGainFocus(bIsCaretMovedWhenGainFocus);
		// What the value looks like while nobody is editing it. Pushed after SetAllowMultiLine, which
		// is the other half of the same one field on the paragraph.
		InputBehaviour->SetOverflowPolicy(OverflowPolicy);
		InputBehaviour->SetKeyboardType(KeyboardType);
		InputBehaviour->SetVirtualKeyboardTrigger(VirtualKeyboardTrigger);
		InputBehaviour->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
		InputBehaviour->SetVirtualKeyboardOptions(VirtualKeyboardOptions);
		// Without an event: pushing the authored text in is not the user typing.
		InputBehaviour->SetTextWithoutNotify(Text);
	}
	SizeControlHeight(Active.Height);
	PushMinimumDesiredWidth();
}

void UDreamTextInput::PushMinimumDesiredWidth()
{
	if (MinimumDesiredWidth <= 0.0f)
	{
		// No opinion, which is every field that existed before this knob -- and the reason the whole
		// thing is a no-op by default rather than a width somebody has to fight.
		return;
	}
	// Onto the TEXT as well as the control, because the two are measured by different consumers: an
	// Auto slot reads the control's own rect (SizeControlHeight syncs the snapshot it takes), while a
	// content-sized parent that measures the paragraph reads UDreamText::MinDesiredWidth.
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetMinDesiredWidth(MinimumDesiredWidth);
	}
	if (GetWidth() < MinimumDesiredWidth)
	{
		// A FLOOR, never a size: a field already wider than its minimum keeps the width whoever
		// placed it gave it, which is the half of "minimum" that a plain SetWidth would throw away.
		SetWidth(MinimumDesiredWidth);
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}
}

void UDreamTextInput::PushError()
{
	if (ErrorNode == nullptr)
	{
		// A template with no error node. Every other writer here null-checks for the same reason, and
		// a field that cannot SHOW an error still knows it has one (HasError reads the text).
		return;
	}
	const bool bHasError = !ErrorText.IsEmpty();
	if (UDreamText* ErrorVisual = Cast<UDreamText>(ErrorNode->GetVisual()))
	{
		ErrorVisual->SetText(ErrorText);
	}
	// Asleep rather than transparent: an invisible paragraph still lays out, and a field with nothing
	// wrong with it should cost exactly what it did before the error state existed.
	ErrorNode->SetWidgetActive(bHasError);
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

void UDreamTextInput::SetPlaceholder(const FText& InPlaceholder)
{
	Placeholder = InPlaceholder;
	// The whole style push, because the placeholder is not merely a string on a visual: whether it is
	// AWAKE is decided against the field's emptiness in the same pass that writes it.
	ApplyStyle();
}

void UDreamTextInput::SetMultiLine(bool bInMultiLine)
{
	if (bMultiLine == bInMultiLine)
	{
		return;
	}
	bMultiLine = bInMultiLine;
	// The line count decides the field's height and what its clip must allow, both written in the
	// style push -- so pushing the behaviour's flag alone would leave a multi-line field one line tall.
	ApplyStyle();
}

void UDreamTextInput::SetStyle(const FDreamTextInputStyle& InStyle)
{
	Style = InStyle;
	// The whole push. A style is one decision and every field in it is read by the same pass, so
	// there is nothing finer to re-push than "the look".
	ApplyStyle();
}

/*
 * The behaviour-knob setters.
 *
 * Every one of them is "write the field, then hand it to the behaviour", and they are written out
 * rather than generated because the SECOND half differs: some of the behaviour's setters re-validate
 * the text, one of them ends a live edit, and a macro that hid that would hide the only interesting
 * part. The null check is not defensive -- a control whose parts a template did not supply has no
 * behaviour at all, and the authored value still has to stick so the next ApplyStyle can push it.
 */

void UDreamTextInput::SetInputType(EUITextInputType InInputType)
{
	InputType = InInputType;
	if (InputBehaviour != nullptr)
	{
		// Which re-runs the rules over what the field already holds: a field switched to
		// IntegerNumber while it spells "12.5" must not keep spelling it.
		InputBehaviour->SetInputType(InInputType);
	}
}

void UDreamTextInput::SetCustomValidation(UDreamTextInputCustomValidation* InCustomValidation)
{
	CustomValidation = InCustomValidation;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetCustomValidation(InCustomValidation);
	}
}

void UDreamTextInput::SetDisplayType(EUITextInputDisplayType InDisplayType)
{
	DisplayType = InDisplayType;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetDisplayType(InDisplayType);
	}
}

void UDreamTextInput::SetIsPassword(bool bInIsPassword)
{
	// UMG's boolean over this library's two-valued enum. One writer, so the two spellings cannot come
	// to disagree about whether the field is masked.
	SetDisplayType(bInIsPassword ? EUITextInputDisplayType::Password : EUITextInputDisplayType::Standard);
}

void UDreamTextInput::SetPasswordChar(const FString& InPasswordChar)
{
	PasswordChar = InPasswordChar;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetPasswordChar(InPasswordChar);
	}
}

void UDreamTextInput::SetIsReadOnly(bool bInReadOnly)
{
	bReadOnly = bInReadOnly;
	if (InputBehaviour != nullptr)
	{
		// The behaviour ends a live edit on the way in: a field that went read-only under the
		// player's caret kept blinking and swallowed the next keystroke.
		InputBehaviour->SetReadOnly(bInReadOnly);
	}
}

void UDreamTextInput::SetMaxLength(int32 InMaxLength)
{
	MaxLength = FMath::Max(0, InMaxLength);
	if (InputBehaviour != nullptr)
	{
		// Which truncates what is already there. A cap that only applied to future typing would let a
		// screen ship a value longer than the field claims to accept.
		InputBehaviour->SetMaxLength(MaxLength);
	}
}

void UDreamTextInput::SetIgnoreKeys(const TArray<FKey>& InIgnoreKeys)
{
	IgnoreKeys = InIgnoreKeys;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetIgnoreKeys(InIgnoreKeys);
	}
}

void UDreamTextInput::SetMultiLineSubmitFunctionKeys(const TArray<FKey>& InKeys)
{
	MultiLineSubmitFunctionKeys = InKeys;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetMultiLineSubmitFunctionKeys(InKeys);
	}
}

void UDreamTextInput::SetSelectAllWhenActivateInput(bool bInSelectAll)
{
	bSelectAllWhenActivateInput = bInSelectAll;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetSelectAllWhenActivateInput(bInSelectAll);
	}
}

void UDreamTextInput::SetAutoActivateInputWhenNavigateIn(bool bInAutoActivate)
{
	bAutoActivateInputWhenNavigateIn = bInAutoActivate;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetAutoActivateInputWhenNavigateIn(bInAutoActivate);
	}
}

void UDreamTextInput::SetSubmitWhenDeactivate(bool bInSubmitWhenDeactivate)
{
	bSubmitWhenDeactivate = bInSubmitWhenDeactivate;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetSubmitWhenDeactivate(bInSubmitWhenDeactivate);
	}
}

void UDreamTextInput::SetAllowContextMenu(bool bInAllowContextMenu)
{
	bAllowContextMenu = bInAllowContextMenu;
	if (InputBehaviour != nullptr)
	{
		// Which closes a menu that is already open: turning the knob off with one on screen must not
		// grandfather it in.
		InputBehaviour->SetAllowContextMenu(bInAllowContextMenu);
	}
}

void UDreamTextInput::SetRevertTextOnEscape(bool bInRevertTextOnEscape)
{
	bRevertTextOnEscape = bInRevertTextOnEscape;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetRevertTextOnEscape(bInRevertTextOnEscape);
	}
}

void UDreamTextInput::SetClearKeyboardFocusOnCommit(bool bInClearKeyboardFocusOnCommit)
{
	bClearKeyboardFocusOnCommit = bInClearKeyboardFocusOnCommit;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetClearKeyboardFocusOnCommit(bInClearKeyboardFocusOnCommit);
	}
}

void UDreamTextInput::SetSelectAllTextOnCommit(bool bInSelectAllTextOnCommit)
{
	bSelectAllTextOnCommit = bInSelectAllTextOnCommit;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetSelectAllTextOnCommit(bInSelectAllTextOnCommit);
	}
}

void UDreamTextInput::SetIsCaretMovedWhenGainFocus(bool bInIsCaretMovedWhenGainFocus)
{
	bIsCaretMovedWhenGainFocus = bInIsCaretMovedWhenGainFocus;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetIsCaretMovedWhenGainFocus(bInIsCaretMovedWhenGainFocus);
	}
}

void UDreamTextInput::SetJustification(EDreamUITextParagraphHorizontalAlign InJustification)
{
	Justification = InJustification;
	// Both paragraphs, in one place, because "where the hint sits" is not a second decision from
	// "where the value sits" -- see the style push.
	auto Align = [InJustification](UDreamWidget* InNode)
	{
		if (UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			TextVisual->SetParagraphHorizontalAlignment(InJustification);
		}
	};
	Align(TextNode);
	Align(PlaceholderNode);
}

void UDreamTextInput::SetTextFlowDirection(EDreamTextFlowDirection InFlowDirection)
{
	TextFlowDirection = InFlowDirection;
	// Onto BOTH paragraphs: a hint that read the other way round from the value it stands in for
	// would sit at the opposite end of the box the moment the field was empty.
	auto Push = [InFlowDirection](UDreamWidget* InNode)
	{
		if (UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			TextVisual->SetFlowDirection(InFlowDirection);
		}
	};
	Push(TextNode);
	Push(PlaceholderNode);
	Push(ErrorNode);
}

void UDreamTextInput::SetTextOverflowPolicy(ETextOverflowPolicy InOverflowPolicy)
{
	OverflowPolicy = InOverflowPolicy;
	if (InputBehaviour != nullptr)
	{
		// The behaviour owns the push, because the paragraph's overflow type is shared with the line
		// mode and there is exactly one place that knows which of the two is speaking right now.
		InputBehaviour->SetOverflowPolicy(InOverflowPolicy);
	}
}

void UDreamTextInput::SetFont(UDreamUIFontData_BaseObject* InFont)
{
	Style.Font = InFont;
	// The whole push, because a typeface is a piece of the LOOK: it has to come back out of
	// ResolveStyle with everything else, or a field on the project sheet would take a font from the
	// inline struct while taking its colours from the sheet.
	ApplyStyle();
}

UMaterialInterface* UDreamTextInput::GetFontMaterial() const
{
	const UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr;
	return TextVisual != nullptr ? TextVisual->GetOverrideMaterial() : nullptr;
}

void UDreamTextInput::SetFontMaterial(UMaterialInterface* InMaterial)
{
	// Straight to the paragraph rather than into the style, and only the VALUE's paragraph: the
	// placeholder and the error message are not the text this is about, and a material meant for a
	// password field's glyphs has no business on the words "Enter your name".
	if (UDreamText* TextVisual = TextNode != nullptr ? Cast<UDreamText>(TextNode->GetVisual()) : nullptr)
	{
		TextVisual->SetOverrideMaterial(InMaterial);
	}
}

void UDreamTextInput::SetMinimumDesiredWidth(float InMinimumDesiredWidth)
{
	MinimumDesiredWidth = FMath::Max(0.0f, InMinimumDesiredWidth);
	PushMinimumDesiredWidth();
}

void UDreamTextInput::SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> InKeyboardType)
{
	KeyboardType = InKeyboardType;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetKeyboardType(InKeyboardType);
	}
}

void UDreamTextInput::SetVirtualKeyboardTrigger(EVirtualKeyboardTrigger InTrigger)
{
	VirtualKeyboardTrigger = InTrigger;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetVirtualKeyboardTrigger(InTrigger);
	}
}

void UDreamTextInput::SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction InDismissAction)
{
	VirtualKeyboardDismissAction = InDismissAction;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetVirtualKeyboardDismissAction(InDismissAction);
	}
}

void UDreamTextInput::SetVirtualKeyboardOptions(const FVirtualKeyboardOptions& InOptions)
{
	VirtualKeyboardOptions = InOptions;
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SetVirtualKeyboardOptions(InOptions);
	}
}

void UDreamTextInput::SetError(const FText& InError)
{
	ErrorText = InError;
	// Only the message moved, so only the message is re-pushed: a whole style push to show one
	// sentence would re-derive every colour, every behaviour knob and the text itself.
	PushError();
}

void UDreamTextInput::ClearError()
{
	SetError(FText::GetEmpty());
}

void UDreamTextInput::SelectAllText()
{
	if (InputBehaviour != nullptr)
	{
		InputBehaviour->SelectAll();
	}
}

bool UDreamTextInput::IsAnyTextSelected() const
{
	return InputBehaviour != nullptr && InputBehaviour->IsAnyTextSelected();
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
