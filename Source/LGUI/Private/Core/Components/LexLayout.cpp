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
	GetWidget()->MarkRenderSizeChanged();
}
bool ULexLayout::CanEditChange(const FProperty* InProperty) const
{
	return UObject::CanEditChange(InProperty);
}
void ULexLayout::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	CleanupSlots();
	Super::PreSave(ObjectSaveContext);
}
#endif

void ULexLayout::CleanupSlots()
{
	auto& Children = GetWidget()->GetUIChildren();
	Slots.Remove(nullptr);
	TSet<const ULexWidget*> WidgetKeysToRemove;
	for (auto KeyValue : Slots)
	{
		if (!Children.Contains(KeyValue.Key))
		{
			WidgetKeysToRemove.Add(KeyValue.Key);
		}
	}
	for (auto Widget : WidgetKeysToRemove)
	{
		Slots.Remove(Widget);
	}
}

void ULexLayout::OnPreSavePrefab_Implementation()
{
	CleanupSlots();
}

const ULexWidget* ULexLayout::GetWidgetBySlot(const ULexLayoutSlot* Slot)
{
	for (auto KeyValue : Slots)
	{
		if (KeyValue.Value == Slot)
		{
			return KeyValue.Key;
		}
	}
	return nullptr;
}

void ULexLayout::UpdateLayout()
{
	OnUpdateLayout();
}

ULexLayoutSlot* ULexLayout::GetSlot(const ULexWidget* Child) const
{
	return *Slots.Find(Child);
}

ULexLayoutSlot* ULexLayout::GetOrCreateSlot(const ULexWidget* Child, TSubclassOf<ULexLayoutSlot> SlotClass)
{
	auto SlotPtr = Slots.Find(Child);
	auto LayoutSlot = SlotPtr ? *SlotPtr : nullptr;
	if (!IsValid(LayoutSlot) || LayoutSlot->GetClass() != SlotClass)
	{
		if (IsValid(LayoutSlot))
		{
			LayoutSlot->MarkAsGarbage();
		}
		LayoutSlot = NewObject<ULexLayoutSlot>(this, SlotClass, NAME_None, RF_Public | RF_Transactional);
		if (SlotPtr == nullptr)
			Slots.Add(Child, LayoutSlot);
		else
			Slots[Child] = LayoutSlot;
	}
	return LayoutSlot;
}

void ULexLayout::OnChildDetached(const ULexWidget* Child)
{
	Slots.Remove(Child);
}

#if WITH_EDITOR

void ULexLayoutSlot::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	GetWidget()->MarkRenderSizeChanged();
}
#endif

ULexWidget* ULexLayoutSlot::GetWidget() const
{
	if (!CacheWidget.IsValid())
	{
		auto Layout = Cast<ULexLayout>(this->GetOuter());
		CacheWidget = const_cast<ULexWidget*>(Layout->GetWidgetBySlot(this));
	}
	return CacheWidget.Get();
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif