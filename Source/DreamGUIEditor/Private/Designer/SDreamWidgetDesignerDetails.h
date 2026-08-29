// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
// FNotifyHook is a BASE below, and FPropertyAndParent is a parameter type: both have to be
// complete here, and inside a unity blob some neighbour always had included them.
#include "Misc/NotifyHook.h"
#include "PropertyEditorDelegates.h"
#pragma once

class FDreamWidgetBlueprintEditor;
class UDreamWidget;
class SDreamWidgetComponentEditor;

/**
 * 
 */
class SDreamWidgetDesignerDetails : public SCompoundWidget, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SDreamWidgetDesignerDetails)
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

	virtual ~SDreamWidgetDesignerDetails();
private:
	UDreamWidget* GetSelectedWidgetContext() const;
	void OnEditorSelectionChanged();
	void OnComponentSelectionChanged(const TArray<TWeakObjectPtr<class UDreamUIBehaviour>>& SelectedComponents);

	TWeakPtr<FDreamWidgetBlueprintEditor> DesignerPtr;
	TWeakObjectPtr<UWorld> World;

	/**
	 * The two halves of "visibly disabled" for a hierarchy that came from a `.dui`.
	 *
	 * Two, because the details view asks two different questions and only reaches one of them per
	 * row: a row backed by a property node goes through FIsPropertyReadOnly and can be answered per
	 * property, while a row that IS a custom widget goes through FIsCustomRowReadOnly and arrives
	 * with nothing but its own name and its category's -- there is no property node behind it to ask
	 * (FDetailItemNode::IsPropertyEditingEnabledImpl). Binding only the first leaves the anchor block,
	 * the Panel/Visual/Self-Layout pickers and the canvas rows fully live on an asset whose structure
	 * the text owns, which is the worst of the two halves to miss.
	 */
	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);
	bool IsCustomRowReadOnly(FName InRowName, FName InCategoryName) const;
	/**
	 * Whether this panel may make STRUCTURAL edits: the rename box in the header, and the component
	 * list's add / remove / cut / paste. False for a text-authored hierarchy -- both of those write
	 * things only the `.dui` can say, and both are drawn disabled rather than failing on click.
	 */
	bool IsEditorAllowEditing()const;
	/** The hierarchy this panel is showing belongs to a `.dui`. */
	bool IsTextAuthoredHierarchy() const;
	/** The banner above the properties: which file owns this, and what is still editable here. */
	FText GetTextAuthoredBannerText() const;
	EVisibility GetTextAuthoredBannerVisibility() const;

	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class SDreamWidgetComponentEditor> ComponentEditor;
	TWeakObjectPtr<UDreamWidget> CachedWidget;
	bool bIsSelectFromDreamUIEditor = false;
	bool bIsSelectFromComponentList = false;
};
