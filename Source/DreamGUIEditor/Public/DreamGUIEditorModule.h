// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"

class UDreamWidget;
class FToolBarBuilder;
class FMenuBuilder;
class FDreamWidgetBlueprintEditor;
DECLARE_LOG_CATEGORY_EXTERN(DreamGUIEditor, Log, All);

/**
 * One project-side addition to the prefab editor's widget context menu, registered on the module's
 * extensibility manager. The menu is built inside the plugin, so without this a project has to
 * fork the hierarchy view to put an entry of its own on it.
 */
class IDreamUIWidgetContextMenuExtension
{
public:
	virtual ~IDreamUIWidgetContextMenuExtension() = default;
	virtual void ExtendWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FDreamWidgetBlueprintEditor> InDesigner)const = 0;
};

/** The registered widget-context-menu extensions, UMG's FWidgetContextMenuExtensibilityManager shape. */
class FDreamUIWidgetContextMenuExtensibilityManager
{
public:
	void AddExtension(const TSharedRef<IDreamUIWidgetContextMenuExtension>& InExtension)
	{
		if (ensure(!Extensions.Contains(InExtension)))
		{
			Extensions.Add(InExtension);
		}
	}
	void RemoveExtension(const TSharedRef<IDreamUIWidgetContextMenuExtension>& InExtension)
	{
		const int32 NumRemoved = Extensions.RemoveSingleSwap(InExtension);
		ensure(NumRemoved == 1);
	}
	const TArray<TSharedPtr<IDreamUIWidgetContextMenuExtension>>& GetExtensions()const{return Extensions;}
private:
	TArray<TSharedPtr<IDreamUIWidgetContextMenuExtension>> Extensions;
};

class FDreamGUIEditorModule : public IModuleInterface, public FGCObject, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:

	static const FName DreamUIDynamicSpriteAtlasViewerTabName;
	static const FName DreamUIWidgetInspectorTabName;

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FDreamGUIEditorModule& Get();
	
	TSharedRef<SWidget> MakeEditorToolsMenu(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, TFunction<void(FMenuBuilder&)> ExtendEditMenuFunction);
	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<TSharedPtr<class FAssetTypeActions_Base>> AssetTypeActionsArray;

	const FSlateBrush* GetInteractionIconBrush(UDreamWidget* Widget);
	const FSlateBrush* GetWidgetIconBrush(UDreamWidget* Widget);

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager()override{return MenuExtensibilityManager;}
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager()override{return ToolBarExtensibilityManager;}
	TSharedPtr<FDreamUIWidgetContextMenuExtensibilityManager> GetWidgetContextMenuExtensibilityManager()const{return WidgetContextMenuExtensibilityManager;}
	/** Run every registered extension, so the menu that hosts them needs a single line. */
	void ExtendWidgetContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FDreamWidgetBlueprintEditor> InDesigner)const
	{
		for (const TSharedPtr<IDreamUIWidgetContextMenuExtension>& Extension : WidgetContextMenuExtensibilityManager->GetExtensions())
		{
			if (Extension.IsValid())Extension->ExtendWidgetContextMenu(MenuBuilder, InDesigner);
		}
	}
private:
	// Built with the module object rather than in StartupModule: an extension can be registered as
	// soon as the module is loadable, which is not necessarily after DreamGUIEditor's own startup ran.
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager = MakeShared<FExtensibilityManager>();
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();
	TSharedPtr<FDreamUIWidgetContextMenuExtensibilityManager> WidgetContextMenuExtensibilityManager = MakeShared<FDreamUIWidgetContextMenuExtensibilityManager>();

	bool IsValidClassName(const FString& InName);

	void CreateUIElementSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction);
	void CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction);
	void CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction);

private:
	TSharedRef<SDockTab> HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> HandleSpawnDreamUIInspectorTab(const FSpawnTabArgs& SpawnTabArgs);
	
	FDelegateHandle SequenceEditorHandle;
	FDelegateHandle OnInitializeSequenceHandle;
	static void OnInitializeSequence(class UDreamUIPrefabSequence* Sequence);
	FDelegateHandle DreamUIMaterialTrackEditorCreateTrackEditorHandle;
	FDelegateHandle DreamUIAnimEventTrackEditorCreateTrackEditorHandle;
	FDelegateHandle DreamUISequenceTrackEditorCreateTrackEditorHandle;
	TObjectPtr<class USequencerSettings> DreamUIPrefabSequencerSettings = nullptr;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
};
