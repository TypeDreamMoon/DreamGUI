// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"

#include "DreamScopedWorld.h"

/*
 * NO UI MANAGER ON A DEDICATED SERVER.
 *
 * A dedicated server draws nothing and has no player, so its worlds get no UI manager -- and with it none of what a
 * manager makes: the canvases' draw data, the renderer, the paint rows. UDreamUIManagerWorldSubsystem::ShouldCreateSubsystem
 * asks one predicate with the world's net mode (NM_DedicatedServer whenever the process is a dedicated server), and the
 * predicate is what is held here, mode by mode: a dedicated server can only be had headlessly by building one, which is
 * the server target nobody has built. The standalone world the rest of the suite runs in still gets its manager.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamManagerNetModeTest,
	"DreamGUI.Lifecycle.TheUIManagerStartsInEveryNetModeButADedicatedServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamManagerNetModeTest::RunTest(const FString& Parameters)
{
	for (int32 Mode = 0; Mode < (int32)NM_MAX; ++Mode)
	{
		const ENetMode NetMode = (ENetMode)Mode;
		const bool bExpected = NetMode != NM_DedicatedServer;
		TestTrue(*FString::Printf(TEXT("A world in %s %s a UI manager"), *::ToString(NetMode), bExpected ? TEXT("gets") : TEXT("gets no")),
			UDreamUIManagerWorldSubsystem::ShouldRunForNetMode(NetMode) == bExpected);
	}

	// The predicate behind the subsystem collection's question, asked the way the collection asks it: of the class default,
	// with the world being made as the outer. This process is no dedicated server and the world is standalone.
	DreamTests::FScopedGameWorld Scoped;
	if (!TestNotNull(TEXT("A game world was made"), Scoped.World))
	{
		return false;
	}
	TestEqual(TEXT("The test world is standalone"), (int32)Scoped.World->GetNetMode(), (int32)NM_Standalone);
	TestTrue(TEXT("The class default says a standalone game world gets a UI manager"),
		GetDefault<UDreamUIManagerWorldSubsystem>()->ShouldCreateSubsystem(Scoped.World));
	TestNotNull(TEXT("...and the world has one"), UDreamUIManagerWorldSubsystem::GetInstance(Scoped.World));
	return true;
}

#endif
