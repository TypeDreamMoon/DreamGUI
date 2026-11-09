// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
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
				// The timeline write-back reads a sequence's sampled keys back as the ease that made them, with the same
				// curves the builder sampled (FDreamUITimelineWriteBack).
				"DreamTween",
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

		// DreamLoc, when the project has it (an Optional plugin of DreamGUI.uplugin): the designer lays each language's
		// translation out in the text it is for as it compiles, and `dloc check` asks DreamGUI to do the same
		// (IDreamLocContextProvider). Without it everything else builds and runs as before; WITH_DREAMLOC says which.
		bool bWithDreamLoc = HasDreamLoc(Target);
		PrivateDefinitions.Add("WITH_DREAMLOC=" + (bWithDreamLoc ? "1" : "0"));
		if (bWithDreamLoc)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "DreamLoc", "DreamLocEditor" });
		}
    }

	/**
	 * Whether DreamLoc is there to depend on: a DreamLoc.uplugin under the project's Plugins/ or the engine's
	 * Plugins/Marketplace, and the project not turning it off. What UBT then does with the Optional dependency is the
	 * same answer: the plugin is enabled with DreamGUI exactly when it is found and not disabled.
	 */
	bool HasDreamLoc(ReadOnlyTargetRules Target)
	{
		List<DirectoryReference> Roots = new List<DirectoryReference>();
		if (Target.ProjectFile != null)
		{
			Roots.Add(DirectoryReference.Combine(Target.ProjectFile.Directory, "Plugins"));
		}
		Roots.Add(DirectoryReference.Combine(new DirectoryReference(EngineDirectory), "Plugins", "Marketplace"));
		bool bFound = Roots.Any(Root => FindPlugin(Root, "DreamLoc.uplugin", 3));
		if (!bFound || Target.ProjectFile == null)
		{
			return bFound;
		}
		try
		{
			JsonObject Project = JsonObject.Read(Target.ProjectFile);
			JsonObject[] Plugins;
			if (Project.TryGetObjectArrayField("Plugins", out Plugins))
			{
				foreach (JsonObject Plugin in Plugins)
				{
					string Name;
					bool bEnabled;
					if (Plugin.TryGetStringField("Name", out Name) && Name == "DreamLoc" && Plugin.TryGetBoolField("Enabled", out bEnabled) && !bEnabled)
					{
						return false;
					}
				}
			}
		}
		catch (Exception)
		{
			// A project file UBT itself has read; if it cannot be read here, the plugin's presence decides.
		}
		return true;
	}

	/** The way the engine finds plugins: a folder with a .uplugin is a plugin and is not looked into further. */
	static bool FindPlugin(DirectoryReference InDirectory, string InFileName, int InDepth)
	{
		if (InDepth < 0 || !DirectoryReference.Exists(InDirectory))
		{
			return false;
		}
		if (FileReference.Exists(FileReference.Combine(InDirectory, InFileName)))
		{
			return true;
		}
		if (DirectoryReference.EnumerateFiles(InDirectory, "*.uplugin").Any())
		{
			return false;
		}
		foreach (DirectoryReference Child in DirectoryReference.EnumerateDirectories(InDirectory))
		{
			string Name = Child.GetDirectoryName();
			if (Name.StartsWith(".") || Name == "Intermediate" || Name == "Binaries" || Name == "Saved")
			{
				continue;
			}
			if (FindPlugin(Child, InFileName, InDepth - 1))
			{
				return true;
			}
		}
		return false;
	}
}
