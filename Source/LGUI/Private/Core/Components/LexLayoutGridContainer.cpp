// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutGridContainer.h"
#include "Core/Components/LexLayoutGridSelf.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayout GridContainer RebuildLayout"), STAT_LexLayoutGridContainer, STATGROUP_LGUI);

ULexLayoutGridContainer::ULexLayoutGridContainer()
{
	Rows = Columns =
	{
		FLexLayoutGridSize(1.0f),
		FLexLayoutGridSize(1.0f),
	};
}

#if WITH_EDITOR
void ULexLayoutGridContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutGridContainer::SetPadding(FMargin Value)
{
	if (Padding != Value)
	{
		Padding = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutGridContainer::SetRows(const TArray<FLexLayoutGridSize>& Value)
{
	if (Rows != Value)
	{
		Rows = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void ULexLayoutGridContainer::SetColumns(const TArray<FLexLayoutGridSize>& Value)
{
	if (Columns != Value)
	{
		Columns = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void ULexLayoutGridContainer::SetSpacing(const FVector2D& Value)
{
	if (Spacing != Value)
	{
		Spacing = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutGridContainer::UpdateLayout()
{
	SCOPE_CYCLE_COUNTER(STAT_LexLayoutGridContainer);
	auto Widget = GetWidget();
	if (!Widget)return;
	FVector2D StartPosition;
	StartPosition.X = Padding.Left;
	StartPosition.Y = -Padding.Top;//left top as start point
	FVector2D RectSize;
	RectSize.X = Widget->GetWidth() - Padding.Left - Padding.Right;
	RectSize.Y = Widget->GetHeight() - Padding.Top - Padding.Bottom;

	float ColumnTotalRatio = 0, RowTotalRatio = 0;
	float ColumnTotalConstantSize = 0, RowTotalConstantSize = 0;
	for (auto& Item : Columns)
	{
		if (Item.Type == ELexLayoutGridSizeType::Ratio)
		{
			ColumnTotalRatio += Item.FixedValue;
		}
		else
		{
			ColumnTotalConstantSize += Item.FixedValue;
		}
	}
	for (auto& Item : Rows)
	{
		if (Item.Type == ELexLayoutGridSizeType::Ratio)
		{
			RowTotalRatio += Item.FixedValue;
		}
		else
		{
			RowTotalConstantSize += Item.FixedValue;
		}
	}
	float InvColumnTotalRatio = 1.0f / ColumnTotalRatio;
	float InvRowTotalRatio = 1.0f / RowTotalRatio;
	float ThisFreeWidth = RectSize.X - ColumnTotalConstantSize - Spacing.X * (Columns.Num() - 1);//width exclude constant size and all space
	float ThisFreeHeight = RectSize.Y - RowTotalConstantSize - Spacing.Y * (Rows.Num() - 1);//height exclude constant size and all space
	auto GetOffset = [&](int ColumnIndex, int RowIndex)
	{
		float OffsetXRatio = 0, OffsetYRatio = 0;
		float OffsetXConstant = 0, OffsetYConstant = 0;
		for (int i = 0; i < ColumnIndex; i++)
		{
			auto& Column = Columns[i];
			if (Column.Type == ELexLayoutGridSizeType::Ratio)
			{
				OffsetXRatio += Column.FixedValue;
			}
			else
			{
				OffsetXConstant += Column.FixedValue;
			}
		}
		for (int i = 0; i < RowIndex; i++)
		{
			auto& Row = Rows[i];
			if (Row.Type == ELexLayoutGridSizeType::Ratio)
			{
				OffsetYRatio += Row.FixedValue;
			}
			else
			{
				OffsetYConstant += Row.FixedValue;
			}
		}
		return StartPosition + FVector2D(
			OffsetXRatio * InvColumnTotalRatio * ThisFreeWidth + OffsetXConstant + ColumnIndex * Spacing.X
			, -OffsetYRatio * InvRowTotalRatio * ThisFreeHeight - OffsetYConstant - RowIndex * Spacing.Y
		);
	};
	TArray<ULexWidget*> NotLocatedWidgets;
	TArray<int> AlreadyFilledRows;
	TArray<int> AlreadyFilledColumns;
	for (auto& Child : Widget->GetUIChildren())
	{
		if (!Child->GetWidgetActiveInHierarchy())continue;
		auto ChildLayoutSelf = Cast<ULexLayoutGridSelf>(Child->GetLayoutSelf());
		if (ChildLayoutSelf && ChildLayoutSelf->GetIgnoreLayoutContainer())
		{
			continue;
		}

		auto AnchorMin = Child->GetAnchorMin();
		auto AnchorMax = Child->GetAnchorMax();
		if (AnchorMin.X != AnchorMax.X)//custom anchor not support
		{
			Child->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
		}
		if (AnchorMin.Y != AnchorMax.Y)
		{
			Child->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
		}

		int ColumnIndex = 0, ColumnCount = 1, RowIndex = 0, RowCount = 1;
		if (ChildLayoutSelf)
		{
			ColumnIndex = ChildLayoutSelf->GetColumnIndex();
			ColumnCount = ChildLayoutSelf->GetColumnCount();
			RowIndex = ChildLayoutSelf->GetRowIndex();
			RowCount = ChildLayoutSelf->GetRowCount();
		}
		else
		{
			NotLocatedWidgets.Add(Child);
			continue;
		}
		float ColumnRatio = 0, ColumnConstant = 0;
		for (int i = ColumnIndex, Count = FMath::Min(i + ColumnCount, Columns.Num()); i < Count; i++)
		{
			AlreadyFilledColumns.AddUnique(i);
			auto& Column = Columns[i];
			if (Column.Type == ELexLayoutGridSizeType::Ratio)
			{
				ColumnRatio += Column.FixedValue;
			}
			else
			{
				ColumnConstant += Column.FixedValue;
			}
		}
		float RowRatio = 0, RowConstant = 0;
		for (int i = RowIndex, Count = FMath::Min(i + RowCount, Rows.Num()); i < Count; i++)
		{
			AlreadyFilledRows.AddUnique(i);
			auto& Row = Rows[i];
			if (Row.Type == ELexLayoutGridSizeType::Ratio)
			{
				RowRatio += Row.FixedValue;
			}
			else
			{
				RowConstant += Row.FixedValue;
			}
		}
		float ColumnSize = ThisFreeWidth * ColumnRatio * InvColumnTotalRatio + ColumnConstant + (ColumnCount - 1) * Spacing.X;
		float RowSize = ThisFreeHeight * RowRatio * InvRowTotalRatio + RowConstant + (RowCount - 1) * Spacing.Y;
		auto AnchorOffset = GetOffset(FMath::Min(ColumnIndex, Columns.Num() - 1), FMath::Min(RowIndex, Rows.Num() - 1));
		float AnchorOffsetX = ColumnSize * (Child->GetPivot().X) + AnchorOffset.X;
		float AnchorOffsetY = -RowSize * (1.0f - Child->GetPivot().Y) + AnchorOffset.Y;
		AnchorMin = Child->GetAnchorMin();
		AnchorOffsetX -= AnchorMin.X * Widget->GetWidth();
		AnchorOffsetY += (1 - AnchorMin.Y) * Widget->GetHeight();
		Child->SetAnchoredPosition(FVector2D(AnchorOffsetX, AnchorOffsetY));

		if (ChildLayoutSelf)
		{
			ChildLayoutSelf->SetSizeByLayoutContainer(FVector2f(ColumnSize, RowSize));
		}
	}
	if (NotLocatedWidgets.Num() > 0)
	{
		for (int ColumnIndex = 0; ColumnIndex < Columns.Num(); ColumnIndex++)
		{
			for (int RowIndex = 0; RowIndex < Rows.Num(); RowIndex++)
			{
				if (!AlreadyFilledColumns.Contains(ColumnIndex) && !AlreadyFilledRows.Contains(RowIndex))
				{
					auto ChildWidget = NotLocatedWidgets[0];
					
					NotLocatedWidgets.RemoveAt(0);
					if (NotLocatedWidgets.Num() <= 0)//break the loop if all widgets are located
					{
						ColumnIndex = Columns.Num();
						RowIndex = Rows.Num();
					}
				}
			}
		}
	}
}

FLexLayoutControlAnchorData ULexLayoutGridContainer::GetLayoutControlAnchor(const ULexWidget* TargetWidget)const
{
	FLexLayoutControlAnchorData Result;
	auto ThisWidget = GetWidget();
	if (ThisWidget == TargetWidget)//self
	{
        
	}
	else if (ThisWidget->GetUIChildren().Contains(TargetWidget))//child
	{
		bool bIgnoreLayout = false;
		if (auto LayoutSelf = TargetWidget->GetLayoutSelf())
		{
			bIgnoreLayout = LayoutSelf->GetIgnoreLayoutContainer();
		}
		if (!bIgnoreLayout)
		{
			Result.bCanControlHorizontalPosition = true;
			Result.bCanControlVerticalPosition = true;
			Result.bCanControlHorizontalSize = true;
			Result.bCanControlVerticalSize = true;
		}
	}
	return Result;
}
