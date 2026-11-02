// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamUILocalization;

/**
 * The extraction manifest a compile leaves for localization tools: every localizable text of one .dui, as JSON,
 * at `<root>/Intermediate/DLoc/extract/DreamGUI/DUI/<path>.dui.json` -- `<root>` being the project, or the plugin
 * whose DUI/ folder holds the file.
 *
 *   {
 *     "version": 1,
 *     "provider": "DreamGUI",
 *     "source": "DUI/UI/Settings.dui",
 *     "namespace": "/Game/UI/Settings/WBP_SettingsScreen",
 *     "entries": [
 *       { "key": "Title.Text", "src": "设置", "at": "Title", "kind": "Text" },
 *       { "key": "Back.Text", "src": "返回", "at": "Footer/Back", "kind": "Button", "renamedFrom": "BackBtn.Text" }
 *     ]
 *   }
 *
 * The format is DreamLoc's (its design, section 7.1), and the file is the whole contract between the two: DreamGUI
 * does not link DreamLoc, DreamLoc does not read .dui. A plugin's file also says `"root": "Plugin.<Name>"`.
 */
namespace DreamUILocalizationManifest
{
	/**
	 * Where InSourcePath's manifest goes, the file as the manifest names it (`DUI/...`), and the root token
	 * (empty for the project's own DUI/). False for a file under no DUI root: it has no place to go.
	 */
	DREAMGUIEDITOR_API bool GetPaths(const FString& InSourcePath, FString& OutManifestPath, FString& OutSource, FString& OutRootToken);

	/** The manifest's text. LF line ends, one entry per line. */
	DREAMGUIEDITOR_API FString ToJson(const FString& InSource, const FString& InRootToken, const FDreamUILocalization& InLocalization);

	/**
	 * Writes the manifest of InSourcePath, leaving the file alone when it already says the same, so a recompile with
	 * nothing new does not look like a change to whatever watches the folder. False when nothing was written because
	 * the file is under no root, or the write failed.
	 */
	DREAMGUIEDITOR_API bool Write(const FString& InSourcePath, const FDreamUILocalization& InLocalization);
}
