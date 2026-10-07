// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamMenuAnchorOperationReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamMenuAnchorDisableReentryTest,
	"DreamGUI.MenuAnchor.DeactivatingAnOpenAnchorCannotReopenItsLiftedPopupFromTheClosedCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamMenuAnchorDisableReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Deactivating the anchor with a reopening listener"));
	OutTestCommands.Add(TEXT("self"));
	OutBeautifiedNames.Add(TEXT("Deactivating its parent with a reopening listener"));
	OutTestCommands.Add(TEXT("parent"));
	OutBeautifiedNames.Add(TEXT("Deactivating without a reopening listener still closes the popup"));
	OutTestCommands.Add(TEXT("normal"));
}

bool FDreamMenuAnchorDisableReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real popup rig is usable"), Rig.IsUsable()))return false;
	UDreamWidget* Screen = Rig.MakeWidget(TEXT("MenuScreen"), nullptr, FVector2D(600.0, 400.0));
	UDreamMenuAnchor* Anchor = Rig.MakeControl<UDreamMenuAnchor>(TEXT("InactiveMenuAnchor"), Screen,
		FVector2D(160.0, 40.0), FVector2D(-150.0, 80.0));
	if (!TestNotNull(TEXT("the screen exists"), Screen) || !TestNotNull(TEXT("the anchor is realized"), Anchor))return false;
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Anchor);
	UDreamWidget* Popup = Anchor->PopupNode;
	if (!TestNotNull(TEXT("the game world's popup layer exists"), Layer) || !TestNotNull(TEXT("the anchor has its popup"), Popup))return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(Rig.GetWorld()));
	Probe->Anchor = Anchor;
	Probe->bReopenWhenClosed = Parameters != TEXT("normal");
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	ON_SCOPE_EXIT
	{
		Probe->bReopenWhenClosed = false;
		if (IsValid(Anchor))Anchor->Close();
	};
	Rig.PumpFrames(2);
	Anchor->Open(false);
	if (!TestTrue(TEXT("the opening really lifted the active popup onto the screen layer"),
		Anchor->IsOpen() && Layer->IsOpen(Popup) && Popup->GetParent() != Anchor && Popup->GetWidgetActiveInHierarchy()))return false;
	TestEqual(TEXT("the initial open is announced once"), Probe->OpenChanges.Num(), 1);

	// The observer is a keep-open policy used by a required menu. It can reopen an explicitly closed
	// active menu, but putting its screen away must take the popup and that policy out of action.
	UDreamWidget* Deactivated = Parameters == TEXT("parent") ? Screen : Anchor;
	Deactivated->SetWidgetActive(false);
	TestFalse(TEXT("the anchor is inactive in its hierarchy"), Anchor->GetWidgetActiveInHierarchy());
	TestEqual(TEXT("the closed notification reached the configured reopening policy"), Probe->ReopenAttempts, Parameters == TEXT("normal") ? 0 : 1);
	TestFalse(TEXT("the inactive anchor is closed when deactivation returns"), Anchor->IsOpen());
	TestFalse(TEXT("its popup is absent from the layer immediately"), Layer->IsOpen(Popup));
	TestFalse(TEXT("its popup is asleep immediately"), Popup->GetWidgetActive());
	TestTrue(TEXT("the popup returned home under the inactive anchor"), Popup->GetParent() == Anchor);

	// The layer sweeps unusable openers every layout pass. Reopening from each dismissal must not
	// create a loop that leaves a live popup answering for an inactive screen after every frame.
	Rig.PumpFrames(2);
	TestFalse(TEXT("the inactive screen stays closed across layout sweeps"), Anchor->IsOpen());
	TestFalse(TEXT("no lifted popup survives the layout sweeps"), Layer->IsOpen(Popup));
	TestFalse(TEXT("no active popup survives the layout sweeps"), Popup->GetWidgetActiveInHierarchy());
	TestTrue(TEXT("the popup remains parented to its own inactive anchor"), Popup->GetParent() == Anchor);
	TestEqual(TEXT("putting the screen away publishes one open and one close"), Probe->OpenChanges.Num(), 2);
	TArray<UDreamWidget*> OpenPopups;
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the player's popup stack is empty"), OpenPopups.Num(), 0);

	Probe->bReopenWhenClosed = false;
	Anchor->Close();
	Deactivated->SetWidgetActive(true);
	Rig.PumpFrames(2);
	Anchor->Open(false);
	TestTrue(TEXT("the restored screen can open the same popup normally"),
		Anchor->IsOpen() && Layer->IsOpen(Popup) && Popup->GetParent() != Anchor && Popup->GetWidgetActiveInHierarchy());
	Anchor->Close();
	TestFalse(TEXT("the restored screen can also close it normally"), Anchor->IsOpen() || Layer->IsOpen(Popup));
	TestTrue(TEXT("the restored popup returns home again"), Popup->GetParent() == Anchor);
	return true;
}

#endif
