// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The packaged text smoke probe: in a game started with -DreamGUITextSmoke=<directory> -- a packaged build, Development or
 * Shipping, or the editor binary with -game for the reference run -- it puts /Game/DreamGUISmoke/WBP_TextSmoke on the
 * screen, waits until every glyph has landed and small text has settled, writes TextSmoke.json (the texts' display lists,
 * the fonts' answers, the safe zone, the memory report) and TextSmoke.png into the directory, and asks the game to exit.
 * Tools/Tests/compare_text_smoke.py holds a packaged run's files to the reference run's. Tools/TestHost/README.md has the
 * commands.
 */
namespace DreamGUIPackagedSmoke
{
	/** Start the probe when the command line asks for it, in a game: never in an editor session or a commandlet. */
	void StartIfAsked();
	/** Take the probe down, if it is up. */
	void Stop();
}
