// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/LexWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

class SLexWidgetEditorHierarchyView;
class ULexWidget;
class FLexUIPrefabEditor;

class SLexWidgetEditorHierarchyViewItem : public STableRow<TWeakObjectPtr<ULexWidget>>
{
public:
	SLATE_BEGIN_ARGS(SLexWidgetEditorHierarchyViewItem) {}
		SLATE_EVENT(FSimpleDelegate, MouseEnter)
		SLATE_EVENT(FSimpleDelegate, MouseExit)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<ULexWidget> InModel
		, TSharedPtr<SLexWidgetEditorHierarchyView> InHierarchyView, TSharedPtr<FLexUIPrefabEditor> InManager);

	// Begin SWidget
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// End SWidget
	void RequestEditName();
	bool CanRename();
private:
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem);
	FReply HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem);
	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void HandleDragEnter(FDragDropEvent const& DragDropEvent);
	void HandleDragLeave(const FDragDropEvent& DragDropEvent);
	FText GetItemText() const;
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
