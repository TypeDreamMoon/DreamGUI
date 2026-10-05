// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIForAdapter.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Interaction/UIRecyclableScrollView.h"
#include "DreamUIEachAdapter.generated.h"

class UDreamUserWidget;
class UDreamWidget;

/**
 * The data source an `each` block becomes: it feeds the host's recyclable view its item count and,
 * per cell, applies the block's item bindings -- read the member off the item object, push it
 * through the target's setter, addressed inside the cell's cloned subtree by display name.
 *
 * Owned by the user widget that resolved the binding; one adapter per `each`. Refresh() re-reads
 * the source (function call or array property, both by reflection, on the user widget or -- with a
 * SourcePath -- on the object the path reaches) and tells the view; the OWNER calls it when the source
 * announces a change, or anything along its path does.
 *
 * Per cell, beside the values: a watcher on the cell's item, so a member the item announces is written
 * onto that cell alone when it changes, and the routes of the block's `-> Item.Func` lines, so the
 * cell's button calls the cell's item. SetCell moves both when the view recycles a cell for another
 * row -- the old item's routes off, then the new item's on (FDreamUIEntryRows, the core's half of it,
 * shared with `for`).
 */
UCLASS()
class DREAMGUICONTROLS_API UDreamUIEachAdapter : public UObject, public IUIRecyclableScrollViewDataSource
{
	GENERATED_BODY()

public:
	void Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UUIRecyclableScrollView* InView);

	/** Re-read the source and update the view. Safe to call any time after Initialize. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Each")
	void Refresh();

	const FDreamWidgetEachBinding& GetBinding() const { return Binding; }

	/** The items the source supplied at the last Initialize or Refresh. */
	const TArray<TObjectPtr<UObject>>& GetItems() const { return Items; }

	/** How many entry bindings have been written onto cells so far, SetCell and item reports together. Tests. */
	int32 GetEntryWriteCount() const { return EntryWriteCount; }

	/** The cells' per-item watchers and routes. Diagnostics and tests. */
	const FDreamUIEntryRows& GetRows() const { return Rows; }

	virtual int32 GetItemCount_Implementation() override { return Items.Num(); }
	virtual void InitOnCreate_Implementation(UDreamUIBehaviour* Component) override {}
	virtual void BeforeSetCell_Implementation() override;
	virtual void SetCell_Implementation(UDreamUIBehaviour* Component, int32 Index) override;
	virtual void AfterSetCell_Implementation() override {}

	virtual void BeginDestroy() override;

private:
	void FetchItems();

	/** The cell's widgets by display name, first of each name kept: what the entry bindings and routes address. */
	static void MapCellWidgets(UDreamWidget* InCellRoot, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName);

	/** Entry InEntryIndex, InItem's member onto the cell's widget. See FDreamUIEntryRows::WriteEntry. */
	void ApplyEntryBinding(int32 InEntryIndex, UObject* InItem, const TMap<FName, UDreamWidget*>& InCellWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets);

	/** InCell's item announced the member entry InEntryIndex reads: that entry, on that cell, and nothing else. */
	void HandleEntryChanged(UDreamWidget* InCell, int32 InEntryIndex);

	/** False once the view is fed by another adapter (its owner re-resolved): the cells are that adapter's now. */
	bool IsFeedingView() const;

	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> Owner;

	UPROPERTY(Transient)
	TObjectPtr<UUIRecyclableScrollView> View;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Items;

	FDreamWidgetEachBinding Binding;

	/** Each cell's item watcher and routes, keyed by the cell's root widget, weakly. */
	FDreamUIEntryRows Rows;

	int32 EntryWriteCount = 0;
};

namespace DreamUIEachAdapter
{
	/** Hand the core the list views' IDreamUIEachBindingHandler; this module's StartupModule does. */
	void RegisterEachBindingHandler();
	/** Take it back, unless something else registered one since; this module's ShutdownModule does. */
	void UnregisterEachBindingHandler();
}
