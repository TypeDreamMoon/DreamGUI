// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** One .dui source tree: the project's own, or one belonging to an enabled plugin. */
struct FDreamUISourceRoot
{
	/** Absolute path of the source directory, with a trailing slash. */
	FString Directory;
	/** The token an author writes to address this root. Empty for the project's own. */
	FString RootToken;
	/**
	 * Absolute path of the project or plugin the root belongs to, with a trailing slash. Not the parent of Directory:
	 * `<Project>/DreamSource/UI/`'s parent is `DreamSource/`, which owns nothing.
	 */
	FString OwnerDirectory;
	/** The root is a legacy `DUI/` directory, read because the DreamSource one does not exist. */
	bool bLegacy = false;
};

/** How the project's own source directory was decided; see DreamUIPaths::ResolveProjectSourceDirectory. */
struct FDreamUIProjectSourceDirectory
{
	/**
	 * The directory to use, absolute, no trailing slash: the configured one when it exists, else the legacy `DUI/` when
	 * that exists, else the configured one again -- the place a directory gets created.
	 */
	FString Directory;
	/** `<Project>/<SourceDirectory>` from UDreamUISourceSettings, absolute. */
	FString ConfiguredDirectory;
	/** `<Project>/DUI`, absolute. */
	FString LegacyDirectory;
	/** Directory exists on disk. */
	bool bExists = false;
	/** Directory is the legacy one: only `DUI/` exists. A migration notice is due. */
	bool bUsingLegacy = false;
	/** Both exist; the configured one won and `DUI/` is being ignored. A notice is due too. */
	bool bLegacyIgnored = false;
};

/**
 * Where .dui files live, and the only place that knows.
 *
 * `<Project>/DreamSource/UI/` (configurable, UDreamUISourceSettings) and `<Plugin>/DreamSource/UI/`, with the legacy
 * `DUI/` of either still read when the DreamSource one does not exist -- for good, not for a transition period, with a
 * notice that it is deprecated. The table is in Plan/DreamSource/dreamsource-layout-spec.md section 11.2, and the VSCode
 * extension implements the same one.
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
	/**
	 * The legacy directory name, under the project or a plugin. Still read (see above), and still the logical prefix
	 * localization keys are spelled with (DreamUILocalizationManifest) until DreamLoc learns language keys.
	 */
	inline constexpr const TCHAR* LegacySourceDirectoryName = TEXT("DUI");

	/** A plugin's source directory, relative to its base directory. Not configurable. */
	inline constexpr const TCHAR* PluginSourceDirectory = TEXT("DreamSource/UI");

	/** The extension, dot included. */
	inline constexpr const TCHAR* SourceExtension = TEXT(".dui");

	/** The project's own source directory and how it was chosen. Cheap: two DirectoryExists. */
	DREAMGUI_API FDreamUIProjectSourceDirectory ResolveProjectSourceDirectory();

	/** ResolveProjectSourceDirectory().Directory: where the project's .dui files are, or would be created. */
	DREAMGUI_API FString GetProjectSourceDirectory();

	/** `<Project>/Intermediate/DreamSource`, absolute: where generated data that rebuilds by itself goes. */
	DREAMGUI_API FString GetGeneratedDirectory();

	/** `<Project>/Intermediate/DreamSource/ui.db`: symbols and compiler diagnostics for external editors. */
	DREAMGUI_API FString GetDatabasePath();

	/**
	 * Logs, once per editor session, each root still read from a legacy `DUI/` directory and a project `DUI/` that is being
	 * ignored. Safe to call often.
	 */
	DREAMGUI_API void LogDeprecatedRootsOnce();

	/**
	 * The project's root first, then every enabled plugin's, in plugin-manager order.
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
