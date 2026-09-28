// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Containers/Ticker.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * WHAT ONE EDITOR OBJECT USED TO DO FOR EVERY WORLD IS EACH WORLD'S, OR THE ENGINE'S.
 *
 * A single rooted editor object ticked every widget that animates in an editor world without play and ran
 * every deferred editor callback from one global queue: a preview's widgets were ticked by an object the
 * preview did not own, and the queue outlived every world whose callbacks it held. The editor tick is each
 * world manager's now, and a deferral is the engine's core ticker's, holding nothing of DreamGUI's.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleEditorTickPerWorldTest,
	"DreamGUI.Lifecycle.AnEditorWorldTicksOnlyItsOwnEditorWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleEditorTickPerWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedWorld First(EWorldType::Editor);
	FScopedWorld Second(EWorldType::Editor);
	FScopedWorld Game(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* FirstManager = UDreamUIManagerWorldSubsystem::GetInstance(First.World);
	UDreamUIManagerWorldSubsystem* SecondManager = UDreamUIManagerWorldSubsystem::GetInstance(Second.World);
	UDreamUIManagerWorldSubsystem* GameManager = UDreamUIManagerWorldSubsystem::GetInstance(Game.World);
	if (!TestNotNull(TEXT("the first editor world has a UI manager"), FirstManager)
		|| !TestNotNull(TEXT("and the second"), SecondManager)
		|| !TestNotNull(TEXT("and the game world"), GameManager))
	{
		return false;
	}

	const TSharedRef<int32> FirstTicks = MakeShared<int32>(0);
	const TSharedRef<int32> SecondTicks = MakeShared<int32>(0);
	const TSharedRef<int32> GameTicks = MakeShared<int32>(0);
	const FDelegateHandle FirstHandle = FirstManager->GetEditorTickDelegate().AddLambda([FirstTicks](float) { ++*FirstTicks; });
	const FDelegateHandle SecondHandle = SecondManager->GetEditorTickDelegate().AddLambda([SecondTicks](float) { ++*SecondTicks; });
	const FDelegateHandle GameHandle = GameManager->GetEditorTickDelegate().AddLambda([GameTicks](float) { ++*GameTicks; });

	FirstManager->Tick(0.016f);
	TestEqual(TEXT("A tick of one editor world reaches its own editor widgets"), *FirstTicks, 1);
	TestEqual(TEXT("and no other world's"), *SecondTicks, 0);
	SecondManager->Tick(0.016f);
	TestEqual(TEXT("The other world ticks its own"), *SecondTicks, 1);
	TestEqual(TEXT("and leaves the first alone"), *FirstTicks, 1);
	GameManager->Tick(0.016f);
	TestEqual(TEXT("A world at play ticks no editor widgets: its widgets have play to run them"), *GameTicks, 0);

	FirstManager->GetEditorTickDelegate().Remove(FirstHandle);
	SecondManager->GetEditorTickDelegate().Remove(SecondHandle);
	GameManager->GetEditorTickDelegate().Remove(GameHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleDeferToLaterTickTest,
	"DreamGUI.Lifecycle.ADeferredEditorCallbackRunsOnTheCoreTickerOnceItsTicksHavePassed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleDeferToLaterTickTest::RunTest(const FString& Parameters)
{
	// Counted through a shared value: a callback still queued when a failing test returns must not be
	// left holding this frame's stack.
	const TSharedRef<int32> Runs = MakeShared<int32>(0);
	DreamUI::DeferToLaterTick([Runs]() { ++*Runs; }, /*InTicks*/ 1);
	TestEqual(TEXT("Nothing runs before the ticker ticks"), *Runs, 0);
	FTSTicker::GetCoreTicker().Tick(0.0f);
	TestEqual(TEXT("One tick later it is still waiting: it asked for one more"), *Runs, 0);
	FTSTicker::GetCoreTicker().Tick(0.0f);
	TestEqual(TEXT("and runs on the next"), *Runs, 1);
	FTSTicker::GetCoreTicker().Tick(0.0f);
	TestEqual(TEXT("once"), *Runs, 1);
	return true;
}

#endif
