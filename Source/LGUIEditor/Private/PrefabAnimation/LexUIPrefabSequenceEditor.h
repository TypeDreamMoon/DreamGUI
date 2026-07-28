// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SCompoundWidget.h"

class ISequencer;
class ULexWidget;
class ULexUIPrefabSequenceComponent;
class ULexUIPrefabSequence;
class SLexUIPrefabSequenceEditorWidget;
struct FWidgetAnimationListItem;
class ULexUIPrefabHelperObject;

class SLexUIPrefabSequenceEditor : public SCompoundWidget
{
public:
	~SLexUIPrefabSequenceEditor();

	SLATE_BEGIN_ARGS(SLexUIPrefabSequenceEditor) {}
	SLATE_END_ARGS();
	void Construct(const FArguments& InArgs);

	void AssignLexUIPrefabSequenceComponent(TWeakObjectPtr<ULexUIPrefabSequenceComponent> InSequenceComponent);
	ULexUIPrefabSequence* GetPrefabSequence() const;
	void SelectAnimation(ULexUIPrefabSequence* InAnimation);
	ULexUIPrefabSequenceComponent* GetSequenceComponent()const { return WeakSequenceComponent.Get(); }
	void RefreshAnimationList();
	void MarkAnimationDataDirty();
	void OnEditingPrefabChanged(ULexWidget* RootWidget);
	TSharedPtr<ISequencer> GetSequencer() const;
private:
	TWeakObjectPtr<ULexWidget> WeakRootWidget;
	TWeakObjectPtr<ULexUIPrefabSequenceComponent> WeakSequenceComponent;
	ULexUIPrefabSequenceComponent* FindAnimationHost(ULexWidget* RootWidget) const;
	ULexUIPrefabSequenceComponent* EnsureAnimationHost();
	FDelegateHandle OnObjectsReplacedHandle;
	FDelegateHandle EditingPrefabChangedHandle;
	FDelegateHandle OnBeforeApplyPrefabHandle;
	FDelegateHandle PostUndoRedoHandle;
	void OnBeforeApplyPrefab(ULexUIPrefabHelperObject* InObject);
	void OnPostUndoRedo();

	TSharedPtr<SLexUIPrefabSequenceEditorWidget> PrefabSequenceEditor;

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
	void OnDeleteAnimation();
	void OnRenameAnimation();
	ULexUIPrefabSequence* GetSelectedAnimation() const;
	int32 GetSelectedAnimationSourceIndex() const;
	bool CanExecuteAnimationListAction() const;
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
};
