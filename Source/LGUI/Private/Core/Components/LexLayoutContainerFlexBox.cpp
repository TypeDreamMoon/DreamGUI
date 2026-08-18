// Copyright 2025-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexWidget.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutContainer FlexBox"), STAT_LexLayoutContainerFlexBox, STATGROUP_LGUI);

namespace LexFlexBoxLocal
{
	static float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static float FiniteFloat(double Value, float Fallback = 0.0f)
	{
		if (!FMath::IsFinite(Value)) return Fallback;
		return static_cast<float>(FMath::Clamp(
			Value, -static_cast<double>(UE_MAX_FLT), static_cast<double>(UE_MAX_FLT)));
	}

	static float AddClamped(float A, float B)
	{
		if (!FMath::IsFinite(A))
		{
			return A > 0.0f ? UE_MAX_FLT : -UE_MAX_FLT;
		}
		const double Result = static_cast<double>(A) + static_cast<double>(B);
		return static_cast<float>(FMath::Clamp(Result, -static_cast<double>(UE_MAX_FLT), static_cast<double>(UE_MAX_FLT)));
	}

	static float AddNonNegativeClamped(float A, float B)
	{
		const double Result = static_cast<double>(NonNegativeFinite(A)) + NonNegativeFinite(B);
		return static_cast<float>(FMath::Min(Result, static_cast<double>(UE_MAX_FLT)));
	}

	static FMargin SanitizeMargin(const FMargin& Value)
	{
		return FMargin(
			NonNegativeFinite(Value.Left),
			NonNegativeFinite(Value.Top),
			NonNegativeFinite(Value.Right),
			NonNegativeFinite(Value.Bottom));
	}
}

void ULexLayoutContainerFlexBox::CalculateLayout()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutContainerFlexBox);

    if (!bIsLayoutDirty)return;
    bIsLayoutDirty = false;
    
    DoCalculate(true);

    //apply result
    {
        // These setters dirty the whole ancestor chain, starting with this container, which has already
        // consumed its dirty flag above. Scope the write-back so it only reaches the children and below.
        ULexWidget::FLayoutWriteScope WriteScope(GetWidget());
        for (auto& LayoutResult : CalculatedLayoutResultArray)
        {
            if (!IsValid(LayoutResult.Widget)) continue;
            if (IsValid(LayoutResult.LayoutSelf))
            {
                LayoutResult.LayoutSelf->SetFinalSizeByLayoutContainer(LayoutResult.Size);
                LayoutResult.Widget->SetAnchoredPositionAndSizeDelta(LayoutResult.AnchoredPos, FVector2D(LayoutResult.Size));
            }
            else
            {
                LayoutResult.Widget->SetAnchoredPosition(LayoutResult.AnchoredPos);
            }
        }
    }
    CalculatedLayoutResultArray.Reset();
}

void ULexLayoutContainerFlexBox::RefreshChildren()
{
    Children.Reset();
    auto Widget = GetWidget();
    if (!IsValid(Widget))
    {
        return;
    }
    for (auto& ChildWidget : Widget->GetChildren())
    {
		if (!IsValid(ChildWidget)) continue;
        if (!ChildWidget->GetLayoutVisibleInHierarchy())continue;
		if (ChildWidget->GetIgnoreLayout())continue;
        Children.Add(ChildWidget);

        auto AnchorMin = ChildWidget->GetAnchorMin();
        auto AnchorMax = ChildWidget->GetAnchorMax();
        if (AnchorMin.X != AnchorMax.X)//custom anchor not support
        {
            ChildWidget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
        }
        if (AnchorMin.Y != AnchorMax.Y)
        {
            ChildWidget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
        }
    }
}

void ULexLayoutContainerFlexBox::DoCalculate(bool bApplyResult)
{
#if WITH_EDITOR
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->IncreateLayoutCalculationCounter(FString::Printf(
			TEXT("%s_%p"), *this->GetPathDisplayName(GetWorld()), static_cast<void*>(this)));
	}
#endif

	RefreshChildren();
	CalculatedLayoutResultArray.Reset();

    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    int PrimaryAxis = bIsVertical ? 1 : 0;
    int SecondaryAxis = bIsVertical ? 0 : 1;
    FVector2f ThisWidgetSize;

    struct FChildSizes
    {
        FVector2f Min, Max, Preferred, Grow, Shrink;
    };
    TMap<ULexWidget*, FChildSizes> MapWidgetToChildSizes;
    auto GetChildSizes = [&](ULexWidget* ChildWidget)
    {
        auto ChildSizePtr = MapWidgetToChildSizes.Find(ChildWidget);
        if (!ChildSizePtr)
        {
            FChildSizes Value;
            if (auto ChildLayoutSelf = Cast<ULexLayoutSelfFlexBox>(ChildWidget->GetLayoutSelf()))
            {
                Value.Preferred = ChildLayoutSelf->GetLayoutPreferredSize();
                ChildLayoutSelf->GetLayoutMinMax(Value.Min, Value.Max);
				const FMargin Margin = LexFlexBoxLocal::SanitizeMargin(ChildLayoutSelf->GetMargin());
				const FVector2f MarginSize(
					LexFlexBoxLocal::AddNonNegativeClamped(Margin.Left, Margin.Right),
					LexFlexBoxLocal::AddNonNegativeClamped(Margin.Top, Margin.Bottom));
				Value.Preferred.X = LexFlexBoxLocal::AddClamped(LexFlexBoxLocal::NonNegativeFinite(Value.Preferred.X), MarginSize.X);
				Value.Preferred.Y = LexFlexBoxLocal::AddClamped(LexFlexBoxLocal::NonNegativeFinite(Value.Preferred.Y), MarginSize.Y);
				Value.Min.X = LexFlexBoxLocal::AddClamped(Value.Min.X, MarginSize.X);
				Value.Min.Y = LexFlexBoxLocal::AddClamped(Value.Min.Y, MarginSize.Y);
				Value.Max.X = LexFlexBoxLocal::AddClamped(Value.Max.X, MarginSize.X);
				Value.Max.Y = LexFlexBoxLocal::AddClamped(Value.Max.Y, MarginSize.Y);
				Value.Preferred.X = FMath::Max(MarginSize.X, Value.Preferred.X);
				Value.Preferred.Y = FMath::Max(MarginSize.Y, Value.Preferred.Y);
				Value.Min.X = FMath::Max(MarginSize.X, Value.Min.X);
				Value.Min.Y = FMath::Max(MarginSize.Y, Value.Min.Y);
				Value.Max.X = FMath::Max(Value.Min.X, Value.Max.X);
				Value.Max.Y = FMath::Max(Value.Min.Y, Value.Max.Y);
				Value.Preferred.X = FMath::Clamp(Value.Preferred.X, Value.Min.X, Value.Max.X);
				Value.Preferred.Y = FMath::Clamp(Value.Preferred.Y, Value.Min.Y, Value.Max.Y);
                if (PrimaryAxis == 0)
                {
                    Value.Grow = FVector2f(LexFlexBoxLocal::NonNegativeFinite(ChildLayoutSelf->GetGrowForLayoutContainer(0)), 0);
                    Value.Shrink = FVector2f(LexFlexBoxLocal::NonNegativeFinite(ChildLayoutSelf->GetShrinkForLayoutContainer(0)), 0);
                }
                else
                {
                    Value.Grow = FVector2f(0, LexFlexBoxLocal::NonNegativeFinite(ChildLayoutSelf->GetGrowForLayoutContainer(1)));
                    Value.Shrink = FVector2f(0, LexFlexBoxLocal::NonNegativeFinite(ChildLayoutSelf->GetShrinkForLayoutContainer(1)));
                }
            }
            else// child does not have LayoutSelf, so use its current arranged size
            {
                Value.Min = Value.Max = Value.Preferred = FVector2f(
					LexFlexBoxLocal::NonNegativeFinite(ChildWidget->GetWidth()),
					LexFlexBoxLocal::NonNegativeFinite(ChildWidget->GetHeight()));
                Value.Grow = Value.Shrink = FVector2f(0, 0);
            }
            ChildSizePtr = &MapWidgetToChildSizes.Add(ChildWidget, Value);
        }
        return ChildSizePtr;
    };
	auto SetChildPositionAndSize = [&](ULexWidget* ChildWidget, FVector2f Pos, FVector2f AreaSize, int PrimaryAxis, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY)
    {
        auto ChildLayoutSelf = Cast<ULexLayoutSelfFlexBox>(ChildWidget->GetLayoutSelf());
		const bool bStretchChild = SecondaryLineAlignment == ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch
			&& IsValid(ChildLayoutSelf)
			&& ChildLayoutSelf->GetSecondaryAxisSizeCanStretchByLayoutContainer(SecondaryAxis);
		const float OuterSecondarySize = bStretchChild
			? LexFlexBoxLocal::NonNegativeFinite(AreaSize[SecondaryAxis])
			: LexFlexBoxLocal::NonNegativeFinite(SecondaryPreferred);
		const float SecondaryAvailable = LexFlexBoxLocal::NonNegativeFinite(AreaSize[SecondaryAxis]);
		float SecondaryOffset = 0.0f;
        switch (SecondaryLineAlignment)
        {
		case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch://stretch
			break;
		case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Start:
			if (Wrap == ELexLayoutFlexBoxWrapType::WrapReverse)
			{
				SecondaryOffset = SecondaryAvailable - OuterSecondarySize;
			}
			break;
        case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Center:
			SecondaryOffset = (SecondaryAvailable - OuterSecondarySize) * 0.5f;
            break;
		case ELexLayoutFlexBoxSecondaryAxisLineAlignment::End:
			if (Wrap != ELexLayoutFlexBoxWrapType::WrapReverse)
			{
				SecondaryOffset = SecondaryAvailable - OuterSecondarySize;
			}
            break;
        }

        if (ReverseX)
        {
            Pos.X -= AreaSize.X;
        }
        if (ReverseY)
        {
            Pos.Y -= AreaSize.Y;
        }
		Pos[SecondaryAxis] += SecondaryOffset;
		AreaSize[SecondaryAxis] = OuterSecondarySize;
		AreaSize.X = LexFlexBoxLocal::NonNegativeFinite(AreaSize.X);
		AreaSize.Y = LexFlexBoxLocal::NonNegativeFinite(AreaSize.Y);

		FVector2f ContentOrigin = Pos;
		FVector2f ContentSize = AreaSize;
		if (IsValid(ChildLayoutSelf))
        {
			const FMargin Margin = LexFlexBoxLocal::SanitizeMargin(ChildLayoutSelf->GetMargin());
			ContentOrigin.X = LexFlexBoxLocal::AddClamped(ContentOrigin.X, Margin.Left);
			ContentOrigin.Y = LexFlexBoxLocal::AddClamped(ContentOrigin.Y, Margin.Top);
			const float HorizontalMargin = LexFlexBoxLocal::AddNonNegativeClamped(Margin.Left, Margin.Right);
			const float VerticalMargin = LexFlexBoxLocal::AddNonNegativeClamped(Margin.Top, Margin.Bottom);
			ContentSize.X = FMath::Max(0.0f, ContentSize.X - HorizontalMargin);
			ContentSize.Y = FMath::Max(0.0f, ContentSize.Y - VerticalMargin);
        }
		const FVector2D Pivot = ChildWidget->GetPivot();
		const FVector2D AnchorMin = ChildWidget->GetAnchorMin();
		const double PivotX = FMath::IsFinite(Pivot.X) ? Pivot.X : 0.5;
		const double PivotY = FMath::IsFinite(Pivot.Y) ? Pivot.Y : 0.5;
		const double AnchorX = FMath::IsFinite(AnchorMin.X) ? AnchorMin.X : 0.5;
		const double AnchorY = FMath::IsFinite(AnchorMin.Y) ? AnchorMin.Y : 0.5;
		const float AnchoredPositionX = LexFlexBoxLocal::FiniteFloat(
			static_cast<double>(ContentOrigin.X) + static_cast<double>(ContentSize.X) * PivotX
			- static_cast<double>(ThisWidgetSize.X) * AnchorX);
		const float AnchoredPositionY = LexFlexBoxLocal::FiniteFloat(
			-(static_cast<double>(ContentOrigin.Y) + static_cast<double>(ContentSize.Y) * (1.0 - PivotY))
			+ static_cast<double>(ThisWidgetSize.Y) * (1.0 - AnchorY));

        FCalculatedLayoutResult CalculatedLayout;
        CalculatedLayout.Widget = ChildWidget;
        CalculatedLayout.AnchoredPos = FVector2D(AnchoredPositionX, AnchoredPositionY);
        CalculatedLayout.LayoutSelf = ChildLayoutSelf;
		CalculatedLayout.Size = ContentSize;
		CalculatedLayoutResultArray.Add(CalculatedLayout);
    };

    bool bAllowWrap = Wrap == ELexLayoutFlexBoxWrapType::Wrap || Wrap == ELexLayoutFlexBoxWrapType::WrapReverse;
    
    auto ChildrenCount = Children.Num();
    //calculate lines
    LineDataArray.Reset();
    auto CurrentLine = FLineData();
    TotalPreferredSize = FVector2f(0, 0);
    
    const auto Gap = FVector2f(
		LexFlexBoxLocal::NonNegativeFinite(WidthGap),
		LexFlexBoxLocal::NonNegativeFinite(HeightGap));
    FVector2f ContainerSize;
    auto Widget = GetWidget();
	if (!IsValid(Widget)) return;
	const FMargin SafePadding = LexFlexBoxLocal::SanitizeMargin(Padding);
    if (auto LayoutSelf = Widget->GetLayoutSelf())
    {
        ContainerSize = LayoutSelf->GetLayoutFinalSize();
    }
    else
    {
        ContainerSize = FVector2f(Widget->GetWidth(), Widget->GetHeight());
    }
	ContainerSize.X = LexFlexBoxLocal::NonNegativeFinite(ContainerSize.X);
	ContainerSize.Y = LexFlexBoxLocal::NonNegativeFinite(ContainerSize.Y);
    ThisWidgetSize = ContainerSize;
	const float HorizontalPadding = LexFlexBoxLocal::AddNonNegativeClamped(SafePadding.Left, SafePadding.Right);
	const float VerticalPadding = LexFlexBoxLocal::AddNonNegativeClamped(SafePadding.Top, SafePadding.Bottom);
	ContainerSize.Y = FMath::Max(0.0f, ContainerSize.Y - VerticalPadding);
	ContainerSize.X = FMath::Max(0.0f, ContainerSize.X - HorizontalPadding);

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        auto ChildSizes = GetChildSizes(Child);
        if (bAllowWrap && !CurrentLine.Children.IsEmpty()
            && CurrentLine.TotalPreferred[PrimaryAxis] + ChildSizes->Preferred[PrimaryAxis] > ContainerSize[PrimaryAxis])
        {
            CurrentLine.TotalMin[PrimaryAxis] -= Gap[PrimaryAxis];
            CurrentLine.TotalMax[PrimaryAxis] -= Gap[PrimaryAxis];
            CurrentLine.TotalPreferred[PrimaryAxis] -= Gap[PrimaryAxis];
            LineDataArray.Add(CurrentLine);
            CurrentLine = FLineData();
        }

        CurrentLine.TotalMin[PrimaryAxis] += ChildSizes->Min[PrimaryAxis] + Gap[PrimaryAxis];
        CurrentLine.TotalMax[PrimaryAxis] += ChildSizes->Max[PrimaryAxis] + Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += ChildSizes->Preferred[PrimaryAxis] + Gap[PrimaryAxis];
        CurrentLine.TotalGrow[PrimaryAxis] += ChildSizes->Grow[PrimaryAxis];
        CurrentLine.TotalShrink[PrimaryAxis] += ChildSizes->Shrink[PrimaryAxis];

        //primary axis size: try not wrap content and get size
        TotalPreferredSize[PrimaryAxis] += ChildSizes->Preferred[PrimaryAxis] + Gap[PrimaryAxis];

        //secondary axis
        CurrentLine.TotalMin[SecondaryAxis] = FMath::Max(ChildSizes->Min[SecondaryAxis], CurrentLine.TotalMin[SecondaryAxis]);
        CurrentLine.TotalMax[SecondaryAxis] = FMath::Max(ChildSizes->Max[SecondaryAxis], CurrentLine.TotalMax[SecondaryAxis]);
        CurrentLine.TotalPreferred[SecondaryAxis] = FMath::Max(ChildSizes->Preferred[SecondaryAxis], CurrentLine.TotalPreferred[SecondaryAxis]);
        CurrentLine.TotalGrow[SecondaryAxis] = FMath::Max(ChildSizes->Grow[SecondaryAxis], CurrentLine.TotalGrow[SecondaryAxis]);
        CurrentLine.TotalShrink[SecondaryAxis] = FMath::Max(ChildSizes->Shrink[SecondaryAxis], CurrentLine.TotalShrink[SecondaryAxis]);

        CurrentLine.Children.Add(Child);
    }
    if (ChildrenCount > 0)
    {
        CurrentLine.TotalMin[PrimaryAxis] -= Gap[PrimaryAxis];
		CurrentLine.TotalMax[PrimaryAxis] -= Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] -= Gap[PrimaryAxis];
        LineDataArray.Add(CurrentLine);

        TotalPreferredSize[PrimaryAxis] -= Gap[PrimaryAxis];
    }

    //secondary axis property
    float SecondaryTotalMin = 0, SecondaryTotalMax = 0, SecondaryTotalPreferred = 0, SecondaryTotalGrow = 0, SecondaryTotalShrink = 0;
    for (int LineIndex = 0; LineIndex < LineDataArray.Num(); LineIndex++)
    {
        auto& LineData = LineDataArray[LineIndex];
        SecondaryTotalMin += LineData.TotalMin[SecondaryAxis] + Gap[SecondaryAxis];
        SecondaryTotalMax += LineData.TotalMax[SecondaryAxis] + Gap[SecondaryAxis];
        SecondaryTotalPreferred += LineData.TotalPreferred[SecondaryAxis] + Gap[SecondaryAxis];
        SecondaryTotalGrow += LineData.TotalGrow[SecondaryAxis];
        SecondaryTotalShrink += LineData.TotalShrink[SecondaryAxis];
    }
    if (ChildrenCount > 0)
    {
        SecondaryTotalMin -= Gap[SecondaryAxis];
        SecondaryTotalMax -= Gap[SecondaryAxis];
        SecondaryTotalPreferred -= Gap[SecondaryAxis];
    }
    //secondary axis size: accumulate secondary sizes
    TotalPreferredSize[SecondaryAxis] += SecondaryTotalPreferred;
	TotalPreferredSize[PrimaryAxis] = LexFlexBoxLocal::AddClamped(
		TotalPreferredSize[PrimaryAxis], PrimaryAxis == 0 ? HorizontalPadding : VerticalPadding);
	TotalPreferredSize[SecondaryAxis] = LexFlexBoxLocal::AddClamped(
		TotalPreferredSize[SecondaryAxis], SecondaryAxis == 0 ? HorizontalPadding : VerticalPadding);
	TotalPreferredSize.X = LexFlexBoxLocal::NonNegativeFinite(TotalPreferredSize.X);
	TotalPreferredSize.Y = LexFlexBoxLocal::NonNegativeFinite(TotalPreferredSize.Y);
	if (!bApplyResult)return;

	if (LineDataArray.IsEmpty())return;
    bool bReverseHorizontal = Direction == ELexLayoutFlexBoxDirectionType::HorizontalReverse;
    bool bReverseVertical = Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    bool bReverse = bReverseHorizontal || bReverseVertical;
    
    float SecondarySurplusSpace = ContainerSize[SecondaryAxis] - SecondaryTotalPreferred;
    float SecondarySpaceGap = 0;//Space between two items beside gap value
    float SecondaryStretchedExtraSize = 0;

    FVector2f PosOffset(0,0);//default position use left top as origin
    PosOffset[SecondaryAxis] = SecondaryAxis == 0 ? SafePadding.Left : SafePadding.Top;
    // Start, Center and End are well defined when the lines overflow the container - the block just moves
    // the other way, exactly as the primary axis already does further down with -Deficit - so they are not
    // gated on surplus. The distributing modes genuinely have nothing to distribute below zero, and CSS
    // stretch only ever grows, so those stay gated. Before this, an overflowing cross axis silently
    // collapsed Center and End to Start: "it was centred until I added one more row".
    switch (SecondaryAlignment)
    {
	case ELexLayoutFlexBoxSecondaryAxisAlignment::Start:
		if (Wrap == ELexLayoutFlexBoxWrapType::WrapReverse)
		{
			PosOffset[SecondaryAxis] += SecondarySurplusSpace;
		}
		break;
    case ELexLayoutFlexBoxSecondaryAxisAlignment::Center:
        PosOffset[SecondaryAxis] += SecondarySurplusSpace * 0.5f;
        break;
	case ELexLayoutFlexBoxSecondaryAxisAlignment::End:
		if (Wrap != ELexLayoutFlexBoxWrapType::WrapReverse)
		{
			PosOffset[SecondaryAxis] += SecondarySurplusSpace;
		}
        break;
    case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceBetween:
        //no offset needed
        if (SecondarySurplusSpace > 0 && LineDataArray.Num() > 1)
        {
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() - 1);
        }
        break;
    case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceAround:
        //half space offset
        if (SecondarySurplusSpace > 0)
        {
            SecondarySpaceGap = SecondarySurplusSpace / FMath::Max(1, LineDataArray.Num());
            PosOffset[SecondaryAxis] += SecondarySpaceGap * 0.5f;
        }
        break;
    case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceEvenly:
        //space offset
        if (SecondarySurplusSpace > 0)
        {
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() + 1);
            PosOffset[SecondaryAxis] += SecondarySpaceGap;
        }
        break;
    case ELexLayoutFlexBoxSecondaryAxisAlignment::Stretch:
        if (SecondarySurplusSpace > 0)
        {
            SecondaryStretchedExtraSize = SecondarySurplusSpace / FMath::Max(1, LineDataArray.Num());
        }
        break;
    }
    
    for (int LineIndex = 0; LineIndex < LineDataArray.Num(); LineIndex++)
    {
        auto Index = (Wrap == ELexLayoutFlexBoxWrapType::WrapReverse) ? (LineDataArray.Num() - 1 - LineIndex) : LineIndex;
        auto& LineData = LineDataArray[Index];
        if (bReverse)
        {
            PosOffset[PrimaryAxis] = (PrimaryAxis == 0 ? SafePadding.Left : SafePadding.Top) + ContainerSize[PrimaryAxis];
        }
        else
        {
            PosOffset[PrimaryAxis] = PrimaryAxis == 0 ? SafePadding.Left : SafePadding.Top;
        }
        TArray<double> FinalPrimarySizes;
        FinalPrimarySizes.Reserve(LineData.Children.Num());
        double OccupiedPrimarySize = static_cast<double>(Gap[PrimaryAxis]) * FMath::Max(0, LineData.Children.Num() - 1);
        for (ULexWidget* Child : LineData.Children)
        {
			const double Preferred = LexFlexBoxLocal::NonNegativeFinite(GetChildSizes(Child)->Preferred[PrimaryAxis]);
            FinalPrimarySizes.Add(Preferred);
            OccupiedPrimarySize += Preferred;
        }

        double RemainingSpace = static_cast<double>(ContainerSize[PrimaryAxis]) - OccupiedPrimarySize;
        auto DistributeSpace = [&](bool bGrow)
        {
            double Amount = FMath::Abs(RemainingSpace);
            TArray<uint8> ActiveItems;
            ActiveItems.Init(1, LineData.Children.Num());
            for (int32 Iteration = 0; Iteration < LineData.Children.Num() && Amount > UE_SMALL_NUMBER; ++Iteration)
            {
                double TotalWeight = 0.0;
                for (int32 ChildIndex = 0; ChildIndex < LineData.Children.Num(); ++ChildIndex)
                {
                    if (!ActiveItems[ChildIndex]) continue;
                    const FChildSizes* ChildSizes = GetChildSizes(LineData.Children[ChildIndex]);
                    const double Weight = bGrow ? ChildSizes->Grow[PrimaryAxis] : ChildSizes->Shrink[PrimaryAxis];
                    const double Capacity = bGrow
                        ? static_cast<double>(ChildSizes->Max[PrimaryAxis]) - FinalPrimarySizes[ChildIndex]
                        : static_cast<double>(FinalPrimarySizes[ChildIndex]) - ChildSizes->Min[PrimaryAxis];
                    if (Weight > UE_SMALL_NUMBER && Capacity > UE_SMALL_NUMBER)
                    {
                        TotalWeight += Weight;
                    }
                    else
                    {
                        ActiveItems[ChildIndex] = 0;
                    }
                }
                if (TotalWeight <= UE_SMALL_NUMBER) break;

                const double AmountAtStart = Amount;
                double AppliedAmount = 0.0;
                for (int32 ChildIndex = 0; ChildIndex < LineData.Children.Num(); ++ChildIndex)
                {
                    if (!ActiveItems[ChildIndex]) continue;
                    const FChildSizes* ChildSizes = GetChildSizes(LineData.Children[ChildIndex]);
                    const double Weight = bGrow ? ChildSizes->Grow[PrimaryAxis] : ChildSizes->Shrink[PrimaryAxis];
                    const double Capacity = bGrow
                        ? static_cast<double>(ChildSizes->Max[PrimaryAxis]) - FinalPrimarySizes[ChildIndex]
                        : static_cast<double>(FinalPrimarySizes[ChildIndex]) - ChildSizes->Min[PrimaryAxis];
					const double Requested = AmountAtStart * (Weight / TotalWeight);
					const double Applied = FMath::Min(Requested, FMath::Max(0.0, Capacity));
					const double NewSize = FinalPrimarySizes[ChildIndex] + (bGrow ? Applied : -Applied);
					FinalPrimarySizes[ChildIndex] = FMath::Clamp(NewSize, 0.0, static_cast<double>(UE_MAX_FLT));
                    AppliedAmount += Applied;
                    if (Applied + UE_SMALL_NUMBER >= Capacity) ActiveItems[ChildIndex] = 0;
                }
                if (AppliedAmount <= UE_SMALL_NUMBER) break;
                Amount = FMath::Max(0.0, Amount - AppliedAmount);
            }
        };
        if (RemainingSpace > UE_SMALL_NUMBER)
        {
            DistributeSpace(true);
        }
        else if (RemainingSpace < -UE_SMALL_NUMBER)
        {
            DistributeSpace(false);
        }

        OccupiedPrimarySize = static_cast<double>(Gap[PrimaryAxis]) * FMath::Max(0, LineData.Children.Num() - 1);
		for (double Size : FinalPrimarySizes) OccupiedPrimarySize += Size;
		RemainingSpace = static_cast<double>(ContainerSize[PrimaryAxis]) - OccupiedPrimarySize;
		const float FiniteRemainingSpace = static_cast<float>(FMath::Clamp(
			RemainingSpace, -static_cast<double>(UE_MAX_FLT), static_cast<double>(UE_MAX_FLT)));

        //calculate alignment after grow/shrink constraints have settled
        float PrimarySpaceGap = 0;
        if (FiniteRemainingSpace > UE_SMALL_NUMBER)
        {
            switch (PrimaryAlignment)
            {
            case ELexLayoutFlexBoxPrimaryAxisAlignment::Start: break;
            case ELexLayoutFlexBoxPrimaryAxisAlignment::Center:
                PosOffset[PrimaryAxis] += (bReverse ? -FiniteRemainingSpace : FiniteRemainingSpace) * 0.5f;
                break;
            case ELexLayoutFlexBoxPrimaryAxisAlignment::End:
                PosOffset[PrimaryAxis] += bReverse ? -FiniteRemainingSpace : FiniteRemainingSpace;
                break;
            case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceBetween:
                if (LineData.Children.Num() > 1) PrimarySpaceGap = FiniteRemainingSpace / (LineData.Children.Num() - 1);
                break;
            case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceAround:
                PrimarySpaceGap = FiniteRemainingSpace / FMath::Max(1, LineData.Children.Num());
                PosOffset[PrimaryAxis] += (bReverse ? -PrimarySpaceGap : PrimarySpaceGap) * 0.5f;
                break;
            case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceEvenly:
                PrimarySpaceGap = FiniteRemainingSpace / (LineData.Children.Num() + 1);
                PosOffset[PrimaryAxis] += bReverse ? -PrimarySpaceGap : PrimarySpaceGap;
                break;
            }
        }
        else if (FiniteRemainingSpace < -UE_SMALL_NUMBER)
        {
            const float Deficit = -FiniteRemainingSpace;
            if (PrimaryAlignment == ELexLayoutFlexBoxPrimaryAxisAlignment::Center)
            {
                PosOffset[PrimaryAxis] += (bReverse ? Deficit : -Deficit) * 0.5f;
            }
            else if (PrimaryAlignment == ELexLayoutFlexBoxPrimaryAxisAlignment::End)
            {
                PosOffset[PrimaryAxis] += bReverse ? Deficit : -Deficit;
            }
        }

        auto CurrentLinePosOffset = PosOffset;
        float SecondaryAxisLineSize = LineData.TotalPreferred[SecondaryAxis];
        if (LineDataArray.Num() == 1)
        {
            // A single-line flex container's line cross size IS the container's inner cross size, and
            // does not depend on align-content - that property distributes *lines*, and one line has
            // nothing to distribute. Without this the line was only as deep as its tallest child, so
            // SecondaryLineAlignment (align-items) Stretch stretched children to their tallest sibling
            // instead of to the container, and appeared to do nothing at all until the unrelated
            // SecondaryAlignment (align-content) was also set to Stretch. ContainerSize already has
            // padding removed.
            SecondaryAxisLineSize = ContainerSize[SecondaryAxis];
        }
        else
        {
            SecondaryAxisLineSize += SecondaryStretchedExtraSize;
        }

        for (int ChildIndex = 0; ChildIndex < LineData.Children.Num(); ChildIndex++)
        {
            auto& Child = LineData.Children[ChildIndex];
            FVector2f AreaSize(0,0);
            auto ChildSizes = GetChildSizes(Child);
			AreaSize[PrimaryAxis] = static_cast<float>(FMath::Clamp(
				FinalPrimarySizes[ChildIndex], 0.0, static_cast<double>(UE_MAX_FLT)));
            //calculate secondary size
            float SecondaryPreferredSize = 0;
            {
                AreaSize[SecondaryAxis] = SecondaryAxisLineSize;
                SecondaryPreferredSize = ChildSizes->Preferred[SecondaryAxis];
            }
            
            SetChildPositionAndSize(Child, CurrentLinePosOffset, AreaSize, PrimaryAxis, SecondaryAxis, SecondaryPreferredSize, bReverseHorizontal, bReverseVertical);
            if (bReverse)
            {
                CurrentLinePosOffset[PrimaryAxis] -= AreaSize[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
            else
            {
                CurrentLinePosOffset[PrimaryAxis] += AreaSize[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
        }
        PosOffset[PrimaryAxis] = CurrentLinePosOffset[PrimaryAxis];
        PosOffset[SecondaryAxis] += SecondaryAxisLineSize + Gap[SecondaryAxis] + SecondarySpaceGap;
    }
}

void ULexLayoutContainerFlexBox::CalculatePreferredSize()
{
    DoCalculate(false);

#if 0
    auto GetChildPreferredSize = [&](ULexWidget* ChildWidget)
    {
		if (!IsValid(ChildWidget)) return FVector2f::ZeroVector;
        if (auto ChildLayoutSelf = Cast<ULexLayoutSelfFlexBox>(ChildWidget->GetLayoutSelf()))
        {
			auto Result = ChildLayoutSelf->GetLayoutPreferredSize();
			const FMargin Margin = LexFlexBoxLocal::SanitizeMargin(ChildLayoutSelf->GetMargin());
			Result.X = LexFlexBoxLocal::AddClamped(
				LexFlexBoxLocal::NonNegativeFinite(Result.X), LexFlexBoxLocal::AddNonNegativeClamped(Margin.Left, Margin.Right));
			Result.Y = LexFlexBoxLocal::AddClamped(
				LexFlexBoxLocal::NonNegativeFinite(Result.Y), LexFlexBoxLocal::AddNonNegativeClamped(Margin.Top, Margin.Bottom));
			return Result;
        }
        else
        {
			return FVector2f(
				LexFlexBoxLocal::NonNegativeFinite(ChildWidget->GetWidth()),
				LexFlexBoxLocal::NonNegativeFinite(ChildWidget->GetHeight()));
        }
    };

    TotalPreferredSize = FVector2f(0,0);
    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    int PrimaryAxis = bIsVertical ? 1 : 0;
    int SecondaryAxis = bIsVertical ? 0 : 1;
    const auto Gap = FVector2f(
		LexFlexBoxLocal::NonNegativeFinite(WidthGap),
		LexFlexBoxLocal::NonNegativeFinite(HeightGap));
	const FMargin SafePadding = LexFlexBoxLocal::SanitizeMargin(Padding);
    auto ChildrenCount = Children.Num();
    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        auto ChildSize = GetChildPreferredSize(Child);
        TotalPreferredSize[PrimaryAxis] += ChildSize[PrimaryAxis];
        TotalPreferredSize[PrimaryAxis] += Gap[PrimaryAxis];
        TotalPreferredSize[SecondaryAxis] = FMath::Max(ChildSize[SecondaryAxis], TotalPreferredSize[SecondaryAxis]);
    }
    if (ChildrenCount > 0)
    {
        TotalPreferredSize[PrimaryAxis] -= Gap[PrimaryAxis];
    }
	const float HorizontalPadding = LexFlexBoxLocal::AddNonNegativeClamped(SafePadding.Left, SafePadding.Right);
	const float VerticalPadding = LexFlexBoxLocal::AddNonNegativeClamped(SafePadding.Top, SafePadding.Bottom);
	TotalPreferredSize[PrimaryAxis] = LexFlexBoxLocal::AddClamped(
		TotalPreferredSize[PrimaryAxis], PrimaryAxis == 0 ? HorizontalPadding : VerticalPadding);
	TotalPreferredSize[SecondaryAxis] = LexFlexBoxLocal::AddClamped(
		TotalPreferredSize[SecondaryAxis], SecondaryAxis == 0 ? HorizontalPadding : VerticalPadding);
	TotalPreferredSize.X = LexFlexBoxLocal::NonNegativeFinite(TotalPreferredSize.X);
	TotalPreferredSize.Y = LexFlexBoxLocal::NonNegativeFinite(TotalPreferredSize.Y);
#endif
}

FLexLayoutControlAnchorData ULexLayoutContainerFlexBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget)const
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
	if (!IsValid(ThisWidget) || !IsValid(TargetWidget)) return Result;
    if (ThisWidget == TargetWidget)//self
    {
        
    }
    else if (ThisWidget->GetChildren().Contains(TargetWidget))//child
    {
        bool bIgnoreLayout = TargetWidget->GetIgnoreLayout();
        if (!bIgnoreLayout)
        {
            Result.bCanControlHorizontalPosition = true;
            Result.bCanControlVerticalPosition = true;
        }
    }
    return Result;
}

#if WITH_EDITOR
void ULexLayoutContainerFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	WidthGap = FMath::IsFinite(WidthGap) ? FMath::Max(0.0f, WidthGap) : 0.0f;
	HeightGap = FMath::IsFinite(HeightGap) ? FMath::Max(0.0f, HeightGap) : 0.0f;
	Padding = LexFlexBoxLocal::SanitizeMargin(Padding);
}
#endif

FVector2f ULexLayoutContainerFlexBox::GetLayoutPreferredSize()
{
    CalculatePreferredSize();
    return this->TotalPreferredSize;
}

void ULexLayoutContainerFlexBox::SetDirection(ELexLayoutFlexBoxDirectionType Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetWrap(ELexLayoutFlexBoxWrapType Value)
{
    if (Wrap != Value)
    {
        Wrap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetPrimaryAlignment(ELexLayoutFlexBoxPrimaryAxisAlignment Value)
{
    if (PrimaryAlignment != Value)
    {
        PrimaryAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment Value)
{
    if (SecondaryAlignment != Value)
    {
        SecondaryAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment Value)
{
    if (SecondaryLineAlignment != Value)
    {
        SecondaryLineAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetWidthGap(float Value)
{
	Value = FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    if (WidthGap != Value)
    {
        WidthGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetHeightGap(float Value)
{
	Value = FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    if (HeightGap != Value)
    {
        HeightGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetPadding(const FMargin& Value)
{
	const FMargin Sanitized = LexFlexBoxLocal::SanitizeMargin(Value);
    if (Padding != Sanitized)
    {
		Padding = Sanitized;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}


