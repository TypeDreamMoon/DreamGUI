// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutFlexBoxSelf.h"

#include "Core/Components/LexLayoutFlexBoxContainer.h"
#include "Core/Components/LexVisual.h"

float FLexLayoutSize::CalculateSize(ULexWidget* Widget, bool IsVertical)const
{
    float CalculatedValue = IsVertical ? Widget->GetHeight() : Widget->GetWidth();
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutSizeType::Auto:
            if (auto Layout = Cast<ULexLayoutFlexBoxContainer>(Widget->GetLayoutContainer()))
            {
                CalculatedValue = IsVertical ? Layout->GetPreferredHeightPixelValue() : Layout->GetPreferredWidthPixelValue();
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
            if (auto ParentWidget = Widget->GetUIParent())
            {
                CalculatedValue = PercentValue * 0.01f * (IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth());
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
        case ELexLayoutMinMaxSizeType::Fixed:
            CalculatedValue = FixedValue;
            break;
        case ELexLayoutMinMaxSizeType::Percent:
            if (auto ParentWidget = Widget->GetUIParent())
            {
                CalculatedValue = PercentValue * 0.01f * (IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth());
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

void ULexLayoutFlexBoxSelf::CalculateSize()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    bIsCalculatingSize = true;
    CalculatedWidth = PreferredWidth.CalculateSize(Widget, false);
    if (PreferredWidth.bEnable)
    {
        CalculatedMinWidth = MinWidth.CalculateSize(Widget, false, true);
    }
    else//if not enable width control then we also don't use minWidth, because layoutContainer could use minWidth to calculate and set the wrong width
    {
        CalculatedMinWidth = CalculatedWidth;
    }
    CalculatedHeight = PreferredHeight.CalculateSize(Widget, true);
    if (PreferredHeight.bEnable)
    {
        CalculatedMinHeight = MinHeight.CalculateSize(Widget, true, true);
    }
    else//if not enable height control then we also don't use minHeight, because layoutContainer could use minHeight to calculate and set the wrong height
    {
        CalculatedMinHeight = CalculatedHeight;
    }
    CalculatedMaxWidth = MaxWidth.CalculateSize(Widget, false, false);
    if (CalculatedMaxWidth < CalculatedMinWidth)
    {
        CalculatedMaxWidth = UE_MAX_FLT;
    }
    CalculatedMaxHeight = MaxHeight.CalculateSize(Widget, true, false);
    if (CalculatedMaxHeight < CalculatedMinHeight)
    {
        CalculatedMaxHeight = UE_MAX_FLT;
    }
    //clamp value before AspectRatio calculation
    CalculatedWidth = FMath::Clamp(CalculatedWidth, CalculatedMinWidth, CalculatedMaxWidth);
    CalculatedHeight = FMath::Clamp(CalculatedHeight, CalculatedMinHeight, CalculatedMaxHeight);
    switch (AspectRatio.Type)
    {
    case ELexLayoutAspectRatioType::None:
        break;
    case ELexLayoutAspectRatioType::HeightControlWidth:
        CalculatedWidth = CalculatedHeight * AspectRatio.Value;
        break;
    case ELexLayoutAspectRatioType::WidthControlHeight:
        CalculatedHeight = CalculatedWidth / AspectRatio.Value;
        break;
    }
    //clamp again because AspectRatio calculation
    CalculatedWidth = FMath::Clamp(CalculatedWidth, CalculatedMinWidth, CalculatedMaxWidth);
    CalculatedHeight = FMath::Clamp(CalculatedHeight, CalculatedMinHeight, CalculatedMaxHeight);

#if WITH_EDITOR
    if (PreferredWidth.Type == ELexLayoutSizeType::Auto)
    {
        PreferredWidth.AutoValue = CalculatedWidth;
    }
    if (PreferredHeight.Type == ELexLayoutSizeType::Auto)
    {
        PreferredHeight.AutoValue = CalculatedHeight;
    }
#endif

    bool bShouldSetWidgetSize = true;
    if (auto ParentWidget = Widget->GetUIParent())
    {
        if (auto LayoutContainer = Cast<ULexLayoutFlexBoxContainer>(ParentWidget->GetLayoutContainer()))
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
            Widget->SetHorizontalAnchorMinMax(FVector2D(0, 0), true, true);
        }
        if (AnchorMin.Y != AnchorMax.Y)
        {
            Widget->SetVerticalAnchorMinMax(FVector2D(1, 1), true, true);
        }
        Widget->SetSizeDelta(FVector2D(CalculatedWidth, CalculatedHeight));
    }

    bIsCalculatingSize = false;
}

void ULexLayoutFlexBoxSelf::OnTransformChanged()
{
}

void ULexLayoutFlexBoxSelf::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
    if (bIsCalculatingSize)return;
    CalculateSize();
}

#if WITH_EDITOR
void ULexLayoutFlexBoxSelf::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    CalculateSize();
}

void ULexLayoutFlexBoxSelf::PostInitProperties()
{
    Super::PostInitProperties();
}
#endif

FLexLayoutControlAnchorData ULexLayoutFlexBoxSelf::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
        if (PreferredWidth.bEnable)
        {
            if (ThisWidget->GetLayoutContainer() != nullptr || ThisWidget->GetVisual() != nullptr)
            {
                Result.bCanControlHorizontalSize = true;
            }
        }
        if (PreferredHeight.bEnable)
        {
            if (ThisWidget->GetLayoutContainer() != nullptr || ThisWidget->GetVisual() != nullptr)
            {
                Result.bCanControlVerticalSize = true;
            }
        }
        if (AspectRatio.Type == ELexLayoutAspectRatioType::HeightControlWidth)
        {
            Result.bCanControlHorizontalSize = true;
        }
        else if (AspectRatio.Type == ELexLayoutAspectRatioType::WidthControlHeight)
        {
            Result.bCanControlVerticalSize = true;
        }
    }
    return Result;
}

float ULexLayoutFlexBoxSelf::GetGrowForLayoutContainer(int Axis, bool IsPrimary) const
{
    if (IsPrimary)
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
    }
    return 0;
}

float ULexLayoutFlexBoxSelf::GetShrinkForLayoutContainer(int Axis, bool IsPrimary) const
{
    if (IsPrimary)
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
    }
    return 0;
}

bool ULexLayoutFlexBoxSelf::SecondarySizeCanStretch(int SecondaryAxis) const
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

void ULexLayoutFlexBoxSelf::SetSizeByLayoutContainer(FVector2f Value, int PrimaryAxis)
{
    auto Widget = GetWidget();
    if (!Widget)return;
    //if primary-axis is horizontal then width is allowed to set when PreferredWidth enabled,
    //yet height can only set when PreferredHeight enabled and type is Auto
    if (PrimaryAxis == 0)
    {
        if (PreferredHeight.Type != ELexLayoutSizeType::Auto)
        {
            Value.Y = CalculatedHeight;
        }
    }
    else
    {
        if (PreferredWidth.Type != ELexLayoutSizeType::Auto)
        {
            Value.X = CalculatedWidth;
        }
    }
    //check Enable
    if (!PreferredWidth.bEnable)
    {
        Value.X = CalculatedWidth;
    }
    if (!PreferredHeight.bEnable)
    {
        Value.Y = CalculatedHeight;
    }
    
    auto AnchorMin = Widget->GetAnchorMin();
    auto AnchorMax = Widget->GetAnchorMax();
    if (AnchorMin.X != AnchorMax.X)//custom anchor not support
    {
        Widget->SetHorizontalAnchorMinMax(FVector2D(0, 0), true, true);
    }
    if (AnchorMin.Y != AnchorMax.Y)
    {
        Widget->SetVerticalAnchorMinMax(FVector2D(1, 1), true, true);
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

void ULexLayoutFlexBoxSelf::SetMinWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MinWidth != Value)
    {
        MinWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetMinHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetMaxWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MaxWidth != Value)
    {
        MaxWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetMaxHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MaxHeight != Value)
    {
        MaxHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetAspectRatio(const FLexLayoutAspectRatio& Value)
{
    if (AspectRatio != Value)
    {
        AspectRatio = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetPreferredWidth(const FLexLayoutSize& Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetPreferredHeight(const FLexLayoutSize& Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        CalculateSize();
    }
}

void ULexLayoutFlexBoxSelf::SetGrow(float Value)
{
    if (Grow != Value)
    {
        Grow = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutFlexBoxSelf::SetShrink(float Value)
{
    if (Shrink != Value)
    {
        Shrink = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

