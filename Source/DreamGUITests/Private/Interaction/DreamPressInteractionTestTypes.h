// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamMenuAnchor.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "UObject/Object.h"
#include "DreamPressInteractionTestTypes.generated.h"

/**
 * Somewhere the public events of the point-and-click controls can land, counted.
 *
 * A control's events are dynamic multicast delegates, and those bind only to a UFUNCTION on a
 * UObject -- a lambda will not do -- so a claim about what a control TOLD its listeners needs a real
 * listener. Counting at the listener rather than reading the control's state is the point: a control
 * whose state moved while its event never fired is exactly the bug a Blueprint author meets (the
 * graph bound to OnClicked simply never runs), and only a listener can see it.
 *
 * Counts are plain members rather than lookups by name, so a misspelt event is a compile error and
 * not a count that is silently zero -- which matters most for the assertions that expect zero. Log
 * keeps the arrival ORDER, for the claims that are about order (pressed before released before
 * clicked); an ordering claim compares whole names, so a misspelling there fails loudly.
 *
 * One class for every point-and-click control rather than one per control: the signatures repeat
 * (a no-argument event, an index, a bool), a test binds only what it asserts on, and a handler
 * nobody binds costs nothing.
 */
UCLASS()
class UDreamPressInteractionListener : public UObject
{
	GENERATED_BODY()

public:
	/** Every event that arrived, by name, in arrival order. */
	TArray<FName> Log;

	/** Log with everything but the named events taken out, so an ordering claim can ignore the rest. */
	TArray<FName> LogOnly(const TArray<FName>& InEvents) const
	{
		TArray<FName> Filtered;
		for (const FName& Entry : Log)
		{
			if (InEvents.Contains(Entry))
			{
				Filtered.Add(Entry);
			}
		}
		return Filtered;
	}

	// ---------------------------------------------------------------- the pointer's five moments

	int32 ClickedCount = 0;
	int32 PressedCount = 0;
	int32 ReleasedCount = 0;
	int32 HoveredCount = 0;
	int32 UnhoveredCount = 0;

	UFUNCTION()
	void HandleClicked() { ++ClickedCount; Log.Add(TEXT("Clicked")); }

	UFUNCTION()
	void HandlePressed() { ++PressedCount; Log.Add(TEXT("Pressed")); }

	UFUNCTION()
	void HandleReleased() { ++ReleasedCount; Log.Add(TEXT("Released")); }

	UFUNCTION()
	void HandleHovered() { ++HoveredCount; Log.Add(TEXT("Hovered")); }

	UFUNCTION()
	void HandleUnhovered() { ++UnhoveredCount; Log.Add(TEXT("Unhovered")); }

	// ---------------------------------------------------------------- check boxes and radios

	/** One entry per OnCheckStateChanged, carrying the state it announced. */
	TArray<EDreamCheckState> CheckStates;

	UFUNCTION()
	void HandleCheckStateChanged(EDreamCheckState InCheckedState)
	{
		CheckStates.Add(InCheckedState);
		Log.Add(TEXT("CheckStateChanged"));
	}

	// ---------------------------------------------------------------- lists that open

	int32 OpeningCount = 0;
	int32 ClosedCount = 0;

	/** One entry per selection change, carrying the index it announced. The dropdown's and the ring's. */
	TArray<int32> SelectionIndices;

	/**
	 * The dropdown's option rows, by option index, as OnItemGenerated handed them over.
	 *
	 * Reflected, because the rows belong to the control and are destroyed with it; a listener that
	 * kept them alive only by luck would be one that dereferences a collected widget.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidget>> GeneratedItems;

	UFUNCTION()
	void HandleOpening() { ++OpeningCount; Log.Add(TEXT("Opening")); }

	UFUNCTION()
	void HandleClosed() { ++ClosedCount; Log.Add(TEXT("Closed")); }

	UFUNCTION()
	void HandleSelectionChanged(int32 InSelectedIndex)
	{
		SelectionIndices.Add(InSelectedIndex);
		Log.Add(TEXT("SelectionChanged"));
	}

	UFUNCTION()
	void HandleItemGenerated(int32 InItemIndex, UDreamWidget* InItem)
	{
		if (InItemIndex < 0)
		{
			return;
		}
		if (GeneratedItems.Num() <= InItemIndex)
		{
			GeneratedItems.SetNum(InItemIndex + 1);
		}
		GeneratedItems[InItemIndex] = InItem;
	}

	// ---------------------------------------------------------------- menus

	/** One entry per OnMenuOpenChanged, carrying whether it said open or closed. */
	TArray<bool> MenuOpenStates;

	int32 TriggerClickedCount = 0;

	/**
	 * The anchor a trigger button opens -- the UMG arrangement, where a menu anchor opens from a
	 * button's click handler rather than from a click of its own.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UDreamMenuAnchor> MenuAnchorToOpen = nullptr;

	UFUNCTION()
	void HandleMenuOpenChanged(bool bInIsOpen)
	{
		MenuOpenStates.Add(bInIsOpen);
		Log.Add(TEXT("MenuOpenChanged"));
	}

	/**
	 * What a button wrapping a UMG menu anchor does with its click: ask ShouldOpenDueToClick, and open
	 * only when it says yes. A click that lands while the menu is up is the click that DISMISSED it,
	 * and opening again from it would make a menu impossible to close by clicking what opened it.
	 */
	UFUNCTION()
	void HandleTriggerClicked()
	{
		++TriggerClickedCount;
		Log.Add(TEXT("TriggerClicked"));
		if (IsValid(MenuAnchorToOpen) && MenuAnchorToOpen->ShouldOpenDueToClick())
		{
			MenuAnchorToOpen->Open();
		}
	}

	// ---------------------------------------------------------------- tabs

	TArray<int32> TabChangedIndices;
	TArray<int32> TabClosedIndices;
	/** X is where the tab came from, Y where it went, both into the strip as it was before the move. */
	TArray<FIntPoint> TabReorders;

	UFUNCTION()
	void HandleTabChanged(int32 InActiveTabIndex)
	{
		TabChangedIndices.Add(InActiveTabIndex);
		Log.Add(TEXT("TabChanged"));
	}

	UFUNCTION()
	void HandleTabClosed(int32 InClosedTabIndex)
	{
		TabClosedIndices.Add(InClosedTabIndex);
		Log.Add(TEXT("TabClosed"));
	}

	UFUNCTION()
	void HandleTabReordered(int32 InFromIndex, int32 InToIndex)
	{
		TabReorders.Add(FIntPoint(InFromIndex, InToIndex));
		Log.Add(TEXT("TabReordered"));
	}

	// ---------------------------------------------------------------- expanders

	TArray<bool> ExpansionStates;

	UFUNCTION()
	void HandleExpansionChanged(bool bInIsExpanded)
	{
		ExpansionStates.Add(bInIsExpanded);
		Log.Add(TEXT("ExpansionChanged"));
	}

	// ---------------------------------------------------------------- dialogs

	TArray<FName> DialogClosedResults;
	TArray<FName> DialogButtonResults;

	UFUNCTION()
	void HandleDialogClosed(FName InResult)
	{
		DialogClosedResults.Add(InResult);
		Log.Add(TEXT("DialogClosed"));
	}

	UFUNCTION()
	void HandleDialogButtonClicked(FName InResult)
	{
		DialogButtonResults.Add(InResult);
		Log.Add(TEXT("DialogButtonClicked"));
	}

	// ---------------------------------------------------------------- borders

	int32 BorderButtonDownCount = 0;
	int32 BorderButtonUpCount = 0;
	int32 BorderMoveCount = 0;
	int32 BorderDoubleClickCount = 0;

	UFUNCTION()
	void HandleBorderButtonDown(UDreamPointerEventData* InPointerEvent) { ++BorderButtonDownCount; Log.Add(TEXT("BorderButtonDown")); }

	UFUNCTION()
	void HandleBorderButtonUp(UDreamPointerEventData* InPointerEvent) { ++BorderButtonUpCount; Log.Add(TEXT("BorderButtonUp")); }

	UFUNCTION()
	void HandleBorderMove(UDreamPointerEventData* InPointerEvent) { ++BorderMoveCount; Log.Add(TEXT("BorderMove")); }

	UFUNCTION()
	void HandleBorderDoubleClick(UDreamPointerEventData* InPointerEvent) { ++BorderDoubleClickCount; Log.Add(TEXT("BorderDoubleClick")); }

	// ---------------------------------------------------------------- ring menus

	TArray<int32> ActivatedIndices;
	TArray<FName> ActivatedTags;

	UFUNCTION()
	void HandleItemActivated(int32 InItemIndex, FName InItemTag)
	{
		ActivatedIndices.Add(InItemIndex);
		ActivatedTags.Add(InItemTag);
		Log.Add(TEXT("ItemActivated"));
	}
};
