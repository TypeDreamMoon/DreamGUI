// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceWatcher.h"

#include "DreamGUI.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIBridgeService.h"
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
#include "Logging/TokenizedMessage.h"
#include "Misc/CoreDelegates.h"
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
	 *
	 * Short, because the interval no longer has to cover the designer's own writes: those are told
	 * apart by content (UDreamUIDocument::IsOwnWrite), not by waiting, so all it has to span is the
	 * burst of events one save produces.
	 */
	constexpr float DebounceSeconds = 0.4f;

	/**
	 * How long after a .dui event or a bridge request the editor is kept out of background throttling.
	 * An editor behind the author's text editor runs at 3 FPS otherwise, and the save the author just made
	 * would wait for the next of those frames at every step between the file system and the designer.
	 */
	constexpr double FullRateHoldSeconds = 3.0;

	/** The compile verdict toast of a plain save: long enough to read a file name, short enough not to be furniture. */
	constexpr float AppliedToastSeconds = 1.5f;
	constexpr float AppliedToastFadeInSeconds = 0.15f;
	constexpr float AppliedToastFadeOutSeconds = 0.4f;

	/** How long a source nobody was found for stays remembered as such, whatever else happens. */
	constexpr double UnresolvedSourceLifetimeSeconds = 5.0;

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

	/**
	 * The state ShouldRunAtFullRate reads. Held directly because the editor asks it every frame, and Session() is a
	 * subsystem lookup; set when the session registers the watcher and cleared when it unregisters.
	 */
	const FDreamUISourceWatcherState* GThrottleState = nullptr;

	/** What one drained queue did, and where to send the author when it did not work. */
	struct FBatchResult
	{
		int32 Compiled = 0;
		int32 Unclaimed = 0;
		int32 Failed = 0;
		/** Files whose text was what the last rebuild already read, so nothing was rebuilt. */
		int32 Unchanged = 0;

		/** Files (clean names) every class of which rebuilt, for the toast of a plain save. */
		TArray<FString> AppliedFiles;

		FString FirstErrorFile;
		FString FirstErrorText;
		/**
		 * The first error's own message token, when the compiler gave it a way to jump to its line. The toast
		 * runs that rather than guessing a position from the text: the compiler knows which file the error is in.
		 */
		TSharedPtr<IMessageToken> FirstErrorToken;

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
		/**
		 * Sources (lowercased keys) a complete lookup found no Blueprint for. Asked again, they are answered null at
		 * once instead of walking every loaded Blueprint and the registry's widget Blueprints again; the answer can
		 * only change when an asset appears, goes, moves or compiles, and each of those empties this.
		 */
		TSet<FString> UnresolvedSources;
		/** A Source File spelling -> the lookup key it resolves to, so a Blueprint is resolved once per spelling, not once per question. */
		TMap<FString, FString> ResolvedKeyBySpelling;
		/** When the oldest entry of either cache above was filed; the caches are dropped whole after UnresolvedSourceLifetimeSeconds. */
		double CachesFiledAt = 0.0;
		FDelegateHandle AssetAddedHandle;
		FDelegateHandle AssetRemovedHandle;
		FDelegateHandle AssetRenamedHandle;
		FDelegateHandle AssetUpdatedOnDiskHandle;
		FDelegateHandle ObjectPreSaveHandle;
		FDelegateHandle BlueprintCompiledHandle;
		FDelegateHandle PostEngineInitHandle;
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

	/** Empty both lookup caches: something that can change which Blueprint a file belongs to has happened. */
	void ClearLookupCaches()
	{
		FSourceClassIndex& Index = SourceClassIndex();
		Index.UnresolvedSources.Reset();
		Index.ResolvedKeyBySpelling.Reset();
	}

	/**
	 * Drop the lookup caches once they are old. Nothing here can see every way the answer changes -- a plugin's
	 * DUI directory appearing, a file created behind a relative spelling -- so no entry is believed for long.
	 */
	void ExpireLookupCaches()
	{
		FSourceClassIndex& Index = SourceClassIndex();
		if ((!Index.UnresolvedSources.IsEmpty() || !Index.ResolvedKeyBySpelling.IsEmpty())
			&& FPlatformTime::Seconds() - Index.CachesFiledAt > UnresolvedSourceLifetimeSeconds)
		{
			ClearLookupCaches();
		}
	}

	/** Stamp the caches as holding something, if they held nothing until now. */
	void NoteLookupCacheFiled()
	{
		FSourceClassIndex& Index = SourceClassIndex();
		if (Index.UnresolvedSources.IsEmpty() && Index.ResolvedKeyBySpelling.IsEmpty())
		{
			Index.CachesFiledAt = FPlatformTime::Seconds();
		}
	}

	/**
	 * The lookup key of the file a Blueprint is built from: GetResolvedSourceKey, lowercased, and remembered by
	 * the Source File spelling it came from.
	 *
	 * A lookup that misses asks this of every loaded widget Blueprint, and resolving a spelling is not free -- a
	 * relative one is searched across the roots with a stat each, and the lookup compared each Blueprint twice. The
	 * spelling is what the answer is a function of, so a Blueprint given another Source File is a different entry,
	 * never a stale one; what can still go stale is the file system under a spelling, which ExpireLookupCaches bounds.
	 */
	FString ResolvedLookupKey(const UDreamWidgetBlueprint* InBlueprint)
	{
		const FString Authored = DreamUITextAuthoring::GetAuthoredSourcePath(InBlueprint);
		if (Authored.IsEmpty())
		{
			return FString();
		}
		FSourceClassIndex& Index = SourceClassIndex();
		if (const FString* Known = Index.ResolvedKeyBySpelling.Find(Authored))
		{
			return *Known;
		}
		const FString Key = IndexKey(GetResolvedSourceKey(InBlueprint));
		NoteLookupCacheFiled();
		Index.ResolvedKeyBySpelling.Add(Authored, Key);
		return Key;
	}

	/** Forget a changed asset and reopen discovery for any source whose selected owner it was. */
	void ForgetPackage(const FName InPackageName)
	{
		FSourceClassIndex& Index = SourceClassIndex();
		ClearLookupCaches();
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
		const FString Key = ResolvedLookupKey(InBlueprint);
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
		auto IsBuiltFrom = [&Key](const UDreamWidgetBlueprint* InBlueprint)
		{
			return IsValid(InBlueprint) && ResolvedLookupKey(InBlueprint).Equals(Key, ESearchCase::IgnoreCase);
		};

		// 0. A source a complete lookup already found nobody for. Asked again before anything that could have given it
		// an owner has happened, so the answer is the same and the walk below is not repeated.
		ExpireLookupCaches();
		if (Index.UnresolvedSources.Contains(Key))
		{
			return nullptr;
		}

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

		// Every widget Blueprint the registry knows has been read and none is built from this file. Remembered, unless
		// the registry has not finished listing: its answer is then incomplete, and a miss remembered from it would
		// outlive the scan that completes it.
		if (!AssetRegistry->IsLoadingAssets())
		{
			NoteLookupCacheFiled();
			Index.UnresolvedSources.Add(Key);
		}
		return nullptr;
	}

	/**
	 * The hash of a file's text as the document hashes it, or empty when it cannot be read.
	 *
	 * One definition (UDreamUIDocument::ComputeContentHash) for every question the watcher asks of a file's
	 * content -- is it our own write, is it what we already built -- so they cannot disagree about equal text.
	 */
	FString ReadContentHash(const FString& InFilePath)
	{
		FString DiskContent;
		if (!FFileHelper::LoadFileToString(DiskContent, *InFilePath))
		{
			return FString();
		}
		return UDreamUIDocument::ComputeContentHash(DiskContent);
	}

	/**
	 * True when the change on disk is the designer's own write coming back.
	 *
	 * Only askable when a document is open for the file, which is exactly when the designer could
	 * have written it. With no document there is nothing of ours to mistake a foreign edit for.
	 *
	 * Takes the file's hash rather than reading it: the drain has already read each changed file once, and
	 * an unreadable file (empty hash) is not "ours" -- letting it through means the compile reports the
	 * real problem instead of this function silently deciding there is none.
	 */
	bool IsOwnWriteComingBack(const FString& InFilePath, const FString& InDiskHash)
	{
		const UDreamUIDocument* Document = FDreamUIDocumentRegistry::Find(InFilePath);
		return Document != nullptr && !InDiskHash.IsEmpty() && Document->GetLastWrittenHash() == InDiskHash;
	}

	FString NormalizeImportKey(const FString& InPath)
	{
		FString Key = InPath;
		FPaths::NormalizeFilename(Key);
		return Key.ToLower();
	}

	/** Notifications are Slate; a commandlet or an automation run without a UI must not try to draw one. */
	bool CanShowNotifications()
	{
		return FSlateApplication::IsInitialized() && !IsRunningCommandlet();
	}

	/**
	 * The notices about a file whose designer text and disk text disagreed share one toast while it is on screen.
	 *
	 * "Your change was refused" arrives first, on every gesture until the outside edit is brought in, and "the outside
	 * edit replaced it" arrives last; as two stacks of toasts the author reads the same event several times, and as one
	 * that is rewritten in place they read it once, with the final word.
	 */
	struct FConflictToast
	{
		TWeakPtr<SNotificationItem> Item;
		double CreatedAt = 0.0;
	};

	TMap<FString, FConflictToast>& ConflictToasts()
	{
		static TMap<FString, FConflictToast> Toasts;
		return Toasts;
	}

	constexpr float ConflictToastSeconds = 12.0f;

	void ShowConflictToast(const FString& InFilePath, const FText& InText,
		const FSimpleDelegate& InLink = FSimpleDelegate(), const FText& InLinkText = FText())
	{
		if (!CanShowNotifications())
		{
			return;
		}
		FConflictToast& Toast = ConflictToasts().FindOrAdd(NormalizeImportKey(InFilePath));
		const double Now = FPlatformTime::Seconds();
		// Reused while it can still be read: a toast past its expiry is already fading, and rewriting that one would
		// put the new text on something about to disappear.
		if (const TSharedPtr<SNotificationItem> Existing = Toast.Item.Pin();
			Existing.IsValid() && Now - Toast.CreatedAt < ConflictToastSeconds * 0.8)
		{
			Existing->SetText(InText);
			if (InLink.IsBound())
			{
				Existing->SetHyperlink(InLink, InLinkText);
			}
			return;
		}

		FNotificationInfo Info(InText);
		Info.ExpireDuration = ConflictToastSeconds;
		Info.bFireAndForget = true;
		if (InLink.IsBound())
		{
			Info.Hyperlink = InLink;
			Info.HyperlinkText = InLinkText;
		}
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
		Toast.Item = Item;
		Toast.CreatedAt = Now;
	}

	/**
	 * Say what an outside change did to the editor's state: it replaced designer text that never reached the file,
	 * and/or it cut the undo history. One toast for both, since both are the same event.
	 */
	void NotifyExternalReload(const FString& InFilePath, bool bInDiscardedDesignerText,
		const FString& InSavedCopyPath, bool bInUndoCut)
	{
		if (!bInDiscardedDesignerText && !bInUndoCut)
		{
			return;
		}
		const FText FileName = FText::FromString(FPaths::GetCleanFilename(InFilePath));

		FText Text;
		if (bInDiscardedDesignerText)
		{
			Text = InSavedCopyPath.IsEmpty()
				? FText::Format(LOCTEXT("DreamUIDesignerEditsReplaced",
					"DreamUI: {0} changed outside the editor, and the designer's edits that had not been written to it were replaced by the external version."),
					FileName)
				: FText::Format(LOCTEXT("DreamUIDesignerEditsReplacedSaved",
					"DreamUI: {0} changed outside the editor, and the designer's edits that had not been written to it were replaced by the external version. The replaced text was saved to {1}"),
					FileName, FText::FromString(InSavedCopyPath));
		}
		if (bInUndoCut)
		{
			const FText UndoText = FText::Format(LOCTEXT("DreamUIUndoHistoryCut",
				"{0} changed outside the editor; the undo history was cut at this point."), FileName);
			Text = Text.IsEmpty() ? FText::Format(LOCTEXT("DreamUIUndoHistoryCutPrefixed", "DreamUI: {0}"), UndoText)
				: FText::Format(LOCTEXT("DreamUIConflictTwoLines", "{0}\n{1}"), Text, UndoText);
		}

		FSimpleDelegate Link;
		FText LinkText;
		if (!InSavedCopyPath.IsEmpty())
		{
			const FString Copy = InSavedCopyPath;
			Link = FSimpleDelegate::CreateLambda([Copy]
			{
				FPlatformProcess::LaunchFileInDefaultExternalApplication(*Copy, nullptr, ELaunchVerb::Open);
			});
			LinkText = LOCTEXT("DreamUIOpenSavedCopy", "Open the saved copy");
		}
		ShowConflictToast(InFilePath, Text, Link, LinkText);
	}

	/**
	 * Whether anything an undo could still reach touches the document or an asset built from its file.
	 *
	 * A reload is not transacted, so every such entry describes text from before it and undoing it would write that
	 * text back over somebody else's edit -- which is what the undo barrier prevents. But the barrier is global: it
	 * stops undo for the whole editor, level edits included. So it is set only when there is something to protect,
	 * which is a transaction (from the barrier already standing, if any, up to the newest undoable entry) that
	 * holds the document or any object in the Blueprint's package.
	 */
	bool UndoHistoryTouches(const UObject* InDocument, const TArray<UDreamWidgetBlueprint*>& InBlueprints)
	{
		if (GEditor == nullptr || GEditor->Trans == nullptr)
		{
			return false;
		}
		const UTransactor* Trans = GEditor->Trans;
		const int32 Newest = Trans->GetQueueLength() - Trans->GetUndoCount() - 1;
		const int32 Oldest = FMath::Max(0, Trans->GetCurrentUndoBarrier());
		if (Newest < Oldest)
		{
			return false;
		}

		TSet<const UPackage*> Packages;
		for (const UDreamWidgetBlueprint* Blueprint : InBlueprints)
		{
			if (IsValid(Blueprint))
			{
				Packages.Add(Blueprint->GetOutermost());
			}
		}

		TArray<UObject*> Objects;
		for (int32 Index = Newest; Index >= Oldest; --Index)
		{
			const FTransaction* Transaction = Trans->GetTransaction(Index);
			if (Transaction == nullptr)
			{
				continue;
			}
			if (InDocument != nullptr && Transaction->ContainsObject(InDocument))
			{
				return true;
			}
			if (Packages.IsEmpty())
			{
				continue;
			}
			Transaction->GetTransactionObjects(Objects);
			for (const UObject* Object : Objects)
			{
				if (Object != nullptr && Packages.Contains(Object->GetOutermost()))
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * One small toast for a save that worked, rewritten in place when another lands while it is still up.
	 *
	 * Stacked, a run of saves would draw a column of them; folded, the author sees one line that says the
	 * newest file was applied and goes away on its own. Not for explicit commands, which have their own summary.
	 */
	void ShowAppliedToast(const TArray<FString>& InFiles)
	{
		if (InFiles.IsEmpty() || !CanShowNotifications())
		{
			return;
		}
		const FText Text = InFiles.Num() == 1
			? FText::Format(LOCTEXT("DreamUIApplied", "DreamUI: applied {0}"), FText::FromString(InFiles[0]))
			: FText::Format(LOCTEXT("DreamUIAppliedMany", "DreamUI: applied {0} and {1} more"),
				FText::FromString(InFiles[0]), FText::AsNumber(InFiles.Num() - 1));

		static TWeakPtr<SNotificationItem> GToast;
		static double GToastCreatedAt = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (const TSharedPtr<SNotificationItem> Existing = GToast.Pin();
			Existing.IsValid() && Now - GToastCreatedAt < AppliedToastFadeInSeconds + AppliedToastSeconds)
		{
			Existing->SetText(Text);
			return;
		}

		FNotificationInfo Info(Text);
		Info.FadeInDuration = AppliedToastFadeInSeconds;
		Info.ExpireDuration = AppliedToastSeconds;
		Info.FadeOutDuration = AppliedToastFadeOutSeconds;
		Info.bUseThrobber = false;
		Info.bFireAndForget = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
		GToast = Item;
		GToastCreatedAt = Now;
	}

	/**
	 * Remember a failed compile's first ERROR -- its text and, when the compiler gave the message a way to jump to its
	 * line, that -- unless the batch already has one. The first error, not the first message: a warning that happens to
	 * come first says nothing about why the compile failed.
	 */
	void RecordFirstError(FBatchResult& OutBatch, const FString& InFile, const UDreamWidgetBlueprint* InBlueprint,
		const FCompilerResultsLog& InResults)
	{
		if (OutBatch.HasFirstError())
		{
			return;
		}
		OutBatch.FirstErrorFile = InFile;
		// The compiler's own first error, which already carries the DUInnnn code and the line and column.
		// Rewording it here would give the author two different sentences for one problem depending on
		// where they read it.
		const TSharedRef<FTokenizedMessage>* FirstError = InResults.Messages.FindByPredicate(
			[](const TSharedRef<FTokenizedMessage>& Message)
			{
				return Message->GetSeverity() == EMessageSeverity::Error;
			});
		OutBatch.FirstErrorText = FirstError != nullptr
			? (*FirstError)->ToText().ToString()
			: FString::Printf(TEXT("%s failed to compile"), *GetNameSafe(InBlueprint));
		if (FirstError != nullptr)
		{
			for (const TSharedRef<IMessageToken>& Token : (*FirstError)->GetMessageTokens())
			{
				if (Token->GetOnMessageTokenActivated().IsBound())
				{
					OutBatch.FirstErrorToken = Token;
					break;
				}
			}
		}
	}

	/** One notification for the whole batch, carrying a way to go to the error that failed it. */
	void ReportBatch(const FBatchResult& InBatch, bool bAnnounceSuccess)
	{
		if (InBatch.Failed == 0 && !bAnnounceSuccess)
		{
			// A save that worked stays out of the way: the designer redrawing is the feedback, and this line only
			// says which file it was.
			ShowAppliedToast(InBatch.AppliedFiles);
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
				const TSharedPtr<IMessageToken> Token = InBatch.FirstErrorToken;
				// The error's own token knows its file, line and column -- the compiler put them there -- so the
				// toast runs it instead of guessing a position out of the text. An error with none (one that is
				// not about a place in a .dui) still takes the author to the file.
				Info.Hyperlink = FSimpleDelegate::CreateLambda([File, Token]
				{
					if (Token.IsValid() && Token->GetOnMessageTokenActivated().IsBound())
					{
						Token->GetOnMessageTokenActivated().ExecuteIfBound(Token.ToSharedRef());
						return;
					}
					FDreamUIBridgeService::OpenAtLocation(File, 1, 1);
				});
				Info.HyperlinkText = Token.IsValid() && Token->GetOnMessageTokenActivated().IsBound()
					? LOCTEXT("DreamUIGoToError", "Go to the error")
					: LOCTEXT("DreamUIOpenFailedSource", "Open the file");
			}
		}
		else if (InBatch.Compiled == 0 && InBatch.Unclaimed == 0)
		{
			Info.Text = LOCTEXT("DreamUINothingToRebuild", "DreamUI: no text-backed widget Blueprint to rebuild.");
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

	/**
	 * Rebuild every loaded class built from InFilePath, from the text on disk.
	 *
	 * InContentHash is that text's hash (empty when the file could not be read). bInAllowSkip lets a file whose text
	 * is what the last successful rebuild already read, for the same classes, be left alone; an explicit command
	 * passes false, because asking for a rebuild is asking for one.
	 */
	void RecompileFor(FDreamUISourceWatcherState& State, const FString& InFilePath, const FString& InContentHash,
		bool bInAllowSkip, FBatchResult& OutBatch)
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

		UDreamUIDocument* Document = FDreamUIDocumentRegistry::Find(InFilePath);
		const FString FileKey = NormalizeImportKey(InFilePath);

		// An event for text this watcher already built from -- a touch, a save with no edit, a tool rewriting a file
		// with the bytes it had -- would otherwise run a full Blueprint compile per class to arrive where it is.
		// Never when a document is open and disagrees with the disk: that document still has to be brought in.
		if (bInAllowSkip
			&& (Document == nullptr || Document->GetLastWrittenHash() == InContentHash)
			&& FDreamUISourceWatcher::IsUpToDateWithLastCompile(State, InFilePath, InContentHash, Blueprints))
		{
			++OutBatch.Unchanged;
			UE_LOG(DreamGUI, Verbose, TEXT("[%s].%d '%s' was written, but with the text its classes were last built from."),
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
		bool bDiscardedEdits = false;
		if (Document != nullptr)
		{
			// Asked for BEFORE the reload because the reload is what loses it: text the designer holds that the
			// file never received (the write was refused or the file is read-only) is replaced here, and used to
			// be replaced without a word. It is kept aside and the author is told where.
			FString DiscardedDesignerText;
			FString LoadError;
			if (!Document->LoadFromFile(LoadError, &DiscardedDesignerText))
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Could not reload '%s': %s"),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath, *LoadError);
			}
			else
			{
				FString SavedCopy;
				const bool bDiscarded = !DiscardedDesignerText.IsEmpty();
				bDiscardedEdits = bDiscarded;
				if (bDiscarded)
				{
					SavedCopy = FDreamUISourceWatcher::SaveDiscardedDesignerText(InFilePath, DiscardedDesignerText);
					UE_LOG(DreamGUI, Warning,
						TEXT("[%s].%d '%s' changed outside the editor while the designer held edits that had not reached it; they were replaced. A copy is at '%s'."),
						ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath, *SavedCopy);
				}

				// The contract UDreamUIDocument::LoadFromFile writes down, and which nothing in the
				// codebase was honouring. A reload is deliberately NOT transacted, so every entry still
				// on the undo stack describes text from BEFORE it -- and undoing past the reload writes
				// that older text back over the file, which is a Ctrl+Z silently discarding somebody
				// else's edit. The header says the host's move is a barrier; this is the host.
				//
				// Only when there is such an entry. The barrier is for the whole editor, so an author with a level
				// open would lose their undo history for a .dui they were not looking at.
				bool bUndoCut = false;
				if (GEditor != nullptr && GEditor->Trans != nullptr && UndoHistoryTouches(Document, Blueprints))
				{
					GEditor->Trans->SetUndoBarrier();
					bUndoCut = true;
				}
				NotifyExternalReload(InFilePath, bDiscarded, SavedCopy, bUndoCut);
			}
		}

		// For the compiler, which has a line of its own to say it: the toast goes away, the message log stays.
		TGuardValue<bool> DiscardGuard(State.bDesignerEditsDiscarded, bDiscardedEdits);

		bool bAllCompiled = true;
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
				bAllCompiled = false;
				++OutBatch.Failed;
				RecordFirstError(OutBatch, InFilePath, Blueprint, Results);
			}
			else
			{
				++OutBatch.Compiled;
			}
		}

		// Remembered only when every class built, and only for text that could be read: a failure must be
		// attempted again by the next save of the same text, since what it failed on may have changed elsewhere.
		if (bAllCompiled && !InContentHash.IsEmpty())
		{
			FDreamUISourceWatcherState::FCompiledSource& Record = State.CompiledSources.FindOrAdd(FileKey);
			Record.ContentHash = InContentHash;
			Record.Blueprints.Reset();
			for (const UDreamWidgetBlueprint* Blueprint : Blueprints)
			{
				Record.Blueprints.Add(FObjectKey(Blueprint));
			}
			OutBatch.AppliedFiles.Add(FPaths::GetCleanFilename(InFilePath));
		}
		else
		{
			State.CompiledSources.Remove(FileKey);
		}
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

	/**
	 * Replace what the table holds for InImporter with InImports.
	 *
	 * Replace, not append: a file that dropped a `use` line must stop recompiling on that library's saves, and
	 * both publishers hand over the WHOLE current list. The importer's previous edges are found through
	 * ImportRecords rather than by walking every edge in the table, so republishing one file is the size of that
	 * file's own list -- the first sweep over a project used to be the number of files times the number of edges.
	 */
	void StoreImportEdges(FDreamUISourceWatcherState& State, const FString& InImporter, const TArray<FString>& InImports)
	{
		FDreamUISourceWatcherState::FImportRecord& Record = State.ImportRecords.FindOrAdd(NormalizeImportKey(InImporter));
		for (const FString& PreviousKey : Record.ImportKeys)
		{
			State.ImportEdges.Remove(PreviousKey, Record.Importer);
		}
		Record.Importer = InImporter;
		Record.ImportKeys.Reset();
		for (const FString& Import : InImports)
		{
			const FString Key = NormalizeImportKey(Import);
			State.ImportEdges.Add(Key, InImporter);
			Record.ImportKeys.Add(Key);
		}
	}

	/**
	 * Republish one file's `use` edges from its text, which the caller has already read.
	 *
	 * Bookkeeping only: it says nothing about the file having been compiled, so it leaves what is remembered about the
	 * file's last rebuild alone. The compiler's own publication, FDreamUISourceWatcher::NoteImports, is the one that
	 * means "somebody just compiled this".
	 */
	void NoteImportsFromText(FDreamUISourceWatcherState& State, const FString& InFilePath, const FString& InText)
	{
		TArray<FString> Spellings;
		ScanImportSpellings(InText, Spellings);

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
		StoreImportEdges(State, InFilePath, Resolved);
	}

	/** Republish one file's `use` edges from what is on disk right now. */
	void NoteImportsFromDisk(FDreamUISourceWatcherState& State, const FString& InFilePath)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InFilePath))
		{
			return;
		}
		NoteImportsFromText(State, InFilePath, Text);
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
			// build resolved them), and replacing it with a text scan would be a downgrade. Asked of the
			// per-importer records, which have an entry for every published file -- one with no imports
			// included, which the edge table alone could not tell from one never published.
			if (!State.ImportRecords.Contains(NormalizeImportKey(Source)))
			{
				NoteImportsFromDisk(State, Source);
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
		//
		// Each changed file is read once, here: its edges, the hash that tells the designer's own write from
		// somebody else's, and the hash the rebuild compares with what it last built from all come from that read.
		TMap<FString, FString> ContentHashes;
		for (const FString& File : Files)
		{
			FString Text;
			if (FPaths::FileExists(File) && FFileHelper::LoadFileToString(Text, *File))
			{
				NoteImportsFromText(State, File, Text);
				ContentHashes.Add(NormalizeImportKey(File), UDreamUIDocument::ComputeContentHash(Text));
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
			const FString* Hash = ContentHashes.Find(Key);
			if (Hash != nullptr && IsOwnWriteComingBack(File, *Hash))
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
			// A file reached only through what it imports was not read above; its hash is what the rebuild records.
			const FString* KnownHash = ContentHashes.Find(Key);
			const FString Hash = KnownHash != nullptr ? *KnownHash : ReadContentHash(File);
			// The text-unchanged skip applies to a file that was written, not to one pulled in by a dependency
			// (its inputs changed whatever its own text did) and not to an explicit command (asking is asking).
			const bool bAllowSkip = !bAnnounceSuccess && !DependencyAffected.Contains(Key);
			RecompileFor(State, File, Hash, bAllowSkip, Batch);
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
			// waiting out another interval only makes the click feel broken.
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

	/**
	 * Whether a Blueprint compile may start now.
	 *
	 * Not during play: compiling a widget Blueprint reinstances every live instance of it, and doing that to a
	 * running game because a file was saved in another window is not a rebuild, it is a crash report. Not while a
	 * package saves or the collector runs. Nor in the middle of something the compile would pull out from under the
	 * author: a drag in flight holds widgets of the very preview a compile rebuilds, and an open transaction would
	 * take the compile's changes into the edit it is recording. All of these end on their own, so a caller that is
	 * refused waits (the queue) or says to try again (a command).
	 */
	bool CanCompileNow()
	{
		if (GEditor != nullptr && GEditor->PlayWorld != nullptr)
		{
			return false;
		}
		if (UE::IsSavingPackage() || IsGarbageCollecting())
		{
			return false;
		}
		if ((FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping())
			|| (GEditor != nullptr && GEditor->IsTransactionActive()))
		{
			return false;
		}
		return true;
	}

	/**
	 * The entry in GEditor->ShouldDisableCPUThrottlingDelegates.
	 *
	 * An editor that is not the foreground window is held to 3 FPS (UEditorEngine::ShouldThrottleCPUUsage), and the
	 * author is, by definition, in another window when they save a .dui. The directory watcher's callback, the
	 * queue's ticker and the debounce all advance one frame at a time, so a save took a second or two to show up. This
	 * keeps the editor at full rate while something is pending that it could act on, and for a few seconds after
	 * the last .dui event -- the author's next save is likely to follow it.
	 *
	 * Asked once per editor frame, so it reads a few numbers and nothing else. Pending work that is blocked (play, a
	 * drag) does not count: running fast would not unblock it, and a backgrounded editor with PIE open is not
	 * to be kept hot indefinitely.
	 */
	bool ShouldRunAtFullRate()
	{
		const FDreamUISourceWatcherState* State = GThrottleState;
		if (State == nullptr)
		{
			return false;
		}
		if (FPlatformTime::Seconds() - State->LastActivityTime < FullRateHoldSeconds)
		{
			return true;
		}
		const bool bPending = State->PendingFiles.Num() > 0 || State->PendingRemovals.Num() > 0;
		return bPending && CanCompileNow();
	}

	bool Tick(float /*DeltaTime*/)
	{
		FDreamUISourceWatcherState* State = Session();
		if (State == nullptr || (State->PendingFiles.Num() == 0 && State->PendingRemovals.Num() == 0))
		{
			return true;
		}
		// The queue waits when a compile would be unsafe; the next tick asks again.
		if (!CanCompileNow())
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
			// those are the events that would make this noisy.
			//
			// Any .dui event, the designer's own writes included, is the author at work in these files: the editor
			// should not drop to background frame rates between one save and the next.
			State->LastActivityTime = FPlatformTime::Seconds();
			// A file appearing or going changes what a relative Source File spelling resolves to.
			if (Change.Action != FFileChangeData::FCA_Modified)
			{
				ClearLookupCaches();
			}
			// A removal of a real .dui is queued for the report pass -- both a delete and a rename arrive
			// this way, and after either one a class still naming that path is a class whose next compile
			// cannot work.
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

	// The compiler publishes this after every parse of a text-backed class, in any process, so it is also how the
	// resolver hears that a widget Blueprint was compiled -- possibly with another Source File than it had. What it
	// remembered about files nobody was found for may no longer hold.
	ClearLookupCaches();

	FDreamUISourceWatcherState* State = Session();
	if (State == nullptr)
	{
		return;
	}
	StoreImportEdges(*State, InImporter, InImports);

	// And it means somebody compiled this file just now. If that was the watcher's own rebuild it records the result
	// when it returns; anyone else's (the designer, the Compile button, the bridge) built from text the watcher cannot
	// vouch for, so the next save of the same text has to rebuild.
	if (!State->bCompilingFromExternalChange)
	{
		State->CompiledSources.Remove(NormalizeImportKey(InImporter));
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

	// Paired with the removal in Unregister. The delegate list is the editor's, not this session's, so an entry left
	// behind would keep answering for a state that no longer exists.
	GThrottleState = &InState;
	if (GEditor != nullptr && !InState.CpuThrottleHandle.IsValid())
	{
		UEditorEngine::FShouldDisableCPUThrottling Exemption =
			UEditorEngine::FShouldDisableCPUThrottling::CreateStatic(&ShouldRunAtFullRate);
		InState.CpuThrottleHandle = Exemption.GetHandle();
		GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(Exemption));
	}
}

void FDreamUISourceWatcher::Unregister(FDreamUISourceWatcherState& InState)
{
	using namespace DreamUISourceWatcherLocal;

	if (GThrottleState == &InState)
	{
		GThrottleState = nullptr;
	}
	if (GEditor != nullptr && InState.CpuThrottleHandle.IsValid())
	{
		const FDelegateHandle Handle = InState.CpuThrottleHandle;
		GEditor->ShouldDisableCPUThrottlingDelegates.RemoveAll(
			[Handle](const UEditorEngine::FShouldDisableCPUThrottling& Exemption)
			{
				return Exemption.GetHandle() == Handle;
			});
	}
	InState.CpuThrottleHandle.Reset();

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

bool FDreamUISourceWatcher::IsDiscardingDesignerEdits()
{
	const FDreamUISourceWatcherState* State = DreamUISourceWatcherLocal::Session();
	return State != nullptr && State->bDesignerEditsDiscarded;
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
	State->LastActivityTime = State->LastChangeTime;
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
	State->LastActivityTime = State->LastChangeTime;
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

bool FDreamUISourceWatcher::CanCompileNow()
{
	// Qualified: the unqualified name is this function.
	return DreamUISourceWatcherLocal::CanCompileNow();
}

void FDreamUISourceWatcher::NotifyBusy()
{
	using namespace DreamUISourceWatcherLocal;
	if (!CanShowNotifications())
	{
		return;
	}
	FNotificationInfo Info(LOCTEXT("DreamUIBusy",
		"DreamUI: not rebuilding right now -- play, a save, a drag or an open edit is in progress. Try again in a moment."));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = true;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

bool FDreamUISourceWatcher::IsQueued(const FString& InFilePath)
{
	const FDreamUISourceWatcherState* State = DreamUISourceWatcherLocal::Session();
	if (State == nullptr)
	{
		return false;
	}
	// Not DeferredBulkFiles: those wait for the author to release them and will not compile on their own.
	return State->PendingFiles.Contains(FDreamUIDocumentRegistry::NormalizePath(InFilePath));
}

void FDreamUISourceWatcher::NotifyDesignerWriteRefused(const FString& InFilePath)
{
	using namespace DreamUISourceWatcherLocal;
	ShowConflictToast(InFilePath, FText::Format(LOCTEXT("DreamUIDesignerWriteRefused",
		"DreamUI: the designer's change was not written to {0}, because the file was changed outside the editor first. The editor brings the new text in next, and this change does not survive that: make it again afterwards."),
		FText::FromString(FPaths::GetCleanFilename(InFilePath))));
}

FString FDreamUISourceWatcher::SaveDiscardedDesignerText(const FString& InFilePath, const FString& InContent)
{
	// Under Saved/, which no source root covers: a .dui left among the sources would be picked up by the watcher and
	// the workspace as one of the author's own files. Stamped to the millisecond, so two losses in a row do not
	// write over each other's only copy.
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUI"), TEXT("Discarded")));
	FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.%s.dui"),
		*FPaths::GetBaseFilename(InFilePath), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-%s"))));
	FPaths::NormalizeFilename(Path);

	// UTF-8 without a BOM, as the document writes the file itself.
	if (!FFileHelper::SaveStringToFile(InContent, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Could not save the designer's replaced text of '%s' to '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath, *Path);
		return FString();
	}
	return Path;
}

bool FDreamUISourceWatcher::IsUpToDateWithLastCompile(const FDreamUISourceWatcherState& InState,
	const FString& InFilePath, const FString& InContentHash, const TArray<UDreamWidgetBlueprint*>& InBlueprints)
{
	if (InContentHash.IsEmpty() || InBlueprints.IsEmpty())
	{
		return false;
	}
	const FDreamUISourceWatcherState::FCompiledSource* Record =
		InState.CompiledSources.Find(DreamUISourceWatcherLocal::NormalizeImportKey(InFilePath));
	if (Record == nullptr || Record->ContentHash != InContentHash)
	{
		return false;
	}
	// Every class built from the file now must be one that was built from this text: a Blueprint loaded since holds
	// whatever it was last saved with, and an equal hash says nothing about it.
	for (const UDreamWidgetBlueprint* Blueprint : InBlueprints)
	{
		if (!Record->Blueprints.Contains(FObjectKey(Blueprint)))
		{
			return false;
		}
	}
	return true;
}

void FDreamUISourceWatcher::ForgetUnresolvedSources()
{
	DreamUISourceWatcherLocal::ClearLookupCaches();
}

int32 FDreamUISourceWatcher::NumUnresolvedSources()
{
	return DreamUISourceWatcherLocal::SourceClassIndex().UnresolvedSources.Num();
}

void FDreamUISourceWatcher::FindBlueprintsForSource(const FString& InAbsoluteFilePath,
	TArray<UDreamWidgetBlueprint*>& OutBlueprints)
{
	DreamUISourceWatcherLocal::FindBlueprints(InAbsoluteFilePath, OutBlueprints);
}

int32 FDreamUISourceWatcher::RebuildAll()
{
	using namespace DreamUISourceWatcherLocal;

	// Asked for, so it is not subject to the debounce or to the content check -- but it compiles every text-backed
	// widget Blueprint, which is exactly what must not happen under a running game, a save or an open edit.
	if (!CanCompileNow())
	{
		NotifyBusy();
		return 0;
	}

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
			// The first ERROR, as the watcher's own rebuilds report it -- not whatever message happened to be
			// first, which for a compile that begins with a warning names the warning as the reason it failed.
			RecordFirstError(Batch, UDreamTextUserWidget::ResolveDuiFilePath(
				DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint)), Blueprint, Results);
		}
		else
		{
			++Batch.Compiled;
		}
	}

	// The one summary of the whole sweep. A caller must not add a second.
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
	// Any Blueprint compile can give a Blueprint another Source File, so any compile reopens the question for the
	// sources nobody was found for. The editor engine is not there yet when this module starts, so the hook waits for
	// it; a module loaded later finds it already running.
	auto HookBlueprintCompiled = []
	{
		FSourceClassIndex& HookIndex = SourceClassIndex();
		if (GEditor != nullptr && !HookIndex.BlueprintCompiledHandle.IsValid())
		{
			HookIndex.BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([]
			{
				ClearLookupCaches();
			});
		}
	};
	if (GEditor != nullptr)
	{
		HookBlueprintCompiled();
	}
	else
	{
		Index.PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda(HookBlueprintCompiled);
	}

	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		Index.AssetAddedHandle = AssetRegistry->OnAssetAdded().AddLambda([](const FAssetData&)
		{
			// A new widget Blueprint may be the owner of a source that had none.
			ClearLookupCaches();
		});
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
	if (Index.PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(Index.PostEngineInitHandle);
	}
	// The editor engine is gone by the time its modules shut down on exit, and its delegates with it.
	if (GEditor != nullptr && Index.BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(Index.BlueprintCompiledHandle);
	}
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetAdded().Remove(Index.AssetAddedHandle);
		AssetRegistry->OnAssetUpdatedOnDisk().Remove(Index.AssetUpdatedOnDiskHandle);
		AssetRegistry->OnAssetRemoved().Remove(Index.AssetRemovedHandle);
		AssetRegistry->OnAssetRenamed().Remove(Index.AssetRenamedHandle);
	}
	Index = FSourceClassIndex();
}

#undef LOCTEXT_NAMESPACE
