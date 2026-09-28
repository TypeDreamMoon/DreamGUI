// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI.h"
#include "Animation/DreamUIMovieScenePropertyAccessors.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/DreamUIRender/DreamUIRendererSettings.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "ShaderCore.h"
#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

#define LOCTEXT_NAMESPACE "FDreamGUIModule"

DEFINE_LOG_CATEGORY(DreamGUI);

namespace
{
	FDelegateHandle GPostEngineInitHandle;
}

void FDreamGUIModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	//
	// First, because the .dui lookups read it and this module loads before anything compiles a .dui:
	// the core's own types are the first place a short type name is looked for.
	DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUI"));

	//
	// Two things the one-liner this replaces assumed. FindPlugin returns a TSharedPtr, and while this
	// module belongs to the plugin it looks up, a dereference is not the way to say so. And the
	// mapping is process-wide and permanent -- the engine has no per-directory unregister, so
	// ShutdownModule cannot undo it -- while AddShaderSourceDirectoryMapping check()s that the
	// virtual directory is not mapped yet. A second StartupModule (legacy hot reload, or the plugin
	// being disabled and re-enabled in a running editor; Live Coding does not re-run this) therefore
	// crashed on a mapping that was already correct. AllShaderSourceDirectoryMappings is the engine's
	// own read side of that map.
	static const TCHAR* const DreamGUIVirtualShaderDirectory = TEXT("/Plugin/DreamGUI");
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI")))
	{
		if (!AllShaderSourceDirectoryMappings().Contains(DreamGUIVirtualShaderDirectory))
		{
			AddShaderSourceDirectoryMapping(DreamGUIVirtualShaderDirectory, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
		}
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d The DreamGUI plugin is not registered with the plugin manager, so its shaders cannot be mapped to %s."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, DreamGUIVirtualShaderDirectory);
	}

	// The renderer takes the project's settings from here rather than reading UDreamUISettings itself: the
	// settings object is the core's. Asked per view, so it reads the settings object as it is at that moment.
	DreamUIRendererSettings::SetProvider([]()
	{
		FDreamUIRendererSettings Settings;
		if (const UDreamUISettings* DreamUISettings = GetDefault<UDreamUISettings>())
		{
			Settings.MSAASampleCount = DreamUISettings->AntiAliasingMethod == EDreamUIRendererAntiAliasingMethod::MSAA
				? static_cast<uint8>(DreamUISettings->MSAASampleCount) : 1;
			Settings.bFrustumCulling = DreamUISettings->bFrustumCulling;
		}
		Settings.ViewExtensionPriority = UDreamUISettings::GetPriorityInSceneViewExtension();
		return Settings;
	});
#if WITH_EDITOR
	// Screen-space UI does not draw while the editor simulates; the renderer does not link the editor engine
	// to find that out.
	FDreamUIRenderer::SetSimulatingInEditorQuery([]()
	{
		const UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
		return Editor != nullptr && Editor->bIsSimulatingInEditor;
	});
#endif

	// This module loads at PostConfigInit, before the sequencer's component registry is a safe
	// thing to touch; the accessors wait for the engine. A late load (a plugin enabled at runtime)
	// finds the engine already up and registers on the spot.
	if (GEngine != nullptr)
	{
		DreamUI::EnsureMovieScenePropertyAccessorsRegistered();
	}
	else
	{
		GPostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
		{
			DreamUI::EnsureMovieScenePropertyAccessorsRegistered();
		});
	}
}

void FDreamGUIModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	if (GPostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(GPostEngineInitHandle);
		GPostEngineInitHandle.Reset();
	}
	DreamUIRendererSettings::SetProvider(nullptr);
#if WITH_EDITOR
	FDreamUIRenderer::SetSimulatingInEditorQuery(nullptr);
#endif
	FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUI"));
	DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUI"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamGUIModule, DreamGUI)
