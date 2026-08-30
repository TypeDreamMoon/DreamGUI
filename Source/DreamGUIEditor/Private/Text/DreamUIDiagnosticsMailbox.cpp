// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIDiagnosticsMailbox.h"

#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIPaths.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace DreamUIMailboxLocal
{
	struct FEntry
	{
		FString CompiledAt;
		TArray<FDreamUIDiagnostic> Diagnostics;
	};

	/**
	 * Session state, not persistence: entries accumulate per editor run and the file is a snapshot
	 * of them. After a restart the first deposit rewrites the file with only what this session has
	 * compiled -- deliberately, because entries carried over from disk would pin line numbers from
	 * files that may have changed underneath them. Absence means "no news", the reader agrees.
	 */
	TMap<FString, FEntry> GEntries;
	FTSTicker::FDelegateHandle GPendingFlush;

	FString DefaultDirectory()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	}
}

void FDreamUIDiagnosticsMailbox::Deposit(const FDreamUIDiagnosticBag& InDiagnostics)
{
	using namespace DreamUIMailboxLocal;

	// No source name = the blueprint is not text-backed; that compile is none of the mailbox's
	// business. This is the negative control the whole pipeline keeps.
	if (InDiagnostics.SourceName.IsEmpty())
	{
		return;
	}

	FEntry& Entry = GEntries.FindOrAdd(InDiagnostics.SourceName);
	Entry.CompiledAt = FDateTime::UtcNow().ToIso8601();
	Entry.Diagnostics = InDiagnostics.Diagnostics;

	if (!GPendingFlush.IsValid())
	{
		GPendingFlush = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			GPendingFlush.Reset();
			FlushNow();
			return false; // one shot; the next deposit re-arms
		}));
	}
}

FString FDreamUIDiagnosticsMailbox::FlushNow(const FString& InOverrideDirectory)
{
	using namespace DreamUIMailboxLocal;

	// The automation suite compiles text-backed fixtures through the real compiler, and their
	// deposits must not land in the project's LIVE mailbox -- a VSCode session may be watching it
	// while the tests run (observed in the field: a suite pass left a fixture entry there). Tests
	// that test the mailbox itself pass an override directory and sail through.
	if (InOverrideDirectory.IsEmpty() && GIsAutomationTesting)
	{
		return FString();
	}

	const FString Directory = InOverrideDirectory.IsEmpty() ? DefaultDirectory() : InOverrideDirectory;
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		return FString();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);

	TSharedPtr<FJsonObject> Files = MakeShared<FJsonObject>();
	for (const TPair<FString, FEntry>& Pair : GEntries)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("compiledAt"), Pair.Value.CompiledAt);

		TArray<TSharedPtr<FJsonValue>> Diagnostics;
		for (const FDreamUIDiagnostic& Diagnostic : Pair.Value.Diagnostics)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("code"), static_cast<int32>(Diagnostic.Code));
			Item->SetStringField(TEXT("severity"), Diagnostic.IsError() ? TEXT("error") : TEXT("warning"));
			// 1-based, exactly as FDreamUISourceLocation counts them; a diagnostic with no
			// location (a file-level refusal like DUI6001) lands on 1,1 rather than 0,0.
			Item->SetNumberField(TEXT("line"), FMath::Max(1, Diagnostic.Location.Line));
			Item->SetNumberField(TEXT("column"), FMath::Max(1, Diagnostic.Location.Column));
			// The bare message: the reader prefixes "DUInnnn:" itself, and ToString's
			// "file(line,col):" layout would double what the Problems panel already shows.
			Item->SetStringField(TEXT("message"), Diagnostic.Message);
			Diagnostics.Add(MakeShared<FJsonValueObject>(Item));
		}
		Entry->SetArrayField(TEXT("diagnostics"), Diagnostics);
		Files->SetObjectField(Pair.Key, Entry);
	}
	Root->SetObjectField(TEXT("files"), Files);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	// Beside-then-rename: the reader is poll-then-read, and an in-place write hands it half a
	// file. Same rule the DreamFX bridge learned the hard way.
	const FString FinalPath = FPaths::Combine(Directory, TEXT(".dui-diagnostics.json"));
	const FString TempPath = FinalPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Serialized, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return FString();
	}
	if (!IFileManager::Get().Move(*FinalPath, *TempPath, /*Replace*/true, /*EvenIfReadOnly*/true))
	{
		IFileManager::Get().Delete(*TempPath);
		return FString();
	}
	return FinalPath;
}

void FDreamUIDiagnosticsMailbox::Reset()
{
	using namespace DreamUIMailboxLocal;
	GEntries.Reset();
	if (GPendingFlush.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GPendingFlush);
		GPendingFlush.Reset();
	}
}
