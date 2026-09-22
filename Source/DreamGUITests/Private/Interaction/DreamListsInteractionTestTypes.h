// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "Interaction/DreamDragDropOperation.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "DreamListsInteractionTestTypes.generated.h"

class UDreamWidget;

/**
 * Something for a list, a tile view or a tree to stand a row for.
 *
 * An object source rather than a text one, on purpose: a text source has no identity, so a list
 * holding one drops its selection whenever the source is replaced, and the removal test would be
 * asserting that rule instead of the one it is about. With objects the selection follows the ITEM,
 * which is also what UMG's ListView keys everything by.
 */
UCLASS()
class UDreamListsInteractionItem : public UObject
{
	GENERATED_BODY()

public:
	/** Its place in the order the items were made, so a failure that is about order can say which one moved. */
	UPROPERTY(Transient)
	int32 Ordinal = INDEX_NONE;
};

/**
 * Where the list family's public events land.
 *
 * The events are dynamic multicast delegates, and those bind only to a UFUNCTION on a UObject, so
 * asserting what a control ANNOUNCED needs a real listener. Every handler keeps every call in order
 * rather than a flag, because the interesting claims are about counts -- "the selection changed
 * once, not once per row it touched", "the list finished scrolling once, not once a frame" -- and a
 * flag cannot tell one from two.
 */
UCLASS()
class UDreamListsInteractionProbe : public UObject
{
	GENERATED_BODY()

public:
	/** FDreamListSelectionChangedEvent: OnSelectionChanged. */
	UFUNCTION()
	void RecordSelectionChanged(int32 InSelectedIndex)
	{
		SelectionChanges.Add(InSelectedIndex);
	}

	/** FDreamListItemEvent: OnItemClicked. */
	UFUNCTION()
	void RecordItemClicked(int32 InItemIndex, UObject* InItem)
	{
		ClickedItems.Add(InItemIndex);
	}

	/** FDreamListItemEvent again, for OnItemDoubleClicked, so a click and a double click stay apart. */
	UFUNCTION()
	void RecordItemDoubleClicked(int32 InItemIndex, UObject* InItem)
	{
		DoubleClickedItems.Add(InItemIndex);
	}

	/** FDreamListItemHoverEvent: OnItemIsHoveredChanged. The item and the edge, in order. */
	UFUNCTION()
	void RecordItemHoverChanged(int32 InItemIndex, UObject* InItem, bool bInIsHovered)
	{
		HoverItems.Add(InItemIndex);
		HoverStates.Add(bInIsHovered);
	}

	/** FDreamListScrolledEvent: OnListViewScrolled. */
	UFUNCTION()
	void RecordListScrolled(float InCurrentOffset, float InViewFraction)
	{
		ScrolledOffsets.Add(InCurrentOffset);
	}

	/** FDreamListScrolledEvent again, for OnListViewFinishedScrolling: "moved" and "stopped" are two claims. */
	UFUNCTION()
	void RecordListFinishedScrolling(float InCurrentOffset, float InViewFraction)
	{
		FinishedOffsets.Add(InCurrentOffset);
	}

	/** FDreamListRowEvent: OnRowGenerated, one per bind. */
	UFUNCTION()
	void RecordRowGenerated(int32 InItemIndex, UDreamWidget* InRow, UObject* InItem)
	{
		GeneratedItems.Add(InItemIndex);
	}

	/** FDreamListRowEvent again, for OnRowReleased. */
	UFUNCTION()
	void RecordRowReleased(int32 InItemIndex, UDreamWidget* InRow, UObject* InItem)
	{
		ReleasedItems.Add(InItemIndex);
	}

	/** FDreamTreeExpansionChangedEvent: OnItemExpansionChanged. */
	UFUNCTION()
	void RecordExpansionChanged(int32 InItemIndex, bool bInExpanded)
	{
		ExpansionItems.Add(InItemIndex);
		ExpansionStates.Add(bInExpanded);
	}

	/** FDreamListItemDragEvent: OnItemDragDetected. */
	UFUNCTION()
	void RecordItemDragDetected(int32 InItemIndex, UObject* InItem, UDreamDragDropOperation* InOperation)
	{
		DragDetectedItems.Add(InItemIndex);
	}

	/** FDreamListItemDragEvent again, for OnItemDragCancelled. */
	UFUNCTION()
	void RecordItemDragCancelled(int32 InItemIndex, UObject* InItem, UDreamDragDropOperation* InOperation)
	{
		DragCancelledItems.Add(InItemIndex);
	}

	/**
	 * FDreamListItemDropEvent: OnItemAcceptDrop. The payload is kept as well as the row, because
	 * "the drop landed on row five" and "what landed there was row two's item" are both the claim.
	 */
	UFUNCTION()
	void RecordItemAcceptDrop(int32 InItemIndex, UObject* InItem, UDreamDragDropOperation* InOperation, EDreamItemDropZone InDropZone)
	{
		DropItems.Add(InItemIndex);
		DropZones.Add(InDropZone);
		LastDropPayload = InOperation != nullptr ? InOperation->Payload.Get() : nullptr;
	}

	/** FDreamListDraggingStateEvent: OnDraggingStateChanged, every edge in order. */
	UFUNCTION()
	void RecordDraggingStateChanged(bool bInIsDragging)
	{
		DraggingStates.Add(bInIsDragging);
	}

	/** Forget everything heard so far, so the next action's events are the only ones counted. */
	void ClearRecords()
	{
		SelectionChanges.Reset();
		ClickedItems.Reset();
		DoubleClickedItems.Reset();
		HoverItems.Reset();
		HoverStates.Reset();
		ScrolledOffsets.Reset();
		FinishedOffsets.Reset();
		GeneratedItems.Reset();
		ReleasedItems.Reset();
		ExpansionItems.Reset();
		ExpansionStates.Reset();
		DragDetectedItems.Reset();
		DragCancelledItems.Reset();
		DropItems.Reset();
		DropZones.Reset();
		DraggingStates.Reset();
		LastDropPayload = nullptr;
	}

	TArray<int32> SelectionChanges;
	TArray<int32> ClickedItems;
	TArray<int32> DoubleClickedItems;
	TArray<int32> HoverItems;
	TArray<bool> HoverStates;
	TArray<float> ScrolledOffsets;
	TArray<float> FinishedOffsets;
	TArray<int32> GeneratedItems;
	TArray<int32> ReleasedItems;
	TArray<int32> ExpansionItems;
	TArray<bool> ExpansionStates;
	TArray<int32> DragDetectedItems;
	TArray<int32> DragCancelledItems;
	TArray<int32> DropItems;
	TArray<EDreamItemDropZone> DropZones;
	TArray<bool> DraggingStates;

	/** Reflected, so a payload the list has already let go of is still here to be compared. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> LastDropPayload = nullptr;
};

/**
 * Setup shared by the four list-family interaction files. Plain inline functions rather than a
 * fixture class: each test still reads top to bottom, and none of this is worth a type.
 */
namespace DreamListsInteraction
{
	/** The viewport every test in this family drives. */
	inline FIntPoint ViewportSize()
	{
		return FIntPoint(1280, 720);
	}

	/** InCount fresh items, outered to the transient package and numbered in the order they were made. */
	inline TArray<UObject*> MakeItems(int32 InCount)
	{
		TArray<UObject*> Result;
		Result.Reserve(InCount);
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			UDreamListsInteractionItem* Item = NewObject<UDreamListsInteractionItem>(GetTransientPackage());
			Item->Ordinal = Index;
			Result.Add(Item);
		}
		return Result;
	}

	/**
	 * InStyle with rows exactly InRowHeight apart: no gap between rows and no inset around them.
	 *
	 * Every pixel and offset a test works out is a multiple of the row pitch, so the pitch has to be
	 * the number the test states -- not the project sheet's, and not the sum of three defaults.
	 */
	inline FDreamListStyle WithRows(FDreamListStyle InStyle, float InRowHeight)
	{
		InStyle.RowHeight = InRowHeight;
		InStyle.RowSpacing = 0.0f;
		InStyle.Padding = FMargin(0.0f);
		return InStyle;
	}

	/** Bind every list event the probe has a handler for. Once per list: a second bind would count twice. */
	inline void ListenToList(UDreamListViewBase& InList, UDreamListsInteractionProbe& InProbe)
	{
		InList.OnSelectionChanged.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordSelectionChanged);
		InList.OnItemClicked.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemClicked);
		InList.OnItemDoubleClicked.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemDoubleClicked);
		InList.OnItemIsHoveredChanged.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemHoverChanged);
		InList.OnListViewScrolled.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordListScrolled);
		InList.OnListViewFinishedScrolling.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordListFinishedScrolling);
		InList.OnRowGenerated.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordRowGenerated);
		InList.OnRowReleased.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordRowReleased);
		InList.OnItemDragDetected.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemDragDetected);
		InList.OnItemDragCancelled.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemDragCancelled);
		InList.OnItemAcceptDrop.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordItemAcceptDrop);
		InList.OnDraggingStateChanged.AddDynamic(&InProbe, &UDreamListsInteractionProbe::RecordDraggingStateChanged);
	}
}
