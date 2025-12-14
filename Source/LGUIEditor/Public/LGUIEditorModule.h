// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "PropertyEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "PropertyHandle.h"

class ULexWidget;
class FToolBarBuilder;
class FMenuBuilder;
DECLARE_LOG_CATEGORY_EXTERN(LGUIEditor, Log, All);

class FLGUIEditorModule : public IModuleInterface, public FGCObject
{
public:

	static const FName LGUIDynamicSpriteAtlasViewerName;
	static const FName LGUIPrefabSequenceTabName;

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FLGUIEditorModule& Get();
	
	TSharedRef<SWidget> MakeEditorToolsMenu(bool InitialSetup, bool ComponentAction, bool EditorCameraControl, bool Others, TFunction<AActor*()> GetSelectedActorFunction);
	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<TSharedPtr<class FAssetTypeActions_Base>> AssetTypeActionsArray;
	DECLARE_EVENT(FLGUIEditorModule, FOnHierarchyChanged);
	FOnHierarchyChanged OnHierarchyChanged;

	const FSlateBrush* GetVisualIconBrush(ULexWidget* Widget);
private:

	bool IsValidClassName(const FString& InName);

	void CreateUIElementSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);
	void CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);
	void CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);
	void CreateCommonActorSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);
	void CreateExtraPrefabsSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);
	void BasicSetupSubMenu(FMenuBuilder& MenuBuilder);
	void ReplaceActorSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction);

	void AddEditorToolsToToolbarExtension(FToolBarBuilder& Builder);

	void ToggleLGUIColumnInfo();
	bool IsLGUIColumnInfoChecked();

	void ToggleAnchorTool();
	bool IsAnchorToolChecked();

	void ToggleDrawHelperFrame();
	bool IsDrawHelperFrameChecked();

	void ApplyLGUIColumnInfo(bool value, bool refreshSceneOutliner);
	TWeakObjectPtr<class ULGUIPrefabHelperObject> CurrentPrefabHelperObject;

	TMap<UClass*, const FSlateBrush*> InteractableClassIconMap;
private:
	TSharedRef<SDockTab> HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> HandleSpawnLGUIPrefabSequenceTab(const FSpawnTabArgs& SpawnTabArgs);
	
	FDelegateHandle SequenceEditorHandle;
	FDelegateHandle OnInitializeSequenceHandle;
	FName LGUIPrefabSequenceComponentName;
	static void OnInitializeSequence(class ULGUIPrefabSequence* Sequence);
	FDelegateHandle LGUIMaterialTrackEditorCreateTrackEditorHandle;
	TObjectPtr<class USequencerSettings> LGUIPrefabSequencerSettings = nullptr;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
};