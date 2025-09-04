// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutCommonSlot.h"

void ULexLayoutCommonSlot::OnTransformChanged()
{
}

void ULexLayoutCommonSlot::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
}

#if WITH_EDITOR
void ULexLayoutCommonSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
}

void ULexLayoutCommonSlot::PostInitProperties()
{
    Super::PostInitProperties();
    if (auto Widget = GetWidget())
    {
        if (auto World = GetWorld())
        {
            if (!World->IsGameWorld())
            {
                //use widget's default size as preferred size
                this->PreferredWidth = Widget->GetWidth();
                this->PreferredHeight = Widget->GetHeight();
            }
        }
    }
}
#endif

void ULexLayoutCommonSlot::SetIgnoreLayout(bool Value)
{
    if (bIgnoreLayout != Value)
    {
        bIgnoreLayout = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetMinWidth(float Value)
{
    if (MinWidth != Value)
    {
        MinWidth = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetMinHeight(float Value)
{
    if (MinHeight != Value)
    {
        MinHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetPreferredWidth(float Value)
{
    if (PreferredWidth != Value)
    {
        PreferredWidth = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetPreferredHeight(float Value)
{
    if (PreferredHeight != Value)
    {
        PreferredHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetFlexibleWidth(float Value)
{
    if (FlexibleWidth != Value)
    {
        FlexibleWidth = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}

void ULexLayoutCommonSlot::SetFlexibleHeight(float Value)
{
    if (FlexibleHeight != Value)
    {
        FlexibleHeight = Value;
        ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
    }
}
