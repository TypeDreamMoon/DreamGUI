// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIToggleGroupComponent.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Interaction/UIToggleComponent.h"


UUIToggleGroupComponent::UUIToggleGroupComponent()
{
	OnValueChanged = FLexUIEventDelegate(ELexUIEventDelegateParameterType::Int32);
}
void UUIToggleGroupComponent::AddToggleComponent(UUIToggleComponent* InComp)
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
	bNeedToSortToggleCollection = true;
}
void UUIToggleGroupComponent::RemoveToggleComponent(UUIToggleComponent* InComp)
{
	int32 foundIndex = ToggleCollection.IndexOfByKey(InComp);
	if (foundIndex == INDEX_NONE)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d Not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	ToggleCollection.RemoveAt(foundIndex);
}
void UUIToggleGroupComponent::SortToggleCollection()
{
	if (bNeedToSortToggleCollection)
	{
		bNeedToSortToggleCollection = false;
		ToggleCollection.Sort([](const TWeakObjectPtr<UUIToggleComponent>& A, const TWeakObjectPtr<UUIToggleComponent>& B) {
			return A->GetWidget()->GetFlattenHierarchyIndex() < B->GetWidget()->GetFlattenHierarchyIndex();
			});
	}
}
void UUIToggleGroupComponent::SetSelection(UUIToggleComponent* Target)
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
void UUIToggleGroupComponent::ClearSelection()
{
	if (LastSelect.IsValid())
	{
		LastSelect->SetValue(false);
		LastSelect.Reset();

		OnValueChangedCPP.Broadcast(-1);
		OnValueChanged.FireEvent(-1);
	}
}
UUIToggleComponent* UUIToggleGroupComponent::GetSelectedItem()const
{
	return LastSelect.Get();
}

int32 UUIToggleGroupComponent::GetToggleIndex(const UUIToggleComponent* InComp)const
{
	if (IsValid(InComp))
	{
		(const_cast<UUIToggleGroupComponent*>(this))->SortToggleCollection();
		return ToggleCollection.IndexOfByKey(InComp);
	}
	return -1;
}
UUIToggleComponent* UUIToggleGroupComponent::GetToggleByIndex(int32 InIndex)const
{
	if (InIndex < 0 || InIndex >= ToggleCollection.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index:%d out of range:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, ToggleCollection.Num());
		return nullptr;
	}
	(const_cast<UUIToggleGroupComponent*>(this))->SortToggleCollection();
	return ToggleCollection[InIndex].Get();
}