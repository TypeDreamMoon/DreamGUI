// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/SListView.h"
#include "Input/Reply.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SCompoundWidget.h"

class ISequencer;
class UDreamWidget;
class UDreamUIPrefabSequenceComponent;
class UDreamUIPrefabSequence;
class SDreamUIPrefabSequenceEditorWidget;
struct FWidgetAnimationListItem;

class SDreamUIPrefabSequenceEditor : public SCompoundWidget
{
public:
	~SDreamUIPrefabSequenceEditor();

	SLATE_BEGIN_ARGS(SDreamUIPrefabSequenceEditor) {}
	SLATE_END_ARGS();
	void Construct(const FArguments& InArgs);
	//route F2 / Delete / Ctrl+D to the animation-list commands
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** See SDreamUIPrefabSequenceEditorWidget::SetToolkitHost. */
	void SetToolkitHost(TSharedPtr<class IToolkitHost> InToolkitHost);
	/** Warn about companion-blueprint calls that still play the old display name. */
	void NotifyAnimationRenamed(const FString& OldName, const FString& NewName);
	void AssignDreamUIPrefabSequenceComponent(TWeakObjectPtr<UDreamUIPrefabSequenceComponent> InSequenceComponent);
	UDreamUIPrefabSequence* GetPrefabSequence() const;
	void SelectAnimation(UDreamUIPrefabSequence* InAnimation);
	/** Leave animation mode: deselect, hand the sequencer a null sequence, restore the pre-animated pose. */
	void ClearAnimationSelection();
	UDreamUIPrefabSequenceComponent* GetSequenceComponent()const { return WeakSequenceComponent.Get(); }
	void RefreshAnimationList();
	void MarkAnimationDataDirty();
	void OnEditingPrefabChanged(UDreamWidget* RootWidget);
	TSharedPtr<ISequencer> GetSequencer() const;
private:
	TWeakObjectPtr<UDreamWidget> WeakRootWidget;
	TWeakObjectPtr<UDreamUIPrefabSequenceComponent> WeakSequenceComponent;
	UDreamUIPrefabSequenceComponent* FindAnimationHost(UDreamWidget* RootWidget) const;
	UDreamUIPrefabSequenceComponent* EnsureAnimationHost();
	FDelegateHandle OnObjectsReplacedHandle;
	FDelegateHandle EditingPrefabChangedHandle;
	FDelegateHandle PostUndoRedoHandle;
	void OnPostUndoRedo();

	TSharedPtr<SDreamUIPrefabSequenceEditorWidget> PrefabSequenceEditor;

	TSharedPtr<SListView<TSharedPtr<FWidgetAnimationListItem>>> AnimationListView;
	TArray< TSharedPtr<FWidgetAnimationListItem> > Animations;
	TSharedRef<ITableRow> OnGenerateRowForAnimationListView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedRef<STableViewBase>& InOwnerTableView);
	void OnAnimationListViewSelectionChanged(TSharedPtr<FWidgetAnimationListItem> InListItem, ESelectInfo::Type InSelectInfo);
	void OnItemScrolledIntoView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedPtr<ITableRow>& InWidget) const;
	FReply OnNewAnimationClicked();
	TSharedPtr<SSearchBox> SearchBoxPtr;
	void OnAnimationListViewSearchChanged(const FText& InSearchText);
	TSharedPtr<SWidget> OnContextMenuOpening()const;
	TSharedPtr<FUICommandList> CommandList;
	void CreateCommandList();
	void OnDuplicateAnimation();
	void OnExportAnimationToAsset();
	void OnDeleteAnimation();
	void OnRenameAnimation();
	UDreamUIPrefabSequence* GetSelectedAnimation() const;
	int32 GetSelectedAnimationSourceIndex() const;
	bool CanExecuteAnimationListAction() const;
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
};
