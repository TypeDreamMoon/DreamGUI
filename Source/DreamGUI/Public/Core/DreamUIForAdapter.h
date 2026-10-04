// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "DreamUIForAdapter.generated.h"

class UDreamUserWidget;

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

	/** Destroy every copy this adapter made and forget its items. The template is left as it is. */
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

	virtual void BeginDestroy() override;

private:
	void RefreshOnce();

	/** The source's items: the function called or the array variable read, by reflection, as the `each` adapter does. */
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

	/**
	 * The `Prop <- Var.Member` lines, for one copy and its item. A target with a setter goes through it, as an
	 * `each` cell's does; one the builder recorded without a setter (a component's `props` variable) is written
	 * straight into the copy, and the user widget written to is collected so its own bindings can be run again.
	 */
	void ApplyEntryBindings(UDreamWidget* InCopy, UObject* InItem, TSet<UDreamUserWidget*>& OutWrittenUserWidgets) const;

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
