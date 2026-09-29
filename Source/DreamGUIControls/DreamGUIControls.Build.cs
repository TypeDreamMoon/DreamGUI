// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The control library: the Controls/ family, the Interaction/UI* behaviours (button, toggle, slider,
 * scrollbar, scroll and list views, dropdown, text input, hyperlink, progress bar), the action bar, the
 * each adapter and the UMG interop. Built on the core; the core reaches the list views through the each
 * handler this module registers, and nothing else in it.
 */
public class DreamGUIControls : ModuleRules
{
	public DreamGUIControls(ReadOnlyTargetRules Target) : base(Target)
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
				"RHI",//UMG interop: the widget renderer
				"RenderCore",
				"InputCore",
				"ApplicationCore",//UITextInput: the virtual keyboard
				"Slate",
				"SlateCore",
				"UMG",//UMG interop
				"FieldNotification",
				"DreamGUI",
				"DreamGUIInput",//UUISelectable, which the UI* behaviours derive from; navigation, modals, drag and drop
				"DreamTween",
			});
	}
}
