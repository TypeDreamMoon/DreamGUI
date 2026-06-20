// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutContainerChildSizeFitter.h"
#include "LGUI.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutSelf ChildSizeFitter"), STAT_LexLayoutSelfChildSizeFitter, STATGROUP_LGUI);
void ULexLayoutContainerChildSizeFitter::CalculateSize()
{
    SCOPE_CYCLE_COUNTER(STAT_LexLayoutSelfChildSizeFitter);
    auto Widget = GetWidget();
    if (!Widget)return;
	ULexWidget* ValidChild = nullptr;
	for (auto& ChildWidget : Widget->GetChildren())
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

void ULexLayoutContainerChildSizeFitter::UpdateLayout()
{
	CalculateSize();
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

FVector2f ULexLayoutContainerChildSizeFitter::GetLayoutPreferredSize()
{
	FVector2f OutPreferred;
    OutPreferred.X = CalculatedPreferred.X;
    OutPreferred.Y = CalculatedPreferred.Y;
	return OutPreferred;
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
