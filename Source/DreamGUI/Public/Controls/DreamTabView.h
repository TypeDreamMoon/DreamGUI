// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamTabView.generated.h"

class UDreamLayoutContainerWidgetSwitcher;
class UDreamWidget;
class UUIToggle;
class UUIToggleGroup;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamTabViewChangedEvent, int32, ActiveTabIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamTabViewTabEvent, int32, TabIndex, UDreamWidget*, Tab);

/**
 * One generated tab's parts.
 *
 * A struct rather than four parallel arrays: the four are created together, restyled together and
 * destroyed together, and four arrays that must stay the same length is a bug waiting for the first
 * early-out that returns between two of them.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTabViewTab
{
	GENERATED_BODY()

	/** The clickable face. Carries the overlay, the toggle, and the pointer transition. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> TabNode = nullptr;

	/** The full-bleed plate the CHECKED transition tints. See UDreamTabView's class comment. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> SelectedNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UUIToggle> Toggle = nullptr;
};

/**
 * A tab view whose hierarchy is code, not an asset: a strip of tabs over a switcher of pages.
 *
 * WHERE THE PAGES COME FROM, which is the only interesting question a tab view asks.
 *
 * The control owns the switcher, and a page is simply a child the consumer nested inside the tab
 * view. Nothing is copied and nothing is bound by name: a UDreamUserWidget placed in a host tree
 * keeps whatever the host authored UNDER it, and InitializeWidgetStatic runs a nested instance's
 * Initialize -- so by the time NativeOnInitialized runs, the pages are already sitting on this
 * widget. All this control does is snapshot them before it builds anything of its own, then hang
 * each one under the switcher. So this is a working tab view, from .dui, with nothing else:
 *
 *     Native.TabView Settings {
 *         Widget Video    { ... }
 *         Widget Audio    { ... }
 *         Widget Controls { ... }
 *     }
 *
 * The two routes that were considered and are NOT what this does:
 *
 *   - A NamedSlot. UDreamNamedSlot takes exactly one child, so pages would need a panel wrapped
 *     around them; worse, the host-fills-the-slot step lives inside InitializeWidgetStatic behind
 *     an `InWidgetTreeArchetype != nullptr` gate, and a NATIVE class never has a widget-tree
 *     archetype. A native control cannot receive named-slot content at all.
 *   - Pages as a TArray property. The language cannot write a container: a tuple on a non-short-form
 *     destination is refused outright (ValueTypeMismatch), and a bare string cannot spell an array
 *     for ImportText. Nesting is the one thing .dui does natively, so nesting is the door.
 *
 * WHERE THE TABS COME FROM. One tab per index, for as many indices as there are labels OR pages --
 * whichever is more, so the strip is never emptier than the thing it drives. TabLabels names tab i
 * when it has an entry; otherwise the tab wears the page's own node id. That fallback is what makes
 * the file above work with no label list at all, and TabLabels stays the spelling that a details
 * panel, a Blueprint and a binding can all reach:
 *
 *     Native.TabView Settings {
 *         TabLabels      <-  GetSettingsTabs()      // localizable, from the host class
 *         ActiveTabIndex <-> CurrentSettingsTab
 *         ...
 *     }
 *
 * The same container limit bites the assignment form -- `TabLabels = (...)` does not parse into an
 * array -- so a .dui that wants translated labels binds them rather than writing them inline. A node
 * id is an identifier and not a translatable string, which is why the fallback is deliberately
 * culture-invariant instead of pretending otherwise.
 *
 * WHY A TAB IS TWO VISUALS. A tab is a radio button that looks like a button: the toggles share one
 * UUIToggleGroup, so mutual exclusion is the group's, not this control's. But a UUISelectable owns
 * exactly two transitions and a tab needs three appearances -- pointer states, selected, and the
 * label. The pointer transition tints the face; the CHECKED transition tints a separate full-bleed
 * plate over it (aimed at one visual they overwrite each other and the selected colour would live
 * only until the next hover -- the toggle's box-and-tick split, restated); and the label colour has
 * no transition left, so the control pushes it directly. Three appearances, two transitions, one
 * explicit push.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Tab View")
class DREAMGUI_API UDreamTabView : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View")
	FDreamTabViewStyle Style;

	/**
	 * The tab captions, index-matched to the pages. An entry names that tab; a missing or empty one
	 * leaves the tab wearing its page's node id (see the class comment). Longer than the page list
	 * is allowed and grows the strip -- a screen that authors its tabs before its pages should see
	 * the strip it is building.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View")
	TArray<FText> TabLabels;

	/**
	 * Which tab is open, and therefore which page the switcher shows. A property rather than the
	 * getter/setter pair alone, because the pair alone is invisible: .dui writes properties, the
	 * designer lists properties, and a binding resolves a property.
	 *
	 * Stored as the REQUEST, never clamped against the current page count -- the index is routinely
	 * authored before the pages attach, and the switcher resolves it at layout time for the same
	 * reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetActiveTabIndex", BlueprintSetter = "SetActiveTabIndex", Category = "Tab View", meta = (ClampMin = "0"))
	int32 ActiveTabIndex = 0;

	/**
	 * A tab's CONTENT, authored elsewhere: one instance of this class is created inside every tab
	 * widget, filling it, and the built-in label steps aside. The tab's face, its selected plate,
	 * its hover and its place in the toggle group stay the control's, so a template only has to draw
	 * a tab.
	 *
	 * Rebuilt with the strip rather than pooled -- tabs are not recycled, there is one per page --
	 * and OnTabGenerated fires for each as it is made.
	 *
	 * Null (the default) is the built-in label tab. Instancing a user widget needs a world, so with
	 * none this quietly stays the built-in tab rather than producing half a strip. Exactly the
	 * bargain UDreamListViewBase::RowTemplateClass makes, in the same words.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View")
	TSubclassOf<UDreamUserWidget> TabTemplateClass;

	/**
	 * One per tab, as the strip is built. The hook for a consumer whose tabs are richer than a word
	 * but who would rather not author a whole class: everything under the tab is reachable from here
	 * by display name. The tab view's counterpart of the list's OnRowGenerated.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewTabEvent OnTabGenerated;

	/** Fired when the open tab changes, whoever changed it. A consumer binds to this, not to a tab. */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewChangedEvent OnTabChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewChangedEvent OnValueChangedBP;

	UFUNCTION(BlueprintCallable, Category = "Tab View")
	int32 GetActiveTabIndex() const { return ActiveTabIndex; }

	/** Open a tab. Broadcasts when the index actually moved, exactly as a click on the tab would. */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetActiveTabIndex(int32 InIndex);

	/**
	 * The same move without the broadcast. The `<->` desugar looks for precisely this name (the
	 * setter's plus "WithoutNotify") so the forward half of a two-way binding cannot echo back into
	 * the variable that just drove it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetActiveTabIndexWithoutNotify(int32 InIndex);

	/** Replace the captions and regenerate the strip. */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabLabels(const TArray<FText>& InLabels);

	/** How many pages the switcher holds. Not necessarily how many tabs the strip shows. */
	UFUNCTION(BlueprintPure, Category = "Tab View")
	int32 GetPageCount() const;

	UFUNCTION(BlueprintPure, Category = "Tab View")
	UDreamWidget* GetPage(int32 InIndex) const;

	/** The page the switcher is showing, or null before any page is attached. */
	UFUNCTION(BlueprintPure, Category = "Tab View")
	UDreamWidget* GetActivePage() const;

	/**
	 * Put a widget in as the next page, for a screen assembled in code rather than in .dui. The
	 * strip regenerates, so a page added past the end of TabLabels still gets a tab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void AddPage(UDreamWidget* InPage);

	virtual void ApplyStyle() override;

	/**
	 * The parts, in the shape the rest of the framework expects to find them.
	 *
	 * UPROPERTY rather than bare pointers on purpose: a part nothing reflects is a part the designer,
	 * the write-back and the bindings cannot see. Transient because they are rebuilt on every
	 * initialization and never belong to a saved package.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> BodyNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> StripNode = nullptr;

	/** The line under the open tab. Layout-ignoring; this control is the only thing that places it. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> IndicatorNode = nullptr;

	/** The page area: the switcher's widget, and the panel a page's background is drawn on. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> PageHostNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamLayoutContainerWidgetSwitcher> PageSwitcher = nullptr;

	/** Mutual exclusion, borrowed whole from the radio button's mechanism. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UUIToggleGroup> TabGroup = nullptr;

	/** One entry per tab in the strip, in strip order. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TArray<FDreamTabViewTab> Tabs;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	virtual void OnPartsReady() override;

#if WITH_EDITOR
	/** The base re-applies style; the label list lives outside ApplyStyle and regenerates here. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Destroy the strip and grow it again from TabLabels and the page count. Ends in ApplyStyle. */
	void RebuildTabs();

	/** Move everything that was already a child of this control into the switcher. */
	void AdoptAuthoredPages(const TArray<TObjectPtr<UDreamWidget>>& InAuthoredChildren);

	/** Attach InPage under the switcher through whichever door its registration state allows. */
	void AttachPage(UDreamWidget* InPage);

	/** Push ActiveTabIndex into the switcher, the toggles, the label colours and the indicator. */
	void ApplyActiveTab();

	/** The indicator's rect, in absolute numbers read from the live strip. The only writer of it. */
	void ApplyIndicator(const FDreamTabViewStyle& InActive);

	/** TabLabels[i] if it says anything, else page i's node id, else the 1-based ordinal. */
	FText ResolveTabLabel(int32 InIndex) const;

	/** How many tabs the strip should show: labels or pages, whichever is more. */
	int32 GetTabCount() const;

	/** A tab was switched on or off. The group's promise makes "which one is on" the whole news. */
	void HandleTabValueChanged(bool bInIsOn);
};
