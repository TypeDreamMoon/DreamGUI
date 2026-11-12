// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// DreamGUI's DreamMCP actions: check, compile, diagnostics, lookup, layout, preview, info.
//
// Each one forwards to what the plugin already does -- the parser and the tree builder, the widget Blueprint compiler,
// the diagnostics mailbox, the symbol export the VS Code extension reads, the designer's way of instancing a screen --
// and turns the answer into the result body. `layout` and `preview` put the screen on a stage of their own
// (DreamGUIMCPStage.h); what they say about it comes from DreamGUIMCPLayout.h and DreamGUIMCPPreviewStats.h.

#pragma once

#include "CoreMinimal.h"

namespace DreamGUIMCP
{
	class FEndpoint;

	/** Registers every action the manifest names. */
	DREAMGUIEDITOR_API void RegisterDreamGUIMCPActions(FEndpoint& Endpoint);

	/** Forgets what the actions remember between calls: the last check of each file, the compile hashes, the stage. */
	DREAMGUIEDITOR_API void ResetDreamGUIMCPActionState();
}
