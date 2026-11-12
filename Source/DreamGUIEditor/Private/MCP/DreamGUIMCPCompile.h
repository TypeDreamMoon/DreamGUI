// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// The DreamMCP endpoint's two ways of reading a .dui: checked (parsed and built into a throwaway tree, nothing written)
// and compiled (into the widget Blueprint its `class` line names, created when it does not exist yet).
//
// The caller names a file and nothing else. The asset comes from the file's `class` line; a component the file places
// through `use "Card.dui" as Card` is compiled before the file that places it, so no manifest has to put them in order.

#pragma once

#include "CoreMinimal.h"

#include "MCP/DreamGUIMCPResult.h"

class UDreamWidgetBlueprint;

namespace DreamGUIMCP
{
	/** What a check of one file found. */
	struct FCheckOutcome
	{
		/** The file could be read. False: Refusal says why. */
		bool bRead = false;
		FString RefusalReason;
		FString RefusalSummary;
		/** Parsed and built without an error. */
		bool bClean = false;
		/** The `class` line's asset, `/Game/UI/WBP_X`; empty when the file has none. */
		FString AssetPath;
		int32 Nodes = 0;
		TArray<FDiagnostic> Diagnostics;
		/**
		 * Component files the source places whose Blueprints have not been compiled yet, project-relative. Not an error of
		 * the source: compile and layout compile them first. Their DUI5018 comes back as an info.
		 */
		TArray<FString> UncompiledComponents;
	};

	/** Parse and build InAbsoluteFile the way the compiler reads it, into a tree that is thrown away. Writes nothing. */
	DREAMGUIEDITOR_API FCheckOutcome CheckSource(const FString& InAbsoluteFile);

	/** One Blueprint a compile wrote, in the order they were compiled (components first). */
	struct FCompiledAsset
	{
		FString SourceFile;
		/** `/Game/UI/WBP_X`. */
		FString AssetPath;
		bool bCreated = false;
		bool bSaved = false;
		/** Nothing had changed since this process last compiled it, so it was not compiled again. */
		bool bUnchanged = false;
		bool bFailed = false;
	};

	/** What a compile of one file did. */
	struct FCompileOutcome
	{
		/** Set when the compile could not even start: no such file, no `class` line, a class path that is not an asset. */
		FString RefusalReason;
		FString RefusalSummary;
		FString RefusalNext;
		/** The Blueprint of the file asked for, compiled (or failed); null on a refusal. */
		UDreamWidgetBlueprint* Blueprint = nullptr;
		bool bFailed = false;
		TArray<FCompiledAsset> Assets;
		/** Every diagnostic of every file compiled, each naming its own file. */
		TArray<FDiagnostic> Diagnostics;
	};

	/**
	 * Compile InAbsoluteFile into its Blueprint, and first every component it places from a .dui of its own. bSave saves each
	 * package written (a host does; the user's editor keeps them in memory for the user to save). bForce compiles even what
	 * has not changed since this process last compiled it.
	 */
	DREAMGUIEDITOR_API FCompileOutcome CompileSource(const FString& InAbsoluteFile, bool bSave, bool bForce);

	/** Forgets which sources were compiled when (tests). */
	DREAMGUIEDITOR_API void ResetCompileState();

	/** Every .dui under the project's and the plugins' DUI/ folders, absolute. */
	DREAMGUIEDITOR_API void FindAllSources(TArray<FString>& OutFiles);
}
