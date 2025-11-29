// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutContainerChildSizeFitter.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutSelf ChildSizeFitter"), STAT_LexLayoutSelfAspectRatio, STATGROUP_LGUI);
void ULexLayoutContainerChildSizeFitter::CalculateSize()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutSelfAspectRatio);
    auto Widget = GetWidget();
    if (!Widget)return;
	ULexWidget* ValidChild = nullptr;
	for (auto& ChildWidget : Widget->GetUIChildren())
	{
		if (!ChildWidget->GetWidgetActiveInHierarchy())continue;
		if (auto ChildLayoutSelf = ChildWidget->GetLayoutSelf())
		{
			if (ChildLayoutSelf->GetIgnoreLayoutContainer())continue;
		}
		ValidChild = ChildWidget;
		break;
	}
	if (ValidChild)
	{
		if (bFitWidth)
		{
    		CalculatedPreferred.X = ValidChild->GetWidth();
		}
		if (bFitHeight)
		{
			CalculatedPreferred.Y = ValidChild->GetHeight();
		}
	}
	else
	{
		CalculatedPreferred = FVector2f(Widget->GetSize());
	}
}

void ULexLayoutContainerChildSizeFitter::OnTransformChanged()
{
}

void ULexLayoutContainerChildSizeFitter::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
    bool InHeightChange)
{
}

#if WITH_EDITOR
void ULexLayoutContainerChildSizeFitter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULexLayoutContainerChildSizeFitter::PostInitProperties()
{
    Super::PostInitProperties();
}
#endif

FLexLayoutControlAnchorData ULexLayoutContainerChildSizeFitter::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
    return FLexLayoutControlAnchorData();
}

void ULexLayoutContainerChildSizeFitter::GetLayoutProperties(FVector2f& OutMin, FVector2f& OutMax, FVector2f& OutPreferred)
{
    OutMin.X = CalculatedPreferred.X;
    OutMin.Y = CalculatedPreferred.Y;
    OutMax.X = CalculatedPreferred.X;
    OutMax.Y = CalculatedPreferred.Y;
    OutPreferred.X = CalculatedPreferred.X;
    OutPreferred.Y = CalculatedPreferred.Y;
}

void ULexLayoutContainerChildSizeFitter::SetFitWidth(bool Value)
{
	if (bFitWidth != Value)
	{
		bFitWidth = Value;
		CalculateSize();
	}
}

void ULexLayoutContainerChildSizeFitter::SetFitHeight(bool Value)
{
	if (bFitHeight != Value)
	{
		bFitHeight = Value;
		CalculateSize();
	}
}
