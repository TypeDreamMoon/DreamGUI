// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlNotificationReentryTestTypes.h"
#include "Core/Components/DreamWidget.h"
#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTextInputCommitReentryTest,
	"DreamGUI.TextInput.ACommitKeepsItsSubmittedPayloadWhenAListenerPreparesTheNextValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTextInputCommitReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("The submitted listener prepares the next value"));
	OutTestCommands.Add(TEXT("replace"));
	OutBeautifiedNames.Add(TEXT("An ordinary Enter keeps both compatibility events identical"));
	OutTestCommands.Add(TEXT("normal"));
}

bool FDreamTextInputCommitReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the input rig is usable"), Rig.IsUsable()))return false;
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("CommitReentryField"), nullptr, FVector2D(320.0, 40.0));
	if (!TestNotNull(TEXT("the real text field exists"), Field) ||
		!TestNotNull(TEXT("the field has its input behaviour"), Field->InputBehaviour.Get()))return false;
	TStrongObjectPtr<UDreamTextCommitReentryProbe> Probe(NewObject<UDreamTextCommitReentryProbe>(Rig.GetWorld()));
	Probe->Field = Field;
	Probe->bReplaceOnSubmit = Parameters == TEXT("replace");
	Field->OnSubmitted.AddDynamic(Probe.Get(), &UDreamTextCommitReentryProbe::HandleSubmitted);
	Field->OnTextCommitted.AddDynamic(Probe.Get(), &UDreamTextCommitReentryProbe::HandleCommitted);
	Rig.PumpFrames(1);

	FDreamElementRef Element = Rig.Driver()->Find(FDreamBy::Name(TEXT("CommitReentryField")));
	Element->Type(TEXT("hello"));
	if (!TestEqual(TEXT("the player really entered the value to commit"), Field->GetText(), FString(TEXT("hello"))))return false;
	Element->Type(EKeys::Enter);

	TestEqual(TEXT("Enter publishes one compatibility submit"), Probe->Submitted.Num(), 1);
	TestEqual(TEXT("Enter publishes one text commit"), Probe->Committed.Num(), 1);
	if (Probe->Submitted.Num() == 1 && Probe->Committed.Num() == 1)
	{
		// The public header promises these spellings report the same moment with the same string.
		// A submit handler preparing a new chat message must not change what another listener saves.
		TestEqual(TEXT("the first spelling reports the value the player submitted"), Probe->Submitted[0], FString(TEXT("hello")));
		TestEqual(TEXT("the later spelling reports that same submitted value"), Probe->Committed[0], FString(TEXT("hello")));
	}
	TestEqual(TEXT("the callback's replacement remains the field's current value"), Field->GetText(),
		FString(Parameters == TEXT("replace") ? TEXT("next") : TEXT("hello")));
	TestFalse(TEXT("the ordinary Enter policy ends the edit"), Field->InputBehaviour->IsInputActive());
	return true;
}

#endif
