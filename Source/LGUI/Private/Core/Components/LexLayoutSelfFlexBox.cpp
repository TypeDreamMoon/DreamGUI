// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "LGUI.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexVisual.h"

TOptional<float> FLexLayoutSize::Calculate(ULexWidget* Widget, ELexLayoutUpdateType UpdateType, bool IsVertical) const
{
    if (bEnable)
    {
        switch (Type)
        {
        case ELexLayoutSizeType::Auto:
            if (auto Layout = Widget->GetLayoutContainer())
            {
                FVector2f LayoutPreferredSize;
                Layout->GetLayoutProperties(LayoutPreferredSize);
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
                    float FinalSize;
                    if (ParentWidget->GetLayoutFinalHeight(FinalSize))
                    {
                        return PercentValue * FinalSize;
                    }
                    else
                    {
                        return 0;
                    }
                }
                else
                {
                    float FinalSize;
                    if (ParentWidget->GetLayoutFinalWidth(FinalSize))
                    {
                        return PercentValue * FinalSize;
                    }
                    else
                    {
                        return 0;
                    }
                }
            }
            return 0;//no valid parent, just return 0
        }
    }
    return IsVertical ? Widget->GetHeight() : Widget->GetWidth();
}

float FLexLayoutMinMaxSize::Calculate(ULexWidget* Widget, ELexLayoutUpdateType UpdateType, bool IsVertical,
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
            if (UpdateType == ELexLayoutUpdateType::FirstPass_RootToLeaf)
            {
                if (auto ParentWidget = Widget->GetParent())
                {
                    if (IsVertical)
                    {
                        float FinalSize;
                        if (ParentWidget->GetLayoutFinalHeight(FinalSize))
                        {
                            return PercentValue * FinalSize;
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        float FinalSize;
                        if (ParentWidget->GetLayoutFinalWidth(FinalSize))
                        {
                            return PercentValue * FinalSize;
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
            break;
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

void ULexLayoutSelfFlexBox::GetLayoutProperties(FVector2f& OutPreferred)
{
    auto Widget = GetWidget();
    if (!Widget->GetLayoutPreferredWidth(OutPreferred.X))
    {
        UE_LOG(LGUI, Error, TEXT(""));
    }
    if (!Widget->GetLayoutPreferredHeight(OutPreferred.Y))
    {
        UE_LOG(LGUI, Error, TEXT(""));
    }
}

void ULexLayoutSelfFlexBox::GetLayoutMinMax(FVector2f& OutMin, FVector2f& OutMax)
{
    OutMin.X = CalculatingMinWidth;
    OutMin.Y = CalculatingMinHeight;
    OutMax.X = CalculatingMaxWidth;
    OutMax.Y = CalculatingMaxHeight;
}

ELexLayoutSelfSizeFitType ULexLayoutSelfFlexBox::GetWidthFitType() const
{
    if (PreferredWidth.bEnable)
    {
        switch (PreferredWidth.Type)
        {
        case ELexLayoutSizeType::Auto:
            {
                if (auto LayoutContainer = GetWidget()->GetLayoutContainer())
                {
                    return ELexLayoutSelfSizeFitType::FitChildren;
                }
            }
        case ELexLayoutSizeType::Percent:
            return ELexLayoutSelfSizeFitType::FitParent;
        }
    }
    return ELexLayoutSelfSizeFitType::None;
}

ELexLayoutSelfSizeFitType ULexLayoutSelfFlexBox::GetHeightFitType() const
{
    if (PreferredHeight.bEnable)
    {
        switch (PreferredHeight.Type)
        {
        case ELexLayoutSizeType::Auto:
            {
                if (auto LayoutContainer = GetWidget()->GetLayoutContainer())
                {
                    return ELexLayoutSelfSizeFitType::FitChildren;
                }
            }
        case ELexLayoutSizeType::Percent:
            return ELexLayoutSelfSizeFitType::FitParent;
        }
    }
    return ELexLayoutSelfSizeFitType::None;
}

void ULexLayoutSelfFlexBox::CalculateSize(ELexLayoutUpdateType UpdateType
    , TOptional<float>& OutPreferredWidth, TOptional<float>& OutPreferredHeight
    , TOptional<float>& OutStretchedWidth, TOptional<float>& OutStretchedHeight)
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutFlexBoxSelf);
    auto Widget = GetWidget();
    if (!Widget)return;

    if (bIsAnimationPlaying)
    {
        bShouldRebuildLayoutAfterAnimation = true;
        return;
    }
    
    bIsCalculatingSize = true;
    {
        if (!OutPreferredWidth.IsSet())
        {
            OutPreferredWidth = PreferredWidth.Calculate(Widget, UpdateType, false);
        }
        if (PreferredWidth.bEnable)
        {
            CalculatingMinWidth = MinWidth.Calculate(Widget, UpdateType, false, true);
        }
        else//if not enable width control then we also don't use minWidth, because layoutContainer could use minWidth to calculate and set the wrong width
        {
            CalculatingMinWidth = -UE_MAX_FLT;
        }
        if (!OutPreferredHeight.IsSet())
        {
            OutPreferredHeight = PreferredHeight.Calculate(Widget, UpdateType, true);
        }
        if (PreferredHeight.bEnable)
        {
            CalculatingMinHeight = MinHeight.Calculate(Widget, UpdateType, true, true);
        }
        else//if not enable height control then we also don't use minHeight, because layoutContainer could use minHeight to calculate and set the wrong height
        {
            CalculatingMinHeight = -UE_MAX_FLT;
        }
        CalculatingMaxWidth = MaxWidth.Calculate(Widget, UpdateType, false, false);
        if (CalculatingMaxWidth < CalculatingMinWidth)
        {
            CalculatingMaxWidth = UE_MAX_FLT;
        }
        CalculatingMaxHeight = MaxHeight.Calculate(Widget, UpdateType, true, false);
        if (CalculatingMaxHeight < CalculatingMinHeight)
        {
            CalculatingMaxHeight = UE_MAX_FLT;
        }
        //clamp value
        if (OutPreferredWidth.IsSet())
        {
            OutPreferredWidth = FMath::Clamp(OutPreferredWidth.GetValue(), CalculatingMinWidth, CalculatingMaxWidth);
        }
        if (OutPreferredHeight.IsSet())
        {
            OutPreferredHeight = FMath::Clamp(OutPreferredHeight.GetValue(), CalculatingMinHeight, CalculatingMaxHeight);
        }
        //stretched size
        bool bShouldSetStretchedWidth = true, bShouldSetStretchedHeight = true;
        if (auto ParentWidget = Widget->GetParent())
        {
            if (auto ParentFlexBoxContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()))
            {
                bool ContainsPrimaryStretchedSize = GetGrowForLayoutContainer(ParentFlexBoxContainer->GetPrimaryAxis()) > 0
                || GetShrinkForLayoutContainer(ParentFlexBoxContainer->GetPrimaryAxis()) > 0;
                bool ContainsSecondaryStretchedSize = ParentFlexBoxContainer->GetSecondaryAlignment() == ELexLayoutFlexBoxSecondaryAxisAlignment::Stretch
                && ParentFlexBoxContainer->GetSecondaryLineAlignment() == ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch;
                if (ParentFlexBoxContainer->GetPrimaryAxis() == 0)
                {
                    bShouldSetStretchedWidth = !ContainsPrimaryStretchedSize;
                    bShouldSetStretchedHeight = !ContainsSecondaryStretchedSize;
                }
                else
                {
                    bShouldSetStretchedHeight = !ContainsPrimaryStretchedSize;
                    bShouldSetStretchedWidth = !ContainsSecondaryStretchedSize;
                }
            }
        }
        if (bShouldSetStretchedWidth)
        {
            OutStretchedWidth = 0;
        }
        if (bShouldSetStretchedHeight)
        {
            OutStretchedHeight = 0;
        }
    }
    if (UpdateType == ELexLayoutUpdateType::SecondPass_LeafToRoot)
    {
        if (!OutPreferredWidth.IsSet())
        {
            auto LayoutContainer = Widget->GetLayoutContainer();
            check(LayoutContainer);
            FVector2f LayoutContainerPreferredSize;
            LayoutContainer->GetLayoutProperties(LayoutContainerPreferredSize);
            OutPreferredWidth = LayoutContainerPreferredSize.X;
        }
        if (!OutPreferredHeight.IsSet())
        {
            auto LayoutContainer = Widget->GetLayoutContainer();
            check(LayoutContainer);
            FVector2f LayoutContainerPreferredSize;
            LayoutContainer->GetLayoutProperties(LayoutContainerPreferredSize);
            OutPreferredHeight = LayoutContainerPreferredSize.Y;
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
            Widget->SetSizeDelta(FVector2D(OutPreferredWidth.GetValue(), OutPreferredHeight.GetValue()));

            BeginSetupAnimations();

            ELexLayoutAnimationType TempAnimationType = AnimationType;
#if WITH_EDITOR
            if (!this->GetWorld()->IsGameWorld())
            {
                TempAnimationType = ELexLayoutAnimationType::Immediately;
            }
#endif
            ApplySizeDeltaWithAnimation(TempAnimationType, FVector2D(OutPreferredWidth.GetValue(), OutPreferredHeight.GetValue()), Widget);

            if (TempAnimationType == ELexLayoutAnimationType::EaseAnimation)
            {
                EndSetupAnimations();
            }
        }
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
            if (!Widget->GetLayoutPreferredHeight(Value.Y))
            {
                UE_LOG(LGUI, Error, TEXT("NotSet"));
            }
        }
    }
    else
    {
        if (PreferredWidth.Type != ELexLayoutSizeType::Auto)
        {
            if (!Widget->GetLayoutPreferredWidth(Value.X))
            {
                UE_LOG(LGUI, Error, TEXT("NotSet"));
            } 
        }
    }
    Widget->SetLayoutFinalWidth(Value.X);
    Widget->SetLayoutFinalHeight(Value.Y);

    BeginSetupAnimations();

    ELexLayoutAnimationType TempAnimationType = AnimationType;
#if WITH_EDITOR
    if (!this->GetWorld()->IsGameWorld())
    {
        TempAnimationType = ELexLayoutAnimationType::Immediately;
    }
#endif
    
    ApplySizeDeltaWithAnimation(TempAnimationType, FVector2D(Value), Widget);

    if (TempAnimationType == ELexLayoutAnimationType::EaseAnimation)
    {
        EndSetupAnimations();
    }
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
        // CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMinHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        // CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMaxWidth(const FLexLayoutMinMaxSize& Value)
{
    if (MaxWidth != Value)
    {
        MaxWidth = Value;
        // CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetMaxHeight(const FLexLayoutMinMaxSize& Value)
{
    if (MaxHeight != Value)
    {
        MaxHeight = Value;
        // CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetPreferredWidth(const FLexLayoutSize& Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        // CalculateSize();
    }
}

void ULexLayoutSelfFlexBox::SetPreferredHeight(const FLexLayoutSize& Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        // CalculateSize();
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

