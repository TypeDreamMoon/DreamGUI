// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSimpleAnchor.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutSimpleAnchor::OnUpdateLayout()
{
	auto Widget = GetWidget();
	if (!Widget)return;
	auto& Children = Widget->GetUIChildren();
	for (int i = 0; i < Children.Num(); i++)
	{
		auto Child = Children[i];
		if (!Child->IsVisibleForLayout())continue;
		auto LayoutSlot = (ULexLayoutSimpleAnchorSlot*)Child->GetLayoutSlot();//make sure layout slot is created
		LayoutSlot->UpdateChildLayout();
	}
}

TSubclassOf<ULexLayoutSlot> ULexLayoutSimpleAnchor::GetSlotClass() const
{
	return ULexLayoutSimpleAnchorSlot::StaticClass();
}

void ULexLayoutSimpleAnchorSlot::SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value)
{
	if (HorizontalAlignment != Value)
	{
		HorizontalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}
void ULexLayoutSimpleAnchorSlot::SetVerticalAlignment(ELexLayoutVerticalAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutSimpleAnchorSlot::SetHorizontalOffset(FLexWidgetOffset Value)
{
	if (HorizontalOffset != Value)
	{
		HorizontalOffset = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutSimpleAnchorSlot::SetVerticalOffset(FLexWidgetOffset Value)
{
	if (VerticalOffset != Value)
	{
		VerticalOffset = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutSimpleAnchorSlot::UpdateChildLayout()
{
	auto Widget = GetWidget();
	if (!Widget)return;
	auto Parent = Widget->GetUIParent();
	if (!Parent)return;

	auto Position = Widget->GetRelativeLocation();
	{
		float PaddingAndMarginOffset = 0;
		float SizeOffset = 0;
		switch (HorizontalAlignment)
		{
		case ELexLayoutHorizontalAlignment::Left:
			{
				auto ParentSize = Parent->GetRenderSize().X - Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right;
				auto WidgetSize = Widget->GetRenderSize().X;
				SizeOffset = (ParentSize - WidgetSize) * -0.5f;
				PaddingAndMarginOffset = (Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right) * 0.5f + (Widget->GetRenderMargin().Left - Widget->GetRenderMargin().Right);
			}
			break;
		case ELexLayoutHorizontalAlignment::Right:
			{
				auto ParentSize = Parent->GetRenderSize().X - Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right;
				auto WidgetSize = Widget->GetRenderSize().X;
				SizeOffset = (ParentSize - WidgetSize) * 0.5f;
				PaddingAndMarginOffset = (Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right) * 0.5f + (Widget->GetRenderMargin().Bottom - Widget->GetRenderMargin().Top);
			}
			break;
		case ELexLayoutHorizontalAlignment::Center:
			PaddingAndMarginOffset = Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right + (Widget->GetRenderMargin().Left - Widget->GetRenderMargin().Right);
			PaddingAndMarginOffset *= 0.5f;
			break;
		}
		float Offset = HorizontalOffset.Value;
		switch (HorizontalOffset.Type)
		{
		case ELexWidgetOffsetType::Fixed:
			Offset = HorizontalOffset.Value;
			break;
		case ELexWidgetOffsetType::RelativeToParentSize:
			{
				auto ParentSize = Parent->GetRenderSize().X - Parent->GetRenderPadding().Left - Parent->GetRenderPadding().Right;
				Offset = HorizontalOffset.Percent * 0.01f * ParentSize;
			}
			break;
		case ELexWidgetOffsetType::RelativeToSelfSize:
			{
				auto WidgetSize = Widget->GetRenderSize().X - Widget->GetRenderPadding().Left - Widget->GetRenderPadding().Right;
				Offset = HorizontalOffset.Percent * 0.01f * WidgetSize;
			}
			break;
		}
		Position.Y = Offset + PaddingAndMarginOffset + SizeOffset;
	}
	{
		float PaddingAndMarginOffset = 0;
		float SizeOffset = 0;
		switch (VerticalAlignment)
		{
		case ELexLayoutVerticalAlignment::Top:
			{
				auto ParentSize = Parent->GetRenderSize().Y - Parent->GetRenderPadding().Top - Parent->GetRenderPadding().Bottom;
				auto WidgetSize = Widget->GetRenderSize().Y;
				SizeOffset = (ParentSize - WidgetSize) * 0.5f;
				PaddingAndMarginOffset = (Parent->GetRenderPadding().Bottom - Parent->GetRenderPadding().Top) * 0.5f + (Widget->GetRenderMargin().Bottom - Widget->GetRenderMargin().Top);
			}
			break;
		case ELexLayoutVerticalAlignment::Bottom:
			{
				auto ParentSize = Parent->GetRenderSize().Y - Parent->GetRenderPadding().Top - Parent->GetRenderPadding().Bottom;
				auto WidgetSize = Widget->GetRenderSize().Y;
				SizeOffset = (ParentSize - WidgetSize) * -0.5f;
				PaddingAndMarginOffset = (Parent->GetRenderPadding().Bottom - Parent->GetRenderPadding().Top) * 0.5f + (Widget->GetRenderMargin().Bottom - Widget->GetRenderMargin().Top);
			}
			break;
		case ELexLayoutVerticalAlignment::Middle:
			PaddingAndMarginOffset = Parent->GetRenderPadding().Bottom - Parent->GetRenderPadding().Top + (Widget->GetRenderMargin().Bottom - Widget->GetRenderMargin().Top);
			PaddingAndMarginOffset *= 0.5f;
			break;
		}
		float Offset = VerticalOffset.Value;
		switch (VerticalOffset.Type)
		{
		case ELexWidgetOffsetType::Fixed:
			Offset = VerticalOffset.Value;
			break;
		case ELexWidgetOffsetType::RelativeToParentSize:
			{
				auto ParentSize = Parent->GetRenderSize().Y - Parent->GetRenderPadding().Top - Parent->GetRenderPadding().Bottom;
				Offset = VerticalOffset.Percent * 0.01f * ParentSize;
			}
			break;
		case ELexWidgetOffsetType::RelativeToSelfSize:
			{
				auto WidgetSize = Widget->GetRenderSize().Y - Widget->GetRenderMargin().Top - Widget->GetRenderMargin().Bottom;
				Offset = VerticalOffset.Percent * 0.01f * WidgetSize;
			}
			break;
		}
		Position.Z = Offset + PaddingAndMarginOffset + SizeOffset;
	}
	Widget->SetRelativeLocation(Position);
}
