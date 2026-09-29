// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamScopedGameInstanceWorld.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace DreamTests
{
	FScopedGameInstanceWorld::FScopedGameInstanceWorld()
	{
		if (GEngine == nullptr)
		{
			return;
		}
		// Outered to the engine because UGameInstance::GetEngine is a CastChecked of the outer, and
		// InitializeStandalone asks it for the world context. The plain class rather than the project's
		// GameInstanceClass: a project's own game instance is free to load maps, open menus or start
		// online sessions in Init, and a UI test wants the subsystems and nothing else.
		GameInstance = NewObject<UGameInstance>(GEngine, UGameInstance::StaticClass(), NAME_None, RF_Transient);
		// Rooted for as long as the test holds it, the way the editor roots a PIE game instance. The
		// world context also references it once it exists, but the root makes "nothing collects it
		// mid-test" a fact of this fixture rather than of the engine's bookkeeping.
		GameInstance->AddToRoot();

		// The engine's own standalone start-up, not a copy of it: a world context of type Game owned
		// by this game instance, a Game world made with UWorld::CreateWorld and set as that context's
		// world (so GetWorld()->GetGameInstance() and GameInstance->GetWorld() answer each other), and
		// Init, whose last act is SubsystemCollection.Initialize -- which is where UDreamTweenManager
		// comes to exist. Standalone games start exactly like this before their first LoadMap.
		GameInstance->InitializeStandalone();
		World = GameInstance->GetWorld();
	}

	FScopedGameInstanceWorld::~FScopedGameInstanceWorld()
	{
		UWorld* OwnedWorld = World;
		// In the order the editor ends a PIE session (UEditorEngine::TeardownPlaySession): the game
		// instance shuts down first -- local players removed, its subsystems deinitialized, which for
		// the tween manager means every tween killed without its completion handlers -- and only then
		// does the world clean up. The world never began play, so there is no EndPlay to route first.
		if (IsValid(GameInstance))
		{
			GameInstance->Shutdown();
		}
		if (OwnedWorld != nullptr)
		{
			// false, matching the CreateWorld(..., bInformEngineOfWorld = false) InitializeStandalone
			// made it with: the engine was never told this world was added, so it is not told it went.
			OwnedWorld->DestroyWorld(false);
		}
		// Last, by pointer: DestroyWorldContext finds the context whose world IS this one, which still
		// holds after DestroyWorld because that only marks the world for collection. Leaving the
		// context behind would leave a Game world in every GEngine->GetWorldContexts() walk made by
		// every test after this one.
		if (GEngine != nullptr && OwnedWorld != nullptr)
		{
			GEngine->DestroyWorldContext(OwnedWorld);
		}
		if (GameInstance != nullptr)
		{
			GameInstance->RemoveFromRoot();
		}
		GameInstance = nullptr;
		World = nullptr;
	}

	bool FScopedGameInstanceWorld::HasWorldContextFor(const UWorld* InWorld)
	{
		if (GEngine == nullptr || InWorld == nullptr)
		{
			return false;
		}
		// Pointer identity only, never a dereference: the world being asked about may well be gone.
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() == InWorld)
			{
				return true;
			}
		}
		return false;
	}

	bool FScopedGameInstanceWorld::HasWorldContextOwnedBy(const UGameInstance* InGameInstance)
	{
		if (GEngine == nullptr || InGameInstance == nullptr)
		{
			return false;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.OwningGameInstance.Get() == InGameInstance)
			{
				return true;
			}
		}
		return false;
	}
}
