// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIDiagnosticsMailbox.h"

#include "DreamGUIEditorModule.h"
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

	/**
	 * Which file each Blueprint last deposited for, so a compile that names a different file or none can
	 * retire the entry the Blueprint leaves behind. Keyed by the owner the compiler passed in.
	 */
	TMap<FString, FString> GOwnerSources;

	FString DefaultDirectory()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	}

	void ScheduleFlush()
	{
		if (!GPendingFlush.IsValid())
		{
			GPendingFlush = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				GPendingFlush.Reset();
				FDreamUIDiagnosticsMailbox::FlushNow();
				return false; // one shot; the next deposit re-arms
			}));
		}
	}

	/** Whether two spellings name the same file; the compiler and the watcher do not always agree on slashes or case. */
	bool IsSameSource(const FString& InA, const FString& InB)
	{
		return FPaths::IsSamePath(InA, InB);
	}

	/** Whether some owner other than InExcept still builds from InSource. */
	bool IsClaimedByAnotherOwner(const FString& InSource, const FString& InExcept)
	{
		for (const TPair<FString, FString>& Pair : GOwnerSources)
		{
			if (Pair.Key != InExcept && IsSameSource(Pair.Value, InSource))
			{
				return true;
			}
		}
		return false;
	}

	/** Drops every entry whose key names InSource; true when there was one. */
	bool RemoveEntries(const FString& InSource)
	{
		bool bRemoved = false;
		for (auto It = GEntries.CreateIterator(); It; ++It)
		{
			if (IsSameSource(It.Key(), InSource))
			{
				It.RemoveCurrent();
				bRemoved = true;
			}
		}
		return bRemoved;
	}
}

void FDreamUIDiagnosticsMailbox::Deposit(const FDreamUIDiagnosticBag& InDiagnostics, const FString& InOwner)
{
	using namespace DreamUIMailboxLocal;

	bool bRetiredEntry = false;
	if (!InOwner.IsEmpty())
	{
		// The owner's previous file stops being the owner's, whatever this compile turns out to name. Its entry goes
		// with it unless a second class is built from the same file, in which case that class's compiles keep it current.
		const FString* Previous = GOwnerSources.Find(InOwner);
		if (Previous != nullptr && !IsSameSource(*Previous, InDiagnostics.SourceName))
		{
			const FString Retired = *Previous;
			if (!IsClaimedByAnotherOwner(Retired, InOwner))
			{
				bRetiredEntry = RemoveEntries(Retired);
			}
		}
		if (InDiagnostics.SourceName.IsEmpty())
		{
			GOwnerSources.Remove(InOwner);
		}
		else
		{
			GOwnerSources.Add(InOwner, InDiagnostics.SourceName);
		}
	}

	// No source name = the blueprint is not text-backed; that compile is none of the mailbox's
	// business. This is the negative control the whole pipeline keeps.
	if (InDiagnostics.SourceName.IsEmpty())
	{
		if (bRetiredEntry)
		{
			ScheduleFlush();
		}
		return;
	}

	FEntry& Entry = GEntries.FindOrAdd(InDiagnostics.SourceName);
	Entry.CompiledAt = FDateTime::UtcNow().ToIso8601();
	Entry.Diagnostics = InDiagnostics.Diagnostics;

	ScheduleFlush();
}

void FDreamUIDiagnosticsMailbox::Remove(const FString& InSourceName)
{
	using namespace DreamUIMailboxLocal;

	if (InSourceName.IsEmpty())
	{
		return;
	}
	// An owner that was building from this file has nothing left to retire later.
	for (auto It = GOwnerSources.CreateIterator(); It; ++It)
	{
		if (IsSameSource(It.Value(), InSourceName))
		{
			It.RemoveCurrent();
		}
	}
	if (RemoveEntries(InSourceName))
	{
		ScheduleFlush();
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
	// Both refusals are LOGGED, which this whole file did not do once. The mailbox is the only thing
	// carrying compile verdicts to an editor outside Unreal, so a write that fails is a VSCode window
	// that quietly keeps showing the last successful compile's problems -- indistinguishable from a
	// clean build. The caller's empty string says "not written"; nothing was saying WHY, and the two
	// causes are things a reader can act on (a read-only DUI/ directory, a file locked by the editor
	// that is reading it).
	if (!FFileHelper::SaveStringToFile(Serialized, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(DreamGUIEditor, Warning,
			TEXT("[%s].%d Could not write '%s'; the diagnostics an external editor reads are now stale."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *TempPath);
		return FString();
	}
	if (!IFileManager::Get().Move(*FinalPath, *TempPath, /*Replace*/true, /*EvenIfReadOnly*/true))
	{
		UE_LOG(DreamGUIEditor, Warning,
			TEXT("[%s].%d Wrote '%s' but could not move it onto '%s'; the diagnostics an external editor reads are now stale."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *TempPath, *FinalPath);
		IFileManager::Get().Delete(*TempPath);
		return FString();
	}
	return FinalPath;
}

void FDreamUIDiagnosticsMailbox::GetEntries(TArray<FDreamUIDiagnosticsMailboxEntry>& OutEntries)
{
	using namespace DreamUIMailboxLocal;
	OutEntries.Reset(GEntries.Num());
	for (const TPair<FString, FEntry>& Pair : GEntries)
	{
		FDreamUIDiagnosticsMailboxEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry.SourceName = Pair.Key;
		Entry.CompiledAt = Pair.Value.CompiledAt;
		Entry.Diagnostics = Pair.Value.Diagnostics;
	}
}

bool FDreamUIDiagnosticsMailbox::FindEntry(const FString& InSourceName, FDreamUIDiagnosticsMailboxEntry& OutEntry)
{
	using namespace DreamUIMailboxLocal;
	for (const TPair<FString, FEntry>& Pair : GEntries)
	{
		if (IsSameSource(Pair.Key, InSourceName))
		{
			OutEntry.SourceName = Pair.Key;
			OutEntry.CompiledAt = Pair.Value.CompiledAt;
			OutEntry.Diagnostics = Pair.Value.Diagnostics;
			return true;
		}
	}
	return false;
}

void FDreamUIDiagnosticsMailbox::Reset()
{
	using namespace DreamUIMailboxLocal;
	GEntries.Reset();
	GOwnerSources.Reset();
	if (GPendingFlush.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GPendingFlush);
		GPendingFlush.Reset();
	}
}
