// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// Which DreamMCP endpoint this process runs, if any, and its lifetime.
//
//   * A host -- any process with -DreamMCPHost -- runs a `host` endpoint. A server starts one of two: the windowless
//     editor (`-DreamMCPHost -RenderOffscreen`, no -run=) when it needs a renderer, a host commandlet
//     (`-run=DreamGUIHost -nullrhi -DreamMCPHost`, or another Dream plugin's) when it does not. Every Dream plugin in
//     the process honours the switch, so each serves its own route from whichever host the server started. A
//     commandlet named Dream...Host counts as a host without the switch too.
//   * An editor -- not a commandlet, an editor session (GIsEditor), not an automation run, the project setting on, no
//     -NoDreamMCP -- runs an `editor` endpoint.
//   * Every other process -- `-run=DreamUICompileWidgets`, a cook, an automation run, a game -- runs none.
//
// The endpoint starts after the engine has initialized (compiling a widget Blueprint needs the editor's subsystems)
// and stops with the module, deleting its discovery file. A host also accepts POST /shutdown, exits when the process
// that started it goes away, exits with code 3 when it cannot start answering within -DreamMCPStartupTimeout seconds,
// and -- as a windowless editor -- writes no config, does not auto-save, renders no realtime viewport and caps its
// frame rate while idle.

#pragma once

#include "CoreMinimal.h"

namespace DreamGUIMCP
{
	class FEndpoint;

	/** `-DreamMCPHost` is on the command line. */
	DREAMGUIEDITOR_API bool HasDreamMCPHostSwitch();

	/** A process with -DreamMCPHost, or a commandlet run as `-run=Dream<...>Host` (or `...HostCommandlet`). */
	DREAMGUIEDITOR_API bool IsDreamMCPHostProcess();

	/**
	 * Whether the project lets the user's editor serve DreamMCP: the setting UDreamGUISettings::bEnableDreamMCPEndpoint,
	 * the one place its default is decided. A host is not asked -- a server could not start one otherwise.
	 */
	DREAMGUIEDITOR_API bool IsDreamGUIMCPEndpointEnabled();

	/** From the editor module's StartupModule. bEditorSession: the module started an editor session (GIsEditor). */
	DREAMGUIEDITOR_API void StartupDreamGUIMCP(bool bEditorSession);

	/** From the editor module's ShutdownModule, and from the host commandlet before it returns. */
	DREAMGUIEDITOR_API void ShutdownDreamGUIMCP();

	/** The running endpoint, or null. */
	DREAMGUIEDITOR_API FEndpoint* GetDreamGUIMCPEndpoint();

	/** Why there is no endpoint, when one was expected; empty otherwise. */
	DREAMGUIEDITOR_API const FString& GetDreamGUIMCPStartupError();
}
