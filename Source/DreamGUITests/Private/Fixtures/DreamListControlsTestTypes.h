// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "UObject/Object.h"
#include "DreamListControlsTestTypes.generated.h"

class UDreamWidget;
class UDreamDragDropOperation;

/**
 * Somewhere a list's or a tree's events can land.
 *
 * A dynamic delegate can only bind a UFUNCTION on a UObject, so asserting WHAT a control announced
 * needs a real object to hang that function off -- the same reason UDreamRingMenuProbe exists. The
 * counts sit beside the values because "fired once with the right index" and "fired twice, the
 * second time correctly" are different claims, and for a list they are usually the interesting one:
 * a row released twice, a click counted as a double, a batch expand that announced nothing.
 */
UCLASS()
class UDreamListControlsProbe : public UObject
{
	GENERATED_BODY()

public:
	/** FDreamListItemEvent: OnItemClicked and OnItemDoubleClicked. */
	UFUNCTION()
	void RecordItem(int32 ItemIndex, UObject* Item)
	{
		ItemIndices.Add(ItemIndex);
		LastItem = Item;
	}

	/** FDreamListItemEvent again, so one probe can watch both halves of a click at once. */
	UFUNCTION()
	void RecordSecondItem(int32 ItemIndex, UObject* Item)
	{
		SecondItemIndices.Add(ItemIndex);
	}

	/** FDreamListRowEvent: OnRowGenerated and OnRowReleased. */
	UFUNCTION()
	void RecordRow(int32 ItemIndex, UDreamWidget* Row, UObject* Item)
	{
		RowIndices.Add(ItemIndex);
		LastRow = Row;
	}

	/** FDreamTreeExpansionChangedEvent. */
	UFUNCTION()
	void RecordExpansion(int32 ItemIndex, bool bExpanded)
	{
		ExpansionIndices.Add(ItemIndex);
		ExpansionStates.Add(bExpanded);
	}

	/** FDreamListSelectionChangedEvent. */
	UFUNCTION()
	void RecordSelection(int32 SelectedIndex)
	{
		SelectionIndices.Add(SelectedIndex);
	}

	/** FDreamMenuAnchorOpenChangedEvent: every open and close, in order, so "twice" is visible. */
	UFUNCTION()
	void RecordOpenChanged(bool bInIsOpen)
	{
		OpenStates.Add(bInIsOpen);
	}

	/**
	 * FDreamScrollBoxUserScrolledOffsetEvent, and FDreamScrollBoxScrolledEvent, which have the same
	 * shape. Every value in order, because "fired at all" is the claim a user-scroll event fails on:
	 * a control that reported its own pushes as the player's would fire here with the right number.
	 */
	UFUNCTION()
	void RecordScrollValue(float Value)
	{
		ScrollValues.Add(Value);
	}

	/** FDreamScrollBoxBarVisibilityChangedEvent: every change, so a repeat is visible. */
	UFUNCTION()
	void RecordBarVisibility(bool bVisible)
	{
		BarVisibilities.Add(bVisible);
	}

	/**
	 * FDreamListScrolledEvent, for OnListViewScrolled: both numbers, because the claim worth making
	 * is that the visible fraction came along with the offset.
	 */
	UFUNCTION()
	void RecordScrollPair(float CurrentOffset, float ViewFraction)
	{
		ScrollValues.Add(CurrentOffset);
		ScrollFractions.Add(ViewFraction);
	}

	/**
	 * FDreamListScrolledEvent again, for OnListViewFinishedScrolling, so one probe can watch both
	 * halves at once -- "moved twice and finished once" is the claim, and it needs two counters.
	 */
	UFUNCTION()
	void RecordFinishedPair(float CurrentOffset, float ViewFraction)
	{
		FinishedValues.Add(CurrentOffset);
	}

	/** FDreamListItemHoverEvent. */
	UFUNCTION()
	void RecordItemHover(int32 ItemIndex, UObject* Item, bool bIsHovered)
	{
		HoverIndices.Add(ItemIndex);
		HoverStates.Add(bIsHovered);
	}

	/** FDreamListEntriesGeneratedEvent: one entry per rebuild, so "rebuilt once" is a countable claim. */
	UFUNCTION()
	void RecordRowsGenerated(int32 RealizedRowCount)
	{
		RowCounts.Add(RealizedRowCount);
	}

	/**
	 * FDreamListItemSelectableQuery: everything is allowed except one index.
	 *
	 * A single vetoed index rather than a predicate, because the claim being tested is that BOTH the
	 * click road and the navigation road ask -- which needs one row that differs, not a policy.
	 */
	UFUNCTION()
	bool AnswerSelectable(int32 ItemIndex, UObject* Item)
	{
		QueriedIndices.Add(ItemIndex);
		return ItemIndex != VetoedIndex;
	}

	/** FDreamListItemDragEvent: OnItemDragDetected. */
	UFUNCTION()
	void RecordItemDrag(int32 ItemIndex, UObject* Item, UDreamDragDropOperation* Operation)
	{
		ItemIndices.Add(ItemIndex);
		LastItem = Item;
	}

	/** FDreamListItemDragEvent again, so one probe can watch a drag's start and its end at once. */
	UFUNCTION()
	void RecordSecondItemDrag(int32 ItemIndex, UObject* Item, UDreamDragDropOperation* Operation)
	{
		SecondItemIndices.Add(ItemIndex);
	}

	/** FDreamWidgetFocusEvent: two counters, because "once" is the claim an edge event fails on. */
	UFUNCTION()
	void RecordFocusReceived(int32 InUserIndex, int32 InPointerId) { ++FocusReceivedCount; }

	UFUNCTION()
	void RecordFocusLost(int32 InUserIndex, int32 InPointerId) { ++FocusLostCount; }

	/** FDreamScrollBoxFocusUpdatedEvent. */
	UFUNCTION()
	void RecordFocusUpdated(UDreamWidget* FocusedWidget)
	{
		++FocusUpdatedCount;
		LastFocusedWidget = FocusedWidget;
	}

	/** FDreamListItemDropEvent: the zone matters as much as the index, so both are kept. */
	UFUNCTION()
	void RecordItemDrop(int32 ItemIndex, UObject* Item, UDreamDragDropOperation* Operation, EDreamItemDropZone DropZone)
	{
		DropIndices.Add(ItemIndex);
		DropZones.Add(DropZone);
	}

	/**
	 * FDreamTreeGetItemChildren: a hierarchy a test declares inline, one Add per parent.
	 *
	 * The map is a plain member rather than a UPROPERTY because a TMap of arrays is not a reflectable
	 * shape, and it does not need to be: every object a test puts in here is also in the tree's own
	 * ItemObjects the moment the walk runs, which is a UPROPERTY and is what keeps them alive.
	 */
	UFUNCTION()
	void ProvideChildren(UObject* Item, TArray<UObject*>& OutChildren)
	{
		if (const TArray<UObject*>* Found = Children.Find(Item))
		{
			OutChildren = *Found;
		}
	}

	TArray<int32> ItemIndices;
	TArray<int32> SecondItemIndices;
	TArray<int32> RowIndices;
	TArray<int32> ExpansionIndices;
	TArray<bool> ExpansionStates;
	TArray<int32> SelectionIndices;
	TArray<bool> OpenStates;
	TArray<float> ScrollValues;
	TArray<float> ScrollFractions;
	TArray<float> FinishedValues;
	TArray<bool> BarVisibilities;
	TArray<int32> HoverIndices;
	TArray<bool> HoverStates;
	TArray<int32> RowCounts;
	TArray<int32> QueriedIndices;
	TArray<int32> DropIndices;
	TArray<EDreamItemDropZone> DropZones;

	int32 FocusReceivedCount = 0;
	int32 FocusLostCount = 0;
	int32 FocusUpdatedCount = 0;

	/** Which index AnswerSelectable refuses. INDEX_NONE lets everything through. */
	int32 VetoedIndex = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> LastFocusedWidget = nullptr;
	TMap<UObject*, TArray<UObject*>> Children;

	/** Reflected, because a probe that keeps a widget alive only by luck is a probe that crashes. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> LastItem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> LastRow = nullptr;
};
