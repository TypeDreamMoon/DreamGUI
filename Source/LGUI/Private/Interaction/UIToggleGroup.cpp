// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIToggleGroup.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Interaction/UIToggle.h"


UUIToggleGroup::UUIToggleGroup()
{
	OnValueChanged = FLexUIEventDelegate(ELexUIEventDelegateParameterType::Int32);
}
void UUIToggleGroup::AddToggleComponent(UUIToggle* InComp)
{
	int32 foundIndex = ToggleCollection.IndexOfByKey(InComp);
	if (foundIndex != INDEX_NONE)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d Already exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (!IsValid(InComp->GetWidget()))
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d InComp must have UIItem as root component!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	ToggleCollection.Add(InComp);
}
void UUIToggleGroup::RemoveToggleComponent(UUIToggle* InComp)
{
	int32 foundIndex = ToggleCollection.IndexOfByKey(InComp);
	if (foundIndex == INDEX_NONE)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d Not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	ToggleCollection.RemoveAt(foundIndex);
}
void UUIToggleGroup::SortToggleCollection()
{
	// Always re-sorted on access: FlattenHierarchyIndex changes whenever any ancestor of any toggle is
	// reordered or reparented, and no event reaches this group when that happens — a dirty flag set only
	// on Add kept returning the stale order forever. The collection is a handful of entries, so sorting
	// on demand is cheaper than being wrong. Stale weak pointers are compacted first: dereferencing one
	// inside a comparator is a null-this call.
	ToggleCollection.RemoveAll([](const TWeakObjectPtr<UUIToggle>& Item)
		{
			return !Item.IsValid() || !IsValid(Item->GetWidget());
		});
	ToggleCollection.StableSort([](const TWeakObjectPtr<UUIToggle>& A, const TWeakObjectPtr<UUIToggle>& B) {
		return A->GetWidget()->GetFlattenHierarchyIndex() < B->GetWidget()->GetFlattenHierarchyIndex();
		});
}
void UUIToggleGroup::SetSelection(UUIToggle* Target)
{
	if (!IsValid(Target))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Toggle item is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (LastSelect.Get() != Target)
	{
		auto TempSelected = LastSelect;
		LastSelect = Target;
		if (TempSelected.IsValid())
		{
			TempSelected->SetValue(false);
		}
		int index = GetToggleIndex(Target);
		OnValueChangedCPP.Broadcast(index);
		OnValueChangedBP.Broadcast(index);
		OnValueChanged.FireEvent(index);
	}
}
void UUIToggleGroup::ClearSelection()
{
	if (LastSelect.IsValid())
	{
		LastSelect->SetValue(false);
		LastSelect.Reset();

		OnValueChangedCPP.Broadcast(-1);
		OnValueChanged.FireEvent(-1);
	}
}
UUIToggle* UUIToggleGroup::GetSelectedItem()const
{
	return LastSelect.Get();
}

int32 UUIToggleGroup::GetToggleIndex(const UUIToggle* InComp)const
{
	if (IsValid(InComp))
	{
		(const_cast<UUIToggleGroup*>(this))->SortToggleCollection();
		return ToggleCollection.IndexOfByKey(InComp);
	}
	return -1;
}
UUIToggle* UUIToggleGroup::GetToggleByIndex(int32 InIndex)const
{
	//sort (and compact) first — validation against the pre-compaction count could pass a stale bound
	(const_cast<UUIToggleGroup*>(this))->SortToggleCollection();
	if (InIndex < 0 || InIndex >= ToggleCollection.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index:%d out of range:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, ToggleCollection.Num());
		return nullptr;
	}
	return ToggleCollection[InIndex].Get();
}