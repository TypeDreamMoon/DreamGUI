// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceWatcher.h"

#include "DreamGUI.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIDocument.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUITextWriteBack.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Ticker.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "IDirectoryWatcher.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
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

	/** This editor session's watcher state, or null outside a session. */
	FDreamUISourceWatcherState* Session()
	{
		UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
		return EditorSubsystem != nullptr ? &EditorSubsystem->GetSourceWatcher() : nullptr;
	}

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

	/**
	 * The file a Blueprint is built from, as the one spelling every comparison here uses; empty when it names none.
	 *
	 * Resolved and normalised. The stored path may be relative, plugin-qualified or absolute, and the watcher only
	 * ever has the absolute one -- comparing the spellings would make a file rebuild or not depending on how its
	 * class happened to name it. Shared by the save path and the class resolver, so the two cannot come to disagree
	 * about which Blueprint a file belongs to.
	 */
	FString GetResolvedSourceKey(const UDreamWidgetBlueprint* InBlueprint)
	{
		const FString Authored = DreamUITextAuthoring::GetAuthoredSourcePath(InBlueprint);
		return Authored.IsEmpty()
			? FString()
			: FDreamUIDocumentRegistry::NormalizePath(UDreamTextUserWidget::ResolveDuiFilePath(Authored));
	}

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
			const FString Resolved = GetResolvedSourceKey(Blueprint);
			if (!Resolved.IsEmpty() && Resolved.Equals(Normalized, ESearchCase::IgnoreCase))
			{
				OutBlueprints.Add(Blueprint);
			}
		}
	}

	/**
	 * What FindClassForSource has learned this process: which Blueprint each file belongs to, and which Blueprint
	 * packages it has already read the Source File of. Process-wide rather than the editor session's, because the
	 * resolver serves the compiler, and the compiler runs where no session does.
	 */
	struct FSourceClassIndex
	{
		/** Resolved, normalised source path (lowercased, the comparison being case-blind) -> the Blueprint built from it. */
		TMap<FString, FSoftObjectPath> BlueprintBySource;
		/** Examined package -> its normalized source key; empty for no source or a failed load. */
		TMap<FName, FString> ExaminedPackages;
		FDelegateHandle AssetRemovedHandle;
		FDelegateHandle AssetRenamedHandle;
		FDelegateHandle AssetUpdatedOnDiskHandle;
		FDelegateHandle ObjectPreSaveHandle;
		/** Set while a lookup runs: a load it causes that asks again is answered null, not with a second scan. */
		bool bResolving = false;
	};

	FSourceClassIndex& SourceClassIndex()
	{
		static FSourceClassIndex Index;
		return Index;
	}

	FString IndexKey(const FString& InNormalizedPath)
	{
		return InNormalizedPath.ToLower();
	}

	/** Forget a changed asset and reopen discovery for any source whose selected owner it was. */
	void ForgetPackage(const FName InPackageName)
	{
		FSourceClassIndex& Index = SourceClassIndex();
		Index.ExaminedPackages.Remove(InPackageName);
		TArray<FString> UnclaimedSources;
		for (auto It = Index.BlueprintBySource.CreateIterator(); It; ++It)
		{
			if (It.Value().GetLongPackageFName() == InPackageName)
			{
				UnclaimedSources.Add(It.Key());
				It.RemoveCurrent();
			}
		}
		// The selected class may have shared its file. Its other owners must be discoverable after collection too.
		for (auto It = Index.ExaminedPackages.CreateIterator(); It; ++It)
		{
			if (UnclaimedSources.Contains(It.Value())) It.RemoveCurrent();
		}
	}

	/** Remember where InBlueprint is built from, whatever was asked: the next question is likely about another file. */
	void NoteBlueprintSource(UDreamWidgetBlueprint* InBlueprint)
	{
		FSourceClassIndex& Index = SourceClassIndex();
		const FName PackageName = InBlueprint->GetOutermost()->GetFName();
		const FString Key = IndexKey(GetResolvedSourceKey(InBlueprint));
		if (const FString* Previous = Index.ExaminedPackages.Find(PackageName); Previous != nullptr && *Previous != Key)
		{
			ForgetPackage(PackageName);
		}
		Index.ExaminedPackages.Add(PackageName, Key);
		if (!Key.IsEmpty())
		{
			const FSoftObjectPath Candidate(InBlueprint);
			const FSoftObjectPath* Known = Index.BlueprintBySource.Find(Key);
			// Incidental observations use the same choice as the requested file's sorted loaded matches.
			if (Known == nullptr || Candidate.ToString() < Known->ToString())
			{
				Index.BlueprintBySource.Add(Key, Candidate);
			}
		}
	}

	/** The class a Blueprint compiles into, as a node type wants it: the real one, never a REINST stand-in. */
	UClass* GetAuthoritativeClass(const UDreamWidgetBlueprint* InBlueprint)
	{
		return InBlueprint != nullptr && InBlueprint->GeneratedClass != nullptr
			? InBlueprint->GeneratedClass->GetAuthoritativeClass() : nullptr;
	}

	UClass* FindClassForSourceImpl(const FString& InResolvedSourcePath)
	{
		FSourceClassIndex& Index = SourceClassIndex();
		const FString Normalized = FDreamUIDocumentRegistry::NormalizePath(InResolvedSourcePath);
		if (Normalized.IsEmpty())
		{
			return nullptr;
		}
		const FString Key = IndexKey(Normalized);
		auto IsBuiltFrom = [&Normalized](const UDreamWidgetBlueprint* InBlueprint)
		{
			return IsValid(InBlueprint) && GetResolvedSourceKey(InBlueprint).Equals(Normalized, ESearchCase::IgnoreCase);
		};

		// 1. The answer from last time, asked again: the Blueprint may have been given another Source File since, and
		// a stale answer is a host built from the wrong component.
		if (const FSoftObjectPath* Known = Index.BlueprintBySource.Find(Key))
		{
			// Loading can deliver asset notifications that mutate the index.
			const FSoftObjectPath KnownPath = *Known;
			UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(KnownPath.ResolveObject());
			if (Blueprint == nullptr)
			{
				// Known, and collected since: brought back, which is what the soft path is kept for.
				Blueprint = Cast<UDreamWidgetBlueprint>(KnownPath.TryLoad());
			}
			if (IsBuiltFrom(Blueprint) && GetAuthoritativeClass(Blueprint) != nullptr)
			{
				return GetAuthoritativeClass(Blueprint);
			}
			ForgetPackage(KnownPath.GetLongPackageFName());
		}

		// 2. Every loaded Blueprint, which costs nothing to ask and is where the answer almost always is: the component
		// a screen uses is open, or was compiled, or was loaded by the last screen that used it.
		TArray<UDreamWidgetBlueprint*> Matches;
		for (TObjectIterator<UDreamWidgetBlueprint> It; It; ++It)
		{
			UDreamWidgetBlueprint* Blueprint = *It;
			if (!IsValid(Blueprint) || Blueprint->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			NoteBlueprintSource(Blueprint);
			if (IsBuiltFrom(Blueprint) && GetAuthoritativeClass(Blueprint) != nullptr)
			{
				Matches.Add(Blueprint);
			}
		}
		if (Matches.Num() > 0)
		{
			// Two classes built from one file is legitimate (a file a native parent shares across subclasses), and
			// there is no right one for an alias to mean. The same one every time, then, and said: the `class` line
			// is how the file names the one it means.
			Matches.Sort([](const UDreamWidgetBlueprint& A, const UDreamWidgetBlueprint& B)
			{
				return A.GetPathName() < B.GetPathName();
			});
			if (Matches.Num() > 1)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %d Blueprints are built from '%s'; a component alias of it means '%s'. Give the file a 'class' line to choose."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, Matches.Num(), *InResolvedSourcePath, *GetPathNameSafe(Matches[0]));
			}
			Index.BlueprintBySource.Add(Key, FSoftObjectPath(Matches[0]));
			return GetAuthoritativeClass(Matches[0]);
		}

		// 3. The widget Blueprints not read yet. Only the registry knows they exist and only loading one says what file
		// it is built from -- the Source File is a class default, which no registry tag carries -- so each is loaded
		// once, remembered, and never loaded for this question again. Likeliest first, so the usual project (one
		// component per file, named after it) loads one asset and stops.
		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		if (AssetRegistry == nullptr)
		{
			return nullptr;
		}
		FARFilter Filter;
		Filter.ClassPaths.Add(UDreamWidgetBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		AssetRegistry->GetAssets(Filter, Assets);
		Assets.RemoveAll([&Index](const FAssetData& InAsset)
		{
			return Index.ExaminedPackages.Contains(InAsset.PackageName);
		});
		const FString FileStem = FPaths::GetBaseFilename(Normalized);
		auto Likeness = [&FileStem](const FAssetData& InAsset)
		{
			const FString AssetName = InAsset.AssetName.ToString();
			if (AssetName.Equals(FileStem, ESearchCase::IgnoreCase))
			{
				return 0;
			}
			return AssetName.EndsWith(TEXT("_") + FileStem, ESearchCase::IgnoreCase) || AssetName.Contains(FileStem) ? 1 : 2;
		};
		Assets.StableSort([&Likeness](const FAssetData& A, const FAssetData& B)
		{
			return Likeness(A) < Likeness(B);
		});
		for (const FAssetData& Asset : Assets)
		{
			Index.ExaminedPackages.Add(Asset.PackageName, FString());
			UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Asset.GetAsset());
			if (Blueprint == nullptr)
			{
				continue;
			}
			NoteBlueprintSource(Blueprint);
			if (IsBuiltFrom(Blueprint) && GetAuthoritativeClass(Blueprint) != nullptr)
			{
				return GetAuthoritativeClass(Blueprint);
			}
		}
		return nullptr;
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

	void RecompileFor(FDreamUISourceWatcherState& State, const FString& InFilePath, FBatchResult& OutBatch)
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

		// Everything from here on is "the file on disk won". The compiler asks, and skips the flush that
		// would otherwise push the designer's unsaved preview values back over the text that just
		// arrived -- see FDreamUISourceWatcher::IsCompilingFromExternalChange.
		TGuardValue<bool> ExternalChangeGuard(State.bCompilingFromExternalChange, true);

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
			else if (GEditor != nullptr && GEditor->Trans != nullptr)
			{
				// The contract UDreamUIDocument::LoadFromFile writes down, and which nothing in the
				// codebase was honouring. A reload is deliberately NOT transacted, so every entry still
				// on the undo stack describes text from BEFORE it -- and undoing past the reload writes
				// that older text back over the file, which is a Ctrl+Z silently discarding somebody
				// else's edit. The header says the host's move is a barrier; this is the host.
				GEditor->Trans->SetUndoBarrier();
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

	FString NormalizeImportKey(const FString& InPath)
	{
		FString Key = InPath;
		FPaths::NormalizeFilename(Key);
		return Key.ToLower();
	}

	/**
	 * Every `use "..."` spelling in one file's text, without parsing it.
	 *
	 * Comments are stripped properly because a commented-out `use` is exactly the shape an author
	 * leaves behind, and an edge from one means saving a library recompiles a screen that no longer
	 * wears it -- harmless but confusing, which is worse than it sounds when the question being asked
	 * is "why did that recompile". Everything else is deliberately crude: this only has to find the
	 * keyword and the quoted string after it, and a spelling that resolves to nothing is dropped by
	 * the caller rather than reported. The parser remains the authority; this is an INDEX.
	 */
	void ScanImportSpellings(const FString& InText, TArray<FString>& OutSpellings)
	{
		const TCHAR* Chars = *InText;
		const int32 Length = InText.Len();
		for (int32 Offset = 0; Offset < Length; ++Offset)
		{
			const TCHAR Char = Chars[Offset];
			if (Char == TEXT('/') && Offset + 1 < Length && Chars[Offset + 1] == TEXT('/'))
			{
				while (Offset < Length && Chars[Offset] != TEXT('\n'))
				{
					++Offset;
				}
				continue;
			}
			if (Char == TEXT('/') && Offset + 1 < Length && Chars[Offset + 1] == TEXT('*'))
			{
				Offset += 2;
				while (Offset + 1 < Length && !(Chars[Offset] == TEXT('*') && Chars[Offset + 1] == TEXT('/')))
				{
					++Offset;
				}
				++Offset;
				continue;
			}
			if (Char == TEXT('"'))
			{
				// Skipped whole, so a path that happens to contain the word `use` cannot be read as
				// a second directive.
				++Offset;
				while (Offset < Length && Chars[Offset] != TEXT('"') && Chars[Offset] != TEXT('\n'))
				{
					++Offset;
				}
				continue;
			}
			if (Char != TEXT('u') || Offset + 3 >= Length)
			{
				continue;
			}
			if (Chars[Offset + 1] != TEXT('s') || Chars[Offset + 2] != TEXT('e'))
			{
				continue;
			}
			// A whole word, so `used` and `Reuse` are not directives. The keyword is matched case
			// sensitively because the grammar matches keywords case sensitively.
			const bool bBoundedLeft = Offset == 0 || !(FChar::IsAlnum(Chars[Offset - 1]) || Chars[Offset - 1] == TEXT('_'));
			const TCHAR After = Chars[Offset + 3];
			if (!bBoundedLeft || FChar::IsAlnum(After) || After == TEXT('_'))
			{
				continue;
			}
			int32 Cursor = Offset + 3;
			while (Cursor < Length && (Chars[Cursor] == TEXT(' ') || Chars[Cursor] == TEXT('\t')))
			{
				++Cursor;
			}
			if (Cursor >= Length || Chars[Cursor] != TEXT('"'))
			{
				continue;
			}
			const int32 Start = ++Cursor;
			while (Cursor < Length && Chars[Cursor] != TEXT('"') && Chars[Cursor] != TEXT('\n'))
			{
				++Cursor;
			}
			if (Cursor < Length && Chars[Cursor] == TEXT('"') && Cursor > Start)
			{
				OutSpellings.Add(InText.Mid(Start, Cursor - Start));
			}
			Offset = Cursor;
		}
	}

	/** Republish one file's `use` edges from what is on disk right now. */
	void NoteImportsFromDisk(const FString& InFilePath)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InFilePath))
		{
			return;
		}
		TArray<FString> Spellings;
		ScanImportSpellings(Text, Spellings);

		TArray<FString> Resolved;
		Resolved.Reserve(Spellings.Num());
		for (const FString& Spelling : Spellings)
		{
			const FString Path = DreamUIPaths::Resolve(Spelling);
			if (!Path.IsEmpty())
			{
				Resolved.AddUnique(Path);
			}
		}
		FDreamUISourceWatcher::NoteImports(InFilePath, Resolved);
	}

	/**
	 * Fill the dependency table from DISK, once, before the first drain uses it.
	 *
	 * Until this existed the table was written by exactly one author -- the compiler, after a parse --
	 * so on a freshly opened editor it was EMPTY, and saving a style library recompiled nothing
	 * whatsoever. The workaround was to compile every importer by hand first, which is the state of
	 * affairs the watcher exists to abolish; worse, it failed silently, because "no importers" and
	 * "no importers known yet" look identical from the drain.
	 *
	 * Direct edges only, and that is enough: the drain expands the worklist through the table
	 * repeatedly, so A-uses-B and B-uses-C reaches A from a change to C without anybody storing the
	 * closure. The compiler's own NoteImports publishes the transitive list instead; the two coexist
	 * because an extra edge only ever means an extra hop the worklist would have taken anyway.
	 *
	 * Lazy rather than at Register(), so the cost lands on the first save rather than on editor
	 * startup, and so a DUI root created after startup (Open Workspace does this) is included.
	 */
	void EnsureImportIndexSeeded(FDreamUISourceWatcherState& State)
	{
		if (State.bImportIndexSeeded)
		{
			return;
		}
		State.bImportIndexSeeded = true;

		TArray<FString> Sources;
		DreamUIPaths::FindSourceFiles(Sources);
		for (const FString& Source : Sources)
		{
			// Left alone when the compiler already published this file's edges: its list is the
			// authoritative one (it comes from a real parse, with `use` failures resolved the way the
			// build resolved them), and replacing it with a text scan would be a downgrade.
			bool bAlreadyKnown = false;
			for (auto It = State.ImportEdges.CreateConstIterator(); It; ++It)
			{
				if (It.Value().Equals(Source, ESearchCase::IgnoreCase))
				{
					bAlreadyKnown = true;
					break;
				}
			}
			if (!bAlreadyKnown)
			{
				NoteImportsFromDisk(Source);
			}
		}
	}

	/**
	 * Say that a .dui the classes depend on is gone.
	 *
	 * Both a delete and a rename arrive here (a rename is a removal plus a creation), and after
	 * either one every loaded class still naming that path is stale with no way to find out: there is
	 * nothing left to rebuild it from, so it keeps the last tree it built and the author meets
	 * DUI6001 at some unrelated compile later. A warning at the moment it happens is the whole fix;
	 * nothing is recompiled, because recompiling is precisely what cannot be done.
	 */
	void ReportRemovals(const TArray<FString>& InRemoved)
	{
		TArray<FString> Orphaned;
		for (const FString& File : InRemoved)
		{
			if (FPaths::FileExists(File))
			{
				// Back already: a save-through-rename, or an author who undid the delete inside the
				// debounce window. The change event for the new contents is in PendingFiles.
				continue;
			}
			TArray<UDreamWidgetBlueprint*> Blueprints;
			FindBlueprints(File, Blueprints);
			if (Blueprints.Num() == 0)
			{
				UE_LOG(DreamGUI, Verbose, TEXT("[%s].%d '%s' is gone, and no loaded class is built from it."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *File);
				continue;
			}
			for (const UDreamWidgetBlueprint* Blueprint : Blueprints)
			{
				UE_LOG(DreamGUI, Warning,
					TEXT("[%s].%d '%s' is gone from disk, and '%s' is built from it -- that class now holds the last hierarchy it compiled, and its next compile will fail with DUI6001."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *File, *GetNameSafe(Blueprint));
				Orphaned.AddUnique(GetNameSafe(Blueprint));
			}
		}

		if (Orphaned.Num() == 0)
		{
			return;
		}
		FNotificationInfo Info(FText::Format(
			LOCTEXT("DreamUISourceRemoved",
				"DreamUI: a source file was deleted or renamed. {0} class(es) are now built from nothing: {1}"),
			FText::AsNumber(Orphaned.Num()), FText::FromString(FString::Join(Orphaned, TEXT(", ")))));
		Info.ExpireDuration = 12.0f;
		Info.bFireAndForget = true;
		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	void DrainQueue(FDreamUISourceWatcherState& State)
	{
		TArray<FString> Files = State.PendingFiles.Array();
		State.PendingFiles.Reset();
		Files.Sort();

		const TArray<FString> Removed = State.PendingRemovals.Array();
		State.PendingRemovals.Reset();

		// Before the table is read, never at Register(): see EnsureImportIndexSeeded.
		EnsureImportIndexSeeded(State);
		// And re-read the edges of the files that just changed, because a `use` line added to a file
		// with no class of its own is published by nobody -- the compiler only republishes for files
		// it compiles, and that file is never compiled.
		for (const FString& File : Files)
		{
			if (FPaths::FileExists(File))
			{
				NoteImportsFromDisk(File);
			}
		}

		const bool bAnnounceSuccess = State.bAnnounceSuccess;
		State.bAnnounceSuccess = false;

		// A changed file recompiles its importers too, transitively: saving the style library IS
		// saving every screen that wears it, as far as the classes are concerned. The worklist
		// carries a visited set so a diamond expands once and a (rejected, but defensive) cycle
		// terminates.
		// An own write suppresses its file's compile, but still changes what its importers read.
		// Track why a file was reached: an importer's unchanged content can match its document hash
		// even when a dependency changed, including when its own event arrived in the same batch.
		TArray<FString> Worklist = Files;
		TSet<FString> Visited;
		TSet<FString> OwnWrites;
		TSet<FString> DependencyAffected;
		for (const FString& File : Files)
		{
			const FString Key = NormalizeImportKey(File);
			Visited.Add(Key);
			if (IsOwnWriteComingBack(File))
			{
				OwnWrites.Add(Key);
			}
		}
		for (int32 Index = 0; Index < Worklist.Num(); ++Index)
		{
			TArray<FString> Importers;
			State.ImportEdges.MultiFind(NormalizeImportKey(Worklist[Index]), Importers);
			for (const FString& Importer : Importers)
			{
				const FString Key = NormalizeImportKey(Importer);
				DependencyAffected.Add(Key);
				bool bAlreadyVisited = false;
				Visited.Add(Key, &bAlreadyVisited);
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
			const FString Key = NormalizeImportKey(File);
			if (OwnWrites.Contains(Key) && !DependencyAffected.Contains(Key))
			{
				continue;
			}
			RecompileFor(State, File, Batch);
		}
		ReportBatch(Batch, bAnnounceSuccess);
		// After the rebuilds, so a rename whose two halves landed in one batch reports the new file's
		// compile first and does not then warn about the name it arrived under.
		ReportRemovals(Removed);
	}

	/**
	 * Offers a bulk batch instead of compiling it.
	 *
	 * The files are kept rather than dropped: dropping them leaves every one of those classes stale
	 * with nothing to say so, which is the failure this watcher exists to end -- just quieter.
	 */
	void OfferDeferredBatch(const FDreamUISourceWatcherState& State)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("DreamUIBulkBacklog",
				"DreamUI: {0} source files changed at once. Rebuilding them all now would queue that "
				"many Blueprint compiles."),
			FText::AsNumber(State.DeferredBulkFiles.Num())));
		Info.ExpireDuration = 30.0f;
		Info.bFireAndForget = true;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]
		{
			// The session's as it is when the link is clicked, which may be a while after the toast.
			FDreamUISourceWatcherState* Clicked = Session();
			if (Clicked == nullptr)
			{
				return;
			}
			Clicked->PendingFiles.Append(Clicked->DeferredBulkFiles);
			Clicked->DeferredBulkFiles.Reset();
			// Say so when it finishes, and skip the debounce: the author just asked for this, and
			// another three quarters of a second only makes the click feel broken.
			Clicked->bAnnounceSuccess = true;
			Clicked->LastChangeTime = 0.0;
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
		FDreamUISourceWatcherState* State = Session();
		if (State == nullptr || (State->PendingFiles.Num() == 0 && State->PendingRemovals.Num() == 0))
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
		if (UE::IsSavingPackage())
		{
			return true;
		}
		// Nor in the middle of something the compile would pull out from under the author: a drag in
		// flight holds widgets of the very preview a compile rebuilds, and an open transaction would take
		// the compile's changes into the edit it is recording. Both end on their own; the queue waits.
		if ((FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping())
			|| (GEditor != nullptr && GEditor->IsTransactionActive()))
		{
			return true;
		}
		if (FPlatformTime::Seconds() - State->LastChangeTime < DebounceSeconds)
		{
			return true;
		}

		// Gated on batch size whenever it arrives, rather than on being near startup. DreamFX
		// measured this exact question and found no startup replay at all -- the watcher begins
		// watching at registration, so changes made while the editor was closed produce nothing. The
		// batch that hurts is a bulk change while the editor is open.
		if (!State->bAnnounceSuccess && State->PendingFiles.Num() > BulkThreshold)
		{
			State->DeferredBulkFiles.Append(State->PendingFiles);
			State->PendingFiles.Reset();
			OfferDeferredBatch(*State);
			return true;
		}

		DrainQueue(*State);
		return true;
	}

	/**
	 * Watch one root, once.
	 *
	 * Shared by startup and by EnsureWatching so there is one answer to "is this already watched":
	 * registering a directory twice delivers every change to the queue twice, and the second handle
	 * is one nothing unregisters.
	 */
	void WatchRoot(FDreamUISourceWatcherState& State, const FString& InDirectory);

	void OnDirectoryChanged(const TArray<FFileChangeData>& InChanges)
	{
		FDreamUISourceWatcherState* State = Session();
		if (State == nullptr)
		{
			return;
		}
		for (const FFileChangeData& Change : InChanges)
		{
			if (!FPaths::GetExtension(Change.Filename, /*bIncludeDot*/true)
				.Equals(DreamUIPaths::SourceExtension, ESearchCase::IgnoreCase))
			{
				continue;
			}
			// The extension test comes FIRST now, which is what makes keeping removals affordable: an
			// editor's save-through-temp-file produces a removal for something that is not a .dui, and
			// those are the events that would make this noisy. A removal of a real .dui is queued for
			// the report pass -- both a delete and a rename arrive this way, and after either one a
			// class still naming that path is a class whose next compile cannot work.
			if (Change.Action == FFileChangeData::FCA_Removed)
			{
				State->PendingRemovals.Add(FDreamUIDocumentRegistry::NormalizePath(Change.Filename));
				continue;
			}
			const FString Normalized = FDreamUIDocumentRegistry::NormalizePath(Change.Filename);
			// A rename shows up as removed-then-added within one batch often enough that dropping the
			// stale removal here is worth the two lines: reporting a file gone and rebuilt in the same
			// drain would be a warning about nothing.
			State->PendingRemovals.Remove(Normalized);
			State->PendingFiles.Add(Normalized);
		}
		if (State->PendingFiles.Num() > 0 || State->PendingRemovals.Num() > 0)
		{
			State->LastChangeTime = FPlatformTime::Seconds();
		}
	}

	void WatchRoot(FDreamUISourceWatcherState& State, const FString& InDirectory)
	{
		if (State.WatchHandles.Contains(InDirectory))
		{
			return;
		}
		FDirectoryWatcherModule& Module =
			FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* Watcher = Module.Get();
		if (Watcher == nullptr)
		{
			return;
		}
		FDelegateHandle Handle;
		if (Watcher->RegisterDirectoryChangedCallback_Handle(
			InDirectory,
			IDirectoryWatcher::FDirectoryChanged::CreateStatic(&OnDirectoryChanged),
			Handle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
		{
			State.WatchHandles.Add(InDirectory, Handle);
			UE_LOG(DreamGUI, Display, TEXT("[%s].%d Watching '%s' for .dui changes."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InDirectory);
		}
	}
}

void FDreamUISourceWatcher::NoteImports(const FString& InImporter, const TArray<FString>& InImports)
{
	using namespace DreamUISourceWatcherLocal;
	FDreamUISourceWatcherState* State = Session();
	if (State == nullptr)
	{
		return;
	}
	// Replace, not append: a file that dropped a `use` line must stop recompiling on that library's
	// saves, and the compiler republished the WHOLE current list.
	for (auto It = State->ImportEdges.CreateIterator(); It; ++It)
	{
		if (It.Value() == InImporter)
		{
			It.RemoveCurrent();
		}
	}
	for (const FString& Import : InImports)
	{
		State->ImportEdges.Add(NormalizeImportKey(Import), InImporter);
	}
}

void FDreamUISourceWatcher::Register(FDreamUISourceWatcherState& InState)
{
	using namespace DreamUISourceWatcherLocal;

	for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
	{
		WatchRoot(InState, Root.Directory);
	}

	// The ticker drains the queue, so it exists even with nothing watched: a project with no DUI
	// directory yet still has menu commands that queue through this same path, and a queue with no
	// drain is a command that silently does nothing.
	InState.TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&Tick), /*InDelay=*/0.25f);
}

void FDreamUISourceWatcher::Unregister(FDreamUISourceWatcherState& InState)
{
	using namespace DreamUISourceWatcherLocal;

	if (InState.TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(InState.TickerHandle);
		InState.TickerHandle.Reset();
	}
	if (FDirectoryWatcherModule* Module =
		FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			for (const TPair<FString, FDelegateHandle>& Entry : InState.WatchHandles)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Entry.Key, Entry.Value);
			}
		}
	}
	// The dependency table goes with the session that built it: the next session seeds its own from
	// disk before its first drain.
	InState = FDreamUISourceWatcherState();
}

void FDreamUISourceWatcher::EnsureWatching(const FString& InDirectory)
{
	using namespace DreamUISourceWatcherLocal;

	FDreamUISourceWatcherState* State = Session();
	if (State == nullptr || !State->TickerHandle.IsValid())
	{
		// Nothing drains the queue, so nothing would come of watching: either Register has not run
		// yet (it will, and it will pick this root up itself) or the session is ending.
		return;
	}

	// Matched against DreamUIPaths' spelling of the roots rather than registered under the caller's.
	// That namespace is the one place that decides what a root path looks like -- absolute, forward
	// slashes, exactly one trailing slash -- and a second opinion here would put a duplicate entry
	// in the watch table the first time the two disagreed about the slash, leaving a live callback
	// behind at Unregister. It also means a directory that is not a root is silently ignored, which
	// is the right answer to "watch this" for a path the language cannot resolve anything under.
	FString Wanted = FPaths::ConvertRelativePathToFull(InDirectory);
	FPaths::NormalizeDirectoryName(Wanted);

	for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
	{
		FString RootDirectory = Root.Directory;
		FPaths::NormalizeDirectoryName(RootDirectory);
		if (RootDirectory.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			WatchRoot(*State, Root.Directory);
			return;
		}
	}
}

bool FDreamUISourceWatcher::IsCompilingFromExternalChange()
{
	const FDreamUISourceWatcherState* State = DreamUISourceWatcherLocal::Session();
	return State != nullptr && State->bCompilingFromExternalChange;
}

void FDreamUISourceWatcher::QueueFile(const FString& InFilePath, const bool bAnnounceSuccess)
{
	using namespace DreamUISourceWatcherLocal;
	FDreamUISourceWatcherState* State = Session();
	if (State == nullptr)
	{
		return;
	}
	State->PendingFiles.Add(FDreamUIDocumentRegistry::NormalizePath(InFilePath));
	State->LastChangeTime = FPlatformTime::Seconds();
	State->bAnnounceSuccess |= bAnnounceSuccess;
}

void FDreamUISourceWatcher::QueueRemoval(const FString& InFilePath)
{
	using namespace DreamUISourceWatcherLocal;
	FDreamUISourceWatcherState* State = Session();
	if (State == nullptr)
	{
		return;
	}
	State->PendingRemovals.Add(FDreamUIDocumentRegistry::NormalizePath(InFilePath));
	State->LastChangeTime = FPlatformTime::Seconds();
}

void FDreamUISourceWatcher::FlushPending()
{
	using namespace DreamUISourceWatcherLocal;
	if (FDreamUISourceWatcherState* State = Session())
	{
		State->LastChangeTime = 0.0;
	}
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

UClass* FDreamUISourceWatcher::FindClassForSource(const FString& InResolvedSourcePath)
{
	using namespace DreamUISourceWatcherLocal;

	// Asked from inside a compile, and able to load: a Blueprint loaded here may reach the builder again before this
	// returns -- its own aliases, resolved on load -- and a second scan started from the middle of the first would
	// examine the same packages twice and load whatever the first had not reached yet. The inner question is
	// answered null, which the builder reports as an alias it could not resolve, on a line of the file being loaded.
	FSourceClassIndex& Index = SourceClassIndex();
	if (Index.bResolving)
	{
		return nullptr;
	}
	TGuardValue<bool> ResolvingGuard(Index.bResolving, true);
	return FindClassForSourceImpl(InResolvedSourcePath);
}

void FDreamUISourceWatcher::InstallSourceClassResolver()
{
	using namespace DreamUISourceWatcherLocal;

	FDreamUITextBuilder::SourceClassResolver() = [](const FString& InResolvedSourcePath) -> UClass*
	{
		return FDreamUISourceWatcher::FindClassForSource(InResolvedSourcePath);
	};

	// A loaded change is observed on lookup; a saved change must also invalidate what remains after collection.
	FSourceClassIndex& Index = SourceClassIndex();
	Index.ObjectPreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddLambda([](UObject* InObject, FObjectPreSaveContext)
	{
		if (UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(InObject))
		{
			ForgetPackage(Blueprint->GetOutermost()->GetFName());
		}
	});
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		Index.AssetUpdatedOnDiskHandle = AssetRegistry->OnAssetUpdatedOnDisk().AddLambda([](const FAssetData& InAsset)
		{
			ForgetPackage(InAsset.PackageName);
		});
		Index.AssetRemovedHandle = AssetRegistry->OnAssetRemoved().AddLambda([](const FAssetData& InAsset)
		{
			ForgetPackage(InAsset.PackageName);
		});
		Index.AssetRenamedHandle = AssetRegistry->OnAssetRenamed().AddLambda([](const FAssetData& InAsset, const FString& InOldObjectPath)
		{
			ForgetPackage(InAsset.PackageName);
			ForgetPackage(FName(*FPackageName::ObjectPathToPackageName(InOldObjectPath)));
		});
	}
}

void FDreamUISourceWatcher::UninstallSourceClassResolver()
{
	using namespace DreamUISourceWatcherLocal;

	FDreamUITextBuilder::SourceClassResolver() = nullptr;
	FSourceClassIndex& Index = SourceClassIndex();
	FCoreUObjectDelegates::OnObjectPreSave.Remove(Index.ObjectPreSaveHandle);
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetUpdatedOnDisk().Remove(Index.AssetUpdatedOnDiskHandle);
		AssetRegistry->OnAssetRemoved().Remove(Index.AssetRemovedHandle);
		AssetRegistry->OnAssetRenamed().Remove(Index.AssetRenamedHandle);
	}
	Index = FSourceClassIndex();
}

#undef LOCTEXT_NAMESPACE
