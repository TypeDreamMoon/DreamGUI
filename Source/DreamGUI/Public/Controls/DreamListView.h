// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIScrollView.h"
#include "DreamListView.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamListSelectionChangedEvent, int32, SelectedIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamListRowEvent, int32, ItemIndex, UDreamWidget*, Row, UObject*, Item);
/** An item something happened TO, with its object when the source has one. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamListItemEvent, int32, ItemIndex, UObject*, Item);
/** An item the pointer entered or left. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamListItemHoverEvent, int32, ItemIndex, UObject*, Item, bool, bIsHovered);
/**
 * The list moved: where to, and how much of it is on screen.
 *
 * Both numbers, because a consumer drawing its own position indicator needs both and can work out
 * neither -- the row pitch and the realized window are the control's business, not theirs.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamListScrolledEvent, float, CurrentOffset, float, ViewFraction);
/**
 * Whether one item may be chosen or navigated to at all.
 *
 * Single-cast, like the tree's children provider and for the same reason: this is a QUESTION, and
 * two answers to a question is an ambiguity nothing can resolve. Unbound means yes.
 */
DECLARE_DYNAMIC_DELEGATE_RetVal_TwoParams(bool, FDreamListItemSelectableQuery, int32, ItemIndex, UObject*, Item);
/** A batch of rows was just rebuilt: how many the control now has realized. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamListEntriesGeneratedEvent, int32, RealizedRowCount);

/**
 * Where a drop landed relative to the row under the pointer -- UMG's EItemDropZone.
 *
 * Three answers, not two, because "between these rows" and "onto this row" are different edits: a
 * re-order wants the seam, an assignment wants the row. The names are UMG's and are written for a
 * vertical list; a horizontal one reads AboveItem as "before it" and BelowItem as "after it",
 * because the question was always about SOURCE ORDER rather than about up and down.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamItemDropZone : uint8
{
	/** The seam before the row: a re-order that puts the dragged item ahead of it. */
	AboveItem,
	/** The row itself: an assignment onto whatever that row stands for. */
	OntoItem,
	/** The seam after the row. */
	BelowItem,
};

class UDreamListViewBase;

/**
 * The drag source a list puts on its rows.
 *
 * A subclass of the library's own UDreamUIDragSource rather than a second detection path: the base
 * already owns the press-and-move test, the operation hand-off and the end-of-drag notification,
 * and a list that re-implemented those would be a second set of rules for the same gesture. All
 * this adds is WHOSE row it is -- the back-pointer and the pool slot -- so the payload can be the
 * item rather than the widget.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable, DisplayName = "Dream List Row Drag Source")
class DREAMGUI_API UDreamListRowDragSource : public UDreamUIDragSource
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDreamListViewBase> OwningList = nullptr;

	/** The POOL slot, not the item: a recycled row stands for a different item every few scrolls. */
	UPROPERTY(Transient)
	int32 PoolIndex = INDEX_NONE;

	virtual UDreamDragDropOperation* CreateDragOperation_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;
};

/**
 * The drop target a list puts on its rows.
 *
 * Same relationship as the source above. What it adds is the ZONE: the base answers "may this drop
 * land here", and a list also has to say WHERE on the row it landed, which is a question about the
 * row's rect and the pointer -- neither of which the base has any reason to look at.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable, DisplayName = "Dream List Row Drop Target")
class DREAMGUI_API UDreamListRowDropTarget : public UDreamUIDropTarget
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDreamListViewBase> OwningList = nullptr;

	UPROPERTY(Transient)
	int32 PoolIndex = INDEX_NONE;

	/** Where the last drop or hover landed on this row. Read by the list when it broadcasts. */
	EDreamItemDropZone LastDropZone = EDreamItemDropZone::OntoItem;

	/**
	 * Bound to this target's OWN enter and leave, so the row never has to be searched for.
	 *
	 * The base's delegates carry only the operation, and the operation cannot say which row fired.
	 * A search would also have had to guess on leave, where the hovered flag is already down by the
	 * time anyone is told -- whereas the target has known its own pool slot since it was made.
	 */
	UFUNCTION()
	void HandleDragEnter(UDreamDragDropOperation* InOperation);

	UFUNCTION()
	void HandleDragLeave(UDreamDragDropOperation* InOperation);

	virtual bool CanAcceptDrop_Implementation(UDreamDragDropOperation* Operation) override;
	virtual bool OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData) override;
};

/** A drag that started on a row: which item, and the operation carrying it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamListItemDragEvent, int32, ItemIndex, UObject*, Item, UDreamDragDropOperation*, Operation);
/** A drop that landed on a row, with the zone it landed in. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FDreamListItemDropEvent, int32, ItemIndex, UObject*, Item, UDreamDragDropOperation*, Operation, EDreamItemDropZone, DropZone);
/** Whether this list is dragging one of its own rows. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamListDraggingStateEvent, bool, bIsDragging);

/**
 * Everything a list and a tree have in common, which is nearly all of it.
 *
 * A tree IS a list that indents, and this class is written to be exactly that: the viewport, the
 * bar, the scrolled column, the row template, the row POOL, the row colours and the one selected
 * index live here, and each concrete control adds only what makes it itself -- its typed style,
 * and (for the tree) a depth per item, a per-row indent and a twisty. FDreamTreeViewStyle is built
 * the same way round, carrying a whole FDreamListStyle rather than restating its fields.
 *
 * This class is abstract and holds NO style property. That is deliberate, and it is the reason
 * there is a base class at all rather than UDreamTreeView deriving from UDreamListView: the
 * family's contract is that a control's look is ONE decision (see UDreamUIControl), and a tree
 * inheriting a spare FDreamListStyle it never resolves would be a second source of truth wearing
 * the same name as the real one. Each subclass declares `Style`, typed, and answers
 * ResolveListStyle() with the list half of it.
 *
 * THE SHAPE
 * ---------
 * A face carrying the look, a viewport clipped inside it holding the scroll behaviour, the column
 * that slides within the viewport, and a bar along the viewport's edge -- UDreamScrollBox's
 * arrangement, and for its reasons. Two of them are worth restating because they are not obvious:
 * the behaviour sits on the VIEWPORT and the bar is the viewport's SIBLING, because a scroll view
 * accepts drags from anywhere inside its own widget, so a bar hung underneath it would scroll the
 * content every time the handle was grabbed; and the column's height is the control's statement of
 * how far there is to scroll, never layout output.
 *
 * ROWS ARE PLACED, NOT ARRANGED
 * -----------------------------
 * The column holds NO layout container and every row states its own rect: top-anchored, stretched
 * across, and offset by its index times the row pitch. Three things follow, and all three matter:
 *
 *  - a row's height is the style's RowHeight, full stop. The previous shape put the rows in a
 *    vertical box on equal-weight Fill slots and then authored the COLUMN's height to exactly
 *    rows*RowHeight + gaps so the box would divide it back out -- an equation solved in two places
 *    that had to agree, and which stopped agreeing the moment anything measured a row's content;
 *  - placing row N costs nothing and depends on no sibling, which is what makes recycling possible
 *    at all: a box arranges every child it has, so a box can never show a window onto a million;
 *  - and the answer is available with no layout pass at all, which is what a headless test and the
 *    designer's first frame actually see.
 *
 * VIRTUALIZATION IS A THRESHOLD, NOT A MODE
 * -----------------------------------------
 * Under VirtualizationThreshold items there is one row widget per item and GetRowWidget answers for
 * every index -- the contract this control has always had, and the one that makes it correct in a
 * headless test where no viewport has been arranged. At or above it, the pool is sized to the
 * window plus an overscan and rows are re-bound as the view moves, which is what UMG's ListView
 * does and the only way a hundred thousand rows is anything but a hang. GetRowWidget then answers
 * for realized rows and null for the rest, which is UMG's contract too.
 *
 * WHY THE PLAIN SCROLL VIEW, NOT UUIListView
 * ------------------------------------------
 * The recycling stack (UUIRecyclableScrollView, and UUIListView on top of it) is the right answer
 * for a data source and the wrong one for a control, for reasons that are all in those files:
 *
 *  - its unit is a UObject* item, because IUIRecyclableScrollViewDataSource is a UObject protocol.
 *    A Native.List's natural item is a line of text -- the same call FDreamDropdownStyle's options
 *    make -- and feeding the recycler would mean minting a UObject per label;
 *  - a cell only exists after Start(), which needs a live world, a registered tree AND an arranged
 *    viewport: InitializeOnDataSource sizes the cell pool from the content parent's local-space
 *    extents. A control has to be correct the instant its properties are set;
 *  - and UUIListView::Awake hands its data-source seat to whoever claimed it first, which is how an
 *    `each` block's adapter gets in. A control hosting one would be a third party to that seat.
 *
 * So the rows are built here, from a template, into a column inside a plain UUIScrollView -- which
 * is what UDreamDropdown's option list does, because it is the same problem.
 *
 * HOW THIS RELATES TO `each`
 * --------------------------
 * They are the two halves of the same need and they do not compete:
 *
 *   `each Track in Tracks { ... }` is the LANGUAGE route. It compiles a repeated subtree into the
 *   generated class, feeds a host recyclable view through UDreamUIEachAdapter, and binds each row's
 *   fields to the item's members (`Text <- Track.Title`). You get arbitrary row shape and per-field
 *   bindings, and you pay for it with a .dui class to live in and a row you have to draw yourself.
 *
 *   `Native.List` is the CONTROL route. One tag, two properties, and you have a list that looks
 *   like every other list in the project because its rows come from the style sheet.
 *
 * Reach for `each` when the ROW is the interesting part. Reach for Native.List when the LIST is.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamListViewBase : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * The rows, as text. The common case, and the control's job is to be the common case -- the same
	 * call UDreamDropdown's Options make. Parallel to ItemObjects when both are given: the object
	 * carries the data, this carries what the built-in row says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetItems", BlueprintSetter = "SetItems", Category = "List")
	TArray<FText> Items;

	/**
	 * The rows, as objects. Non-empty, this decides the row count and each row's item; the label
	 * then comes from the matching entry of Items, or from the object's name when there is none.
	 * This is the seat UUIListView's own Items array occupies, kept so a consumer that already
	 * models its rows as UObjects does not have to unpack them into text first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	TArray<TObjectPtr<UObject>> ItemObjects;

	/**
	 * A row's CONTENT, authored elsewhere: one instance of this class is created inside every row
	 * widget, filling it, and the built-in label steps aside. The row's face, height, hover and
	 * selection stay the control's, so a template only has to draw an item.
	 *
	 * Created once per POOL row rather than per item, which is what makes it survive recycling:
	 * OnRowGenerated fires on every bind, and that is where a consumer updates it.
	 *
	 * Null (the default) is the built-in label row. Instancing a user widget needs a world, so with
	 * none this quietly stays the built-in row rather than producing half a list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetRowTemplateClass", BlueprintSetter = "SetRowTemplateClass", Category = "List")
	TSubclassOf<UDreamUserWidget> RowTemplateClass;

	/**
	 * The selected row, as an index into the SOURCE -- not into the rows on screen. For a list the
	 * two are the same; for a tree they are not, and an index that survives a collapse is the one
	 * worth handing to a binding. -1 is none.
	 *
	 * With SelectionMode at Multi this is the ANCHOR -- the row the last selection landed on -- and
	 * SelectedIndices is the whole answer. The two are kept in step: writing this one (a .dui line, a
	 * details-panel edit, a `<->` binding) is read as "that row is selected", and a set that empties
	 * puts this back to -1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectedIndex", BlueprintSetter = "SetSelectedIndex", Category = "List")
	int32 SelectedIndex = INDEX_NONE;

	/**
	 * How many rows can be selected at once, in UUIListView's four modes -- the same enum, because
	 * they are the same four answers and a second one spelling them again is a second thing to keep
	 * true. None ignores clicks; Single always leaves exactly one chosen; SingleToggle lets a second
	 * click on the chosen row clear it; Multi accumulates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectionMode", BlueprintSetter = "SetSelectionMode", Category = "List")
	EUIListSelectionMode SelectionMode = EUIListSelectionMode::Single;

	/**
	 * Every selected row, as source indices, in the order they were chosen. Authorable, but the
	 * ordinary road is SetItemSelection / SetSelectedIndex -- those keep SelectedIndex in step and
	 * repaint.
	 *
	 * BlueprintReadWrite through a setter rather than read-only: a raw write used to be picked up
	 * only on the next rebuild, which left the anchor pointing at a row that was no longer selected
	 * and the rows painted for a selection that had moved. SetSelectedIndices settles both.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectedIndices", BlueprintSetter = "SetSelectedIndices", Category = "List")
	TArray<int32> SelectedIndices;

	/**
	 * Every other row wears FDreamListStyle::RowAlternate. Off is the dense list UMG draws.
	 *
	 * BlueprintSetter, like every writable knob on this control: nothing in this family re-derives a
	 * control from a property that moved -- the SynchronizeProperties tax UDreamUIControl documents --
	 * so a runtime write straight onto the variable used to change the number and leave the list
	 * exactly as it was, with nothing anywhere saying why.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAlternatingRowColors", BlueprintSetter = "SetAlternatingRowColors", Category = "List")
	bool bAlternatingRowColors = false;

	/** Off means no bar at all, and the viewport keeps the gutter it would have cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShowScrollBar", BlueprintSetter = "SetShowScrollBar", Category = "List")
	bool bShowScrollBar = true;

	/** Whether the bar stays put or disappears while every row already fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollBarVisibility", BlueprintSetter = "SetScrollBarVisibility", Category = "List", meta = (EditCondition = "bShowScrollBar"))
	EDreamScrollBoxScrollbarVisibility ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/**
	 * Above this many rows the list recycles a window of widgets instead of building one per item.
	 *
	 * A threshold rather than a switch because the two behaviours are each right somewhere: below
	 * it every index has a widget, which is what a headless test, a designer preview and a consumer
	 * asking "give me the widget for item 7" all want; above it a list of a hundred thousand is a
	 * pool of thirty. Zero recycles always; a very large number never does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVirtualizationThreshold", BlueprintSetter = "SetVirtualizationThreshold", Category = "List", meta = (ClampMin = "0"))
	int32 VirtualizationThreshold = 200;

	/** Extra rows kept realized past each edge of the window, so a fast scroll never shows a gap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVirtualizationOverscan", BlueprintSetter = "SetVirtualizationOverscan", Category = "List", meta = (ClampMin = "0", ClampMax = "16"))
	int32 VirtualizationOverscan = 2;

	/**
	 * How many ROWS a wheel notch travels. One is the only sensitivity a list can state without
	 * guessing -- it is the unit the content is made of -- and this is the multiplier on it, for a
	 * long list where a row at a time is too slow. Zero stops the wheel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWheelScrollMultiplier", BlueprintSetter = "SetWheelScrollMultiplier", Category = "List", meta = (ClampMin = "0.0"))
	float WheelScrollMultiplier = 1.0f;

	/**
	 * Which way the rows run -- UMG's Orientation, and the axis this whole control is written in.
	 *
	 * Vertical is a column of rows that scrolls up and down; Horizontal is a band of rows that
	 * scrolls left and right. Every other measurement follows it: RowHeight becomes the row's extent
	 * ALONG the scroll axis, RowSpacing the gap between two of them, and the row stretches across the
	 * other axis instead of down it. The scroll bar moves to the bottom edge, and the wheel, the
	 * reveal and the first/last calls all change axis with it.
	 *
	 * Vertical is the default, which is what every existing list already is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetOrientation", BlueprintSetter = "SetOrientation", Category = "List")
	EDreamPanelOrientation Orientation = EDreamPanelOrientation::Vertical;

	/**
	 * Whether the wheel is swallowed here or handed to an outer scrolling ancestor at a limit -- the
	 * same three answers, and the same enum, a scroll box gives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetConsumeMouseWheel", BlueprintSetter = "SetConsumeMouseWheel", Category = "List")
	EDreamScrollBoxConsumeMouseWheel ConsumeMouseWheel = EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible;

	/** Let the rows be pulled past an end and spring back -- UMG's AllowOverscroll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowOverscroll", BlueprintSetter = "SetAllowOverscroll", Category = "List")
	bool bAllowOverscroll = true;

	/** Whether a TOUCH drag scrolls the rows -- UMG's bEnableTouchScrolling. A mouse drag is unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableTouchScrolling", BlueprintSetter = "SetEnableTouchScrolling", Category = "List")
	bool bEnableTouchScrolling = true;

	/** Whether a drag with the RIGHT button scrolls the rows -- UMG's bEnableRightClickScrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableRightClickScrolling", BlueprintSetter = "SetEnableRightClickScrolling", Category = "List")
	bool bEnableRightClickScrolling = true;

	/**
	 * Where a row revealed by ScrollIndexIntoView or by navigation ends up -- UMG calls this
	 * ScrollIntoViewAlignment and spells it as a float; this is the four-answer enum the scroll box
	 * and the scroll behaviour already share, because they are the same question.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollIntoViewDestination", BlueprintSetter = "SetScrollIntoViewDestination", Category = "List", meta = (InvalidEnumValues = "Configured"))
	EDreamUIScrollDestination ScrollIntoViewDestination = EDreamUIScrollDestination::IntoView;

	/**
	 * Pin a revealed row at a fixed share of the window instead of merely bringing it inside --
	 * UMG's bEnableFixedLineOffset.
	 *
	 * What a cursor-driven menu wants: the highlighted row stays put and the list moves underneath
	 * it, rather than the row sliding to whichever edge it came in from. Off by default, which is
	 * the reveal every existing list does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableFixedLineOffset", BlueprintSetter = "SetEnableFixedLineOffset", Category = "List")
	bool bEnableFixedLineOffset = false;

	/** Where in the window that row is pinned, 0 = the near edge, 1 = the far one. Half is the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetFixedLineScrollOffset", BlueprintSetter = "SetFixedLineScrollOffset", Category = "List", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableFixedLineOffset"))
	float FixedLineScrollOffset = 0.5f;

	/**
	 * Whether landing on a row by navigation also SELECTS it -- UMG's bSelectItemOnNavigation.
	 *
	 * True, which is what NavigateToIndex has always done. Off, navigation only reveals, and the
	 * selection stays wherever the player last put it -- a shopping list you scroll through without
	 * losing the line you were on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectItemOnNavigation", BlueprintSetter = "SetSelectItemOnNavigation", Category = "List")
	bool bSelectItemOnNavigation = true;

	/**
	 * Whether a selection made before its item arrived is REMEMBERED -- UMG's
	 * bAllowKeepPreselectedItems.
	 *
	 * ReconcileSelection drops indices the source no longer answers to, which is right for a source
	 * that shrank and wrong for one that has not been filled yet: a screen that restores "row 7 was
	 * selected" before its data loads would otherwise silently lose it. With this on, a dropped index
	 * is parked and re-applied the moment the source is long enough to hold it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowKeepPreselectedItems", BlueprintSetter = "SetAllowKeepPreselectedItems", Category = "List")
	bool bAllowKeepPreselectedItems = false;

	/**
	 * Whether choosing a row also stops a fling -- UMG's bClearScrollVelocityOnSelection.
	 *
	 * Off, which is what this control has always done. On, a click or a navigation press kills the
	 * momentum, so the row the player picked does not slide out from under the cursor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetClearScrollVelocityOnSelection", BlueprintSetter = "SetClearScrollVelocityOnSelection", Category = "List")
	bool bClearScrollVelocityOnSelection = false;

	/**
	 * Whether the reveal and the wheel GLIDE rather than jump -- UMG's bEnableScrollAnimation.
	 *
	 * Off by default: every existing list's reveal lands in one frame, and a project that has tuned
	 * its own timing around that should keep it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableScrollAnimation", BlueprintSetter = "SetEnableScrollAnimation", Category = "List")
	bool bEnableScrollAnimation = false;

	/**
	 * How fast that glide closes the distance -- UMG's ScrollingAnimationInterpolationSpeed, and its
	 * parameterisation: a SPEED, where the scroll behaviour's own tween takes a duration.
	 *
	 * Turned into a duration at the push, because the two describe the same ease and the behaviour
	 * only knows one of them: a higher speed is a shorter glide.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetScrollingAnimationInterpolationSpeed", BlueprintSetter = "SetScrollingAnimationInterpolationSpeed", Category = "List", meta = (ClampMin = "0.01", EditCondition = "bEnableScrollAnimation"))
	float ScrollingAnimationInterpolationSpeed = 6.667f;

	/** Whether a TOUCH drag glides too -- UMG's bInEnableTouchAnimatedScrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableTouchAnimatedScrolling", BlueprintSetter = "SetEnableTouchAnimatedScrolling", Category = "List", meta = (EditCondition = "bEnableScrollAnimation"))
	bool bEnableTouchAnimatedScrolling = false;

	/**
	 * Whether a POINTER can scroll this list at all -- UMG's bIsPointerScrollingEnabled.
	 *
	 * The wheel and the mouse drag together, because they are one gesture set: a list that answers
	 * the wheel but not the drag is a list whose scroll bar is the only way down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsPointerScrollingEnabled", BlueprintSetter = "SetIsPointerScrollingEnabled", Category = "List")
	bool bIsPointerScrollingEnabled = true;

	/** Whether the analog stick can scroll it -- UMG's bIsGamepadScrollingEnabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsGamepadScrollingEnabled", BlueprintSetter = "SetIsGamepadScrollingEnabled", Category = "List")
	bool bIsGamepadScrollingEnabled = true;

	/**
	 * Whether focus arriving at the LIST is handed on to the selected row -- UMG's
	 * bReturnFocusToSelection.
	 *
	 * What a player coming back from a submenu expects: focus lands where they left it rather than
	 * at the top. Off by default, because a list whose focus jumped somewhere else on arrival would
	 * change where an existing screen's next directional press goes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetReturnFocusToSelection", BlueprintSetter = "SetReturnFocusToSelection", Category = "List")
	bool bReturnFocusToSelection = false;

	/**
	 * Draw a fade at each end of the window while there is more list out there -- UMG's
	 * bEnableShadowBrush.
	 *
	 * Two overlays inside the viewport, each awake only while the list is scrolled away from that
	 * end, so the fade says "there is more this way" rather than being permanent decoration. Off by
	 * default; nothing existing gains an overlay it did not have.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableShadowBrush", BlueprintSetter = "SetEnableShadowBrush", Category = "List")
	bool bEnableShadowBrush = false;

	/**
	 * What that fade is drawn with -- UMG's ShadowBrushStyle.
	 *
	 * On the CONTROL rather than in FDreamListStyle: the style struct is the list's shared look and
	 * is resolved against the project sheet, where a fade is a per-screen decision about one list's
	 * edges. Keeping it here also means a project sheet cannot switch the overlay on for every list
	 * at once, which is not an edit anyone would make on purpose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetShadowBrush", Category = "List", meta = (EditCondition = "bEnableShadowBrush"))
	FDreamUIFaceBrush ShadowBrush;

	/** How deep the fade reaches into the window, in local units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShadowBrushThickness", BlueprintSetter = "SetShadowBrushThickness", Category = "List", meta = (ClampMin = "0.0", EditCondition = "bEnableShadowBrush"))
	float ShadowBrushThickness = 12.0f;

	/**
	 * Whether a row can be picked UP -- UMG's bAllowDragging.
	 *
	 * Off, and off is literal: with this false no row carries a drag behaviour at all, so an existing
	 * list gains nothing to subscribe to, nothing to tick and nothing that could change what a press
	 * on a row means. Turning it on adds the behaviour to every pool row, including ones already
	 * made.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowDragging", BlueprintSetter = "SetAllowDragging", Category = "List|Drag")
	bool bAllowDragging = false;

	/**
	 * Whether a row can be dropped ON -- UMG's bAllowDragDrop.
	 *
	 * Separate from bAllowDragging on purpose, and both directions are useful: a palette a player
	 * drags OUT of but never into wants dragging alone, and an equipment list that accepts items
	 * from elsewhere wants dropping alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowDragDrop", BlueprintSetter = "SetAllowDragDrop", Category = "List|Drag")
	bool bAllowDragDrop = false;

	/**
	 * Where the drag visual sits relative to the pointer, as a fraction of its own size -- UMG's
	 * DragDropVisualPivot. (0,0) hangs it below-right of the cursor, (0.5,0.5) centres it on it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetDragDropVisualPivot", Category = "List|Drag", meta = (EditCondition = "bAllowDragging"))
	FVector2D DragDropVisualPivot = FVector2D(0.5, 0.5);

	/** A further nudge in local units, after the pivot -- UMG's DragDropVisualOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetDragDropVisualOffset", Category = "List|Drag", meta = (EditCondition = "bAllowDragging"))
	FVector2D DragDropVisualOffset = FVector2D::ZeroVector;

	/**
	 * What to show under the cursor while dragging -- UMG's DragDropVisualEntryClass.
	 *
	 * Null falls back to RowTemplateClass, which is the answer that needs no authoring: the thing
	 * under the cursor should look like the row it came from, and the list already knows how to make
	 * one of those.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetDragDropVisualEntryClass", Category = "List|Drag", meta = (EditCondition = "bAllowDragging"))
	TSubclassOf<UDreamUserWidget> DragDropVisualEntryClass;

	/**
	 * The operation class a row's drag builds -- UMG's DragDropOperationClass.
	 *
	 * UDreamDragDropOperation by default, which already carries a payload, a tag and a visual class.
	 * A project with its own subclass names it here and gets it on every row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetDragDropOperationClass", Category = "List|Drag", meta = (EditCondition = "bAllowDragging"))
	TSubclassOf<UDreamDragDropOperation> DragDropOperationClass;

	/** The tag copied onto every operation this list creates, for drop targets to filter on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = "SetDragOperationTag", Category = "List|Drag", meta = (EditCondition = "bAllowDragging"))
	FName DragOperationTag;


	/** Re-broadcast from the rows, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListSelectionChangedEvent OnSelectionChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact name,
	 * so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListSelectionChangedEvent OnValueChangedBP;

	/**
	 * One per row, every time it is BOUND to an item -- which, while recycling, is every time that
	 * row comes back round to a new item rather than once in its life. The hook for a consumer whose
	 * rows are richer than a label but who would rather not author a whole class: everything under
	 * the row is reachable from here by display name.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListRowEvent OnRowGenerated;

	/**
	 * The counterpart of OnRowGenerated: a pool row is about to stop standing for this item, either
	 * because it was re-bound to another one or because it was parked. UMG's OnEntryReleased, and
	 * the hook for undoing whatever OnRowGenerated did to that row.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListRowEvent OnRowReleased;

	/**
	 * A row was clicked -- every click, whatever the selection mode did with it. Separate from
	 * OnSelectionChanged because a click on the already-selected row is still a click, and because a
	 * list whose SelectionMode is None still wants to know.
	 *
	 * Fires AFTER the selection has moved, so a handler asking GetSelectedIndex sees the answer the
	 * user just gave.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListItemEvent OnItemClicked;

	/**
	 * Two clicks on the same row inside the event system's DoubleClickTime.
	 *
	 * That clock and no other: the pointer input module already decides what a double click is (it is
	 * what UUITextInput's select-the-word gesture uses), and a list measuring its own would disagree
	 * with the field beside it -- and would be counting click EVENTS rather than pointer presses.
	 * The single click still fires first, so a row that opens on a double click also selects on the
	 * way there.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListItemEvent OnItemDoubleClicked;

	/**
	 * The pointer entered or left a row -- UMG's OnItemIsHoveredChanged.
	 *
	 * The item index and its object, with the hovered flag; a row that is re-bound while the pointer
	 * sits over it announces the new item, because what is hovered is the ITEM and not the widget.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListItemHoverEvent OnItemIsHoveredChanged;

	/**
	 * The list moved -- UMG's OnListViewScrolled. Carries the offset and the visible fraction, which
	 * is what a consumer drawing its own position indicator needs and cannot work out alone.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListScrolledEvent OnListViewScrolled;

	/**
	 * The list stopped moving -- UMG's OnListViewFinishedScrolling.
	 *
	 * Fired the first time a move lands with no momentum left, so a consumer loading thumbnails for
	 * what is now on screen gets one call per gesture rather than one per frame of a flick.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListScrolledEvent OnListViewFinishedScrolling;

	/**
	 * A row widget was just created -- UMG's OnEntryInitialized, and ONCE in that widget's life.
	 *
	 * Distinct from OnRowGenerated, which fires on every bind: this is where something permanent
	 * belongs, and subscribing to it from the generated hook instead is how a recycled row ends up
	 * carrying one subscription per item it has ever shown.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListRowEvent OnRowInitialized;

	/**
	 * A whole rebuild finished -- UMG's BP_OnEntriesGenerated.
	 *
	 * Broadcast before RebuildRows returns, which IS the batch boundary here: this control rebuilds
	 * synchronously, so there is no pending request for a later frame to complete.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListEntriesGeneratedEvent OnRowsGenerated;

	/**
	 * An item came into the realized window -- UMG's BP_OnItemScrolledIntoView.
	 *
	 * The EDGE, not the state: only items that were not realized a moment ago are announced, so a
	 * consumer loading a thumbnail per row is called once per arrival rather than once per scroll.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListRowEvent OnItemScrolledIntoView;

	/** A touch drag began on the rows -- UMG's BP_OnListViewTouchStart. Offset and visible fraction. */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListScrolledEvent OnListViewTouchStart;

	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListScrolledEvent OnListViewTouchMove;

	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListScrolledEvent OnListViewTouchEnd;

	/**
	 * Whether one item may be chosen or navigated to -- UMG's BP_OnIsItemSelectableOrNavigable.
	 *
	 * Asked on BOTH roads, which is the point: a row that cannot be clicked but can be navigated to
	 * is a dead end the player can reach. Deselecting is never vetoed, or a row that became
	 * unselectable while chosen could never stop being chosen.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "List")
	FDreamListItemSelectableQuery OnIsItemSelectableOrNavigable;

	/** A row was picked up -- UMG's BP_OnItemDragDetected. The operation is still editable here. */
	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListItemDragEvent OnItemDragDetected;

	/** A drag entered a row -- UMG's BP_OnItemDragEnter. Any drag, not only this list's own. */
	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListItemDragEvent OnItemDragEnter;

	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListItemDragEvent OnItemDragLeave;

	/**
	 * A drop landed on a row -- UMG's BP_OnItemAcceptDrop, with the zone it landed in.
	 *
	 * Fired only while bAllowDragDrop is on. The list does NOT re-order itself: what a drop means is
	 * the consumer's to decide, and a list that moved its own source would be guessing at an edit
	 * only the data's owner can make.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListItemDropEvent OnItemAcceptDrop;

	/** A drag from this list ended with nobody accepting -- UMG's BP_OnItemDragCancelled. */
	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListItemDragEvent OnItemDragCancelled;

	/** This list started or stopped dragging one of its rows -- UMG's BP_OnListViewDraggingStateChanged. */
	UPROPERTY(BlueprintAssignable, Category = "List|Drag")
	FDreamListDraggingStateEvent OnDraggingStateChanged;

	/** The face: the list's own look, and what cuts everything off at its rounded edge. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The window the rows slide behind, and where the scroll behaviour lives. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> ViewportNode = nullptr;

	/** The scrolled content: as tall as ALL rows while the viewport shows only what fits. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> ColumnNode = nullptr;

	/**
	 * The thing rows are copied from -- authored once, inactive, never drawn itself. Public because
	 * it is half of the "hand it a template" story: anything added under this node before the first
	 * rebuild rides into every row.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> RowTemplateNode = nullptr;

	/** The template's label. Every row has a copy of it, found by display name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> RowLabelNode = nullptr;

	/** A real Native.ScrollBar, so the handle geometry the anchor rule dictates exists once. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamScrollBar> ScrollBarNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UUIScrollView> ScrollBehaviour = nullptr;

	/**
	 * The row widgets that exist, in pool order -- which is visual order only while the list is not
	 * recycling. RowSourceIndices says what each one is currently showing.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TArray<TObjectPtr<UDreamWidget>> RowNodes;

	/** Parallel to RowNodes: which source item each pool row stands for, or -1 while it is parked. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TArray<int32> RowSourceIndices;

	UFUNCTION(BlueprintPure, Category = "List")
	TArray<FText> GetItems() const { return Items; }

	/** Replace the text source and rebuild. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetItems(const TArray<FText>& InItems);

	UFUNCTION(BlueprintPure, Category = "List")
	TSubclassOf<UDreamUserWidget> GetRowTemplateClass() const { return RowTemplateClass; }

	/**
	 * Replace the class every row's content is made from.
	 *
	 * A teardown, not a restyle: the instance lives INSIDE each pool row and is made once per row, so
	 * a rebind can carry a new style into an existing row but never a new class.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetRowTemplateClass(TSubclassOf<UDreamUserWidget> InClass);

	/** Replace the whole selection at once. Keeps the anchor and the rows' paint in step with it. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectedIndices(const TArray<int32>& InIndices);

	/**
	 * The object source, as raw pointers -- UMG's GetListItems.
	 *
	 * A copy rather than a reference, because a UFUNCTION return is a value and because handing out
	 * the live array would let a caller resize the thing the rows are indexed into.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	TArray<UObject*> GetListItems() const;

	/** Append one object to the source and rebuild -- UMG's AddItem. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void AddItem(UObject* InItem);

	/** Append one line of text to the source and rebuild. The text source's half of AddItem. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void AddTextItem(FText InItem);

	/**
	 * Drop one object from the source and rebuild -- UMG's RemoveItem.
	 *
	 * The matching text, if there is one, goes with it: the two arrays are parallel, and a removal
	 * that shortened only one of them would re-label every row after it.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RemoveItem(UObject* InItem);

	/** Drop the item at a source index and rebuild. Out of range does nothing. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RemoveItemAt(int32 InItemIndex);

	/** Empty both sources, clear the selection and rebuild -- UMG's ClearListItems. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void ClearListItems();

	/** How many items the source holds -- UMG's name for GetItemCount. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetNumItems() const { return GetItemCount(); }

	/** The object at a source index, or null for a text-only source or an index out of range. */
	UFUNCTION(BlueprintPure, Category = "List")
	UObject* GetItemAt(int32 InItemIndex) const;

	/** Where an object sits in the source, or -1 when it is not in it -- UMG's GetIndexForItem. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetIndexForItem(UObject* InItem) const;

	/** How many rows are selected -- UMG's GetNumItemsSelected. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetNumItemsSelected() const { return SelectedIndices.Num(); }

	/** True when an item currently has a row widget standing for it -- UMG's IsItemVisible. */
	UFUNCTION(BlueprintPure, Category = "List")
	bool IsItemVisible(int32 InItemIndex) const { return GetRowWidget(InItemIndex) != nullptr; }

	/** Whether the veto lets this item be chosen or navigated to. True when nothing is bound. */
	UFUNCTION(BlueprintPure, Category = "List")
	bool IsItemSelectableOrNavigable(int32 InItemIndex) const;

	/**
	 * Insert one object at a source index and rebuild -- UMG's AddItemAt.
	 *
	 * The parallel text goes in at the SAME place, because the two arrays describe the same rows:
	 * inserting into one alone re-labels every row after the seam.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void AddItemAt(UObject* InItem, int32 InItemIndex);

	/** Append several objects and rebuild ONCE -- UMG's AddItems. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void AddItems(const TArray<UObject*>& InItems);

	/** Insert several objects at a source index and rebuild once -- UMG's AddItemsAt. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void AddItemsAt(const TArray<UObject*>& InItems, int32 InItemIndex);

	/** Drop several objects and rebuild once -- UMG's RemoveItems. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RemoveItems(const TArray<UObject*>& InItems);

	/**
	 * Select an item's row AND bring it into view -- UMG's NavigateToIndex, which is what directional
	 * navigation does when it lands on a list.
	 *
	 * The two halves in one call because doing them separately is always wrong in the same way: a
	 * selection that is off screen is a selection nobody can see they made.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void NavigateToIndex(int32 InItemIndex);

	/** Bring an item's row into view without touching the selection -- UMG's ScrollIndexIntoView. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void ScrollIndexIntoView(int32 InItemIndex);

	/** Jump to the first row -- UMG's ScrollToTop. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void ScrollToTop();

	/** Jump to the last row -- UMG's ScrollToBottom. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void ScrollToBottom();

	/** Drop the fling, keeping the position -- UMG's EndInertialScrolling. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void EndInertialScrolling();

	/** Signed distance past an end, in local units -- UMG's GetOverscroll. */
	UFUNCTION(BlueprintPure, Category = "List")
	float GetOverscroll() const;

	/** Fraction of the rows currently on screen, 0..1. One when they all fit. */
	UFUNCTION(BlueprintPure, Category = "List")
	float GetViewFraction() const;

	UFUNCTION(BlueprintPure, Category = "List")
	EDreamPanelOrientation GetOrientation() const { return Orientation; }

	/**
	 * Turn the whole control through ninety degrees.
	 *
	 * Virtual so a subclass can refuse: a tree is a column by definition, and a horizontal one is not
	 * a thing UMG has either.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	virtual void SetOrientation(EDreamPanelOrientation InOrientation);

	UFUNCTION(BlueprintPure, Category = "List")
	EDreamScrollBoxConsumeMouseWheel GetConsumeMouseWheel() const { return ConsumeMouseWheel; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel InConsume);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetAllowOverscroll() const { return bAllowOverscroll; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetAllowOverscroll(bool bInAllow);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableTouchScrolling() const { return bEnableTouchScrolling; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableTouchScrolling(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableRightClickScrolling() const { return bEnableRightClickScrolling; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableRightClickScrolling(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	EDreamUIScrollDestination GetScrollIntoViewDestination() const { return ScrollIntoViewDestination; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetScrollIntoViewDestination(EDreamUIScrollDestination InDestination);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableFixedLineOffset() const { return bEnableFixedLineOffset; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableFixedLineOffset(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	float GetFixedLineScrollOffset() const { return FixedLineScrollOffset; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetFixedLineScrollOffset(float InOffset);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetSelectItemOnNavigation() const { return bSelectItemOnNavigation; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectItemOnNavigation(bool bInSelect) { bSelectItemOnNavigation = bInSelect; }

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetAllowKeepPreselectedItems() const { return bAllowKeepPreselectedItems; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetAllowKeepPreselectedItems(bool bInAllow);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetClearScrollVelocityOnSelection() const { return bClearScrollVelocityOnSelection; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetClearScrollVelocityOnSelection(bool bInClear) { bClearScrollVelocityOnSelection = bInClear; }

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableScrollAnimation() const { return bEnableScrollAnimation; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableScrollAnimation(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	float GetScrollingAnimationInterpolationSpeed() const { return ScrollingAnimationInterpolationSpeed; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetScrollingAnimationInterpolationSpeed(float InSpeed);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableTouchAnimatedScrolling() const { return bEnableTouchAnimatedScrolling; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableTouchAnimatedScrolling(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetIsPointerScrollingEnabled() const { return bIsPointerScrollingEnabled; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetIsPointerScrollingEnabled(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetIsGamepadScrollingEnabled() const { return bIsGamepadScrollingEnabled; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetIsGamepadScrollingEnabled(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetReturnFocusToSelection() const { return bReturnFocusToSelection; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetReturnFocusToSelection(bool bInReturn) { bReturnFocusToSelection = bInReturn; }

	/**
	 * Put keyboard focus on the selected row's widget, if it has one on screen.
	 *
	 * False when there is no selection or its row is not realized -- which is not a failure, only
	 * the honest answer: a recycled list genuinely has no widget standing for an off-screen item.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	bool FocusSelectedRow();

	UFUNCTION(BlueprintPure, Category = "List|Drag")
	bool GetAllowDragging() const { return bAllowDragging; }

	/** Adds the drag behaviour to every pool row, or sleeps it. See RefreshRowDragBehaviours. */
	/** The image drawn over the viewport's edges while there is more to scroll to. Pushed at once. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetShadowBrush(const FDreamUIFaceBrush& InShadowBrush);

	// The five below are read when a drag STARTS, so setting one is all a setter has to do; a drag
	// already in flight keeps the visual and the operation it began with.
	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetDragDropVisualPivot(FVector2D InPivot);

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetDragDropVisualOffset(FVector2D InOffset);

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetDragDropVisualEntryClass(TSubclassOf<UDreamUserWidget> InEntryClass);

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetDragDropOperationClass(TSubclassOf<UDreamDragDropOperation> InOperationClass);

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetDragOperationTag(FName InTag);

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetAllowDragging(bool bInAllow);

	UFUNCTION(BlueprintPure, Category = "List|Drag")
	bool GetAllowDragDrop() const { return bAllowDragDrop; }

	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	void SetAllowDragDrop(bool bInAllow);

	/** True while a row of THIS list is being dragged -- UMG's bIsDragging / GetIsDraggingListItem. */
	UFUNCTION(BlueprintPure, Category = "List|Drag")
	bool GetIsDraggingListItem() const { return bIsDragging; }

	/** The item index the drag in flight started on, or -1 when nothing is being dragged. */
	UFUNCTION(BlueprintPure, Category = "List|Drag")
	int32 GetDraggedItemIndex() const { return DraggedItemIndex; }

	/**
	 * End a drag from this list with no drop -- UMG's CancelListViewDragDrop.
	 *
	 * Goes through the drag-drop subsystem rather than just clearing the flags: the operation, the
	 * visual and the pointer's state are the subsystem's, and a list that only forgot its own half
	 * would leave a visual on screen with nothing to end it.
	 */
	UFUNCTION(BlueprintCallable, Category = "List|Drag")
	bool CancelListViewDragDrop();

	/**
	 * Which zone a point falls in, as a fraction ALONG the row's main axis.
	 *
	 * Static and pure, because it is the one piece of this that is worth pinning exactly: the first
	 * and last EdgeFraction of the row are the seams and everything between is the row itself. A
	 * quarter each end by default, which leaves the middle half -- wide enough to hit on a phone,
	 * narrow enough that a re-order does not need precision.
	 */
	UFUNCTION(BlueprintPure, Category = "List|Drag")
	static EDreamItemDropZone ResolveDropZone(float InFractionAlongRow, float InEdgeFraction = 0.25f);

	/**
	 * The same question for a point in WORLD space, against one pool row's rect.
	 *
	 * Public because the row's drop target asks it, and it is the only place the orientation matters:
	 * a horizontal list's seams are its left and right edges, which is the main-axis abstraction
	 * doing exactly what it was introduced for.
	 */
	EDreamItemDropZone GetDropZoneForPoint(int32 InPoolIndex, const FVector& InWorldPoint) const;

	/** Told by a row's target that a drag entered, left, or dropped on it. Public for the same reason. */
	void HandleRowDragEnter(int32 InPoolIndex, UDreamDragDropOperation* InOperation);
	void HandleRowDragLeave(int32 InPoolIndex, UDreamDragDropOperation* InOperation);
	bool HandleRowDrop(int32 InPoolIndex, UDreamDragDropOperation* InOperation, EDreamItemDropZone InZone);
	/** Told by a row's source that a drag began on it, or ended. */
	void HandleRowDragDetected(int32 InPoolIndex, UDreamDragDropOperation* InOperation);
	void HandleRowDragEnded(UDreamDragDropOperation* InOperation);

	UFUNCTION(BlueprintPure, Category = "List")
	bool GetEnableShadowBrush() const { return bEnableShadowBrush; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetEnableShadowBrush(bool bInEnable);

	UFUNCTION(BlueprintPure, Category = "List")
	float GetShadowBrushThickness() const { return ShadowBrushThickness; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetShadowBrushThickness(float InThickness);

	/** Replace the object source and rebuild. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetItemObjects(const TArray<UObject*>& InItems);

	/** How many items the source holds -- objects if it has any, texts otherwise. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetItemCount() const;

	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/**
	 * Moves the highlight and fires both selection events. Out of range selects nothing.
	 *
	 * Selects exactly ONE row whatever the mode -- this is the single-selection road, and the name
	 * says so. SetItemSelection is the one that can add to a Multi selection.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectedIndex(int32 InIndex);

	/** The same move, silently: for pushing an authored value in, which is not the user choosing. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectedIndexWithoutNotify(int32 InIndex);

	/** Which mode decides how many rows can be chosen. Re-narrows the selection when it has to. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectionMode(EUIListSelectionMode InMode);

	UFUNCTION(BlueprintCallable, Category = "List")
	bool GetAlternatingRowColors() const { return bAlternatingRowColors; }

	/**
	 * The five knobs below are read only by the style push or the row build, so each setter writes
	 * the field and then makes that push itself -- which is the whole of what a BlueprintSetter buys
	 * here. Restyling never creates or destroys a row (see the list's restyle-identity test), so the
	 * first three are cheap; the two virtualization knobs change how many rows there ARE, and go
	 * through RebuildRows for that reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetAlternatingRowColors(bool bInAlternating);

	UFUNCTION(BlueprintCallable, Category = "List")
	bool GetShowScrollBar() const { return bShowScrollBar; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetShowScrollBar(bool bInShowScrollBar);

	UFUNCTION(BlueprintCallable, Category = "List")
	EDreamScrollBoxScrollbarVisibility GetScrollBarVisibility() const { return ScrollBarVisibility; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility InVisibility);

	UFUNCTION(BlueprintCallable, Category = "List")
	int32 GetVirtualizationThreshold() const { return VirtualizationThreshold; }

	/** Crossing the threshold changes whether the list pools, so this rebuilds rather than restyles. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetVirtualizationThreshold(int32 InThreshold);

	UFUNCTION(BlueprintCallable, Category = "List")
	int32 GetVirtualizationOverscan() const { return VirtualizationOverscan; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetVirtualizationOverscan(int32 InOverscan);

	UFUNCTION(BlueprintCallable, Category = "List")
	float GetWheelScrollMultiplier() const { return WheelScrollMultiplier; }

	UFUNCTION(BlueprintCallable, Category = "List")
	void SetWheelScrollMultiplier(float InMultiplier);

	UFUNCTION(BlueprintPure, Category = "List")
	EUIListSelectionMode GetSelectionMode() const { return SelectionMode; }

	UFUNCTION(BlueprintPure, Category = "List")
	bool IsItemSelected(int32 InItemIndex) const;

	/**
	 * Add or remove ONE row from the selection, which is the only call that can express a multi
	 * selection. bInClearOthers is implied by every mode except Multi.
	 *
	 * Fires both selection events whenever the selection actually moved -- including the case where
	 * THIS row's state did not change but others were cleared, which is where the behaviour-side
	 * version left rows painted as selected after they had been dropped.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetItemSelection(int32 InItemIndex, bool bInSelected, bool bInClearOthers = true);

	/** Nothing selected. Announced, unless there was nothing selected to begin with. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void ClearSelection();

	/** Every selected row, as source indices, in the order they were chosen. */
	UFUNCTION(BlueprintPure, Category = "List")
	TArray<int32> GetSelectedIndices() const { return SelectedIndices; }

	/**
	 * The selected rows as their item OBJECTS, for a source that has them -- UMG's GetSelectedItems.
	 * A text-only source has no objects to answer with, so this comes back empty there and
	 * GetSelectedIndices is the question to ask instead.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	TArray<UObject*> GetSelectedItems() const;

	/** How far down the column the viewport currently sits, in local units. */
	UFUNCTION(BlueprintPure, Category = "List")
	float GetScrollOffset() const;

	/** Put the viewport at an offset down the column, in local units. Clamped by the scroll range. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetScrollOffset(float InOffset);

	/**
	 * How many rows the list is SHOWING -- for a tree the visible count, which is the point of a
	 * tree. Not the number of widgets: see GetRealizedRowCount for that.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRowCount() const { return VisibleItemIndices.Num(); }

	/** How many row widgets exist. Equal to GetRowCount until the list starts recycling. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRealizedRowCount() const { return RowNodes.Num(); }

	/** True while the list is showing a window of widgets rather than one per item. */
	UFUNCTION(BlueprintPure, Category = "List")
	bool IsVirtualizing() const { return bVirtualizing; }

	/**
	 * The row standing for a source item, or null when that item has no row right now -- collapsed
	 * under a tree node, gone from the source, or (while recycling) scrolled out of the window.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	UDreamWidget* GetRowWidget(int32 InItemIndex) const;

	/** Which source item a POOL row is currently showing, or -1 while it is parked. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRowItemIndex(int32 InPoolIndex) const;

	/**
	 * Scroll the least distance that brings an item's row fully into the viewport.
	 *
	 * Computed from the row pitch rather than from a widget, so it answers for an item whose row is
	 * not realized -- which is the only version of this that means anything while recycling.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	bool ScrollItemIntoView(int32 InItemIndex, bool bInAnimate = true);

	/**
	 * Throw the rows away and build them again from the source. Called for you by ApplyStyle -- row
	 * geometry and row colour are both style, so there is no such thing as re-styling without it --
	 * and by every setter that moves the source.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RebuildRows();

	/** Re-resolve the gutter, the bar and the realized window after this control is resized. */
	void HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged);

	// Public, as it is on UDreamUIControl: narrowing it here only meant callers had to reach it through
	// a base pointer, and the tile and tree views re-push a style from their own setters.
	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	virtual void OnPartsReady() override;

	/**
	 * The list half of this control's style. Each concrete control resolves its own family and hands
	 * back a reference into it -- the sheet's entry or this instance's Style, both of which outlive
	 * the call. The base never sees which.
	 */
	virtual FDreamListStyle ResolveListStyle() const;

	/** Which source items get rows, in order. All of them, for a flat list. */
	virtual void CollectVisibleItemIndices(TArray<int32>& OutIndices) const;

	/**
	 * How many rows sit side by side. One, for anything that is a LIST.
	 *
	 * The tile view is the same control with this answering more than one, and everything that has to
	 * agree about it reads it from here: how tall the scrolled column is (lines, not rows), where a
	 * row's line starts, and how many widgets a window holds. A second place computing it is a second
	 * chance for the column's height and the rows' positions to stop agreeing -- which is exactly the
	 * equation-in-two-places this control's row placement was rewritten to remove.
	 */
	virtual int32 ResolveColumnCount() const { return 1; }

	/**
	 * Put a row where its DISPLAY index says it goes.
	 *
	 * A list stretches a row across the column and offsets it by its line; a tile view gives it a
	 * width and a place along the line as well. Always from the display index and never from the pool
	 * index or a sibling, which is what makes recycling possible: placing row N costs nothing and
	 * depends on nothing.
	 */
	virtual void PlaceRow(UDreamWidget& InRow, int32 InDisplayIndex, const FDreamListStyle& InStyle);

	/**
	 * The source was just replaced, and anything a subclass keys BY INDEX is now pointing at whatever
	 * landed in that slot. Re-locate it or drop it here.
	 *
	 * Runs after the new source is in place and before the rows are rebuilt. The outgoing object
	 * array comes along because that is the only thing that can turn an INDEX the subclass was
	 * holding back into the ITEM it meant -- a lookup that stops being possible the moment ItemObjects
	 * is overwritten. The base's own index-keyed state (the selection) is settled by the setters
	 * themselves, for the same reason and at the same moment.
	 */
	virtual void OnSourceChanged(const TArray<TObjectPtr<UObject>>& InPreviousItemObjects) {}

	/** A source item's depth. Always zero here; the tree is the only thing that indents. */
	virtual int32 GetItemDepth(int32 InItemIndex) const { return 0; }

	/**
	 * How far in a row's content starts, on top of the row's own padding. The base writes it into
	 * the label's slot; what it MEANS is the subclass's (the tree makes it depth times indent, plus
	 * room for the twisty).
	 */
	virtual float GetRowContentInset(int32 InItemIndex) const { return 0.0f; }

	/** Add to the row template, once, before the first row is copied from it. */
	virtual void DecorateRowTemplate(UDreamWidget& InTemplate) {}

	/**
	 * A pool row was just created. ONCE in that widget's life, which is what anything permanent
	 * belongs in -- a click handler above all: subscribing from DecorateRow instead would add one
	 * more every time the row came round to another item.
	 *
	 * A handler that needs to know which item it is acting on asks GetRowItemIndex(InPoolIndex) at
	 * the moment it fires, because the answer changes underneath it.
	 */
	virtual void DecorateNewRow(UDreamWidget& InRow, int32 InPoolIndex) {}

	/** A pool row was just bound to an item. Runs after the base has skinned, sized and placed it. */
	virtual void DecorateRow(UDreamWidget& InRow, int32 InPoolIndex, int32 InItemIndex) {}

	/** The label a row shows: the matching text, else the item object's name, else nothing. */
	FText GetItemLabel(int32 InItemIndex) const;

	/** The object a row stands for, if the source has one. */
	UObject* GetItemObject(int32 InItemIndex) const;

	/** Re-push every row's resting colour without rebuilding anything. What selection actually moves. */
	void RefreshRowColors();

	/** Between a row's edge and its content -- the row's own inset, not the viewport's Padding. */
	static FMargin GetRowPadding();

	/** Row height plus the gap under it: what one step down the column costs. */
	/**
	 * The axis helpers every placement and every scroll query goes through.
	 *
	 * One pair of names -- MAIN is the scroll axis, CROSS is the one rows stretch across -- so the
	 * arithmetic below can be written once for both orientations rather than twice with a branch in
	 * each. A vertical list's main axis is Y and its padding starts at Top; a horizontal list's main
	 * axis is X and its padding starts at Left. Nothing else in the control needs to know which.
	 */
	bool IsHorizontalList() const { return Orientation == EDreamPanelOrientation::Horizontal; }
	float GetMainPadStart(const FDreamListStyle& InStyle) const;
	float GetMainPadEnd(const FDreamListStyle& InStyle) const;
	float GetCrossPadStart(const FDreamListStyle& InStyle) const;
	float GetCrossPadEnd(const FDreamListStyle& InStyle) const;
	/** The viewport's extent along the scroll axis, and across it. Zero when nothing has arranged it. */
	float GetViewportMainExtent() const;
	float GetViewportCrossExtent() const;

	float GetRowPitch() const;

	/** Where a row sits in the column, as an offset from the column's top edge. */
	float GetRowTopOffset(int32 InDisplayIndex) const;

	/** How many LINES the visible rows make: the row count over the column count, rounded up. */
	int32 GetLineCount() const;

	/** The scrolled column's height -- the scroll range -- stated from the lines and the style. */
	void RefreshContentHeight(const FDreamListStyle& InStyle);

	/** The display positions of the items the source is showing, in order. */
	UPROPERTY(Transient)
	TArray<int32> VisibleItemIndices;

private:
	void HandleRowClicked(int32 InPoolIndex);
	void HandleRowDoubleClicked(int32 InPoolIndex);
	void HandleScrollViewMoved(FVector2D InProgress);
	/** A pool row's pointer state moved. Turns "this WIDGET is hovered" into "this ITEM is hovered". */
	void HandleRowSelectionStateChanged(int32 InPoolIndex, bool bInHovered);
	/** The behaviour's drag gesture, re-broadcast as the three touch events when it WAS a touch. */
	void HandleScrollGesture(EDreamScrollDragPhase InPhase, bool bInTouch);
	/** Focus landed inside this list. Hands it on to the selected row when the author asked for that. */
	void HandleContentFocusMoved(UDreamWidget* InFocusedWidget);

	/**
	 * Place the two edge fades and decide which of them is awake.
	 *
	 * Called from the style push AND from every move, because one of the two answers changes with
	 * the offset: the rect is layout, the wakefulness is state. Made on demand, so a list that never
	 * turns the setting on never carries the two extra nodes.
	 */
	void RefreshShadowBrushes(const FDreamListStyle& InStyle);

	/**
	 * Put the drag and drop behaviours on every pool row, or sleep them.
	 *
	 * Made on demand and slept rather than destroyed, like the shadow brushes and the scroll box's
	 * focus selectable: building and destroying behaviours marks the outliner dirty and costs the
	 * designer a full details rebuild each way. With both switches off no row has ever had one, so
	 * a list that does not use this pays nothing at all.
	 */
	void RefreshRowDragBehaviours();
	/** The same, for one row that has just been created. Called from CreatePoolRow. */
	void RefreshRowDragBehaviour(UDreamWidget& InRow, int32 InPoolIndex);

	/** Whether a row of THIS list is in flight, and which item it started on. */
	bool bIsDragging = false;
	int32 DraggedItemIndex = INDEX_NONE;

	/** The operation this list's own drag is riding, so an ending drag can be told apart from anyone's. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamDragDropOperation> ActiveDragOperation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> ShadowStartNode = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> ShadowEndNode = nullptr;

	/** Re-state every scrolling knob on the behaviour. One list, called from the push and the setters. */
	void PushScrollBehaviourSettings();

	/** Which pool rows the pointer is currently on. A set, so nothing has to be resized with the pool. */
	TSet<int32> HoveredPoolIndices;

	/**
	 * A pointer is dragging the rows right now. A drag is one gesture however many moves it makes and
	 * however still the finger is held in the middle of it, so nothing inside one is "finished".
	 */
	bool bScrollDragInProgress = false;

	/** Which items were realized last time the window moved, so an ARRIVAL can be told from a stay. */
	TSet<int32> RealizedItemIndices;

	/**
	 * Selections made before the source was long enough to hold them.
	 *
	 * Only filled while bAllowKeepPreselectedItems is on, and drained by ReconcileSelection the
	 * moment an index becomes valid. Not a UPROPERTY because it holds nothing but numbers.
	 */
	TArray<int32> PendingSelectedIndices;

	/**
	 * How many nested batch edits are in flight.
	 *
	 * A counter rather than a flag: the batch calls are written in terms of each other (AddItems is
	 * AddItemsAt at the end), and a flag would let the inner one clear the outer one's suppression
	 * and rebuild in the middle of a half-applied edit.
	 */
	int32 RebuildSuppressionDepth = 0;

	/** Whether a rebuild was asked for while suppressed, so exactly one happens on the way out. */
	bool bRebuildRequestedWhileSuppressed = false;

	/** RAII for the counter above; the paths it guards have early returns in them. */
	struct FScopedRebuildSuppression
	{
		explicit FScopedRebuildSuppression(UDreamListViewBase& InList) : List(InList)
		{
			++List.RebuildSuppressionDepth;
		}
		~FScopedRebuildSuppression()
		{
			if (--List.RebuildSuppressionDepth == 0 && List.bRebuildRequestedWhileSuppressed)
			{
				List.bRebuildRequestedWhileSuppressed = false;
				List.RebuildRows();
			}
		}
		FScopedRebuildSuppression(const FScopedRebuildSuppression&) = delete;
		FScopedRebuildSuppression& operator=(const FScopedRebuildSuppression&) = delete;
	private:
		UDreamListViewBase& List;
	};

	/**
	 * Make the selection agree with the source, the mode and the authored anchor -- once per rebuild,
	 * which is the only place all three are known to have settled.
	 *
	 * Indices the source no longer answers to are dropped; an anchor an author wrote is taken as a
	 * selection; a single mode holding several rows keeps the anchor; and the anchor is re-derived
	 * from the set last, so SelectedIndex always names a row that is actually selected.
	 */
	void ReconcileSelection();

	/** Duplicate one row widget out of the template and wire what it keeps for life. */
	UDreamWidget* CreatePoolRow(int32 InPoolIndex);

	/** Point a pool row at an item: label, colour, inset, rect, and both decoration hooks. */
	void BindRow(int32 InPoolIndex, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle);

	/** Put a pool row to sleep: no item, no draw, no place in anything. */
	void ParkRow(int32 InPoolIndex);

	/**
	 * Put the right DRAWING on a row for the state it is in -- FDreamListStyle::StateFaces, with
	 * RowBrush as the fallback every unstated state falls back to.
	 *
	 * Called from the bind (with the row's current state, because a row re-bound under a resting
	 * pointer is already hovered) and from the selectable's state-changed subscription, which is the
	 * same subscription the hover bookkeeping rides -- so the row's colour and its picture can never
	 * be a frame apart.
	 */
	void SkinRowForState(int32 InPoolIndex, EUISelectableSelectionState InState);

	void ApplyRowColor(UDreamWidget* InRow, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle);

	/** Grow or shrink the pool to exactly this many widgets. */
	void ResizePool(int32 InPoolSize);

	/** Bind the pool to whatever the current scroll offset makes visible. The recycling pass. */
	void RefreshVisibleWindow();

	/** The gutter, the bar's rect and the scroll range -- everything that follows from the row count. */
	void RefreshScrollFurniture(const FDreamListStyle& InStyle);

	/** True while the bar has something to say: shown at all, and either permanent or overflowing. */
	bool ShouldShowScrollBar() const;

	/** How many widgets the window needs: the viewport's worth, plus overscan at both edges. */
	int32 ResolveWindowSize() const;

	/** Set while the pool is a window onto the source rather than a widget per item. */
	UPROPERTY(Transient)
	bool bVirtualizing = false;

	/**
	 * The authored row class the pool was built with. A rebind can carry a new style into an existing
	 * row but not a new CLASS -- the instance lives inside the row and is made once per pool row --
	 * so this is one of the two things that still forces a teardown. See RebuildRows for why a
	 * teardown is worth avoiding at all.
	 */
	UPROPERTY(Transient)
	TSubclassOf<UDreamUserWidget> PoolRowTemplateClass = nullptr;

	/** The display index the pool's first row currently shows. Zero while not recycling. */
	UPROPERTY(Transient)
	int32 WindowStart = 0;
};

/**
 * A list whose hierarchy is code, not an asset: rows built from a source, in a scrolling viewport.
 *
 * UMG's ListView in the DreamGUI idiom -- and deliberately smaller than it. There is no entry-widget
 * protocol to implement and no data source to write: the two properties a designer actually reaches
 * for (a set of items, and which one is selected) are UPROPERTYs, so .dui, the designer, Blueprint
 * and a `<->` binding can all drive them without anyone writing a line of glue.
 *
 * See UDreamListViewBase for the shape of the tree, why the rows are placed rather than arranged,
 * when it starts recycling them, and how a Native.List sits beside the `each` language feature.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream List View")
class DREAMGUI_API UDreamListView : public UDreamListViewBase
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why it
	 * stays editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "List")
	FDreamListStyle Style;

	/** By value, not by reference: a UFUNCTION return has to be a value, and a style is a small struct. */
	UFUNCTION(BlueprintPure, Category = "List")
	FDreamListStyle GetStyle() const { return Style; }

	/** Replace the whole look and re-push it. Row geometry is style, so this rebuilds the rows. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetStyle(const FDreamListStyle& InStyle);

protected:
	virtual FDreamListStyle ResolveListStyle() const override;
};
