// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "LGUI.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutSelf AspectRatio"), STAT_LexLayoutSelfAspectRatio, STATGROUP_LGUI);
void ULexLayoutSelfAspectRatio::CalculateSize()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutSelfAspectRatio);
    auto Widget = GetWidget();
    if (!Widget)return;
    bIsCalculatingSize = true;
    switch (AspectRatioType)
    {
    case ELexLayoutAspectRatioType::HeightControlWidth:
    	{
    		auto Width = Widget->GetHeight() * AspectRatio;
    		Widget->SetWidth(Width);
    	}
    	break;
    case ELexLayoutAspectRatioType::WidthControlHeight:
    	{
    		auto Height = Widget->GetWidth() / AspectRatio;
    		Widget->SetHeight(Height);
    	}
    	break;
    case ELexLayoutAspectRatioType::FitInParent:
    	{
    		if (auto ParentWidget = Widget->GetUIParent())
    		{
    			Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5));
				Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5));

    			auto ParentWidth = ParentWidget->GetWidth();
    			auto ParentHeight = ParentWidget->GetHeight();
    			auto ParentAspectRatio = ParentWidth / ParentHeight;
    			FVector2D ThisSize;
    			if (ParentAspectRatio > AspectRatio)
    			{
    				ThisSize.X = ParentHeight * AspectRatio;
    				ThisSize.Y = ParentHeight;
    			}
    			else
    			{
    				ThisSize.Y = ParentWidth / AspectRatio;
    				ThisSize.X = ParentWidth;
    			}
    			FVector2D AnchoredPosition;
    			AnchoredPosition.X = ParentWidth * (Widget->GetPivot().X - 0.5f);
    			AnchoredPosition.Y = ParentHeight * (Widget->GetPivot().Y - 0.5f);
    			Widget->SetAnchoredPosition(AnchoredPosition);
    			Widget->SetSizeDelta(ThisSize);
    		}
    	}
    	break;
    case ELexLayoutAspectRatioType::EnvelopeParent:
    	{
    		if (auto ParentWidget = Widget->GetUIParent())
    		{
    			Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5));
    			Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5));

    			auto ParentWidth = ParentWidget->GetWidth();
    			auto ParentHeight = ParentWidget->GetHeight();
    			auto ParentAspectRatio = ParentWidth / ParentHeight;
    			FVector2D ThisSize;
    			if (ParentAspectRatio > AspectRatio)
    			{
    				ThisSize.Y = ParentWidth / AspectRatio;
    				ThisSize.X = ParentWidth;
    			}
    			else
    			{
    				ThisSize.X = ParentHeight * AspectRatio;
    				ThisSize.Y = ParentHeight;
    			}
    			FVector2D AnchoredPosition;
    			AnchoredPosition.X = ParentWidth * (Widget->GetPivot().X - 0.5f);
    			AnchoredPosition.Y = ParentHeight * (Widget->GetPivot().Y - 0.5f);
    			Widget->SetAnchoredPosition(AnchoredPosition);
    			Widget->SetSizeDelta(ThisSize);
    		}
    	}
    	break;
    }
	CalculatedPreferred = FVector2f(Widget->GetSize());
    bIsCalculatingSize = false;
}

void ULexLayoutSelfAspectRatio::OnTransformChanged()
{
}

void ULexLayoutSelfAspectRatio::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
    if (bIsCalculatingSize)return;
    CalculateSize();
}

#if WITH_EDITOR
void ULexLayoutSelfAspectRatio::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
    CalculateSize();
}

void ULexLayoutSelfAspectRatio::PostInitProperties()
{
    Super::PostInitProperties();
}
#endif

FLexLayoutControlAnchorData ULexLayoutSelfAspectRatio::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
    FLexLayoutControlAnchorData Result;
    auto ThisWidget = GetWidget();
    if (ThisWidget == TargetWidget)//self
    {
	    switch (AspectRatioType)
	    {
		case ELexLayoutAspectRatioType::EnvelopeParent:
	    case ELexLayoutAspectRatioType::FitInParent:
	    	Result.bCanControlHorizontalSize = true;
	    	Result.bCanControlVerticalSize = true;
	    	Result.bCanControlHorizontalPosition = true;
	    	Result.bCanControlVerticalPosition = true;
	    	break;
	    case ELexLayoutAspectRatioType::HeightControlWidth:
	    	Result.bCanControlHorizontalSize = true;
	    	break;
	    case ELexLayoutAspectRatioType::WidthControlHeight:
	    	Result.bCanControlVerticalSize = true;
	    	break;
	    }
    }
    return Result;
}

void ULexLayoutSelfAspectRatio::GetLayoutProperties(FVector2f& OutMin, FVector2f& OutMax, FVector2f& OutPreferred)
{
    OutMin.X = CalculatedPreferred.X;
    OutMin.Y = CalculatedPreferred.Y;
    OutMax.X = CalculatedPreferred.X;
    OutMax.Y = CalculatedPreferred.Y;
    OutPreferred.X = CalculatedPreferred.X;
    OutPreferred.Y = CalculatedPreferred.Y;
}

void ULexLayoutSelfAspectRatio::SetAspectRatioType(const ELexLayoutAspectRatioType& Value)
{
	if (AspectRatioType != Value)
	{
		AspectRatioType = Value;
		CalculateSize();
	}
}

void ULexLayoutSelfAspectRatio::SetAspectRatio(float Value)
{
	if (AspectRatio != Value)
	{
		AspectRatio = Value;
		CalculateSize();
	}
}
