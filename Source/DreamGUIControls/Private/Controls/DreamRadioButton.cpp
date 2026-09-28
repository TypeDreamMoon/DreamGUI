// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamRadioButton.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

void UDreamRadioButton::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Box"), BoxNode);
	OutParts.Emplace(TEXT("Dot"), DotNode);
}

void UDreamRadioButton::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The control IS the round box -- no label, no row. The text beside a radio is the consumer's
	// layout, exactly as with the check box.
	Realize(this,
		Node<UDreamRectBlock>("Box")
			.Stretch()
			// An overlay so the dot has a slot to be centred in.
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				Node<UDreamRectBlock>("Dot")
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					})));
}

void UDreamRadioButton::WireParts()
{
	// Ensure, not Get: on the template road the box is somebody's drawing, and this is what makes it
	// a radio at all.
	ToggleBehaviour = EnsureComponent<UUIToggle>(BoxNode);
	if (ToggleBehaviour == nullptr)
	{
		return;
	}
	// Before Awake reads it: WireParts runs during Initialize, Awake at begin play, so the flag set
	// here is the one the behaviour's own group search consults.
	ToggleBehaviour->SetAutoFindToggleGroupInParent(bAutoGroupWithSiblings);
	// The two transitions, deliberately on two visuals: pointed at one they overwrite each other and
	// the checked colour survives until the next hover.
	ToggleBehaviour->SetTransitionTarget(BoxNode != nullptr ? BoxNode->GetVisual() : nullptr);
	ToggleBehaviour->SetToggleTransitionTarget(DotNode != nullptr ? DotNode->GetVisual() : nullptr);
	ToggleBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamRadioButton::HandleValueChanged);
}

void UDreamRadioButton::ApplyStyle()
{
	const FDreamRadioButtonStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RadioButtonStyle);

	// The default radius is half the default box: the face is round without anyone styling it,
	// which is the one visual fact separating this control from the toggle.
	ShapeFace(BoxNode, Active.CornerRadius);
	ShapeFace(DotNode, static_cast<float>(FMath::Min(Active.DotSize.X, Active.DotSize.Y)) * 0.5f);
	SkinFace(BoxNode, Active.BoxBrush);
	SkinFace(DotNode, Active.DotBrush);

	// The control's own authored size: the box fills it; in an Auto slot the desired-size fallback
	// reads exactly this. Either brush may state its own drawn size.
	//
	// SizeControl for the CONTROL and SizeFace for the part, which is the whole distinction: SizeFace
	// re-captures the authored rect from live anchors, and a control a panel has already arranged is
	// holding layout output in those. The dot is a part and has no such history.
	SizeControl(BrushSizeOr(Active.BoxBrush, Active.BoxSize));
	SizeFace(DotNode, BrushSizeOr(Active.DotBrush, Active.DotSize));

	if (ToggleBehaviour != nullptr)
	{
		// The two spellings, made coherent before either is read: an authored .dui line writes one of
		// them raw, and everything below is pushed from the pair.
		ReconcileCheckSpellings();
		// Without notify: pushing the authored value in is not the user selecting. Value before
		// colours, so SetOnColor/SetOffColor's immediate application lands on the right state.
		ToggleBehaviour->SetIsOnWithoutNotify(CheckedState == EDreamCheckState::Checked);
		// The pointer transition tints the box; the checked one tints the dot. A selectable left
		// without explicit colours ships white -- these are never optional.
		PushSelectableState(ToggleBehaviour, Active.BoxNormal, Active.BoxHovered, Active.BoxPressed,
			Active.BoxDisabled, Active.BoxFocused, Active.TransitionDuration);
		// Which mouse buttons count at all: the behaviour answers every one unless told, and a radio
		// is told the left one alone -- SCheckBox's rule, a radio being a check box to UMG.
		ToggleBehaviour->SetAcceptedMouseButtons(AcceptedMouseButtons);
		ToggleBehaviour->SetOnColor(Active.DotChecked);
		// Forced: re-pushing the style is exactly when an equal-looking colour must land anyway, and
		// the guarded path would decline it. Same argument, same word, as the toggle's.
		PushCheckStateVisuals(true);
	}
}

bool UDreamRadioButton::GetIsOn() const
{
	// The behaviour is the truth once it exists; before that the authored value is all there is.
	return ToggleBehaviour != nullptr ? ToggleBehaviour->GetValue() : bIsOn;
}

void UDreamRadioButton::SetIsOn(bool bInIsOn)
{
	// Through the one true setter, so the two spellings cannot drift no matter which a caller speaks.
	SetCheckedState(bInIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked);
}

EDreamCheckState UDreamRadioButton::GetCheckedState() const
{
	// The behaviour is the truth for the two states it can hold; Undetermined is the control's own,
	// and while it stands the behaviour deliberately reads unchecked.
	if (ToggleBehaviour != nullptr && CheckedState != EDreamCheckState::Undetermined)
	{
		return ToggleBehaviour->GetValue() ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	}
	return CheckedState;
}

bool UDreamRadioButton::IsChecked() const
{
	return GetCheckedState() == EDreamCheckState::Checked;
}

void UDreamRadioButton::SetIsChecked(bool bInIsChecked)
{
	SetCheckedState(bInIsChecked ? EDreamCheckState::Checked : EDreamCheckState::Unchecked);
}

void UDreamRadioButton::SetCheckedState(EDreamCheckState InCheckedState)
{
	if (ToggleBehaviour == nullptr)
	{
		// Not built yet, so this is authoring rather than interaction: store both spellings coherently
		// and silently. ApplyStyle pushes them once the parts exist, and no event may fire before the
		// screen they belong to has finished building.
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
		// The behaviour underneath is two-state on purpose and stays that way -- which is also what
		// keeps the GROUP honest: an undetermined radio reads as not-selected, so it neither holds
		// the group's selection nor stops a sibling from taking it. Parked WITHOUT notify, because
		// pushing state is not the user choosing.
		const bool bWasOn = bIsOn;
		CheckedState = EDreamCheckState::Undetermined;
		bIsOn = false;
		// Visuals BEFORE the value push: the dot's colour rides the OFF colour, so any transition the
		// push starts must already aim at it.
		PushCheckStateVisuals();
		ToggleBehaviour->SetIsOnWithoutNotify(false);
		OnCheckStateChanged.Broadcast(EDreamCheckState::Undetermined);
		if (bWasOn)
		{
			// The bool projection moved too (true -> false); Unchecked -> Undetermined stays silent on
			// this spelling because false -> false is not a change.
			OnToggleChanged.Broadcast(false);
			OnValueChangedBP.Broadcast(false);
		}
		return;
	}

	const bool bTargetOn = (InCheckedState == EDreamCheckState::Checked);
	if (ToggleBehaviour->GetValue() != bTargetOn)
	{
		// Through the behaviour WITH notify -- the path a click takes, and the only path the toggle
		// GROUP hears, which is what makes a programmatic selection exclude its siblings. The change
		// comes back through HandleValueChanged, which owns the translation and both broadcasts.
		ToggleBehaviour->SetValue(bTargetOn);
		return;
	}
	if (CheckedState != InCheckedState)
	{
		// The behaviour already holds the target (Undetermined -> Unchecked: both read false), so no
		// callback is coming; translate here. The bool spelling did not move, so only the tri-state
		// event fires.
		CheckedState = InCheckedState;
		bIsOn = bTargetOn;
		PushCheckStateVisuals();
		OnCheckStateChanged.Broadcast(CheckedState);
	}
}

void UDreamRadioButton::ReconcileCheckSpellings()
{
	if (bIsOn == (CheckedState == EDreamCheckState::Checked))
	{
		// Coherent -- Undetermined counts as false, which is its bool projection.
		return;
	}
	if (CheckedState != EDreamCheckState::Unchecked)
	{
		// CheckedState says Checked or Undetermined against a disagreeing bIsOn: the tri-state
		// spelling wins.
		bIsOn = (CheckedState == EDreamCheckState::Checked);
	}
	else
	{
		// bIsOn = true against a still-default Unchecked. Indistinguishable from "only bIsOn was
		// authored" -- the compatibility path existing .dui takes -- so the bool wins.
		CheckedState = EDreamCheckState::Checked;
	}
}

void UDreamRadioButton::PushCheckStateVisuals(bool bForceOffColour)
{
	if (ToggleBehaviour == nullptr)
	{
		return;
	}
	const FDreamRadioButtonStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RadioButtonStyle);
	// While Undetermined stands the behaviour deliberately reads unchecked, so left alone the dot
	// would wear DotUnchecked -- the "I do not know" state would be indistinguishable from "no".
	// Aiming the OFF colour at DotChecked is the whole of the third state's appearance here, and it
	// is the toggle's rule with a dot where the em-dash is. Guarded by default so ordinary two-state
	// clicks never touch it: SetOffColor applies immediately while the value is off, and an unguarded
	// push would snap a running uncheck tween dead.
	const FColor DesiredOff = (CheckedState == EDreamCheckState::Undetermined)
		? Active.DotChecked
		: Active.DotUnchecked;
	if (bForceOffColour || ToggleBehaviour->GetOffColor() != DesiredOff)
	{
		ToggleBehaviour->SetOffColor(DesiredOff);
	}
}

void UDreamRadioButton::SetStyle(const FDreamRadioButtonStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

void UDreamRadioButton::SetAutoGroupWithSiblings(bool bInAutoGroupWithSiblings)
{
	bAutoGroupWithSiblings = bInAutoGroupWithSiblings;
	if (ToggleBehaviour == nullptr)
	{
		return;
	}
	ToggleBehaviour->SetAutoFindToggleGroupInParent(bInAutoGroupWithSiblings);
	if (bInAutoGroupWithSiblings && ToggleBehaviour->GetToggleGroup() == nullptr && BoxNode != nullptr)
	{
		// The search the behaviour runs at Awake, run now. Without this the switch decides nothing
		// for any radio that already exists -- which is every radio a screen builds at runtime.
		ToggleBehaviour->SetToggleGroup(BoxNode->GetComponentInParent<UUIToggleGroup>());
	}
}

void UDreamRadioButton::SetToggleGroup(UUIToggleGroup* InGroup)
{
	if (ToggleBehaviour != nullptr)
	{
		ToggleBehaviour->SetToggleGroup(InGroup);
	}
}

UUIToggleGroup* UDreamRadioButton::GetToggleGroup() const
{
	return ToggleBehaviour != nullptr ? ToggleBehaviour->GetToggleGroup() : nullptr;
}

void UDreamRadioButton::SetAcceptedMouseButtons(int32 InAcceptedMouseButtons)
{
	AcceptedMouseButtons = InAcceptedMouseButtons;
	if (ToggleBehaviour != nullptr)
	{
		// Straight onto the behaviour: it is what the next press consults, and nothing about the look
		// depends on it.
		ToggleBehaviour->SetAcceptedMouseButtons(InAcceptedMouseButtons);
	}
}

void UDreamRadioButton::HandleValueChanged(bool bInIsOn)
{
	// The user clicked it, or the group switched it off because a sibling went on. The behaviour is
	// two-state, so the translation is total -- a click while Undetermined arrives here as true and
	// becomes Checked, which is how the third state is left but never entered. Mirror both spellings
	// so the property and the behaviour never disagree, then re-broadcast: a consumer binds to this
	// control, not to a part of it.
	const EDreamCheckState OldState = CheckedState;
	CheckedState = bInIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	bIsOn = bInIsOn;
	PushCheckStateVisuals();
	if (CheckedState != OldState)
	{
		OnCheckStateChanged.Broadcast(CheckedState);
	}
	OnValueChangedBP.Broadcast(bInIsOn), OnToggleChanged.Broadcast(bInIsOn);
}

#if WITH_EDITOR
void UDreamRadioButton::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Mirror in the direction of the EDIT before the base class re-applies everything: a details panel
	// writes the property raw, and without this, unticking bIsOn on a Checked radio would lose to the
	// non-default CheckedState in ReconcileCheckSpellings and snap straight back.
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamRadioButton, bIsOn))
	{
		CheckedState = bIsOn ? EDreamCheckState::Checked : EDreamCheckState::Unchecked;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamRadioButton, CheckedState))
	{
		bIsOn = (CheckedState == EDreamCheckState::Checked);
	}
	// The base runs ApplyStyle, which pushes the now-coherent pair to the parts.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "RadioButton", UDreamRadioButton)
