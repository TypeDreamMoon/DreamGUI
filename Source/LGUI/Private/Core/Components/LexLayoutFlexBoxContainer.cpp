// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutFlexBoxContainer.h"
#include "Core/Components/LexLayoutFlexBoxSelf.h"
#include "Core/Components/LexWidget.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayout FlexBoxContainer RebuildLayout"), STAT_LexLayoutFlexBoxContainer, STATGROUP_LGUI);

void ULexLayoutFlexBoxContainer::UpdateLayout()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutFlexBoxContainer);
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

    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    int PrimaryAxis = bIsVertical ? 1 : 0;
    int SecondaryAxis = bIsVertical ? 0 : 1;

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
            if (auto ChildLayoutSelf = Cast<ULexLayoutFlexBoxSelf>(ChildWidget->GetLayoutSelf()))
            {
                ChildLayoutSelf->GetLayoutProperties(Value.Min, Value.Max, Value.Preferred);
                if (PrimaryAxis == 0)
                {
                    Value.Grow = FVector2f(ChildLayoutSelf->GetGrowForLayoutContainer(0), 0);
                    Value.Shrink = FVector2f(ChildLayoutSelf->GetShrinkForLayoutContainer(0), 0);
                }
                else
                {
                    Value.Grow = FVector2f(0, ChildLayoutSelf->GetGrowForLayoutContainer(1));
                    Value.Shrink = FVector2f(0, ChildLayoutSelf->GetShrinkForLayoutContainer(1));
                }
            }
            else
            {
                Value.Min = Value.Max = Value.Preferred = FVector2f(ChildWidget->GetWidth(), ChildWidget->GetHeight());
                Value.Grow = Value.Shrink = FVector2f(0, 0);
            }
            ChildSizePtr = &MapWidgetToChildSizes.Add(ChildWidget, Value);
        }
        return ChildSizePtr;
    };
    
    bool bAllowWarp = Wrap == ELexLayoutFlexBoxWrapType::Wrap || Wrap == ELexLayoutFlexBoxWrapType::WrapReverse;
    
    auto ChildrenCount = Children.Num();
    //calculate lines
    LineDataArray.Reset();
    auto CurrentLine = FLineData();
    
    auto Gap = FVector2f(WidthGap, HeightGap);
    auto ContainerSize = FVector2f(Widget->GetSize());
    ContainerSize.Y -= Padding.Top + Padding.Bottom;
    ContainerSize.X -= Padding.Left + Padding.Right;

    TotalMinSize[0] = TotalMaxSize[0] = TotalPreferredSize[0] = Padding.Left + Padding.Right;
    TotalMinSize[1] = TotalMaxSize[1] = TotalPreferredSize[1] = Padding.Top + Padding.Bottom;

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        auto ChildSizes = GetChildSizes(Child);
        CurrentLine.TotalMin[PrimaryAxis] += ChildSizes->Min[PrimaryAxis];
        CurrentLine.TotalMax[PrimaryAxis] += ChildSizes->Max[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += ChildSizes->Preferred[PrimaryAxis];
        CurrentLine.TotalGrow[PrimaryAxis] += ChildSizes->Grow[PrimaryAxis];
        CurrentLine.TotalShrink[PrimaryAxis] += ChildSizes->Shrink[PrimaryAxis];
        if (bAllowWarp)
        {
            if (CurrentLine.TotalPreferred[PrimaryAxis] > ContainerSize[PrimaryAxis])//larger than container size, warp it
            {
                CurrentLine.TotalMin[PrimaryAxis] -= ChildSizes->Min[PrimaryAxis] + Gap[PrimaryAxis];
                CurrentLine.TotalMax[PrimaryAxis] -= ChildSizes->Max[PrimaryAxis] + Gap[PrimaryAxis];
                CurrentLine.TotalPreferred[PrimaryAxis] -= ChildSizes->Preferred[PrimaryAxis] + Gap[PrimaryAxis];
                CurrentLine.TotalGrow[PrimaryAxis] -= ChildSizes->Grow[PrimaryAxis];
                CurrentLine.TotalShrink[PrimaryAxis] -= ChildSizes->Shrink[PrimaryAxis];
                //finish current line and make new line
                LineDataArray.Add(CurrentLine);
                CurrentLine = FLineData();
                CurrentLine.TotalMin[PrimaryAxis] += ChildSizes->Min[PrimaryAxis];
                CurrentLine.TotalMax[PrimaryAxis] += ChildSizes->Max[PrimaryAxis];
                CurrentLine.TotalPreferred[PrimaryAxis] += ChildSizes->Preferred[PrimaryAxis];
                CurrentLine.TotalGrow[PrimaryAxis] += ChildSizes->Grow[PrimaryAxis];
                CurrentLine.TotalShrink[PrimaryAxis] += ChildSizes->Shrink[PrimaryAxis];
            }
        }
        CurrentLine.TotalMin[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalMax[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += Gap[PrimaryAxis];
        
        //primary axis size: try not warp content and get size
        TotalMinSize[PrimaryAxis] += ChildSizes->Min[PrimaryAxis] + Gap[PrimaryAxis];
        TotalMaxSize[PrimaryAxis] += ChildSizes->Max[PrimaryAxis] + Gap[PrimaryAxis];
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
        auto Index = (Wrap == ELexLayoutFlexBoxWrapType::WrapReverse) ? (LineDataArray.Num() - 1 - LineIndex) : LineIndex;
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
            auto ChildSizes = GetChildSizes(Child);
            if (CalculatedChildren.Contains(Child))
            {
                AreaSize[PrimaryAxis] = PrimaryAxis == 0 ? Child->GetWidth() : Child->GetHeight();
            }
            else//calculate primary size
            {
                float PrimarySize = ChildSizes->Preferred[PrimaryAxis];
                //handle shrink
                if (PrimaryShrinkMultiplier > 0)
                {
                    float ShrinkSize = ChildSizes->Shrink[PrimaryAxis] * PrimaryShrinkMultiplier;
                    float MaxShrinkSize = ChildSizes->Preferred[PrimaryAxis] - ChildSizes->Min[PrimaryAxis];
                    if (ShrinkSize >= MaxShrinkSize)
                    {
                        PrimarySize = ChildSizes->Min[PrimaryAxis];
                        //prepare data for next loop
                        CalculatedChildren.Add(Child);
                        LineData.TotalShrink[PrimaryAxis] -= ChildSizes->Shrink[PrimaryAxis];
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
                    float GrowSize = ChildSizes->Grow[PrimaryAxis] * PrimaryGrowMultiplier;
                    float MaxGrowSize = ChildSizes->Max[PrimaryAxis] - ChildSizes->Preferred[PrimaryAxis];
                    if (GrowSize >= MaxGrowSize)
                    {
                        PrimarySize = ChildSizes->Max[PrimaryAxis];
                        //prepare data for next loop
                        CalculatedChildren.Add(Child);
                        LineData.TotalGrow[PrimaryAxis] -= ChildSizes->Grow[PrimaryAxis];
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
                SecondaryPreferredSize = ChildSizes->Preferred[SecondaryAxis];
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

void ULexLayoutFlexBoxContainer::SetChildPositionAndSize(ULexWidget* ChildWidget, FVector2f Pos, FVector2f AreaSize, int PrimaryAxis, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY)
{
    auto ChildLayoutSelf = Cast<ULexLayoutFlexBoxSelf>(ChildWidget->GetLayoutSelf());
    
    if (SecondaryLineAlignment == ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch)//stretch use full area size as widget size
    {
        if (ChildLayoutSelf && ChildLayoutSelf->GetSecondaryAxisSizeCanStretchByLayoutContainer(SecondaryAxis))
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

    float AnchoredPositionX = Pos[0] + AreaSize[0] * ChildWidget->GetPivot()[0];
    float AnchoredPositionY = -Pos[1] - AreaSize[1] * (1.0f - ChildWidget->GetPivot()[1]);
    auto ParentWidget = ChildWidget->GetUIParent();
    auto AnchorMin = ChildWidget->GetAnchorMin();
    AnchoredPositionX += -AnchorMin.X * ParentWidget->GetWidth();
    AnchoredPositionY += (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(FVector2D(AnchoredPositionX, AnchoredPositionY));
    
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

void ULexLayoutFlexBoxContainer::GetLayoutProperties(FVector2f& OutMin, FVector2f& OutMax, FVector2f& OutPreferred)
{
    OutMin = TotalMinSize;
    OutMax = TotalMaxSize;
    OutPreferred = TotalPreferredSize;
}

void ULexLayoutFlexBoxContainer::SetDirection(ELexLayoutFlexBoxDirectionType Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetWrap(ELexLayoutFlexBoxWrapType Value)
{
    if (Wrap != Value)
    {
        Wrap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetPrimaryAlignment(ELexLayoutFlexBoxPrimaryAxisAlignment Value)
{
    if (PrimaryAlignment != Value)
    {
        PrimaryAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment Value)
{
    if (SecondaryAlignment != Value)
    {
        SecondaryAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment Value)
{
    if (SecondaryLineAlignment != Value)
    {
        SecondaryLineAlignment = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetWidthGap(float Value)
{
    if (WidthGap != Value)
    {
        WidthGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetHeightGap(float Value)
{
    if (HeightGap != Value)
    {
        HeightGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutFlexBoxContainer::SetPadding(FMargin Value)
{
    if (Padding != Value)
    {
        Padding = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}


