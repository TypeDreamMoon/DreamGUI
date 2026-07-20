// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexVisual.h"

namespace LexLayoutSelfFlexBoxLocal
{
	static float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static FMargin SanitizeMargin(const FMargin& Value)
	{
		return FMargin(
			NonNegativeFinite(Value.Left),
			NonNegativeFinite(Value.Top),
			NonNegativeFinite(Value.Right),
			NonNegativeFinite(Value.Bottom));
	}
}

float FLexLayoutSize::Calculate(ULexWidget* Widget, bool IsVertical) const
{
	if (!IsValid(Widget)) return 0.0f;
	if (!bEnable)
	{
		return LexLayoutSelfFlexBoxLocal::NonNegativeFinite(IsVertical ? Widget->GetHeight() : Widget->GetWidth());
	}

	switch (Type)
	{
	case ELexLayoutSizeType::Auto:
		if (ULexLayoutContainer* LayoutContainer = Widget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			const FVector2f LayoutPreferredSize = LayoutContainer->GetLayoutPreferredSize();
			const float Value = IsVertical ? LayoutPreferredSize.Y : LayoutPreferredSize.X;
			if (const ULexLayoutContainerSizeBox* SizeBox = Cast<ULexLayoutContainerSizeBox>(LayoutContainer);
				IsValid(SizeBox) && (IsVertical ? SizeBox->bOverrideHeight : SizeBox->bOverrideWidth))
			{
				return LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Value);
			}
			if (FMath::IsFinite(Value) && Value > 0.0f) return Value;
		}
		if (ULexVisual* Visual = Widget->GetVisual(); IsValid(Visual))
		{
			const float Value = IsVertical ? Visual->GetPreferredHeight() : Visual->GetPreferredWidth();
			if (FMath::IsFinite(Value) && Value >= 0.0f) return Value;
		}
		return FMath::IsFinite(FixedValue) ? FMath::Max(0.0f, FixedValue) : 0.0f;
	case ELexLayoutSizeType::Fixed:
		return FMath::IsFinite(FixedValue) ? FMath::Max(0.0f, FixedValue) : 0.0f;
	case ELexLayoutSizeType::Percent:
		if (ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
		{
			float FinalSize = IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth();
			if (ULexLayoutSelf* ParentLayoutSelf = ParentWidget->GetLayoutSelf(); IsValid(ParentLayoutSelf))
			{
				const FVector2f LayoutSize = ParentLayoutSelf->GetLayoutFinalSize();
				FinalSize = IsVertical ? LayoutSize.Y : LayoutSize.X;
			}
			if (const ULexLayoutContainerFlexBox* ParentLayoutContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()))
			{
				const FMargin Padding = LexLayoutSelfFlexBoxLocal::SanitizeMargin(ParentLayoutContainer->GetPadding());
				FinalSize -= IsVertical ? Padding.Top + Padding.Bottom : Padding.Left + Padding.Right;
			}
			const float Percent = FMath::IsFinite(PercentValue) ? FMath::Clamp(PercentValue, 0.0f, 1.0f) : 0.0f;
			return Percent * LexLayoutSelfFlexBoxLocal::NonNegativeFinite(FinalSize);
		}
		return 0.0f;
	}
	return 0.0f;
}

float FLexLayoutMinMaxSize::Calculate(ULexWidget* Widget, bool IsVertical,
    bool IsMinOrMax) const
{
	const float DisabledValue = IsMinOrMax ? -UE_MAX_FLT : UE_MAX_FLT;
	if (!bEnable || !IsValid(Widget)) return DisabledValue;
	if (Type == ELexLayoutMinMaxSizeType::Fixed)
	{
		return FMath::IsFinite(FixedValue) ? FMath::Max(0.0f, FixedValue) : DisabledValue;
	}
	if (ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
	{
		float FinalSize = IsVertical ? ParentWidget->GetHeight() : ParentWidget->GetWidth();
		if (ULexLayoutSelf* ParentLayoutSelf = ParentWidget->GetLayoutSelf(); IsValid(ParentLayoutSelf))
		{
			const FVector2f LayoutSize = ParentLayoutSelf->GetLayoutFinalSize();
			FinalSize = IsVertical ? LayoutSize.Y : LayoutSize.X;
		}
		if (const ULexLayoutContainerFlexBox* ParentLayoutContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()))
		{
			const FMargin Padding = LexLayoutSelfFlexBoxLocal::SanitizeMargin(ParentLayoutContainer->GetPadding());
			FinalSize -= IsVertical ? Padding.Top + Padding.Bottom : Padding.Left + Padding.Right;
		}
		const float Percent = FMath::IsFinite(PercentValue) ? FMath::Clamp(PercentValue, 0.0f, 1.0f) : 0.0f;
		return Percent * LexLayoutSelfFlexBoxLocal::NonNegativeFinite(FinalSize);
	}
	return DisabledValue;
}

FName ULexLayoutSelfFlexBox::GetPropertyName_PreferredWidth()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, PreferredWidth);
}
FName ULexLayoutSelfFlexBox::GetPropertyName_PreferredHeight()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, PreferredHeight);
}
FName ULexLayoutSelfFlexBox::GetPropertyName_MinWidth()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, MinWidth);
}
FName ULexLayoutSelfFlexBox::GetPropertyName_MinHeight()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, MinHeight);
}
FName ULexLayoutSelfFlexBox::GetPropertyName_MaxWidth()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, MaxWidth);
}
FName ULexLayoutSelfFlexBox::GetPropertyName_MaxHeight()
{
    return GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, MaxHeight);
}

void ULexLayoutSelfFlexBox::OnTransformChanged()
{
}

void ULexLayoutSelfFlexBox::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
    if (InWidthChange || InHeightChange)
    {
        bIsSizeDirty = true;
    }
}

#if WITH_EDITOR
void ULexLayoutSelfFlexBox::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Grow = FMath::IsFinite(Grow) ? FMath::Max(Grow, 0.0f) : 0.0f;
	Shrink = FMath::IsFinite(Shrink) ? FMath::Max(Shrink, 0.0f) : 0.0f;
	auto SanitizeSize = [](FLexLayoutSize& Size)
	{
		Size.FixedValue = FMath::IsFinite(Size.FixedValue) ? FMath::Max(0.0f, Size.FixedValue) : 0.0f;
		Size.PercentValue = FMath::IsFinite(Size.PercentValue) ? FMath::Clamp(Size.PercentValue, 0.0f, 1.0f) : 0.0f;
	};
	auto SanitizeMinMax = [](FLexLayoutMinMaxSize& Size)
	{
		Size.FixedValue = FMath::IsFinite(Size.FixedValue) ? FMath::Max(0.0f, Size.FixedValue) : 0.0f;
		Size.PercentValue = FMath::IsFinite(Size.PercentValue) ? FMath::Clamp(Size.PercentValue, 0.0f, 1.0f) : 0.0f;
	};
	SanitizeSize(PreferredWidth);
	SanitizeSize(PreferredHeight);
	SanitizeMinMax(MinWidth);
	SanitizeMinMax(MinHeight);
	SanitizeMinMax(MaxWidth);
	SanitizeMinMax(MaxHeight);
	Margin = LexLayoutSelfFlexBoxLocal::SanitizeMargin(Margin);
}

bool ULexLayoutSelfFlexBox::CanEditChange(const FProperty* InProperty) const
{
    bool bCanEditChange = Super::CanEditChange(InProperty);
    if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexLayoutSelfFlexBox, Margin))
    {
        if (auto Widget = GetWidget())
        {
            if (auto ParentWidget = Widget->GetParent())
            {
                if (auto LayoutContainer = Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer()))
                {
                    bCanEditChange = true;
                }
                else
                {
                    bCanEditChange = false;
                }
            }
        }
    }
    return bCanEditChange;
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
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget)) return FVector2f::ZeroVector;
	const FVector2f CurrentSize(
		LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Widget->GetWidth()),
		LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Widget->GetHeight()));

	// A panel assignment is only authoritative while its slot still owns the arranged
	// geometry. Once the slot restores authored geometry (or this item opts out), the
	// cached final size belongs to the previous layout pass and must not leak downstream.
	if (GetIgnoreLayoutContainer())
	{
		return CurrentSize;
	}
	const ULexPanelSlot* PanelSlot = Widget->GetPanelSlot();
	if (IsValid(PanelSlot) && !PanelSlot->HasLayoutGeometryApplied())
	{
		return CurrentSize;
	}

	if (ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
	{
		ULexLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer();
		const bool bAssignedByFlexContainer = IsValid(Cast<ULexLayoutContainerFlexBox>(ParentLayout));
		const bool bAssignedByPanel = IsValid(Cast<ULexPanelLayoutBase>(ParentLayout))
			&& IsValid(PanelSlot) && PanelSlot->HasLayoutGeometryApplied();
		if (bAssignedByFlexContainer || bAssignedByPanel)
		{
			// Layout runs root-to-leaf, so the parent has already assigned this size.
			return FVector2f(
				LexLayoutSelfFlexBoxLocal::NonNegativeFinite(CalculatedFinalWidth),
				LexLayoutSelfFlexBoxLocal::NonNegativeFinite(CalculatedFinalHeight));
		}
	}
	return CurrentSize;
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
    if (!bIsLayoutDirty)return;
    bIsLayoutDirty = false;

    auto Widget = GetWidget();
    if (!Widget)return;
    
	auto PrevSize = FVector2f(CalculatedPreferredWidth, CalculatedPreferredHeight);
    
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
		if (Cast<ULexLayoutContainerFlexBox>(ParentWidget->GetLayoutContainer())
			|| Cast<ULexPanelLayoutBase>(ParentWidget->GetLayoutContainer()))
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

	(void)PrevSize;
}

void ULexLayoutSelfFlexBox::MarkLayoutDirty()
{
	Super::MarkLayoutDirty();
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
	if (!IsValid(Widget))return;
	Value.X = LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Value.X);
	Value.Y = LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Value.Y);

    this->CalculatedFinalWidth = Value.X;
    this->CalculatedFinalHeight = Value.Y;
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

void ULexLayoutSelfFlexBox::SetMargin(const FMargin& Value)
{
	const FMargin Sanitized = LexLayoutSelfFlexBoxLocal::SanitizeMargin(Value);
	if (Margin != Sanitized)
    {
		Margin = Sanitized;
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
	Value = LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Value);
    if (Grow != Value)
    {
		Grow = Value;
		if (ULexWidget* Widget = GetWidget(); IsValid(Widget))
        {
			ULexWidget::MarkLayoutForRebuild(Widget->GetParent());
        }
    }
}

void ULexLayoutSelfFlexBox::SetShrink(float Value)
{
	Value = LexLayoutSelfFlexBoxLocal::NonNegativeFinite(Value);
    if (Shrink != Value)
    {
		Shrink = Value;
		if (ULexWidget* Widget = GetWidget(); IsValid(Widget))
        {
			ULexWidget::MarkLayoutForRebuild(Widget->GetParent());
        }
    }
}

