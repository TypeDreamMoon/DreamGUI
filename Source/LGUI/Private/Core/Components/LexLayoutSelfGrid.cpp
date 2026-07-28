// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayoutSelfGrid.h"
#include "Core/Components/LexLayoutContainerGrid.h"
#include "Core/Components/LexWidget.h"

namespace LexLayoutSelfGridLocal
{
	static float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static void RequestParentLayout(ULexLayoutSelfGrid* LayoutSelf)
	{
		if (!IsValid(LayoutSelf)) return;
		if (ULexWidget* Widget = LayoutSelf->GetWidget(); IsValid(Widget))
		{
			if (ULexWidget* Parent = Widget->GetParent(); IsValid(Parent))
			{
				ULexWidget::MarkLayoutForRebuild(Parent);
			}
		}
	}
}

void ULexLayoutSelfGrid::BeginPlay()
{
	Super::BeginPlay();
}

FLexLayoutControlAnchorData ULexLayoutSelfGrid::GetLayoutControlAnchor(const ULexWidget* Widget) const
{
	FLexLayoutControlAnchorData Result;
	ULexWidget* ThisWidget = GetWidget();
	ULexWidget* ParentWidget = IsValid(ThisWidget) ? ThisWidget->GetParent() : nullptr;
	if (ThisWidget == Widget && IsValid(ParentWidget)
		&& IsValid(Cast<ULexLayoutContainerGrid>(ParentWidget->GetLayoutContainer())))
	{
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}

FVector2f ULexLayoutSelfGrid::GetLayoutPreferredSize()
{
	if (ULexWidget* Widget = GetWidget(); IsValid(Widget))
	{
		return FVector2f(
			LexLayoutSelfGridLocal::NonNegativeFinite(Widget->GetWidth()),
			LexLayoutSelfGridLocal::NonNegativeFinite(Widget->GetHeight()));
	}
	return FVector2f::ZeroVector;
}

#if WITH_EDITOR
void ULexLayoutSelfGrid::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RowIndex = FMath::Max(0, RowIndex);
	ColumnIndex = FMath::Max(0, ColumnIndex);
	RowCount = FMath::Max(1, RowCount);
	ColumnCount = FMath::Max(1, ColumnCount);
	LexLayoutSelfGridLocal::RequestParentLayout(this);
}
#endif

void ULexLayoutSelfGrid::SetSizeByLayoutContainer(FVector2f Value)
{
	if (ULexWidget* Widget = GetWidget(); IsValid(Widget))
	{
		Widget->SetSizeDelta(FVector2D(
			LexLayoutSelfGridLocal::NonNegativeFinite(Value.X),
			LexLayoutSelfGridLocal::NonNegativeFinite(Value.Y)));
	}
}

void ULexLayoutSelfGrid::SetRowIndex(int Value)
{
	Value = FMath::Max(0, Value);
	if (RowIndex != Value)
	{
		RowIndex = Value;
		LexLayoutSelfGridLocal::RequestParentLayout(this);
	}
}

void ULexLayoutSelfGrid::SetRowCount(int Value)
{
	Value = FMath::Max(1, Value);
	if (RowCount != Value)
	{
		RowCount = Value;
		LexLayoutSelfGridLocal::RequestParentLayout(this);
	}
}

void ULexLayoutSelfGrid::SetColumnIndex(int Value)
{
	Value = FMath::Max(0, Value);
	if (ColumnIndex != Value)
	{
		ColumnIndex = Value;
		LexLayoutSelfGridLocal::RequestParentLayout(this);
	}
}

void ULexLayoutSelfGrid::SetColumnCount(int Value)
{
	Value = FMath::Max(1, Value);
	if (ColumnCount != Value)
	{
		ColumnCount = Value;
		LexLayoutSelfGridLocal::RequestParentLayout(this);
	}
}
