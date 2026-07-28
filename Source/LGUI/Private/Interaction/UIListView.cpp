// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UIListView.h"
#include "Core/Components/LexWidget.h"

bool UUIListEntry::OnPointerClick_Implementation(ULexPointerEventData* EventData)
{
	if (OwnerList.IsValid())
	{
		OwnerList->HandleEntryClicked(this);
	}
	return true;
}

void UUIListEntry::Assign(UUIListView* InOwner, UObject* InItem, int32 InIndex, int32 InTreeDepth, bool bInSelected)
{
	const bool bSelectionChanged = bSelected != bInSelected;
	OwnerList = InOwner;
	Item = InItem;
	ItemIndex = InIndex;
	TreeDepth = InTreeDepth;
	bSelected = bInSelected;
	ReceiveOnListItemAssigned(Item, ItemIndex, bSelected);
	if (bSelectionChanged)
	{
		ReceiveOnSelectionChanged(bSelected);
	}
}

void UUIListView::Awake()
{
	DataSource = this;
	bInfiniteLoop = false;
	if (GetWidget())
	{
		GetWidget()->SetIsFocusable(true);
	}
	Super::Awake();
}

int UUIListView::GetItemCount_Implementation()
{
	return Items.Num();
}

void UUIListView::InitOnCreate_Implementation(ULexUIBehaviour* Component)
{
	if (UUIListEntry* Entry = ResolveEntry(Component))
	{
		Entry->Assign(this, nullptr, INDEX_NONE, 0, false);
	}
}

void UUIListView::BeforeSetCell_Implementation()
{
}

void UUIListView::SetCell_Implementation(ULexUIBehaviour* Component, int Index)
{
	UUIListEntry* Entry = ResolveEntry(Component);
	if (!Entry || !Items.IsValidIndex(Index))
	{
		return;
	}
	UObject* NewItem = Items[Index];
	UObject* PreviousItem = Entry->GetItem();
	if (IsValid(PreviousItem) && PreviousItem != NewItem)
	{
		OnEntryReleased.Broadcast(PreviousItem, Entry);
	}
	Entry->Assign(this, NewItem, Index, GetEntryDepth(Index), SelectedItems.Contains(NewItem));
	if (PreviousItem != NewItem)
	{
		OnEntryGenerated.Broadcast(NewItem, Entry);
	}
}

void UUIListView::AfterSetCell_Implementation()
{
}

UUIListEntry* UUIListView::ResolveEntry(ULexUIBehaviour* Component) const
{
	if (UUIListEntry* Entry = Cast<UUIListEntry>(Component))
	{
		return Entry;
	}
	return Component && Component->GetWidget() ? Component->GetWidget()->GetComponent<UUIListEntry>() : nullptr;
}

void UUIListView::SetListItems(const TArray<UObject*>& InItems)
{
	Items.Reset(InItems.Num());
	for (UObject* Item : InItems)
	{
		if (IsValid(Item))
		{
			Items.Add(Item);
		}
	}
	for (auto It = SelectedItems.CreateIterator(); It; ++It)
	{
		if (!Items.Contains(*It))
		{
			It.RemoveCurrent();
		}
	}
	RecreateList();
}

void UUIListView::AddItem(UObject* Item)
{
	if (IsValid(Item))
	{
		Items.Add(Item);
		RecreateList();
	}
}

bool UUIListView::RemoveItem(UObject* Item)
{
	const int32 Removed = Items.Remove(Item);
	if (Removed > 0)
	{
		SelectedItems.Remove(Item);
		RecreateList();
		return true;
	}
	return false;
}

void UUIListView::ClearListItems()
{
	Items.Reset();
	SelectedItems.Reset();
	ClearAllCells();
}

TArray<UObject*> UUIListView::GetListItems() const
{
	TArray<UObject*> Result;
	Result.Reserve(Items.Num());
	for (UObject* Item : Items)
	{
		Result.Add(Item);
	}
	return Result;
}

void UUIListView::SetItemSelection(UObject* Item, bool bSelected, bool bClearOthers)
{
	if (SelectionMode == EUIListSelectionMode::None || !Items.Contains(Item))
	{
		return;
	}
	const bool bMustClearOthers = bSelected && (bClearOthers || SelectionMode == EUIListSelectionMode::Single || SelectionMode == EUIListSelectionMode::SingleToggle);
	if (bMustClearOthers)
	{
		TArray<TObjectPtr<UObject>> PreviousSelection = SelectedItems.Array();
		for (UObject* Previous : PreviousSelection)
		{
			if (Previous != Item)
			{
				SelectedItems.Remove(Previous);
				OnSelectionChanged.Broadcast(Previous, false);
			}
		}
	}
	const bool bWasSelected = SelectedItems.Contains(Item);
	if (bSelected)
	{
		SelectedItems.Add(Item);
	}
	else
	{
		SelectedItems.Remove(Item);
	}
	if (bWasSelected != bSelected)
	{
		OnSelectionChanged.Broadcast(Item, bSelected);
		RefreshVisibleSelection();
	}
}

void UUIListView::ClearSelection()
{
	TArray<TObjectPtr<UObject>> PreviousSelection = SelectedItems.Array();
	SelectedItems.Reset();
	for (UObject* Item : PreviousSelection)
	{
		OnSelectionChanged.Broadcast(Item, false);
	}
	RefreshVisibleSelection();
}

TArray<UObject*> UUIListView::GetSelectedItems() const
{
	TArray<UObject*> Result;
	Result.Reserve(SelectedItems.Num());
	for (UObject* Item : SelectedItems)
	{
		Result.Add(Item);
	}
	return Result;
}

void UUIListView::HandleEntryClicked(UUIListEntry* Entry)
{
	if (!Entry || !IsValid(Entry->GetItem()))
	{
		return;
	}
	UObject* Item = Entry->GetItem();
	OnItemClicked.Broadcast(Item, Entry);
	switch (SelectionMode)
	{
	case EUIListSelectionMode::Single:
		SetItemSelection(Item, true, true);
		break;
	case EUIListSelectionMode::SingleToggle:
		SetItemSelection(Item, !SelectedItems.Contains(Item), true);
		break;
	case EUIListSelectionMode::Multi:
		SetItemSelection(Item, !SelectedItems.Contains(Item), false);
		break;
	default:
		break;
	}
}

void UUIListView::RefreshVisibleSelection()
{
	for (const FUIRecyclableScrollViewCellContainer& Cell : GetCacheCellList())
	{
		if (UUIListEntry* Entry = ResolveEntry(Cell.CellComponent))
		{
			Entry->Assign(this, Entry->GetItem(), Entry->GetItemIndex(), Entry->GetTreeDepth(), SelectedItems.Contains(Entry->GetItem()));
		}
	}
}

void UUITileView::Awake()
{
	Vertical = true;
	Horizontal = false;
	Columns = FMath::Max(1, NumColumns);
	Super::Awake();
}

int32 UUITreeView::GetEntryDepth(int32 Index) const
{
	return ItemDepths.IsValidIndex(Index) ? ItemDepths[Index] : 0;
}

void UUITreeView::SetRootItems(const TArray<UObject*>& InRootItems)
{
	RootItems.Reset(InRootItems.Num());
	for (UObject* Item : InRootItems)
	{
		if (IsValid(Item))
		{
			RootItems.Add(Item);
		}
	}
	RebuildTree();
}

void UUITreeView::SetItemExpansion(UObject* Item, bool bExpanded)
{
	if (!IsValid(Item))
	{
		return;
	}
	if (bExpanded)
	{
		ExpandedItems.Add(Item);
	}
	else
	{
		ExpandedItems.Remove(Item);
	}
	RebuildTree();
}

void UUITreeView::ToggleItemExpansion(UObject* Item)
{
	SetItemExpansion(Item, !ExpandedItems.Contains(Item));
}

void UUITreeView::RebuildTree()
{
	Items.Reset();
	ItemDepths.Reset();
	TSet<UObject*> Visited;
	for (UObject* Root : RootItems)
	{
		AppendItemRecursive(Root, 0, Visited);
	}
	RecreateList();
}

void UUITreeView::AppendItemRecursive(UObject* Item, int32 Depth, TSet<UObject*>& Visited)
{
	if (!IsValid(Item) || Visited.Contains(Item))
	{
		return;
	}
	Visited.Add(Item);
	Items.Add(Item);
	ItemDepths.Add(Depth);
	if (!ExpandedItems.Contains(Item) || !Item->GetClass()->ImplementsInterface(UUITreeViewItem::StaticClass()))
	{
		return;
	}
	TArray<UObject*> Children;
	IUITreeViewItem::Execute_GetTreeChildren(Item, Children);
	for (UObject* Child : Children)
	{
		AppendItemRecursive(Child, Depth + 1, Visited);
	}
}
