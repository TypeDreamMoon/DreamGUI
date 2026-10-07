// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamMenuAnchorOperationReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamMenuAnchorDestroyReentryTest,
	"DreamGUI.MenuAnchor.DestroyingAnOpenAnchorCannotReopenItsLiftedPopupFromTheClosedCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamMenuAnchorDestroyReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Destroying the anchor with a reopening listener"));
	OutTestCommands.Add(TEXT("self"));
	OutBeautifiedNames.Add(TEXT("Destroying its parent with a reopening listener"));
	OutTestCommands.Add(TEXT("parent"));
	OutBeautifiedNames.Add(TEXT("Destroying without a reopening listener still removes the popup"));
	OutTestCommands.Add(TEXT("normal"));
	OutBeautifiedNames.Add(TEXT("An ended lifetime stays closed and a later begin play can open again"));
	OutTestCommands.Add(TEXT("endplay"));
}

bool FDreamMenuAnchorDestroyReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real popup rig is usable"), Rig.IsUsable()))return false;
	UDreamWidget* Screen = Rig.MakeWidget(TEXT("DestroyedMenuScreen"), nullptr, FVector2D(600.0, 400.0));
	UDreamMenuAnchor* Anchor = Rig.MakeControl<UDreamMenuAnchor>(TEXT("DestroyedMenuAnchor"), Screen,
		FVector2D(160.0, 40.0), FVector2D(-150.0, 80.0));
	if (!TestNotNull(TEXT("the screen exists"), Screen) || !TestNotNull(TEXT("the anchor is realized"), Anchor))return false;
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Anchor);
	UDreamWidget* Popup = Anchor->PopupNode;
	if (!TestNotNull(TEXT("the game world's popup layer exists"), Layer) || !TestNotNull(TEXT("the anchor has its popup"), Popup))return false;
	const TWeakObjectPtr<UDreamWidget> WeakPopup(Popup);
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(Rig.GetWorld()));
	Probe->Anchor = Anchor;
	Probe->bReopenWhenClosed = Parameters != TEXT("normal");
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	ON_SCOPE_EXIT
	{
		Probe->bReopenWhenClosed = false;
		// A failing implementation can let the popup escape its destroyed owner. The test still
		// owns that orphan and must dispose of it instead of leaking it into a later test's GC.
		if (UDreamWidget* Remaining = WeakPopup.Get())
		{
			if (Layer->IsOpen(Remaining))Layer->Dismiss(Remaining, EDreamPopupDismissReason::Explicit);
			if (IsValid(Remaining))Remaining->DestroyWidget();
		}
	};
	Rig.PumpFrames(2);
	Anchor->Open(false);
	if (!TestTrue(TEXT("the opening really lifted the active popup onto the screen layer"),
		Anchor->IsOpen() && Layer->IsOpen(Popup) && Popup->GetParent() != Anchor && Popup->GetWidgetActiveInHierarchy()))return false;

	if (Parameters == TEXT("endplay"))
	{
		Anchor->EndPlay();
		TestTrue(TEXT("EndPlay leaves the registered anchor alive"), IsValid(Anchor) && Anchor->HasRegistered());
		TestFalse(TEXT("the ended lifetime is no longer playing"), Anchor->HasBegunPlay());
		TestTrue(TEXT("EndPlay does not disguise the bug by changing hierarchy visibility"), Anchor->GetWidgetActiveInHierarchy());
		TestEqual(TEXT("EndPlay announces one close to the reopening listener"), Probe->ReopenAttempts, 1);
		TestFalse(TEXT("the ending lifetime cannot reopen the lifted popup"), Anchor->IsOpen() || Layer->IsOpen(Popup));
		TestTrue(TEXT("the still-live popup returns home"), WeakPopup.IsValid() && Popup->GetParent() == Anchor);
		Anchor->Open(false);
		TestFalse(TEXT("an ended lifetime also rejects an explicit late open"), Anchor->IsOpen());
		Probe->bReopenWhenClosed = false;
		Anchor->BeginPlay();
		Anchor->Open(false);
		TestTrue(TEXT("the new lifetime may open that same popup normally"), Anchor->IsOpen() && Layer->IsOpen(Popup));
		Anchor->Close();
		TestFalse(TEXT("the new lifetime may close normally too"), Anchor->IsOpen() || Layer->IsOpen(Popup));
		return true;
	}

	UDreamWidget* Destroyed = Parameters == TEXT("parent") ? Screen : Anchor;
	Destroyed->DestroyWidget();
	TestFalse(TEXT("the selected owner really was destroyed"), IsValid(Destroyed));
	TestFalse(TEXT("the anchor was destroyed with that owner"), IsValid(Anchor));
	TestEqual(TEXT("one close reaches the configured reopening policy"), Probe->ReopenAttempts, Parameters == TEXT("normal") ? 0 : 1);
	TestFalse(TEXT("the popup was destroyed with its owner instead of escaping through a reopen"), WeakPopup.IsValid());
	TArray<UDreamWidget*> OpenPopups;
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("destruction immediately empties the player's popup stack"), OpenPopups.Num(), 0);
	TestEqual(TEXT("destruction publishes just the original open and its close"), Probe->OpenChanges.Num(), 2);

	Rig.PumpFrames(2);
	TestFalse(TEXT("no live orphan popup survives the following layout sweeps"), WeakPopup.IsValid());
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the popup stack remains empty"), OpenPopups.Num(), 0);
	return true;
}

#endif
