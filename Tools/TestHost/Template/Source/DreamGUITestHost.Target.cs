// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The game target of the DreamGUI test host.
 *
 * The test runner never builds this one: it only ever builds DreamGUITestHostEditor, because every
 * automated test carries EditorContext. The packaged text smoke test does, in Development and
 * Shipping, to cook and run the host as a game (Tools/TestHost/README.md); and it keeps the host an
 * ordinary, complete project whose project files generate.
 *
 * Be aware of what building it costs. A game target against a source engine is monolithic, and a
 * monolithic target defaults to a UNIQUE build environment (TargetRules.BuildEnvironment), which means
 * the whole engine is compiled again into this project's own Intermediate folder.
 *
 * The three settings are DevTest's (DevTest.Target.cs), which are the Blank template's: the host has to
 * build under exactly the rules the project it stands in for builds under.
 */
public class DreamGUITestHostTarget : TargetRules
{
	public DreamGUITestHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("DreamGUITestHost");
	}
}
