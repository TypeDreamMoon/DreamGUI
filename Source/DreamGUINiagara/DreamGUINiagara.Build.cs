// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Niagara particles drawn by DreamGUI: a direct-mesh visual that owns a Niagara component it simulates and never renders,
 * reads the CPU particle data after each tick and writes it into the canvas's sections, one segment per renderer. It loads
 * at Default, after Niagara, and the core knows nothing of it.
 */
public class DreamGUINiagara : ModuleRules
{
	public DreamGUINiagara(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Same debugging setup as the core: unoptimized, one translation unit per file, in a Debug editor.
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
				"DreamGUIRenderer",//the vertex, the index and the blend mode the segments are written in
				"Niagara",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"NiagaraCore",
				"RenderCore",
			});
	}
}
