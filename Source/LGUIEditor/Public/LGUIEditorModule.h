// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"

class ULexWidget;
class FToolBarBuilder;
class FMenuBuilder;
class FLexUIPrefabEditor;
DECLARE_LOG_CATEGORY_EXTERN(LGUIEditor, Log, All);

/**
 * One project-side addition to the prefab editor's widget context menu, registered on the module's
 * extensibility manager. The menu is built inside the plugin, so without this a project has to
 * fork the hierarchy view to put an entry of its own on it.
 */
class ILexUIWidgetContextMenuExtension
{
public:
	virtual ~ILexUIWidgetContextMenuExtension() = default;
	virtual void ExtendWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FLexUIPrefabEditor> InPrefabEditor)const = 0;
};

/** The registered widget-context-menu extensions, UMG's FWidgetContextMenuExtensibilityManager shape. */
class FLexUIWidgetContextMenuExtensibilityManager
{
public:
	void AddExtension(const TSharedRef<ILexUIWidgetContextMenuExtension>& InExtension)
	{
		if (ensure(!Extensions.Contains(InExtension)))
		{
			Extensions.Add(InExtension);
		}
	}
	void RemoveExtension(const TSharedRef<ILexUIWidgetContextMenuExtension>& InExtension)
	{
		const int32 NumRemoved = Extensions.RemoveSingleSwap(InExtension);
		ensure(NumRemoved == 1);
	}
	const TArray<TSharedPtr<ILexUIWidgetContextMenuExtension>>& GetExtensions()const{return Extensions;}
private:
	TArray<TSharedPtr<ILexUIWidgetContextMenuExtension>> Extensions;
};

class FLGUIEditorModule : public IModuleInterface, public FGCObject, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:

	static const FName LexUIDynamicSpriteAtlasViewerTabName;
	static const FName LexUIWidgetInspectorTabName;

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FLGUIEditorModule& Get();
	
	TSharedRef<SWidget> MakeEditorToolsMenu(TFunction<ULexWidget*()> GetSelectedWidgetFunction, TFunction<void(FMenuBuilder&)> ExtendEditMenuFunction);
	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<TSharedPtr<class FAssetTypeActions_Base>> AssetTypeActionsArray;

	const FSlateBrush* GetInteractionIconBrush(ULexWidget* Widget);
	const FSlateBrush* GetWidgetIconBrush(ULexWidget* Widget);

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager()override{return MenuExtensibilityManager;}
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager()override{return ToolBarExtensibilityManager;}
	TSharedPtr<FLexUIWidgetContextMenuExtensibilityManager> GetWidgetContextMenuExtensibilityManager()const{return WidgetContextMenuExtensibilityManager;}
	/** Run every registered extension, so the menu that hosts them needs a single line. */
	void ExtendWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FLexUIPrefabEditor> InPrefabEditor)const
	{
		for (const TSharedPtr<ILexUIWidgetContextMenuExtension>& Extension : WidgetContextMenuExtensibilityManager->GetExtensions())
		{
			if (Extension.IsValid())Extension->ExtendWidgetContextMenu(MenuBuilder, InPrefabEditor);
		}
	}
private:
	// Built with the module object rather than in StartupModule: an extension can be registered as
	// soon as the module is loadable, which is not necessarily after LGUIEditor's own startup ran.
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager = MakeShared<FExtensibilityManager>();
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();
	TSharedPtr<FLexUIWidgetContextMenuExtensibilityManager> WidgetContextMenuExtensibilityManager = MakeShared<FLexUIWidgetContextMenuExtensibilityManager>();

	bool IsValidClassName(const FString& InName);

	void CreateUIElementSubMenu(FMenuBuilder& MenuBuilder, TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	void CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder, TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	void CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder, TFunction<ULexWidget*()> GetSelectedWidgetFunction);
	void CreateExtraPrefabsSubMenu(FMenuBuilder& MenuBuilder, TFunction<ULexWidget*()> GetSelectedActorFunction);

	TWeakObjectPtr<class ULexUIPrefabHelperObject> CurrentPrefabHelperObject;
private:
	TSharedRef<SDockTab> HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> HandleSpawnLexUIInspectorTab(const FSpawnTabArgs& SpawnTabArgs);
	
	FDelegateHandle SequenceEditorHandle;
	FDelegateHandle OnInitializeSequenceHandle;
	static void OnInitializeSequence(class ULexUIPrefabSequence* Sequence);
	FDelegateHandle LexUIMaterialTrackEditorCreateTrackEditorHandle;
	TObjectPtr<class USequencerSettings> LexUIPrefabSequencerSettings = nullptr;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
};
