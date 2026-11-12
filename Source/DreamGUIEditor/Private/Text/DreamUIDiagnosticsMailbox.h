// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamUIDiagnostic;
struct FDreamUIDiagnosticBag;

/** One file's last compile outcome as the mailbox holds it. */
struct FDreamUIDiagnosticsMailboxEntry
{
	/** The file, as the compile that deposited it named it (the resolved, absolute path). */
	FString SourceName;
	/** ISO 8601, UTC. */
	FString CompiledAt;
	TArray<FDreamUIDiagnostic> Diagnostics;
};

/**
 * The diagnostics mailbox: `DUI/.dui-diagnostics.json`, one entry per .dui the compiler has seen
 * this session, rewritten after every compile of a text-backed class. The VSCode extension reads
 * it into the Problems panel -- this is how the compiler's verdicts (values, builder, compile:
 * everything a lexer cannot know) reach an editor that is not this one, without that editor
 * growing a second compiler.
 *
 * The format contract lives on the READER side (the extension's parseMailbox tests); this writer
 * conforms to it. Version 1:
 *
 *   { "version": 1, "files": { "<abs path>": { "compiledAt": "<ISO>", "diagnostics": [
 *       { "code": 5004, "severity": "error", "line": 12, "column": 5, "message": "..." } ] } } }
 *
 * Three load-bearing choices, none of them free to undo:
 *   - a clean compile writes an EMPTY array -- that is what clears the file's squiggles over
 *     there; dropping clean files would leave stale errors standing forever;
 *   - the write is beside-then-rename: the reader polls-and-reads, and an in-place write hands
 *     it half a file;
 *   - deposits coalesce through one ticker flush: a watcher batch recompiles N classes in one
 *     go, and N rewrites of the same file for one keypress is the shape the debounce exists for.
 */
struct DREAMGUIEDITOR_API FDreamUIDiagnosticsMailbox
{
	/**
	 * Records one file's compile outcome and schedules the debounced write. A bag with no source
	 * name (the blueprint is not text-backed) adds nothing.
	 *
	 * InOwner names the Blueprint that compiled, and is what lets the mailbox notice a class that has
	 * stopped being built from a file: the next compile of the same owner either names another file
	 * or none, and the entry of the one it used to name is dropped -- unless another owner still
	 * builds from it. Without an owner an entry can only ever be replaced, never retired, which is
	 * how a Blueprint converted back to a plain one left its last errors in the file for good.
	 */
	static void Deposit(const FDreamUIDiagnosticBag& InDiagnostics, const FString& InOwner = FString());

	/**
	 * Drops the entry of a file that no class is built from any more, and schedules the write. A
	 * path the mailbox holds nothing for is left alone. Compared as paths, so the spelling the
	 * entry was deposited under does not have to be the spelling asked about.
	 */
	static void Remove(const FString& InSourceName);

	/**
	 * Writes the mailbox now. InOverrideDirectory is the test seam; empty means the project's
	 * DUI/ directory, and without one nothing is written (same rule the symbols export follows).
	 * Returns the path written, or empty.
	 */
	static FString FlushNow(const FString& InOverrideDirectory = FString());

	/** Drops every deposited entry and any pending flush. Tests start clean with this. */
	static void Reset();

	/**
	 * What the mailbox holds now, for a reader in this process (the DreamMCP endpoint's `diagnostics`): one entry per file a
	 * compile of this session deposited, its diagnostics as that compile raised them, in no particular order.
	 */
	static void GetEntries(TArray<FDreamUIDiagnosticsMailboxEntry>& OutEntries);

	/** The entry of one file, compared as paths; false when no compile of this session deposited one for it. */
	static bool FindEntry(const FString& InSourceName, FDreamUIDiagnosticsMailboxEntry& OutEntry);
};
