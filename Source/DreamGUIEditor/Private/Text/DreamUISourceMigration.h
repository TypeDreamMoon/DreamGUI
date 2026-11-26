// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The deprecation notice for a project still keeping its .dui sources in `DUI/`, and the button that moves them to
 * `DreamSource/UI/` (or wherever UDreamUISourceSettings points).
 *
 * `DUI/` keeps working for good -- this only tells the author it is deprecated and that Dream sources are managed
 * together under DreamSource/. Logged once per session in every process; the notification only in the editor the
 * author works in, and not again once they said so (EditorPerProjectUserSettings, this machine only).
 * Plan/DreamSource/dreamsource-layout-spec.md section 11.5.
 */
struct FDreamUISourceMigration
{
	static void Register();
	static void Unregister();

	/**
	 * Moves the legacy `DUI/` to the configured directory, drops the json files ui.db replaced, and points the watcher
	 * and the generated data at the new place. Refuses when the configured directory already exists. False with
	 * OutError on failure; whatever could not be moved is left where it was.
	 */
	static bool Migrate(FString& OutError);
};
