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
 * Nothing here is cached. DreamFX caches its roots and pays for it with an InvalidateSourceRoots that
 * every mutation site has to remember to call; here the answer is wanted a few times per compile and
 * once when a designer opens, so scanning is cheaper than the staleness -- notably the case where an
 * author creates the DUI folder while the editor is running, which a cache answers wrongly until a
 * restart.
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
	 */
	DREAMGUI_API FString Resolve(const FString& InPath);

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
