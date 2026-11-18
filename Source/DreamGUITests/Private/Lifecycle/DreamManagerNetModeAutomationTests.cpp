// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamServerWorldWidgetTest,
	"DreamGUI.Lifecycle.WidgetsMadeInADedicatedServersWorldAreReportedOnceAsAWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A dedicated server as play-in-editor makes one for Play As Client: a PIE world told its net mode before it is
 * initialized, which is what UWorld::InternalGetNetMode answers until a net driver exists. It gets no manager. A widget
 * created there -- a player controller's BeginPlay runs on the server for every player that joins -- is the author's to
 * move to the client, so it is said once for the world, as a warning that says how; an ensure for each widget stalled
 * the editor for each one and only said that the manager was missing. Nor is a widget left there reported again when
 * the world goes and it is collected: it never had a manager to leak into. The collection runs inside the test, so an
 * error it logs is this test's.
 */
bool FDreamServerWorldWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::PIE, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, nullptr,
		/*bInSkipInitWorld*/ true);
	if (!TestNotNull(TEXT("A play-in-editor world was made"), World))
	{
		return false;
	}
	bool bWorldDestroyed = false;
	ON_SCOPE_EXIT
	{
		if (!bWorldDestroyed)
		{
			World->DestroyWorld(false);
		}
	};
	World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
	World->InitWorld();
	TestEqual(TEXT("The world is a dedicated server"), (int32)World->GetNetMode(), (int32)NM_DedicatedServer);
	TestFalse(TEXT("...which by design gets no manager"), UDreamUIManagerWorldSubsystem::ShouldRunForWorld(World));
	if (!TestNull(TEXT("...and has none"), UDreamUIManagerWorldSubsystem::GetInstance(World)))
	{
		return false;
	}

	// Three players joining: three widgets, one warning, and no ensure (which the automation would count as an error).
	AddExpectedMessage(TEXT("a dedicated server's world"), ELogVerbosity::Warning, EAutomationExpectedErrorFlags::Contains, 1,
		/*bIsRegex*/ false);
	for (int32 Player = 0; Player < 3; ++Player)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
		Widget->OnRegister();
		TestTrue(*FString::Printf(TEXT("Widget %d is registered"), Player), Widget->GetLifecycle() == EDreamWidgetLifecycle::Registered);
	}

	// The server's world ends with the widgets still in it, as a play session does when the author never destroys them.
	World->DestroyWorld(false);
	bWorldDestroyed = true;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return true;
}

#endif
