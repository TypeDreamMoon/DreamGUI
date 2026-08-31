// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamButton.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"

void UDreamButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// The face IS the root: a button is one rectangle, and giving it a separate background child
	// would only manufacture a gap for the hit test to fall through.
	Realize(this,
		Node<UDreamRectBlock>("Face").Out(FaceNode)
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.With<UUIButton>()
			.Children(
				Text("Label").Out(LabelNode)
					.Visual([](UDreamText& InText)
					{
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					}))
			.Then([this](UDreamWidget& InRoot)
			{
				ButtonBehaviour = InRoot.GetComponent<UUIButton>();
				if (ButtonBehaviour != nullptr)
				{
					// Its own visual: a button's pointer transition tints the face it is standing on.
					ButtonBehaviour->SetTransitionTarget(InRoot.GetVisual());
					ButtonBehaviour->GetOnClickEvent().AddUObject(this, &UDreamButton::HandleClicked);
				}
			}));

	ApplyStyle();
}


void UDreamButton::ApplyStyle()
{
	const FDreamButtonStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ButtonStyle);
	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.FaceBrush);

	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetText(Label);
		LabelVisual->SetColor(Active.LabelColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}
	if (UDreamPanelSlot* LabelSlot = LabelNode != nullptr ? LabelNode->GetPanelSlot() : nullptr)
	{
		LabelSlot->SetPadding(Active.ContentPadding);
	}
	if (ButtonBehaviour != nullptr)
	{
		ButtonBehaviour->SetNormalColor(Active.Normal);
		ButtonBehaviour->SetHoveredColor(Active.Hovered);
		ButtonBehaviour->SetPressedColor(Active.Pressed);
	}
	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SetHeight(Active.Height);
}

void UDreamButton::HandleClicked()
{
	OnClicked.Broadcast();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Button", UDreamButton)
