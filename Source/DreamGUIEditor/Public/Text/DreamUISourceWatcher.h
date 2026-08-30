// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidgetBlueprint;

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
	/** Starts watching every DUI root. Safe to call with no roots -- the queue is used by menus too. */
	static void Register();
	static void Unregister();

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
	 * Recompiles every text-backed widget Blueprint in the project, loading the ones that are not.
	 *
	 * The deliberate opposite of the watcher's own scope, and the reason it can afford to be: this is
	 * asked for explicitly, so it may take as long as it takes. Returns how many Blueprints it found.
	 */
	static int32 RebuildAll();

	/** Rebuilds everything queued so far, ignoring the debounce. */
	static void FlushPending();

	/** Loaded widget Blueprints whose Source File resolves to this path. Exposed for tests. */
	static void FindBlueprintsForSource(const FString& InAbsoluteFilePath,
		TArray<UDreamWidgetBlueprint*>& OutBlueprints);
};
