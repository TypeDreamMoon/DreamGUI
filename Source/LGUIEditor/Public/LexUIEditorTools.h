// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#pragma once
class ULexWidget;
class ULexUIPrefabHelperObject;
class ULexUIPrefab;

DECLARE_MULTICAST_DELEGATE_OneParam(FEditingPrefabChangedDelegate, AActor*);
DECLARE_MULTICAST_DELEGATE_OneParam(FBeforeApplyPrefabDelegate, ULexUIPrefabHelperObject*);

class LGUIEDITOR_API FLexUIEditorTools
{
private:
	static FString PrevSavePrefabFolder;
public:
	static FString LexUIPresetPrefabPath;
	static FEditingPrefabChangedDelegate OnEditingPrefabChanged;
	static FBeforeApplyPrefabDelegate OnBeforeApplyPrefab;
	static AActor* GetFirstSelectedActor();
	static FString GetUniqueNumericName(const FString& InPrefix, const TArray<FString>& InExistNames);
	static FString GetNameForNewWidget(ULexWidget* InParentWidget, const FString& InBaseName);
	static FString GetNamePrefixForCopy(const FString& InSrcName, FString& OutNumericSuffix);
	static TArray<AActor*> GetRootActorListFromSelection(const TArray<AActor*>& selectedActors);
	static void CreateLexWidget(TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* VisualClass, TFunction<void(class ULexWidget*)> Callback);
	static void CreateEmptyActor(TFunction<AActor*()> GetSelectedActorFunction);
	static void CreateUIControls(TFunction<AActor*()> GetSelectedActorFunction, FString InPrefabPath);
	static void DuplicateActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static void CopyActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static void PasteActors(TFunction<TArray<AActor*>()> GetSelectedActorFunction);
	static void DeleteActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static void CutActors(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static void ToggleSelectedActorsSpatiallyLoaded(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static ECheckBoxState GetActorsSpatiallyLoadedProperty(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static bool CanDuplicateActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static bool CanCopyActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static bool CanPasteActor(TFunction<AActor*()> GetSelectedActorFunction);
	static bool CanCutActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static bool CanDeleteActor(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	static bool CanToggleActorsSpatiallyLoaded(TFunction<TArray<AActor*>()> GetSelectedActorArrayFunction);
	
	static bool CanCreatePrefab(TFunction<AActor*()> GetSelectedActorFunction);
	static void CreatePrefabAsset(TFunction<AActor*()> GetSelectedActorFunction);
	static void RefreshLevelLoadedPrefab();
	static void RefreshOpenedPrefabEditor(ULexUIPrefab* InPrefab);
	static void RefreshOnSubPrefabChange(ULexUIPrefab* InSubPrefab);
	static TArray<ULexUIPrefab*> GetAllPrefabArray();
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
	static void CleanupPrefabsInWorld(UWorld* World);
	static bool IsActorCompatibleWithLexUIToolsMenu(AActor* InActor);

	static TMap<FString, TWeakObjectPtr<ULexUIPrefab>> CopiedActorPrefabMap;//map ActorLabel to prefab
	static bool HaveValidCopiedActors();

	static void MakeCurrentLevel(AActor* InActor);
};
