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
 * LOADED Blueprints only. A `.dui` names its class only in one direction -- the class points at the
 * file -- so finding the classes for a file means asking each of them, and asking every widget
 * Blueprint in the project means loading every widget Blueprint in the project. That is not a thing
 * to do on a keystroke.
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
