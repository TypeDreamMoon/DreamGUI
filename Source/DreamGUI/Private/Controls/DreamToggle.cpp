// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamToggle.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIToggle.h"

void UDreamToggle::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	Realize(this,
		Widget("Toggle")
			.Stretch()
			.With<UDreamLayoutContainerHorizontalBox>()
			// On the root, not on the box, so the whole control is one click target: the box is what
			// gets hit, and the event bubbles up to here. Same arrangement BP_Toggle has.
			.With<UUIToggle>()
			.Children(
				Image("Box").Out(BoxNode)
					// An overlay so the tick has a slot to be centred in. Without a layout container
					// a child has no slot at all, and centring would have to be spelled in anchors.
					.With<UDreamLayoutContainerOverlay>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					})
					.Children(
						Image("Tick").Out(TickNode)
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
							})),
				Text("Label").Out(LabelNode)
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					}))
			.Then([this](UDreamWidget& InRoot)
			{
				// Everything here names a node other than the one it is written on, which is why it
				// cannot be inline: the root is built before the box and the tick exist.
				ToggleBehaviour = InRoot.GetComponent<UUIToggle>();
				if (ToggleBehaviour == nullptr)
				{
					return;
				}
				// The two transitions, deliberately on two visuals. Pointed at one they would
				// overwrite each other and the checked colour would survive until the next hover.
				ToggleBehaviour->SetTransitionTarget(BoxNode != nullptr ? BoxNode->GetVisual() : nullptr);
				ToggleBehaviour->SetToggleTransitionTarget(TickNode != nullptr ? TickNode->GetVisual() : nullptr);
				ToggleBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamToggle::HandleValueChanged);
			}));

	ApplyStyle();
}

namespace
{
	/** How a node states its size to a layout: the brush it draws with. */
	void SetImageSize(UDreamWidget* InNode, const FVector2D& InSize)
	{
		if (UDreamImage* Image = InNode != nullptr ? Cast<UDreamImage>(InNode->GetVisual()) : nullptr)
		{
			FDreamUIImageBrush Brush = Image->GetBrush();
			Brush.ImageSize = FVector2f(static_cast<float>(InSize.X), static_cast<float>(InSize.Y));
			Image->SetBrush(Brush);
		}
	}
}

void UDreamToggle::ApplyStyle()
{
	// Through the BRUSH, not SetWidth. Inside a layout container the child's rect is the layout's to
	// decide, and UDreamPanelLayoutBase::GetDesiredSize reads a node's size off its visual's preferred
	// size -- UDreamImage::GetPreferredWidth returns Brush.ImageSize.X. Setting the widget's width
	// directly is overwritten by the next arrange pass, silently, which is what this control did on
	// its first run: a 26-wide box came out 32 and the tick filled it.
	SetImageSize(BoxNode, Style.BoxSize);
	SetImageSize(TickNode, Style.TickSize);
	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetText(Label);
		LabelVisual->SetColor(Style.LabelColor);
	}
	if (UDreamLayoutContainerHorizontalBox* Row = GetWidgetTree() != nullptr && GetWidgetTree()->RootWidget != nullptr
		? Cast<UDreamLayoutContainerHorizontalBox>(GetWidgetTree()->RootWidget->GetLayoutContainer())
		: nullptr)
	{
		Row->SetSpacing(static_cast<float>(Style.Spacing));
	}
	if (ToggleBehaviour != nullptr)
	{
		// Without notify: pushing the authored value in is not the user toggling it, and a
		// broadcast here would reach handlers before the screen they belong to has finished building.
		ToggleBehaviour->SetIsOnWithoutNotify(bIsOn);
		// The pointer transition tints the box; the checked one tints the tick. Which colour goes
		// where is the whole of what this control decides.
		ToggleBehaviour->SetNormalColor(Style.BoxNormal);
		ToggleBehaviour->SetHoveredColor(Style.BoxHovered);
		ToggleBehaviour->SetPressedColor(Style.BoxPressed);
		ToggleBehaviour->SetOnColor(Style.TickChecked);
		ToggleBehaviour->SetOffColor(Style.TickUnchecked);
	}
}

bool UDreamToggle::GetIsOn() const
{
	// The behaviour is the truth once it exists; before that the authored value is all there is.
	return ToggleBehaviour != nullptr ? ToggleBehaviour->GetValue() : bIsOn;
}

void UDreamToggle::SetIsOn(bool bInIsOn)
{
	bIsOn = bInIsOn;
	if (ToggleBehaviour != nullptr)
	{
		ToggleBehaviour->SetValue(bInIsOn);
	}
}

void UDreamToggle::HandleValueChanged(bool bInIsOn)
{
	// The user clicked it. Mirror it back so the property and the behaviour never disagree, then
	// re-broadcast: a consumer binds to this control, not to a part of it.
	bIsOn = bInIsOn;
	OnToggleChanged.Broadcast(bInIsOn);
}

#if WITH_EDITOR
void UDreamToggle::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The cost of building the tree in code: nothing re-derives from a property the way instancing a
	// changed template would, so every knob has to be pushed through by hand. This is UMG's
	// SynchronizeProperties, and it is the tax the trade comes with.
	ApplyStyle();
}
#endif
