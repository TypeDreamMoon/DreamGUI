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

	Realize(this,
		Widget("RadioButton")
			.Stretch()
			.With<UDreamLayoutContainerHorizontalBox>()
			// On the root, not on the box, so the whole row is one click target -- the toggle's
			// arrangement, kept.
			.With<UUIToggle>()
			.Children(
				Node<UDreamRectBlock>("Box").Out(BoxNode)
					// An overlay so the dot has a slot to be centred in.
					.With<UDreamLayoutContainerOverlay>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					})
					.Children(
						Node<UDreamRectBlock>("Dot").Out(DotNode)
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
							})),
				DreamUI::Text("Label").Out(LabelNode)
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
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

	// A rect block states no intrinsic size; authored width/height feed the Auto slot's (and the
	// overlay's centred slot's) desired-size fallback, captured before the first arrange.
	auto SizeAuthored = [](UDreamWidget* InNode, const FVector2D& InSize)
	{
		if (InNode != nullptr)
		{
			InNode->SetWidth(static_cast<float>(InSize.X));
			InNode->SetHeight(static_cast<float>(InSize.Y));
		}
	};
	SizeAuthored(BoxNode, Active.BoxSize);
	SizeAuthored(DotNode, Active.DotSize);

	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetText(Label);
		LabelVisual->SetColor(Active.LabelColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}
	if (UDreamLayoutContainerHorizontalBox* Row = GetWidgetTree() != nullptr && GetWidgetTree()->RootWidget != nullptr
		? Cast<UDreamLayoutContainerHorizontalBox>(GetWidgetTree()->RootWidget->GetLayoutContainer())
		: nullptr)
	{
		Row->SetSpacing(Active.Spacing);
	}
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
