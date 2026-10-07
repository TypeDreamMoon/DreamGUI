// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamInputKeySelectorDestroyReentryTestTypes.h"
#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverGameHost.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamInputKeySelectorDestroyDuringCaptureTest,
	"DreamGUI.InputKeySelector.ACapturedKeyStopsPublishingAfterItsNotificationDestroysTheSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamInputKeySelectorDestroyDuringCaptureTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Destroying from listening ended"));
	OutTestCommands.Add(TEXT("listening"));
	OutBeautifiedNames.Add(TEXT("Destroying from chord selected"));
	OutTestCommands.Add(TEXT("chord"));
	OutBeautifiedNames.Add(TEXT("Destroying from key selected"));
	OutTestCommands.Add(TEXT("key"));
}

bool FDreamInputKeySelectorDestroyDuringCaptureTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real player-controller input rig came up"), Rig.IsUsable()
		&& Rig.GetPlayerController() != nullptr && Rig.GetPlayerController()->PlayerInput != nullptr))return false;
	UDreamInputKeySelector* Selector = Rig.MakeControl<UDreamInputKeySelector>(TEXT("TransientBinding"), nullptr, FVector2D(220.0, 60.0));
	if (!TestNotNull(TEXT("the settings selector is realized"), Selector))return false;
	Selector->SetSelectedKey(EKeys::G);
	if (!TestTrue(TEXT("the selector belongs to the controller whose keys will be sent"), Selector->GetOwningPlayer() == Rig.GetPlayerController()))return false;
	TStrongObjectPtr<UDreamInputKeySelectorDestroyReentryProbe> Probe(NewObject<UDreamInputKeySelectorDestroyReentryProbe>());
	Probe->Selector = Selector;
	Probe->DestroyPhase = Parameters == TEXT("listening") ? 0 : (Parameters == TEXT("chord") ? 1 : 2);
	Selector->OnIsListeningChanged.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleListeningChanged);
	Selector->OnChordSelected.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleChordSelected);
	Selector->OnKeySelected.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleKeySelected);
	Selector->OnValueChangedBP.AddDynamic(Probe.Get(), &UDreamInputKeySelectorDestroyReentryProbe::HandleValueChanged);
	const TWeakObjectPtr<UDreamInputKeySelector> WeakSelector(Selector);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("the pointer click armed the selector"), Rig.Driver()->Find(FDreamBy::Widget(Selector))->Click()
		&& Selector->GetIsListening()))return false;
	TestEqual(TEXT("the click announced the armed edge"), Probe->ListeningCount, 1);
	// Enter the real controller's key stack and let the selector's spawned capture agent receive
	// it. The public callback calls only DestroyWidget: no collector or direct delegate invocation.
	FString WhyNot;
	const bool bKeyQueued = DreamDriverGameHost::TypeKey(Rig.Context(), EKeys::F, FKey(), WhyNot);
	if (!TestTrue(FString::Printf(TEXT("F was sent through the player's controller: %s"), *WhyNot), bKeyQueued))return false;
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	DreamDriverGameHost::TickPlayerInput(Rig.Context(), Rig.Context().FrameSeconds);
	TestEqual(TEXT("the chosen capture notification removed its owner exactly once"), Probe->DestroyCount, 1);
	TestTrue(TEXT("DestroyWidget invalidated the owner before its handler returned"), Probe->bOwnerBecameInvalidInsideHandler);
	TestFalse(TEXT("the removed selector remains invalid after processing the key"), WeakSelector.IsValid());
	TestEqual(TEXT("the capture announced listening ended once"), Probe->ListeningCount, 2);
	TestEqual(TEXT("no chord notification follows destruction at the listening edge"), Probe->ChordCount, Probe->DestroyPhase == 0 ? 0 : 1);
	TestEqual(TEXT("no key notification follows destruction at an earlier edge"), Probe->KeyCount, Probe->DestroyPhase == 2 ? 1 : 0);
	TestEqual(TEXT("a destroyed selector sends no later two-way value notification"), Probe->ValueCount, 0);
	TestTrue(TEXT("only a value published while the selector existed reaches the setting"),
		Probe->LastPublishedKey == (Probe->DestroyPhase == 2 ? EKeys::F : EKeys::G));
	return true;
}

#endif
