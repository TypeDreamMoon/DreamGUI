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
                });

        // msdfgen, for the glyph distance fields (DreamGlyphSdf.cpp). The engine's own copy is not there
        // to include: upstream generates its single-file pair at build time rather than committing it, so
        // Engine/Source/ThirdParty/msdfgen holds only msdfgen.tps in a launcher (installed) engine and the
        // pair itself only ever appears in a source build. The plugin therefore carries its own generated
        // copy -- see ThirdParty/README.md -- and only that folder goes on the include path, so the
        // rasteriser's #include "msdfgen.cpp" cannot resolve to some other copy of the library.
        PrivateIncludePaths.Add(System.IO.Path.Combine(PluginDirectory, "ThirdParty", "msdfgen-single-file"));

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
            // Text shaping. WITH_HARFBUZZ comes from the engine's HarfBuzz module and is what the
            // shaping code actually keys off (DreamTextShaper.cpp, DreamUIFontData_FreeTypeRender.cpp),
            // always paired with WITH_FREETYPE where a face is involved. No header of ours forks on it,
            // so the class layout dependent modules see does not change with it -- which is why the
            // DREAMGUI_WITH_HARFBUZZ mirror that used to be published here is gone: nothing in the
            // plugin ever read it, and it disagreed with WITH_HARFBUZZ whenever bCompileFreeType was
            // off while HarfBuzz still compiled.
            AddEngineThirdPartyPrivateStaticDependencies(Target, "HarfBuzz");
            // The engine's HarfBuzz takes its Unicode functions from ICU, so the static lib needs it too.
            if (Target.bCompileICU)
            {
                AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
            }
        }
        else
        {
            // A dedicated server draws no text, so there is no FreeType to measure with: every glyph
            // comes back zero-sized and a whole paragraph measures zero by zero. That is acceptable for
            // a server but it used to be silent; UDreamUIFontData_FreeTypeRender::GetCharData now says
            // it once. WITH_HARFBUZZ is spelled out because the HarfBuzz module -- the only other thing
            // that defines it, to this same 0 on a platform it does not support -- is not linked here,
            // and #if on an undefined macro is not something to leave to the compiler's mood.
            PublicDefinitions.Add("WITH_FREETYPE=0");
            PublicDefinitions.Add("WITH_HARFBUZZ=0");
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
                "JsonUtilities",//kept with Json; the prefab save round-trip that needed it went with the prefab asset model
            }
            );
        }

        //PublicDefinitions.Add("LEXUI_USE_32BIT_INDEXBUFFER");//uncommet this line to use 32-bit index buffer
    }
}
