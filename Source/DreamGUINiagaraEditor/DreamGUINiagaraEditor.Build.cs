// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The editor's side of the Niagara visual: the details panel's report of what a system's renderers are drawn as, and the
 * conversion of particle materials into UI materials.
 */
public class DreamGUINiagaraEditor : ModuleRules
{
	public DreamGUINiagaraEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"DreamGUI",
				"DreamGUINiagara",
				"Niagara",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"AssetTools",
				"DreamGUIEditor",//the palette (FDreamUIControlRegistry)
				"DreamGUIRenderer",
				"InputCore",
				"MaterialEditor",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
			});
	}
}
