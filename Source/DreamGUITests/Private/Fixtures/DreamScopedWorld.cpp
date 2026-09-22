// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamScopedWorld.h"

#include "Engine/World.h"

namespace DreamTests
{
	FScopedGameWorld::FScopedGameWorld(EWorldType::Type InType)
	{
		// bInformEngineOfWorld stays false: registering the world with the engine would put it in
		// GEngine's world list, where a test that forgets to tear down would be visible to everything
		// else, and it is not needed for subsystems to be created.
		World = UWorld::CreateWorld(InType, false);
	}

	FScopedGameWorld::~FScopedGameWorld()
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
	}
}
