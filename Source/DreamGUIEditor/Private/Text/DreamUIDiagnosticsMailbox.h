// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamUIDiagnosticBag;

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
struct FDreamUIDiagnosticsMailbox
{
	/**
	 * Records one file's compile outcome and schedules the debounced write. A bag with no source
	 * name (the blueprint is not text-backed) is a no-op.
	 */
	static void Deposit(const FDreamUIDiagnosticBag& InDiagnostics);

	/**
	 * Writes the mailbox now. InOverrideDirectory is the test seam; empty means the project's
	 * DUI/ directory, and without one nothing is written (same rule the symbols export follows).
	 * Returns the path written, or empty.
	 */
	static FString FlushNow(const FString& InOverrideDirectory = FString());

	/** Drops every deposited entry and any pending flush. Tests start clean with this. */
	static void Reset();
};
