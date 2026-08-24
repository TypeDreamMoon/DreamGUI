// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

using UnrealBuildTool;

public class DreamGUIEditor : ModuleRules
{
	public DreamGUIEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        
        // disable optimize in editor and debug-build
        if (Target.bBuildEditor && Target.Configuration == UnrealTargetConfiguration.Debug)
        {
	        OptimizeCode = CodeOptimization.Never;
			
	        //(optional) enable debug symbol
	        bUseUnity = false;
	        bUseRTTI = true;
	        bEnableExceptions = true;
        }
        else
        {
	        OptimizeCode = CodeOptimization.Default;
        }

        string EnginSourceFolder = EngineDirectory + "/Source/";
        PrivateIncludePaths.AddRange(
                new string[] {
                    EnginSourceFolder + "/Editor/DetailCustomizations/Private",
                });

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "CoreUObject",
                "Slate",
                "SlateCore",
                "Engine",
                "UnrealEd",
                "PropertyEditor",
                "RenderCore",
                "RHI",
                "DreamGUI",
                "LevelEditor",
                "Projects",
                "EditorWidgets",
                "DesktopPlatform",//file system
                "ImageWrapper",//texture load
                "InputCore",//STableRow
                "AssetTools",//Asset editor
                "ContentBrowser",//DreamGUI editor
                "SceneOutliner",//DreamGUIPrefab editor, extend SceneOutliner
                "ApplicationCore",//ClipboardCopy
                "KismetCompiler",
                "BlueprintGraph",//UEdGraphSchema_K2 pin categories (Promote to Behaviour Variable)
                "AppFramework",
                //"AssetRegistry",
                //"InputCore",
				// ... add other public dependencies that you statically link with here ...
                
                "Kismet",
                "ToolMenus",//PrefabEditor
                "SubobjectEditor",//PrefabEditor, Actor component panel
                "UMG",//UMGStyle
                "Sequencer",
                "UniversalObjectLocator",
				"MovieScene",
				"MovieSceneTracks",
				"MovieSceneTools",
                "TypedElementFramework",
                "TypedElementRuntime",
                "EditorFramework",
                "PlacementMode",
                "ClassViewer",
                "ToolWidgets",
                "AssetRegistry",
                "MessageLog",
            }
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "EditorStyle",
				// UDreamUIDesignerSettings: the designer's view preferences live in
				// EditorPerProjectUserSettings, not in the prefab asset.
				"DeveloperSettings",
				// ... add private dependencies that you statically link with here ...

            }
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

    }
}
