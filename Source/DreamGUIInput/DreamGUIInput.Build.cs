// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The input system: the event systems and their preset actors, the raycasters, the input modules and the
 * pointer policy, the action router, navigation (scopes, the stack, the virtual cursor), drag and drop,
 * tooltips and modals, the selectable base the controls are built on, and the game viewport client that
 * brings typed characters in. The core asks it for anything through UDreamUIInputServices, which
 * UDreamUIInputSubsystem implements here.
 */
public class DreamGUIInput : ModuleRules
{
	public DreamGUIInput(ReadOnlyTargetRules Target) : base(Target)
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
				"InputCore",
				"EnhancedInput",//the Enhanced Input event system actor
				"ApplicationCore",//cursors, the input device mapper
				"Slate",
				"SlateCore",
				"DeveloperSettings",//UDreamGUISettings, read for the event system to spawn
				"DreamGUI",
				"DreamTween",
			});

		if (Target.Type == TargetType.Editor)
		{
			// The navigation arrows the input subsystem draws over the editor viewport.
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
