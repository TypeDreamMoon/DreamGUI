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
 * under a shared environment such a setting is not ignored, it is a build error. The build settings and
 * include order are each engine's latest (on the newest engine, what a working project created from the
 * Blank template has), so that the one host template builds on every engine the plugin supports and, where
 * it shares an engine with a working project, agrees with it on every engine module being up to date.
 */
public class DreamGUITestHostEditorTarget : TargetRules
{
	public DreamGUITestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		ExtraModuleNames.Add("DreamGUITestHost");
	}
}
