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

void UDreamRadioButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// The control IS the round box -- no label, no row. The text beside a radio is the consumer's
	// layout, exactly as with the check box.
	Realize(this,
		Node<UDreamRectBlock>("Box").Out(BoxNode)
			.Stretch()
			// An overlay so the dot has a slot to be centred in.
			.With<UDreamLayoutContainerOverlay>()
			.With<UUIToggle>()
			.Children(
				Node<UDreamRectBlock>("Dot").Out(DotNode)
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					}))
			.Then([this](UDreamWidget& InRoot)
			{
				ToggleBehaviour = InRoot.GetComponent<UUIToggle>();
				if (ToggleBehaviour == nullptr)
				{
					return;
				}
				// Before Awake reads it: NativeOnInitialized runs during Initialize, Awake at begin
				// play, so the flag set here is the one the behaviour's own group search consults.
				ToggleBehaviour->SetAutoFindToggleGroupInParent(bAutoGroupWithSiblings);
				// The two transitions, deliberately on two visuals: pointed at one they overwrite
				// each other and the checked colour survives until the next hover.
				ToggleBehaviour->SetTransitionTarget(BoxNode != nullptr ? BoxNode->GetVisual() : nullptr);
				ToggleBehaviour->SetToggleTransitionTarget(DotNode != nullptr ? DotNode->GetVisual() : nullptr);
				ToggleBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamRadioButton::HandleValueChanged);
			}));

	ApplyStyle();
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
	// reads exactly this.
	SizeFace(this, Active.BoxSize);
	SizeFace(DotNode, Active.DotSize);

	if (ToggleBehaviour != nullptr)
	{
		// Without notify: pushing the authored value in is not the user selecting. Value before
		// colours, so SetOnColor/SetOffColor's immediate application lands on the right state.
		ToggleBehaviour->SetIsOnWithoutNotify(bIsOn);
		// The pointer transition tints the box; the checked one tints the dot. A selectable left
		// without explicit colours ships white -- these are never optional.
		ToggleBehaviour->SetNormalColor(Active.BoxNormal);
		ToggleBehaviour->SetHoveredColor(Active.BoxHovered);
		ToggleBehaviour->SetPressedColor(Active.BoxPressed);
		ToggleBehaviour->SetOnColor(Active.DotChecked);
		ToggleBehaviour->SetOffColor(Active.DotUnchecked);
	}
}

bool UDreamRadioButton::GetIsOn() const
{
	// The behaviour is the truth once it exists; before that the authored value is all there is.
	return ToggleBehaviour != nullptr ? ToggleBehaviour->GetValue() : bIsOn;
}

void UDreamRadioButton::SetIsOn(bool bInIsOn)
{
	bIsOn = bInIsOn;
	if (ToggleBehaviour != nullptr)
	{
		ToggleBehaviour->SetValue(bInIsOn);
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

void UDreamRadioButton::HandleValueChanged(bool bInIsOn)
{
	// The user clicked it, or the group switched it off because a sibling went on. Mirror it back
	// so the property and the behaviour never disagree, then re-broadcast: a consumer binds to this
	// control, not to a part of it.
	bIsOn = bInIsOn;
	OnValueChangedBP.Broadcast(bInIsOn), OnToggleChanged.Broadcast(bInIsOn);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "RadioButton", UDreamRadioButton)
