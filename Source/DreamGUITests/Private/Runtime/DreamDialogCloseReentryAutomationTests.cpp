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
	OutBeautifiedNames.Add(TEXT("The button listener closes and reactivates a new question"));
	OutTestCommands.Add(TEXT("reopen"));
	OutBeautifiedNames.Add(TEXT("The closed listener tries to close the same question recursively"));
	OutTestCommands.Add(TEXT("recursive"));
	OutBeautifiedNames.Add(TEXT("The button listener ends play and a later lifetime is reusable"));
	OutTestCommands.Add(TEXT("endplay"));
	OutBeautifiedNames.Add(TEXT("The closed listener destroys the dialog before the close resumes"));
	OutTestCommands.Add(TEXT("closed_destroy"));
	OutBeautifiedNames.Add(TEXT("The closed listener ends play without hiding the next lifetime"));
	OutTestCommands.Add(TEXT("closed_endplay"));
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
	Probe->ButtonAction = Parameters == TEXT("close") ? 1 : Parameters == TEXT("destroy") ? 2 : Parameters == TEXT("reopen") ? 3 : 0;
	if (Parameters == TEXT("endplay"))Probe->ButtonAction = 4;
	Probe->bCloseAgainFromClosed = Parameters == TEXT("recursive");
	Probe->bDestroyFromClosed = Parameters == TEXT("closed_destroy");
	Probe->bEndPlayFromClosed = Parameters == TEXT("closed_endplay");
	Dialog->OnButtonClicked.AddDynamic(Probe.Get(), &UDreamDialogCloseReentryProbe::HandleButtonClicked);
	Dialog->OnDialogClosed.AddDynamic(Probe.Get(), &UDreamDialogCloseReentryProbe::HandleClosed);
	Rig.PumpFrames(1);

	// This public control entry point routes through the actual default button's click binding.
	// OnDialogClosed documents exactly one result, before its owner tears the dialog down.
	Dialog->SubmitDefaultButton();
	TestEqual(TEXT("the default button reached the control listener once"), Probe->ClickCount, 1);
	TestEqual(TEXT("a destroyed dialog publishes no later close notification"), Probe->ClosedAfterDestruction, 0);
	if (Parameters == TEXT("destroy") || Parameters == TEXT("closed_destroy"))
	{
		TestFalse(TEXT("the listener really destroyed the dialog"), IsValid(Dialog));
		TestEqual(TEXT("destruction keeps only notifications already delivered before teardown"), Probe->ClosedResults.Num(),
			Parameters == TEXT("closed_destroy") ? 1 : 0);
	}
	else if (Parameters == TEXT("endplay") || Parameters == TEXT("closed_endplay"))
	{
		const int32 PriorResults = Parameters == TEXT("closed_endplay") ? 1 : 0;
		TestTrue(TEXT("EndPlay leaves the dialog alive"), IsValid(Dialog));
		TestFalse(TEXT("the callback really ended the current lifetime"), Dialog->HasBegunPlay());
		TestTrue(TEXT("EndPlay left hierarchy activity unchanged"), Dialog->GetWidgetActiveInHierarchy());
		TestEqual(TEXT("the expired operation cannot publish another close"), Probe->ClosedResults.Num(), PriorResults);
		Dialog->Close(TEXT("Late"));
		TestEqual(TEXT("the ended lifetime rejects a late explicit close too"), Probe->ClosedResults.Num(), PriorResults);
		Probe->ButtonAction = 0;
		Probe->bEndPlayFromClosed = false;
		Dialog->BeginPlay();
		Dialog->SubmitDefaultButton();
		TestEqual(TEXT("the new lifetime may accept its own default button"), Probe->ClickCount, 2);
		TestEqual(TEXT("the new lifetime delivers its own result"), Probe->ClosedResults.Num(), PriorResults + 1);
		if (Probe->ClosedResults.Num() == PriorResults + 1)TestEqual(TEXT("the new lifetime keeps Confirm"), Probe->ClosedResults.Last(), FName(TEXT("Confirm")));
		TestFalse(TEXT("the new lifetime closes normally"), Dialog->GetWidgetActive());
	}
	else
	{
		TestEqual(TEXT("only a callback that opened a new question leaves the dialog active"), Dialog->GetWidgetActive(), Parameters == TEXT("reopen"));
		TestEqual(TEXT("one button action publishes one close result"), Probe->ClosedResults.Num(), 1);
		if (!Probe->ClosedResults.IsEmpty())
		{
			TestEqual(TEXT("the close keeps the result chosen by its current owner"), Probe->ClosedResults[0],
				FName(Parameters == TEXT("close") || Parameters == TEXT("reopen") ? TEXT("Cancel") : TEXT("Confirm")));
		}
		TestEqual(TEXT("the configured closed listener really attempted its recursive close"), Probe->NestedCloseAttempts,
			Parameters == TEXT("recursive") ? 1 : 0);

		// A close guard must protect one question without silencing the next use of this dialog.
		Probe->ButtonAction = 0;
		Probe->bCloseAgainFromClosed = false;
		Dialog->SetWidgetActive(true);
		Dialog->SubmitDefaultButton();
		TestEqual(TEXT("the second question delivers its own click"), Probe->ClickCount, 2);
		TestEqual(TEXT("the second question delivers one independent close"), Probe->ClosedResults.Num(), 2);
		if (Probe->ClosedResults.Num() == 2)TestEqual(TEXT("the second question keeps its own result"), Probe->ClosedResults[1], FName(TEXT("Confirm")));
		TestFalse(TEXT("the second question closes normally"), Dialog->GetWidgetActive());
	}
	return true;
}

#endif
