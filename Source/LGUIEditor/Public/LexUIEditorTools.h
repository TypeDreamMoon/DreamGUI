// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#pragma once
class ULexWidget;
class ULexUIBehaviour;
class ULexUIPrefabHelperObject;
class ULexUIPrefab;

DECLARE_MULTICAST_DELEGATE_OneParam(FEditingPrefabChangedDelegate, ULexWidget*);
DECLARE_MULTICAST_DELEGATE_OneParam(FBeforeApplyPrefabDelegate, ULexUIPrefabHelperObject*);

class LGUIEDITOR_API FLexUIEditorTools
{
private:
	static FString PrevSavePrefabFolder;
public:
	static FString LexUIPresetPrefabPath;
	static FEditingPrefabChangedDelegate OnEditingPrefabChanged;
	static FBeforeApplyPrefabDelegate OnBeforeApplyPrefab;
	static TArray<ULexWidget*> GetRootWidgetListFromSelection(const TArray<ULexWidget*>& InSelectedWidgets);
	/** UMG-style unique name in the containing prefab: Name, Name_1, Name_2, ... */
	static FString MakeUniqueWidgetDisplayName(ULexWidget* ContextWidget, const FString& DesiredName,
		const ULexWidget* WidgetToIgnore = nullptr);
	/** Normalize an existing prefab tree and return how many duplicate names were changed. */
	static int32 EnsureUniqueWidgetDisplayNames(ULexWidget* RootWidget, TArray<FString>* OutRenamedWidgets = nullptr);
	static void CreateWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(class ULexWidget*)> Callback);
	static void CreateUIControls(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath);
	static ULexWidget* CreateWidgetAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(class ULexWidget*)> Callback);
	static ULexWidget* CreateUIControlsAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath, TFunction<void(class ULexWidget*)> Callback = nullptr);
	/**
	 * Instantiate a prefab under the selection and keep it linked to its source asset, the way a
	 * Content Browser drop does. CreateUIControls flattens instead, which is only what the plugin's
	 * own preset recipes want -- a project asset dropped that way loses every override and every
	 * route back to the thing it came from.
	 */
	static ULexWidget* CreateSubPrefabAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FString InPrefabPath, TFunction<void(class ULexWidget*)> Callback = nullptr);
	/** Reject self-nesting, cyclic nesting and prefab versions too old to deserialize. */
	static bool CanNestPrefabUnderWidget(ULexUIPrefab* InPrefab, ULexWidget* InParentWidget, FText& OutError);
	static void CreateRegisteredControl(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FName ControlName);
	static ULexWidget* CreateRegisteredControlAndReturn(TFunction<ULexWidget*()> GetSelectedWidgetFunction, FName ControlName, TFunction<void(class ULexWidget*)> Callback = nullptr);
	static void DuplicateWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static void CopyWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static void PasteWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetFunction);
	enum class EDeleteWidgetWarningType
	{
		/** Delete even when a behaviour variable points at the widget. */
		DeleteSilently,
		/** Name the bindings the delete would break and let the user call it off. */
		WarnAndAskUser,
	};
	/**
	 * Companion-behaviour variables that point at these widgets, or at anything under them, as
	 * "Variable -> Widget". This is the same link "Find References" searches for, so the two
	 * agree on what counts as a binding.
	 */
	static TArray<FText> CollectBehaviourBindingsToWidgets(ULexUIBehaviour* InCompanionBehaviour, const TArray<ULexWidget*>& InWidgets);
	/** The companion behaviour of the prefab these widgets belong to, whose variables the delete would strand. */
	static ULexUIBehaviour* FindCompanionForWidgets(const TArray<ULexWidget*>& InWidgets);
	/**
	 * UMG's ShouldContinueDeleteOperation. A deleted widget takes its bound variable's target with
	 * it: the variable is authored on the companion, not generated from the widget, so it stays on
	 * the blueprint holding nothing and everything still compiles -- the loss surfaces at runtime,
	 * with nothing left to name the widget that went away.
	 */
	static bool ShouldContinueDeleteOperation(const TArray<ULexWidget*>& InWidgets);
	static void DeleteWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction, EDeleteWidgetWarningType WarningType = EDeleteWidgetWarningType::DeleteSilently);
	static void CutWidgets(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanDuplicateWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanCopyWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanPasteWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static bool CanCutWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanDeleteWidget(TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArrayFunction);
	
	static bool CanCreatePrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void CreatePrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void RefreshLoadedPrefab();
	static void RefreshOpenedPrefabEditor(ULexUIPrefab* InPrefab);
	static void RefreshOnSubPrefabChange(ULexUIPrefab* InSubPrefab);
	static TArray<ULexUIPrefab*> GetAllPrefabArray();
	static bool CanUnpackWidgetForPrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void UnpackPrefab(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void SelectPrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static bool CanBrowsePrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void OpenPrefabAsset(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static bool CanCheckPrefabOverrideParameter(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static bool CanCreateWidget(TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	static void CleanupPrefabs();
	static bool IsWidgetCompatibleWithLexUIToolsMenu(ULexWidget* InWidget);

	/**
	 * One entry per copied widget, in selection order.
	 *
	 * This used to be keyed by display name, so two copied widgets sharing a name collapsed into one
	 * and pasted as one, silently. Sub-prefab children collide by construction --
	 * EnsureUniqueWidgetDisplayNames deliberately skips them -- so the name is a naming hint for
	 * paste and nothing more; it must never decide how many widgets there are.
	 */
	struct FCopiedWidgetPrefab
	{
		FString DisplayName;
		TWeakObjectPtr<ULexUIPrefab> Prefab;
	};
	static TArray<FCopiedWidgetPrefab> CopiedWidgetPrefabList;
	static bool HaveValidCopiedWidgets();
};
