// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UGameInstance;
class UWorld;

namespace DreamTests
{
	/**
	 * A Game world that belongs to a UGameInstance, built for one test and torn down after it.
	 *
	 * FScopedGameWorld's bare UWorld::CreateWorld world has no GameInstance, so no GameInstance
	 * subsystem exists in it -- and the tween manager is one. Every Selectable transition, every
	 * tween a behaviour starts, is a no-op there. That is exactly the designer's preview world and a
	 * fine world for layout tests, but it is not the world a game runs controls in; this is.
	 *
	 * Built the way UGameInstance::InitializeStandalone builds a standalone game instance (a world
	 * context of its own, a Game world set as that context's world, the game instance's Init, which
	 * is what creates its subsystems) -- see the definition for the order and why. Not loaded from a
	 * map and not begun: beginning play is the rig's business, and a map would bring a GameMode and
	 * everything it spawns.
	 *
	 * TEAR-DOWN IS THE POINT. The game instance registers a world context with GEngine; a test that
	 * left it there would leave a Game world findable by every GEngine->GetWorldContexts() walk in
	 * every test after it, with its subsystems ticking wherever anything ticks worlds. The destructor
	 * shuts the game instance down, destroys the world, and removes the context -- HasWorldContextFor
	 * is how a test checks.
	 */
	struct FScopedGameInstanceWorld
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;

		FScopedGameInstanceWorld();
		~FScopedGameInstanceWorld();

		FScopedGameInstanceWorld(const FScopedGameInstanceWorld&) = delete;
		FScopedGameInstanceWorld& operator=(const FScopedGameInstanceWorld&) = delete;

		/** Whether GEngine still has a world context whose world is InWorld. For asserting a tear-down left nothing behind. */
		static bool HasWorldContextFor(const UWorld* InWorld);

		/** Whether GEngine still has a world context owned by InGameInstance. */
		static bool HasWorldContextOwnedBy(const UGameInstance* InGameInstance);
	};
}
