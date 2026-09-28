// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

/*
 * The extensions: 2D lines, polygons and rings, the static-mesh visual, the retainer and render-target
 * helpers, lyrics, the concrete mesh modifiers and the three post-process effects. They build on the core
 * and the core knows nothing of them.
 */
public class DreamGUIExtensions : ModuleRules
{
	public DreamGUIExtensions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Same debugging setup as the core: unoptimized, one translation unit per file, in a Debug editor.
		if (Target.bBuildEditor && Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			OptimizeCode = CodeOptimization.Never;
			bUseUnity = false;
		}

		// The effects' render proxies include the core's post-process proxy header, which includes the
		// engine's SceneTextures.h from Renderer/Internal. Only this module's own sources need it: nothing
		// public here includes that header.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Renderer", "Internal"));

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"RenderCore",
				"Renderer",
				"Slate",
				"SlateCore",
				"DreamGUI",
				"DreamTween",
			});
	}
}
