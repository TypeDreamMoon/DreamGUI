// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBindingObserver.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Templates/UniquePtr.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "DreamUIForAdapter.generated.h"

class UDreamUserWidget;

/**
 * The per-row half of a loop, shared by both kinds: for every row -- a `for` copy (UDreamUIForAdapter), a list cell
 * (the list views' UDreamUIEachAdapter) -- the item it shows, a watcher on that item's bound members, and the routes
 * its `-> Item.Func` lines placed on the row's events.
 *
 *     for Item in Player.Items {
 *         HorizontalBox Row { Text Label { Text <- Item.Name }  Native.Button Use { OnClicked -> Item.Use() } }
 *     }
 *
 * The watcher is what lets one row follow its own item: Item.Name announced on the third item rewrites the third row's
 * Label and nothing else, with no list refresh. One FDreamUIBindingObserver per row, rooted at the item, one path per
 * entry binding with the entry's index as its client; members the item's class does not announce are simply not
 * subscribed (a row of a class that announces none of them gets no watcher at all), and only a refresh writes those.
 *
 * The routes are the other half: a row's button calls ITS item. Aiming a row at another item -- a recycled cell, a
 * `for` copy re-aimed -- takes the old item's routes off before placing the new one's, so a row never calls two items.
 * A row seen for the first time is also scrubbed of what a duplicate may have brought along: a widget copied while it
 * carried a route (the copies of a duplicated owner, the cells of a duplicated list) arrives with the listener and no
 * record of it, and every listener of the route's function on that event is taken off before the row's own is placed.
 *
 * Not a UObject. Held by value inside the adapter, which is the lifetime owner every watcher's delegates are bound to
 * weakly, and keyed by row widget weakly: a row destroyed by anyone is dropped at the next ReleaseDeadRows.
 */
class FDreamUIEntryRows
{
public:
	/** One entry of InRow needs writing again: InRow's item announced the member entry InEntryIndex reads. */
	DECLARE_DELEGATE_TwoParams(FOnEntryChanged, UDreamWidget* /*InRow*/, int32 /*InEntryIndex*/);

	FDreamUIEntryRows() = default;
	FDreamUIEntryRows(const FDreamUIEntryRows&) = delete;
	FDreamUIEntryRows& operator=(const FDreamUIEntryRows&) = delete;
	DREAMGUI_API ~FDreamUIEntryRows();

	/** Forget every row (watchers stopped, routes left where they are) and start over for a new loop. */
	DREAMGUI_API void Initialize(UObject* InLifetimeOwner, FOnEntryChanged InOnEntryChanged);

	/**
	 * Show InItem on InRow: when it is not what InRow showed, the old item's routes come off, the watcher is re-aimed
	 * (or made, the first time an item of a class that announces something arrives), and InItem's routes go on.
	 * Nothing when InRow already shows InItem. Null InItem leaves the row showing nothing: no routes, no watcher.
	 * InWidgetsByDisplayName is the row's widgets as the entry bindings address them (first of each name wins).
	 * Writes no values; the caller writes the entries, all of them, once.
	 */
	DREAMGUI_API void AimRow(UDreamWidget* InRow, UObject* InItem, const FDreamWidgetEachBinding& InBinding,
		const TMap<FName, UDreamWidget*>& InWidgetsByDisplayName);

	/** Stop InRow's watcher, take its routes off when bInUnbindRoutes, and forget it. */
	DREAMGUI_API void ReleaseRow(const UDreamWidget* InRow, bool bInUnbindRoutes);

	/** ReleaseRow for every row. */
	DREAMGUI_API void ReleaseAll(bool bInUnbindRoutes);

	/** Forget the rows whose widget is gone. Their routes went down with their widgets; their watchers are stopped. */
	DREAMGUI_API void ReleaseDeadRows();

	/** Stop every watcher and place nothing more: what BeginDestroy does, inside a collection, touching no widget. */
	DREAMGUI_API void StopWatching();

	/** The item InRow shows, or null. */
	DREAMGUI_API UObject* GetItem(const UDreamWidget* InRow) const;

	/** How many rows have a watcher, and how many (object, field) subscriptions those hold. Diagnostics and tests. */
	DREAMGUI_API int32 GetWatchedRowCount() const;
	DREAMGUI_API int32 GetSubscriptionCount() const;

	/** How many routes the rows have placed and not taken back. Diagnostics and tests. */
	DREAMGUI_API int32 GetRouteCount() const;

	int32 Num() const { return Rows.Num(); }

	/**
	 * Whether a loop has anything per row at all: an entry binding to watch, or a route to place. A loop with neither
	 * keeps no rows -- what every loop was before item watching existed.
	 */
	static bool HasRowWork(const FDreamWidgetEachBinding& InBinding)
	{
		return InBinding.EntryBindings.Num() > 0 || InBinding.EntryRoutes.Num() > 0;
	}

	/** InWidgets by display name, first of each name kept: the addressing both adapters' entry bindings use. */
	DREAMGUI_API static void MapByDisplayName(TConstArrayView<UDreamWidget*> InWidgets, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName);

	/**
	 * Write one entry binding: InItem's member onto InTarget, through the setter when the builder named one (the shared
	 * conversion: an exact type copied whole, two numbers of different widths converted, anything else refused), or
	 * straight into the property when it did not -- which it allows only for a user widget's variable (a component's
	 * `props`). A direct write broadcasts the variable when it is FieldNotify, skips a value that is already there, and
	 * adds the user widget to OutWrittenUserWidgets for RerunWrittenUserWidgets. False when nothing was written.
	 */
	DREAMGUI_API static bool WriteEntry(const FDreamWidgetEntryBinding& InEntry, UObject* InTarget, UObject* InItem,
		TSet<UDreamUserWidget*>& OutWrittenUserWidgets);

	/**
	 * What a setter would have done after a direct write -- show the change: every written user widget runs its own
	 * `<-` bindings and its `for` and `each` blocks again, once however many of its properties were written. Only an
	 * initialized one: anything else is a duplicate still waiting for its tree.
	 */
	DREAMGUI_API static void RerunWrittenUserWidgets(const TSet<UDreamUserWidget*>& InWrittenUserWidgets);

private:
	/** One route a row placed: the object whose event it is, the event, and the item's function it calls. */
	struct FBoundRoute
	{
		TWeakObjectPtr<UObject> Target;
		FName EventName;
		FName ItemFunction;
	};

	struct FRow
	{
		FRow() = default;
		FRow(FRow&&) = default;
		FRow& operator=(FRow&&) = default;
		FRow(const FRow&) = delete;
		FRow& operator=(const FRow&) = delete;

		TWeakObjectPtr<UDreamWidget> Widget;
		TWeakObjectPtr<UObject> Item;
		/** Null while the row shows nothing a watcher could hear. Behind a pointer so its address survives the map growing. */
		TUniquePtr<FDreamUIBindingObserver> Observer;
		TArray<FBoundRoute> Routes;
	};

	void PlaceRoutes(FRow& InOutRow, UObject* InItem, const FDreamWidgetEachBinding& InBinding,
		const TMap<FName, UDreamWidget*>& InWidgetsByDisplayName, bool bInScrubFirst);
	void TakeRoutesOff(FRow& InOutRow);
	void Retire(TUniquePtr<FDreamUIBindingObserver>&& InObserver);
	bool ShouldWatch(const UClass* InItemClass, const FDreamWidgetEachBinding& InBinding);
	void HandleClientChanged(TWeakObjectPtr<UDreamWidget> InRow, int32 InClientId);

	TMap<TObjectKey<UDreamWidget>, FRow> Rows;

	TWeakObjectPtr<UObject> LifetimeOwner;
	FOnEntryChanged OnEntryChanged;

	/**
	 * Watchers taken off a row while a report is on the stack -- possibly from the very watcher reporting -- kept, stopped,
	 * until the outermost report returns: one must never be destroyed under its own callback.
	 */
	TArray<TUniquePtr<FDreamUIBindingObserver>> RetiredObservers;
	int32 ReportDepth = 0;

	/** The last item class ruled on by ShouldWatch, and the ruling: a list is nearly always one class throughout. */
	TWeakObjectPtr<const UClass> LastRuledClass;
	bool bLastRuledClassAnnounces = false;
};

/**
 * What a `for` block becomes at run time: one copy of the template widget per item of the source, made inside the
 * panel the template sits in, in item order, where the template sits among that panel's children.
 *
 *     VerticalBox Options {
 *         Text Header { }
 *         for Option in GetOptions() {
 *             Row { Label <- Option.Label }
 *         }
 *         Text Footer { }
 *     }
 *
 * gives Header, the template (collapsed), one Row per option, Footer. The template stays in the tree because it is
 * the class's authored copy -- every instance of the class has one, the class variable points at it, and it is what
 * every copy is cloned from -- but Collapsed: no layout space, no drawing, no hit testing, and no navigation (both
 * the selectable search and UDreamWidgetNavigation ask GetRenderVisibleInHierarchy). The copies are ordinary widgets
 * that join the tree through DuplicateWidget, so they are registered as they arrive and their selectables register
 * with them, exactly as an authored widget's do.
 *
 * Where an `each` is the list views' (virtualized, run through IDreamUIEachBindingHandler by the controls module),
 * a `for` is the core's: no list view, no recycling, a copy per item for as long as the item is in the source. That
 * is the right shape for the short lists screens are made of -- a settings page's options, a tab bar, a legend --
 * and the wrong one for ten thousand rows, which is what `each` is for.
 *
 * The source may be a member path (`for Item in Player.Items`), read on whatever the path's leading members hold at
 * each refresh; the owner refreshes the adapter when anything along it changes. Between refreshes each copy follows
 * its own item (FDreamUIEntryRows): a member the item announces is written onto that copy alone, and the body's
 * `-> Item.Func` lines call that copy's item.
 *
 * Owned by the user widget that resolved the binding (UDreamUserWidget::ResolveEachBindings keeps it in
 * EachAdapters beside the `each` adapters), outered to it, one per `for`. The copies live in that widget's tree
 * like any other widget, so destroying the widget takes them down with it.
 */
UCLASS(Transient)
class DREAMGUI_API UDreamUIForAdapter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Take over one `for` of InOwner and make its first copies.
	 *
	 * InHost is the widget the `for` was written in; InTemplate the widget built from its body. The copies go under
	 * the template's parent at the time of each refresh, which is InHost unless a component routed the template into
	 * one of its slots when it was initialized -- the copies belong beside the template, wherever that ended up.
	 *
	 * Copies the owner already has are adopted rather than doubled: an owner that is itself a COPY (a `for` row
	 * whose own class has a `for`, an `each` cell) arrives with the source's copies duplicated along with everything
	 * else, and a new adapter that knew nothing of them would leave them standing beside its own.
	 */
	void Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UDreamWidget* InHost, UDreamWidget* InTemplate);

	/**
	 * Re-read the source and bring the copies in line with it.
	 *
	 * Keyed by item, the way Vue keys a `v-for`: a copy that showed an object still in the list is kept and moved,
	 * a copy whose object left is destroyed, and a copy is made for each object that is new. Kept copies keep what
	 * they hold that the item bindings do not write -- focus, a toggle's state, a running animation -- which is the
	 * difference between a reorder and a rebuild for the player who had a row selected. Every copy then has the
	 * item bindings applied again, because an object that stayed may have changed underneath; when the list is the
	 * same objects in the same order that re-application is all a refresh does.
	 *
	 * Safe to call any time after Initialize, from anywhere, including from inside a refresh -- a nested request
	 * runs once the outer one is done.
	 */
	void Refresh();

	/**
	 * Destroy every copy this adapter made and forget its items, taking every watcher off the items and every route off
	 * the copies first. The template is left as it is.
	 */
	void ReleaseCopies();

	const FDreamWidgetEachBinding& GetBinding() const { return Binding; }
	/** Out of line: UDreamUserWidget is only declared here, and the weak pointer's Get casts to it. */
	UDreamUserWidget* GetOwningWidget() const;
	UDreamWidget* GetHost() const { return Host.Get(); }
	UDreamWidget* GetTemplate() const { return Template.Get(); }

	/** The copies, in item order -- the order they stand in among the panel's children. */
	TArray<UDreamWidget*> GetCopies() const;

	/** The items the copies show, in the same order. */
	const TArray<TObjectPtr<UObject>>& GetItems() const { return Items; }

	/** How many entry bindings have been written onto copies so far, refreshes and item reports together. Tests. */
	int32 GetEntryWriteCount() const { return EntryWriteCount; }

	/** The copies' per-item watchers and routes. Diagnostics and tests. */
	const FDreamUIEntryRows& GetRows() const { return Rows; }

	virtual void BeginDestroy() override;

private:
	void RefreshOnce();

	/**
	 * The source's items: the function called or the array variable read, by reflection, as the `each` adapter does --
	 * on the user widget, or with a SourcePath on the object the path's leading members reach (none when it is broken).
	 */
	void FetchItems(TArray<UObject*>& OutItems) const;

	/** The source's copies in a duplicated owner, taken as this adapter's own. See Initialize. */
	void AdoptCopiesLeftByDuplication(UDreamWidget* InPanel, UDreamWidget* InTemplate);

	/**
	 * False while a user widget inside the template is a duplicate that has not been adopted yet.
	 *
	 * DuplicateDreamWidgetHierarchy hands every duplicated user widget its own tree in one pass, parents first, so
	 * while an owner that is a copy is being initialized, the user widgets further down -- its `for` template's
	 * among them -- still point at the SOURCE's trees. A copy cloned from one of those would get no tree of its own
	 * at all (DuplicateDreamWidgetHierarchy warns and leaves it on the template's), so new copies wait for the
	 * next frame, when the pass has finished.
	 */
	bool IsTemplateReadyToCopy(const UDreamWidget* InTemplate) const;

	/** One copy of InTemplate under InPanel, shown. Null when the panel refused it. */
	UDreamWidget* MakeCopy(UDreamWidget* InPanel, UDreamWidget* InTemplate) const;

	/** Stand InCopies right after InTemplate among InPanel's children, in the order given. */
	static void PlaceCopies(UDreamWidget* InPanel, UDreamWidget* InTemplate, TConstArrayView<UDreamWidget*> InCopies);

	/** The widgets of InCopy the entry bindings and routes address, by display name. See CollectAuthoredWidgets. */
	static void MapCopyWidgets(UDreamWidget* InCopy, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName);

	/**
	 * The `Prop <- Var.Member` lines, for one copy and its item. A target with a setter goes through it, as an
	 * `each` cell's does; one the builder recorded without a setter (a component's `props` variable) is written
	 * straight into the copy, and the user widget written to is collected so its own bindings can be run again.
	 */
	void ApplyEntryBindings(UObject* InItem, const TMap<FName, UDreamWidget*>& InCopyWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets);

	/** One of those lines alone: entry InEntryIndex. See FDreamUIEntryRows::WriteEntry. */
	void ApplyEntryBinding(int32 InEntryIndex, UObject* InItem, const TMap<FName, UDreamWidget*>& InCopyWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets);

	/** InCopy's item announced the member entry InEntryIndex reads: that entry, on that copy, and nothing else. */
	void HandleEntryChanged(UDreamWidget* InCopy, int32 InEntryIndex);

	void ScheduleDeferredRefresh();
	bool HandleDeferredRefresh(float InDeltaTime);

	/**
	 * Weak: the owner holds this adapter, not the other way round, and a copy of the owner must never be able to
	 * reach back through a stale pointer into the source. DuplicateTransient on every reference for that second
	 * reason -- an adapter copied along with anything is inert rather than in charge of somebody else's copies.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	TWeakObjectPtr<UDreamUserWidget> Owner;

	UPROPERTY(Transient, DuplicateTransient)
	TWeakObjectPtr<UDreamWidget> Host;

	UPROPERTY(Transient, DuplicateTransient)
	TWeakObjectPtr<UDreamWidget> Template;

	/**
	 * Held, as the `each` adapter holds its items: the key a copy is kept by is the object's identity, and an
	 * object let go while its copy still shows it could be replaced by a new one at the same address.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<UObject>> Items;

	/** Parallel to Items. An entry is null only when the panel refused that copy. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<UDreamWidget>> Copies;

	FDreamWidgetEachBinding Binding;

	/** Each copy's item watcher and routes, keyed by copy. Not reflected: weak keys, and nothing in it is a reference. */
	FDreamUIEntryRows Rows;

	int32 EntryWriteCount = 0;

	/**
	 * What a copy is shown as: the template's AUTHORED visibility, read off the class's archetype, since the live
	 * template is collapsed by the time anyone could ask it (and arrives collapsed in a duplicated owner). A template
	 * authored Collapsed would make every copy invisible, which is never what a `for` means, so that reads Visible.
	 */
	EDreamWidgetVisibility ShownVisibility = EDreamWidgetVisibility::Visible;

	FTSTicker::FDelegateHandle DeferredRefreshHandle;

	/** How many refreshes have waited a frame for the template; see IsTemplateReadyToCopy. Bounded. */
	int32 DeferredRefreshCount = 0;

	/** Set once a refresh has found the template and the copies adopted. Nothing un-adopts them afterwards. */
	bool bCopiesKnownReady = false;

	/** Plain bools rather than bitfields: TGuardValue takes them by reference. */
	bool bRefreshing = false;
	bool bRefreshRequestedWhileRefreshing = false;
};
