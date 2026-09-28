// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * What the renderer takes from the project's settings. The settings object is the core's, a module above
 * the renderer, so the renderer does not read it: the core registers a provider that does, and the renderer
 * asks the provider whenever it sets up a view -- the same moment it used to read the settings itself, so an
 * edit to them takes effect on the next frame, as before.
 */
struct FDreamUIRendererSettings
{
	/** Samples per pixel the UI is drawn with, as the project asks for it; 1 is no MSAA. */
	uint8 MSAASampleCount = 1;
	bool bFrustumCulling = true;
	/** FDreamUIRenderer's priority among the scene view extensions. */
	int32 ViewExtensionPriority = 0;
};

namespace DreamUIRendererSettings
{
	/** Where the renderer reads its settings from. The core sets it on startup and clears it on shutdown. */
	DREAMGUI_API void SetProvider(TFunction<FDreamUIRendererSettings()> InProvider);
	/** Whether a provider is set; without one, Get answers the defaults above. */
	DREAMGUI_API bool HasProvider();
	/** The settings as the provider answers them now. */
	DREAMGUI_API FDreamUIRendererSettings Get();
}
