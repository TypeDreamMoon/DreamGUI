// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexWidget.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutContainer FlexBox"), STAT_LexLayoutContainerFlexBox, STATGROUP_LGUI);

void ULexLayoutContainerFlexBox::UpdateLayout(ELexLayoutUpdateType UpdateType)
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutContainerFlexBox);
    if (bIsAnimationPlaying)
    {
        bShouldRebuildLayoutAfterAnimation = true;
        return;
    }
    
    if (UpdateType == ELexLayoutUpdateType::FirstPass_RootToLeaf)
    {
        auto Widget = GetWidget();
        if (!Widget)return;
    
        Children.Empty();
        for (auto& ChildWidget : Widget->GetChildren())
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
        
        CalculateLayout(false);
    }
    else if (UpdateType == ELexLayoutUpdateType::SecondPass_LeafToRoot)
    {
        CalculateLayout(true);
        
        BeginSetupAnimations();

        ELexLayoutAnimationType TempAnimationType = AnimationType;
#if WITH_EDITOR
        if (!this->GetWorld()->IsGameWorld())
        {
            TempAnimationType = ELexLayoutAnimationType::Immediately;
        }
#endif
        for (auto& LayoutResult : CalculatedLayoutResultArray)
        {
            ApplyAnchoredPositionWithAnimation(TempAnimationType, LayoutResult.AnchoredPos, LayoutResult.Widget);
            
            if (LayoutResult.LayoutSelf)
            {
                LayoutResult.LayoutSelf->SetSizeByLayoutContainer(LayoutResult.Size, LayoutResult.PrimaryAxis);
            }
            else
            {
                ApplySizeDeltaWithAnimation(TempAnimationType, FVector2D(LayoutResult.Size), LayoutResult.Widget);
            }
        }

        if (TempAnimationType == ELexLayoutAnimationType::EaseAnimation)
        {
            EndSetupAnimations();
        }
    }
}

void ULexLayoutContainerFlexBox::CalculateLayout(bool bApplyLayoutToChildren)
{
    auto Widget = GetWidget();
    if (!Widget)return;
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
                ChildLayoutSelf->GetLayoutProperties(Value.Preferred);
                ChildLayoutSelf->GetLayoutMinMax(Value.Min, Value.Max);
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
                if (!ChildWidget->GetLayoutPreferredWidth(Value.Preferred.X))
                {
                    UE_LOG(LGUI, Error, TEXT("NotSet"));
                }
                if (!ChildWidget->GetLayoutPreferredHeight(Value.Preferred.Y))
                {
                    UE_LOG(LGUI, Error, TEXT("NotSet"));
                }
                Value.Min = Value.Max = Value.Preferred;
                Value.Grow = Value.Shrink = FVector2f(0, 0);
            }
            ChildSizePtr = &MapWidgetToChildSizes.Add(ChildWidget, Value);
        }
        return ChildSizePtr;
    };
    auto SetChildPositionAndSize = [&](ULexWidget* ChildWidget, FVector2f Pos, FVector2f AreaSize, int PrimaryAxis, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY)
    {
        auto ChildLayoutSelf = Cast<ULexLayoutSelfFlexBox>(ChildWidget->GetLayoutSelf());
    
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
        auto AnchorMin = ChildWidget->GetAnchorMin();
        AnchoredPositionX += -AnchorMin.X * ThisWidgetSize.X;
        AnchoredPositionY += (1 - AnchorMin.Y) * ThisWidgetSize.Y;
        
        if (ChildLayoutSelf)
        {
            if (SecondaryLineAlignment != ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch)
            {
                AreaSize[SecondaryAxis] = SecondaryPreferred;//not stretch mean we will not change it's secondary-axis size, so restore it
            }
        }
        //now the AreaSize is the final size
        ChildWidget->SetLayoutFinalWidth(AreaSize[0]);
        ChildWidget->SetLayoutFinalHeight(AreaSize[1]);
        
        FCalculatedLayoutResult CalculatedLayout;
        CalculatedLayout.Widget = ChildWidget;
        CalculatedLayout.AnchoredPos = FVector2D(AnchoredPositionX, AnchoredPositionY);
        CalculatedLayout.LayoutSelf = ChildLayoutSelf;
        CalculatedLayout.Size = AreaSize;
        CalculatedLayout.PrimaryAxis = PrimaryAxis;
        CalculatedLayoutResultArray.Add(CalculatedLayout);
    };

    bool bAllowWrap = Wrap == ELexLayoutFlexBoxWrapType::Wrap || Wrap == ELexLayoutFlexBoxWrapType::WrapReverse;
    
    auto ChildrenCount = Children.Num();
    //calculate lines
    LineDataArray.Reset();
    auto CurrentLine = FLineData();
    TotalPreferredSize = FVector2f(0, 0);
    
    auto Gap = FVector2f(WidthGap, HeightGap);
    FVector2f ContainerSize = FVector2f(Widget->GetWidth(), Widget->GetHeight());
    if (!Widget->GetLayoutFinalWidth(ContainerSize.X))
    {
        UE_LOG(LGUI, Error, TEXT("NotSet"));
    }
    if (!Widget->GetLayoutFinalHeight(ContainerSize.Y))
    {
        UE_LOG(LGUI, Error, TEXT("NotSet"));
    }
    ThisWidgetSize = ContainerSize;
    ContainerSize.Y -= Padding.Top + Padding.Bottom;
    ContainerSize.X -= Padding.Left + Padding.Right;

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        auto ChildSizes = GetChildSizes(Child);
        CurrentLine.TotalMin[PrimaryAxis] += ChildSizes->Min[PrimaryAxis];
        CurrentLine.TotalMax[PrimaryAxis] += ChildSizes->Max[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += ChildSizes->Preferred[PrimaryAxis];
        CurrentLine.TotalGrow[PrimaryAxis] += ChildSizes->Grow[PrimaryAxis];
        CurrentLine.TotalShrink[PrimaryAxis] += ChildSizes->Shrink[PrimaryAxis];
        if (bAllowWrap)
        {
            if (CurrentLine.TotalPreferred[PrimaryAxis] > ContainerSize[PrimaryAxis])//larger than container size, wrap it
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

    if (!bApplyLayoutToChildren)return;

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
        auto CurrentLinePosOffset = PosOffset;
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

            if (ShouldRecalculate)
            {
                //reset it because we are going to recalculate
                CurrentLinePosOffset = PosOffset;
                //make loop restart
                ChildIndex = -1;
                CalculatedLayoutResultArray.Reset();
            }
            else
            {
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
        }
        PosOffset[PrimaryAxis] = CurrentLinePosOffset[PrimaryAxis];
        PosOffset[SecondaryAxis] += SecondaryAxisLineSize + Gap[SecondaryAxis] + SecondarySpaceGap;
    }
}

FLexLayoutControlAnchorData ULexLayoutContainerFlexBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget)const
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        
    }
    else if (ThisWidget->GetChildren().Contains(TargetWidget))//child
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
void ULexLayoutContainerFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexLayoutContainerFlexBox::GetLayoutProperties(FVector2f& OutPreferred)
{
    OutPreferred = TotalPreferredSize;
}

int ULexLayoutContainerFlexBox::GetPrimaryAxis() const
{
    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    return bIsVertical ? 1 : 0;
}

int ULexLayoutContainerFlexBox::GetCrossAxis() const
{
    bool bIsVertical = Direction == ELexLayoutFlexBoxDirectionType::Vertical || Direction == ELexLayoutFlexBoxDirectionType::VerticalReverse;
    return bIsVertical ? 0 : 1;
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
    if (WidthGap != Value)
    {
        WidthGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetHeightGap(float Value)
{
    if (HeightGap != Value)
    {
        HeightGap = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutContainerFlexBox::SetPadding(FMargin Value)
{
    if (Padding != Value)
    {
        Padding = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}


