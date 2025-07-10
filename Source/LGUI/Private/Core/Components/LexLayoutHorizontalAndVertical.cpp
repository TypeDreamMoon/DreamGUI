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
	ThisSize.X -= Widget->GetPadding().Left + Widget->GetPadding().Right;
	ThisSize.Y -= Widget->GetPadding().Bottom + Widget->GetPadding().Top;
	float TotalChildrenSize = 0;
	int NumLayoutChildren = 0;
	bool ShouldSortChildren = false;
	TArray<ULexWidget*> GrowChildWidgets;
	TArray<ULexWidget*> ShrinkChildWidgets;
	float AccumulatedGrowValue = 0.0f;
	float AccumulatedShrinkValue = 0.0f;
	for (int i = 0; i < Children.Num(); i++)
	{
		auto Child = Children[i];
		auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)Child->CheckAndGetLayoutSlot();//make sure layout slot is created
		if (!Child->IsVisibleForLayout())continue;
		if (LayoutSlot->GetOrder() != 0)
		{
			ShouldSortChildren = true;
		}
		if (LayoutSlot->GetGrow() > 0)
		{
			AccumulatedGrowValue += LayoutSlot->GetGrow();
			GrowChildWidgets.Add(Child);
		}
		if (LayoutSlot->GetShrink() > 0)
		{
			AccumulatedShrinkValue += LayoutSlot->GetShrink();
			ShrinkChildWidgets.Add(Child);
		}
		NumLayoutChildren++;
		auto ChildSize = Child->GetRenderSize();
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			TotalChildrenSize += ChildSize.X + Child->GetMargin().Left + Child->GetMargin().Right;
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			TotalChildrenSize += ChildSize.Y + Child->GetMargin().Top + Child->GetMargin().Bottom;
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
				if (GrowChildWidgets.Num() <= 0)//if child can grow then space will be 0 
				{
					SpaceValue = (ThisSize.X - TotalChildrenSize) / (Spacing.Type == ELexLayoutSpacingType::Around ? NumLayoutChildren : (NumLayoutChildren - 1));
				}
			}
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			if (ThisSize.Y > TotalChildrenSize)
			{
				if (GrowChildWidgets.Num() <= 0)//if child can grow then space will be 0 
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
			if (GrowChildWidgets.Num() > 0)
			{
				auto ExtraSize = ThisSize.X - TotalChildrenSize;
				auto AverageExtraSize = ExtraSize / AccumulatedGrowValue;
				for (auto Child : GrowChildWidgets)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto GrowSize = LayoutSlot->GetGrow() * AverageExtraSize;
					auto RenderSize = Child->GetRenderSize();
					RenderSize.X += GrowSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
		else if (ThisSize.X < TotalChildrenSize)
		{
			if (ShrinkChildWidgets.Num() > 0)
			{
				auto ExtraSize = TotalChildrenSize - ThisSize.X;
				auto AverageExtraSize = ExtraSize / AccumulatedShrinkValue;
				for (auto Child : ShrinkChildWidgets)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto ShrinkSize = LayoutSlot->GetShrink() * AverageExtraSize;
					auto RenderSize = Child->GetRenderSize();
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
			if (GrowChildWidgets.Num() > 0)
			{
				auto ExtraSize = ThisSize.Y - TotalChildrenSize;
				auto AverageExtraSize = ExtraSize / AccumulatedGrowValue;
				for (auto Child : GrowChildWidgets)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto GrowSize = LayoutSlot->GetGrow() * AverageExtraSize;
					auto RenderSize = Child->GetRenderSize();
					RenderSize.Y += GrowSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
		else if (ThisSize.Y < TotalChildrenSize)
		{
			if (ShrinkChildWidgets.Num() > 0)
			{
				auto ExtraSize = TotalChildrenSize - ThisSize.Y;
				auto AverageExtraSize = ExtraSize / AccumulatedShrinkValue;
				for (auto Child : ShrinkChildWidgets)
				{
					auto LayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)(Child->GetLayoutSlot());
					auto ShrinkSize = LayoutSlot->GetShrink() * AverageExtraSize;
					auto RenderSize = Child->GetRenderSize();
					RenderSize.Y -= ShrinkSize;
					Child->SetRenderSizeByLayout(RenderSize);
				}
				TotalChildrenSize = ThisSize.X;//grow/shrink will fill all space
			}
		}
	}
	
	FVector ChildPosition = FVector(0, 0, 0);
	switch (Direction)
	{
	case ELexLayoutDirection::Horizontal:
	case ELexLayoutDirection::HorizontalReverse:
		{
			ChildPosition.Y -= TotalChildrenSize * 0.5;
			ChildPosition.Y += (Widget->GetPadding().Left - Widget->GetPadding().Right) * 0.5f;
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
			ChildPosition.Z += (Widget->GetPadding().Bottom - Widget->GetPadding().Top) * 0.5f;
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
				return A.GetHierarchyIndex() < B.GetHierarchyIndex();
			return LayoutSlotA->GetOrder() < LayoutSlotB->GetOrder();
		});
	}
	bool ReverseDirection = Direction == ELexLayoutDirection::HorizontalReverse || Direction == ELexLayoutDirection::VerticalReverse;
	for (int i = 0; i < ReorderedChildren.Num(); i++)
	{
		auto Child = ReorderedChildren[ReverseDirection ? ReorderedChildren.Num() - i - 1 : i];
		if (!Child->IsVisibleForLayout())continue;
		auto ChildLayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)Child->GetLayoutSlot();
		ChildLayoutSlot->UpdateChildLayout(this);
		auto ChildSize = Child->GetRenderSize();
		ChildSize.X += Child->GetMargin().Left + Child->GetMargin().Right;
		ChildSize.Y += Child->GetMargin().Top + Child->GetMargin().Bottom;
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
				float OffsetByMargin = (Child->GetMargin().Left - Child->GetMargin().Right) * 0.5f;
				auto Pos = Child->GetRelativeLocation();
				Pos.Y = ChildPosition.Y + HalfChildWidth + OffsetByMargin;
				float OffsetV = Widget->GetPadding().Bottom - Widget->GetPadding().Top + (Child->GetMargin().Bottom - Child->GetMargin().Top);
				OffsetV *= 0.5f;
				Pos.Z = ChildVOffset + OffsetV;
				Child->SetRelativeLocation(Pos);
				ChildPosition.Y += ChildSize.X + SpaceValue;
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
				float OffsetByMargin = (Child->GetMargin().Bottom - Child->GetMargin().Top) * 0.5f;
				auto Pos = Child->GetRelativeLocation();
				Pos.Z = ChildPosition.Z - HalfChildHeight + OffsetByMargin;
				float OffsetH = Widget->GetPadding().Left - Widget->GetPadding().Right + (Child->GetMargin().Left - Child->GetMargin().Right);
				OffsetH *= 0.5f;
				Pos.Y = ChildHOffset + OffsetH;
				Child->SetRelativeLocation(Pos);
				ChildPosition.Z -= ChildSize.Y + SpaceValue;
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
		auto ChildSize = Child->GetRenderSize();
		switch (Direction)
		{
		case ELexLayoutDirection::Horizontal:
		case ELexLayoutDirection::HorizontalReverse:
			ResultSize += ChildSize.X + Child->GetMargin().Left + Child->GetMargin().Right;
			break;
		case ELexLayoutDirection::Vertical:
		case ELexLayoutDirection::VerticalReverse:
			ChildSize.Y += Child->GetMargin().Top + Child->GetMargin().Bottom;
			if (ResultSize < ChildSize.X)
			{
				ResultSize = ChildSize.X;
			}
			break;
		}
	}
	ResultSize += Widget->GetPadding().Left + Widget->GetPadding().Right;
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
		auto ChildSize = Child->GetRenderSize();
		ChildSize.X += Child->GetMargin().Left + Child->GetMargin().Right;
		ChildSize.Y += Child->GetMargin().Top + Child->GetMargin().Bottom;
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
	ResultSize += Widget->GetPadding().Top + Widget->GetPadding().Bottom;
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
		GetWidget()->MarkSizeDirty_Recursive();
	}
}

void ULexLayoutHorizontalAndVertical::SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value)
{
	if (HorizontalAlignment != Value)
	{
		HorizontalAlignment = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}
void ULexLayoutHorizontalAndVertical::SetVerticalAlignment(ELexLayoutVerticalAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}
void ULexLayoutHorizontalAndVertical::SetSpacing(const FLexLayoutSpacing& Value)
{
	if (Spacing != Value)
	{
		Spacing = Value;
		GetWidget()->MarkSizeDirty_Recursive();
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
		GetWidget()->MarkSizeDirty_Recursive();
	}
}
void ULexLayoutHorizontalAndVerticalSlot::SetVerticalAlignment(ELexLayoutVerticalAlignment Value)
{
	if (VerticalAlignment != Value)
	{
		VerticalAlignment = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}
void ULexLayoutHorizontalAndVerticalSlot::SetPositionOffset(const FVector2D& Value)
{
	if (PositionOffset != Value)
	{
		PositionOffset = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetOrder(int32 Value)
{
	if (Order != Value)
	{
		Order = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetGrow(float Value)
{
	if (Grow != Value)
	{
		Grow = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::SetShrink(float Value)
{
	if (Shrink != Value)
	{
		Shrink = Value;
		GetWidget()->MarkSizeDirty_Recursive();
	}
}

void ULexLayoutHorizontalAndVerticalSlot::UpdateChildLayout(ULexLayoutHorizontalAndVertical* Layout)
{
	auto Widget = GetWidget();
	if (!Widget)return;
	if (!Layout)return;
	auto Parent = Widget->GetUIParent();
	if (!Parent)return;
	
	bool ShouldSetPositionX = true;
	bool ShouldSetPositionY = true;
	auto Direction = Layout->GetDirection();
	if (Direction == ELexLayoutDirection::Horizontal || Direction == ELexLayoutDirection::HorizontalReverse)
	{
		ShouldSetPositionX = false;
	}
	if (Direction == ELexLayoutDirection::Vertical || Direction == ELexLayoutDirection::VerticalReverse)
	{
		ShouldSetPositionY = false;
	}

	auto Position = Widget->GetRelativeLocation();
	if (ShouldSetPositionX)
	{
		float PaddingAndMarginOffsetX = Parent->GetPadding().Left - Parent->GetPadding().Right + (Widget->GetMargin().Left - Widget->GetMargin().Right);
		PaddingAndMarginOffsetX *= 0.5f;
		float SizeOffsetX = 0;
		switch (HorizontalAlignment)
		{
		case ELexLayoutHorizontalAlignment::Left:
			SizeOffsetX = (Parent->GetRenderSize().X - Widget->GetRenderSize().X) * -0.5f;
			break;
		case ELexLayoutHorizontalAlignment::Right:
			SizeOffsetX = (Parent->GetRenderSize().X - Widget->GetRenderSize().X) * 0.5f;
			break;
		case ELexLayoutHorizontalAlignment::Center:
			break;
		}
		float OffsetX = PaddingAndMarginOffsetX + SizeOffsetX + PositionOffset.X;
		Position.Y = OffsetX;
	}
	if (ShouldSetPositionY)
	{
		float PaddingAndMarginOffsetY = Parent->GetPadding().Bottom - Parent->GetPadding().Top + (Widget->GetMargin().Bottom - Widget->GetMargin().Top);
		PaddingAndMarginOffsetY *= 0.5f;
		float SizeOffsetY = 0;
		switch (VerticalAlignment)
		{
		case ELexLayoutVerticalAlignment::Top:
			SizeOffsetY = (Parent->GetRenderSize().Y - Widget->GetRenderSize().Y) * 0.5f;
			break;
		case ELexLayoutVerticalAlignment::Bottom:
			SizeOffsetY = (Parent->GetRenderSize().Y - Widget->GetRenderSize().Y)* -0.5f;
			break;
		case ELexLayoutVerticalAlignment::Middle:
			break;
		}
		float OffsetY = PaddingAndMarginOffsetY + SizeOffsetY + PositionOffset.Y;
		Position.Z = OffsetY;
	}
	Widget->SetRelativeLocation(Position);
}


