// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The editor half of the drop-folder bridge: `Saved/DreamGUI/Bridge/`, requests in, responses
 * out, a heartbeat in between. The protocol contract lives on the CLIENT side (the VSCode
 * extension's bridgeProtocol tests are the spec); this service conforms to it, and
 * PROTOCOL_VERSION moves when either half does.
 *
 * The shape is the DreamFX bridge's, which already paid for its choices:
 *   - POLLING, not DirectoryWatcher -- a watcher owes nothing for files that changed while the
 *     editor was closed, which for source files is fine and for RPC means requests vanish;
 *   - take-then-delete before executing, so a crash mid-action cannot replay the request into
 *     the next session;
 *   - responses written beside and renamed, because the client polls-for-existence then reads;
 *   - unknown actions MUST be answered -- a silent editor and a missing feature look identical
 *     from the other end;
 *   - one project, one editor: two editors would steal each other's requests, recorded and not
 *     defended against.
 *
 * Actions: ping / functions (what `<-`, `->` and a binding expression can name on a class) /
 * variables (what `<->` and a binding expression can read) / members (what a `.` reaches inside a
 * struct or class, including through a TArray) / assets / reveal (open the designer, select a
 * widget) / revealAsset (find an asset in the content browser and open it) / compile. Compile's
 * verdicts travel through the diagnostics mailbox, not the response -- the bridge does not
 * re-ship what already has a channel.
 *
 * One direction is NOT request/response: the editor also pushes, through
 * `Bridge/reveal-to-editor.json`, when someone right-clicks a widget in the designer and asks for
 * the line that authored it. A drop file rather than a reply because there is no request to reply
 * to -- VSCode is not asking, it is being told -- and the client polls that one file the way it
 * polls status.json. Written atomically for the same reason responses are: the reader is another
 * process, and a half-written file it happens to catch reads as a corrupt reveal rather than as
 * "not yet".
 */
struct DREAMGUIEDITOR_API FDreamUIBridgeService
{
	/**
	 * Whether THIS process should be the editor external tools talk to.
	 *
	 * Not a commandlet, not an automation run, not an unattended process: those are started beside an editor that
	 * is open (a headless test pass, a build step), and registering would clear the open editor's Requests/ and
	 * Responses/ and overwrite its status.json -- and unregistering at exit would delete it, so VS Code would
	 * report the editor closed while it was running. The same guard the diagnostics mailbox has for the same reason.
	 */
	static bool ShouldOwnBridge();
	static bool ShouldOwnBridge(bool bInCommandlet, bool bInAutomationTesting, bool bInUnattended);

	/**
	 * Module startup: ensures the folders, starts the poll ticker and the heartbeat. Unconditional -- whether this
	 * process should be the bridge is ShouldOwnBridge's question, asked by the module, so that a test can still
	 * register one explicitly.
	 */
	static void Register();

	/**
	 * Module shutdown: stops the ticker and removes status.json -- absence means "closed". Does nothing to
	 * status.json when this process never registered: it belongs to whichever editor did.
	 */
	static void Unregister();

	/** True between Register and Unregister. */
	static bool IsRegistered();

	/**
	 * Whether something on the other side has talked to this bridge recently enough to be listening for a reveal.
	 *
	 * The protocol has no client heartbeat, so "a request was answered in the last ten minutes" is the closest thing
	 * to knowing that VS Code is there. Wrong in one direction only, on purpose: a client that has been quiet longer
	 * is treated as absent, and the caller opens the file itself.
	 */
	static bool HasRecentClient();
	static bool IsClientRecent(double InLastRequestSeconds, double InNowSeconds);

	/**
	 * Take the author to a place in a file: through the bridge, so VS Code puts its cursor there, when a client is
	 * listening, and by opening the file in whatever the system uses for it when none is. The position is 1-based;
	 * the fallback cannot honour it.
	 */
	static void OpenAtLocation(const FString& InAbsoluteFilePath, int32 InLine, int32 InColumn);

	/**
	 * Drains the requests directory once, synchronously. The ticker calls this; tests call it
	 * directly with an override root so they never touch the live bridge. Returns how many
	 * requests were processed.
	 */
	static int32 ProcessPendingNow(const FString& InOverrideRoot = FString());

	/**
	 * Asks the VSCode side to put its cursor at InLine/InColumn of InAbsoluteFilePath -- the
	 * designer's half of "reveal", pointing the other way.
	 *
	 * The reverse of the `reveal` ACTION, and deliberately not shaped like it: that one is an
	 * answer the client waited for, this one is an announcement nobody asked for. So it is one
	 * file, overwritten, never queued -- the newest reveal is the only one anybody wants, and a
	 * backlog of stale cursor jumps is worse than none.
	 *
	 * InWidgetId rides along even when the position was not found (line and column then being
	 * 1/1): the client can still say which node it failed to locate, and a message naming the
	 * node beats a silent jump to the top of the file. Returns false when the file could not be
	 * written; the caller reports that, because a menu command with no reaction reads as broken.
	 */
	static bool WriteRevealToEditor(const FString& InAbsoluteFilePath, int32 InLine, int32 InColumn,
		const FString& InWidgetId, const FString& InOverrideRoot = FString());
};
