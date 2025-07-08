// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

ULexLayout::ULexLayout()
{
}

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
bool ULexLayout::CanEditChange(const FProperty* InProperty) const
{
	return UObject::CanEditChange(InProperty);
}
#endif

void ULexLayout::OnTransformChanged()
{
	MarkLayoutDirty();
}

void ULexLayout::UpdateLayout()
{
	if (bIsLayoutDirty)
	{
		bIsLayoutDirty = false;
		OnUpdateLayout();
	}
}

#if WITH_EDITOR
void ULexLayoutSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
#endif


#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif