// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#pragma once
class UDreamWidget;
class UDreamUIBehaviour;

DECLARE_MULTICAST_DELEGATE_OneParam(FEditingWidgetChangedDelegate, UDreamWidget*);

class DREAMGUIEDITOR_API FDreamUIEditorTools
{
private:
public:
	static FEditingWidgetChangedDelegate OnEditingWidgetChanged;
	static TArray<UDreamWidget*> GetRootWidgetListFromSelection(const TArray<UDreamWidget*>& InSelectedWidgets);
	/** UMG-style unique name in the containing prefab: Name, Name_1, Name_2, ... */
	static FString MakeUniqueWidgetDisplayName(UDreamWidget* ContextWidget, const FString& DesiredName,
		const UDreamWidget* WidgetToIgnore = nullptr);
	/** Normalize an existing prefab tree and return how many duplicate names were changed. */
	static int32 EnsureUniqueWidgetDisplayNames(UDreamWidget* RootWidget, TArray<FString>* OutRenamedWidgets = nullptr);
	static void CreateWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(class UDreamWidget*)> Callback);
	static void CreateUIControls(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InControlClassPath);
	static UDreamWidget* CreateWidgetAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString Name, UClass* VisualClass, TFunction<void(class UDreamWidget*)> Callback);
	static UDreamWidget* CreateUIControlsAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString InControlClassPath, TFunction<void(class UDreamWidget*)> Callback = nullptr);
	/**
	 * Place an instance of a control CLASS under the selected widget: the shared half of the two
	 * roads to one, a Blueprint resolved from an asset path and a C++ control that arrives as the
	 * class. InDisplayName names the new widget in the hierarchy and is what failures are logged
	 * against.
	 */
	static UDreamWidget* PlaceControlClassAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, UClass* ControlClass, const FString& InDisplayName, TFunction<void(class UDreamWidget*)> Callback = nullptr);
	/**
	 * Instantiate a prefab under the selection and keep it linked to its source asset, the way a
	 * Content Browser drop does. CreateUIControls flattens instead, which is only what the plugin's
	 * own preset recipes want -- a project asset dropped that way loses every override and every
	 * route back to the thing it came from.
	 */
	/** Reject self-nesting, cyclic nesting and prefab versions too old to deserialize. */
	static void CreateRegisteredControl(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FName ControlName);
	static UDreamWidget* CreateRegisteredControlAndReturn(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FName ControlName, TFunction<void(class UDreamWidget*)> Callback = nullptr);
	static void DuplicateWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static void CopyWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static void PasteWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetFunction);
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
	static TArray<FText> CollectBehaviourBindingsToWidgets(UDreamUIBehaviour* InCompanionBehaviour, const TArray<UDreamWidget*>& InWidgets);
	/** The companion behaviour of the prefab these widgets belong to, whose variables the delete would strand. */
	static UDreamUIBehaviour* FindCompanionForWidgets(const TArray<UDreamWidget*>& InWidgets);
	/**
	 * UMG's ShouldContinueDeleteOperation. A deleted widget takes its bound variable's target with
	 * it: the variable is authored on the companion, not generated from the widget, so it stays on
	 * the blueprint holding nothing and everything still compiles -- the loss surfaces at runtime,
	 * with nothing left to name the widget that went away.
	 */
	static bool ShouldContinueDeleteOperation(const TArray<UDreamWidget*>& InWidgets);
	static void DeleteWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction, EDeleteWidgetWarningType WarningType = EDeleteWidgetWarningType::DeleteSilently);
	static void CutWidgets(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanDuplicateWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanCopyWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanPasteWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction);
	static bool CanCutWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	static bool CanDeleteWidget(TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArrayFunction);
	
	static void RefreshLoadedWidget();
	static bool CanCheckNestedOverrideParameter(TFunction<UDreamWidget*()> GetSelectedWidgetFunction);
	static bool CanCreateWidget(TFunction<UDreamWidget*()> GetSelectedWidgetFunction);
	static bool IsWidgetCompatibleWithDreamUIToolsMenu(UDreamWidget* InWidget);

};
