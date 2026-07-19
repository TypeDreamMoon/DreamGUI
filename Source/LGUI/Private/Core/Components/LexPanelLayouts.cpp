// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexVisual.h"
#include "Framework/Application/SlateApplication.h"

ULexPanelSlot* ULexPanelLayoutBase::EnsureSlot(ULexWidget* Child) const
{
	return Child->GetPanelSlot() ? Child->GetPanelSlot() : Child->CreateNewPanelSlot<ULexPanelSlot>();
}

TArray<ULexWidget*> ULexPanelLayoutBase::CollectLayoutChildren() const
{
	TArray<ULexWidget*> Result;
	if (ULexWidget* Widget = GetWidget())
	{
		for (ULexWidget* Child : Widget->GetChildren())
		{
			if (!IsValid(Child) || !Child->GetLayoutVisibleInHierarchy())
			{
				continue;
			}
			if (ULexLayoutSelf* LayoutSelf = Child->GetLayoutSelf(); LayoutSelf && LayoutSelf->GetIgnoreLayoutContainer())
			{
				continue;
			}
			EnsureSlot(Child);
			Result.Add(Child);
		}
	}
	return Result;
}

FVector2D ULexPanelLayoutBase::GetDesiredSize(ULexWidget* Child) const
{
	TFunction<FVector2D(ULexWidget*, TSet<ULexWidget*>&)> GetIntrinsicSize;
	GetIntrinsicSize = [&GetIntrinsicSize](ULexWidget* Widget, TSet<ULexWidget*>& Visited) -> FVector2D
	{
		if (!IsValid(Widget) || Visited.Contains(Widget))
		{
			return FVector2D::ZeroVector;
		}
		Visited.Add(Widget);

		FVector2D Desired(-1.0, -1.0);
		if (ULexLayoutSelf* LayoutSelf = Widget->GetLayoutSelf())
		{
			const FVector2f LayoutDesired = LayoutSelf->GetLayoutPreferredSize();
			if (const ULexLayoutSelfFlexBox* FlexSelf = Cast<ULexLayoutSelfFlexBox>(LayoutSelf))
			{
				if (FlexSelf->GetPreferredWidth().bEnable && LayoutDesired.X >= 0.0f) Desired.X = LayoutDesired.X;
				if (FlexSelf->GetPreferredHeight().bEnable && LayoutDesired.Y >= 0.0f) Desired.Y = LayoutDesired.Y;
			}
			else
			{
				if (LayoutDesired.X >= 0.0f) Desired.X = LayoutDesired.X;
				if (LayoutDesired.Y >= 0.0f) Desired.Y = LayoutDesired.Y;
			}
		}
		if (ULexLayoutContainer* LayoutContainer = Widget->GetLayoutContainer())
		{
			const FVector2f LayoutDesired = LayoutContainer->GetLayoutPreferredSize();
			if (LayoutDesired.X > 0.0f) Desired.X = FMath::Max(Desired.X, static_cast<double>(LayoutDesired.X));
			if (LayoutDesired.Y > 0.0f) Desired.Y = FMath::Max(Desired.Y, static_cast<double>(LayoutDesired.Y));
		}
		if (ULexVisual* Visual = Widget->GetVisual())
		{
			const float VisualWidth = Visual->GetPreferredWidth();
			const float VisualHeight = Visual->GetPreferredHeight();
			if (VisualWidth >= 0.0f) Desired.X = FMath::Max(Desired.X, static_cast<double>(VisualWidth));
			if (VisualHeight >= 0.0f) Desired.Y = FMath::Max(Desired.Y, static_cast<double>(VisualHeight));
		}
		for (ULexWidget* ContentChild : Widget->GetChildren())
		{
			if (IsValid(ContentChild) && ContentChild->GetLayoutVisibleInHierarchy())
			{
				const FVector2D ContentDesired = GetIntrinsicSize(ContentChild, Visited);
				Desired.X = FMath::Max(Desired.X, ContentDesired.X);
				Desired.Y = FMath::Max(Desired.Y, ContentDesired.Y);
			}
		}
		if (Desired.X < 0.0) Desired.X = Widget->GetWidth();
		if (Desired.Y < 0.0) Desired.Y = Widget->GetHeight();
		return Desired;
	};

	TSet<ULexWidget*> Visited;
	return GetIntrinsicSize(Child, Visited);
}

void ULexPanelLayoutBase::ApplyChildRect(ULexWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill) const
{
	ULexWidget* Panel = GetWidget();
	ULexPanelSlot* Slot = EnsureSlot(Child);
	if (!Panel || !Slot)
	{
		return;
	}

	const FVector2D InnerPosition = Position + FVector2D(Slot->Padding.Left, Slot->Padding.Top);
	const FVector2D InnerSize(
		FMath::Max(0.0, Size.X - Slot->Padding.Left - Slot->Padding.Right),
		FMath::Max(0.0, Size.Y - Slot->Padding.Top - Slot->Padding.Bottom));
	const FVector2D Desired = GetDesiredSize(Child);

	double Width = InnerSize.X;
	double Height = InnerSize.Y;
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	if (!bForceFill && Slot->HorizontalAlignment != ELexPanelHorizontalAlignment::Fill)
	{
		Width = FMath::Min(Desired.X, InnerSize.X);
		switch (Slot->HorizontalAlignment)
		{
		case ELexPanelHorizontalAlignment::Center: Left += (InnerSize.X - Width) * 0.5; break;
		case ELexPanelHorizontalAlignment::Right: Left += InnerSize.X - Width; break;
		default: break;
		}
	}
	if (!bForceFill && Slot->VerticalAlignment != ELexPanelVerticalAlignment::Fill)
	{
		Height = FMath::Min(Desired.Y, InnerSize.Y);
		switch (Slot->VerticalAlignment)
		{
		case ELexPanelVerticalAlignment::Center: Top += (InnerSize.Y - Height) * 0.5; break;
		case ELexPanelVerticalAlignment::Bottom: Top += InnerSize.Y - Height; break;
		default: break;
		}
	}
	if (Slot->bAutoSize && !bForceFill)
	{
		Width = Desired.X;
		Height = Desired.Y;
	}

	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5), FVector2D(0.5), true, true);
	const FVector2f FinalSize(static_cast<float>(FMath::Max(0.0, Width)), static_cast<float>(FMath::Max(0.0, Height)));
	if (ULexLayoutSelfFlexBox* FlexSelf = Cast<ULexLayoutSelfFlexBox>(Child->GetLayoutSelf()))
	{
		FlexSelf->SetSizeByLayoutContainer(FinalSize, 0);
	}
	else
	{
		Child->SetWidth(FinalSize.X);
		Child->SetHeight(FinalSize.Y);
	}
	const FVector2D Pivot = Child->GetPivot();
	Child->SetAnchoredPosition(FVector2D(
		-Panel->GetWidth() * 0.5 + Left + Width * Pivot.X,
		Panel->GetHeight() * 0.5 - Top - Height * (1.0 - Pivot.Y)));
}

FLexLayoutControlAnchorData ULexPanelLayoutBase::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	FLexLayoutControlAnchorData Result;
	if (GetWidget() && GetWidget()->GetChildren().Contains(TargetWidget))
	{
		Result.bCanControlHorizontalPosition = true;
		Result.bCanControlVerticalPosition = true;
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}

void ULexPanelLayoutBase::RequestLayoutRefresh()
{
	ULexWidget::MarkLayoutForRebuild(GetWidget());
}

void ULexLayoutContainerCanvasPanel::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	if (bSortChildrenByZOrder)
	{
		Children.StableSort([](const ULexWidget& A, const ULexWidget& B)
		{
			return A.GetPanelSlot()->ZOrder < B.GetPanelSlot()->ZOrder;
		});
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (Children[Index]->GetSiblingIndex() != Index)
			{
				Children[Index]->SetSiblingIndex(Index);
			}
		}
	}
	PreferredSize = FVector2f(GetWidget()->GetSize());
}

void ULexLayoutContainerOverlay::CalculateLayout()
{
	const FVector2D AreaPosition(Padding.Left, Padding.Top);
	const FVector2D AreaSize(
		FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right),
		FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom));
	PreferredSize = FVector2f::ZeroVector;
	for (ULexWidget* Child : CollectLayoutChildren())
	{
		ApplyChildRect(Child, AreaPosition, AreaSize);
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		PreferredSize.X = FMath::Max(PreferredSize.X, static_cast<float>(Desired.X + Slot->Padding.Left + Slot->Padding.Right));
		PreferredSize.Y = FMath::Max(PreferredSize.Y, static_cast<float>(Desired.Y + Slot->Padding.Top + Slot->Padding.Bottom));
	}
	PreferredSize += FVector2f(Padding.Left + Padding.Right, Padding.Top + Padding.Bottom);
}

void ULexLayoutContainerStackBox::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	const bool bHorizontal = Orientation == ELexPanelOrientation::Horizontal;
	const float AvailablePrimary = bHorizontal
		? FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right)
		: FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom);
	const float AvailableSecondary = bHorizontal
		? FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom)
		: FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right);

	float AutoSize = FMath::Max(0, Children.Num() - 1) * Spacing;
	float DesiredPrimarySize = AutoSize;
	float FillWeight = 0.0f;
	float MaxSecondary = 0.0f;
	for (ULexWidget* Child : Children)
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		const float PrimaryPadding = bHorizontal ? Slot->Padding.Left + Slot->Padding.Right : Slot->Padding.Top + Slot->Padding.Bottom;
		const float SecondaryPadding = bHorizontal ? Slot->Padding.Top + Slot->Padding.Bottom : Slot->Padding.Left + Slot->Padding.Right;
		DesiredPrimarySize += static_cast<float>((bHorizontal ? Desired.X : Desired.Y) + PrimaryPadding);
		if (Slot->SizeRule == ELexPanelSizeRule::Fill)
		{
			FillWeight += Slot->FillWeight;
		}
		else
		{
			AutoSize += static_cast<float>((bHorizontal ? Desired.X : Desired.Y) + PrimaryPadding);
		}
		MaxSecondary = FMath::Max(MaxSecondary, static_cast<float>((bHorizontal ? Desired.Y : Desired.X) + SecondaryPadding));
	}

	const float FillSpace = FMath::Max(0.0f, AvailablePrimary - AutoSize);
	float Cursor = bHorizontal ? Padding.Left : Padding.Top;
	for (ULexWidget* Child : Children)
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		const float PrimaryPadding = bHorizontal ? Slot->Padding.Left + Slot->Padding.Right : Slot->Padding.Top + Slot->Padding.Bottom;
		const float Primary = Slot->SizeRule == ELexPanelSizeRule::Fill
			? (FillWeight > 0.0f ? FillSpace * Slot->FillWeight / FillWeight : 0.0f)
			: static_cast<float>((bHorizontal ? Desired.X : Desired.Y) + PrimaryPadding);
		if (bHorizontal)
		{
			ApplyChildRect(Child, FVector2D(Cursor, Padding.Top), FVector2D(Primary, AvailableSecondary));
		}
		else
		{
			ApplyChildRect(Child, FVector2D(Padding.Left, Cursor), FVector2D(AvailableSecondary, Primary));
		}
		Cursor += Primary + Spacing;
	}
	PreferredSize = bHorizontal
		? FVector2f(DesiredPrimarySize + Padding.Left + Padding.Right, MaxSecondary + Padding.Top + Padding.Bottom)
		: FVector2f(MaxSecondary + Padding.Left + Padding.Right, DesiredPrimarySize + Padding.Top + Padding.Bottom);
}

ULexLayoutContainerHorizontalBox::ULexLayoutContainerHorizontalBox()
{
	Orientation = ELexPanelOrientation::Horizontal;
}

ULexLayoutContainerVerticalBox::ULexLayoutContainerVerticalBox()
{
	Orientation = ELexPanelOrientation::Vertical;
}

void ULexLayoutContainerWrapBox::CalculateLayout()
{
	const float AvailableWidth = bExplicitWrapSize && WrapSize > 0.0f
		? WrapSize
		: FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right);
	float X = 0.0f;
	float Y = 0.0f;
	float LineHeight = 0.0f;
	float MaxWidth = 0.0f;
	for (ULexWidget* Child : CollectLayoutChildren())
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		const float ItemWidth = static_cast<float>(Desired.X + Slot->Padding.Left + Slot->Padding.Right);
		const float ItemHeight = static_cast<float>(Desired.Y + Slot->Padding.Top + Slot->Padding.Bottom);
		if (X > 0.0f && X + ItemWidth > AvailableWidth)
		{
			X = 0.0f;
			Y += LineHeight + Spacing.Y;
			LineHeight = 0.0f;
		}
		ApplyChildRect(Child, FVector2D(Padding.Left + X, Padding.Top + Y), FVector2D(ItemWidth, ItemHeight));
		X += ItemWidth + Spacing.X;
		LineHeight = FMath::Max(LineHeight, ItemHeight);
		MaxWidth = FMath::Max(MaxWidth, X - Spacing.X);
	}
	PreferredSize = FVector2f(MaxWidth + Padding.Left + Padding.Right, Y + LineHeight + Padding.Top + Padding.Bottom);
}

namespace LexPanelLayoutLocal
{
	static TArray<float> MakeWeights(const TArray<float>& Configured, int32 Count)
	{
		TArray<float> Result;
		Result.SetNum(Count);
		float Total = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result[Index] = Configured.IsValidIndex(Index) ? FMath::Max(0.0f, Configured[Index]) : 1.0f;
			Total += Result[Index];
		}
		if (Total <= UE_SMALL_NUMBER)
		{
			Result.Init(1.0f, Count);
		}
		return Result;
	}

	static float SumWeights(const TArray<float>& Weights, int32 Start, int32 Count)
	{
		float Result = 0.0f;
		for (int32 Index = Start; Index < FMath::Min(Start + Count, Weights.Num()); ++Index)
		{
			Result += Weights[Index];
		}
		return Result;
	}
}

void ULexLayoutContainerGridPanel::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	int32 ColumnCount = 1;
	int32 RowCount = 1;
	for (ULexWidget* Child : Children)
	{
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		ColumnCount = FMath::Max(ColumnCount, Slot->Column + Slot->ColumnSpan);
		RowCount = FMath::Max(RowCount, Slot->Row + Slot->RowSpan);
	}
	const TArray<float> Columns = LexPanelLayoutLocal::MakeWeights(ColumnFill, ColumnCount);
	const TArray<float> Rows = LexPanelLayoutLocal::MakeWeights(RowFill, RowCount);
	const float TotalColumnWeight = LexPanelLayoutLocal::SumWeights(Columns, 0, Columns.Num());
	const float TotalRowWeight = LexPanelLayoutLocal::SumWeights(Rows, 0, Rows.Num());
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right - Spacing.X * (ColumnCount - 1));
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom - Spacing.Y * (RowCount - 1));
	for (ULexWidget* Child : Children)
	{
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		const int32 Column = FMath::Clamp(Slot->Column, 0, ColumnCount - 1);
		const int32 Row = FMath::Clamp(Slot->Row, 0, RowCount - 1);
		const int32 ColumnSpan = FMath::Min(FMath::Max(1, Slot->ColumnSpan), ColumnCount - Column);
		const int32 RowSpan = FMath::Min(FMath::Max(1, Slot->RowSpan), RowCount - Row);
		const float X = Padding.Left + AvailableWidth * LexPanelLayoutLocal::SumWeights(Columns, 0, Column) / TotalColumnWeight + Spacing.X * Column;
		const float Y = Padding.Top + AvailableHeight * LexPanelLayoutLocal::SumWeights(Rows, 0, Row) / TotalRowWeight + Spacing.Y * Row;
		const float Width = AvailableWidth * LexPanelLayoutLocal::SumWeights(Columns, Column, ColumnSpan) / TotalColumnWeight + Spacing.X * (ColumnSpan - 1);
		const float Height = AvailableHeight * LexPanelLayoutLocal::SumWeights(Rows, Row, RowSpan) / TotalRowWeight + Spacing.Y * (RowSpan - 1);
		ApplyChildRect(Child, FVector2D(X, Y), FVector2D(Width, Height));
	}
	PreferredSize = FVector2f(GetWidget()->GetSize());
}

void ULexLayoutContainerUniformGridPanel::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	int32 ColumnCount = 1;
	int32 RowCount = 1;
	for (ULexWidget* Child : Children)
	{
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		ColumnCount = FMath::Max(ColumnCount, Slot->Column + Slot->ColumnSpan);
		RowCount = FMath::Max(RowCount, Slot->Row + Slot->RowSpan);
	}
	const float CellWidth = FMath::Max(MinDesiredSlotWidth, (GetWidget()->GetWidth() - Padding.Left - Padding.Right - Spacing.X * (ColumnCount - 1)) / ColumnCount);
	const float CellHeight = FMath::Max(MinDesiredSlotHeight, (GetWidget()->GetHeight() - Padding.Top - Padding.Bottom - Spacing.Y * (RowCount - 1)) / RowCount);
	for (ULexWidget* Child : Children)
	{
		const ULexPanelSlot* Slot = Child->GetPanelSlot();
		const int32 ColumnSpan = FMath::Max(1, Slot->ColumnSpan);
		const int32 RowSpan = FMath::Max(1, Slot->RowSpan);
		ApplyChildRect(Child,
			FVector2D(Padding.Left + Slot->Column * (CellWidth + Spacing.X), Padding.Top + Slot->Row * (CellHeight + Spacing.Y)),
			FVector2D(CellWidth * ColumnSpan + Spacing.X * (ColumnSpan - 1), CellHeight * RowSpan + Spacing.Y * (RowSpan - 1)));
	}
	PreferredSize = FVector2f(CellWidth * ColumnCount + Spacing.X * (ColumnCount - 1) + Padding.Left + Padding.Right,
		CellHeight * RowCount + Spacing.Y * (RowCount - 1) + Padding.Top + Padding.Bottom);
}

void ULexLayoutContainerSizeBox::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	FVector2D Desired = Children.IsEmpty() ? FVector2D::ZeroVector : GetDesiredSize(Children[0]);
	Desired += FVector2D(Padding.Left + Padding.Right, Padding.Top + Padding.Bottom);
	if (bOverrideWidth) Desired.X = WidthOverride;
	if (bOverrideHeight) Desired.Y = HeightOverride;
	Desired.X = FMath::Max(Desired.X, MinDesiredSize.X);
	Desired.Y = FMath::Max(Desired.Y, MinDesiredSize.Y);
	if (MaxDesiredSize.X > 0.0) Desired.X = FMath::Min(Desired.X, MaxDesiredSize.X);
	if (MaxDesiredSize.Y > 0.0) Desired.Y = FMath::Min(Desired.Y, MaxDesiredSize.Y);
	PreferredSize = FVector2f(Desired);
	if (!Children.IsEmpty())
	{
		ApplyChildRect(Children[0], FVector2D(Padding.Left, Padding.Top), FVector2D(
			FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right),
			FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom)));
	}
}

void ULexLayoutContainerScaleBox::CalculateLayout()
{
	TArray<ULexWidget*> Children = CollectLayoutChildren();
	if (Children.IsEmpty())
	{
		PreferredSize = FVector2f::ZeroVector;
		return;
	}
	ULexWidget* Child = Children[0];
	const FVector2D Desired = GetDesiredSize(Child);
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth() - Padding.Left - Padding.Right);
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight() - Padding.Top - Padding.Bottom);
	const float ScaleX = Desired.X > UE_SMALL_NUMBER ? AvailableWidth / Desired.X : 1.0f;
	const float ScaleY = Desired.Y > UE_SMALL_NUMBER ? AvailableHeight / Desired.Y : 1.0f;
	FVector2D Scale(1.0, 1.0);
	switch (Stretch)
	{
	case ELexScaleBoxStretch::Fill: Scale = FVector2D(ScaleX, ScaleY); break;
	case ELexScaleBoxStretch::ScaleToFit: Scale = FVector2D(FMath::Min(ScaleX, ScaleY)); break;
	case ELexScaleBoxStretch::ScaleToFill: Scale = FVector2D(FMath::Max(ScaleX, ScaleY)); break;
	case ELexScaleBoxStretch::ScaleToFitX: Scale = FVector2D(ScaleX); break;
	case ELexScaleBoxStretch::ScaleToFitY: Scale = FVector2D(ScaleY); break;
	case ELexScaleBoxStretch::UserSpecified: Scale = FVector2D(UserSpecifiedScale); break;
	default: break;
	}
	if (bIgnoreInheritedScale)
	{
		const FVector ParentScale = GetWidget()->GetWorldScale();
		if (!FMath::IsNearlyZero(ParentScale.Y)) Scale.X /= FMath::Abs(ParentScale.Y);
		if (!FMath::IsNearlyZero(ParentScale.Z)) Scale.Y /= FMath::Abs(ParentScale.Z);
	}
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5), FVector2D(0.5), true, true);
	Child->SetWidth(static_cast<float>(Desired.X));
	Child->SetHeight(static_cast<float>(Desired.Y));
	Child->SetAnchoredPosition(FVector2D((Padding.Left - Padding.Right) * 0.5f, (Padding.Bottom - Padding.Top) * 0.5f));
	FVector ChildScale = Child->GetRelativeScale();
	ChildScale.Y = Scale.X;
	ChildScale.Z = Scale.Y;
	Child->SetRelativeScale(ChildScale);
	PreferredSize = FVector2f(Desired.X + Padding.Left + Padding.Right, Desired.Y + Padding.Top + Padding.Bottom);
}

void ULexLayoutContainerSafeZone::CalculateLayout()
{
	FMargin PlatformPadding;
	if (bUsePlatformSafeZone && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetSafeZoneSize(PlatformPadding, GetWidget()->GetSize());
		if (!bPadLeft) PlatformPadding.Left = 0.0f;
		if (!bPadTop) PlatformPadding.Top = 0.0f;
		if (!bPadRight) PlatformPadding.Right = 0.0f;
		if (!bPadBottom) PlatformPadding.Bottom = 0.0f;
	}
	const FMargin Combined(
		SafePadding.Left + PlatformPadding.Left + GetWidget()->GetWidth() * NormalizedSafePadding.Left,
		SafePadding.Top + PlatformPadding.Top + GetWidget()->GetHeight() * NormalizedSafePadding.Top,
		SafePadding.Right + PlatformPadding.Right + GetWidget()->GetWidth() * NormalizedSafePadding.Right,
		SafePadding.Bottom + PlatformPadding.Bottom + GetWidget()->GetHeight() * NormalizedSafePadding.Bottom);
	for (ULexWidget* Child : CollectLayoutChildren())
	{
		ApplyChildRect(Child, FVector2D(Combined.Left, Combined.Top), FVector2D(
			FMath::Max(0.0f, GetWidget()->GetWidth() - Combined.Left - Combined.Right),
			FMath::Max(0.0f, GetWidget()->GetHeight() - Combined.Top - Combined.Bottom)));
	}
	PreferredSize = FVector2f(GetWidget()->GetSize());
}

ULexLayoutContainerScrollBox::ULexLayoutContainerScrollBox()
{
	Orientation = ELexPanelOrientation::Vertical;
}

void ULexLayoutContainerScrollBox::OnRegister()
{
	Super::OnRegister();
	if (GetWidget() && GetWidget()->GetClipping() == ELexWidgetClipping::Inherit)
	{
		GetWidget()->SetClipping(ELexWidgetClipping::ClipToBounds);
	}
}

void ULexLayoutContainerWidgetSwitcher::CalculateLayout()
{
	ULexWidget* Panel = GetWidget();
	if (!Panel)
	{
		return;
	}
	ActiveWidgetIndex = FMath::Clamp(ActiveWidgetIndex, 0, FMath::Max(0, Panel->GetChildrenCount() - 1));
	PreferredSize = FVector2f::ZeroVector;
	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		ULexWidget* Child = Panel->GetChildren()[Index];
		if (!IsValid(Child))
		{
			continue;
		}
		Child->SetVisibility(Index == ActiveWidgetIndex ? ELexWidgetVisibility::Visible : ELexWidgetVisibility::Collapsed);
		if (Index == ActiveWidgetIndex)
		{
			EnsureSlot(Child);
			ApplyChildRect(Child, FVector2D(Padding.Left, Padding.Top), FVector2D(
				FMath::Max(0.0f, Panel->GetWidth() - Padding.Left - Padding.Right),
				FMath::Max(0.0f, Panel->GetHeight() - Padding.Top - Padding.Bottom)));
			PreferredSize = FVector2f(GetDesiredSize(Child)) + FVector2f(Padding.Left + Padding.Right, Padding.Top + Padding.Bottom);
		}
	}
}

void ULexLayoutContainerWidgetSwitcher::SetActiveWidgetIndex(int32 Value)
{
	const int32 Clamped = FMath::Clamp(Value, 0, FMath::Max(0, GetWidget()->GetChildrenCount() - 1));
	if (ActiveWidgetIndex != Clamped)
	{
		ActiveWidgetIndex = Clamped;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

ULexWidget* ULexLayoutContainerWidgetSwitcher::GetActiveWidget() const
{
	return GetWidget() && GetWidget()->GetChildren().IsValidIndex(ActiveWidgetIndex)
		? GetWidget()->GetChildren()[ActiveWidgetIndex]
		: nullptr;
}
