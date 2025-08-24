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
        if (!ChildWidget->IsVisibleForLayout())continue;
        auto ChildLayoutSlot = (ULexLayoutFlexBoxSlot*)ChildWidget->GetLayoutSlot();
        if (ChildLayoutSlot->GetIgnoreLayout())continue;
        Children.Add(ChildWidget);
    }

    auto GetChildSizes = [](ULexWidget* ChildWidget, int Axis, float& OutMin, float& OutPreferred, float& OutFlexible)
    {
        if (Axis == 0)
        {
            OutMin = ChildWidget->GetMinWidth();
            OutPreferred = ChildWidget->GetPreferredWidth();
            OutFlexible = ChildWidget->GetFlexibleWidth();
        }
        else
        {
            OutMin = ChildWidget->GetMinHeight();
            OutPreferred = ChildWidget->GetPreferredHeight();
            OutFlexible = ChildWidget->GetFlexibleHeight();
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

    for (int i = 0; i < ChildrenCount; i++)
    {
        auto Child = Children[i];
        float Min, Preferred, Flexible;
        GetChildSizes(Child, PrimaryAxis, Min, Preferred, Flexible);
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
        //cross axis
        GetChildSizes(Child, SecondaryAxis, Min, Preferred, Flexible);
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

    bool bReverseHorizontal = Direction == ELexLayoutFlexBoxDirection::HorizontalReverse;
    bool bReverseVertical = Direction == ELexLayoutFlexBoxDirection::VerticalReverse;
    bool bReverse = bReverseHorizontal || bReverseVertical;
    
    float SecondaryMinMaxLerp = 0;
    if (SecondaryTotalMin != SecondaryTotalPreferred)
        SecondaryMinMaxLerp = FMath::Clamp((ContainerSize[SecondaryAxis] - SecondaryTotalMin) / (SecondaryTotalPreferred - SecondaryTotalMin), 0, 1);
    float SecondarySurplusSpace = ContainerSize[SecondaryAxis] - SecondaryTotalPreferred;
    float SecondarySpaceGap = 0;//Space between two items beside gap value

    FVector2f PosOffset(0,0);//default position use left top as origin
    PosOffset[SecondaryAxis] = SecondaryAxis == 0 ? Padding.Left : Padding.Top;
    if (SecondarySurplusSpace > 0)
    {
        switch (SecondaryAlignment)
        {
        case ELexLayoutFlexBoxAlignment::Start:break;
        case ELexLayoutFlexBoxAlignment::Center:
            PosOffset[SecondaryAxis] += SecondarySurplusSpace * 0.5f;
            break;
        case ELexLayoutFlexBoxAlignment::End:
            PosOffset[SecondaryAxis] += SecondarySurplusSpace;
            break;
        case ELexLayoutFlexBoxAlignment::SpaceBetween:
            //no offset needed
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() - 1);
            break;
        case ELexLayoutFlexBoxAlignment::SpaceAround:
            //half space offset
            SecondarySpaceGap = SecondarySurplusSpace / LineDataArray.Num();
            PosOffset[SecondaryAxis] += SecondarySpaceGap * 0.5f;
            break;
        case ELexLayoutFlexBoxAlignment::SpaceEvenly:
            //space offset
            SecondarySpaceGap = SecondarySurplusSpace / (LineDataArray.Num() + 1);
            PosOffset[SecondaryAxis] += SecondarySpaceGap;
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
                case ELexLayoutFlexBoxAlignment::Start:break;
                case ELexLayoutFlexBoxAlignment::Center:
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySurplusSpace : PrimarySurplusSpace) * 0.5f;
                    break;
                case ELexLayoutFlexBoxAlignment::End:
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySurplusSpace : PrimarySurplusSpace);
                    break;
                case ELexLayoutFlexBoxAlignment::SpaceBetween:
                    //no offset needed
                    PrimarySpaceGap = PrimarySurplusSpace / (LineData.Children.Num() - 1);
                    break;
                case ELexLayoutFlexBoxAlignment::SpaceAround:
                    //half space offset
                    PrimarySpaceGap = PrimarySurplusSpace / LineData.Children.Num();
                    PosOffset[PrimaryAxis] += (bReverse ? -PrimarySpaceGap : PrimarySpaceGap) * 0.5f;
                    break;
                case ELexLayoutFlexBoxAlignment::SpaceEvenly:
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

        float SecondarySize = FMath::Lerp(LineData.TotalMin[SecondaryAxis], LineData.TotalPreferred[SecondaryAxis], SecondaryMinMaxLerp);
        for (int ChildIndex = 0; ChildIndex < LineData.Children.Num(); ChildIndex++)
        {
            auto& Child = LineData.Children[ChildIndex];
            FVector2f Size(0,0);
            {
                float Min, Preferred, Flexible;
                GetChildSizes(Child, PrimaryAxis, Min, Preferred, Flexible);
                Size[PrimaryAxis] = FMath::Lerp(Min, Preferred, PrimaryMinMaxLerp);
                Size[PrimaryAxis] += Flexible * PrimaryFlexibleMultiplier;
            }
            {
                float Min, Preferred, Flexible;
                GetChildSizes(Child, SecondaryAxis, Min, Preferred, Flexible);
                if (Flexible > 0)
                {
                    Size[SecondaryAxis] = SecondarySize;
                }
                else
                {
                    float MinMaxLerp = 0;
                    if (Preferred != Min)
                        MinMaxLerp = FMath::Clamp((SecondarySize - Min) / (Preferred - Min), 0, 1);
                    Size[SecondaryAxis] = FMath::Lerp(Min, Preferred, MinMaxLerp);
                }
            }
            SetChildPositionAndSize(Child, PosOffset, Size, bReverseHorizontal, bReverseVertical);

            if (bReverse)
            {
                PosOffset[PrimaryAxis] -= Size[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
            else
            {
                PosOffset[PrimaryAxis] += Size[PrimaryAxis] + Gap[PrimaryAxis] + PrimarySpaceGap;
            }
        }
        PosOffset[SecondaryAxis] += SecondarySize + Gap[SecondaryAxis] + SecondarySpaceGap;
    }
}

void ULexLayoutFlexBox::SetChildPositionAndSize(ULexWidget* ChildWidget, FVector2f Pos, FVector2f Size, bool ReverseX, bool ReverseY)
{
    if (ReverseX)
    {
        Pos.X -= Size.X;
    }
    if (ReverseY)
    {
        Pos.Y -= Size.Y;
    }
    auto AnchorMin = ChildWidget->GetAnchorMin();
    auto AnchorMax = ChildWidget->GetAnchorMax();
    if (AnchorMin.X != AnchorMax.X)//custom anchor not support
    {
        ChildWidget->SetHorizontalAnchorMinMax(FVector2D(0, 0), true, true);
    }
    if (AnchorMin.Y != AnchorMax.Y)
    {
        ChildWidget->SetVerticalAnchorMinMax(FVector2D(1, 1), true, true);
    }
    
    ChildWidget->SetSizeDelta(FVector2D(Size));

    auto AnchoredPosition = ChildWidget->GetAnchoredPosition();
    AnchoredPosition.X = Pos[0] + Size[0] * ChildWidget->GetPivot()[0];
    AnchoredPosition.Y = -Pos[1] - Size[1] * (1.0f - ChildWidget->GetPivot()[1]);
    auto ParentWidget = ChildWidget->GetUIParent();
    AnchoredPosition.X += -AnchorMin.X * ParentWidget->GetWidth();
    AnchoredPosition.Y += (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(AnchoredPosition);
}

void ULexLayoutFlexBox::GetLayoutControlAnchor(ULexWidget* TargetWidget, FLexLayoutControlAnchorData& Result)
{
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        Result.bCanControlHorizontalSizeDelta = SizeFitToChildren.bWidth;
        Result.bCanControlVerticalSizeDelta = SizeFitToChildren.bHeight;
    }
    else if (ThisWidget->GetUIChildren().Contains(TargetWidget))//child
    {
        if (auto LayoutSlot = Cast<ULexLayoutFlexBoxSlot>(TargetWidget->GetLayoutSlot()))
        {
            if (!LayoutSlot->GetIgnoreLayout())
            {
                Result.bCanControlHorizontalAnchoredPosition = true;
                Result.bCanControlVerticalAnchoredPosition = true;
                Result.bCanControlHorizontalSizeDelta = true;
                Result.bCanControlVerticalSizeDelta = true;
            }
        }
    }
}

#if WITH_EDITOR
void ULexLayoutFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

TSubclassOf<ULexLayoutSlot> ULexLayoutFlexBox::GetSlotClass() const
{
	return ULexLayoutFlexBoxSlot::StaticClass();
}

void ULexLayoutFlexBox::SetDirection(ELexLayoutFlexBoxDirection Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::OnTransformChanged()
{
}

void ULexLayoutFlexBoxSlot::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
}

#if WITH_EDITOR
void ULexLayoutFlexBoxSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    GetLayout()->MarkLayoutDirty();
}
#endif

void ULexLayoutFlexBoxSlot::SetIgnoreLayout(bool Value)
{
    if (bIgnoreLayout != Value)
    {
        bIgnoreLayout = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetMinWidth(float Value)
{
    if (MinWidth != Value)
    {
        MinWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetMinHeight(float Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetPreferredWidth(float Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetPreferredHeight(float Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetFlexibleWidth(float Value)
{
    if (FlexibleWidth != Value)
    {
        FlexibleWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutFlexBoxSlot::SetFlexibleHeight(float Value)
{
    if (FlexibleHeight != Value)
    {
        FlexibleHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}
