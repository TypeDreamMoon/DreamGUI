// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The editor target of the DreamGUI test host -- the one the test runner builds and launches.
 *
 * SHARED BUILD ENVIRONMENT is the point of this file. An editor target is modular, and a modular
 * target shares the engine's build products: the engine modules already compiled into
 * Engine/Binaries/Win64 are linked against as they are, and only this project's module and its
 * project plugin (DreamGUI, whose Binaries and Intermediate live in its own worktree) are compiled.
 * That is already the default (TargetRules.BuildEnvironment answers Shared for any non-monolithic,
 * non-program target); it is written out so that nobody "fixes" it to Unique, which would compile the
 * entire engine a second time into this project.
 *
 * It also means this file must not set anything UnrealBuildTool marks [RequiresUniqueBuildEnvironment]:
 * under a shared environment such a setting is not ignored, it is a build error. The settings below are
 * DevTest's (DevTestEditor.Target.cs) line for line, apart from the explicit environment -- the host
 * builds under exactly the rules the project it stands in for builds under, so that the two agree on
 * every engine module being up to date and neither rebuilds the other's.
 */
public class DreamGUITestHostEditorTarget : TargetRules
{
	public DreamGUITestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		ExtraModuleNames.Add("DreamGUITestHost");
	}
}
