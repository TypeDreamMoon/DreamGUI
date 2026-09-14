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
#include "Interaction/DreamContentWidget.h"

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

void UDreamBorder::ApplyStyle()
{
	const FDreamBorderStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::BorderStyle);

	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.BackgroundBrush);
	if (UDreamVisual* FaceVisual = FaceNode != nullptr ? FaceNode->GetVisual() : nullptr)
	{
		FaceVisual->SetColor(Active.Background);
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
	}
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Border", UDreamBorder)
