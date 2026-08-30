// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Dumps what the compiler knows into `DUI/.dui-symbols.json`, for editors that are not this one.
 *
 * The VSCode extension's completion, hover and validation all read this file, and that is the whole
 * design: the extension never guesses at the language. Tags come from the builder's own table,
 * properties from the same reflective policy the write-back sweeps with, enums from their UEnum --
 * so what the extension offers is exactly what the compiler accepts, kept in step by regenerating
 * the file rather than by anyone remembering to.
 *
 * Written on editor startup (once classes exist) and on demand via `DreamUI.ExportSymbols`. Only
 * into a project `DUI/` directory that already exists: a project that has never used a .dui should
 * not wake up owning one.
 */
class DREAMGUIEDITOR_API FDreamUISymbolExport
{
public:
	/** Registers the startup write and the console command. */
	static void Register();
	static void Unregister();

	/** Writes the file now. Returns the path it wrote, empty when there was no DUI/ to write into. */
	static FString ExportNow();
};
