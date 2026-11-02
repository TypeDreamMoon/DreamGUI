// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamCultureListenerTestTypes.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Internationalization/TextLocalizationResource.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamScopedWorld.h"

/*
 * New translations in the culture the game is already in.
 *
 * A hot-reloaded or refreshed translation table, a chunk's locres arriving, a text source registered
 * late: none of these change the culture, so FInternationalization::OnCultureChanged never fires. The
 * engine announces them through FTextLocalizationManager::OnTextRevisionChangedEvent instead. Text
 * that only relayouts on the culture event keeps showing the old words until something else happens
 * to dirty it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIManagerTextRevisionReachesCultureListenersTest,
	"DreamGUI.Manager.NewTranslationsInTheSameCultureReachCultureListenersOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIManagerTextRevisionReachesCultureListenersTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("a game world with a UI manager"), Manager))
	{
		return false;
	}

	TStrongObjectPtr<UDreamCultureListenerProbe> Probe(
		NewObject<UDreamCultureListenerProbe>(TestWorld.World, UDreamCultureListenerProbe::StaticClass()));
	UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(Probe.Get());
	// Whatever an earlier test left pending is spent here, so the count below is this test's own.
	Manager->Tick(1.0f / 30.0f);
	const int32 Before = Probe->Calls;

	// One entry in a namespace nothing else uses, so the live table is left as every other text found it.
	int32 Revisions = 0;
	FTextLocalizationManager& TextManager = FTextLocalizationManager::Get();
	const FDelegateHandle Counter = TextManager.OnTextRevisionChangedEvent.AddLambda([&Revisions]() { ++Revisions; });
	FTextLocalizationResource Resource;
	Resource.AddEntry(FTextKey(TEXT("DreamGUITests.TextRevision")), FTextKey(TEXT("Probe")),
		FString(TEXT("Source")), FString(TEXT("Translated")), /*InPriority*/0);
	TextManager.UpdateFromLocalizationResource(Resource);
	TextManager.OnTextRevisionChangedEvent.Remove(Counter);

	// The engine half: if this stops holding, the rest of the test proves nothing.
	if (TestTrue(TEXT("the engine announces a live-table update as a text revision change"), Revisions >= 1))
	{
		TestEqual(TEXT("nothing is told before the manager's next frame"), Probe->Calls, Before);
		Manager->Tick(1.0f / 30.0f);
		TestEqual(TEXT("the frame after, the listener is told the words may have changed"), Probe->Calls, Before + 1);
		Manager->Tick(1.0f / 30.0f);
		TestEqual(TEXT("and told once, not every frame after"), Probe->Calls, Before + 1);
	}

	UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(Probe.Get());
	return true;
}

#endif
