// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamBorder.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
// The brush's image slot is a UObject pointer, and assigning a UTexture2D* into one needs the
// derived type to be complete -- a forward declaration compiles the header and not this.
#include "Engine/Texture2D.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIEventTrigger.h"

const FName UDreamBorder::ContentSlotName(TEXT("Content"));

void UDreamBorder::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(UDreamBorder::ContentSlotName, ContentNode);
}

void UDreamBorder::RealizeBuiltIn()
{
	using namespace DreamUI;

	// Two nodes and nothing else: the face draws, the hole holds. The padding lives on the hole's
	// SLOT rather than on the hole itself, because an overlay's slot is where an inset belongs and a
	// node cannot inset itself from its own parent.
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.Self([](UDreamWidget& InFace)
			{
				// The face carries the rounded silhouette, so it has to be what cuts content off at
				// it -- the same call every other face in this library makes.
				InFace.SetClipping(EDreamWidgetClipping::ClipToBounds);
			})
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				Widget("Content")
					.With<UDreamLayoutContainerOverlay>()
					.With<UDreamNamedSlot>([](UDreamNamedSlot& InSlot)
					{
						// Several, for UDreamScrollBox's reason: the container above is an overlay, so
						// stacking is already defined, and a hole that took one child would drop the
						// second somewhere nobody can see.
						InSlot.bAcceptsSeveral = true;
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
}

void UDreamBorder::WireParts()
{
	// The listener is made here and nowhere else, so a border that was authored with the switch on
	// is listening from its first frame rather than from whenever something happened to write the
	// property again.
	ApplyMouseEventReporting();
	if (ContentNode != nullptr)
	{
		// The authored content tint, applied ONCE at setup rather than on every style push. Render
		// opacity is a channel the host may also be driving (a tween fading a panel in), and a
		// restyle that wrote 1 back over a fade in progress would be a style pass undoing an
		// animation -- which is not what anybody means by restyling.
		ContentNode->SetRenderOpacity(ContentColorAndOpacity.A);
	}
}

void UDreamBorder::ApplyStyle()
{
	const FDreamBorderStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::BorderStyle);

	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.BackgroundBrush);
	if (UDreamVisual* FaceVisual = FaceNode != nullptr ? FaceNode->GetVisual() : nullptr)
	{
		FaceVisual->SetColor(TintOver(Active.Background, BrushColor));
	}
	if (UDreamRectBlock* Rect = FaceNode != nullptr ? Cast<UDreamRectBlock>(FaceNode->GetVisual()) : nullptr)
	{
		// The outline is the rect's own, not a second widget: a border drawn by a nested node would
		// need its own rect, its own rounding and its own clip to stay on the silhouette.
		const bool bHasBorder = Active.BorderThickness > KINDA_SMALL_NUMBER;
		Rect->SetEnableBorder(bHasBorder);
		if (bHasBorder)
		{
			Rect->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Value);
			Rect->SetBorderWidth(Active.BorderThickness);
			Rect->SetBorderColor(Active.BorderColor);
		}
	}
	if (UDreamPanelSlot* ContentSlot = ContentNode != nullptr ? ContentNode->GetPanelSlot() : nullptr)
	{
		// UMG's Padding, and the reason this control is not just a rect: what is inside a border is
		// held off its edge by the same number for every border in the project.
		ContentSlot->SetPadding(Active.Padding);
		// Where in that padded area the content sits. The control's, not the style's: two borders
		// sharing a sheet routinely hold different things, and where a thing sits is that thing's
		// business. Fill is what this has always arranged.
		ContentSlot->SetHorizontalAlignment(HorizontalAlignment);
		ContentSlot->SetVerticalAlignment(VerticalAlignment);
	}
}

void UDreamBorder::SetStyle(const FDreamBorderStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

void UDreamBorder::SetHorizontalAlignment(EDreamPanelHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if (UDreamPanelSlot* ContentSlot = ContentNode != nullptr ? ContentNode->GetPanelSlot() : nullptr)
	{
		// Straight to the slot: an alignment decides nothing about the face's colours or rounding,
		// so a whole style push would be a lot of work with one line of effect.
		ContentSlot->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

void UDreamBorder::SetVerticalAlignment(EDreamPanelVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if (UDreamPanelSlot* ContentSlot = ContentNode != nullptr ? ContentNode->GetPanelSlot() : nullptr)
	{
		ContentSlot->SetVerticalAlignment(InVerticalAlignment);
	}
}

void UDreamBorder::SetBrushColor(FColor InBrushColor)
{
	BrushColor = InBrushColor;
	// Through the push, because a tint multiplies the STYLE's background colour and this is where
	// that colour is resolved; writing the product onto the visual would lose it at the next push.
	ApplyStyle();
}

FMargin UDreamBorder::GetPadding() const
{
	// The style in EFFECT, so a border driven by the project sheet answers the number it is drawn
	// with rather than whatever this instance happens to be carrying underneath the sheet.
	return ResolveStyle(Style, &UDreamUIStyleSheet::BorderStyle).Padding;
}

void UDreamBorder::SetPadding(FMargin InPadding)
{
	Style.Padding = InPadding;
	ApplyStyle();
}

FDreamUIFaceBrush UDreamBorder::GetBrush() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::BorderStyle).BackgroundBrush;
}

void UDreamBorder::SetBrush(const FDreamUIFaceBrush& InBrush)
{
	Style.BackgroundBrush = InBrush;
	ApplyStyle();
}

void UDreamBorder::SetBrushFromTexture(UTexture2D* InTexture)
{
	// Only the image, so a face that was authored nine-sliced or fitted keeps being drawn that way
	// when the picture on it changes -- which is what UMG's own SetBrushFromTexture leaves alone too.
	Style.BackgroundBrush.Image = InTexture;
	ApplyStyle();
}

void UDreamBorder::SetBrushFromSprite(UDreamUISpriteData_BaseObject* InSprite)
{
	// Same slot: the brush's image is a UObject and the face decides what it got by asking. Sprite
	// wins over texture there, so writing one here is enough to switch the face onto the atlas.
	Style.BackgroundBrush.Image = InSprite;
	ApplyStyle();
}

void UDreamBorder::SetContentColorAndOpacity(FLinearColor InContentColorAndOpacity)
{
	ContentColorAndOpacity = InContentColorAndOpacity;
	if (ContentNode != nullptr)
	{
		// Straight to the node: the content's opacity says nothing about the face's colours or
		// rounding, so a whole style push would be a lot of work with one line of effect.
		ContentNode->SetRenderOpacity(InContentColorAndOpacity.A);
	}
}

UUIEventTrigger* UDreamBorder::GetEventTrigger() const
{
	return EventTrigger;
}

void UDreamBorder::SetReportMouseEvents(bool bInReportMouseEvents)
{
	if (bReportMouseEvents == bInReportMouseEvents)
	{
		return;
	}
	bReportMouseEvents = bInReportMouseEvents;
	ApplyMouseEventReporting();
}

void UDreamBorder::SetConsumeMouseEvents(bool bInConsumeMouseEvents)
{
	bConsumeMouseEvents = bInConsumeMouseEvents;
	if (EventTrigger != nullptr)
	{
		// Consuming is the trigger's AllowEventBubbleUp inverted: bubbling up IS letting the event
		// carry on, and consuming it is refusing to.
		EventTrigger->SetAllowEventBubbleUp(!bInConsumeMouseEvents);
	}
}

void UDreamBorder::ApplyMouseEventReporting()
{
	if (!bReportMouseEvents)
	{
		if (EventTrigger != nullptr)
		{
			// Destroyed rather than muted: a trigger left in place keeps answering the raycaster and
			// keeps consuming, which is exactly what turning the switch off is meant to stop.
			EventTrigger->DestroyComponent();
			EventTrigger = nullptr;
		}
		return;
	}
	if (FaceNode == nullptr)
	{
		return;
	}
	EventTrigger = EnsureComponent<UUIEventTrigger>(FaceNode);
	if (EventTrigger == nullptr)
	{
		return;
	}
	EventTrigger->SetAllowEventBubbleUp(!bConsumeMouseEvents);
	// The C++ seams, not the Blueprint ones: a consumer binds to the CONTROL, and the trigger is an
	// implementation detail this control would rather not have anyone reach around it to.
	EventTrigger->GetOnPointerDownEvent().AddUObject(this, &UDreamBorder::HandlePointerDown);
	EventTrigger->GetOnPointerUpEvent().AddUObject(this, &UDreamBorder::HandlePointerUp);
	// MOTION over a border arrives as the drag it belongs to: this event system reports a moving
	// pointer to the element it pressed on, which is the only motion a border can honestly claim.
	EventTrigger->GetOnPointerDragEvent().AddUObject(this, &UDreamBorder::HandlePointerDrag);
	// The double click from its own route, which is the second press itself: it arrives in place of that
	// press's down, so the border reports it at the moment UBorder does and never as a second down.
	EventTrigger->GetOnPointerDoubleClickEvent().AddUObject(this, &UDreamBorder::HandlePointerDoubleClick);
}

void UDreamBorder::HandlePointerDown(UDreamPointerEventData* InEventData)
{
	OnMouseButtonDownEvent.Broadcast(InEventData);
}

void UDreamBorder::HandlePointerUp(UDreamPointerEventData* InEventData)
{
	OnMouseButtonUpEvent.Broadcast(InEventData);
}

void UDreamBorder::HandlePointerDrag(UDreamPointerEventData* InEventData)
{
	OnMouseMoveEvent.Broadcast(InEventData);
}

void UDreamBorder::HandlePointerDoubleClick(UDreamPointerEventData* InEventData)
{
	OnMouseDoubleClickEvent.Broadcast(InEventData);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Border", UDreamBorder)
