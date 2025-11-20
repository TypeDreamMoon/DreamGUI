// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutGridSelf.h"
#include "Core/Components/LexLayoutGridContainer.h"

void ULexLayoutGridSelf::BeginPlay()
{
	Super::BeginPlay();
}

FLexLayoutControlAnchorData ULexLayoutGridSelf::GetLayoutControlAnchor(const ULexWidget* Widget) const
{
	return FLexLayoutControlAnchorData();
}

#if WITH_EDITOR
void ULexLayoutGridSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutGridSelf::SetSizeByLayoutContainer(FVector2f Value)
{
	auto Widget = GetWidget();
	if (!Widget)return;
	Widget->SetSizeDelta(FVector2D(Value));
}

void ULexLayoutGridSelf::SetRowIndex(int Value)
{
	if (RowIndex != Value)
	{
		RowIndex = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutGridSelf::SetRowCount(int Value)
{
	if (RowCount != Value)
	{
		RowCount = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutGridSelf::SetColumnIndex(int Value)
{
	if (ColumnIndex != Value)
	{
		ColumnIndex = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
void ULexLayoutGridSelf::SetColumnCount(int Value)
{
	if (ColumnCount != Value)
	{
		ColumnCount = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
