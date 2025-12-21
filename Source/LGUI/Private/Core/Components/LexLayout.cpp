// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutContainer::BeginDestroy()
{
	Super::BeginDestroy();
	if (auto Widget = GetWidget())
	{
		Widget->RemoveLayoutContainer();
	}
}

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULexLayoutContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
#endif

void ULexLayoutSelf::BeginDestroy()
{
	Super::BeginDestroy();
	if (auto Widget = GetWidget())
	{
		Widget->RemoveLayoutSelf();
	}
}

void ULexLayoutSelf::SetIgnoreLayoutContainer(bool Value)
{
	if (bIgnoreLayoutContainer != Value)
	{
		bIgnoreLayoutContainer = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
