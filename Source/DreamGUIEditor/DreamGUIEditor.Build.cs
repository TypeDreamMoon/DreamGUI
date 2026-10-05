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
                "EditorSubsystem",//UDreamGUIEditorSubsystem
                "PropertyEditor",
                "RenderCore",
                "RHI",
                "DreamGUI",
                "DreamGUIRenderer",
                "DreamGUIControls",
                "DreamGUIInput",
                "DreamGUIExtensions",
                "LevelEditor",
                "Projects",
                "DirectoryWatcher",//FDreamUISourceWatcher
                "Json",//FDreamUISymbolExport
                "FieldNotification",//FDreamUISymbolExport lists the INotifyFieldValueChanged classes (view models)
                "EditorWidgets",
                "DesktopPlatform",//file system
                "ImageWrapper",//texture load
                "InputCore",//STableRow
                "AssetTools",//Asset editor
                "AssetDefinition",//FAssetCategoryPath: the sections of the DreamGUI Add menu (UDreamUIAssetFactory)
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
				// SCulturePicker: the cultures of a font's fallback entries, a text's Language, and the
				// font's "Resolve Sample" language are picked from the engine's own culture list.
				"InternationalizationSettings",
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
