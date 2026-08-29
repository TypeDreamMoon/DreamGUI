// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#pragma once

class FDreamWidgetBlueprintEditor;
class UDreamWidget;
class SDreamWidgetComponentEditor;

/**
 * 
 */
class SDreamUIPrefabEditorDetails : public SCompoundWidget, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPrefabEditorDetails)
    {
    }

    SLATE_END_ARGS()

    /** Widget constructor */
	void Construct(const FArguments& Args, UWorld* InWorld);
	void Refresh();

	// FNotifyHook -- the details view edits PREVIEW widgets, and a preview is rebuilt from the
	// authoring tree, so an edit that stopped there would be gone at the next rebuild. These two are
	// where it reaches the asset. UMG's SWidgetDetailsView has the same pair for the same reason.
	/** What the panel is currently showing -- widgets, visuals or behaviours, whichever was selected. */
	TArray<UObject*> GetEditedObjects() const;

	virtual void NotifyPreChange(FEditPropertyChain* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged) override;
	// End FNotifyHook

	virtual ~SDreamUIPrefabEditorDetails();
private:
	UDreamWidget* GetSelectedWidgetContext() const;
	void OnEditorSelectionChanged();
	void OnComponentSelectionChanged(const TArray<TWeakObjectPtr<class UDreamUIBehaviour>>& SelectedComponents);

	TWeakPtr<FDreamWidgetBlueprintEditor> PrefabEditorPtr;
	TWeakObjectPtr<UWorld> World;

	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);
	bool IsEditorAllowEditing()const;

	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class SDreamWidgetComponentEditor> ComponentEditor;
	TWeakObjectPtr<UDreamWidget> CachedWidget;
	bool bIsSelectFromDreamUIEditor = false;
	bool bIsSelectFromComponentList = false;
};
