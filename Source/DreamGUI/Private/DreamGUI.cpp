// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI.h"
#include "Animation/DreamUIMovieScenePropertyAccessors.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRendererSettings.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
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
	// The renderer sits below the core and cannot call into it; its one reflected type, the blend mode, is
	// found by a short name through this list like any other.
	DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUIRenderer"));

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
	DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUIRenderer"));
	DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUI"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamGUIModule, DreamGUI)
