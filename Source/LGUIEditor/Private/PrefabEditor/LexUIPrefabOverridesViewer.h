// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLexUIPrefabEditor;
class ULexUIPrefab;
class ULexWidget;
class SVerticalBox;
class IDetailsView;

/**
 * Whole-asset statistics of nested-instance overrides. Every sub-prefab instance in this asset can
 * pin properties on its objects (FLexUISubPrefabData::ObjectOverrideParameterArray); a pinned
 * property silently shadows any later edit made in the sub-prefab asset itself — the classic
 * "I changed the page but the parent still shows the old value" trap. This tab enumerates every
 * pinned object and property in one searchable place so those shadows are visible at a glance.
 *
 * Rows are actionable: clicking an object selects it in the prefab editor and loads it into the
 * embedded details view for direct property editing, and each row's Revert menu un-pins single
 * properties or the whole object through the same helper machinery as the Details panel's
 * "Prefab Override Properties" dropdown.
 */
class SLexUIPrefabOverridesViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabOverridesViewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject);

	/** Repopulate from the asset's current SubPrefabMap. */
	void Rebuild();

private:
	bool MatchesFilter(const FString& Haystack) const;
	/** Select the override object in the prefab editor and load it into the embedded details view. */
	void SelectOverrideObject(UObject* Object);
	/** The widget an override object belongs to (itself, its outer widget, or none). */
	static ULexWidget* ResolveOwnerWidget(UObject* Object);

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TWeakObjectPtr<ULexUIPrefab> PrefabWeak;
	TSharedPtr<SVerticalBox> ContentBox;
	TSharedPtr<IDetailsView> ObjectDetailView;
	TWeakObjectPtr<UObject> SelectedObject;
	FString FilterString;
};
