// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamToggle.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIToggle.h"

void UDreamToggle::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Box"), BoxNode);
	OutParts.Emplace(TEXT("Tick"), TickNode);
	// The image mark stands in for the glyph, so a template that only draws one of the two is a
	// coherent check box -- PushCheckStateVisuals picks whichever it was given.
	OutParts.Emplace(TEXT("Mark"), MarkNode, /*bRequired*/false);
}

void UDreamToggle::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The control IS the box. No label, no row: a check box is one square and its mark, and
	// whatever text sits beside it is the consumer's layout, not this control's opinion -- UMG's
	// check box draws the same line.
	Realize(this,
		Node<UDreamRectBlock>("Box")
			.Stretch()
			// An overlay so the tick has a slot to be centred in. Without a layout container
			// a child has no slot at all, and centring would have to be spelled in anchors.
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				DreamUI::Text("Tick")
					.Visual([](UDreamText& InText)
					{
						InText.SetText(FText::AsCultureInvariant(TEXT("✓")));
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					}),
				// The image mark, the glyph's stand-in: exactly one of the two is active,
				// decided per state by whether that state's brush holds an image. Asleep by default
				// -- an imageless rect block draws a plain white square.
				Node<UDreamRectBlock>("Mark")
					.Self([](UDreamWidget& InMark)
					{
						InMark.SetWidgetActive(false);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					})));
}

void UDreamToggle::WireParts()
{
	// Ensure, not Get: on the template road the box is somebody's drawing, and this is what makes it
	// a check box at all.
	ToggleBehaviour = EnsureComponent<UUIToggle>(BoxNode);
	if (ToggleBehaviour == nullptr)
	{
		return;
	}
	// The two transitions, deliberately on two visuals. Pointed at one they would overwrite each
	// other and the checked colour would survive until the next hover.
	ToggleBehaviour->SetTransitionTarget(BoxNode != nullptr ? BoxNode->GetVisual() : nullptr);
	ToggleBehaviour->SetToggleTransitionTarget(TickNode != nullptr ? TickNode->GetVisual() : nullptr);
	ToggleBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamToggle::HandleValueChanged);
}

void UDreamToggle::ApplyStyle()
{
	// The sheet when the project has one and this instance did not opt out; the inline Style
	// otherwise. One decision at the top, so everything below is about one style, whichever it is.
	const FDreamToggleStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ToggleStyle);

	ShapeFace(BoxNode, Active.CornerRadius);
	SkinFace(BoxNode, Active.BoxBrush);
	// The control's own authored size: the box fills it, and in an Auto slot this is what the
	// desired-size fallback reads. The background brush may state its own drawn size.
	SizeFace(this, BrushSizeOr(Active.BoxBrush, Active.BoxSize));
	if (UDreamText* TickText = TickNode != nullptr ? Cast<UDreamText>(TickNode->GetVisual()) : nullptr)
	{
		// A glyph, sized by the style's tick height; its colour is the checked transition's to give.
		TickText->SetFontSize(static_cast<float>(Active.TickSize.Y));
	}
	if (ToggleBehaviour != nullptr)
	{
		ReconcileCheckSpellings();
		// Without notify: pushing the authored value in is not the user toggling it.
		PushCheckStateVisuals(false);
		ToggleBehaviour->SetIsOnWithoutNotify(CheckedState == EDreamCheckState::Checked);
		// The pointer transition tints the box; the checked one tints the tick. Which colour goes
		// where is the whole of what this control decides.
		ToggleBehaviour->SetNormalColor(Active.BoxNormal);
		ToggleBehaviour->SetHoveredColor(Active.BoxHovered);
		ToggleBehaviour->SetPressedColor(Active.BoxPressed);
		ToggleBehaviour->SetOnColor(Active.TickChecked);
		PushCheckStateVisuals(true);
	}
}

bool UDreamToggle::GetIsOn() const
{
	// The behaviour is the truth once it exists; before that the authored value is all there is.
	return ToggleBehaviour != nullptr ? ToggleBehaviour->GetValue() : bIsOn;
}

void UDreamToggle::SetIsOn(bool bInIsOn)
{
	// The compatibility spelling routes through the one true setter, so the two spellings cannot
	// drift no matter which one a caller speaks.
	SetCheckedState(bInIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked);
}

EDreamCheckState UDreamToggle::GetCheckedState() const
{
	// The behaviour is the truth for the two states it can hold, same rule as GetIsOn; Undetermined
	// is the control's own, and while it stands the behaviour deliberately reads unchecked.
	if (ToggleBehaviour != nullptr && CheckedState != EDreamCheckState::Undetermined)
	{
		return ToggleBehaviour->GetValue() ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	}
	return CheckedState;
}

bool UDreamToggle::IsChecked() const
{
	return GetCheckedState() == EDreamCheckState::Checked;
}

void UDreamToggle::SetIsChecked(bool bInIsChecked)
{
	SetCheckedState(bInIsChecked ? EDreamCheckState::Checked : EDreamCheckState::Unchecked);
}

void UDreamToggle::SetCheckedState(EDreamCheckState InCheckedState)
{
	if (ToggleBehaviour == nullptr)
	{
		// Not built yet, so this is authoring, not interaction: store both spellings coherently and
		// silently. ApplyStyle pushes them once the parts exist, and no event may fire before the
		// screen they belong to has finished building (see the push in ApplyStyle).
		CheckedState = InCheckedState;
		bIsOn = (InCheckedState == EDreamCheckState::Checked);
		return;
	}

	if (InCheckedState == EDreamCheckState::Undetermined)
	{
		if (CheckedState == EDreamCheckState::Undetermined)
		{
			return;
		}
		// The behaviour underneath is two-state on purpose and stays that way: Undetermined lives
		// on the control. The behaviour is parked at unchecked WITHOUT notify -- pushing state is
		// not the user acting -- which is also what makes the state authorable but not
		// clickable-into: the click that leaves it arrives at the behaviour as unchecked -> checked.
		const bool bWasOn = bIsOn;
		CheckedState = EDreamCheckState::Undetermined;
		bIsOn = false;
		// Visuals BEFORE the value push: the bar's colour rides the OFF colour, so any transition
		// the push starts must already aim at it. Coming from Checked the tick wears TickChecked
		// already; coming from Unchecked the retarget lands immediately (SetOffColor applies at
		// once while the value is off). Either way the bar is never seen in TickUnchecked.
		PushCheckStateVisuals();
		ToggleBehaviour->SetIsOnWithoutNotify(false);
		OnCheckStateChanged.Broadcast(EDreamCheckState::Undetermined);
		if (bWasOn)
		{
			// The bool projection moved too (true -> false); Unchecked -> Undetermined stays silent
			// on this spelling because false -> false is not a change.
			OnToggleChanged.Broadcast(false);
		}
		return;
	}

	const bool bTargetOn = (InCheckedState == EDreamCheckState::Checked);
	if (ToggleBehaviour->GetValue() != bTargetOn)
	{
		// Through the behaviour WITH notify -- the path a click takes and the path SetIsOn has
		// always taken. The change comes back through HandleValueChanged, which owns the
		// translation, the glyph and both broadcasts.
		ToggleBehaviour->SetValue(bTargetOn);
		return;
	}
	if (CheckedState != InCheckedState)
	{
		// The behaviour already holds the target (Undetermined -> Unchecked: both read false), so
		// no callback is coming; translate here. The bool spelling did not move, so only the
		// tri-state event fires.
		CheckedState = InCheckedState;
		bIsOn = bTargetOn;
		PushCheckStateVisuals();
		OnCheckStateChanged.Broadcast(CheckedState);
	}
}

void UDreamToggle::HandleValueChanged(bool bInIsOn)
{
	// The behaviour spoke: the user clicked, or code drove it directly. The behaviour is two-state,
	// so the translation is total -- a click while Undetermined lands here as true and becomes
	// Checked, glyph restored with it (Undetermined is authorable, never clicked into). Mirror both
	// spellings so property and behaviour never disagree, then re-broadcast: a consumer binds to
	// this control, not to a part of it.
	const EDreamCheckState OldState = CheckedState;
	CheckedState = bInIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	bIsOn = bInIsOn;
	PushCheckStateVisuals();
	if (CheckedState != OldState)
	{
		OnCheckStateChanged.Broadcast(CheckedState);
	}
	OnToggleChanged.Broadcast(bInIsOn);
	OnValueChangedBP.Broadcast(bInIsOn);
}

void UDreamToggle::ReconcileCheckSpellings()
{
	// Every setter keeps the pair coherent, so a disagreement here is always a raw write: an
	// authored .dui value, a details-panel edit (already mirrored edit-wards in
	// PostEditChangeProperty before this runs), or a direct C++ member write.
	if (bIsOn == (CheckedState == EDreamCheckState::Checked))
	{
		// Coherent -- Undetermined counts as false, which is its bool projection.
		return;
	}
	if (CheckedState != EDreamCheckState::Unchecked)
	{
		// CheckedState says Checked or Undetermined against a disagreeing bIsOn: the tri-state
		// spelling wins, as the class comment promises.
		bIsOn = (CheckedState == EDreamCheckState::Checked);
	}
	else
	{
		// bIsOn = true against a still-default Unchecked. Indistinguishable from "only bIsOn was
		// authored" -- the compatibility path existing .dui takes -- so the bool wins. This is the
		// one corner where an EXPLICITLY authored Unchecked loses, and it loses only to bIsOn=true.
		CheckedState = EDreamCheckState::Checked;
	}
}

void UDreamToggle::PushCheckStateVisuals(bool bForceOffColour)
{
	const bool bUndetermined = (CheckedState == EDreamCheckState::Undetermined);
	const FDreamToggleStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ToggleStyle);

	// Which face the mark wears is decided per STATE: a state whose brush holds an image draws the
	// image (the UMG arrangement -- Checked/Unchecked/Undetermined each their own picture); a state
	// whose brush is empty keeps the glyph. The two nodes are exclusive stand-ins for each other.
	const FDreamUIFaceBrush& StateBrush = bUndetermined
		? Active.UndeterminedBrush
		: (CheckedState == EDreamCheckState::Checked ? Active.CheckedBrush : Active.UncheckedBrush);
	const bool bImageMark = StateBrush.Image != nullptr;
	if (MarkNode != nullptr)
	{
		MarkNode->SetWidgetActive(bImageMark);
		if (bImageMark)
		{
			SkinFace(MarkNode, StateBrush);
			SizeFace(MarkNode, BrushSizeOr(StateBrush, Active.TickSize));
		}
	}
	if (TickNode != nullptr)
	{
		TickNode->SetWidgetActive(!bImageMark);
	}

	// The glyph is where the third state shows: an em-dash bar (U+2014) instead of the check mark
	// (U+2713, the one the builder starts the tick with). Literals, same as the builder's -- this
	// file is already UTF-8 with a literal check mark in it. Culture-invariant because they are
	// glyphs, not words. Written even while the image stands in, so a style edit that empties the
	// brush finds the glyph already correct.
	if (UDreamText* TickText = TickNode != nullptr ? Cast<UDreamText>(TickNode->GetVisual()) : nullptr)
	{
		TickText->SetText(FText::AsCultureInvariant(bUndetermined ? TEXT("—") : TEXT("✓")));
	}

	if (ToggleBehaviour != nullptr)
	{
		// The tick's colour is the checked transition's to give, and while Undetermined the
		// behaviour deliberately reads unchecked -- left alone, the bar would wear TickUnchecked.
		// Aim the OFF colour at TickChecked for exactly as long as the state stands (UMG's
		// undetermined glyph wears the checked foreground the same way). Guarded by default, so
		// ordinary two-state clicks never touch it: SetOffColor applies immediately while the value
		// is off, and an unguarded push would snap the uncheck tween dead. ApplyStyle forces it --
		// re-pushing the style is exactly when an equal-looking colour must land anyway (see there).
		const FColor DesiredOff = bUndetermined ? Active.TickChecked : Active.TickUnchecked;
		if (bForceOffColour || ToggleBehaviour->GetOffColor() != DesiredOff)
		{
			ToggleBehaviour->SetOffColor(DesiredOff);
		}
	}
}

#if WITH_EDITOR
void UDreamToggle::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Mirror in the direction of the EDIT before the base class re-applies everything: a details
	// panel writes the property raw, and without this, unchecking bIsOn on a Checked toggle would
	// lose to the non-default CheckedState in ReconcileCheckSpellings and snap straight back.
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamToggle, bIsOn))
	{
		CheckedState = bIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamToggle, CheckedState))
	{
		bIsOn = (CheckedState == EDreamCheckState::Checked);
	}
	// The base runs ApplyStyle, which pushes the now-coherent pair to the parts.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
