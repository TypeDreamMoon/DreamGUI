// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamToggle.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
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
						DreamUI::Text("Tick").Out(TickNode)
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

void UDreamToggle::ApplyStyle()
{
	// The sheet when the project has one and this instance did not opt out; the inline Style
	// otherwise. One decision at the top, so everything below is about one style, whichever it is.
	const FDreamToggleStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ToggleStyle);

	// Through the BRUSH, not SetWidth. Inside a layout container the child's rect is the layout's to
	// decide, and UDreamPanelLayoutBase::GetDesiredSize reads a node's size off its visual's preferred
	// size -- UDreamImage::GetPreferredWidth returns Brush.ImageSize.X. Setting the widget's width
	// directly is overwritten by the next arrange pass, silently, which is what this control did on
	// its first run: a 26-wide box came out 32 and the tick filled it.
	ShapeFace(BoxNode, Active.CornerRadius);
	if (UDreamImage* BoxImage = BoxNode != nullptr ? Cast<UDreamImage>(BoxNode->GetVisual()) : nullptr)
	{
		// Through the brush while the face is an image: the Auto slot reads the visual's preferred
		// size, which for an image is Brush.ImageSize.
		FDreamUIImageBrush Brush = BoxImage->GetBrush();
		Brush.ImageSize = FVector2f(static_cast<float>(Active.BoxSize.X), static_cast<float>(Active.BoxSize.Y));
		BoxImage->SetBrush(Brush);
	}
	if (UDreamText* TickText = TickNode != nullptr ? Cast<UDreamText>(TickNode->GetVisual()) : nullptr)
	{
		// A glyph, sized by the style's tick height; its colour is the checked transition's to give.
		TickText->SetFontSize(static_cast<float>(Active.TickSize.Y));
	}
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
		// Without notify: pushing the authored value in is not the user toggling it, and a
		// broadcast here would reach handlers before the screen they belong to has finished building.
		ToggleBehaviour->SetIsOnWithoutNotify(bIsOn);
		// The pointer transition tints the box; the checked one tints the tick. Which colour goes
		// where is the whole of what this control decides.
		ToggleBehaviour->SetNormalColor(Active.BoxNormal);
		ToggleBehaviour->SetHoveredColor(Active.BoxHovered);
		ToggleBehaviour->SetPressedColor(Active.BoxPressed);
		ToggleBehaviour->SetOnColor(Active.TickChecked);
		ToggleBehaviour->SetOffColor(Active.TickUnchecked);
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
