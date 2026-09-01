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
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIButton.h"

const FName UDreamButton::ContentSlotName(TEXT("Content"));

void UDreamButton::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	// The names the built-in tree gives them, and the names a template has to use. Content is
	// optional because a template that offers no hole is a perfectly good button -- everything that
	// writes to it null-checks, and the slot machinery treats "no such slot" as an empty one.
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Label"), LabelNode);
	OutParts.Emplace(TEXT("Content"), ContentNode, /*bRequired*/false);
}

void UDreamButton::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The face IS the root: a button is one rectangle, and giving it a separate background child
	// would only manufacture a gap for the hit test to fall through.
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				Text("Label")
					.Visual([](UDreamText& InText)
					{
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					}),
				// The hole, over the label rather than beside it: the two are alternatives, and
				// ApplyStyle stands the label down whenever this holds anything. Fill on both axes so
				// whatever the host puts here gets the whole face to arrange itself in, exactly as
				// the label does; the node itself draws nothing.
				Widget("Content")
					.With<UDreamNamedSlot>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
	// No .Out() and no .Then(): the parts are bound by name afterwards, and the behaviour goes on in
	// WireParts. Both of those have to work for a tree this code did not write.
}

void UDreamButton::WireParts()
{
	// Ensure, not Get: on the built-in road the face is ours and this adds the behaviour; on the
	// template road the face is somebody's drawing and this is the only thing that makes it a
	// button. A control that always carries its own UIButton has no state in which clicking it does
	// nothing -- which is the omission BP_Button shipped with for months.
	ButtonBehaviour = EnsureComponent<UUIButton>(FaceNode);
	if (ButtonBehaviour != nullptr && FaceNode != nullptr)
	{
		// Its own visual: a button's pointer transition tints the face it is standing on.
		ButtonBehaviour->SetTransitionTarget(FaceNode->GetVisual());
		ButtonBehaviour->GetOnClickEvent().AddUObject(this, &UDreamButton::HandleClicked);
	}
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
	// Content wins over the stock label. Here rather than at attach time because ApplyStyle is the
	// one function every road already ends at -- initialize, a property edit, a runtime restyle --
	// and a rule enforced in only some of them is a rule that holds until someone recolours.
	SwapBuiltInForSlot(LabelNode, ContentNode, ContentSlotName);
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
	SizeControlHeight(Active.Height);
}

void UDreamButton::HandleClicked()
{
	OnClicked.Broadcast();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Button", UDreamButton)
