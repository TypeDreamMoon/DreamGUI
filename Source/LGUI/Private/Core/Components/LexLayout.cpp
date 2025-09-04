// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"
#include "UObject/ObjectSaveContext.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	MarkLayoutDirty();
}
bool ULexLayout::CanEditChange(const FProperty* InProperty) const
{
	return UObject::CanEditChange(InProperty);
}
void ULexLayout::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);
}
#endif

void ULexLayout::MarkLayoutDirty()
{
	ULexWidget::MarkLayoutForRebuild(GetWidget());
}

void ULexLayout::OnPreSavePrefab_Implementation()
{
	
}

void ULexLayout::UpdateLayout()
{
	OnUpdateLayout();
}

#if WITH_EDITOR

void ULexLayoutSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	ULexWidget::MarkLayoutForRebuild(GetWidget()->GetUIParent());
}
#endif

ULexWidget* ULexLayoutSlot::GetWidget() const
{
	if (!CacheWidget.IsValid())
	{
		CacheWidget = this->GetTypedOuter<ULexWidget>();
	}
	return CacheWidget.Get();
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif