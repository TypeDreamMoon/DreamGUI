// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamDropdownGenerationReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamDropdownGenerationReentryTestLocal
{
	UDreamDropdown* MakeDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig,
		UDreamDropdownGenerationReentryProbe& InProbe)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the dropdown generation rig came up"), InRig.IsUsable()))return nullptr;
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("GeneratedDropdown"), nullptr,
			FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
		if (!InTest.TestTrue(TEXT("the dropdown and its list behaviour were realized"),
			Dropdown != nullptr && Dropdown->DropdownBehaviour != nullptr && Dropdown->ListNode != nullptr))return nullptr;
		Dropdown->SetOptions({FText::AsCultureInvariant(TEXT("First")), FText::AsCultureInvariant(TEXT("Second"))});
		Dropdown->SetSelectedIndex(0);
		InProbe.Dropdown = Dropdown;
		Dropdown->OnItemGenerated.AddDynamic(&InProbe, &UDreamDropdownGenerationReentryProbe::OnGenerated);
		Dropdown->OnOpening.AddDynamic(&InProbe, &UDreamDropdownGenerationReentryProbe::OnOpening);
		InRig.PumpFrames(2);
		return Dropdown;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDropdownGeneratedRowHideReentryTest,
	"DreamGUI.Dropdown.AGeneratedRowCanCloseItsDropdownWithoutTheOldShowPublishingOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDropdownGeneratedRowHideReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	TStrongObjectPtr<UDreamDropdownGenerationReentryProbe> Probe(NewObject<UDreamDropdownGenerationReentryProbe>());
	UDreamDropdown* Dropdown = DreamDropdownGenerationReentryTestLocal::MakeDropdown(*this, Rig, *Probe.Get());
	if (Dropdown == nullptr)return false;
	UDreamWidget* List = Dropdown->ListNode;
	// The first row calls the public Hide API before Show has announced an opening. The old
	// generation pass must not subsequently announce open or overwrite the control's closed flag.
	Dropdown->DropdownBehaviour->Show();
	Rig.PumpFrames(30);
	bool bValid = TestEqual(TEXT("the generated-row callback really canceled the opening once"), Probe->MutationCount, 1);
	bValid = TestFalse(TEXT("the generated-row close owns the public open state"), Dropdown->IsOpen()) && bValid;
	bValid = TestFalse(TEXT("the canceled list is asleep after its close finishes"), List->GetWidgetActive()) && bValid;
	bValid = TestEqual(TEXT("the canceled Show sends no later opening notification"), Probe->OpeningCount, 0) && bValid;
	Dropdown->DropdownBehaviour->Hide();
	bValid = TestFalse(TEXT("repeating Hide does not expose a stale open flag"), Dropdown->IsOpen()) && bValid;

	Dropdown->DropdownBehaviour->Show();
	Rig.PumpFrames(2);
	bValid = TestTrue(TEXT("a later fresh Show opens normally"), Dropdown->IsOpen()) && bValid;
	bValid = TestTrue(TEXT("the fresh list is active"), List->GetWidgetActive()) && bValid;
	bValid = TestEqual(TEXT("only the successful fresh opening is announced"), Probe->OpeningCount, 1) && bValid;
	bValid = TestEqual(TEXT("the generation callback canceled only its first opening"), Probe->MutationCount, 1) && bValid;
	Dropdown->DropdownBehaviour->Hide();
	Rig.PumpFrames(30);
	bValid = TestFalse(TEXT("the fresh list closes normally"), Dropdown->IsOpen()) && bValid;
	bValid = TestFalse(TEXT("the fresh list goes back to sleep"), List->GetWidgetActive()) && bValid;
	return bValid;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDropdownGeneratedRowDestroyOwnerReentryTest,
	"DreamGUI.Dropdown.AGeneratedRowCanDestroyItsDropdownAndStopTheOldGenerationPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDropdownGeneratedRowDestroyOwnerReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	TStrongObjectPtr<UDreamDropdownGenerationReentryProbe> Probe(NewObject<UDreamDropdownGenerationReentryProbe>());
	Probe->bDestroyOwner = true;
	UDreamDropdown* Dropdown = DreamDropdownGenerationReentryTestLocal::MakeDropdown(*this, Rig, *Probe.Get());
	if (Dropdown == nullptr)return false;
	const TWeakObjectPtr<UDreamDropdown> WeakDropdown(Dropdown);
	const TWeakObjectPtr<UDreamWidget> WeakList(Dropdown->ListNode);
	// Run this test independently on an unfixed build: returning from this real public callback
	// currently resumes row creation using the destroyed owner, template and list root.
	Dropdown->DropdownBehaviour->Show();
	bool bValid = TestEqual(TEXT("the owner-destruction callback actually ran once"), Probe->MutationCount, 1);
	bValid = TestTrue(TEXT("DestroyWidget invalidated the owner inside the callback"), Probe->bOwnerWasDestroyedInsideCallback) && bValid;
	bValid = TestFalse(TEXT("the destroyed dropdown remains invalid after Show returns"), WeakDropdown.IsValid()) && bValid;
	bValid = TestFalse(TEXT("the destroyed dropdown's list is also invalid"), WeakList.IsValid()) && bValid;
	bValid = TestEqual(TEXT("no later option is generated after its owner was destroyed"), Probe->GeneratedCount, 1) && bValid;
	bValid = TestEqual(TEXT("a destroyed opening emits no later opening notification"), Probe->OpeningCount, 0) && bValid;
	return bValid;
}

#endif
