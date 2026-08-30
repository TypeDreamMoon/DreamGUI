// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceWatcher.h"

#include "DreamGUI.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIDocument.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUITextWriteBack.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "IDirectoryWatcher.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamUISourceWatcher"

namespace DreamUISourceWatcherLocal
{
	/**
	 * Editors write a file more than once per save -- a temp file, a rename, a metadata touch -- and
	 * a compile per event would mean three Blueprint compiles for one Ctrl-S. Collecting and acting
	 * after a quiet interval is what makes save-to-recompile usable rather than merely present.
	 */
	constexpr float DebounceSeconds = 0.75f;

	/**
	 * More files than this in one batch is not a save; it is a branch switch, a bulk rewrite or a
	 * content pack landing. Compiling that many Blueprint classes unasked stalls the editor for as
	 * long as it takes, with no way to say no.
	 */
	constexpr int32 BulkThreshold = 8;

	TMap<FString, FDelegateHandle> GWatchHandles;
	FTSTicker::FDelegateHandle GTickerHandle;
	TSet<FString> GPendingFiles;
	double GLastChangeTime = 0.0;

	/** A batch too large to compile unasked. Held rather than dropped -- see OfferDeferredBatch. */
	TSet<FString> GDeferredBulkFiles;

	/** Set by an explicit command; a plain save leaves it false and stays quiet when it worked. */
	bool GAnnounceSuccess = false;

	/** What one drained queue did, and where to send the author when it did not work. */
	struct FBatchResult
	{
		int32 Compiled = 0;
		int32 Unclaimed = 0;
		int32 Failed = 0;

		FString FirstErrorFile;
		FString FirstErrorText;

		bool HasFirstError() const { return !FirstErrorFile.IsEmpty(); }
	};

	void FindBlueprints(const FString& InFilePath, TArray<UDreamWidgetBlueprint*>& OutBlueprints)
	{
		const FString Normalized = FDreamUIDocumentRegistry::NormalizePath(InFilePath);
		for (TObjectIterator<UDreamWidgetBlueprint> It; It; ++It)
		{
			UDreamWidgetBlueprint* Blueprint = *It;
			if (!IsValid(Blueprint) || Blueprint->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			const FString Authored = DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint);
			if (Authored.IsEmpty())
			{
				continue;
			}
			// Resolved and normalised on both sides. The stored path may be relative, plugin-qualified
			// or absolute, and the watcher only ever has the absolute one -- comparing the spellings
			// would make a file rebuild or not depending on how its class happened to name it.
			const FString Resolved = FDreamUIDocumentRegistry::NormalizePath(
				UDreamTextUserWidget::ResolveDuiFilePath(Authored));
			if (Resolved.Equals(Normalized, ESearchCase::IgnoreCase))
			{
				OutBlueprints.Add(Blueprint);
			}
		}
	}

	/**
	 * True when the change on disk is the designer's own write coming back.
	 *
	 * Only askable when a document is open for the file, which is exactly when the designer could
	 * have written it. With no document there is nothing of ours to mistake a foreign edit for.
	 */
	bool IsOwnWriteComingBack(const FString& InFilePath)
	{
		UDreamUIDocument* Document = FDreamUIDocumentRegistry::Find(InFilePath);
		if (Document == nullptr)
		{
			return false;
		}
		FString DiskContent;
		if (!FFileHelper::LoadFileToString(DiskContent, *InFilePath))
		{
			// Unreadable is not "ours". Letting it through means the compile reports the real problem
			// instead of this function silently deciding there is none.
			return false;
		}
		return Document->IsOwnWrite(DiskContent);
	}

	/** One notification for the whole batch, carrying a way to open the file that failed. */
	void ReportBatch(const FBatchResult& InBatch, bool bAnnounceSuccess)
	{
		if (InBatch.Failed == 0 && !bAnnounceSuccess)
		{
			return;
		}

		FNotificationInfo Info(FText::GetEmpty());
		Info.ExpireDuration = InBatch.Failed > 0 ? 10.0f : 5.0f;
		Info.bFireAndForget = true;

		if (InBatch.Failed > 0)
		{
			Info.Text = FText::Format(LOCTEXT("DreamUICompileFailed", "DreamUI: {0}"),
				FText::FromString(InBatch.FirstErrorText));
			if (InBatch.HasFirstError())
			{
				const FString File = InBatch.FirstErrorFile;
				Info.Hyperlink = FSimpleDelegate::CreateLambda([File]
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*File, nullptr, ELaunchVerb::Open);
				});
				Info.HyperlinkText = LOCTEXT("DreamUIOpenFailedSource", "Open the file");
			}
		}
		else
		{
			Info.Text = FText::Format(
				LOCTEXT("DreamUICompileSucceeded", "DreamUI: {0} class(es) rebuilt, {1} file(s) with no class."),
				FText::AsNumber(InBatch.Compiled), FText::AsNumber(InBatch.Unclaimed));
		}

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(
				InBatch.Failed > 0 ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
	}

	void RecompileFor(const FString& InFilePath, FBatchResult& OutBatch)
	{
		TArray<UDreamWidgetBlueprint*> Blueprints;
		FindBlueprints(InFilePath, Blueprints);
		if (Blueprints.Num() == 0)
		{
			// Not an error and not silence either: a .dui with no loaded class is the ordinary state
			// of every file in the project except the one being worked on, and it is also what an
			// author sees after typing the path wrong. The count reaches the toast; the name reaches
			// the log.
			++OutBatch.Unclaimed;
			UE_LOG(DreamGUI, Verbose, TEXT("[%s].%d '%s' changed, but no loaded class is built from it."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath);
			return;
		}

		// The document first, and only when one is open. The designer's write-back compares the tree
		// against the text it believes is on disk; leaving it believing the old text would make the
		// next flush plan its edits against lines that have moved.
		if (UDreamUIDocument* Document = FDreamUIDocumentRegistry::Find(InFilePath))
		{
			FString LoadError;
			if (!Document->LoadFromFile(LoadError))
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Could not reload '%s': %s"),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath, *LoadError);
			}
		}

		for (UDreamWidgetBlueprint* Blueprint : Blueprints)
		{
			UE_LOG(DreamGUI, Display, TEXT("[%s].%d Rebuilding '%s' after '%s' changed."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(Blueprint), *InFilePath);

			FCompilerResultsLog Results;
			// SkipGarbageCollection, like every other compile in this plugin. A save-triggered rebuild
			// runs on Ctrl-S, and a full GC per keystroke is a stall the author did not ask for and
			// cannot attribute to anything they did. The engine collects on its own schedule.
			FKismetEditorUtilities::CompileBlueprint(Blueprint,
				EBlueprintCompileOptions::SkipGarbageCollection, &Results);

			if (Results.NumErrors > 0)
			{
				++OutBatch.Failed;
				if (!OutBatch.HasFirstError())
				{
					OutBatch.FirstErrorFile = InFilePath;
					// The compiler's own first error, which already carries the DUInnnn code and the
					// line and column. Rewording it here would give the author two different sentences
					// for one problem depending on where they read it.
					const TSharedRef<FTokenizedMessage>* FirstError = Results.Messages.FindByPredicate(
						[](const TSharedRef<FTokenizedMessage>& Message)
						{
							return Message->GetSeverity() == EMessageSeverity::Error;
						});
					OutBatch.FirstErrorText = FirstError != nullptr
						? (*FirstError)->ToText().ToString()
						: FString::Printf(TEXT("%s failed to compile"), *GetNameSafe(Blueprint));
				}
			}
			else
			{
				++OutBatch.Compiled;
			}
		}
	}

	/** import (normalized, lowercased) -> importer (same spelling RecompileFor expects). */
	TMultiMap<FString, FString> GImportEdges;

	FString NormalizeImportKey(const FString& InPath)
	{
		FString Key = InPath;
		FPaths::NormalizeFilename(Key);
		return Key.ToLower();
	}

	void DrainQueue()
	{
		TArray<FString> Files = GPendingFiles.Array();
		GPendingFiles.Reset();
		Files.Sort();

		const bool bAnnounceSuccess = GAnnounceSuccess;
		GAnnounceSuccess = false;

		// A changed file recompiles its importers too, transitively: saving the style library IS
		// saving every screen that wears it, as far as the classes are concerned. The worklist
		// carries a visited set so a diamond expands once and a (rejected, but defensive) cycle
		// terminates.
		TArray<FString> Worklist = Files;
		TSet<FString> Visited;
		for (const FString& File : Files)
		{
			Visited.Add(NormalizeImportKey(File));
		}
		for (int32 Index = 0; Index < Worklist.Num(); ++Index)
		{
			TArray<FString> Importers;
			GImportEdges.MultiFind(NormalizeImportKey(Worklist[Index]), Importers);
			for (const FString& Importer : Importers)
			{
				bool bAlreadyVisited = false;
				Visited.Add(NormalizeImportKey(Importer), &bAlreadyVisited);
				if (!bAlreadyVisited)
				{
					Worklist.Add(Importer);
				}
			}
		}

		FBatchResult Batch;
		for (const FString& File : Worklist)
		{
			if (!FPaths::FileExists(File))
			{
				continue; // deleted or renamed between the event and now
			}
			if (IsOwnWriteComingBack(File))
			{
				continue;
			}
			RecompileFor(File, Batch);
		}
		ReportBatch(Batch, bAnnounceSuccess);
	}

	/**
	 * Offers a bulk batch instead of compiling it.
	 *
	 * The files are kept rather than dropped: dropping them leaves every one of those classes stale
	 * with nothing to say so, which is the failure this watcher exists to end -- just quieter.
	 */
	void OfferDeferredBatch()
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("DreamUIBulkBacklog",
				"DreamUI: {0} source files changed at once. Rebuilding them all now would queue that "
				"many Blueprint compiles."),
			FText::AsNumber(GDeferredBulkFiles.Num())));
		Info.ExpireDuration = 30.0f;
		Info.bFireAndForget = true;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]
		{
			GPendingFiles.Append(GDeferredBulkFiles);
			GDeferredBulkFiles.Reset();
			// Say so when it finishes, and skip the debounce: the author just asked for this, and
			// another three quarters of a second only makes the click feel broken.
			GAnnounceSuccess = true;
			GLastChangeTime = 0.0;
		});
		Info.HyperlinkText = LOCTEXT("DreamUIRebuildBacklog", "Rebuild them now");

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_None);
		}
	}

	bool Tick(float /*DeltaTime*/)
	{
		if (GPendingFiles.Num() == 0)
		{
			return true;
		}
		// Not during play. Compiling a widget Blueprint reinstances every live instance of it, and
		// doing that to a running game because a file was saved in another window is not a rebuild,
		// it is a crash report. The queue waits; PIE ending is a tick like any other.
		if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
		{
			return true;
		}
		if (IsGarbageCollecting() || GIsSavingPackage)
		{
			return true;
		}
		if (FPlatformTime::Seconds() - GLastChangeTime < DebounceSeconds)
		{
			return true;
		}

		// Gated on batch size whenever it arrives, rather than on being near startup. DreamFX
		// measured this exact question and found no startup replay at all -- the watcher begins
		// watching at registration, so changes made while the editor was closed produce nothing. The
		// batch that hurts is a bulk change while the editor is open.
		if (!GAnnounceSuccess && GPendingFiles.Num() > BulkThreshold)
		{
			GDeferredBulkFiles.Append(GPendingFiles);
			GPendingFiles.Reset();
			OfferDeferredBatch();
			return true;
		}

		DrainQueue();
		return true;
	}

	void OnDirectoryChanged(const TArray<FFileChangeData>& InChanges)
	{
		for (const FFileChangeData& Change : InChanges)
		{
			if (Change.Action == FFileChangeData::FCA_Removed)
			{
				continue;
			}
			if (!FPaths::GetExtension(Change.Filename, /*bIncludeDot*/true)
				.Equals(DreamUIPaths::SourceExtension, ESearchCase::IgnoreCase))
			{
				continue;
			}
			GPendingFiles.Add(FDreamUIDocumentRegistry::NormalizePath(Change.Filename));
		}
		if (GPendingFiles.Num() > 0)
		{
			GLastChangeTime = FPlatformTime::Seconds();
		}
	}
}

void FDreamUISourceWatcher::NoteImports(const FString& InImporter, const TArray<FString>& InImports)
{
	using namespace DreamUISourceWatcherLocal;
	// Replace, not append: a file that dropped a `use` line must stop recompiling on that library's
	// saves, and the compiler republished the WHOLE current list.
	for (auto It = GImportEdges.CreateIterator(); It; ++It)
	{
		if (It.Value() == InImporter)
		{
			It.RemoveCurrent();
		}
	}
	for (const FString& Import : InImports)
	{
		GImportEdges.Add(NormalizeImportKey(Import), InImporter);
	}
}

void FDreamUISourceWatcher::Register()
{
	using namespace DreamUISourceWatcherLocal;

	FDirectoryWatcherModule& Module =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if (IDirectoryWatcher* Watcher = Module.Get())
	{
		for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
		{
			FDelegateHandle Handle;
			if (Watcher->RegisterDirectoryChangedCallback_Handle(
				Root.Directory,
				IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnDirectoryChanged),
				Handle,
				IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
			{
				GWatchHandles.Add(Root.Directory, Handle);
				UE_LOG(DreamGUI, Display, TEXT("[%s].%d Watching '%s' for .dui changes."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Root.Directory);
			}
		}
	}

	// The ticker drains the queue, so it exists even with nothing watched: a project with no DUI
	// directory yet still has menu commands that queue through this same path, and a queue with no
	// drain is a command that silently does nothing.
	GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&Tick), /*InDelay=*/0.25f);
}

void FDreamUISourceWatcher::Unregister()
{
	using namespace DreamUISourceWatcherLocal;

	if (GTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTickerHandle);
		GTickerHandle.Reset();
	}
	if (FDirectoryWatcherModule* Module =
		FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			for (const TPair<FString, FDelegateHandle>& Entry : GWatchHandles)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Entry.Key, Entry.Value);
			}
		}
	}
	GWatchHandles.Reset();
	GPendingFiles.Reset();
	GDeferredBulkFiles.Reset();
	GAnnounceSuccess = false;
}

void FDreamUISourceWatcher::QueueFile(const FString& InFilePath, const bool bAnnounceSuccess)
{
	using namespace DreamUISourceWatcherLocal;

	GPendingFiles.Add(FDreamUIDocumentRegistry::NormalizePath(InFilePath));
	GLastChangeTime = FPlatformTime::Seconds();
	GAnnounceSuccess |= bAnnounceSuccess;
}

void FDreamUISourceWatcher::FlushPending()
{
	using namespace DreamUISourceWatcherLocal;

	GLastChangeTime = 0.0;
	Tick(0.0f);
}

void FDreamUISourceWatcher::FindBlueprintsForSource(const FString& InAbsoluteFilePath,
	TArray<UDreamWidgetBlueprint*>& OutBlueprints)
{
	DreamUISourceWatcherLocal::FindBlueprints(InAbsoluteFilePath, OutBlueprints);
}

int32 FDreamUISourceWatcher::RebuildAll()
{
	using namespace DreamUISourceWatcherLocal;

	// Through the asset registry rather than through the files, which is the opposite direction from
	// everything else here and is the only direction that works: a .dui does not name its class, so
	// the set of classes cannot be derived from the set of files. It can only be derived from the
	// set of classes.
	const FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UDreamWidgetBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssets(Filter, Assets);

	FScopedSlowTask SlowTask(static_cast<float>(Assets.Num()),
		LOCTEXT("DreamUIRebuildAll", "Rebuilding DreamUI text widgets..."));
	SlowTask.MakeDialog(/*bShowCancelButton*/true);

	FBatchResult Batch;
	int32 Found = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}
		SlowTask.EnterProgressFrame(1.0f, FText::FromName(Asset.AssetName));

		// Loaded here, which is what separates this from the watcher: the sweep is asked for, so it
		// may pay for what the save path refuses to.
		UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Asset.GetAsset());
		if (!IsValid(Blueprint) || !DreamUITextAuthoring::IsTextAuthored(Blueprint))
		{
			continue;
		}
		++Found;

		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		if (Results.NumErrors > 0)
		{
			++Batch.Failed;
			if (!Batch.HasFirstError())
			{
				Batch.FirstErrorFile = UDreamTextUserWidget::ResolveDuiFilePath(
					DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint));
				Batch.FirstErrorText = Results.Messages.Num() > 0
					? Results.Messages[0]->ToText().ToString()
					: FString::Printf(TEXT("%s failed to compile"), *GetNameSafe(Blueprint));
			}
		}
		else
		{
			++Batch.Compiled;
		}
	}

	ReportBatch(Batch, /*bAnnounceSuccess*/true);
	return Found;
}

#undef LOCTEXT_NAMESPACE
