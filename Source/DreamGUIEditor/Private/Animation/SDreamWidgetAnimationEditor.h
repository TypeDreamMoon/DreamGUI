// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/SListView.h"
#include "Input/Reply.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SCompoundWidget.h"

class ISequencer;
class FDreamWidgetBlueprintEditor;
class UDreamUISequence;
class UDreamWidget;
class UDreamWidgetAnimationComponent;
class UDreamWidgetAnimation;
class UDreamWidgetBlueprint;
class SDreamWidgetAnimationEditorWidget;
struct FWidgetAnimationListItem;

class DREAMGUIEDITOR_API SDreamWidgetAnimationEditor : public SCompoundWidget
{
public:
	~SDreamWidgetAnimationEditor();

	SLATE_BEGIN_ARGS(SDreamWidgetAnimationEditor) {}
	SLATE_END_ARGS();
	/** InDesigner is the designer this panel belongs to: its asset is the only one the panel ever edits. */
	void Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner);
	//route F2 / Delete / Ctrl+D to the animation-list commands
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** See SDreamWidgetAnimationEditorWidget::SetToolkitHost. */
	void SetToolkitHost(TSharedPtr<class IToolkitHost> InToolkitHost);
	/**
	 * Carry the graph's references to an animation across a rename of it, once its display name says the new one.
	 *
	 * An animation is a member variable of the class, named after it, so a rename is a new member as far as
	 * the graph is concerned and every node that read the old one stopped compiling. Marks the Blueprint
	 * structurally modified, so the class declares the new name, and then moves the references the way the
	 * compiler moves a widget's (FDreamWidgetBlueprintCompilerContext::MigrateVariableReferences).
	 */
	void NotifyAnimationRenamed(const FString& OldName, const FString& NewName);
	void AssignDreamWidgetAnimationComponent(TWeakObjectPtr<UDreamWidgetAnimationComponent> InSequenceComponent);
	/**
	 * Find the designer's animation host again, and keep the selection: the same animation when it is still
	 * there, otherwise the one that now has its name.
	 *
	 * The designer calls this whenever its asset changes, compiles or is undone. The host is found on the
	 * asset's authoring root each time rather than remembered, because a compile of a text-authored asset
	 * builds a new tree with a new copy of every animation, and the panel used to go on editing the old
	 * copies -- which the next compile threw away.
	 */
	void RefreshAnimationHost();
	UDreamWidgetAnimation* GetAnimation() const;
	void SelectAnimation(UDreamWidgetAnimation* InAnimation);
	/** Leave animation mode: deselect, hand the sequencer a null sequence, restore the pre-animated pose. */
	void ClearAnimationSelection();
	UDreamWidgetAnimationComponent* GetSequenceComponent()const { return WeakSequenceComponent.Get(); }
	void RefreshAnimationList();
	/**
	 * Tell the Blueprint its animations changed, so the class picks the change up -- and so does PIE, which
	 * recompiles only a Blueprint marked dirty, and played the class's previous copy of every animation.
	 *
	 * bStructural for an animation added, deleted or renamed: each one is a member variable of the class, so
	 * the skeleton has to be regenerated for the graph to see it.
	 */
	void MarkAnimationDataDirty(bool bStructural = true);
	/**
	 * Fill InAsset with a standalone copy of InSource: the movie scene whole, and the bindings rebuilt as
	 * widget paths from the designer's authoring root. Everything "Export to Asset..." does once its dialog
	 * has made the asset.
	 *
	 * OutKept and OutDropped count the bindings carried over and the ones that resolved to nothing. False
	 * when there is nothing to export or nothing to export into.
	 */
	bool ExportAnimationToAsset(UDreamWidgetAnimation* InSource, UDreamUISequence* InAsset, int32& OutKept, int32& OutDropped) const;
	TSharedPtr<ISequencer> GetSequencer() const;
private:
	/** The designer that owns this panel. Weak: the designer owns the panel, not the other way round. */
	TWeakPtr<FDreamWidgetBlueprintEditor> WeakDesigner;
	TWeakObjectPtr<UDreamWidgetAnimationComponent> WeakSequenceComponent;
	/** The designer's authoring root, asked each time. */
	UDreamWidget* GetRootWidget() const;
	/** The asset whose animations these are. */
	UDreamWidgetBlueprint* GetWidgetBlueprint() const;
	UDreamWidgetAnimationComponent* FindAnimationHost(UDreamWidget* RootWidget) const;
	UDreamWidgetAnimationComponent* EnsureAnimationHost();
	FDelegateHandle OnObjectsReplacedHandle;

	TSharedPtr<SDreamWidgetAnimationEditorWidget> AnimationEditorWidget;

	TSharedPtr<SListView<TSharedPtr<FWidgetAnimationListItem>>> AnimationListView;
	TArray< TSharedPtr<FWidgetAnimationListItem> > Animations;
	/** True while RebuildAnimationList replaces the rows; the selection callback waits for its one assignment. */
	bool bRebuildingAnimationList = false;
	/**
	 * Rebuild the rows from the host, selecting InSelected again when it is still there, or else the
	 * animation now called InSelectedName, and hand the sequencer the result once.
	 */
	void RebuildAnimationList(const UDreamWidgetAnimation* InSelected, const FString& InSelectedName);
	/** The row for InAnimation, or for the animation called InName when that one is gone. */
	TSharedPtr<FWidgetAnimationListItem> FindListItem(const UDreamWidgetAnimation* InAnimation, const FString& InName) const;
	/** Select a just-made animation's row and open it for renaming. */
	void BeginRenamingNewAnimation(const UDreamWidgetAnimation* InAnimation, const FString& InName);
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
	UDreamWidgetAnimation* GetSelectedAnimation() const;
	int32 GetSelectedAnimationSourceIndex() const;
	bool CanExecuteAnimationListAction() const;
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
};
