// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Interaction/DreamUITabSwitchTarget.h"
#include "DreamTabView.generated.h"

class UDreamLayoutContainerWidgetSwitcher;
class UDreamWidget;
class UUIButton;
class UUIToggle;
class UUIToggleGroup;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamTabViewChangedEvent, int32, ActiveTabIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamTabViewTabEvent, int32, TabIndex, UDreamWidget*, Tab);
/** A tab moved from one place in the strip to another. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamTabViewReorderEvent, int32, FromIndex, int32, ToIndex);

/**
 * One generated tab's parts.
 *
 * A struct rather than four parallel arrays: the four are created together, restyled together and
 * destroyed together, and four arrays that must stay the same length is a bug waiting for the first
 * early-out that returns between two of them.
 */
USTRUCT(BlueprintType)
struct DREAMGUICONTROLS_API FDreamTabViewTab
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

	/** The close button, present only while the view offers closing. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UDreamWidget> CloseNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tab View")
	TObjectPtr<UUIButton> CloseBehaviour = nullptr;
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
 *
 * THE SHOULDER BUTTONS switch tabs (IDreamUITabSwitchTarget): the project's PreviousTabKeys and
 * NextTabKeys, LB and RB by default, reach the tab view the player's focus is in -- else the first one on
 * their active screen -- and step to the previous or next enabled tab, round at the ends, as
 * CommonUI's tab list does; the action bar shows the two prompts while there is such a tab view.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Tab View")
class DREAMGUICONTROLS_API UDreamTabView : public UDreamUIControl, public IDreamUITabSwitchTarget
{
	GENERATED_BODY()

public:
	//~ IDreamUITabSwitchTarget
	/** In play, active, drawn and enabled, with an enabled tab other than the open one to go to. */
	virtual bool CanSwitchTab(int32 InUserIndex) const override;
	/**
	 * Open the enabled tab InDelta steps away -- -1 the previous, 1 the next -- round at the ends, as a click
	 * on it would: announced, and the page focused while bFocusPageOnTabChange says so. Player InUserIndex's
	 * focus goes with it when it was on the strip or in the page being left: onto the tab now open, or into
	 * its page; focus anywhere else stays where it is.
	 */
	virtual bool SwitchTab(int32 InUserIndex, int32 InDelta) override;

	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetStyle", Category = "Tab View")
	FDreamTabViewStyle Style;

	/**
	 * The tab captions, index-matched to the pages. An entry names that tab; a missing or empty one
	 * leaves the tab wearing its page's node id (see the class comment). Longer than the page list
	 * is allowed and grows the strip -- a screen that authors its tabs before its pages should see
	 * the strip it is building.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetTabLabels", Category = "Tab View")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetTabTemplateClass", Category = "Tab View")
	TSubclassOf<UDreamUserWidget> TabTemplateClass;

	/**
	 * Which tabs can be picked, index-matched to the strip -- the same parallel-array idiom TabLabels
	 * uses, and for the same reason: a struct per tab would make the common case (no opinion at all)
	 * cost an array literal the language cannot write.
	 *
	 * A missing entry means ENABLED, so a shorter list than the strip is an ordinary state and the
	 * default empty one disables nothing. A disabled tab is drawn in the style's TabDisabled colour
	 * and cannot be clicked; code may still open it through SetActiveTabIndex, which is the rule
	 * everywhere else in this library -- an interactable flag stops the PLAYER, not the program.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetTabEnabledStates", Category = "Tab View")
	TArray<bool> TabEnabled;

	/**
	 * Every tab carries a close button, and clicking one closes that tab -- the browser's arrangement.
	 *
	 * Off by default: a settings view's tabs are its structure and must not be dismissable. Closing
	 * DESTROYS the page (it is the tab's content and nothing else holds it) and drops the caption,
	 * after OnTabClosed has been broadcast with the index -- so a consumer that wants to keep the
	 * page takes it out of the switcher from that handler.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetTabsClosable", Category = "Tab View")
	bool bTabsClosable = false;

	/**
	 * Tabs can be dragged along the strip to reorder them, carrying their pages with them.
	 *
	 * Off by default, for the same reason: a strip whose order is structure must not shuffle under a
	 * stray drag. The reorder is LIVE -- the tab under the pointer swaps with the dragged one as it
	 * passes, which is what every browser does and what makes the gesture readable without a ghost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetTabsDraggable", Category = "Tab View")
	bool bTabsDraggable = false;

	/**
	 * Switching tabs moves focus into the new page.
	 *
	 * What a gamepad needs and a pointer does not care about: without it, opening a tab leaves focus
	 * on the tab itself, so the next stick press walks along the strip rather than into the page the
	 * player just opened. Only for a switch the USER made -- an authored index or a two-way binding
	 * pushing a value in must not steal focus from wherever it is.
	 *
	 * Whatever this says, focus that is inside the page being left moves onto the tab now open
	 * before that page is hidden, rather than being cleared with it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetFocusPageOnTabChange", Category = "Tab View")
	bool bFocusPageOnTabChange = false;

	/**
	 * One per tab, as the strip is built. The hook for a consumer whose tabs are richer than a word
	 * but who would rather not author a whole class: everything under the tab is reachable from here
	 * by display name. The tab view's counterpart of the list's OnRowGenerated.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewTabEvent OnTabGenerated;

	/**
	 * Fired when the open tab changes, whoever changed it -- a click, SetActiveTabIndex, or a close or a
	 * reorder that moved it. A consumer binds to this, not to a tab.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewChangedEvent OnTabChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewChangedEvent OnValueChangedBP;

	/**
	 * A tab is closing. Fired BEFORE the page is destroyed and the caption dropped, so a consumer
	 * that wants to keep the page can take it out of the switcher from here -- the same order
	 * UDreamDialog::Close broadcasts in, and for the same reason.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewChangedEvent OnTabClosed;

	/** A tab was dragged to a new place. Both indices are into the strip as it was before the move. */
	UPROPERTY(BlueprintAssignable, Category = "Tab View")
	FDreamTabViewReorderEvent OnTabReordered;

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

	/** Replace the look and push it. */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetStyle(const FDreamTabViewStyle& InStyle);

	/** The authored tab content. Instanced into each tab at build, so a new class means a rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabTemplateClass(TSubclassOf<UDreamUserWidget> InTabTemplateClass);

	/**
	 * Show or hide every tab's close button. A restyle: the buttons are woken in the style loop. Hiding
	 * them moves focus that is on one onto its tab first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabsClosable(bool bInTabsClosable);

	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabsDraggable(bool bInTabsDraggable);

	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetFocusPageOnTabChange(bool bInFocusPageOnTabChange);

	/** Every tab's enabled flag at once; a missing entry means enabled. SetTabEnabled changes one. */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabEnabledStates(const TArray<bool>& InTabEnabled);

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

	/** A missing entry means enabled -- see TabEnabled. */
	UFUNCTION(BlueprintPure, Category = "Tab View")
	bool IsTabEnabled(int32 InIndex) const;

	/**
	 * Grows TabEnabled to reach InIndex when it has to, then re-pushes the strip's colours. Disabling a
	 * tab that has focus moves it to the tab's right neighbour, else the last enabled tab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void SetTabEnabled(int32 InIndex, bool bInEnabled);

	/**
	 * Close a tab: broadcast, then drop its caption and destroy its page, then regrow the strip.
	 *
	 * The active index follows the way a browser's does -- closing the open tab opens its neighbour,
	 * closing one before it shifts the index down so the SAME page stays open. Either way OnTabChanged
	 * reports it, because the open tab or its index moved.
	 *
	 * Pad focus on the closed tab (its close button, say) or in its page goes to the tab that took its
	 * place; focus on any other tab stays on that tab, although every tab is rebuilt.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void CloseTab(int32 InIndex);

	/**
	 * Move a tab, carrying its page. Both indices are into the strip as it stands. The tab's widget
	 * moves with it rather than the strip being rebuilt, and OnTabChanged reports the open tab's new
	 * index when the move shifted it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tab View")
	void MoveTab(int32 InFromIndex, int32 InToIndex);

	virtual void ApplyStyle() override;

	/**
	 * The reorder drag. Reaches this control because a tab's UUIToggle does not implement the drag
	 * interfaces at all, so a drag that starts on one bubbles straight up here.
	 */
	virtual bool NativeOnBeginDrag(UDreamPointerEventData* EventData) override;
	virtual bool NativeOnDrag(UDreamPointerEventData* EventData) override;
	virtual bool NativeOnEndDrag(UDreamPointerEventData* EventData) override;

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
	/** Waking and sleeping is the shoulder buttons' prompts coming and going: see RefreshTabSwitchAvailability. */
	virtual void NativeOnEnable() override;
	virtual void NativeOnDisable() override;

#if WITH_EDITOR
	/** The base re-applies style; the label list lives outside ApplyStyle and regenerates here. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/**
	 * The enabled tab InDelta steps from the open one, going round at the ends and over the disabled tabs;
	 * INDEX_NONE when there is no other enabled tab to go to.
	 */
	int32 FindSwitchTarget(int32 InDelta) const;

	/**
	 * Whether the owning player's shoulder buttons may switch tabs here (CanSwitchTab), and when that answer
	 * changed, the news to the action router -- the action bar shows the two prompts only while some tab
	 * view would take the keys, and rebuilds when the router says the prompts moved. bInSleeping says the
	 * answer is no whatever the flags read: the view is going to sleep, or away.
	 */
	void RefreshTabSwitchAvailability(bool bInSleeping = false);

	/** The answer RefreshTabSwitchAvailability last gave the router. */
	bool bTabSwitchAvailable = false;

	/**
	 * The player a user change is for while bTabChangeFromUser is set by a shoulder button (SwitchTab), so
	 * the page is focused for the player who switched; INDEX_NONE for a click, which is the owner's.
	 */
	int32 TabChangeUserIndex = INDEX_NONE;

	/**
	 * One player's focus on the strip -- on a tab or on something inside one, its close button -- kept
	 * by what survives the tab being rebuilt: the tab widget itself while it lives, the page it opens,
	 * its authored caption, and its place.
	 */
	struct FTabFocusCarry
	{
		int32 UserIndex = 0;
		TWeakObjectPtr<UDreamWidget> TabNode;
		TWeakObjectPtr<UDreamWidget> Page;
		FText Caption;
		int32 TabIndex = INDEX_NONE;
	};

	/**
	 * Every player whose focus is on a tab, or on something inside one. InOnlyTabIndex keeps it to one
	 * tab (INDEX_NONE: all of them); InClosingPage also counts focus inside that page as focus on its
	 * tab -- the page a close is about to destroy.
	 */
	TArray<FTabFocusCarry> CaptureTabFocus(int32 InOnlyTabIndex = INDEX_NONE, const UDreamWidget* InClosingPage = nullptr) const;

	/**
	 * Put each carried player back on the same tab -- the same widget while it lives, else the tab with
	 * the same page, else the same caption -- through FocusForNavigation, so the pad's cursor moves too.
	 * A tab that is gone, or cannot take focus now, hands it to its right neighbour, else the last enabled
	 * tab, else the first selectable in this view; with none of them, the focus is cleared.
	 */
	void RestoreTabFocus(const TArray<FTabFocusCarry>& InCarried);

	/** Where a carried tab is now: its index in the strip, or INDEX_NONE when it is gone. */
	int32 FindCarriedTab(const FTabFocusCarry& InCarried) const;

	/**
	 * Destroy the strip and grow it again from TabLabels and the page count. Ends in ApplyStyle.
	 *
	 * Pad focus on the strip is carried across (CaptureTabFocus, RestoreTabFocus) -- taken here unless
	 * InCarriedFocus says the caller took it already, as a close does before it destroys the page.
	 */
	void RebuildTabs(const TArray<FTabFocusCarry>* InCarriedFocus = nullptr);

	/**
	 * Open the tab at InSanitizedIndex without announcing it: what both index setters share. Focus inside
	 * the page being left goes to the tab now open before the switcher hides that page.
	 */
	void SwitchActiveTab(int32 InSanitizedIndex);

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

	/**
	 * InIndex brought into range: never below zero, and never past the last tab ONCE THERE IS ONE.
	 *
	 * The upper half is conditional on purpose. This property is documented as the REQUEST, because
	 * an index is routinely authored before the pages attach and the switcher resolves it at layout
	 * time -- clamping against an empty strip would turn every such author into a zero. A strip that
	 * exists, though, is a bound that exists, and an index past it used to live in the property for
	 * good while every reader quietly clamped it for its own pass.
	 */
	int32 SanitizeTabIndex(int32 InIndex) const;

	/** How many tabs the strip should show: labels or pages, whichever is more. */
	int32 GetTabCount() const;

	/** A tab was switched on or off. The group's promise makes "which one is on" the whole news. */
	void HandleTabValueChanged(bool bInIsOn);

	/** A close button was clicked; the payload is the tab it closes, whose index is looked up now. */
	void HandleCloseClicked(TWeakObjectPtr<UDreamWidget> InTab);

	/**
	 * Fire OnTabChanged and OnValueChangedBP for a close or a reorder that moved the open tab: when the
	 * index differs from InIndexBefore, or bInOpenTabReplaced says a different page is open now.
	 */
	void BroadcastActiveTabMoved(int32 InIndexBefore, bool bInOpenTabReplaced);

	/**
	 * Put focus on the first navigable thing inside the open page, for the player whose shoulder button
	 * switched the tab (TabChangeUserIndex), else the owning player, through FocusForNavigation so the pad's
	 * cursor goes with it. See bFocusPageOnTabChange. The switcher has to have shown the page by then: its
	 * selectables are not found while it is still collapsed.
	 */
	void FocusActivePage();

	/** Which tab the pointer is over right now, or INDEX_NONE. The reorder drag's whole hit test. */
	int32 TabIndexAtPointer(const UDreamPointerEventData* InEventData) const;

	/** The tab a reorder drag picked up, or INDEX_NONE while none is being dragged. */
	UPROPERTY(Transient)
	int32 DraggingTabIndex = INDEX_NONE;

	/**
	 * True while a tab change came from the USER rather than from a push.
	 *
	 * The only thing that separates "the player opened this tab" from "a binding wrote the index",
	 * and focus may only follow the first -- a two-way binding echoing a value back must not take
	 * focus away from whatever the player is actually on.
	 */
	UPROPERTY(Transient)
	bool bTabChangeFromUser = false;
};
