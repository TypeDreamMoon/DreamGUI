// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamUISourceRoot;

/**
 * The VSCode side door: `DUI/DreamUI.code-workspace`, rewritten from the current source roots and
 * opened in VSCode (falling back to the default editor, then Notepad) -- the same shape
 * DreamShader and DreamFX give their languages. Opening through here rather than a bare folder is
 * what lights up the extension's workspace features: the file names every enabled plugin's DUI/
 * root beside the project's, pins the *.dui association, and recommends the extension.
 */
struct FDreamUIWorkspaceService
{
	/**
	 * The workspace JSON for these roots. Split from the writer so a test can hold it without
	 * touching disk -- this file is read by ANOTHER program (VSCode) and written by nobody else;
	 * a malformed byte would not error anywhere on this side, VSCode would just quietly refuse to
	 * treat it as a workspace.
	 */
	static FString BuildWorkspaceJson(const TArray<FDreamUISourceRoot>& InRoots, const FString& InWorkspaceDirectory);

	/**
	 * Writes `<Project>/DUI/DreamUI.code-workspace`, creating the directory when the project has
	 * none yet -- the button is allowed to be the first thing that ever makes a DUI folder.
	 */
	static bool WriteWorkspaceFile(FString& OutWorkspaceFilePath, FString& OutError);

	/**
	 * Refreshes the symbols export (so completion data is fresh the moment the editor opens),
	 * rewrites the workspace file and opens it: VSCode, else the default editor, else Notepad.
	 */
	static void OpenWorkspace();
};
