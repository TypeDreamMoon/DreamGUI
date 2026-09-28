// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/** What the renderer logs to. Its own category: the core's is declared in a module above the renderer. */
DREAMGUI_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamGUIRenderer, Log, All);

/**
 * The plugin's stat group, `stat DreamGUI`. Declared here, in the lowest module that counts into it, so the
 * renderer and every module above it name the same group: a stat group cannot be declared twice.
 */
DECLARE_STATS_GROUP(TEXT("DreamGUI"), STATGROUP_DreamGUI, STATCAT_Advanced);
