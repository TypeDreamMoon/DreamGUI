// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

using UnrealBuildTool;

public class DreamGUI : ModuleRules
{
	public DreamGUI(ReadOnlyTargetRules Target) : base(Target)
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
                    EnginSourceFolder + "/Runtime/Renderer/Private",//#include "SceneRendering.h", #include "ScenePrivate.h"
					EnginSourceFolder + "/Runtime/Renderer/Internal",//#include "SceneTextures.h"
                    EnginSourceFolder,//#include "ThirdParty/msdfgen/msdfgen.cpp" (single-file msdfgen, as SlateCore includes it)
                });

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "RHI","RenderCore","Renderer",
                "DreamTween",
                "InputCore",//UITextInput
                "EnhancedInput",//DreamEnhancedInputEventSystemActor
                "DeveloperSettings",//UDreamGUISettings
                "FieldNotification",//UDreamUserWidget implements INotifyFieldValueChanged
                //"FreeType2",
                "UElibPNG",
                "zlib",
                "ApplicationCore",//UITextInput/RequiresVirtualKeyboard, debug
                "Projects",
                "MovieScene",
                "LevelSequence",
                "UniversalObjectLocator",
                "MovieSceneTracks",
                "UMG",
				// ... add other public dependencies that you statically link with here ...
            }
            );
		if(Target.Type != TargetType.Server)
        {
            if (Target.bCompileFreeType)
            {
                PublicDependencyModuleNames.Add("FreeType2");
                //AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2");
                PublicDefinitions.Add("WITH_FREETYPE=1");
            }
            else
            {
                PublicDefinitions.Add("WITH_FREETYPE=0");
            }
            // Text shaping. The HarfBuzz module defines WITH_HARFBUZZ privately for our own TUs; our
            // public headers key off DREAMGUI_WITH_HARFBUZZ so dependent modules see the same class layout.
            AddEngineThirdPartyPrivateStaticDependencies(Target, "HarfBuzz");
            // The engine's HarfBuzz takes its Unicode functions from ICU, so the static lib needs it too.
            if (Target.bCompileICU)
            {
                AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
            }
            PublicDefinitions.Add("DREAMGUI_WITH_HARFBUZZ=" + (Target.bCompileFreeType ? "1" : "0"));
        }
        else
        {
            PublicDefinitions.Add("WITH_FREETYPE=0");
            PublicDefinitions.Add("WITH_HARFBUZZ=0");
            PublicDefinitions.Add("DREAMGUI_WITH_HARFBUZZ=0");
        }
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"XmlParser",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UnrealEd",
                "EditorStyle",
                "TargetPlatform",
                "LevelEditor",
                "ToolWidgets",//SCustomDialog
                "Json",
                "JsonUtilities",//prefab save round-trip verification
            }
            );
        }

        //PublicDefinitions.Add("LEXUI_USE_32BIT_INDEXBUFFER");//uncommet this line to use 32-bit index buffer
    }
}
