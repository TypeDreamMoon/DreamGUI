// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutFlexBoxContainer.h"

#include "Core/Components/LexLayoutFlexBoxSelf.h"
#include "Core/Components/LexWidget.h"


void ULexLayoutFlexBoxContainer::UpdateLayout()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    if (Direction == ELexLayoutFlexBoxDirectionType::None)
    {
        TotalMinSize[0] = TotalPreferredSize[0] = Widget->GetWidth();
        TotalMinSize[1] = TotalPreferredSize[1] = Widget->GetHeight();
        TotalMaxSize[0] = TotalMaxSize[1] = UE_MAX_FLT;
        return;
    }
    Children.Empty();
    for (auto& ChildWidget : Widget->GetUIChildren())
    {
        if (!ChildWidget->GetWidgetActiveInHierarchy())continue;
        if (auto ChildLayoutSelf = ChildWidget->GetLayoutSelf())
        {
            if (ChildLayoutSelf->GetIgnoreLayoutContainer())continue;
        }
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

    auto GetChildSizes = [](ULexWidget* ChildWidget, int Axis, bool IsPrimaryAxis, float& OutMin, float& OutMax, float& OutPreferred, float& OutGrow, float& OutShrink)
    {
        if (auto ChildLayoutSelf = Cast<ULexLayoutFlexBoxSelf>(ChildWidget->GetLayoutSelf()))
        {
            OutMin = Axis == 0 ? ChildLayoutSelf->GetMinWidthForLayoutContainer() : ChildLayoutSelf->GetMinHeightForLayoutContainer();
            OutPreferred = Axis == 0 ? ChildLayoutSelf->GetPreferredWidthForLayoutContainer() : ChildLayoutSelf->GetPreferredHeightForLayoutContainer();
            OutMax = Axis == 0 ? ChildLayoutSelf->GetMaxWidthForLayoutContainer() : ChildLayoutSelf->GetMaxHeightForLayoutContainer();
            OutGrow = ChildLayoutSelf->GetGrowForLayoutContainer(Axis, IsPrimaryAxis);
            OutShrink = ChildLayoutSelf->GetShrinkForLayoutContainer(Axis, IsPrimaryAxis);
        }
        else
        {
            OutMin = OutMax = OutPreferred = Axis == 0 ? ChildWidget->GetWidth() : ChildWidget->GetHeight();
            OutGrow = OutShrink = 0;
        }
    };
    
    bool bAllowWarp = Warp == ELexLayoutFlexBoxWrapType::Wrap || Warp == ELexLayoutFlexBoxWrapType::WrapReverse;
    
    auto ChildrenCount = Children.Num();
    //calculate lines
    LineDataArray.Reset();
    auto CurrentLine = FLineData();
    
    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    int PrimaryAxis = bIsVertical ? 1 : 0;
    int SecondaryAxis = bIsVertical ? 0 : 1;
    auto Gap = FVector2f(WidthGap, HeightGap);
    auto ContainerSize = FVector2f(Widget->GetSize());
    ContainerSize.Y -= Padding.Top + Padding.Bottom;
    ContainerSize.X -= Padding.Left + Padding.Right;

    TotalMinSize[0] = TotalMaxSize[0] = TotalPreferredSize[0] = Padding.Left + Padding.Right;
    TotalMinSize[1] = TotalMaxSize[1] = TotalPreferredSize[1] = Padding.Top + Padding.Bottom;

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        float Min, Max, Preferred, Grow, Shrink;
        GetChildSizes(Child, PrimaryAxis, true, Min, Max, Preferred, Grow, Shrink);
        CurrentLine.TotalMin[PrimaryAxis] += Min;
        CurrentLine.TotalMax[PrimaryAxis] += Max;
        CurrentLine.TotalPreferred[PrimaryAxis] += Preferred;
        CurrentLine.TotalGrow[PrimaryAxis] += Grow;
        CurrentLine.TotalShrink[PrimaryAxis] += Shrink;
        if (bAllowWarp)
        {
            if (CurrentLine.TotalPreferred[PrimaryAxis] > ContainerSize[PrimaryAxis])//larger than container size, warp it
            {
                CurrentLine.TotalMin[PrimaryAxis] -= Min + Gap[PrimaryAxis];
                CurrentLine.TotalMax[PrimaryAxis] -= Max + Gap[PrimaryAxis];
                CurrentLine.TotalPreferred[PrimaryAxis] -= Preferred + Gap[PrimaryAxis];
                CurrentLine.TotalGrow[PrimaryAxis] -= Grow;
                CurrentLine.TotalShrink[PrimaryAxis] -= Shrink;
                //finish current line and make new line
                LineDataArray.Add(CurrentLine);
                CurrentLine = FLineData();
                CurrentLine.TotalMin[PrimaryAxis] += Min;
                CurrentLine.TotalMax[PrimaryAxis] += Max;
                CurrentLine.TotalPreferred[PrimaryAxis] += Preferred;
                CurrentLine.TotalGrow[PrimaryAxis] += Grow;
                CurrentLine.TotalShrink[PrimaryAxis] += Shrink;
            }
        }
        CurrentLine.TotalMin[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalMax[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += Gap[PrimaryAxis];
        
        //primary axis size: try not warp content and get size
        TotalMinSize[PrimaryAxis] += Min + Gap[PrimaryAxis];
        TotalMaxSize[PrimaryAxis] += Max + Gap[PrimaryAxis];
        TotalPreferredSize[PrimaryAxis] += Preferred + Gap[PrimaryAxis];
        
        //secondary axis
        GetChildSizes(Child, SecondaryAxis, false, Min, Max, Preferred, Grow, Shrink);
        CurrentLine.TotalMin[SecondaryAxis] = FMath::Max(Min, CurrentLine.TotalMin[SecondaryAxis]);
        CurrentLine.TotalMax[SecondaryAxis] = FMath::Max(Max, CurrentLine.TotalMax[SecondaryAxis]);
        CurrentLine.TotalPreferred[SecondaryAxis] = FMath::Max(Preferred, CurrentLine.TotalPreferred[SecondaryAxis]);
        CurrentLine.TotalGrow[SecondaryAxis] = FMath::Max(Grow, CurrentLine.TotalGrow[SecondaryAxis]);
        CurrentLine.TotalShrink[SecondaryAxis] = FMath::Max(Shrink, CurrentLine.TotalShrink[SecondaryAxis]);
        
        CurrentLine.Children.Add(Child);
    }
    if (ChildrenCount > 0)
    {
        CurrentLine.TotalMin[PrimaryAxis] -= Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] -= Gap[PrimaryAxis];
        LineDataArray.Add(CurrentLine);

        TotalMinSize[PrimaryAxis] -= Gap[PrimaryAxis];
        TotalMaxSize[PrimaryAxis] -= Gap[PrimaryAxis];
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
    TotalMinSize[SecondaryAxis] += SecondaryTotalMin;
    TotalMaxSize[SecondaryAxis] += SecondaryTotalMax;
    TotalPreferredSize[SecondaryAxis] += SecondaryTotalPreferred;

    bool bReverseHorizontal = Direction == ELexLayoutFlexBoxDirectionType::HorizontalReverse;
    bool bReverseVertical = Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    bool bReverse = bReverseHorizontal || bReverseVertical;
    
    float SecondarySurplusSpace = ContainerSize[SecondaryAxis] - SecondaryTotalPreferred;
    float SecondarySpaceGap = 0;//Space between two items beside gap value
    float SecondaryStretchedExtraSize = 0;

    FVector2f PosOffset(0,0);//default position use left top as origin
    PosOffset[SecondaryAxis] = SecondaryAxis == 0 ? Padding.Left : Padding.Top;
    if (SecondarySurplusSpace > 0)
    {
        switch (SecondaryAlignment)
        {
        case ELexLayoutFlexBoxSecondaryAxisAlignment::Start:break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::Center:
            PosOffset[SecondaryAxis] += SecondarySurplusSpace * 0.5f;
            break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::End:
            PosOffset[SecondaryAxis] += SecondarySurplusSpace;
            break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceBetween:
            //no offset needed
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() - 1);
            break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceAround:
            //half space offset
            SecondarySpaceGap = SecondarySurplusSpace / LineDataArray.Num();
            PosOffset[SecondaryAxis] += SecondarySpaceGap * 0.5f;
            break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::SpaceEvenly:
            //space offset
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() + 1);
            PosOffset[SecondaryAxis] += SecondarySpaceGap;
            break;
        case ELexLayoutFlexBoxSecondaryAxisAlignment::Stretch:
            SecondaryStretchedExtraSize = SecondarySurplusSpace / LineDataArray.Num();
            break;
        }
    }
    
    for (int LineIndex = 0; LineIndex < LineDataArray.Num(); LineIndex++)
    {
        auto Index = (Warp == ELexLayoutFlexBoxWrapType::WrapReverse) ? (LineDataArray.Num() - 1 - LineIndex) : LineIndex;
        auto& LineData = LineDataArray[Index];
        if (bReverse)
        {
            PosOffset[PrimaryAxis] = ContainerSize[PrimaryAxis];
        }
        else
        {
            PosOffset[PrimaryAxis] = PrimaryAxis == 0 ? Padding.Left : Padding.Top;
        }
        float PrimaryGrowMultiplier = 0;
        float PrimarySurplusSpace = ContainerSize[PrimaryAxis] - LineData.TotalPreferred[PrimaryAxis];
        float PrimarySpaceGap = 0;//Space between two items beside gap value
        if (PrimarySurplusSpace > 0)
        {
            if (LineData.TotalGrow[PrimaryAxis] == 0)
            {
                switch (PrimaryAlignment)
                {
                case ELexLayoutFlexBoxPrimaryAxisAlignment::Start:break;
                case ELexLayoutFlexBoxPrimaryAxisAlignment::Center:
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySurplusSpace : PrimarySurplusSpace) * 0.5f;
                    break;
                case ELexLayoutFlexBoxPrimaryAxisAlignment::End:
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySurplusSpace : PrimarySurplusSpace);
                    break;
                case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceBetween:
                    //no offset needed
                    PrimarySpaceGap = PrimarySurplusSpace / (LineData.Children.Num() - 1);
                    break;
                case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceAround:
                    //half space offset
                    PrimarySpaceGap = PrimarySurplusSpace / LineData.Children.Num();
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySpaceGap : PrimarySpaceGap) * 0.5f;
                    break;
                case ELexLayoutFlexBoxPrimaryAxisAlignment::SpaceEvenly:
                    //space offset
                    PrimarySpaceGap = PrimarySurplusSpace / (LineData.Children.Num() + 1);
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySpaceGap : PrimarySpaceGap);
                    break;
                }
            }
            else if (LineData.TotalGrow[PrimaryAxis] > 0)
                PrimaryGrowMultiplier = PrimarySurplusSpace / LineData.TotalGrow[PrimaryAxis];
        }

        float PrimaryShrinkMultiplier = 0;
        float PrimaryDeficitSpace = -PrimarySurplusSpace;//container less than preferred
        if (PrimaryDeficitSpace > 0)
        {
            if (LineData.TotalShrink[PrimaryAxis] > 0)
            {
                PrimaryShrinkMultiplier = PrimaryDeficitSpace / LineData.TotalShrink[PrimaryAxis];
            }
        }

        TArray<ULexWidget*> CalculatedChildren;
        auto TempPositionOffset = PosOffset;
        float SecondaryAxisLineSize = LineData.TotalPreferred[SecondaryAxis];
        SecondaryAxisLineSize += SecondaryStretchedExtraSize;

        for (int ChildIndex = 0; ChildIndex < LineData.Children.Num(); ChildIndex++)
        {
            auto& Child = LineData.Children[ChildIndex];
            bool ShouldRecalculate = false;
            FVector2f AreaSize(0,0);
            if (CalculatedChildren.Contains(Child))
            {
                AreaSize[PrimaryAxis] = PrimaryAxis == 0 ? Child->GetWidth() : Child->GetHeight();
            }
            else//calculate primary size
            {
                float Min, Max, Preferred, Grow, Shrink;
                GetChildSizes(Child, PrimaryAxis, true, Min, Max, Preferred, Grow, Shrink);
                float PrimarySize = Preferred;
                //handle shrink
                if (PrimaryShrinkMultiplier > 0)
                {
                    float ShrinkSize = Shrink * PrimaryShrinkMultiplier;
                    float MaxShrinkSize = Preferred - Min;
                    if (ShrinkSize >= MaxShrinkSize)
                    {
                        PrimarySize = Min;
                        //prepare data for next loop
                        CalculatedChildren.Add(Child);
                        LineData.TotalShrink[PrimaryAxis] -= Shrink;
                        PrimaryDeficitSpace -= MaxShrinkSize;
                        if (LineData.TotalShrink[PrimaryAxis] > 0)
                        {
                            PrimaryShrinkMultiplier = PrimaryDeficitSpace / LineData.TotalShrink[PrimaryAxis];
                        }
                        //to next calculation
                        ShouldRecalculate = true;
                    }
                    else
                    {
                        PrimarySize -= ShrinkSize;
                    }
                }
                //handle grow
                if (PrimaryGrowMultiplier)
                {
                    float GrowSize = Grow * PrimaryGrowMultiplier;
                    float MaxGrowSize = Max - Preferred;
                    if (GrowSize >= MaxGrowSize)
                    {
                        PrimarySize = Max;
                        //prepare data for next loop
                        CalculatedChildren.Add(Child);
                        LineData.TotalGrow[PrimaryAxis] -= Grow;
                        PrimarySurplusSpace -= MaxGrowSize;
                        if (LineData.TotalGrow[PrimaryAxis] > 0)
                        {
                            PrimaryGrowMultiplier = PrimarySurplusSpace / LineData.TotalGrow[PrimaryAxis];
                        }
                        //to next calculation
                        ShouldRecalculate = true;
                    }
                    else
                    {
                        PrimarySize += GrowSize;
                    }
                }
                AreaSize[PrimaryAxis] = PrimarySize;
            }
            //calculate secondary size
            float SecondaryPreferredSize = 0;
            {
                AreaSize[SecondaryAxis] = SecondaryAxisLineSize;

                float Min, Max, Preferred, Grow, Shrink;
                GetChildSizes(Child, SecondaryAxis, false, Min, Max, Preferred, Grow, Shrink);
                SecondaryPreferredSize = Preferred;
            }
            SetChildPositionAndSize(Child, TempPositionOffset, AreaSize, PrimaryAxis, SecondaryAxis, SecondaryPreferredSize, bReverseHorizontal, bReverseVertical);

            if (ShouldRecalculate)
            {
                //reset it because we are going to recalculate
                TempPositionOffset = PosOffset;
                //make loop restart
                ChildIndex = -1;
            }
            else//skip TempPositionOffset calculation because we will recalculate all
            {
                if (bReverse)
                {
                    TempPositionOffset[PrimaryAxis] -= AreaSize[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
                }
                else
                {
                    TempPositionOffset[PrimaryAxis] += AreaSize[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
                }
            }
        }
        PosOffset[PrimaryAxis] = TempPositionOffset[PrimaryAxis];
        PosOffset[SecondaryAxis] += SecondaryAxisLineSize + Gap[SecondaryAxis] + SecondarySpaceGap;
    }
}

float ULexLayoutFlexBoxContainer::GetTotalMinSize(int Axis)
{
    return TotalMinSize[Axis];
}

float ULexLayoutFlexBoxContainer::GetTotalMaxSize(int Axis)
{
    return TotalMaxSize[Axis];
}

float ULexLayoutFlexBoxContainer::GetTotalPreferredSize(int Axis)
{
    return TotalPreferredSize[Axis];
}

void ULexLayoutFlexBoxContainer::SetChildPositionAndSize(ULexWidget* ChildWidget, FVector2f Pos, FVector2f AreaSize, int PrimaryAxis, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY)
{
    auto ChildLayoutSelf = Cast<ULexLayoutFlexBoxSelf>(ChildWidget->GetLayoutSelf());
    
    if (SecondaryLineAlignment == ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch)//stretch use full area size as widget size
    {
        if (ChildLayoutSelf && ChildLayoutSelf->SecondarySizeCanStretch(SecondaryAxis))
        {
            SecondaryPreferred = AreaSize[SecondaryAxis];
        }
    }
    float AlignmentOnAxis = 0;
    switch (SecondaryLineAlignment)
    {
    case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch://stretch
    case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Start:
        AlignmentOnAxis = -0.5f;
        break;
    case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Center:
        AlignmentOnAxis = 0.0f;
        break;
    case ELexLayoutFlexBoxSecondaryAxisLineAlignment::End:
        AlignmentOnAxis = 0.5f;
        break;
    }
    FVector2f OffsetInCell;
    OffsetInCell[SecondaryAxis] = (AreaSize[SecondaryAxis] - SecondaryPreferred) * AlignmentOnAxis;

    if (ReverseX)
    {
        Pos.X -= AreaSize.X;
    }
    if (ReverseY)
    {
        Pos.Y -= AreaSize.Y;
    }
    Pos[SecondaryAxis] += OffsetInCell[SecondaryAxis];

    auto AnchoredPosition = ChildWidget->GetAnchoredPosition();
    AnchoredPosition.X = Pos[0] + AreaSize[0] * ChildWidget->GetPivot()[0];
    AnchoredPosition.Y = -Pos[1] - AreaSize[1] * (1.0f - ChildWidget->GetPivot()[1]);
    auto ParentWidget = ChildWidget->GetUIParent();
    auto AnchorMin = ChildWidget->GetAnchorMin();
    AnchoredPosition.X += -AnchorMin.X * ParentWidget->GetWidth();
    AnchoredPosition.Y += (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(AnchoredPosition);
    
    if (ChildLayoutSelf)
    {
        if (SecondaryLineAlignment != ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch)
        {
            AreaSize[SecondaryAxis] = SecondaryPreferred;//not stretch mean we will not change it's secondary-axis size, so restore it
        }
        ChildLayoutSelf->SetSizeByLayoutContainer(AreaSize, PrimaryAxis);
    }
}

FLexLayoutControlAnchorData ULexLayoutFlexBoxContainer::GetLayoutControlAnchor(const ULexWidget* TargetWidget)const
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
        }
    }
    return Result;
}

#if WITH_EDITOR
void ULexLayoutFlexBoxContainer::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutFlexBoxContainer::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
}

void ULexLayoutFlexBoxContainer::SetDirection(ELexLayoutFlexBoxDirectionType Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}


