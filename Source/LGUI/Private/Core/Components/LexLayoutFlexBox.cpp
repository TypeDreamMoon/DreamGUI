// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutFlexBox.h"
#include "Core/Components/LexWidget.h"



void ULexLayoutFlexBox::OnUpdateLayout()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    Children.Empty();
    for (auto& ChildWidget : Widget->GetUIChildren())
    {
        if (!ChildWidget->GetWidgetActiveInHierarchy())continue;
        if (auto ChildLayoutSlot = ChildWidget->GetLayoutSlot())
        {
            if (ChildLayoutSlot->GetIgnoreLayout())continue;
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

    auto GetChildSizes = [](ULexWidget* ChildWidget, int Axis, bool bControlSize, float& OutMin, float& OutPreferred, float& OutFlexible)
    {
        if (!bControlSize)
        {
            OutMin = ChildWidget->GetSize()[Axis];
            OutPreferred = OutMin;
            OutFlexible = 0;
        }
        else
        {
            if (Axis == 0)
            {
                OutMin = ChildWidget->GetMinWidth();
                OutPreferred = ChildWidget->GetPreferredWidth();
                OutPreferred = FMath::Max(OutMin, OutPreferred);
                OutFlexible = ChildWidget->GetFlexibleWidth();
            }
            else
            {
                OutMin = ChildWidget->GetMinHeight();
                OutPreferred = ChildWidget->GetPreferredHeight();
                OutPreferred = FMath::Max(OutMin, OutPreferred);
                OutFlexible = ChildWidget->GetFlexibleHeight();
            }
        }
    };
    
    bool bAllowWarp = false;
    if (Warp == ELexLayoutFlexBoxWrapType::Wrap || Warp == ELexLayoutFlexBoxWrapType::WrapReverse)
    {
        bAllowWarp = true;
        switch (Direction)
        {
        case ELexLayoutFlexBoxDirection::Horizontal:
        case ELexLayoutFlexBoxDirection::HorizontalReverse:
            if (SizeFitToChildren.bWidth)//if size fit to content then can't warp
                bAllowWarp = false;
            break;
        case ELexLayoutFlexBoxDirection::Vertical:
        case ELexLayoutFlexBoxDirection::VerticalReverse:
            if (SizeFitToChildren.bHeight)//if size fit to content then can't warp
                bAllowWarp = false;
            break;
        }
    }
    
    auto ChildrenCount = Children.Num();
    //calculate lines
    LineDataArray.Reset();
    auto CurrentLine = FLineData();
    
    bool bIsVertical = Direction == ELexLayoutFlexBoxDirection::Vertical || Direction == ELexLayoutFlexBoxDirection::VerticalReverse;
    int PrimaryAxis = bIsVertical ? 1 : 0;
    int SecondaryAxis = bIsVertical ? 0 : 1;
    auto Gap = FVector2f(WidthGap, HeightGap);
    auto ContainerSize = FVector2f(Widget->GetSize());
    ContainerSize.Y -= Padding.Top + Padding.Bottom;
    ContainerSize.X -= Padding.Left + Padding.Right;

    TotalMinSize[0] = TotalPreferredSize[0] = Padding.Left + Padding.Right;
    TotalMinSize[1] = TotalPreferredSize[1] = Padding.Top + Padding.Bottom;
    TotalFlexibleSize[0] = TotalFlexibleSize[1] = 0;

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        float Min, Preferred, Flexible;
        GetChildSizes(Child, PrimaryAxis, ControlChildSize[PrimaryAxis], Min, Preferred, Flexible);
        CurrentLine.TotalMin[PrimaryAxis] += Min;
        CurrentLine.TotalPreferred[PrimaryAxis] += Preferred;
        if (bAllowWarp)
        {
            if (CurrentLine.TotalPreferred[PrimaryAxis] > ContainerSize[PrimaryAxis])//larger than container size, warp it
            {
                CurrentLine.TotalMin[PrimaryAxis] -= Min + Gap[PrimaryAxis];
                CurrentLine.TotalPreferred[PrimaryAxis] -= Preferred + Gap[PrimaryAxis];
                //finish current line and make new line
                LineDataArray.Add(CurrentLine);
                CurrentLine = FLineData();
                CurrentLine.TotalMin[PrimaryAxis] += Min;
                CurrentLine.TotalPreferred[PrimaryAxis] += Preferred;
            }
        }
        CurrentLine.TotalMin[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] += Gap[PrimaryAxis];
        CurrentLine.TotalFlexible[PrimaryAxis] += Flexible;
        
        //primary axis size: try not warp content and get size
        TotalMinSize[PrimaryAxis] += Min + Gap[PrimaryAxis];
        TotalPreferredSize[PrimaryAxis] += Preferred + Gap[PrimaryAxis];
        TotalFlexibleSize[PrimaryAxis] += Flexible;
        
        //secondary axis
        GetChildSizes(Child, SecondaryAxis, ControlChildSize[SecondaryAxis], Min, Preferred, Flexible);
        CurrentLine.TotalMin[SecondaryAxis] = FMath::Max(Min, CurrentLine.TotalMin[SecondaryAxis]);
        CurrentLine.TotalPreferred[SecondaryAxis] = FMath::Max(Preferred, CurrentLine.TotalPreferred[SecondaryAxis]);
        CurrentLine.TotalFlexible[SecondaryAxis] = FMath::Max(Flexible, CurrentLine.TotalFlexible[SecondaryAxis]);
        
        CurrentLine.Children.Add(Child);
    }
    if (ChildrenCount > 0)
    {
        CurrentLine.TotalMin[PrimaryAxis] -= Gap[PrimaryAxis];
        CurrentLine.TotalPreferred[PrimaryAxis] -= Gap[PrimaryAxis];
        LineDataArray.Add(CurrentLine);

        TotalMinSize[PrimaryAxis] -= Gap[PrimaryAxis];
        TotalPreferredSize[PrimaryAxis] -= Gap[PrimaryAxis];
    }

    //secondary axis property
    float SecondaryTotalMin = 0, SecondaryTotalPreferred = 0, SecondaryTotalFlexible = 0;
    for (int LineIndex = 0; LineIndex < LineDataArray.Num(); LineIndex++)
    {
        auto& LineData = LineDataArray[LineIndex];
        SecondaryTotalMin += LineData.TotalMin[SecondaryAxis] + Gap[SecondaryAxis];
        SecondaryTotalPreferred += LineData.TotalPreferred[SecondaryAxis] + Gap[SecondaryAxis];
        SecondaryTotalFlexible += LineData.TotalFlexible[SecondaryAxis];
    }
    if (ChildrenCount > 0)
    {
        SecondaryTotalMin -= Gap[SecondaryAxis];
        SecondaryTotalPreferred -= Gap[SecondaryAxis];
    }
    //secondary axis size: accumulate secondary sizes
    TotalMinSize[SecondaryAxis] += SecondaryTotalMin;
    TotalPreferredSize[SecondaryAxis] += SecondaryTotalPreferred;
    TotalFlexibleSize[SecondaryAxis] += SecondaryTotalFlexible;

    //container size is fully calculated at this line
    if (SizeFitToChildren.bWidth)
    {
        ContainerSize[0] = TotalPreferredSize[0] - (Padding.Left + Padding.Right);
        Widget->SetWidth(TotalPreferredSize[0]);
    }
    if (SizeFitToChildren.bHeight)
    {
        ContainerSize[1] = TotalPreferredSize[1] - (Padding.Bottom + Padding.Top);
        Widget->SetHeight(TotalPreferredSize[1]);
    }

    bool bReverseHorizontal = Direction == ELexLayoutFlexBoxDirection::HorizontalReverse;
    bool bReverseVertical = Direction == ELexLayoutFlexBoxDirection::VerticalReverse;
    bool bReverse = bReverseHorizontal || bReverseVertical;
    
    float SecondaryMinMaxLerp = 0;
    if (SecondaryTotalMin != SecondaryTotalPreferred)
        SecondaryMinMaxLerp = FMath::Clamp((ContainerSize[SecondaryAxis] - SecondaryTotalMin) / (SecondaryTotalPreferred - SecondaryTotalMin), 0, 1);
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
        float PrimaryFlexibleMultiplier = 0;
        float PrimarySurplusSpace = ContainerSize[PrimaryAxis] - LineData.TotalPreferred[PrimaryAxis];
        float PrimarySpaceGap = 0;//Space between two items beside gap value
        if (PrimarySurplusSpace > 0)
        {
            if (LineData.TotalFlexible[PrimaryAxis] == 0)
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
            else if (LineData.TotalFlexible[PrimaryAxis] > 0)
                PrimaryFlexibleMultiplier = PrimarySurplusSpace / LineData.TotalFlexible[PrimaryAxis];
        }

        float PrimaryMinMaxLerp = 0;
        if (LineData.TotalMin[PrimaryAxis] != LineData.TotalPreferred[PrimaryAxis])
            PrimaryMinMaxLerp = FMath::Clamp((ContainerSize[PrimaryAxis] - LineData.TotalMin[PrimaryAxis]) / (LineData.TotalPreferred[PrimaryAxis] - LineData.TotalMin[PrimaryAxis]), 0, 1);

        float SecondaryAxisLineSize = FMath::Lerp(LineData.TotalMin[SecondaryAxis], LineData.TotalPreferred[SecondaryAxis], SecondaryMinMaxLerp);
        SecondaryAxisLineSize += SecondaryStretchedExtraSize;
        for (int ChildIndex = 0; ChildIndex < LineData.Children.Num(); ChildIndex++)
        {
            auto& Child = LineData.Children[ChildIndex];
            FVector2f Size(0,0);
            {
                float Min, Preferred, Flexible;
                GetChildSizes(Child, PrimaryAxis, ControlChildSize[PrimaryAxis], Min, Preferred, Flexible);
                Size[PrimaryAxis] = FMath::Lerp(Min, Preferred, PrimaryMinMaxLerp);
                Size[PrimaryAxis] += Flexible * PrimaryFlexibleMultiplier;
            }
            float SecondaryPreferredSize = 0;
            {
                Size[SecondaryAxis] = SecondaryAxisLineSize;

                float Min, Preferred, Flexible;
                GetChildSizes(Child, SecondaryAxis, ControlChildSize[SecondaryAxis], Min, Preferred, Flexible);
                SecondaryPreferredSize = Preferred;
            }
            SetChildPositionAndSize(Child, PosOffset, Size, SecondaryAxis, SecondaryPreferredSize, bReverseHorizontal, bReverseVertical);

            if (bReverse)
            {
                PosOffset[PrimaryAxis] -= Size[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
            else
            {
                PosOffset[PrimaryAxis] += Size[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
        }
        PosOffset[SecondaryAxis] += SecondaryAxisLineSize + Gap[SecondaryAxis] + SecondarySpaceGap;
    }
}

float ULexLayoutFlexBox::GetTotalMinSize(int Axis) const
{
    return TotalMinSize[Axis];
}

float ULexLayoutFlexBox::GetTotalPreferredSize(int Axis) const
{
    return TotalPreferredSize[Axis];
}

float ULexLayoutFlexBox::GetTotalFlexibleSize(int Axis) const
{
    return TotalFlexibleSize[Axis];
}

void ULexLayoutFlexBox::SetChildPositionAndSize(ULexWidget* ChildWidget, FVector2f Pos, FVector2f Size, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY)
{
    float AlignmentOnAxis = 0;
    switch (SecondaryLineAlignment)
    {
        case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Start:break;
        case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch:
            break;
        case ELexLayoutFlexBoxSecondaryAxisLineAlignment::Center:
            AlignmentOnAxis = 0.5f;
        break;
        case ELexLayoutFlexBoxSecondaryAxisLineAlignment::End:
            AlignmentOnAxis = 1.0f;
        break;
    }
    FVector2f OffsetInCell;
    OffsetInCell[SecondaryAxis] = (Size[SecondaryAxis] - ChildWidget->GetSizeDelta()[SecondaryAxis]) * AlignmentOnAxis;
    if (!ControlChildSize.bWidth)
    {
        Size[0] = ChildWidget->GetSizeDelta()[0];
    }
    if (!ControlChildSize.bHeight)
    {
        Size[1] = ChildWidget->GetSizeDelta()[1];
    }
    if (ControlChildSize[SecondaryAxis])
    {
        if (SecondaryLineAlignment != ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch)//stretch use full size
        {
            Size[SecondaryAxis] = SecondaryPreferred;//non-stretch use preferred size
        }
    }
    ChildWidget->SetSizeDelta(FVector2D(Size));

    if (ReverseX)
    {
        Pos.X -= Size.X;
    }
    if (ReverseY)
    {
        Pos.Y -= Size.Y;
    }
    Pos[SecondaryAxis] += OffsetInCell[SecondaryAxis];

    auto AnchoredPosition = ChildWidget->GetAnchoredPosition();
    AnchoredPosition.X = Pos[0] + Size[0] * ChildWidget->GetPivot()[0];
    AnchoredPosition.Y = -Pos[1] - Size[1] * (1.0f - ChildWidget->GetPivot()[1]);
    auto ParentWidget = ChildWidget->GetUIParent();
    auto AnchorMin = ChildWidget->GetAnchorMin();
    AnchoredPosition.X += -AnchorMin.X * ParentWidget->GetWidth();
    AnchoredPosition.Y += (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(AnchoredPosition);
}

FLexLayoutControlAnchorData ULexLayoutFlexBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget)
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        Result.bCanControlHorizontalSizeDelta = SizeFitToChildren.bWidth;
        Result.bCanControlVerticalSizeDelta = SizeFitToChildren.bHeight;
    }
    else if (ThisWidget->GetUIChildren().Contains(TargetWidget))//child
    {
        bool bIgnoreLayout = false;
        if (auto LayoutSlot = TargetWidget->GetLayoutSlot())
        {
            bIgnoreLayout = LayoutSlot->GetIgnoreLayout();
        }
        if (!bIgnoreLayout)
        {
            Result.bCanControlHorizontalAnchoredPosition = true;
            Result.bCanControlVerticalAnchoredPosition = true;
            if (this->ControlChildSize.bWidth)
            {
                Result.bCanControlHorizontalSizeDelta = true;
            }
            if (this->ControlChildSize.bHeight)
            {
                Result.bCanControlVerticalSizeDelta = true;
            }
        }
    }
    return Result;
}

#if WITH_EDITOR
void ULexLayoutFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void ULexLayoutFlexBox::SetDirection(ELexLayoutFlexBoxDirection Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        MarkLayoutDirty();
    }
}


