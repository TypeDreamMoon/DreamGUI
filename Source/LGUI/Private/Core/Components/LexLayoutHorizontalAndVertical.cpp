// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutHorizontalAndVertical.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutHorizontalAndVertical::OnUpdateLayout()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    Children.Empty();
    for (auto& ChildWidget : Widget->GetUIChildren())
    {
        if (!ChildWidget->IsVisibleForLayout())continue;
        auto ChildLayoutSlot = (ULexLayoutHorizontalAndVerticalSlot*)ChildWidget->GetLayoutSlot();
        if (ChildLayoutSlot->GetIgnoreLayout())continue;
        Children.Add(ChildWidget);
    }
    bool bIsVertical = Direction == ELexLayoutDirection::Vertical;
    this->CalcAlongAxis(0, bIsVertical);
    if (this->SizeFitToChildren[0])
        this->SetSelfAlongAxis(0);
    this->SetChildrenAlongAxis(0, bIsVertical);
    this->CalcAlongAxis(1, bIsVertical);
    if (this->SizeFitToChildren[1])
        this->SetSelfAlongAxis(1);
    this->SetChildrenAlongAxis(1, bIsVertical);
}

void ULexLayoutHorizontalAndVertical::CalcAlongAxis(int Axis, bool bIsVertical)
{
    auto Widget = GetWidget();
    float CombinedPadding = Axis == 0 ? (Padding.Left + Padding.Right) : (Padding.Bottom + Padding.Top);
    bool ControlSize = ControlChildSize[Axis];
    bool bUseScale = UseChildScale[Axis];
    bool bChildForceExpandSize = ChildForceExpand[Axis];

    float TotalMin = CombinedPadding;
    float TotalPreferred = CombinedPadding;
    float TotalFlexible = 0;

    bool bAlongOtherAxis = (bIsVertical ^ (Axis == 1));
    auto ChildrenCount = Children.Num();
    for (int i = 0; i < ChildrenCount; i++)
    {
        auto ChildWidget = Children[i];
        float Min, Preferred, Flexible;
        GetChildSizes(ChildWidget, Axis, ControlSize, bChildForceExpandSize, Min, Preferred, Flexible);

        if (bUseScale)
        {
            float scaleFactor = ChildWidget->GetRelativeScale3D()[Axis + 1];
            Min *= scaleFactor;
            Preferred *= scaleFactor;
            Flexible *= scaleFactor;
        }

        if (bAlongOtherAxis)
        {
            TotalMin = FMath::Max(Min + CombinedPadding, TotalMin);
            TotalPreferred = FMath::Max(Preferred + CombinedPadding, TotalPreferred);
            TotalFlexible = FMath::Max(Flexible, TotalFlexible);
        }
        else
        {
            TotalMin += Min + Spacing;
            TotalPreferred += Preferred + Spacing;

            // Increment flexible size with element's flexible size.
            TotalFlexible += Flexible;
        }
    }

    if (!bAlongOtherAxis && ChildrenCount > 0)
    {
        TotalMin -= Spacing;
        TotalPreferred -= Spacing;
    }
    TotalPreferred = FMath::Max(TotalMin, TotalPreferred);
    if (SizeFitToChildren[Axis])
    {
        TotalFlexible = 0;
    }
    SetLayoutInputForAxis(TotalMin, TotalPreferred, TotalFlexible, Axis);
}

float ULexLayoutHorizontalAndVertical::GetStartOffset(int Axis, float RequiredSpaceWithoutPadding)
{
    auto Widget = GetWidget();
    float RequiredSpace = RequiredSpaceWithoutPadding + (Axis == 0 ? (Padding.Left + Padding.Right) : (Padding.Bottom + Padding.Top));
    float AvailableSpace = Axis == 0 ? Widget->GetWidth() : Widget->GetHeight();
    float SurplusSpace = AvailableSpace - RequiredSpace;
    float AlignmentOnAxis = GetAlignmentOnAxis(Axis);
    return (Axis == 0 ? Padding.Left : Padding.Top) + SurplusSpace * AlignmentOnAxis;
}
float ULexLayoutHorizontalAndVertical::GetAlignmentOnAxis(int Axis)
{
    if (Axis == 0)
        return ((int)ChildAlignment % 3) * 0.5f;
    else
        return ((int)ChildAlignment / 3) * 0.5f;
}
void ULexLayoutHorizontalAndVertical::SetLayoutInputForAxis(float TotalMin, float TotalPreferred, float TotalFlexible, int Axis)
{
    TotalMinSize[Axis] = TotalMin;
    TotalPreferredSize[Axis] = TotalPreferred;
    TotalFlexibleSize[Axis] = TotalFlexible;
}

void ULexLayoutHorizontalAndVertical::SetChildrenAlongAxis(int Axis, bool isVertical)
{
    auto Widget = GetWidget();
    float Size = Axis == 0 ? Widget->GetWidth() : Widget->GetHeight();
    bool bControlSize = ControlChildSize[Axis];
    bool bUseScale = UseChildScale[Axis];
    bool bChildForceExpandSize = ChildForceExpand[Axis];
    float AlignmentOnAxis = GetAlignmentOnAxis(Axis);

    bool bAlongOtherAxis = (isVertical ^ (Axis == 1));
    int StartIndex = bReverseArrangement ? Children.Num() - 1 : 0;
    int EndIndex = bReverseArrangement ? 0 : Children.Num();
    int Increment = bReverseArrangement ? -1 : 1;
    if (bAlongOtherAxis)
    {
        float InnerSize = Size - (Axis == 0 ? (Padding.Left + Padding.Right) : (Padding.Bottom + Padding.Top));

        for (int i = StartIndex; bReverseArrangement ? i >= EndIndex : i < EndIndex; i += Increment)
        {
            auto ChildWidget = Children[i];
            float Min, Preferred, Flexible;
            GetChildSizes(ChildWidget, Axis, bControlSize, bChildForceExpandSize, Min, Preferred, Flexible);
            float ScaleFactor = bUseScale ? ChildWidget->GetRelativeScale3D()[Axis + 1] : 1.0f;

            float RequiredSpace = FMath::Clamp(InnerSize, Min, Flexible > 0 ? Size : Preferred);
            float StartOffset = GetStartOffset(Axis, RequiredSpace * ScaleFactor);
            if (bControlSize)
            {
                SetChildAlongAxisWithScale(ChildWidget, Axis, StartOffset, RequiredSpace, ScaleFactor);
            }
            else
            {
                float OffsetInCell = (RequiredSpace - ChildWidget->GetSizeDelta()[Axis]) * AlignmentOnAxis;
                SetChildAlongAxisWithScale(ChildWidget, Axis, StartOffset + OffsetInCell, ScaleFactor);
            }
        }
    }
    else
    {
        float Pos = (Axis == 0 ? Padding.Left : Padding.Top);
        float ItemFlexibleMultiplier = 0;
        float SurplusSpace = Size - GetTotalPreferredSize(Axis);

        if (SurplusSpace > 0)
        {
            if (GetTotalFlexibleSize(Axis) == 0)
                Pos = GetStartOffset(Axis, GetTotalPreferredSize(Axis) - (Axis == 0 ? (Padding.Left + Padding.Right) : (Padding.Bottom + Padding.Top)));
            else if (GetTotalFlexibleSize(Axis) > 0)
                ItemFlexibleMultiplier = SurplusSpace / GetTotalFlexibleSize(Axis);
        }

        float MinMaxLerp = 0;
        if (GetTotalMinSize(Axis) != GetTotalPreferredSize(Axis))
            MinMaxLerp = FMath::Clamp((Size - GetTotalMinSize(Axis)) / (GetTotalPreferredSize(Axis) - GetTotalMinSize(Axis)), 0, 1);

        for (int i = StartIndex; bReverseArrangement ? i >= EndIndex : i < EndIndex; i += Increment)
        {
            auto ChildWidget = Children[i];
            float Min, Preferred, Flexible;
            GetChildSizes(ChildWidget, Axis, bControlSize, bChildForceExpandSize, Min, Preferred, Flexible);
            float ScaleFactor = bUseScale ? ChildWidget->GetRelativeScale3D()[Axis + 1] : 1.0f;

            float ChildSize = FMath::Lerp(Min, Preferred, MinMaxLerp);
            ChildSize += Flexible * ItemFlexibleMultiplier;
            if (bControlSize)
            {
                SetChildAlongAxisWithScale(ChildWidget, Axis, Pos, ChildSize, ScaleFactor);
            }
            else
            {
                float offsetInCell = (ChildSize - ChildWidget->GetSizeDelta()[Axis]) * AlignmentOnAxis;
                SetChildAlongAxisWithScale(ChildWidget, Axis, Pos + offsetInCell, ScaleFactor);
            }
            Pos += ChildSize * ScaleFactor + Spacing;
        }
    }
}

void ULexLayoutHorizontalAndVertical::SetSelfAlongAxis(int Axis)
{
    auto Widget = GetWidget();
    auto Size= FMath::Max(GetTotalPreferredSize(Axis), GetTotalMinSize(Axis));
    if (Axis == 0)
        Widget->SetWidth(Size);
    else
        Widget->SetHeight(Size);
}

void ULexLayoutHorizontalAndVertical::GetChildSizes(ULexWidget* ChildWidget, int Axis, bool bControlSize, bool bChildForceExpand,
                                                    float& OutMin, float& OutPreferred, float& OutFlexible)
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
            OutFlexible = ChildWidget->GetFlexibleWidth();
        }
        else
        {
            OutMin = ChildWidget->GetMinHeight();
            OutPreferred = ChildWidget->GetPreferredHeight();
            OutFlexible = ChildWidget->GetFlexibleHeight();
        }
    }

    if (bChildForceExpand)
        OutFlexible = FMath::Max(OutFlexible, 1);
}

void ULexLayoutHorizontalAndVertical::SetChildAlongAxisWithScale(ULexWidget* ChildWidget, int Axis, float Pos, float Size, float ScaleFactor)
{
    if (ChildWidget == nullptr)
        return;

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
    
    auto SizeDelta = ChildWidget->GetSize();
    SizeDelta[Axis] = Size;
    ChildWidget->SetSizeDelta(SizeDelta);

    auto AnchoredPosition = ChildWidget->GetAnchoredPosition();
    AnchoredPosition[Axis] = (Axis == 0) ? (Pos + Size * ChildWidget->GetPivot()[Axis] * ScaleFactor) : (-Pos - Size * (1.0f - ChildWidget->GetPivot()[Axis]) * ScaleFactor);
    auto ParentWidget = ChildWidget->GetUIParent();
    AnchoredPosition[Axis] += Axis == 0 ? -AnchorMin.X * ParentWidget->GetWidth() : (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(AnchoredPosition);
}

void ULexLayoutHorizontalAndVertical::SetChildAlongAxisWithScale(ULexWidget* ChildWidget, int Axis, float Pos, float ScaleFactor)
{
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

    auto AnchoredPosition = ChildWidget->GetAnchoredPosition();
    AnchoredPosition[Axis] = (Axis == 0) ? (Pos + ChildWidget->GetSizeDelta()[Axis] * ChildWidget->GetPivot()[Axis] * ScaleFactor) : (-Pos - ChildWidget->GetSizeDelta()[Axis] * (1.0f - ChildWidget->GetPivot()[Axis]) * ScaleFactor);
    auto ParentWidget = ChildWidget->GetUIParent();
    AnchoredPosition[Axis] += Axis == 0 ? -AnchorMin.X * ParentWidget->GetWidth() : (1 - AnchorMin.Y) * ParentWidget->GetHeight();
    ChildWidget->SetAnchoredPosition(AnchoredPosition);
}

void ULexLayoutHorizontalAndVertical::GetLayoutControlAnchor(ULexWidget* TargetWidget, FLexLayoutControlAnchorData& Result)
{
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        Result.bCanControlHorizontalSizeDelta = SizeFitToChildren.bWidth;
        Result.bCanControlVerticalSizeDelta = SizeFitToChildren.bHeight;
    }
    else if (ThisWidget->GetUIChildren().Contains(TargetWidget))//child
    {
        if (auto LayoutSlot = Cast<ULexLayoutHorizontalAndVerticalSlot>(TargetWidget->GetLayoutSlot()))
        {
            if (!LayoutSlot->GetIgnoreLayout())
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
    }
}

#if WITH_EDITOR
void ULexLayoutHorizontalAndVertical::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

TSubclassOf<ULexLayoutSlot> ULexLayoutHorizontalAndVertical::GetSlotClass() const
{
	return ULexLayoutHorizontalAndVerticalSlot::StaticClass();
}

void ULexLayoutHorizontalAndVertical::SetDirection(ELexLayoutDirection Value)
{
    if (Direction != Value)
    {
        Direction = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetPadding(FMargin Value)
{
    if (Padding != Value)
    {
        Padding = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetSpacing(float Value)
{
    if (Spacing != Value)
    {
        Spacing = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetChildAlignment(ELexLayoutChildAlignment Value)
{
    if (ChildAlignment != Value)
    {
        ChildAlignment = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetReverseArrangement(bool Value)
{
    if (bReverseArrangement != Value)
    {
        bReverseArrangement = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetControlChildSize(FLexLayoutHorizontalAndVerticalSizeControl Value)
{
    if (ControlChildSize != Value)
    {
        ControlChildSize = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetUseChildScale(FLexLayoutHorizontalAndVerticalSizeControl Value)
{
    if (UseChildScale != Value)
    {
        UseChildScale = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetChildForceExpand(FLexLayoutHorizontalAndVerticalSizeControl Value)
{
    if (ChildForceExpand != Value)
    {
        ChildForceExpand = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVertical::SetSizeFitToChildren(FLexLayoutHorizontalAndVerticalSizeControl Value)
{
    if (SizeFitToChildren != Value)
    {
        SizeFitToChildren = Value;
        MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::OnTransformChanged()
{
}

void ULexLayoutHorizontalAndVerticalSlot::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
}

#if WITH_EDITOR
void ULexLayoutHorizontalAndVerticalSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    GetLayout()->MarkLayoutDirty();
}
#endif

void ULexLayoutHorizontalAndVerticalSlot::SetIgnoreLayout(bool Value)
{
    if (bIgnoreLayout != Value)
    {
        bIgnoreLayout = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetMinWidth(float Value)
{
    if (MinWidth != Value)
    {
        MinWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetMinHeight(float Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetPreferredWidth(float Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetPreferredHeight(float Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetFlexibleWidth(float Value)
{
    if (FlexibleWidth != Value)
    {
        FlexibleWidth = Value;
        GetLayout()->MarkLayoutDirty();
    }
}

void ULexLayoutHorizontalAndVerticalSlot::SetFlexibleHeight(float Value)
{
    if (FlexibleHeight != Value)
    {
        FlexibleHeight = Value;
        GetLayout()->MarkLayoutDirty();
    }
}
