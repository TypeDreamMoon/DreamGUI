// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "LGUI.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexVisual.h"

float FLexLayoutSize::Calculate(ULexWidget* Widget, bool IsVertical) const
{
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutSizeType::Auto:
            if (auto Layout = Widget->GetLayoutContainer())
            {
                auto LayoutPreferredSize = Layout->GetLayoutPreferredSize();
                return IsVertical ? LayoutPreferredSize.Y : LayoutPreferredSize.X;
            }
            if (auto Visual = Widget->GetVisual())
            {
                return IsVertical ? Visual->GetPreferredHeight() : Visual->GetPreferredWidth();
            }
            return 0;
        case ELexLayoutSizeType::Fixed:
            return FixedValue;
        case ELexLayoutSizeType::Percent:
            if (auto ParentWidget = Widget->GetParent())
            {
                if (IsVertical)
                {
                    if (auto LayoutSelf = ParentWidget->GetLayoutSelf())
                    {
                        auto FinalSize = LayoutSelf->GetLayoutFinalSize();
                        return PercentValue * FinalSize.Y;
                    }
                    else
                    {
                        return PercentValue * ParentWidget->GetHeight();
                    }
                }
                else
                {
                    if (auto LayoutSelf = ParentWidget->GetLayoutSelf())
                    {
                        auto FinalSize = LayoutSelf->GetLayoutFinalSize();
                        return PercentValue * FinalSize.X;
                    }
                    else
                    {
                        return PercentValue * ParentWidget->GetWidth();
                    }
                }
            }
            return 0;//no valid parent, just return 0
        }
    }
    return IsVertical ? Widget->GetHeight() : Widget->GetWidth();
}

float FLexLayoutMinMaxSize::Calculate(ULexWidget* Widget, bool IsVertical,
    bool IsMinOrMax) const
{
    float CalculatedValue = IsMinOrMax ? -UE_MAX_FLT : UE_MAX_FLT;
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutMinMaxSizeType::Fixed:
            CalculatedValue = FixedValue;
            break;
        case ELexLayoutMinMaxSizeType::Percent:
            if (auto ParentWidget = Widget->GetParent())
            {
                if (IsVertical)
                {
                    if (auto LayoutSelf = ParentWidget->GetLayoutSelf())
                    {
                        auto FinalSize = LayoutSelf->GetLayoutFinalSize();
                        return PercentValue * FinalSize.Y;
                    }
                    else
                    {
                        return 0;
                    }
                }
                else
                {
                    if (auto LayoutSelf = ParentWidget->GetLayoutSelf())
                    {
                        auto FinalSize = LayoutSelf->GetLayoutFinalSize();
                        return PercentValue * FinalSize.X;
                    }
                    else
                    {
                        return CalculatedValue;
                    }
                }
            }
            else
            {
                return CalculatedValue;
            }
        }
    }
    return CalculatedValue;
}

DECLARE_CYCLE_STAT(TEXT("LexLayout FlexBoxSelf"), STAT_LexLayoutFlexBoxSelf, STATGROUP_LGUI);

void ULexLayoutSelfFlexBox::OnTransformChanged()
{
}

void ULexLayoutSelfFlexBox::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
    // if (bIsCalculatingSize)return;
    // CalculateSize();
}

#if WITH_EDITOR
void ULexLayoutSelfFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    Grow = FMath::Max(Grow, 0);
    Shrink = FMath::Max(Shrink, 0);
    // CalculateSize();
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

FVector2f ULexLayoutSelfFlexBox::GetLayoutPreferredSize()
{
    CalculateSize();
    return FVector2f(CalculatedPreferredWidth, CalculatedPreferredHeight);
}

FVector2f ULexLayoutSelfFlexBox::GetLayoutFinalSize()
{
    if (auto ParentWidget = GetWidget()->GetParent())
    {
        if (auto LayoutContainer = ParentWidget->GetLayoutContainer())
        {
            //since we calculate form root to leaf, final size should already be set by parent LayoutContainer
            return FVector2f(CalculatedFinalWidth, CalculatedFinalHeight);
        }
    }
    return FVector2f(CalculatedPreferredWidth, CalculatedPreferredHeight);
}

void ULexLayoutSelfFlexBox::GetLayoutMinMax(FVector2f& OutMin, FVector2f& OutMax)
{
    OutMin.X = CalculatedMinWidth;
    OutMin.Y = CalculatedMinHeight;
    OutMax.X = CalculatedMaxWidth;
    OutMax.Y = CalculatedMaxHeight;
}

void ULexLayoutSelfFlexBox::CalculateSize()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutFlexBoxSelf);
    auto Widget = GetWidget();
    if (!Widget)return;
    
    bIsCalculatingSize = true;
    {
        CalculatedPreferredWidth = PreferredWidth.Calculate(Widget, false);
        if (PreferredWidth.bEnable)
        {
            CalculatedMinWidth = MinWidth.Calculate(Widget, false, true);
        }
        else//if not enable width control then we also don't use minWidth, because layoutContainer could use minWidth to calculate and set the wrong width
        {
            CalculatedMinWidth = -UE_MAX_FLT;
        }
        CalculatedPreferredHeight = PreferredHeight.Calculate(Widget, true);
        if (PreferredHeight.bEnable)
        {
            CalculatedMinHeight = MinHeight.Calculate(Widget, true, true);
        }
        else//if not enable height control then we also don't use minHeight, because layoutContainer could use minHeight to calculate and set the wrong height
        {
            CalculatedMinHeight = -UE_MAX_FLT;
        }
        CalculatedMaxWidth = MaxWidth.Calculate(Widget, false, false);
        if (CalculatedMaxWidth < CalculatedMinWidth)
        {
            CalculatedMaxWidth = UE_MAX_FLT;
        }
        CalculatedMaxHeight = MaxHeight.Calculate(Widget, true, false);
        if (CalculatedMaxHeight < CalculatedMinHeight)
        {
            CalculatedMaxHeight = UE_MAX_FLT;
        }
        //clamp value
        CalculatedPreferredWidth = FMath::Clamp(CalculatedPreferredWidth, CalculatedMinWidth, CalculatedMaxWidth);
        CalculatedPreferredHeight = FMath::Clamp(CalculatedPreferredHeight, CalculatedMinHeight, CalculatedMaxHeight);
    }

    bool bShouldSetPreferredSize = true;
    if (auto ParentWidget = Widget->GetParent())
    {
        //if parent widget have FlexBoxContainer, then widget size should be set by it, because Grow/Shrink/Stretch is calculated by FlexBoxContainer
        if (auto ParentFlexBoxContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()))
        {
            bShouldSetPreferredSize = false;
        }
    }
    if (bShouldSetPreferredSize)
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
        Widget->SetSizeDelta(FVector2D(CalculatedPreferredWidth, CalculatedPreferredHeight));
    }
    
    bIsCalculatingSize = false;
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
        if (PreferredWidth.bEnable)
        {
            return true;
        }
    }
    else
    {
        if (PreferredHeight.bEnable)
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
            Value.Y = Widget->GetHeight();
        }
    }
    else
    {
        if (PreferredWidth.Type != ELexLayoutSizeType::Auto)
        {
            Value.X = Widget->GetWidth();
        }
    }
    this->CalculatedFinalWidth = Value.X;
    this->CalculatedFinalHeight = Value.Y;

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
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetMinHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetMaxWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MaxWidth != Value)
    {
        MaxWidth = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetMaxHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MaxHeight != Value)
    {
        MaxHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetPreferredWidth(const FLexLayoutSize& Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetPreferredHeight(const FLexLayoutSize& Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget());
    }
}

void ULexLayoutSelfFlexBox::SetGrow(float Value)
{
    if (Grow != Value)
    {
        Grow = FMath::Max(0, Value);
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetParent());
    }
}

void ULexLayoutSelfFlexBox::SetShrink(float Value)
{
    if (Shrink != Value)
    {
        Shrink = FMath::Max(0, Value);
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetParent());
    }
}

