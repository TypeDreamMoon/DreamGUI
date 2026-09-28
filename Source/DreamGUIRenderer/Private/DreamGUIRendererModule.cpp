// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIRendererLogging.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FDreamGUIRendererModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The shaders are this module's, so the virtual directory the materials and global shaders name is
		// mapped here, at PostConfigInit, before anything compiles a shader. It stays /Plugin/DreamGUI: the
		// plugin's content materials include /Plugin/DreamGUI/Private/DreamUIShade.ush from Custom nodes.
		//
		// The mapping is process-wide and permanent -- the engine has no per-directory unregister, so
		// ShutdownModule cannot undo it -- while AddShaderSourceDirectoryMapping check()s that the virtual
		// directory is not mapped yet. A second StartupModule (legacy hot reload, or the plugin being
		// disabled and re-enabled in a running editor) must therefore not map it again.
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
			UE_LOG(LogDreamGUIRenderer, Error, TEXT("[%s].%d The DreamGUI plugin is not registered with the plugin manager, so its shaders cannot be mapped to %s."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, DreamGUIVirtualShaderDirectory);
		}
	}
};

IMPLEMENT_MODULE(FDreamGUIRendererModule, DreamGUIRenderer)
