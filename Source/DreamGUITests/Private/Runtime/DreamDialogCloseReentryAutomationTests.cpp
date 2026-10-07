// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlNotificationReentryTestTypes.h"
#include "Core/Components/DreamWidget.h"
#include "Driver/DreamDriverRig.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamDialogCloseReentryTest,
	"DreamGUI.Controls.Dialog.AButtonCallbackCannotBeFollowedByAnObsoleteCloseNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamDialogCloseReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("The button listener closes with its own result"));
	OutTestCommands.Add(TEXT("close"));
	OutBeautifiedNames.Add(TEXT("The button listener destroys the dialog"));
	OutTestCommands.Add(TEXT("destroy"));
	OutBeautifiedNames.Add(TEXT("An ordinary default button closes once with its result"));
	OutTestCommands.Add(TEXT("normal"));
}

bool FDreamDialogCloseReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the dialog rig is usable"), Rig.IsUsable()))return false;
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("ReentrantDialog"), nullptr, FVector2D(600.0, 400.0));
	if (!TestNotNull(TEXT("the real dialog exists"), Dialog) ||
		!TestNotNull(TEXT("its default button exists"), Dialog->GetDefaultButton()))return false;
	TStrongObjectPtr<UDreamDialogCloseReentryProbe> Probe(NewObject<UDreamDialogCloseReentryProbe>(Rig.GetWorld()));
	Probe->Dialog = Dialog;
	Probe->ButtonAction = Parameters == TEXT("close") ? 1 : Parameters == TEXT("destroy") ? 2 : 0;
	Dialog->OnButtonClicked.AddDynamic(Probe.Get(), &UDreamDialogCloseReentryProbe::HandleButtonClicked);
	Dialog->OnDialogClosed.AddDynamic(Probe.Get(), &UDreamDialogCloseReentryProbe::HandleClosed);
	Rig.PumpFrames(1);

	// This public control entry point routes through the actual default button's click binding.
	// OnDialogClosed documents exactly one result, before its owner tears the dialog down.
	Dialog->SubmitDefaultButton();
	TestEqual(TEXT("the default button reached the control listener once"), Probe->ClickCount, 1);
	TestEqual(TEXT("a destroyed dialog publishes no later close notification"), Probe->ClosedAfterDestruction, 0);
	if (Parameters == TEXT("destroy"))
	{
		TestFalse(TEXT("the listener really destroyed the dialog"), IsValid(Dialog));
		TestEqual(TEXT("there is no new close operation after destruction"), Probe->ClosedResults.Num(), 0);
	}
	else
	{
		TestFalse(TEXT("the standalone dialog went to sleep"), Dialog->GetWidgetActive());
		TestEqual(TEXT("one button action publishes one close result"), Probe->ClosedResults.Num(), 1);
		if (!Probe->ClosedResults.IsEmpty())
		{
			TestEqual(TEXT("the close keeps the result chosen by its current owner"), Probe->ClosedResults[0],
				FName(Parameters == TEXT("close") ? TEXT("Cancel") : TEXT("Confirm")));
		}
	}
	return true;
}

#endif
