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
	/**
	 * The chainless PRE-change overload, the twin of the post-change one below.
	 *
	 * The transform section notifies through this one (it has no property node, so it has no chain),
	 * and the base class's body is empty -- so the destination was written without ever being
	 * snapshotted first, and undoing a transform typed into the panel restored the preview and left
	 * the template holding the new value.
	 */
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged) override;
	/** The chainless overload custom sections notify through; forwards into the one above. */
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
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
	 * The answers above, kept for a moment.
	 *
	 * The details view asks them from an attribute, so every visible row asks on every frame, and for a
	 * text-authored hierarchy the answer prints the row's value to see whether the language can spell it.
	 * Kept until anything that could change an answer happens here -- an edit, a selection, a refresh --
	 * and never longer than ReadOnlyCacheSeconds, which covers the changes this panel does not hear about
	 * (an undo, a viewport drag, a recompile).
	 */
	struct FReadOnlyKey
	{
		TArray<const void*, TInlineAllocator<8>> Parts;
		bool operator==(const FReadOnlyKey& Other) const { return Parts == Other.Parts; }
		friend uint32 GetTypeHash(const FReadOnlyKey& Key)
		{
			uint32 Hash = 0;
			for (const void* Part : Key.Parts)
			{
				Hash = HashCombineFast(Hash, ::PointerHash(Part));
			}
			return Hash;
		}
	};
	static constexpr double ReadOnlyCacheSeconds = 0.5;
	mutable TMap<FReadOnlyKey, bool> PropertyReadOnlyCache;
	mutable TMap<TPair<FName, FName>, bool> CustomRowReadOnlyCache;
	mutable double ReadOnlyCacheBornAt = 0.0;
	/** Empties both caches when they are older than ReadOnlyCacheSeconds. */
	void ExpireReadOnlyCache() const;
	bool ComputeIsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent) const;
	void InvalidateReadOnlyCache() const;
	/**
	 * Whether this panel may make STRUCTURAL edits: the rename box in the header, and the component
	 * list's add / remove / cut / paste. False for a text-authored hierarchy -- both of those write
	 * things only the `.dui` can say, and both are drawn disabled rather than failing on click.
	 */
	bool IsEditorAllowEditing()const;
	bool IsNameEditable()const;
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
