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

class LGUIEDITOR_API LGUIEditorTools
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
	static void CreateLexWidget(FString Name, UClass* VisualClass, TFunction<void(class ULexWidget*)> Callback);
	static void CreateEmptyActor();
	static void CreateUIControls(FString InPrefabPath);
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
	static bool CanCopyWidgetReference();
	static void CopyReference_Widget();
	static void CopyReference_Visual();
	static void CopyReference_Layout();
	static bool CanCopyComponentReference();
	static void CopyReference_Component(UActorComponent* Comp);
	static bool CanCopyActorReference();
	static void CopyReference_Actor();
	static void CreateScreenSpaceUI_BasicSetup();
	static void CreateWorldSpaceUIBuiltinRenderer_BasicSetup();
	static void CreateWorldSpaceUILexUIRenderer_BasicSetup();
	static void CreatePresetEventSystem_BasicSetup(bool WorldSpace);
	static bool CreateTraceChannel_BasicSetup(ETraceTypeQuery& OutTraceTypeQuery);
	static void AttachComponentToSelectedActor(TSubclassOf<UActorComponent> InComponentClass);
	static UWorld* GetWorldFromSelection();
	static void CreatePrefabAsset();
	static void RefreshLevelLoadedPrefab(ULGUIPrefab* InPrefab);
	static void RefreshOpenedPrefabEditor(ULGUIPrefab* InPrefab);
	static void RefreshOnSubPrefabChange(ULGUIPrefab* InSubPrefab);
	static TArray<ULGUIPrefab*> GetAllPrefabArray();
	static void UnpackPrefab();
	static void SelectPrefabAsset();
	static void OpenPrefabAsset();
	static void UpdateLevelPrefab();
	static void ToggleLevelPrefabAutoUpdate();
	static void CleanupPrefabsInWorld(UWorld* World);
	static ULGUIPrefabHelperObject* GetPrefabHelperObject_WhichManageThisActor(AActor* InActor);
	static bool IsSelectUIActor();
	static bool IsCanvasActor(AActor* InActor);
	static int GetDrawcallCount(AActor* InActor);
	static void FocusToScreenSpaceUI();
	static void FocusToSelectedUI();
	static bool IsActorCompatibleWithLGUIToolsMenu(AActor* InActor);

	static TMap<FString, TWeakObjectPtr<class ULGUIPrefab>> CopiedActorPrefabMap;//map ActorLabel to prefab
	static TWeakObjectPtr<class UActorComponent> CopiedComponent;
	static bool HaveValidCopiedActors();
	static bool HaveValidCopiedComponent();

	static void MakeCurrentLevel(AActor* InActor);

	static void ForceGC();
};
