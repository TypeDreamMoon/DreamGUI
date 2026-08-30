// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * DreamUI's level-editor menu entries, and this plugin's share of the Dream-family combo button.
 *
 * The convention the three plugins (DreamShader, DreamFX, DreamGUI) follow -- each keeps its own
 * copy of the ensure function, shapes shared, packages not:
 *
 *   - ONE toolbar entry for the family: `LevelEditor.LevelEditorToolBar.AssetsToolBar`, section
 *     "DreamTools", combo entry "DreamTools.OpenWorkspaceCombo". Every plugin tries to add it;
 *     a FindEntry check makes that idempotent, so any subset of the three being enabled still
 *     yields exactly one button. The entry is owned by the shared owner name "DreamToolsShared",
 *     which no module ever unregisters -- otherwise unloading whichever plugin happened to win
 *     the race would take the other plugins' door with it.
 *   - The combo opens the shared menu "DreamTools.OpenInVSCode". Each plugin adds its own section
 *     there under its own owner, so ITS entries do come and go with it.
 */
struct FDreamUIMenus
{
	/** Queues menu registration on the ToolMenus startup callback. Call from module startup. */
	static void Register();
	static void Unregister();
};
