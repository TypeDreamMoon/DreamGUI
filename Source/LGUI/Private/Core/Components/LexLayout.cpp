// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"

void ULexLayoutContainer::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

void ULexLayoutContainer::OnRegister()
{
	Super::OnRegister();
	if (auto Widget = GetWidget())
	{
		ULexWidget::MarkLayoutForRebuild(Widget);
	}
}

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULexLayout::MarkLayoutDirty()
{
	bIsLayoutDirty = true;
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
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
#endif

void ULexLayoutSelf::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

FVector2f ULexLayoutSelf::GetLayoutFinalSize()
{
	auto Widget = GetWidget();
	return FVector2f(Widget->GetWidth(), Widget->GetHeight());
}

void ULexLayoutSelf::SetIgnoreLayoutContainer(bool Value)
{
	if (bIgnoreLayoutContainer != Value)
	{
		bIgnoreLayoutContainer = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
