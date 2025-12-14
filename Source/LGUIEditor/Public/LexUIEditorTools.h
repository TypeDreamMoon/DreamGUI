// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#pragma once
class ULexWidget;
class ULGUIPrefabHelperObject;
class ULGUIPrefab;

DECLARE_MULTICAST_DELEGATE_OneParam(FEditingPrefabChangedDelegate, AActor*);
DECLARE_MULTICAST_DELEGATE_OneParam(FBeforeApplyPrefabDelegate, ULGUIPrefabHelperObject*);

class LGUIEDITOR_API FLexUIEditorTools
{
private:
	static FString PrevSavePrefabFolder;
public:
	static FString LGUIPresetPrefabPath;
	static FEditingPrefabChangedDelegate OnEditingPrefabChanged;
	static FBeforeApplyPrefabDelegate OnBeforeApplyPrefab;
	static AActor* GetFirstSelectedActor();
	static TArray<AActor*> GetSelectedActors();
	static FString GetUniqueNumericName(const FString& InPrefix, const TArray<FString>& InExistNames);
	static FString GetNameForNewWidget(ULexWidget* InParentWidget, const FString& InBaseName);
	static FString GetNamePrefixForCopy(const FString& InSrcName, FString& OutNumericSuffix);
	static TArray<AActor*> GetRootActorListFromSelection(const TArray<AActor*>& selectedActors);
	static void CreateActorByClass(UClass* ActorClass, TFunction<void(AActor*)> Callback);
	static void CreateLexWidget(TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* VisualClass, TFunction<void(class ULexWidget*)> Callback);
	static void CreateEmptyActor(TFunction<AActor*()> GetSelectedActorFunction);
	static void CreateUIControls(TFunction<AActor*()> GetSelectedActorFunction, FString InPrefabPath);
	static void ReplaceActorByClass(UClass* ActorClass);
	static void DuplicateSelectedActors_Impl();
	static void CopySelectedActors_Impl();
	static void PasteSelectedActors_Impl();
	static void DeleteSelectedActors_Impl();
	static void CutSelectedActors_Impl();
	static void ToggleSelectedActorsSpatiallyLoaded_Impl();
	static ECheckBoxState GetActorSpatiallyLoadedProperty();
	static void DeleteActors_Impl(const TArray<AActor*>& InActors);
	static bool CanDuplicateActor();
	static bool CanCopyActor();
	static bool CanPasteActor();
	static bool CanCutActor();
	static bool CanDeleteActor();
	static bool CanToggleActorSpatiallyLoaded();
	static void CopyComponentValues_Impl();
	static void PasteComponentValues_Impl();
	static void OpenAtlasViewer_Impl();
	static void CreateScreenSpaceUI_BasicSetup();
	static void CreateWorldSpaceUIBuiltinRenderer_BasicSetup();
	static void CreateWorldSpaceUILexUIRenderer_BasicSetup();
	static void CreatePresetEventSystem_BasicSetup(bool WorldSpace);
	static class ULexWorldSpaceRaycasterSource* CreatePresetWorldSpaceRaycasterSource();
	static void AttachComponentToSelectedActor(TSubclassOf<UActorComponent> InComponentClass);
	static UWorld* GetWorldFromSelection();
	
	static bool CanCreatePrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void CreatePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction);
	static void RefreshLevelLoadedPrefab(ULGUIPrefab* InPrefab);
	static void RefreshOpenedPrefabEditor(ULGUIPrefab* InPrefab);
	static void RefreshOnSubPrefabChange(ULGUIPrefab* InSubPrefab);
	static TArray<ULGUIPrefab*> GetAllPrefabArray();
	static bool CanUnpackActorForPrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void UnpackPrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void SelectPrefabAsset(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanBrowsePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction);
	static void OpenPrefabAsset(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanUpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void UpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static ECheckBoxState GetAutoUpdateLevelPrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void ToggleLevelPrefabAutoUpdate(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanCheckPrefabOverrideParameter(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanCreateActor(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanReplaceActor(TFunction<AActor*()> GetSelectedActorFunction);
	static void CleanupPrefabsInWorld(UWorld* World);
	static bool IsSelectUIActor();
	static bool IsCanvasActor(AActor* InActor);
	static int GetDrawcallCount(AActor* InActor);
	static void FocusToScreenSpaceUI();
	static void FocusToSelectedUI();
	static bool IsActorCompatibleWithLexUIToolsMenu(AActor* InActor);

	static TMap<FString, TWeakObjectPtr<class ULGUIPrefab>> CopiedActorPrefabMap;//map ActorLabel to prefab
	static TWeakObjectPtr<class UActorComponent> CopiedComponent;
	static bool HaveValidCopiedActors();
	static bool HaveValidCopiedComponent();

	static void MakeCurrentLevel(AActor* InActor);

	static void ForceGC();
};
