// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamFocusReentryTestTypes.h"
#include "Controls/DreamTabView.h"
#include "Core/DreamUIInputServices.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/UIToggle.h"

namespace DreamTabViewFocusCarryTestLocal
{
	UDreamTabView* MakeView(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the actual game-world rig came up"), InRig.IsUsable()))return nullptr;
		UDreamTabView* View = InRig.MakeControl<UDreamTabView>(TEXT("FocusCarryTabs"), nullptr, FVector2D(600.0, 300.0));
		if (!InTest.TestNotNull(TEXT("the tab view is realized"), View))return nullptr;
		View->SetTabLabels({FText::AsCultureInvariant(TEXT("A")), FText::AsCultureInvariant(TEXT("B")), FText::AsCultureInvariant(TEXT("C"))});
		InRig.PumpFrames(2);
		return InTest.TestEqual(TEXT("three tabs were generated"), View->Tabs.Num(), 3) ? View : nullptr;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTabViewFocusCarryRedirectTest,
	"DreamGUI.TabView.AFocusRestoreCallbackKeepsThePlayersExplicitNewDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTabViewFocusCarryRedirectTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("The restoring player moves elsewhere"));
	OutTestCommands.Add(TEXT("same"));
	OutBeautifiedNames.Add(TEXT("The other player moves elsewhere"));
	OutTestCommands.Add(TEXT("other"));
	OutBeautifiedNames.Add(TEXT("A nested rebuild moves the other player elsewhere"));
	OutTestCommands.Add(TEXT("rebuild"));
}

bool FDreamTabViewFocusCarryRedirectTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.ViewportSize = FIntPoint(1280, 720);
	Options.PlayerCount = 2;
	Options.PlayerScreens = EDreamRigPlayerScreens::Shared;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	UDreamTabView* View = DreamTabViewFocusCarryTestLocal::MakeView(*this, Rig);
	if (View == nullptr)return false;
	UDreamWidget* Outside = Rig.MakeWidget(TEXT("ExplicitDestination"), nullptr, FVector2D(150.0, 50.0), FVector2D(450.0, 0.0));
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("the public input service and outside destination exist"), Services != nullptr && Outside != nullptr))return false;
	Outside->SetIsFocusable(true);
	Rig.PumpFrames(1);
	UDreamWidget* FirstTab = View->Tabs[0].TabNode.Get();
	UDreamWidget* DestinationTab = View->Tabs[1].TabNode.Get();
	if (!TestTrue(TEXT("both players start on the tab that will be disabled"),
		Services->FocusForNavigation(FirstTab, 0) && Services->FocusForNavigation(FirstTab, 1)))return false;
	UDreamFocusReentryProbe* Probe = DestinationTab->AddComponent<UDreamFocusReentryProbe>();
	if (!TestNotNull(TEXT("the destination tab has a real focus listener"), Probe))return false;
	DestinationTab->OnFocusReceived.AddDynamic(Probe, &UDreamFocusReentryProbe::OnReceived);
	Probe->Trigger = EDreamFocusReentryCallback::Received;
	const bool bRebuild = Parameters == TEXT("rebuild");
	const int32 RedirectedPlayer = Parameters == TEXT("same") ? 0 : 1;
	int32 CallbackCount = 0;
	bool bFirstPlayerWasRestoring = false;
	bool bRedirectSucceeded = false;
	bool bOutsideHeldFocusInsideCallback = false;
	const TWeakObjectPtr<UDreamWidget> OriginalDestination(DestinationTab);
	Probe->Action = [View, Services, Outside, DestinationTab, RedirectedPlayer, bRebuild,
		&CallbackCount, &bFirstPlayerWasRestoring, &bRedirectSucceeded, &bOutsideHeldFocusInsideCallback]()
	{
		++CallbackCount;
		bFirstPlayerWasRestoring = Services->GetFocusedWidget(0) == DestinationTab;
		if (bRebuild)
		{
			View->SetTabLabels({FText::AsCultureInvariant(TEXT("New A")), FText::AsCultureInvariant(TEXT("New B")), FText::AsCultureInvariant(TEXT("New C"))});
		}
		bRedirectSucceeded = Services->FocusForNavigation(Outside, RedirectedPlayer);
		bOutsideHeldFocusInsideCallback = Services->GetFocusedWidget(RedirectedPlayer) == Outside;
	};
	// Disabling A restores both players onto B. Its first received callback deliberately
	// gives a player another destination; the old carry must not overwrite that decision.
	View->SetTabEnabled(0, false);
	TestEqual(TEXT("the received callback made its explicit choice exactly once"), CallbackCount, 1);
	TestTrue(TEXT("the callback ran while player 0 was being restored"), bFirstPlayerWasRestoring);
	TestTrue(TEXT("the nested public focus request succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the requested destination held focus before the callback returned"), bOutsideHeldFocusInsideCallback);
	if (bRebuild)
	{
		TestFalse(TEXT("the callback replaced the original strip"), OriginalDestination.IsValid());
		TestEqual(TEXT("the replacement strip has exactly three tabs"), View->Tabs.Num(), 3);
	}
	TestTrue(TEXT("the old carry preserves the callback's explicit destination"),
		Services->GetFocusedWidget(RedirectedPlayer) == Outside);
	Rig.PumpFrames(1);
	TestTrue(TEXT("the explicit destination also survives the next real frame"),
		Services->GetFocusedWidget(RedirectedPlayer) == Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTabViewEnabledArrayGrowthTest,
	"DreamGUI.TabView.DisablingALaterTabPreservesEarlierDisabledTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabViewEnabledArrayGrowthTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	UDreamTabView* View = DreamTabViewFocusCarryTestLocal::MakeView(*this, Rig);
	if (View == nullptr)return false;
	View->SetTabEnabled(0, false);
	if (!TestFalse(TEXT("the first public call disables A"), View->IsTabEnabled(0)))return false;
	View->SetTabEnabled(2, false);
	TestFalse(TEXT("disabling C retains the earlier disabled A"), View->IsTabEnabled(0));
	TestTrue(TEXT("the intervening tab remains enabled by default"), View->IsTabEnabled(1));
	TestFalse(TEXT("the newly requested tab C is disabled"), View->IsTabEnabled(2));
	TestFalse(TEXT("A's actual toggle still refuses interaction"), View->Tabs[0].Toggle->IsInteractable());
	TestTrue(TEXT("B's actual toggle remains interactable"), View->Tabs[1].Toggle->IsInteractable());
	TestFalse(TEXT("C's actual toggle refuses interaction"), View->Tabs[2].Toggle->IsInteractable());
	return true;
}

#endif
