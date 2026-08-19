// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLexUIPrefabEditor;
class ULexUIPrefab;
class ULexWidget;
class ULexUIBehaviour;
class SVerticalBox;
class FObjectProperty;

/**
 * The behaviour panel: everything the prefab's companion behaviour needs and provides, in one
 * place — UMG's "Bind Widgets" panel adapted to LexUI's code-behind trinity.
 *
 * - Widget References: every widget/visual/behaviour-typed object property on the effective
 *   behaviour class with its bind status; unbound savable properties get a quick-bind menu of
 *   type-compatible widgets from the hierarchy, plus an on-demand run of the save-time
 *   auto-bind pass (name matching). Transient properties are shown as runtime-bound
 *   (native behaviours resolve them by display name in Awake).
 * - Provides: the class's implementable events, callable functions, and assignable delegates,
 *   with signatures — what a designer can call or implement.
 * - Selected Widget Events: every FLexUIEventDelegate on the selected widget's behaviours with
 *   bound status and one-click handler generation (the Event+ flow), plus promote-to-variable.
 */
class SLexUIPrefabBehaviourViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabBehaviourViewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject);

	/** Repopulate every section from the current behaviour class, instance, and selection. */
	void Rebuild();

private:
	bool MatchesFilter(const FString& Haystack) const;
	void HandleSelectionChanged();
	void RunAutoBind();
	void BuildWidgetReferenceSection(UClass* BehaviourClass, ULexUIBehaviour* Primary);
	void BuildProvidesSection(UClass* BehaviourClass);
	void BuildSelectedWidgetSection();
	/**
	 * Type-compatible bind targets under the loaded root for a widget/visual/behaviour property.
	 * Sub-prefab widgets are counted into OutSubPrefabSkipped rather than offered: the prefab
	 * writer cannot reference them, so the automatic pass refuses them too.
	 */
	void CollectBindCandidates(FObjectProperty* Property, TArray<TPair<UObject*, FString>>& OutCandidates, int32& OutSubPrefabSkipped) const;

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TWeakObjectPtr<ULexUIPrefab> PrefabWeak;
	TSharedPtr<SVerticalBox> ContentBox;
	FString FilterString;
};
