// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI.h"
#include "Animation/DreamUIMovieScenePropertyAccessors.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FDreamGUIModule"

DEFINE_LOG_CATEGORY(DreamGUI);

namespace
{
	FDelegateHandle GPostEngineInitHandle;
}

void FDreamGUIModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("DreamGUI"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/DreamGUI"), PluginShaderDir);

	// This module loads at PostConfigInit, before the sequencer's component registry is a safe
	// thing to touch; the accessors wait for the engine. A late load (a plugin enabled at runtime)
	// finds the engine already up and registers on the spot.
	if (GEngine != nullptr)
	{
		DreamUI::EnsureMovieScenePropertyAccessorsRegistered();
	}
	else
	{
		GPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([]()
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
		FCoreDelegates::OnPostEngineInit.Remove(GPostEngineInitHandle);
		GPostEngineInitHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamGUIModule, DreamGUI)
