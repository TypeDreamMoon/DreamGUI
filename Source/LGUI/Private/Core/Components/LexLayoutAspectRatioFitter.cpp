// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutAspectRatioFitter.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutAspectRatioFitter::UpdateSize()
{
    auto Widget = GetWidget();
    if (!Widget)return;
    switch (AspectMode)
    {
    case ELexLayoutAspectRatioFitterMode::None:
#if WITH_EDITOR
        if (auto World = GetWorld())
        {
            if (!World->IsGameWorld())
            {
                AspectRatio = FMath::Clamp(Widget->GetWidth() / Widget->GetHeight(), 0.001f, 1000.0f);
            }
        }
#endif
        return;
    case ELexLayoutAspectRatioFitterMode::HeightControlsWidth:
        Widget->SetWidth(Widget->GetHeight() * AspectRatio);
        break;
    case ELexLayoutAspectRatioFitterMode::WidthControlsHeight:
        Widget->SetHeight(Widget->GetWidth() / AspectRatio);
        break;
    case ELexLayoutAspectRatioFitterMode::FitInParent:
    case ELexLayoutAspectRatioFitterMode::EnvelopeParent:
        {
            auto UIParent = Widget->GetUIParent();
            if (!UIParent)return;
            Widget->SetAnchorMin(FVector2D::Zero());
            Widget->SetAnchorMax(FVector2D::One());
            Widget->SetAnchoredPosition(FVector2D::Zero());
            auto SizeDelta = FVector2D::Zero();
            auto ParentSize = UIParent->GetSize();
            if ((ParentSize.Y * AspectRatio < ParentSize.X) ^ (AspectMode == ELexLayoutAspectRatioFitterMode::FitInParent))
            {
                SizeDelta.Y = (UIParent->GetWidth() / AspectRatio) - (UIParent->GetHeight() * (Widget->GetAnchorMax().Y - Widget->GetAnchorMin().Y));
            }
            else
            {
                SizeDelta.X = (UIParent->GetHeight() * AspectRatio) - (UIParent->GetWidth() * (Widget->GetAnchorMax().X - Widget->GetAnchorMin().X));
            }
            Widget->SetSizeDelta(SizeDelta);
        }
        break;
    }
}

void ULexLayoutAspectRatioFitter::OnUpdateLayout()
{
    UpdateSize();
}

void ULexLayoutAspectRatioFitter::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    UpdateSize();
}

FLexLayoutControlAnchorData ULexLayoutAspectRatioFitter::GetLayoutControlAnchor(const ULexWidget* TargetWidget)
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)
    {
        switch (AspectMode)
        {
        case ELexLayoutAspectRatioFitterMode::None:
            break;
        case ELexLayoutAspectRatioFitterMode::HeightControlsWidth:
            Result.bCanControlHorizontalSizeDelta = true;
            break;
        case ELexLayoutAspectRatioFitterMode::WidthControlsHeight:
            Result.bCanControlVerticalSizeDelta = true;
            break;
        case ELexLayoutAspectRatioFitterMode::FitInParent:
        case ELexLayoutAspectRatioFitterMode::EnvelopeParent:
            Result.bCanControlHorizontalAnchoredPosition = true;
            Result.bCanControlVerticalAnchoredPosition = true;
            Result.bCanControlHorizontalSizeDelta = true;
            Result.bCanControlVerticalSizeDelta = true;
            break;
        }
    }
    return Result;
}

#if WITH_EDITOR
void ULexLayoutAspectRatioFitter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

float ULexLayoutAspectRatioFitter::GetPreferredWidth() const
{
    auto Widget = GetWidget();
    if (!Widget)return -1;
    switch (AspectMode)
    {
    case ELexLayoutAspectRatioFitterMode::None:
    case ELexLayoutAspectRatioFitterMode::WidthControlsHeight:
        return -1;
    case ELexLayoutAspectRatioFitterMode::HeightControlsWidth:
        return Widget->GetHeight() * AspectRatio;
    case ELexLayoutAspectRatioFitterMode::FitInParent:
    case ELexLayoutAspectRatioFitterMode::EnvelopeParent:
        auto UIParent = Widget->GetUIParent();
        if (!UIParent)return -1;
        auto ParentSize = UIParent->GetSize();
        if ((ParentSize.Y * AspectRatio < ParentSize.X) ^ (AspectMode == ELexLayoutAspectRatioFitterMode::FitInParent))
        {
            return -1;
        }
        else
        {
            return (UIParent->GetHeight() * AspectRatio) - (UIParent->GetWidth() * (Widget->GetAnchorMax().X - Widget->GetAnchorMin().X));
        }
    }
    return -1;
}

float ULexLayoutAspectRatioFitter::GetPreferredHeight() const
{
    auto Widget = GetWidget();
    if (!Widget)return -1;
    switch (AspectMode)
    {
    case ELexLayoutAspectRatioFitterMode::None:
    case ELexLayoutAspectRatioFitterMode::WidthControlsHeight:
        return Widget->GetWidth() / AspectRatio;
    case ELexLayoutAspectRatioFitterMode::HeightControlsWidth:
        return -1;
    case ELexLayoutAspectRatioFitterMode::FitInParent:
    case ELexLayoutAspectRatioFitterMode::EnvelopeParent:
        auto UIParent = Widget->GetUIParent();
        if (!UIParent)return -1;
        auto ParentSize = UIParent->GetSize();
        if ((ParentSize.Y * AspectRatio < ParentSize.X) ^ (AspectMode == ELexLayoutAspectRatioFitterMode::FitInParent))
        {
            return (UIParent->GetWidth() / AspectRatio) - (UIParent->GetHeight() * (Widget->GetAnchorMax().Y - Widget->GetAnchorMin().Y));
        }
        else
        {
            return -1;
        }
    }
    return -1;
}

void ULexLayoutAspectRatioFitter::SetAspectMode(ELexLayoutAspectRatioFitterMode Value)
{
    if (AspectMode != Value)
    {
        AspectMode = Value;
        UpdateSize();
    }
}

void ULexLayoutAspectRatioFitter::SetAspectRatio(float Value)
{
    if (AspectRatio != Value)
    {
        AspectRatio = Value;
        UpdateSize();
    }
}
