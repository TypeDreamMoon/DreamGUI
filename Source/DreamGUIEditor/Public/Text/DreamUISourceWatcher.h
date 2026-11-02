// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectKey.h"

class UDreamWidgetBlueprint;

/**
 * What the source watcher keeps between a change on disk and the compile it causes, for one editor
 * session. UDreamGUIEditorSubsystem holds it, and registers the watcher with it when the session starts
 * and unregisters it when the session ends; everything else reaches it through the subsystem, and does
 * nothing outside a session -- a commandlet compiling a text-authored class has no watcher to tell.
 */
struct FDreamUISourceWatcherState
{
	/** The directories being watched, with the directory watcher's handle for each. */
	TMap<FString, FDelegateHandle> WatchHandles;
	/** The ticker that drains the queue. */
	FTSTicker::FDelegateHandle TickerHandle;
	/** Changed files, waiting out the debounce. */
	TSet<FString> PendingFiles;
	/**
	 * Files the watcher saw GO AWAY, which used to be dropped where the event arrived.
	 *
	 * A deletion or a rename is the one change to a .dui that leaves its class with no way to notice:
	 * there is nothing to recompile from, so the class keeps the tree it last built and stays wrong
	 * until somebody presses Compile by hand and finally meets DUI6001 -- by which time the file has
	 * been gone long enough that nothing connects the two. Kept apart from PendingFiles because the
	 * answer is a report, not a rebuild.
	 */
	TSet<FString> PendingRemovals;
	/** When the last change arrived, for the debounce. */
	double LastChangeTime = 0.0;
	/** A batch too large to compile unasked. Held rather than dropped -- see OfferDeferredBatch. */
	TSet<FString> DeferredBulkFiles;
	/** Set by an explicit command; a plain save leaves it false and stays quiet when it worked. */
	bool bAnnounceSuccess = false;
	/**
	 * True for the span of a rebuild caused by a change on DISK rather than by the designer.
	 *
	 * Read by the compiler through FDreamUISourceWatcher::IsCompilingFromExternalChange. A flag
	 * rather than a parameter because the question is asked five frames down a call chain that runs
	 * through FKismetEditorUtilities and the compilation manager, neither of which has anywhere to
	 * carry it.
	 */
	bool bCompilingFromExternalChange = false;
	/**
	 * True for the span of a rebuild whose reload replaced designer text that had never reached the file.
	 * Read by the compiler through FDreamUISourceWatcher::IsDiscardingDesignerEdits, so the loss is recorded in the
	 * compile's message log as well as announced in a toast.
	 */
	bool bDesignerEditsDiscarded = false;
	/** import (normalized, lowercased) -> importer (same spelling RecompileFor expects). */
	TMultiMap<FString, FString> ImportEdges;
	/** False until the on-disk sweep of `use` lines has run once this session. */
	bool bImportIndexSeeded = false;

	/**
	 * What one importer's edges in ImportEdges are, so republishing them removes exactly those and a
	 * lookup of "is this file's list known" does not walk the whole table.
	 */
	struct FImportRecord
	{
		/** The spelling the importer was stored under in ImportEdges. */
		FString Importer;
		/** The import keys (normalized, lowercased) it holds an edge for. */
		TArray<FString> ImportKeys;
	};
	/** importer (normalized, lowercased) -> its edges. */
	TMap<FString, FImportRecord> ImportRecords;

	/** What a file's text was when the watcher last rebuilt the classes built from it, and which classes those were. */
	struct FCompiledSource
	{
		FString ContentHash;
		TSet<FObjectKey> Blueprints;
	};
	/** file (normalized, lowercased) -> the last rebuild that succeeded. Dropped when anything else compiles the file. */
	TMap<FString, FCompiledSource> CompiledSources;

	/** When a .dui event arrived last, for keeping the editor out of background throttling. */
	double LastActivityTime = -1000.0;
	/** The entry this session added to GEditor->ShouldDisableCPUThrottlingDelegates. */
	FDelegateHandle CpuThrottleHandle;
};

/**
 * Save a `.dui`, and the classes built from it recompile.
 *
 * This closes the loop the whole text direction assumes. Without it "the text is the only truth" is
 * true of the compiler and false of the editor: a file edited in VSCode changed nothing until
 * somebody remembered to press Compile, and the designer went on drawing the old hierarchy with no
 * indication that it was stale. Every other part of the pipeline was built expecting this to exist
 * -- UDreamUIDocument::IsOwnWrite has been sitting there since the write-back landed, with a comment
 * saying wiring the watcher is the host's job.
 *
 * ## What it rebuilds, and what it does not
 *
 * LOADED Blueprints only, and asked of each Blueprint rather than read off the file.
 *
 * A `.dui` CAN name its class -- `class /Game/UI/WBP_Login` on the first line -- but that line is
 * not what it looks like. It is optional, nothing validates it against the Blueprint being compiled,
 * and its only current use is as a stable localization namespace (DreamUITextBuilder.cpp). Trusting
 * it here would mean trusting a string no compile has ever checked, and being wrong about it means
 * rebuilding the wrong class or silently rebuilding none.
 *
 * So the mapping runs the direction that IS load-bearing -- the class names the file -- which means
 * finding the classes for a file is asking each of them, and asking every widget Blueprint in the
 * project means loading every widget Blueprint in the project. Not a keystroke's worth of work.
 *
 * Validating `class` would change this: it would make the file->class direction real, let a save
 * reach an unloaded class, and catch two classes claiming one file. It is the obvious next step and
 * this deliberately does not take it on a line nothing enforces yet.
 *
 * It is also not the loop this exists to shorten. The loop is: designer open, edit the file, look.
 * The Blueprint in that loop is loaded by definition.
 *
 * The cost is a real hole and it is worth naming rather than hiding: a `.dui` edited while its class
 * is not loaded leaves that class stale, and it stays stale, because loading a Blueprint does not
 * recompile it. RebuildAll() is the answer on demand; a content hash on the asset that made the
 * staleness detectable is the answer this does not implement yet.
 *
 * ## Not our own write coming back
 *
 * The designer writes these files too. Reacting to that write means reload -> regenerate -> write ->
 * reload, forever, at whatever rate the file system reports. The suppression is a content hash the
 * document already keeps (UDreamUIDocument::IsOwnWrite) rather than a flag set around the write: a
 * flag has to be right about timing, and the file system's timing is not ours to know.
 */
class DREAMGUIEDITOR_API FDreamUISourceWatcher
{
public:
	/**
	 * Starts watching every DUI root, into InState. Safe to call with no roots -- the queue is used by
	 * menus too. UDreamGUIEditorSubsystem calls it as the editor session starts, and Unregister as it ends.
	 */
	static void Register(FDreamUISourceWatcherState& InState);
	static void Unregister(FDreamUISourceWatcherState& InState);

	/**
	 * Start watching InDirectory as a DUI root, if it is one and nothing is watching it yet.
	 *
	 * Register() runs once, as the editor session starts, over the roots that EXIST at that moment -- and a
	 * project's `DUI/` directory is routinely created later, by Open Workspace, on a project that
	 * never had one. Until something called this, every file in that brand new directory saved into
	 * silence for the rest of the session and the author's first experience of the language was the
	 * one thing it exists to prevent: an edit that changed nothing and said nothing.
	 *
	 * Idempotent. Registering a directory twice would deliver every change to the queue twice.
	 */
	static void EnsureWatching(const FString& InDirectory);

	/**
	 * True while a Blueprint compile is running because the FILE changed underneath it.
	 *
	 * The compiler asks, and the answer decides who wins a conflict. Its ordinary path flushes the
	 * designer's pending template edits into the .dui before reading it, which is right when the
	 * compile was caused by the designer -- and exactly wrong here: the text on disk is the newer
	 * one, and flushing would patch the preview's older values back over somebody else's edit and
	 * save the result. On this path the file wins, so nothing is flushed.
	 */
	static bool IsCompilingFromExternalChange();

	/**
	 * True while a compile caused by a change on disk follows a reload that replaced designer edits the file never
	 * received. The watcher has already told the author and kept a copy; the compiler adds the line to its log.
	 */
	static bool IsDiscardingDesignerEdits();

	/**
	 * Record which files InImporter pulls in through `use`, replacing its previous edges. The
	 * compiler publishes this after every parse, which is what lets a saved style library recompile
	 * the classes that wear it: the drain expands a changed file to its transitive importers.
	 */
	static void NoteImports(const FString& InImporter, const TArray<FString>& InImports);

	/**
	 * Queues one file as if it had just been saved.
	 *
	 * @param bAnnounceSuccess  toast on success as well as failure. A save stays quiet when it worked
	 *                          -- the designer redrawing IS the feedback -- but a menu command that
	 *                          produced no visible reaction reads as a broken menu.
	 */
	static void QueueFile(const FString& InFilePath, bool bAnnounceSuccess = false);

	/**
	 * Queues one file as if it had just been deleted or renamed away.
	 *
	 * Nothing is recompiled -- there is nothing left to compile from, which is the entire problem.
	 * What the drain does instead is SAY SO: every loaded class still naming that path now holds the
	 * last hierarchy it built and will fail its next compile with DUI6001, and without this the
	 * author meets that failure much later, with nothing connecting it to the file they moved.
	 * Removal events used to be dropped where the directory watcher reported them.
	 */
	static void QueueRemoval(const FString& InFilePath);

	/**
	 * Recompiles every text-backed widget Blueprint in the project, loading the ones that are not.
	 *
	 * The deliberate opposite of the watcher's own scope, and the reason it can afford to be: this is
	 * asked for explicitly, so it may take as long as it takes. Returns how many Blueprints it found.
	 */
	static int32 RebuildAll();

	/** Rebuilds everything queued so far, ignoring the debounce. */
	static void FlushPending();

	/**
	 * Whether a Blueprint compile may start now: not during play, not while a package is saving or the
	 * collector runs, and not in the middle of a drag or an open transaction. The queue waits on exactly this,
	 * and every other path that compiles unasked (the bridge's `compile`, the Rebuild menu) asks the same
	 * question so the answers cannot drift apart.
	 */
	static bool CanCompileNow();

	/** Tell the author a command was not carried out because CanCompileNow said no, and that trying again shortly will do. */
	static void NotifyBusy();

	/** True when InFilePath is waiting in the queue and will be compiled by the next drain. A file parked in a bulk batch is not. */
	static bool IsQueued(const FString& InFilePath);

	/**
	 * Tell the author that the designer's edit of InFilePath was not written because the file changed on disk first.
	 *
	 * One toast per file while it is on screen: a flush is attempted on every gesture, and every one of them is
	 * refused for as long as the outside edit has not been brought in.
	 */
	static void NotifyDesignerWriteRefused(const FString& InFilePath);

	/**
	 * The text a designer keeps for a file that has not reached the disk, when an outside change is about to replace it.
	 * Returns the path of the copy under Saved/, or empty when it could not be written. Exposed for tests.
	 */
	static FString SaveDiscardedDesignerText(const FString& InFilePath, const FString& InContent);

	/**
	 * Whether the file whose text hashes to InContentHash needs no rebuild because the last rebuild the watcher
	 * did for it read the same text and compiled exactly InBlueprints. A pure lookup in InState. Exposed for tests.
	 */
	static bool IsUpToDateWithLastCompile(const FDreamUISourceWatcherState& InState, const FString& InFilePath,
		const FString& InContentHash, const TArray<UDreamWidgetBlueprint*>& InBlueprints);

	/**
	 * Drop what is remembered about the classes whose source could not be found, so the next question about any
	 * file looks again. Called when something that could change the answer happened; exposed for tests.
	 */
	static void ForgetUnresolvedSources();

	/** How many sources FindClassForSource currently remembers as having no class. Exposed for tests. */
	static int32 NumUnresolvedSources();

	/** Loaded widget Blueprints whose Source File resolves to this path. Exposed for tests. */
	static void FindBlueprintsForSource(const FString& InAbsoluteFilePath,
		TArray<UDreamWidgetBlueprint*>& OutBlueprints);

	/**
	 * The class of the widget Blueprint whose Source File is InResolvedSourcePath, or null -- what `use "Row.dui" as
	 * Row` means when Row.dui has no `class` line. FDreamUITextBuilder::SourceClassResolver, installed.
	 *
	 * The opposite direction from everything else here, and unlike the save path this one may LOAD: an alias names a
	 * component the host needs now, and the Blueprint behind it is as likely as not one nobody has opened this
	 * session. So the answer is looked for in what is cheapest first -- the Blueprint that answered last time, checked
	 * again; then every loaded one, which is what FindBlueprintsForSource compares, by the same normalized resolved
	 * path; and only then the widget Blueprints the asset registry lists and this session has not read yet, likeliest
	 * first (an asset named like the file), each loaded once and remembered, so the second question about any file
	 * costs nothing. Removed, renamed, saved or updated assets are forgotten; if a selected class shared its file,
	 * its other examined owners can be discovered again. Loaded observations keep the same smallest asset path.
	 *
	 * It never compiles anything: it is asked from inside the builder, which runs inside some other Blueprint's
	 * compile, and a compile started from there would be a compile inside a compile. A class is returned as it
	 * stands. And it does not answer itself: a load it causes that comes back to ask again gets null rather than a
	 * second scan.
	 */
	static UClass* FindClassForSource(const FString& InResolvedSourcePath);

	/**
	 * Install FindClassForSource as FDreamUITextBuilder::SourceClassResolver, and take it out again. The editor module
	 * does both, at startup and shutdown, in every process that compiles -- an uncooked game included, where a host's
	 * `use … as` must resolve as surely as in the editor.
	 */
	static void InstallSourceClassResolver();
	static void UninstallSourceClassResolver();
};
