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
 * Actions: ping / functions (what `<-` and `->` can name on a class) / assets / reveal (open the
 * designer, select a widget) / compile. Compile's verdicts travel through the diagnostics
 * mailbox, not the response -- the bridge does not re-ship what already has a channel.
 */
struct FDreamUIBridgeService
{
	/** Module startup: ensures the folders, starts the poll ticker and the heartbeat. */
	static void Register();

	/** Module shutdown: stops the ticker and removes status.json -- absence means "closed". */
	static void Unregister();

	/**
	 * Drains the requests directory once, synchronously. The ticker calls this; tests call it
	 * directly with an override root so they never touch the live bridge. Returns how many
	 * requests were processed.
	 */
	static int32 ProcessPendingNow(const FString& InOverrideRoot = FString());
};
