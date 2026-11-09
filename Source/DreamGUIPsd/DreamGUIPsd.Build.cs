// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The layered-document importer: a reader for the layered image format (both of its sizes, every compression it
 * stores pixels with, the descriptors its effects, text, fills and adjustments are written in) and the mapping of a
 * document onto DreamGUI -- a .dui and the textures it names, compiled into a widget Blueprint through the text
 * pipeline the editor already has.
 *
 * UncookedOnly: nothing here runs in a game. The reader itself touches no UObject, so a test can feed it bytes.
 */
public class DreamGUIPsd : ModuleRules
{
	public DreamGUIPsd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		if (Target.bBuildEditor && Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			OptimizeCode = CodeOptimization.Never;
			bUseUnity = false;
		}

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// FMargin, in the mapper's public header.
				"SlateCore",
				// UDreamPsdImportSettings, in a public header.
				"DeveloperSettings",
				"DreamGUI",
				"DreamGUIRenderer",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"ContentBrowserData",
				"DesktopPlatform",
				"DreamGUIEditor",
				"Json",
				"Kismet",
				"Projects",
				"Slate",
				"ToolMenus",
				"UnrealEd",
			});
	}
}
