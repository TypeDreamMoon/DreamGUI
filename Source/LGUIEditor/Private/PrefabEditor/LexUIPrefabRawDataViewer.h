// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLexUIPrefabEditor;
class ULexUIPrefab;
class SVerticalBox;
class IDetailsView;

/**
 * Structured inspector for a prefab asset's serialized innards. The old viewer was a bare details
 * view of the asset object — a version shown as a naked number, BinaryData as tens of thousands of
 * bytes, and the GUID bookkeeping invisible. This one decodes what can be decoded (version names,
 * payload sizes, reference tables resolved to names, GUID->object mappings with dead entries
 * flagged, sub-prefab summaries) and keeps the raw property dump collapsed at the bottom for the
 * cases where nothing but the raw truth helps.
 */
class SLexUIPrefabRawDataViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabRawDataViewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject);

	/** Repopulate every section from the asset's current state. */
	void Rebuild();

private:
	TSharedRef<SWidget> BuildOverviewSection(ULexUIPrefab* Prefab);
	TSharedRef<SWidget> BuildReferenceListsSection(ULexUIPrefab* Prefab);
	TSharedRef<SWidget> BuildGuidMapSection(ULexUIPrefab* Prefab);
	TSharedRef<SWidget> BuildSubPrefabSection(ULexUIPrefab* Prefab);
	/** Case-insensitive substring match against the current search text; everything matches when the box is empty. */
	bool MatchesFilter(const FString& Haystack) const;

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TWeakObjectPtr<ULexUIPrefab> PrefabWeak;
	TSharedPtr<SVerticalBox> ContentBox;
	TSharedPtr<IDetailsView> DescriptorDetailView;
	FString FilterString;
};
