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
	// The list AS IT WAS, not merely how long it was. A refresh is raised by any broadcast on the
	// source field, and most of those change nothing about the rows -- an array property written with
	// the same contents, a FieldNotify fired defensively, a second `each` sharing one source. Each of
	// those used to re-bind every visible cell, which means one reflected setter call per bound member
	// per visible row, every time.
	TArray<TWeakObjectPtr<UObject>> PreviousItems;
	PreviousItems.Reserve(Items.Num());
	for (const TObjectPtr<UObject>& Item : Items)
	{
		PreviousItems.Emplace(Item.Get());
	}

	FetchItems();
	if (!IsValid(View))
	{
		return;
	}

	if (Items.Num() == PreviousItems.Num())
	{
		bool bSameItems = true;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (PreviousItems[Index].Get() != Items[Index].Get())
			{
				bSameItems = false;
				break;
			}
		}
		if (bSameItems)
		{
			// Same objects in the same order. A cell's CONTENTS can still have changed underneath the
			// object, which is what UpdateCellData is for -- but that is a question for whoever changed
			// them, and re-binding here on a broadcast that moved nothing is the part that was free to
			// stop doing. Callers that mutate an item in place call UpdateCellData themselves.
			return;
		}
		// Same length, different objects: a reorder or a wholesale replacement. The pool is right and
		// only the data behind each cell changed.
		View->UpdateCellData();
		return;
	}

	// The count changed, so the content area has to be re-sized and the cell pool re-checked against
	// the new count. Note this is NOT proportional to the number of rows: UUIRecyclableScrollView
	// sizes its pool from VisibleCellCount -- how many cells fit in the viewport, capped at the item
	// count -- and grows it only when that number rises, so a list of ten thousand rows rebuilds the
	// same handful of cells a list of twenty does.
	View->RecreateList();
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

	// One pass over the cell instead of one per entry binding. This runs for every visible cell on
	// every scroll update, and a cell with N widgets and M bindings was paying N*M display-name
	// comparisons for an answer that does not change within the call. First match wins, exactly as
	// the linear search that used to break on it did -- display names are not unique in a subtree.
	TMap<FName, UDreamWidget*> WidgetsByDisplayName;
	WidgetsByDisplayName.Reserve(CellWidgets.Num());
	for (UDreamWidget* Candidate : CellWidgets)
	{
		if (IsValid(Candidate))
		{
			WidgetsByDisplayName.FindOrAdd(FName(*Candidate->GetDisplayName()), Candidate);
		}
	}

	for (const FDreamWidgetEntryBinding& Entry : Binding.EntryBindings)
	{
		UDreamWidget* const* FoundWidget = WidgetsByDisplayName.Find(Entry.TargetWidgetDisplayName);
		UDreamWidget* TargetWidget = FoundWidget != nullptr ? *FoundWidget : nullptr;
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
