// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayoutContainerGrid.h"
#include "Core/Components/LexLayoutSelfGrid.h"
#include "Core/Components/LexWidget.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutContainer Grid"), STAT_LexLayoutContainerGrid, STATGROUP_LGUI);

namespace LexResponsiveGridLocal
{
	static float FiniteOrZero(float Value)
	{
		return FMath::IsFinite(Value) ? Value : 0.0f;
	}

	static float NonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static float FiniteFloat(double Value, float Fallback = 0.0f)
	{
		if (!FMath::IsFinite(Value)) return Fallback;
		return static_cast<float>(FMath::Clamp(
			Value, -static_cast<double>(UE_MAX_FLT), static_cast<double>(UE_MAX_FLT)));
	}

	static float NonNegativeFloat(double Value)
	{
		return FMath::Max(0.0f, FiniteFloat(Value));
	}

	static void SanitizeTracks(TArray<FLexLayoutGridSize>& Tracks)
	{
		for (FLexLayoutGridSize& Track : Tracks)
		{
			Track.FixedValue = NonNegative(Track.FixedValue);
			Track.RatioValue = NonNegative(Track.RatioValue);
		}
	}

	static TArray<float> BuildTrackSizes(const TArray<FLexLayoutGridSize>& Tracks, float AvailableSize)
	{
		TArray<float> Result;
		Result.Init(0.0f, Tracks.Num());
		double FixedTotal = 0.0;
		double RatioTotal = 0.0;
		for (const FLexLayoutGridSize& Track : Tracks)
		{
			if (Track.Type == ELexLayoutGridSizeType::Ratio)
			{
				RatioTotal += NonNegative(Track.RatioValue);
			}
			else
			{
				FixedTotal += NonNegative(Track.FixedValue);
			}
		}
		const double RatioSpace = FMath::Max(0.0, static_cast<double>(NonNegative(AvailableSize)) - FixedTotal);
		for (int32 Index = 0; Index < Tracks.Num(); ++Index)
		{
			const FLexLayoutGridSize& Track = Tracks[Index];
			const double TrackSize = Track.Type == ELexLayoutGridSizeType::Ratio
				? (RatioTotal > UE_SMALL_NUMBER ? (NonNegative(Track.RatioValue) / RatioTotal) * RatioSpace : 0.0)
				: NonNegative(Track.FixedValue);
			Result[Index] = static_cast<float>(FMath::Clamp(TrackSize, 0.0, static_cast<double>(UE_MAX_FLT)));
		}
		return Result;
	}

	static float SumTracks(const TArray<float>& Tracks, int32 Start, int32 Count)
	{
		double Result = 0.0;
		const int32 End = FMath::Min(Tracks.Num(), Start + Count);
		for (int32 Index = FMath::Max(0, Start); Index < End; ++Index)
		{
			Result += Tracks[Index];
		}
		return NonNegativeFloat(Result);
	}
}

ULexLayoutContainerGrid::ULexLayoutContainerGrid()
{
	Rows = Columns =
	{
		FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 1.0f),
		FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 1.0f),
	};
}

#if WITH_EDITOR
void ULexLayoutContainerGrid::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	LexResponsiveGridLocal::SanitizeTracks(Rows);
	LexResponsiveGridLocal::SanitizeTracks(Columns);
	Spacing.X = LexResponsiveGridLocal::NonNegative(Spacing.X);
	Spacing.Y = LexResponsiveGridLocal::NonNegative(Spacing.Y);
	Padding.Left = LexResponsiveGridLocal::FiniteOrZero(Padding.Left);
	Padding.Top = LexResponsiveGridLocal::FiniteOrZero(Padding.Top);
	Padding.Right = LexResponsiveGridLocal::FiniteOrZero(Padding.Right);
	Padding.Bottom = LexResponsiveGridLocal::FiniteOrZero(Padding.Bottom);
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutContainerGrid::SetPadding(FMargin Value)
{
	Value.Left = LexResponsiveGridLocal::FiniteOrZero(Value.Left);
	Value.Top = LexResponsiveGridLocal::FiniteOrZero(Value.Top);
	Value.Right = LexResponsiveGridLocal::FiniteOrZero(Value.Right);
	Value.Bottom = LexResponsiveGridLocal::FiniteOrZero(Value.Bottom);
	if (Padding != Value)
	{
		Padding = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutContainerGrid::SetRows(const TArray<FLexLayoutGridSize>& Value)
{
	TArray<FLexLayoutGridSize> Sanitized = Value;
	LexResponsiveGridLocal::SanitizeTracks(Sanitized);
	if (Rows != Sanitized)
	{
		Rows = MoveTemp(Sanitized);
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutContainerGrid::SetColumns(const TArray<FLexLayoutGridSize>& Value)
{
	TArray<FLexLayoutGridSize> Sanitized = Value;
	LexResponsiveGridLocal::SanitizeTracks(Sanitized);
	if (Columns != Sanitized)
	{
		Columns = MoveTemp(Sanitized);
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutContainerGrid::SetSpacing(const FVector2D& Value)
{
	const FVector2D Sanitized(
		LexResponsiveGridLocal::NonNegative(Value.X), LexResponsiveGridLocal::NonNegative(Value.Y));
	if (Spacing != Sanitized)
	{
		Spacing = Sanitized;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutContainerGrid::CalculateLayout()
{
	SCOPE_CYCLE_COUNTER(STAT_LexLayoutContainerGrid);
	if (!bIsLayoutDirty) return;
	bIsLayoutDirty = false;
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget) || Columns.IsEmpty() || Rows.IsEmpty()) return;

	const float GapX = LexResponsiveGridLocal::NonNegative(Spacing.X);
	const float GapY = LexResponsiveGridLocal::NonNegative(Spacing.Y);
	const float PaddingLeft = LexResponsiveGridLocal::FiniteOrZero(Padding.Left);
	const float PaddingTop = LexResponsiveGridLocal::FiniteOrZero(Padding.Top);
	const float PaddingRight = LexResponsiveGridLocal::FiniteOrZero(Padding.Right);
	const float PaddingBottom = LexResponsiveGridLocal::FiniteOrZero(Padding.Bottom);
	const float WidgetWidth = LexResponsiveGridLocal::NonNegative(Widget->GetWidth());
	const float WidgetHeight = LexResponsiveGridLocal::NonNegative(Widget->GetHeight());
	const float ContentWidth = LexResponsiveGridLocal::NonNegativeFloat(
		static_cast<double>(WidgetWidth) - PaddingLeft - PaddingRight - static_cast<double>(GapX) * (Columns.Num() - 1));
	const float ContentHeight = LexResponsiveGridLocal::NonNegativeFloat(
		static_cast<double>(WidgetHeight) - PaddingTop - PaddingBottom - static_cast<double>(GapY) * (Rows.Num() - 1));
	const TArray<float> ColumnSizes = LexResponsiveGridLocal::BuildTrackSizes(Columns, ContentWidth);
	const TArray<float> RowSizes = LexResponsiveGridLocal::BuildTrackSizes(Rows, ContentHeight);

	auto ApplyCell = [&](ULexWidget* Child, int32 Column, int32 Row, int32 ColumnSpan, int32 RowSpan, ULexLayoutSelfGrid* GridSelf)
	{
		if (!IsValid(Child)) return;
		Column = FMath::Clamp(Column, 0, Columns.Num() - 1);
		Row = FMath::Clamp(Row, 0, Rows.Num() - 1);
		ColumnSpan = FMath::Clamp(ColumnSpan, 1, Columns.Num() - Column);
		RowSpan = FMath::Clamp(RowSpan, 1, Rows.Num() - Row);
		const float Left = LexResponsiveGridLocal::FiniteFloat(
			static_cast<double>(PaddingLeft) + LexResponsiveGridLocal::SumTracks(ColumnSizes, 0, Column) + static_cast<double>(GapX) * Column);
		const float Top = LexResponsiveGridLocal::FiniteFloat(
			static_cast<double>(PaddingTop) + LexResponsiveGridLocal::SumTracks(RowSizes, 0, Row) + static_cast<double>(GapY) * Row);
		const float Width = LexResponsiveGridLocal::NonNegativeFloat(
			static_cast<double>(LexResponsiveGridLocal::SumTracks(ColumnSizes, Column, ColumnSpan)) + static_cast<double>(GapX) * (ColumnSpan - 1));
		const float Height = LexResponsiveGridLocal::NonNegativeFloat(
			static_cast<double>(LexResponsiveGridLocal::SumTracks(RowSizes, Row, RowSpan)) + static_cast<double>(GapY) * (RowSpan - 1));

		const FVector2D AnchorMin = Child->GetAnchorMin();
		const FVector2D AnchorMax = Child->GetAnchorMax();
		if (AnchorMin.X != AnchorMax.X) Child->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
		if (AnchorMin.Y != AnchorMax.Y) Child->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
		const FVector2D Pivot = Child->GetPivot();
		const FVector2D EffectiveAnchorMin = Child->GetAnchorMin();
		const double PivotX = FMath::IsFinite(Pivot.X) ? Pivot.X : 0.5;
		const double PivotY = FMath::IsFinite(Pivot.Y) ? Pivot.Y : 0.5;
		const double AnchorX = FMath::IsFinite(EffectiveAnchorMin.X) ? EffectiveAnchorMin.X : 0.5;
		const double AnchorY = FMath::IsFinite(EffectiveAnchorMin.Y) ? EffectiveAnchorMin.Y : 0.5;
		const float AnchoredX = LexResponsiveGridLocal::FiniteFloat(
			static_cast<double>(Left) + static_cast<double>(Width) * PivotX - static_cast<double>(WidgetWidth) * AnchorX);
		const float AnchoredY = LexResponsiveGridLocal::FiniteFloat(
			-(static_cast<double>(Top) + static_cast<double>(Height) * (1.0 - PivotY))
			+ static_cast<double>(WidgetHeight) * (1.0 - AnchorY));
		Child->SetAnchoredPosition(FVector2D(AnchoredX, AnchoredY));
		if (IsValid(GridSelf)) GridSelf->SetSizeByLayoutContainer(FVector2f(Width, Height));
	};

	TArray<ULexWidget*> UnlocatedWidgets;
	TSet<FIntPoint> OccupiedCells;
	for (ULexWidget* Child : Widget->GetChildren())
	{
		if (!IsValid(Child) || !Child->GetLayoutVisibleInHierarchy()) continue;
		if (Child->GetIgnoreLayout()) continue;
		ULexLayoutSelfGrid* GridSelf = Cast<ULexLayoutSelfGrid>(Child->GetLayoutSelf());
		if (!IsValid(GridSelf))
		{
			UnlocatedWidgets.Add(Child);
			continue;
		}
		const int32 Column = FMath::Clamp(GridSelf->GetColumnIndex(), 0, Columns.Num() - 1);
		const int32 Row = FMath::Clamp(GridSelf->GetRowIndex(), 0, Rows.Num() - 1);
		const int32 ColumnSpan = FMath::Clamp(GridSelf->GetColumnCount(), 1, Columns.Num() - Column);
		const int32 RowSpan = FMath::Clamp(GridSelf->GetRowCount(), 1, Rows.Num() - Row);
		for (int32 CellColumn = Column; CellColumn < Column + ColumnSpan; ++CellColumn)
		{
			for (int32 CellRow = Row; CellRow < Row + RowSpan; ++CellRow)
			{
				OccupiedCells.Add(FIntPoint(CellColumn, CellRow));
			}
		}
		ApplyCell(Child, Column, Row, ColumnSpan, RowSpan, GridSelf);
	}

	int32 UnlocatedIndex = 0;
	for (int32 Row = 0; Row < Rows.Num() && UnlocatedIndex < UnlocatedWidgets.Num(); ++Row)
	{
		for (int32 Column = 0; Column < Columns.Num() && UnlocatedIndex < UnlocatedWidgets.Num(); ++Column)
		{
			if (OccupiedCells.Contains(FIntPoint(Column, Row))) continue;
			ApplyCell(UnlocatedWidgets[UnlocatedIndex++], Column, Row, 1, 1, nullptr);
		}
	}
}

FVector2f ULexLayoutContainerGrid::GetLayoutPreferredSize()
{
	return FVector2f::ZeroVector;
}

FLexLayoutControlAnchorData ULexLayoutContainerGrid::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	FLexLayoutControlAnchorData Result;
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget) || !IsValid(TargetWidget) || Columns.IsEmpty() || Rows.IsEmpty()
		|| !Widget->GetChildren().Contains(TargetWidget)) return Result;
	if (TargetWidget->GetIgnoreLayout()) return Result;
	Result.bCanControlHorizontalPosition = true;
	Result.bCanControlVerticalPosition = true;
	if (IsValid(Cast<ULexLayoutSelfGrid>(TargetWidget->GetLayoutSelf())))
	{
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}
