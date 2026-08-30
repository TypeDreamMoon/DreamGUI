// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIEachAdapter.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "UObject/UnrealType.h"

void UDreamUIEachAdapter::Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UUIRecyclableScrollView* InView)
{
	Owner = InOwner;
	Binding = InBinding;
	View = InView;
	FetchItems();
}

void UDreamUIEachAdapter::FetchItems()
{
	Items.Reset();
	if (!IsValid(Owner))
	{
		return;
	}

	// The compiler vetted the shape; a miss anywhere below means the class moved underneath us,
	// which is the property bindings' rule too: skip, never guess.
	auto CopyOut = [this](const FArrayProperty* InItemsProperty, const void* InItemsMemory)
	{
		const FObjectPropertyBase* Inner = InItemsProperty != nullptr ? CastField<FObjectPropertyBase>(InItemsProperty->Inner) : nullptr;
		if (Inner == nullptr || InItemsMemory == nullptr)
		{
			return;
		}
		FScriptArrayHelper Helper(InItemsProperty, InItemsMemory);
		Items.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Items.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
		}
	};

	if (Binding.bSourceIsFunction)
	{
		UFunction* Source = Owner->FindFunction(Binding.SourceName);
		if (Source == nullptr)
		{
			return;
		}
		FStructOnScope SourceFrame(Source);
		Owner->ProcessEvent(Source, SourceFrame.GetStructMemory());
		const FArrayProperty* ItemsProperty = CastField<FArrayProperty>(Source->GetReturnProperty());
		CopyOut(ItemsProperty, ItemsProperty != nullptr
			? ItemsProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()) : nullptr);
	}
	else
	{
		const FArrayProperty* ItemsProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), Binding.SourceName);
		CopyOut(ItemsProperty, ItemsProperty != nullptr
			? ItemsProperty->ContainerPtrToValuePtr<void>(Owner) : nullptr);
	}
}

void UDreamUIEachAdapter::Refresh()
{
	const int32 PreviousCount = Items.Num();
	FetchItems();
	if (!IsValid(View))
	{
		return;
	}
	if (Items.Num() != PreviousCount)
	{
		// The cell pool is sized from the count; a data-only update cannot grow it.
		View->RecreateList();
	}
	else
	{
		View->UpdateCellData();
	}
}

void UDreamUIEachAdapter::SetCell_Implementation(UDreamUIBehaviour* Component, int32 Index)
{
	UDreamWidget* CellRoot = IsValid(Component) ? Component->GetWidget() : nullptr;
	UObject* Item = Items.IsValidIndex(Index) ? Items[Index].Get() : nullptr;
	if (!IsValid(CellRoot) || !IsValid(Item))
	{
		return;
	}

	TArray<UDreamWidget*> CellWidgets;
	UDreamWidget::CollectChildrenWidgets(CellRoot, CellWidgets, /*IncludeTarget*/true);

	for (const FDreamWidgetEntryBinding& Entry : Binding.EntryBindings)
	{
		UDreamWidget* TargetWidget = nullptr;
		for (UDreamWidget* Candidate : CellWidgets)
		{
			if (FName(*Candidate->GetDisplayName()) == Entry.TargetWidgetDisplayName)
			{
				TargetWidget = Candidate;
				break;
			}
		}
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Entry.Target, Entry.BehaviourIndex);
		if (!IsValid(Target))
		{
			continue;
		}
		const FProperty* ItemProperty = Item->GetClass()->FindPropertyByName(Entry.ItemMember);
		UFunction* Setter = Target->FindFunction(Entry.SetterName);
		if (ItemProperty == nullptr || Setter == nullptr)
		{
			continue;
		}
		FProperty* SetterParameter = nullptr;
		for (TFieldIterator<FProperty> It(Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			SetterParameter = *It;
			break;
		}
		if (SetterParameter == nullptr || !SetterParameter->SameType(ItemProperty))
		{
			continue;
		}
		FStructOnScope SetterFrame(Setter);
		SetterParameter->CopyCompleteValue(
			SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory()),
			ItemProperty->ContainerPtrToValuePtr<void>(Item));
		Target->ProcessEvent(Setter, SetterFrame.GetStructMemory());
	}
}
