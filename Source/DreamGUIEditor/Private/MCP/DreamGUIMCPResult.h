// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// The result body every DreamMCP action answers with, and the conversions into it.
//
// One shape for every action, short or long, success or refusal: `ok`, `outcome`, `reason`, `summary`,
// `diagnostics`, `writes`, `artifacts`, `data`, `next`. The MCP server reads only these fields -- never
// the wording -- so `outcome` and `reason` carry the decision and the text is free to change. Every
// string here is wire text: invariant English, and every path is relative to the project root with
// forward slashes, never absolute.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FDreamUIDiagnostic;
struct FDreamUIDiagnosticBag;

namespace DreamGUIMCP
{
	/** `ok` / `source-error` / `refused` / `engine-error`. */
	enum class EOutcome : uint8
	{
		/** The action did what it was asked. */
		Ok,
		/** The source has an error: the diagnostics say where; the fix is an edit. */
		SourceError,
		/** Not the source's fault: bad arguments, a missing file, an asset that may not be rebuilt. `reason` is a stable code. */
		Refused,
		/** Something inside the engine or the plugin failed. */
		EngineError,
	};

	DREAMGUIEDITOR_API const TCHAR* LexOutcome(EOutcome Outcome);

	/** One diagnostic, as the result body carries it. */
	struct FDiagnostic
	{
		FString Code;
		/** `error` / `warning` / `info`. */
		FString Severity = TEXT("error");
		/** Relative to the project root. Empty when the diagnostic names no file. */
		FString File;
		int32 Line = 0;
		int32 Column = 0;
		FString Message;
		/** What to do about it, when the message alone does not say. */
		FString Hint;
		/** The stage the code belongs to (`lexer`, `parser`, `semantic`, `values`, `builder`, `compile`, `layout`). */
		FString Stage;
		/** The node the diagnostic is about, as ids from below the root down joined with '/'. Empty when it names none. */
		FString Node;
		/** The documentation page of the code. Empty when there is no code. */
		FString Doc;
	};

	/** One asset the action wrote. `state`: `memory` / `saved` / `created` / `deleted`. */
	struct FWrite
	{
		FString Asset;
		FString State;
	};

	/** A file the action produced for the caller to read: an image. */
	struct FArtifact
	{
		FString Kind;
		/** Relative to the project root. */
		FString Path;
		FString Title;
	};

	struct DREAMGUIEDITOR_API FResult
	{
		EOutcome Outcome = EOutcome::Ok;
		/** A stable machine code for a refusal or an engine error, `kebab-case`. Empty for `ok` and `source-error`. */
		FString Reason;
		/** One line for the model: what happened. */
		FString Summary;
		TArray<FDiagnostic> Diagnostics;
		TArray<FWrite> Writes;
		TArray<FArtifact> Artifacts;
		/** Action-specific payload. Null serializes as an empty object. */
		TSharedPtr<FJsonObject> Data;
		/** What the model should do next, one sentence each. */
		TArray<FString> Next;

		bool IsOk() const { return Outcome == EOutcome::Ok; }

		static FResult MakeOk(const FString& InSummary);
		static FResult MakeRefused(const FString& InReason, const FString& InSummary, const FString& InNext = FString());
		static FResult MakeEngineError(const FString& InReason, const FString& InSummary, const FString& InNext = FString());

		TSharedRef<FJsonObject> ToJson() const;
	};

	/** The body serialized, condensed. */
	DREAMGUIEDITOR_API FString SerializeJson(const TSharedRef<FJsonObject>& Object);

	/** Parses a JSON object. Null when Text is not one. */
	DREAMGUIEDITOR_API TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text);

	/**
	 * A path as the wire carries it: relative to the project directory, forward slashes. A path outside the project keeps its
	 * relative spelling (`../...`) where it has one and only its file name where it has none, so no absolute path ever leaves
	 * the process; an empty path stays empty.
	 */
	DREAMGUIEDITOR_API FString MakeProjectRelativePath(const FString& Path);

	/**
	 * A `file` argument resolved: an absolute path as given; a relative one against the project directory first, then
	 * against the project's DUI/ folder -- whichever exists, the project spelling when neither does.
	 */
	DREAMGUIEDITOR_API FString ResolveProjectFileArgument(const FString& Argument);

	/** Text with every spelling of the absolute project directory taken out, so a path in it reads project-relative. */
	DREAMGUIEDITOR_API FString ScrubAbsolutePaths(const FString& Text);

	/** "DUI3001" for a numbered code; empty for None. */
	DREAMGUIEDITOR_API FString FormatDiagnosticCode(int32 Code);

	/** `lexer` for 1xxx ... `layout` for 8xxx: the stage a code's thousand names. */
	DREAMGUIEDITOR_API FString DescribeDiagnosticStage(int32 Code);

	/** The manual's page of a DUInnnn code, from the plugin's DocsURL. Empty for anything that is not a code. */
	DREAMGUIEDITOR_API FString MakeDiagnosticDocUrl(int32 Code);

	/** One compiler diagnostic on the wire. The file is SourceFile (absolute or relative), made project-relative. */
	DREAMGUIEDITOR_API FDiagnostic MakeDiagnostic(const FDreamUIDiagnostic& Diagnostic, const FString& SourceFile);

	/** Every diagnostic of a bag, in the order raised. */
	DREAMGUIEDITOR_API void AppendDiagnostics(TArray<FDiagnostic>& Out, const FDreamUIDiagnosticBag& Bag, const FString& SourceFile);

	/** How many diagnostics have the severity `error` / `warning` / `info`. */
	DREAMGUIEDITOR_API int32 CountSeverity(const TArray<FDiagnostic>& Diagnostics, const TCHAR* Severity);
}
