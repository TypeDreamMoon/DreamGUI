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

namespace DreamInputKeySelectorReentryTestLocal
{
	UDreamInputKeySelector* MakeCorrectingSelector(FAutomationTestBase& InTest, FDreamDriverRig& InRig,
		UDreamInputKeySelectorDestroyReentryProbe& InProbe)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the real key-input rig is usable"), InRig.IsUsable() && InRig.GetPlayerController() != nullptr))return nullptr;
		UDreamInputKeySelector* Selector = InRig.MakeControl<UDreamInputKeySelector>(TEXT("CorrectedBinding"), nullptr, FVector2D(220.0, 60.0));
		if (!InTest.TestNotNull(TEXT("the correcting selector is realized"), Selector))return nullptr;
		Selector->SetSelectedKey(EKeys::G);
		InProbe.Selector = Selector;
		InProbe.bDestroyFromNotification = false;
		Selector->OnIsListeningChanged.AddDynamic(&InProbe, &UDreamInputKeySelectorDestroyReentryProbe::HandleListeningChanged);
		Selector->OnChordSelected.AddDynamic(&InProbe, &UDreamInputKeySelectorDestroyReentryProbe::HandleChordSelected);
		Selector->OnKeySelected.AddDynamic(&InProbe, &UDreamInputKeySelectorDestroyReentryProbe::HandleKeySelected);
		Selector->OnValueChangedBP.AddDynamic(&InProbe, &UDreamInputKeySelectorDestroyReentryProbe::HandleValueChanged);
		InRig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("clicking arms the real capture agent"), InRig.Driver()->Find(FDreamBy::Widget(Selector))->Click()
			&& Selector->GetIsListening()))return nullptr;
		return Selector;
	}

	bool TypeCapturedKey(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FKey InKey)
	{
		FString WhyNot;
		const bool bQueued = DreamDriverGameHost::TypeKey(InRig.Context(), InKey, FKey(), WhyNot);
		if (!InTest.TestTrue(FString::Printf(TEXT("the key reaches the real controller: %s"), *WhyNot), bQueued))return false;
		DreamDriverGameHost::TickPlayerInput(InRig.Context(), InRig.Context().FrameSeconds);
		DreamDriverGameHost::TickPlayerInput(InRig.Context(), InRig.Context().FrameSeconds);
		return true;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamInputKeySelectorBindingCorrectionReentryTest,
	"DreamGUI.InputKeySelector.ASelectionNotificationCanCorrectTheBindingWithoutAStaleValueEcho",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamInputKeySelectorBindingCorrectionReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Correcting from listening ended"));
	OutTestCommands.Add(TEXT("listening"));
	OutBeautifiedNames.Add(TEXT("Correcting from chord selected"));
	OutTestCommands.Add(TEXT("chord"));
}

bool FDreamInputKeySelectorBindingCorrectionReentryTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	TStrongObjectPtr<UDreamInputKeySelectorDestroyReentryProbe> Probe(NewObject<UDreamInputKeySelectorDestroyReentryProbe>());
	Probe->ReentryPhase = Parameters == TEXT("listening") ? 0 : 1;
	UDreamInputKeySelector* Selector = DreamInputKeySelectorReentryTestLocal::MakeCorrectingSelector(*this, Rig, *Probe.Get());
	if (Selector == nullptr)return false;
	const TWeakObjectPtr<UDreamInputKeySelector> WeakSelector(Selector);
	if (!DreamInputKeySelectorReentryTestLocal::TypeCapturedKey(*this, Rig, EKeys::F))return false;
	TestEqual(TEXT("the notification explicitly corrected the binding once"), Probe->MutationCount, 1);
	if (!TestTrue(TEXT("the correcting selector remains alive"), WeakSelector.IsValid()))return false;
	TestTrue(TEXT("the explicitly corrected H remains the control's binding"), Selector->GetSelectedKey() == EKeys::H);
	TestTrue(TEXT("the setting receives the same corrected binding"), Probe->LastPublishedKey == EKeys::H);
	TestFalse(TEXT("the settled capture is disarmed"), Selector->GetIsListening());
	TestEqual(TEXT("only the corrected key is published"), Probe->KeyCount, 1);
	TestEqual(TEXT("only its matching two-way value is published"), Probe->ValueCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamInputKeySelectorNextListenReentryTest,
	"DreamGUI.InputKeySelector.AListeningEndedHandlerMayBeginTheNextCaptureWhileTheCurrentKeySettles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeySelectorNextListenReentryTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	TStrongObjectPtr<UDreamInputKeySelectorDestroyReentryProbe> Probe(NewObject<UDreamInputKeySelectorDestroyReentryProbe>());
	Probe->ReentryPhase = 2;
	UDreamInputKeySelector* Selector = DreamInputKeySelectorReentryTestLocal::MakeCorrectingSelector(*this, Rig, *Probe.Get());
	if (Selector == nullptr)return false;
	const TWeakObjectPtr<UDreamInputKeySelector> WeakSelector(Selector);
	if (!DreamInputKeySelectorReentryTestLocal::TypeCapturedKey(*this, Rig, EKeys::F))return false;
	if (!TestTrue(TEXT("the selector remains alive after its listener re-arms it"), WeakSelector.IsValid()))return false;
	TestEqual(TEXT("the listening-ended callback starts one new capture"), Probe->MutationCount, 1);
	TestTrue(TEXT("the current key F still settles normally"), Selector->GetSelectedKey() == EKeys::F);
	TestTrue(TEXT("the new capture remains armed"), Selector->GetIsListening());
	TestEqual(TEXT("F was published once"), Probe->KeyCount, 1);
	TestEqual(TEXT("its two-way value was also published once"), Probe->ValueCount, 1);
	if (!DreamInputKeySelectorReentryTestLocal::TypeCapturedKey(*this, Rig, EKeys::H))return false;
	TestTrue(TEXT("the next captured key H becomes the next binding"), Selector->GetSelectedKey() == EKeys::H);
	TestTrue(TEXT("the setting follows H"), Probe->LastPublishedKey == EKeys::H);
	TestFalse(TEXT("the second capture ends normally"), Selector->GetIsListening());
	TestEqual(TEXT("the two real presses publish two keys"), Probe->KeyCount, 2);
	TestEqual(TEXT("and two matching values"), Probe->ValueCount, 2);
	return true;
}
#endif
