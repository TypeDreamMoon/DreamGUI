// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "DreamInputKeySelectorDestroyReentryTestTypes.h"
#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverGameHost.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamTextInteractionTestTypes.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamInputKeySelectorDisableReentryTest,
	"DreamGUI.InputKeySelector.DeactivatingAnArmedSelectorCannotRearmItsCaptureFromTheListeningEndedCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamInputKeySelectorDisableReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Deactivating the selector with a rearming listener"));
	OutTestCommands.Add(TEXT("self"));
	OutBeautifiedNames.Add(TEXT("Deactivating its parent with a rearming listener"));
	OutTestCommands.Add(TEXT("parent"));
	OutBeautifiedNames.Add(TEXT("Deactivating without a rearming listener still releases input"));
	OutTestCommands.Add(TEXT("normal"));
}

bool FDreamInputKeySelectorDisableReentryTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	APlayerController* Player = Rig.GetPlayerController();
	if (!TestTrue(TEXT("the real player-controller input rig is usable"), Rig.IsUsable()
		&& Player != nullptr && Player->PlayerInput != nullptr))return false;
	UDreamInputKeySelector* Selector = Rig.MakeControl<UDreamInputKeySelector>(TEXT("InactiveBinding"), nullptr, FVector2D(220.0, 60.0));
	if (!TestNotNull(TEXT("the selector is realized in the active game hierarchy"), Selector))return false;
	Selector->SetSelectedKey(EKeys::G);
	TStrongObjectPtr<UDreamInputKeySelectorDestroyReentryProbe> Probe(NewObject<UDreamInputKeySelectorDestroyReentryProbe>());
	Probe->Selector = Selector;
	Probe->bDestroyFromNotification = false;
	Probe->ReentryPhase = Parameters == TEXT("normal") ? -1 : 2;
	Selector->OnIsListeningChanged.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleListeningChanged);
	Selector->OnKeySelected.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleKeySelected);
	Selector->OnValueChangedBP.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleValueChanged);
	TStrongObjectPtr<UDreamTextInteractionListener> GameListener(NewObject<UDreamTextInteractionListener>());
	TStrongObjectPtr<UInputComponent> GameInput(NewObject<UInputComponent>(Player));
	GameInput->Priority = 0;
	GameInput->BindKey(EKeys::F, IE_Pressed, GameListener.Get(), &UDreamTextInteractionListener::HandlePassedKey);
	Player->PushInputComponent(GameInput.Get());
	ON_SCOPE_EXIT { Player->PopInputComponent(GameInput.Get()); };
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("a real pointer click starts the selector's capture"), Rig.Driver()->Find(FDreamBy::Widget(Selector))->Click()
		&& Selector->GetIsListening()))return false;

	// SetWidgetActive is the public screen lifecycle path. Its real event bridge cancels listening,
	// and the observer asks for the next capture once, just as a consecutive-binding screen does.
	UDreamWidget* Deactivated = Parameters == TEXT("parent") ? Rig.Root() : Selector;
	Deactivated->SetWidgetActive(false);
	TestFalse(TEXT("the selector is inactive in its hierarchy"), Selector->GetWidgetActiveInHierarchy());
	TestEqual(TEXT("the callback made exactly its configured rearm attempt"), Probe->MutationCount, Parameters == TEXT("normal") ? 0 : 1);
	TestFalse(TEXT("the listening-ended callback cannot leave an inactive selector armed"), Selector->GetIsListening());
	TestEqual(TEXT("deactivation publishes only the armed and disarmed edges"), Probe->ListeningCount, 2);

	FString WhyNot;
	const bool bFQueued = DreamDriverGameHost::TypeKey(Rig.Context(), EKeys::F, FKey(), WhyNot);
	if (!TestTrue(FString::Printf(TEXT("F enters the player's actual key stack: %s"), *WhyNot), bFQueued))return false;
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	TestEqual(TEXT("the inactive screen leaves F available to the game's own binding"), GameListener->PassedKeyCount, 1);
	TestTrue(TEXT("the inactive selector keeps its original G binding"), Selector->GetSelectedKey() == EKeys::G);
	TestEqual(TEXT("the inactive selector publishes no key selection"), Probe->KeyCount, 0);
	TestEqual(TEXT("the inactive selector publishes no two-way setting change"), Probe->ValueCount, 0);

	// Deactivation must not permanently end this widget's lifetime: revealing the same screen makes
	// it possible to bind again, through the same real pointer and input-stack paths.
	Deactivated->SetWidgetActive(true);
	Rig.PumpFrames(2);
	TestTrue(TEXT("the same selector becomes active again"), Selector->GetWidgetActiveInHierarchy());
	if (!TestTrue(TEXT("it can capture again after its screen returns"), Rig.Driver()->Find(FDreamBy::Widget(Selector))->Click()
		&& Selector->GetIsListening()))return false;
	if (!TestTrue(TEXT("H enters the player's actual key stack"), DreamDriverGameHost::TypeKey(Rig.Context(), EKeys::H, FKey(), WhyNot)))return false;
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	TestTrue(TEXT("the restored screen can bind H normally"), Selector->GetSelectedKey() == EKeys::H);
	TestFalse(TEXT("the normal restored capture finishes"), Selector->GetIsListening());
	return true;
}

#endif
