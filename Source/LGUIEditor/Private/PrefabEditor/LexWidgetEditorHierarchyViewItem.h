// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/LexWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class SLexWidgetEditorHierarchyView;
class ULexWidget;
class FLexUIPrefabEditor;
class FScopedTransaction;

/**
 * A widget (or several) being dragged to a new parent.
 *
 * Declared here rather than in the .cpp because the design surface accepts the same drag: a
 * hierarchy drag dropped onto the canvas has to mean what it means in the tree, and reusing the op
 * and the validator below is what keeps the two surfaces from drifting into two sets of rules.
 */
class FHierarchyLexWidgetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FHierarchyLexWidgetDragDropOp, FDecoratedDragDropOp)

		virtual ~FHierarchyLexWidgetDragDropOp();

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	struct FItem
	{
		/** The widget being dragged and dropped */
		ULexWidget* Widget = nullptr;

		/** The original parent of the widget. */
		ULexWidget* WidgetParent = nullptr;
	};

	TArray<FItem> DraggedWidgets;

	/** The widget being dragged and dropped */
	FScopedTransaction* Transaction;

	/** Constructs a new drag/drop operation */
	static TSharedRef<FHierarchyLexWidgetDragDropOp> New(const TArray<ULexWidget*>& InWidgets);
};

/**
 * Validate (bIsDrop false) or perform (bIsDrop true) a drop of any of the editor's drag operations
 * onto TargetItem: a hierarchy move, a palette element, or a Content-Browser asset. Unset return
 * means refused, and the op's hover text and icon carry the reason.
 */
TOptional<EItemDropZone> ProcessHierarchyDragDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone,
	bool bIsDrop, TSharedPtr<FLexUIPrefabEditor> Manager, ULexWidget* TargetItem,
	TOptional<int32> Index = TOptional<int32>());

namespace LexWidgetHierarchyDrop
{
	/**
	 * Whose designer lock has a say over a drop in this zone, or null if no widget does.
	 *
	 * A lock protects what is inside a widget, and Above/Below never go inside it: those two zones are
	 * rewritten into an insert in the PARENT's child list, so it is the parent that must consent.
	 * Asking the hovered row instead is what made a locked widget un-neighbourable -- no sibling could
	 * be placed next to it at all -- while still letting an insert land in a locked parent whose
	 * children happened to be unlocked.
	 *
	 * The exception is a row with no parent: there is no sibling list, so the drop path rewrites the
	 * zone into a drop inside that row, and it is the row itself that has the say.
	 */
	const ULexWidget* GetLockOwnerForDropZone(const ULexWidget* TargetItem, EItemDropZone DropZone);
}

class SLexWidgetEditorHierarchyViewItem : public STableRow<TWeakObjectPtr<ULexWidget>>
{
public:
	SLATE_BEGIN_ARGS(SLexWidgetEditorHierarchyViewItem) {}
		SLATE_EVENT(FSimpleDelegate, MouseEnter)
		SLATE_EVENT(FSimpleDelegate, MouseExit)
		/** The tree's live search text, so the row can show which part of the name matched. */
		SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<ULexWidget> InModel
		, TSharedPtr<SLexWidgetEditorHierarchyView> InHierarchyView, TSharedPtr<FLexUIPrefabEditor> InManager);

	// Begin SWidget
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// End SWidget
	void RequestEditName();
	bool CanRename() const;
private:
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem);
	FReply HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem);
	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void HandleDragEnter(FDragDropEvent const& DragDropEvent);
	void HandleDragLeave(const FDragDropEvent& DragDropEvent);
	FText GetItemText() const;
	FText GetItemTypeText() const;
	FText GetItemTooltipText() const;
	FSlateColor GetNameTextColorAndOpacity() const;
	FSlateColor GetVisibilityIconColorAndOpacity() const;
	bool IsReadOnly() const;
	void OnBeginNameTextEdit();
	void OnEndNameTextEdit();
	bool OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage);
	void OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo);
	FReply OnToggleVisibility();
	FText GetVisibilityBrushForWidget() const;
	FReply OnToggleLockedInDesigner();
	FText GetLockBrushForWidget() const;
	FSlateColor GetLockIconColorAndOpacity() const;

	bool SupportDrop(ULexWidget* Dragging, ULexWidget* Current, EItemDropZone DropZone);

private:
	TWeakPtr<SLexWidgetEditorHierarchyView> HierarchyView;
	TWeakPtr<FLexUIPrefabEditor> Manager;
	FSimpleDelegate MouseEnter;
	FSimpleDelegate MouseExit;
	/** Edit box for the name. */
	TSharedPtr<SInlineEditableTextBlock> EditBox;
	/* The model that this tree item represents */
	TWeakObjectPtr<ULexWidget> Widget;
	/** Text when we start editing. */
	FText InitialText;
};
