// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutPreferredSizeFitter.h"

#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutPreferredSizeFitter::UpdateSize()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    if (bFitWidth)
    {
        Widget->SetWidth(this->GetPreferredWidth());
    }
    if (bFitHeight)
    {
        Widget->SetHeight(this->GetPreferredHeight());
    }
}

void ULexLayoutPreferredSizeFitter::OnUpdateLayout()
{
    UpdateSize();
}

void ULexLayoutPreferredSizeFitter::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    UpdateSize();
}

FLexLayoutControlAnchorData ULexLayoutPreferredSizeFitter::GetLayoutControlAnchor(const ULexWidget* TargetWidget)
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)
    {
        if (bFitWidth)
            Result.bCanControlHorizontalSizeDelta = true;
        if (bFitHeight)
            Result.bCanControlVerticalSizeDelta = true;
    }
    return Result;
}

#if WITH_EDITOR
void ULexLayoutPreferredSizeFitter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

float ULexLayoutPreferredSizeFitter::GetPreferredWidth() const
{
    auto Widget = GetWidget();
    if (!Widget)return -1;
    if (bFitWidth)
    {
        if (auto Visual = Widget->GetVisual())
        {
            return Visual->GetPreferredWidth() + AdditionalWidth;
        }
    }
    return -1;
}

float ULexLayoutPreferredSizeFitter::GetPreferredHeight() const
{
    auto Widget = GetWidget();
    if (!Widget)return -1;
    if (bFitHeight)
    {
        if (auto Visual = Widget->GetVisual())
        {
            return Visual->GetPreferredHeight() + AdditionalHeight;
        }
    }
    return -1;
}

void ULexLayoutPreferredSizeFitter::SetFitWidth(bool Value)
{
    if (bFitWidth != Value)
    {
        bFitWidth = Value;
        UpdateSize();
    }
}

void ULexLayoutPreferredSizeFitter::SetAdditionalWidth(float Value)
{
    if (AdditionalWidth != Value)
    {
        AdditionalWidth = Value;
        UpdateSize();
    }
}

void ULexLayoutPreferredSizeFitter::SetFitHeight(bool Value)
{
    if (bFitHeight != Value)
    {
        bFitHeight = Value;
        UpdateSize();
    }
}

void ULexLayoutPreferredSizeFitter::SetAdditionalHeight(float Value)
{
    if (AdditionalHeight != Value)
    {
        AdditionalHeight = Value;
        UpdateSize();
    }
}

