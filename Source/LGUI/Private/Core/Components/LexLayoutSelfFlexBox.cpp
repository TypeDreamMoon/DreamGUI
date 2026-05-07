// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "LGUI.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexVisual.h"

float FLexLayoutSize::CalculateSize(ULexWidget* Widget, bool IsVertical)const
{
    float CalculatedValue = IsVertical ? Widget->GetHeight() : Widget->GetWidth();
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutSizeType::Auto:
            if (auto Layout = Widget->GetLayoutContainer())
            {
                FVector2f Min, Max, Preferred;
                Layout->GetLayoutProperties(Min, Max, Preferred);
                CalculatedValue = IsVertical ? Preferred.Y : Preferred.X;
            }
            else if (auto Visual = Widget->GetVisual())
            {
                CalculatedValue = IsVertical ? Visual->GetPreferredHeight() : Visual->GetPreferredWidth();
            }
            else
            {
                CalculatedValue = FixedValue;
            }
            break;
        case ELexLayoutSizeType::Fixed:
            CalculatedValue = FixedValue;
            break;
        case ELexLayoutSizeType::Percent:
            if (auto ParentWidget = Widget->GetParent())
            {
                CalculatedValue = PercentValue * (IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth());
            }
            else
            {
                CalculatedValue = FixedValue;
            }
            break;
        }
    }
    return CalculatedValue;
}

float FLexLayoutMinMaxSize::CalculateSize(ULexWidget* Widget, bool IsVertical, bool IsMinOrMax)const
{
    float CalculatedValue = IsMinOrMax ? 0 : UE_MAX_FLT;
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutMinMaxSizeType::Auto:
            if (auto LayoutContainer = Widget->GetLayoutContainer())
            {
                int Axis = IsVertical ? 1 : 0;
                FVector2f Min, Max, Preferred;
                LayoutContainer->GetLayoutProperties(Min, Max, Preferred);
                CalculatedValue = IsMinOrMax ? Min[Axis] : Max[Axis];
            }
            break;
        case ELexLayoutMinMaxSizeType::Fixed:
            CalculatedValue = FixedValue;
            break;
        case ELexLayoutMinMaxSizeType::Percent:
            if (auto ParentWidget = Widget->GetParent())
            {
                CalculatedValue = PercentValue * (IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth());
            }
            else
            {
                CalculatedValue = FixedValue;
            }
            break;
        }
    }
    return CalculatedValue;
}

DECLARE_CYCLE_STAT(TEXT("LexLayout FlexBoxSelf"), STAT_LexLayoutFlexBoxSelf, STATGROUP_LGUI);
void ULexLayoutSelfFlexBox::CalculateSize()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutFlexBoxSelf);
    auto Widget = GetWidget();
    if (!Widget)return;
    bIsCalculatingSize = true;
    CalculatedPreferred.X = PreferredWidth.CalculateSize(Widget, false);
    if (PreferredWidth.bEnable)
    {
        CalculatedMin.X = MinWidth.CalculateSize(Widget, false, true);
    }
    else//if not enable width control then we also don't use minWidth, because layoutContainer could use minWidth to calculate and set the wrong width
    {
        CalculatedMin.X = CalculatedPreferred.X;
    }
    CalculatedPreferred.Y = PreferredHeight.CalculateSize(Widget, true);
    if (PreferredHeight.bEnable)
    {
        CalculatedMin.Y = MinHeight.CalculateSize(Widget, true, true);
    }
    else//if not enable height control then we also don't use minHeight, because layoutContainer could use minHeight to calculate and set the wrong height
    {
        CalculatedMin.Y = CalculatedPreferred.Y;
    }
    CalculatedMax.X = MaxWidth.CalculateSize(Widget, false, false);
    if (CalculatedMax.X < CalculatedMin.X)
    {
        CalculatedMax.X = UE_MAX_FLT;
    }
    CalculatedMax.Y = MaxHeight.CalculateSize(Widget, true, false);
    if (CalculatedMax.Y < CalculatedMin.Y)
    {
        CalculatedMax.Y = UE_MAX_FLT;
    }
    //clamp value
    CalculatedPreferred.X = FMath::Clamp(CalculatedPreferred.X, CalculatedMin.X, CalculatedMax.X);
    CalculatedPreferred.Y = FMath::Clamp(CalculatedPreferred.Y, CalculatedMin.Y, CalculatedMax.Y);

#if WITH_EDITOR
    if (PreferredWidth.Type == ELexLayoutSizeType::Auto)
    {
        PreferredWidth.AutoValue = CalculatedPreferred.X;
    }
    if (PreferredHeight.Type == ELexLayoutSizeType::Auto)
    {
        PreferredHeight.AutoValue = CalculatedPreferred.Y;
    }
#endif

    bool bShouldSetWidgetSize = true;
    if (auto ParentWidget = Widget->GetParent())
    {
        if (Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()) != nullptr)//if parent widget have FlexBoxContainer, then it will set this widget size
        {
            bShouldSetWidgetSize = false;
        }
    }
    if (bShouldSetWidgetSize)
    {
        auto AnchorMin = Widget->GetAnchorMin();
        auto AnchorMax = Widget->GetAnchorMax();
        if (AnchorMin.X != AnchorMax.X)//custom anchor not support
        {
            Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
        }
        if (AnchorMin.Y != AnchorMax.Y)
        {
            Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
        }
        Widget->SetSizeDelta(FVector2D(CalculatedPreferred.X, CalculatedPreferred.Y));
    }

    bIsCalculatingSize = false;
}

void ULexLayoutSelfFlexBox::OnTransformChanged()
{
}

void ULexLayoutSelfFlexBox::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
    if (bIsCalculatingSize)return;
    CalculateSize();
}

#if WITH_EDITOR
void ULexLayoutSelfFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    CalculateSize();
}

void ULexLayoutSelfFlexBox::PostInitProperties()
{
    Super::PostInitProperties();
}
#endif

FLexLayoutControlAnchorData ULexLayoutSelfFlexBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        if (PreferredWidth.bEnable)
        {
            Result.bCanControlHorizontalSize = true;
        }
        if (PreferredHeight.bEnable)
        {
            Result.bCanControlVerticalSize = true;
        }
    }
    return Result;
}

void ULexLayoutSelfFlexBox::GetLayoutProperties(FVector2f& OutMin, FVector2f& OutMax, FVector2f& OutPreferred)
{
    OutMin.X = CalculatedMin.X;
    OutMin.Y = CalculatedMin.Y;
    OutMax.X = CalculatedMax.X;
    OutMax.Y = CalculatedMax.Y;
    OutPreferred.X = CalculatedPreferred.X;
    OutPreferred.Y = CalculatedPreferred.Y;
}

float ULexLayoutSelfFlexBox::GetGrowForLayoutContainer(int Axis) const
{
    //If width not enable then it is danger to use Grow.
    //Because width is get from widget, after increase width by Grow the result will set to widget too, that will make width keep increasing.
    //Same problem for height.
    if (Axis == 0 && PreferredWidth.bEnable)
    {
        return Grow;
    }
    if (Axis == 1 && PreferredHeight.bEnable)
    {
        return Grow;
    }
    return 0;
}

float ULexLayoutSelfFlexBox::GetShrinkForLayoutContainer(int Axis) const
{
    //If width not enable then it is danger to use shrink.
    //Because width is get from widget, after decrease width by Shrink the result will set to widget too, that will make width keep decreasing.
    //Same problem for height.
    if (Axis == 0 && PreferredWidth.bEnable)
    {
        return Shrink;
    }
    if (Axis == 1 && PreferredHeight.bEnable)
    {
        return Shrink;
    }
    return 0;
}

bool ULexLayoutSelfFlexBox::GetSecondaryAxisSizeCanStretchByLayoutContainer(int SecondaryAxis) const
{
    if (SecondaryAxis == 0)
    {
        if (PreferredWidth.bEnable && PreferredHeight.Type == ELexLayoutSizeType::Auto)
        {
            return true;
        }
    }
    else
    {
        if (PreferredHeight.bEnable && PreferredHeight.Type == ELexLayoutSizeType::Auto)
        {
            return true;
        }
    }
    return false;
}

void ULexLayoutSelfFlexBox::SetSizeByLayoutContainer(FVector2f Value, int PrimaryAxis)
{
    auto Widget = GetWidget();
    if (!Widget)return;
    //if primary-axis is horizontal then width is allowed to set when PreferredWidth enabled,
    //yet height can only set when PreferredHeight enabled and type is Auto
    if (PrimaryAxis == 0)
    {
        if (PreferredHeight.Type != ELexLayoutSizeType::Auto)
        {
            Value.Y = CalculatedPreferred.Y;
        }
    }
    else
    {
        if (PreferredWidth.Type != ELexLayoutSizeType::Auto)
        {
            Value.X = CalculatedPreferred.X;
        }
    }
    //check Enable
    if (!PreferredWidth.bEnable)
    {
        Value.X = CalculatedPreferred.X;
    }
    if (!PreferredHeight.bEnable)
    {
        Value.Y = CalculatedPreferred.Y;
    }
    
    auto AnchorMin = Widget->GetAnchorMin();
    auto AnchorMax = Widget->GetAnchorMax();
    if (AnchorMin.X != AnchorMax.X)//custom anchor not support
    {
        Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
    }
    if (AnchorMin.Y != AnchorMax.Y)
    {
        Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), true, true);
    }
    Widget->SetSizeDelta(FVector2D(Value));

#if WITH_EDITOR
    if (PreferredWidth.Type == ELexLayoutSizeType::Auto)
    {
        PreferredWidth.AutoValue = Value.X;
    }
    if (PreferredHeight.Type == ELexLayoutSizeType::Auto)
    {
        PreferredHeight.AutoValue = Value.Y;
    }
#endif
}

void ULexLayoutSelfFlexBox::SetMinWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MinWidth != Value)
    {
        MinWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMinHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMaxWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MaxWidth != Value)
    {
        MaxWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMaxHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MaxHeight != Value)
    {
        MaxHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetPreferredWidth(const FLexLayoutSize& Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetPreferredHeight(const FLexLayoutSize& Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetGrow(float Value)
{
    if (Grow != Value)
    {
        Grow = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetParent());
    }
}

void ULexLayoutSelfFlexBox::SetShrink(float Value)
{
    if (Shrink != Value)
    {
        Shrink = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetParent());
    }
}

