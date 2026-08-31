// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamInputKeySelector.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"

#define LOCTEXT_NAMESPACE "DreamInputKeySelector"

void UDreamInputKeySelector::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// The button's tree, unchanged: the face IS the root -- a key binder is one rectangle, and a
	// separate background child would only manufacture a gap for the hit test to fall through.
	Realize(this,
		Node<UDreamRectBlock>("Face").Out(FaceNode)
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.With<UUIButton>()
			.Children(
				DreamUI::Text("Label").Out(LabelNode)
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
					// Its own visual: the pointer transition tints the face it is standing on, and the
					// listening state rides those same three colours (see PushFaceColours).
					ButtonBehaviour->SetTransitionTarget(InRoot.GetVisual());
					ButtonBehaviour->GetOnClickEvent().AddUObject(this, &UDreamInputKeySelector::HandleClicked);
				}
			}));

	ApplyStyle();
}

void UDreamInputKeySelector::ApplyStyle()
{
	const FDreamInputKeySelectorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::InputKeySelectorStyle);

	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.FaceBrush);

	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetColor(Active.LabelColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}
	if (UDreamPanelSlot* LabelSlot = LabelNode != nullptr ? LabelNode->GetPanelSlot() : nullptr)
	{
		LabelSlot->SetPadding(Active.ContentPadding);
	}

	// The words and the colours both depend on the armed state, so both are pushed through the same
	// two functions the state transition uses -- there is no second copy of either rule.
	PushLabel();
	PushFaceColours();

	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SizeControlHeight(Active.Height);
}

FKey UDreamInputKeySelector::GetSelectedKey() const
{
	return SelectedKey;
}

void UDreamInputKeySelector::SetSelectedKey(FKey InKey)
{
	if (SelectedKey == InKey)
	{
		// Still re-label: this is also the path ApplyStyle-adjacent code takes after an authored
		// value, and a silent equal write must not leave the face saying something else.
		PushLabel();
		return;
	}
	SelectedKey = InKey;
	PushLabel();
	OnKeySelected.Broadcast(SelectedKey);
	OnValueChangedBP.Broadcast(SelectedKey);
}

bool UDreamInputKeySelector::GetIsListening() const
{
	return bIsListening;
}

void UDreamInputKeySelector::BeginListening()
{
	SetIsListening(true);
}

void UDreamInputKeySelector::CancelListening()
{
	SetIsListening(false);
}

bool UDreamInputKeySelector::NotifyKeyPressed(FKey InKey)
{
	if (!bIsListening)
	{
		// Not armed, so this key is none of this control's business -- and saying so is what lets a
		// project route every key here without asking first.
		return false;
	}
	if (!InKey.IsValid())
	{
		return false;
	}
	if (bEscapeCancels && InKey == EKeys::Escape)
	{
		// Taken, but not bound: the caller must still treat it as consumed, or the same Escape would
		// also close the screen the player is rebinding on.
		SetIsListening(false);
		return true;
	}
	// Disarm BEFORE the value moves, so a handler on OnKeySelected sees a settled control -- one that
	// re-opened a dialog from that handler would otherwise arm the next selector and immediately have
	// this same key still in flight.
	SetIsListening(false);
	SetSelectedKey(InKey);
	return true;
}

void UDreamInputKeySelector::HandleClicked()
{
	// A click on an armed selector disarms it: the button is the only thing the player can reach
	// while it is waiting, and a control with no way out of its own state is a trap.
	SetIsListening(!bIsListening);
}

void UDreamInputKeySelector::SetIsListening(bool bInIsListening)
{
	if (bIsListening == bInIsListening)
	{
		return;
	}
	bIsListening = bInIsListening;
	PushLabel();
	PushFaceColours();
	OnIsListeningChanged.Broadcast(bIsListening);
}

void UDreamInputKeySelector::PushLabel()
{
	UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr;
	if (LabelVisual == nullptr)
	{
		return;
	}
	if (bIsListening)
	{
		// Empty keeps the built-in words, the same "an empty brush keeps the glyph" bargain the check
		// box struck -- so a project overrides the prompt without overriding the control.
		LabelVisual->SetText(ListeningText.IsEmpty()
			? LOCTEXT("Listening", "Press a key...")
			: ListeningText);
		return;
	}
	LabelVisual->SetText(SelectedKey.IsValid()
		// The key's OWN display name, not a spelling this control keeps: the engine already
		// localizes it, and a second table would drift from the one the rest of the game shows.
		? SelectedKey.GetDisplayName()
		: (NoKeyText.IsEmpty() ? LOCTEXT("NoKey", "Unbound") : NoKeyText));
}

void UDreamInputKeySelector::PushFaceColours()
{
	if (ButtonBehaviour == nullptr)
	{
		return;
	}
	const FDreamInputKeySelectorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::InputKeySelectorStyle);
	// A UUISelectable-hosted face renders WHITE without explicit colours: the transition is the only
	// writer of that visual's colour, and an unset transition colour is not "leave it alone".
	//
	// All three, not just Normal, while armed. The pointer is by definition still on the button the
	// player just clicked, so the selectable is sitting in Hovered or Pressed -- writing the listening
	// colour into Normal alone means the feedback appears only once the mouse is moved away, which is
	// precisely when the player has stopped looking for it.
	ButtonBehaviour->SetNormalColor(bIsListening ? Active.Listening : Active.Normal);
	ButtonBehaviour->SetHoveredColor(bIsListening ? Active.Listening : Active.Hovered);
	ButtonBehaviour->SetPressedColor(bIsListening ? Active.Listening : Active.Pressed);
}

#undef LOCTEXT_NAMESPACE

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "InputKeySelector", UDreamInputKeySelector)
