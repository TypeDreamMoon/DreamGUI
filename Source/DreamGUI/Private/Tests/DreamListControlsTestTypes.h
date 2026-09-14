// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamListControlsTestTypes.generated.h"

class UDreamWidget;

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
	TMap<UObject*, TArray<UObject*>> Children;

	/** Reflected, because a probe that keeps a widget alive only by luck is a probe that crashes. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> LastItem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> LastRow = nullptr;
};
