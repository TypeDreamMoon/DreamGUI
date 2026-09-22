// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

/*
 * Every automated test in the plugin lives here, in a module the shipped runtime and editor DLLs do
 * not contain. A test that sits next to the code it tests can reach anything, which makes it a poor
 * witness: it passes on symbols no caller outside the module could ever link. Moving the tests out
 * forces the production modules to export what they claim is usable, and it keeps the test-only
 * UCLASSes (counters, fake triggers, probe actors) out of every cooked build.
 *
 * The module is Editor rather than UncookedOnly because every test carries EditorContext and a good
 * share of them touch UnrealEd, which an uncooked game target cannot link.
 */
public class DreamGUITests : ModuleRules
{
	public DreamGUITests(ReadOnlyTargetRules Target) : base(Target)
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

		PrivateIncludePaths.AddRange(
			new string[]
			{
				// The shared fixtures are included by their bare file name, the way they were when
				// they sat beside the tests.
				Path.Combine(ModuleDirectory, "Private", "Fixtures"),
				// Tests that were written against implementation detail keep working: the private
				// headers stay private -- nothing here asks for them to be promoted to Public --
				// and only this module is told where they live.
				Path.Combine(ModuleDirectory, "..", "DreamGUI", "Private"),
				Path.Combine(ModuleDirectory, "..", "DreamGUIEditor", "Private"),
				Path.Combine(ModuleDirectory, "..", "DreamGUIK2Nodes", "Private"),
				Path.Combine(ModuleDirectory, "..", "DreamTween", "Private"),
			});

		// The union of what the four modules under test depend on, plus the four themselves. A test
		// compiles against the same headers its subject does, so anything the subject's public
		// surface names has to be reachable from here too.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AppFramework",
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"AutomationController",
				"AutomationDriver",
				"BlueprintGraph",
				"ClassViewer",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DeveloperSettings",
				"DirectoryWatcher",
				"DreamGUI",
				"DreamGUIEditor",
				"DreamGUIK2Nodes",
				"DreamTween",
				"EditorFramework",
				"EditorStyle",
				"EditorWidgets",
				"EnhancedInput",
				"Engine",
				"FieldNotification",
				"ImageWrapper",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Kismet",
				"KismetCompiler",
				"KismetWidgets",
				"LevelEditor",
				"LevelSequence",
				"MessageLog",
				"MovieScene",
				"MovieSceneTools",
				"MovieSceneTracks",
				"PlacementMode",
				"Projects",
				"PropertyEditor",
				"RenderCore",
				"Renderer",
				"RHI",
				"SceneOutliner",
				"Sequencer",
				"Slate",
				"SlateCore",
				"SubobjectEditor",
				"TargetPlatform",
				"ToolMenus",
				"ToolWidgets",
				"TypedElementFramework",
				"TypedElementRuntime",
				"UElibPNG",
				"UMG",
				"UniversalObjectLocator",
				"UnrealEd",
				"XmlParser",
				"zlib",
			});

		// One test reads glyph indices straight out of a FreeType face to check what the shaper
		// produced against the library's own answer, so it needs the headers rather than just the
		// WITH_FREETYPE define DreamGUI publishes.
		if (Target.Type != TargetType.Server && Target.bCompileFreeType)
		{
			PublicDependencyModuleNames.Add("FreeType2");
		}

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			});
	}
}
