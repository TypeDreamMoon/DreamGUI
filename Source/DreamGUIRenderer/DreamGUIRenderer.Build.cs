// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The renderer: the scene view extension that draws DreamUI (FDreamUIRenderer), its shaders, vertex and
 * index formats, the post-process proxies and the render-thread primitive contract. It sits below the core
 * and knows nothing of widgets: the core registers what it needs from it -- a view source per screen-space
 * root, a settings provider, the simulate-in-editor query -- and the renderer asks through those.
 */
public class DreamGUIRenderer : ModuleRules
{
	public DreamGUIRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Same debugging setup as the core: unoptimized, one translation unit per file, in a Debug editor.
		if (Target.bBuildEditor && Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			OptimizeCode = CodeOptimization.Never;
			bUseUnity = false;
		}

		// No include path into the engine renderer's Private or Internal folders: the scene depth and the view
		// rectangle come through its public API (SceneRenderTargetParameters.h, FXRenderingUtils.h), which is
		// what keeps an engine upgrade from breaking this module in a header nobody promised to keep.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"RenderCore",
				"Renderer",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",//the plugin's base directory, for the shader directory mapping
			});
	}
}
