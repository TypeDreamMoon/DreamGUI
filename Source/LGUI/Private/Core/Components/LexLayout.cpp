// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	GetWidget()->MarkSizeDirty_Recursive();
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
	GetWidget()->MarkSizeDirty_Recursive();
}
#endif


#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif