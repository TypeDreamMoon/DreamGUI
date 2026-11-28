// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Dumps what the compiler knows into the `symbols` table of `Intermediate/DreamSource/ui.db` (FDreamUIDatabase), for
 * editors that are not this one. It used to be `DUI/.dui-symbols.json`; the table holds the same object, split into one
 * row per member of each object section so that a change to one class rewrites one row.
 *
 * The VSCode extension's completion, hover and validation all read this, and that is the whole
 * design: the extension never guesses at the language. Tags come from the builder's own table,
 * properties from the same reflective policy the write-back sweeps with, enums from their UEnum --
 * so what the extension offers is exactly what the compiler accepts, kept in step by regenerating
 * rather than by anyone remembering to.
 *
 * Written on editor startup (once classes exist) and on demand via `DreamUI.ExportSymbols`. Only
 * for a project that has a .dui source directory (DreamSource/UI, or the legacy DUI): a project that
 * has never used a .dui should not wake up owning a database.
 */
class DREAMGUIEDITOR_API FDreamUISymbolExport
{
public:
	/** Registers the startup write and the console command. */
	static void Register();
	static void Unregister();

	/**
	 * Writes the changed rows now. Returns the database path, empty when nothing could be written (no source directory,
	 * or a database error, logged). InOverrideDatabasePath is the test seam.
	 */
	static FString ExportNow(const FString& InOverrideDatabasePath = FString());

	/**
	 * What the file holds, built now and not written anywhere: for a reader in this process (the DreamMCP endpoint's
	 * `lookup`), which must answer in a project that has no DUI/ directory yet as well. bInvariantTooltips: each tooltip as
	 * its source wrote it (English), not as the editor's language shows it.
	 */
	static TSharedRef<FJsonObject> BuildSymbols(bool bInvariantTooltips = false);

	/**
	 * The file's `viewModels` object, on its own: every loaded class a `viewmodels` line can name and what a member path
	 * can reach on it. Exposed for the tests, which have no DUI/ directory to read the file back from.
	 */
	static TSharedRef<FJsonObject> DescribeViewModels();
};
