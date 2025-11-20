// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"


#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULexLayoutContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	ULexWidget::MarkLayoutForRebuild(GetWidget());
}

void ULexLayoutSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
}
#endif

void ULexLayoutSelf::SetIgnoreLayoutContainer(bool Value)
{
	if (bIgnoreLayoutContainer != Value)
	{
		bIgnoreLayoutContainer = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
	}
}
