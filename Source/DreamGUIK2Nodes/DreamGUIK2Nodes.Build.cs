// Copyright 2019-Present LexLiu. All Rights Reserved.

using UnrealBuildTool;

public class DreamGUIK2Nodes : ModuleRules
{
	public DreamGUIK2Nodes(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "DreamGUI",
                "MovieScene",
                "KismetWidgets",
                "BlueprintGraph",
                "KismetCompiler",
                "Kismet",
            }
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{

            }
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				
			}
			);

    }
}
