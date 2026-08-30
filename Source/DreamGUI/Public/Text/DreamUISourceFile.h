// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"

/**
 * Text in, AST out. The whole front end of the .dui language, behind one function.
 *
 * One entry point rather than a lexer class and a parser class hanging out in public, because there
 * is exactly one thing anybody wants from this file and two exposed halves would invite a second
 * caller to drive them in some order the first caller does not. The tokens are an implementation
 * detail and the grammar is allowed to move underneath them.
 *
 * It touches no UObject, loads nothing and logs nothing. That is what lets the parser's entire test
 * suite be strings in and data out, and it is also what lets the runtime path (a mod's .dui, hot
 * reloaded) and the compile path share one implementation instead of drifting apart the way
 * DreamFX's three green channels did while the picture was wrong.
 *
 * What this stage does NOT decide: whether a type name is a real widget class, whether a property
 * exists, or what type a literal becomes. Those need reflection, and reflection is the builder's.
 * The AST records shape; UnknownNodeType, UnknownProperty and the whole 4xxx/5xxx band are raised
 * downstream. The exceptions are the checks that need nothing but the file itself -- duplicate ids,
 * duplicate and unresolved style names, loop variable shadowing -- which are cheaper and far better
 * reported here, where the source location is in hand.
 */
struct DREAMGUI_API FDreamUISourceFile
{
	/**
	 * Parse InText, which is the whole file.
	 *
	 * Returns false when THIS parse raised at least one error, not when the bag holds one: a caller
	 * collecting several files into a single bag still gets a per-file answer. Warnings never change
	 * the result -- an author with a shadowed loop variable still gets their tree.
	 *
	 * OutAst is overwritten. OutDiagnostics is appended to, and its SourceName is set to InSourceName
	 * so every diagnostic raised from here is stamped with the file it came from.
	 *
	 * Recovery is on: a file with five mistakes reports five, because the usual author is a language
	 * model producing a whole file at once and a front end that stops at the first error turns one
	 * round trip into five.
	 */
	static bool Parse(const FString& InText, const FString& InSourceName,
		FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics);

	/**
	 * As above, with `use` imports served by InImportReader: (authored spelling) -> (resolved path,
	 * text), false when it resolves to nothing readable. Resolution lives INSIDE the reader so a
	 * test can serve spellings from a map while the real one walks the DUI roots; the resolved path
	 * is what the cycle guard keys on and what Imports records for the watcher. The plain overload
	 * above passes no reader, under which a `use` line reports ImportFailed -- a caller that
	 * offered no way to read files gets no silent half-import.
	 */
	static bool Parse(const FString& InText, const FString& InSourceName,
		FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics,
		TFunction<bool(const FString&, FString&, FString&)> InImportReader);

	/** The DUI-roots-and-file-system import reader the compiler and watcher use. */
	static TFunction<bool(const FString&, FString&, FString&)> MakeFileImportReader();
};
