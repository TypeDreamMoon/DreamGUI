// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** One `DUI/` source tree: the project's own, or one belonging to an enabled plugin. */
struct FDreamUISourceRoot
{
	/** Absolute path of the DUI directory, with a trailing slash. */
	FString Directory;
	/** The token an author writes to address this root. Empty for the project's own. */
	FString RootToken;
};

/**
 * Where .dui files live, and the only place that knows.
 *
 * `<Project>/DUI/` and `<Plugin>/DUI/`, the same shape DreamShader gives .dsm and DreamFX gives .dfs.
 * Deliberately not Content/: a .dui is source, not an asset. Putting it under Content means the
 * cooker walks it, the content browser shows a file it cannot open, and a plugin shipping widget
 * classes has to ship its sources inside its cooked content to have them found at all. The three
 * languages in this project having three different answers to "where does source go" would be the
 * worse outcome either way.
 *
 * No cache anybody has to invalidate. DreamFX caches its roots and pays for it with an
 * InvalidateSourceRoots that every mutation site has to remember to call, and with a DUI folder created
 * while the editor runs going unseen until somebody does. GetSourceRoots only keeps its answer for half
 * a second (a write-back flush asks once per import), never keeps an empty answer, and checks on every
 * call whether the project's own DUI folder exists, so a folder the editor has just made is seen at
 * once; a plugin's DUI folder appearing or going away is seen within the half second. The .cpp says why
 * each of those is so.
 */
namespace DreamUIPaths
{
	/** The directory name, under the project or a plugin. */
	inline constexpr const TCHAR* SourceDirectoryName = TEXT("DUI");

	/** The extension, dot included. */
	inline constexpr const TCHAR* SourceExtension = TEXT(".dui");

	/**
	 * The project's `DUI/` first, then every enabled plugin's, in plugin-manager order.
	 *
	 * Only directories that exist are returned, so a project with no DUI folder gets an empty list
	 * rather than a candidate that can never resolve.
	 */
	DREAMGUI_API TArray<FDreamUISourceRoot> GetSourceRoots();

	/** Every .dui under every source root, absolute and sorted. */
	DREAMGUI_API void FindSourceFiles(TArray<FString>& OutFiles);

	/**
	 * An authored path as an absolute filename. Empty in, empty out.
	 *
	 * Three spellings, and the first two are unambiguous:
	 *
	 *   D:/Work/Proj/DUI/Panels/Settings.dui   absolute, used as written
	 *   Plugin.DreamGUI:Panels/Settings.dui    that plugin's DUI directory
	 *   Panels/Settings.dui                    searched: project DUI first, then each plugin's
	 *
	 * The search takes the first candidate that EXISTS, which makes a bare path mean "whichever root
	 * has it" -- convenient, and ambiguous the moment two roots hold the same relative path. That
	 * ambiguity is why the plugin-qualified spelling exists, and why MakePortablePath produces it for
	 * anything outside the project's own root.
	 *
	 * When nothing exists, the project-root candidate comes back rather than an empty string: the
	 * caller is about to report a file it could not read, and a diagnostic that names a path is worth
	 * more than one that names nothing.
	 *
	 * OutRootTokenResolved, when given, is set false for the one case the returned path cannot say
	 * anything about: `Plugin.X:…` where no enabled plugin X has a `DUI/` directory. The project's
	 * own root stands in so that the answer is still an absolute filename -- every caller here opens
	 * this string as well as printing it -- but the path then names a place that has nothing to do
	 * with what the author wrote, and a diagnostic that quotes it alone sends them to the wrong
	 * folder. A caller that reports a failure should say which plugin instead.
	 */
	DREAMGUI_API FString Resolve(const FString& InPath, bool* OutRootTokenResolved = nullptr);

	/**
	 * The spelling to STORE for a file the user picked, given as an absolute path.
	 *
	 * A file picker hands back an absolute path, and an absolute path in an asset is a path that
	 * works on exactly one machine. This turns it back into a root-relative one where it can --
	 * "Panels/Settings.dui" under the project, "Plugin.X:Panels/Settings.dui" under a plugin -- and
	 * returns the absolute path unchanged when the file is under no root at all, because refusing it
	 * would be a picker that silently discards the user's choice.
	 */
	DREAMGUI_API FString MakePortablePath(const FString& InAbsolutePath);
}
