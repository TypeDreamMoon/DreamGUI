// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutHorizontalAndVertical.h"

#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutHorizontalAndVertical::OnUpdateLayout()
{
	auto Widget = GetWidget();
	if (!Widget)return;
	auto& Children = Widget->GetUIChildren();
	auto ThisSize = Widget->GetRenderSize();
	ThisSize.X -= Widget->GetRenderPadding().Left + Widget->GetRenderPadding().Right;
	ThisSize.Y -= Widget->GetRenderPadding().Bottom + Widget->GetRenderPadding().Top;
	float TotalChildrenSize = 0;
	int NumLayoutChildren = 0;
	bool ShouldSortChildren = false;
	bool ShouldIgnoreChildrenGrowOrShrink = false;
	switch (Direction)
	{
	case ELexLayoutDirection::Horizontal:
	case ELexLayoutDirection::HorizontalReverse:
		ShouldIgnoreChildrenGrowOrShrink = Widget->GetWidth().Type == ELexWidgetSizeType::ShrinkToChildren;
		break;
	case ELexLayoutDirection::Vertical:
	case ELexLayoutDirection::VerticalReverse:
		ShouldIgnoreChildrenGrowOrShrink = Widget->GetHeight().Type == ELexWidgetSizeType::ShrinkToChildren;
		break;
	}
	TArray<ULexWidget*> WidgetsWithGrow;
	TArray<ULexWidget*> WidgetsWithShrink;
	TArray<ULexWidget*> WidgetsWithoutGrowOrShrink;
	float AccumulatedGrowValue = 0.0f;
	float AccumulatedShrinkValue = 0.0f;
	//use PreferredSize to calculate RenderSize
	for (int i = 0; i < Children.Num(); i++)
	{
		auto Child = Children[i];
		auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)Child->GetLayoutSlot();//make sure layout slot is created
		if (!Child->IsVisibleForLayout())continue;
		if (LayoutSlot->GetOrder() != 0)
		{
			ShouldSortChildren = true;
		}
		if (!ShouldIgnoreChildrenGrowOrShrink)
		{
			if (LayoutSlot->GetGrow() > 0)
			{
				AccumulatedGrowValue += LayoutSlot->GetGrow();
				WidgetsWithGrow.Add(Child);
			}
		}
		if (!ShouldIgnoreChildrenGrowOrShrink)
		{
			if (LayoutSlot->GetShrink() > 0)
			{
				AccumulatedShrinkValue += LayoutSlot->GetShrink();
				WidgetsWithShrink.Add(Child);
			}
		}
		if (ShouldIgnoreChildrenGrowOrShrink || (LayoutSlot->GetGrow() == 0 && LayoutSlot->GetShrink() == 0))
		{
			WidgetsWithoutGrowOrShrink.Add(Child);
		}
		NumLayoutChildren++;
		auto ChildSize = Child->GetPreferredSize();
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			TotalChildrenSize += ChildSize.X + Child->GetRenderMargin().Left + Child->GetRenderMargin().Right;
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			TotalChildrenSize += ChildSize.Y + Child->GetRenderMargin().Top + Child->GetRenderMargin().Bottom;
			break;
		}
	}
	auto SpaceValue = 0;
	if (Spacing.Type == ELexLayoutSpacingType::Between || Spacing.Type == ELexLayoutSpacingType::Around)
	{
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			if (ThisSize.X > TotalChildrenSize)
			{
				if (WidgetsWithGrow.Num() <= 0)//if child can grow then space will be 0 
				{
					SpaceValue = (ThisSize.X - TotalChildrenSize) / (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
				}
			}
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			if (ThisSize.Y > TotalChildrenSize)
			{
				if (WidgetsWithGrow.Num() <= 0)//if child can grow then space will be 0 
				{
					SpaceValue = (ThisSize.Y - TotalChildrenSize) / (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
				}
			}
			break;
		}
	}
	else
	{
		SpaceValue = Spacing.Value;
	}
	if (SpaceValue > 0)
	{
		TotalChildrenSize += SpaceValue * (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
	}
	
	if (Direction == ELexLayoutDirection::Horizontal || Direction == ELexLayoutDirection::HorizontalReverse)
	{
		if (ThisSize.X > TotalChildrenSize)
		{
			if (WidgetsWithGrow.Num() > 0)
			{
				auto ExtraSize = ThisSize.X - TotalChildrenSize;
				auto AverageExtraSize = ExtraSize / AccumulatedGrowValue;
				for (auto Child : WidgetsWithGrow)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto GrowSize = LayoutSlot->GetGrow() * AverageExtraSize;
					auto RenderSize = Child->GetPreferredSize();
					RenderSize.X += GrowSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
		else if (ThisSize.X < TotalChildrenSize)
		{
			if (WidgetsWithShrink.Num() > 0)
			{
				auto ExtraSize = TotalChildrenSize - ThisSize.X;
				auto AverageExtraSize = ExtraSize / AccumulatedShrinkValue;
				for (auto Child : WidgetsWithShrink)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto ShrinkSize = LayoutSlot->GetShrink() * AverageExtraSize;
					auto RenderSize = Child->GetPreferredSize();
					RenderSize.X -= ShrinkSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
	}
	else if (Direction == ELexLayoutDirection::Vertical || Direction == ELexLayoutDirection::VerticalReverse)
	{
		if (ThisSize.Y > TotalChildrenSize)
		{
			if (WidgetsWithGrow.Num() > 0)
			{
				auto ExtraSize = ThisSize.Y - TotalChildrenSize;
				auto AverageExtraSize = ExtraSize / AccumulatedGrowValue;
				for (auto Child : WidgetsWithGrow)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto GrowSize = LayoutSlot->GetGrow() * AverageExtraSize;
					auto RenderSize = Child->GetPreferredSize();
					RenderSize.Y += GrowSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
		else if (ThisSize.Y < TotalChildrenSize)
		{
			if (WidgetsWithShrink.Num() > 0)
			{
				auto ExtraSize = TotalChildrenSize - ThisSize.Y;
				auto AverageExtraSize = ExtraSize / AccumulatedShrinkValue;
				for (auto Child : WidgetsWithShrink)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto ShrinkSize = LayoutSlot->GetShrink() * AverageExtraSize;
					auto RenderSize = Child->GetPreferredSize();
					RenderSize.Y -= ShrinkSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
	}
	for (auto Child : WidgetsWithoutGrowOrShrink)
	{
		Child->SetRenderSizeByLayout(Child->GetPreferredSize());
	}

	//after set children's RenderSize, we can use RenderSize to calculate location	
	FVector ChildPosition = FVector(0, 0, 0);
	switch (Direction)
	{
	case ELexLayoutDirection::Horizontal:
	case ELexLayoutDirection::HorizontalReverse:
		{
			ChildPosition.Y -= TotalChildrenSize * 0.5;
			ChildPosition.Y += (Widget->GetRenderPadding().Left - Widget->GetRenderPadding().Right) * 0.5f;
			ChildPosition.Y += Spacing.Type == ELexLayoutSpacingType::Around ? SpaceValue * 0.5f : 0;
			double SizeOffset = (ThisSize.X - TotalChildrenSize) * 0.5;
			switch (HorizontalAlignment)
			{
			case ELexLayoutHorizontalAlignment::Left:
				ChildPosition.Y -= SizeOffset;
				break;
			case ELexLayoutHorizontalAlignment::Center:
				break;
			case ELexLayoutHorizontalAlignment::Right:
				ChildPosition.Y += SizeOffset;
				break;
			}
		}
		break;
	case ELexLayoutDirection::Vertical:
	case ELexLayoutDirection::VerticalReverse:
		{
			ChildPosition.Z += TotalChildrenSize * 0.5;
			ChildPosition.Z += (Widget->GetRenderPadding().Bottom - Widget->GetRenderPadding().Top) * 0.5f;
			ChildPosition.Z -= Spacing.Type == ELexLayoutSpacingType::Around ? SpaceValue * 0.5f : 0;
			double SizeOffset = (ThisSize.Y - TotalChildrenSize) * 0.5;
			switch (VerticalAlignment)
			{
			case ELexLayoutVerticalAlignment::Top:
				ChildPosition.Z += SizeOffset;
				break;
			case ELexLayoutVerticalAlignment::Middle:
				break;
			case ELexLayoutVerticalAlignment::Bottom:
				ChildPosition.Z -= SizeOffset;
				break;
			}
		}
		break;
	}

	auto ReorderedChildren = Children;
	if (ShouldSortChildren)
	{
		ReorderedChildren.Sort([this](const ULexWidget& A, const ULexWidget& B)
		{
			auto LayoutSlotA = (ULexLayoutHorizontalAndVerticalSlot*)A.GetLayoutSlot();
			auto LayoutSlotB = (ULexLayoutHorizontalAndVerticalSlot*)B.GetLayoutSlot();
			if (LayoutSlotA->GetOrder() == LayoutSlotB->GetOrder())
				return A.GetSiblingIndex() < B.GetSiblingIndex();
			return LayoutSlotA->GetOrder() < LayoutSlotB->GetOrder();
		});
	}
	bool ReverseDirection = Direction == ELexLayoutDirection::HorizontalReverse || Direction == ELexLayoutDirection::VerticalReverse;
	for (int i = 0; i < ReorderedChildren.Num(); i++)
	{
		auto Child = ReorderedChildren[ReverseDirection ? ReorderedChildren.Num() - i - 1 : i];
		if (!Child->IsVisibleForLayout())continue;
		auto ChildLayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)Child->GetLayoutSlot();
		auto ChildSize = Child->GetRenderSize();
		// ChildSize.X += Child->GetMargin().Left + Child->GetMargin().Right;
		// ChildSize.Y += Child->GetMargin().Top + Child->GetMargin().Bottom;
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			{
				auto HalfChildWidth = ChildSize.X * 0.5f;
				auto ChildVOffset = ChildLayoutSlot->GetPositionOffset().Y;
				switch (ChildLayoutSlot->GetVerticalAlignment())
				{
				case ELexLayoutVerticalAlignment::Top:
					ChildVOffset += (ThisSize.Y - ChildSize.Y) * 0.5f;
					break;
				case ELexLayoutVerticalAlignment::Middle:
					break;
				case ELexLayoutVerticalAlignment::Bottom:
					ChildVOffset += -(ThisSize.Y - ChildSize.Y) * 0.5f;
					break;
				}
				float OffsetByMargin = Child->GetRenderMargin().Left;
				auto Pos = Child->GetRelativeLocation();
				auto PivotOffsetX =
					Child->GetRenderSize().X * (Child->GetPivot().X - 0.5f)//this pivot
				+ Widget->GetRenderSize().X * (0.5f - Widget->GetPivot().X);//parent pivot
				Pos.Y = ChildPosition.Y + HalfChildWidth + OffsetByMargin + PivotOffsetX;
				float OffsetV = Widget->GetRenderPadding().Bottom - Widget->GetRenderPadding().Top + (Child->GetRenderMargin().Bottom - Child->GetRenderMargin().Top);
				OffsetV *= 0.5f;
				auto PivotOffsetY =
					Child->GetRenderSize().Y * (Child->GetPivot().Y - 0.5f)//this pivot
				+ Widget->GetRenderSize().Y * (0.5f - Widget->GetPivot().Y);//parent pivot
				Pos.Z = ChildVOffset + OffsetV + PivotOffsetY;
				Child->SetRelativeLocation(Pos);
				ChildPosition.Y += ChildSize.X + (Child->GetRenderMargin().Left + Child->GetRenderMargin().Right) + SpaceValue;
			}
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			{
				auto HalfChildHeight = ChildSize.Y * 0.5f;
				auto ChildHOffset = ChildLayoutSlot->GetPositionOffset().X;
				switch (ChildLayoutSlot->GetHorizontalAlignment())
				{
				case ELexLayoutHorizontalAlignment::Left:
					ChildHOffset += -(ThisSize.X - ChildSize.X) * 0.5f;
					break;
				case ELexLayoutHorizontalAlignment::Center:
					break;
				case ELexLayoutHorizontalAlignment::Right:
					ChildHOffset += (ThisSize.X - ChildSize.X) * 0.5f;
					break;
				}
				float OffsetByMargin = Child->GetRenderMargin().Bottom;
				auto Pos = Child->GetRelativeLocation();
				auto PivotOffsetY =
					Child->GetRenderSize().Y * (Child->GetPivot().Y - 0.5f)//this pivot
				+ Widget->GetRenderSize().Y * (0.5f - Widget->GetPivot().Y);//parent pivot
				Pos.Z = ChildPosition.Z - HalfChildHeight + OffsetByMargin + PivotOffsetY;
				float OffsetH = Widget->GetRenderPadding().Left - Widget->GetRenderPadding().Right + (Child->GetRenderMargin().Left - Child->GetRenderMargin().Right);
				OffsetH *= 0.5f;
				auto PivotOffsetX =
					Child->GetRenderSize().X * (Child->GetPivot().X - 0.5f)//this pivot
				+ Widget->GetRenderSize().X * (0.5f - Widget->GetPivot().X);//parent pivot
				Pos.Y = ChildHOffset + OffsetH + PivotOffsetX;
				Child->SetRelativeLocation(Pos);
				ChildPosition.Z -= ChildSize.Y + (Child->GetRenderMargin().Top + Child->GetRenderMargin().Bottom) + SpaceValue;
			}
			break;
		}
	}
}

#if WITH_EDITOR
void ULexLayoutHorizontalAndVertical::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

TSubclassOf<ULexLayoutSlot> ULexLayoutHorizontalAndVertical::GetSlotClass() const
{
	return ULexLayoutHorizontalAndVerticalSlot::StaticClass();
}
float ULexLayoutHorizontalAndVertical::GetShrinkToChildrenWidth()
{
	auto Widget = GetWidget();
	if (!Widget)return 0;
	auto& Children = Widget->GetUIChildren();
	float ResultSize = 0;
	int NumLayoutChildren = 0;
	for (int i = 0; i < Children.Num(); i++)
	{
		auto Child = Children[i];
		if (!Child->IsVisibleForLayout())continue;
		NumLayoutChildren++;
		auto ChildSize = Child->GetPreferredSize();
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			ResultSize += ChildSize.X + Child->GetRenderMargin().Left + Child->GetRenderMargin().Right;
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			ChildSize.Y += Child->GetRenderMargin().Top + Child->GetRenderMargin().Bottom;
			if (ResultSize < ChildSize.X)
			{
				ResultSize = ChildSize.X;
			}
			break;
		}
	}
	ResultSize += Widget->GetRenderPadding().Left + Widget->GetRenderPadding().Right;
	if (Spacing.Type == ELexLayoutSpacingType::Fixed)
	{
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			{
				auto AllSpaceValue = Spacing.Value * (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
				ResultSize += AllSpaceValue;
			}
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			break;
		}
	}
	return ResultSize;
}
float ULexLayoutHorizontalAndVertical::GetShrinkToChildrenHeight()
{
	auto Widget = GetWidget();
	if (!Widget)return 0;
	auto& Children = Widget->GetUIChildren();
	float ResultSize = 0;
	int NumLayoutChildren = 0;
	for (int i = 0; i < Children.Num(); i++)
	{
		auto Child = Children[i];
		if (!Child->IsVisibleForLayout())continue;
		NumLayoutChildren++;
		auto ChildSize = Child->GetPreferredSize();
		ChildSize.X += Child->GetRenderMargin().Left + Child->GetRenderMargin().Right;
		ChildSize.Y += Child->GetRenderMargin().Top + Child->GetRenderMargin().Bottom;
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			if (ResultSize < ChildSize.Y)
			{
				ResultSize = ChildSize.Y;
			}
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			ResultSize += ChildSize.Y;
			break;
		}
	}
	ResultSize += Widget->GetRenderPadding().Top + Widget->GetRenderPadding().Bottom;
	if (Spacing.Type == ELexLayoutSpacingType::Fixed)
	{
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			{
				auto AllSpaceValue = Spacing.Value * (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
				ResultSize += AllSpaceValue;
			}
			break;
		}
	}
	return ResultSize;
}

void ULexLayoutHorizontalAndVertical::SetDirection(ELexLayoutDirection Value)
{
	if (Direction != Value)
	{
		Direction = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutHorizontalAndVertical::SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value)
{
	if (HorizontalAlignment != Value)
	{
		HorizontalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}
void ULexLayoutHorizontalAndVertical::SetVerticalAlignment(ELexLayoutVerticalAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}
void ULexLayoutHorizontalAndVertical::SetSpacing(const FLexLayoutSpacing& Value)
{
	if (Spacing != Value)
	{
		Spacing = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}


#if WITH_EDITOR
void ULexLayoutHorizontalAndVerticalSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

ULexLayoutHorizontalAndVertical* ULexLayoutHorizontalAndVerticalSlot::GetLayout()
{
	if (!CacheLayout.IsValid())
	{
		if (auto Parent = GetWidget()->GetUIParent())
		{
			CacheLayout = Cast<ULexLayoutHorizontalAndVertical>(Parent->GetLayout());
		}
	}
	return CacheLayout.Get();
}

void ULexLayoutHorizontalAndVerticalSlot::SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value)
{
	if (HorizontalAlignment != Value)
	{
		HorizontalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}
void ULexLayoutHorizontalAndVerticalSlot::SetVerticalAlignment(ELexLayoutVerticalAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}
void ULexLayoutHorizontalAndVerticalSlot::SetPositionOffset(const FVector2D& Value)
{
	if (PositionOffset != Value)
	{
		PositionOffset = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetOrder(int32 Value)
{
	if (Order != Value)
	{
		Order = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetGrow(float Value)
{
	if (Grow != Value)
	{
		Grow = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetShrink(float Value)
{
	if (Shrink != Value)
	{
		Shrink = Value;
		GetWidget()->MarkRenderSizeChanged();
	}
}

bool ULexLayoutHorizontalAndVerticalSlot::GetLayoutControlWidth() const
{
	return Grow != 0 || Shrink != 0;
}

bool ULexLayoutHorizontalAndVerticalSlot::GetLayoutControlHeight() const
{
	return Grow != 0 || Shrink != 0;
}
