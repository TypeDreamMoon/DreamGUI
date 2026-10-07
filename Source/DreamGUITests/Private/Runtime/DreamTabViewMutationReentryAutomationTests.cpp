// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamTabViewMutationReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Engine/World.h"
#include "Interaction/UIToggle.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamTabViewMutationReentryTestLocal
{
	UDreamTabView* MakeView(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the tab view's real game-world rig is usable"), InRig.IsUsable()))return nullptr;
		UDreamTabView* View = InRig.MakeControl<UDreamTabView>(TEXT("MutatingTabs"), nullptr, FVector2D(600.0, 300.0));
		if (!InTest.TestNotNull(TEXT("the tab view is realized"), View))return nullptr;
		View->SetTabLabels({FText::AsCultureInvariant(TEXT("A")), FText::AsCultureInvariant(TEXT("B")), FText::AsCultureInvariant(TEXT("C"))});
		for (const TCHAR* Name : {TEXT("Page A"), TEXT("Page B"), TEXT("Page C")})
		{
			UDreamWidget* Page = NewObject<UDreamWidget>(InRig.GetWorld());
			Page->SetDisplayName(Name);
			View->AddPage(Page);
		}
		InRig.PumpFrames(2);
		if (!InTest.TestEqual(TEXT("three actual pages were adopted"), View->GetPageCount(), 3)
			|| !InTest.TestEqual(TEXT("three actual tabs were generated"), View->Tabs.Num(), 3))return nullptr;
		return View;
	}

	int32 CountLiveStripTabs(const UDreamTabView& InView)
	{
		int32 Count = 0;
		for (UDreamWidget* Child : InView.StripNode->GetChildren())
		{
			if (IsValid(Child) && Child->GetComponent<UUIToggle>() != nullptr)++Count;
		}
		return Count;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTabViewCloseHookMutationTest,
	"DreamGUI.TabView.ACloseHookMutationCannotDestroyThePageThatReplacesItsOriginalTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTabViewCloseHookMutationTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Moving the closing tab"));
	OutTestCommands.Add(TEXT("move"));
	OutBeautifiedNames.Add(TEXT("Closing the same tab from the hook"));
	OutTestCommands.Add(TEXT("close"));
}

bool FDreamTabViewCloseHookMutationTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	UDreamTabView* View = DreamTabViewMutationReentryTestLocal::MakeView(*this, Rig);
	if (View == nullptr)return false;
	const TWeakObjectPtr<UDreamTabView> WeakView(View);
	const TWeakObjectPtr<UDreamWidget> PageA(View->GetPage(0));
	const TWeakObjectPtr<UDreamWidget> PageB(View->GetPage(1));
	const TWeakObjectPtr<UDreamWidget> PageC(View->GetPage(2));
	if (!TestTrue(TEXT("all three pages are live registered widgets"), PageA.IsValid() && PageB.IsValid() && PageC.IsValid()
		&& PageA->HasRegistered() && PageB->HasRegistered() && PageC->HasRegistered()))return false;
	TStrongObjectPtr<UDreamTabViewMutationReentryProbe> Probe(NewObject<UDreamTabViewMutationReentryProbe>());
	Probe->View = View;
	Probe->bCloseAgain = Parameters == TEXT("close");
	View->OnTabClosed.AddDynamic(Probe.Get(), &UDreamTabViewMutationReentryProbe::HandleTabClosed);
	// CloseTab's public hook runs before destruction. Moving A to the end, or closing A once
	// inside that hook, puts B at index 0 before the original call resumes.
	View->CloseTab(0);
	Rig.PumpFrames(2);
	TestEqual(TEXT("the close hook actually performed its one nested mutation"), Probe->MutationCount, 1);
	if (!TestTrue(TEXT("the owner survives the close"), WeakView.IsValid()))return false;
	TestFalse(TEXT("the originally requested page A was closed"), PageA.IsValid());
	TestTrue(TEXT("page B that replaced index 0 remains alive"), PageB.IsValid());
	TestTrue(TEXT("the unrelated page C remains alive"), PageC.IsValid());
	TestEqual(TEXT("closing A leaves exactly two pages"), View->GetPageCount(), 2);
	TestEqual(TEXT("the strip retains one tab per surviving page"), View->Tabs.Num(), 2);
	TestTrue(TEXT("page B is still the first surviving page"), View->GetPage(0) == PageB.Get());
	TestTrue(TEXT("page C is still the second surviving page"), View->GetPage(1) == PageC.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTabViewGeneratedHookRebuildTest,
	"DreamGUI.TabView.AGeneratedHookRebuildOwnsTheReplacementStripWithoutOldDuplicateTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabViewGeneratedHookRebuildTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	UDreamTabView* View = DreamTabViewMutationReentryTestLocal::MakeView(*this, Rig);
	if (View == nullptr)return false;
	const TWeakObjectPtr<UDreamTabView> WeakView(View);
	const TWeakObjectPtr<UDreamWidget> PageA(View->GetPage(0));
	const TWeakObjectPtr<UDreamWidget> PageB(View->GetPage(1));
	const TWeakObjectPtr<UDreamWidget> PageC(View->GetPage(2));
	TStrongObjectPtr<UDreamTabViewMutationReentryProbe> Probe(NewObject<UDreamTabViewMutationReentryProbe>());
	Probe->View = View;
	View->OnTabGenerated.AddDynamic(Probe.Get(), &UDreamTabViewMutationReentryProbe::HandleTabGenerated);
	View->SetTabLabels({FText::AsCultureInvariant(TEXT("Outer A")), FText::AsCultureInvariant(TEXT("Outer B")), FText::AsCultureInvariant(TEXT("Outer C"))});
	Rig.PumpFrames(2);
	TestEqual(TEXT("the generated hook replaced the captions exactly once"), Probe->MutationCount, 1);
	if (!TestTrue(TEXT("the owner survives its nested rebuild"), WeakView.IsValid()))return false;
	TestTrue(TEXT("all original pages survive a captions-only rebuild"), PageA.IsValid() && PageB.IsValid() && PageC.IsValid());
	TestEqual(TEXT("the replacement still has three actual pages"), View->GetPageCount(), 3);
	TestEqual(TEXT("the replacement captions generate exactly three tab entries"), View->Tabs.Num(), 3);
	TestEqual(TEXT("the live strip also contains exactly three tab widgets"), DreamTabViewMutationReentryTestLocal::CountLiveStripTabs(*View), 3);
	TestEqual(TEXT("the old pass stops after one row and the new pass produces its three"), Probe->GeneratedCount, 4);
	TestEqual(TEXT("the first replacement caption remains authoritative"), View->TabLabels[0].ToString(), FString(TEXT("New A")));
	TestEqual(TEXT("the last replacement caption remains authoritative"), View->TabLabels[2].ToString(), FString(TEXT("New C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTabViewChangedHookRedirectTest,
	"DreamGUI.TabView.AChangedHookRedirectKeepsItsPageAndSettingWithoutAnOldValueEcho",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabViewChangedHookRedirectTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	UDreamTabView* View = DreamTabViewMutationReentryTestLocal::MakeView(*this, Rig);
	if (View == nullptr)return false;
	const TWeakObjectPtr<UDreamTabView> WeakView(View);
	const TWeakObjectPtr<UDreamWidget> PageC(View->GetPage(2));
	TStrongObjectPtr<UDreamTabViewMutationReentryProbe> Probe(NewObject<UDreamTabViewMutationReentryProbe>());
	Probe->View = View;
	View->OnTabChanged.AddDynamic(Probe.Get(), &UDreamTabViewMutationReentryProbe::HandleTabChanged);
	View->OnValueChangedBP.AddDynamic(Probe.Get(), &UDreamTabViewMutationReentryProbe::HandleValueChanged);
	// The consumer corrects its setting and silently redirects the control from B to C.
	// The old B setter must not follow that with a stale reverse-binding write of 1.
	View->SetActiveTabIndex(1);
	Rig.PumpFrames(2);
	TestEqual(TEXT("the changed callback redirected the selection exactly once"), Probe->MutationCount, 1);
	if (!TestTrue(TEXT("the owner and destination page remain alive"), WeakView.IsValid() && PageC.IsValid()))return false;
	TestEqual(TEXT("the callback's corrected index remains authoritative"), View->GetActiveTabIndex(), 2);
	TestTrue(TEXT("the corresponding destination page is shown"), View->GetActivePage() == PageC.Get());
	TestEqual(TEXT("the old operation emits no stale two-way value echo"), Probe->ValueNotificationCount, 0);
	TestEqual(TEXT("the setting still agrees with the control's visible page"), Probe->PublishedIndex, 2);
	return true;
}
#endif
