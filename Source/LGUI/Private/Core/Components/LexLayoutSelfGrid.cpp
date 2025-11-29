// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfGrid.h"
#include "Core/Components/LexLayoutContainerGrid.h"

void ULexLayoutSelfGrid::BeginPlay()
{
	Super::BeginPlay();
}

FLexLayoutControlAnchorData ULexLayoutSelfGrid::GetLayoutControlAnchor(const ULexWidget* Widget) const
{
	return FLexLayoutControlAnchorData();
}

#if WITH_EDITOR
void ULexLayoutSelfGrid::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutSelfGrid::SetSizeByLayoutContainer(FVector2f Value)
{
	auto Widget = GetWidget();
	if (!Widget)return;
	Widget->SetSizeDelta(FVector2D(Value));
}

void ULexLayoutSelfGrid::SetRowIndex(int Value)
{
	if (RowIndex != Value)
	{
		RowIndex = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutSelfGrid::SetRowCount(int Value)
{
	if (RowCount != Value)
	{
		RowCount = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutSelfGrid::SetColumnIndex(int Value)
{
	if (ColumnIndex != Value)
	{
		ColumnIndex = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutSelfGrid::SetColumnCount(int Value)
{
	if (ColumnCount != Value)
	{
		ColumnCount = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
