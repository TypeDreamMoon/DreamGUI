// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UWorld;

namespace DreamTests
{
	/**
	 * A bare world that lives exactly as long as the test does.
	 *
	 * Almost every test needs an outer that behaves like a world -- subsystems, a transient package
	 * to own widgets, somewhere for actors to spawn -- and needs it torn down before the next test
	 * runs, because a leaked world keeps its subsystems and its widget tree alive and the next test
	 * finds them. CreateWorld/DestroyWorld is the cheapest pair that gives that; nothing here loads
	 * a map or begins play.
	 *
	 * Game is the default because that is the world type the runtime actually ships into. Editor is
	 * for the handful of tests that deliberately want the editor's answers -- UDreamCanvas derives
	 * its viewport size from the widget itself in an editor world, rather than from a player
	 * controller that a headless test has not got.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;

		explicit FScopedGameWorld(EWorldType::Type InType = EWorldType::Game);
		~FScopedGameWorld();
	};
}
