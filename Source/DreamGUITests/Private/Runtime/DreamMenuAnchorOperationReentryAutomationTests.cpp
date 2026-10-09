// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "DreamControlTestScope.h"
#include "DreamFocusReentryTestTypes.h"
#include "DreamMenuAnchorOperationReentryTestTypes.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamMenuAnchorOperationReentryTestLocal
{
	UDreamMenuAnchor* MakeAnchor(FAutomationTestBase& InTest, UWorld* InWorld, UDreamWidget* InRoot)
	{
		InRoot->SetWidth(1000.0f);
		InRoot->SetHeight(600.0f);
		InRoot->OnRegister();
		UDreamMenuAnchor* Anchor = NewObject<UDreamMenuAnchor>(InWorld);
		Anchor->StyleSource = EDreamUIStyleSource::Inline;
		Anchor->SetWidth(160.0f);
		Anchor->SetHeight(40.0f);
		Anchor->Initialize();
		Anchor->OnRegister();
		if (!InTest.TestTrue(TEXT("the initialized anchor belongs to its registered root"), Anchor->TrySetParent(InRoot, false)))return nullptr;
		if (!InTest.TestNotNull(TEXT("the anchor realized its popup"), Anchor->PopupNode.Get()))return nullptr;
		return Anchor;
	}

	UDreamWidget* MakeContent(UWorld* InWorld, UDreamWidget* InRoot)
	{
		UDreamWidget* Content = NewObject<UDreamWidget>(InWorld);
		Content->SetWidth(120.0f);
		Content->SetHeight(80.0f);
		Content->OnRegister();
		Content->TrySetParent(InRoot, false);
		return Content;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorProviderNestedOpenTest,
	"DreamGUI.MenuAnchor.AContentProviderReopeningTheAnchorKeepsItsNewContentAndAnnouncesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorProviderNestedOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorOperationReentryTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TDreamTestControl<UDreamWidget> Root(NewObject<UDreamWidget>(TestWorld.World));
	UDreamMenuAnchor* Anchor = MakeAnchor(*this, TestWorld.World, Root.Get());
	if (Anchor == nullptr)return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(TestWorld.World));
	Probe->Anchor = Anchor;
	Probe->FirstContent = MakeContent(TestWorld.World, Root.Get());
	Probe->NestedContent = MakeContent(TestWorld.World, Root.Get());
	Anchor->OnGetUserMenuContentEvent.BindDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::ProvideContent);
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	Anchor->Open(false);
	TestEqual(TEXT("the callback really closed and reopened through a second provider call"), Probe->ProviderCalls, 2);
	TestTrue(TEXT("the nested open owns the final state"), Anchor->IsOpen());
	TestTrue(TEXT("its popup remains awake"), Anchor->PopupNode->GetWidgetActive());
	TestTrue(TEXT("the nested provider's content is still the adopted content"),
		Anchor->ProvidedMenuContent == Probe->NestedContent && Probe->NestedContent->GetParent() == Anchor->MenuNode);
	TestTrue(TEXT("the stale outer answer was not adopted over the new menu"), Probe->FirstContent->GetParent() == Root.Get());
	TestEqual(TEXT("only the surviving nested open was announced"), Probe->OpenChanges.Num(), 1);
	Anchor->Close();
	TestFalse(TEXT("the surviving menu closes normally"), Anchor->IsOpen());
	TestFalse(TEXT("its popup goes to sleep"), Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("one open has one matching close"), Probe->OpenChanges.Num(), 2);
	if (Probe->OpenChanges.Num() == 2)
	{
		TestTrue(TEXT("first announced open"), Probe->OpenChanges[0]);
		TestFalse(TEXT("then announced closed"), Probe->OpenChanges[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorProviderDestroysAnchorTest,
	"DreamGUI.MenuAnchor.AContentProviderCanDestroyAndCollectItsAnchorWithoutTheOldOpenResuming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorProviderDestroysAnchorTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorOperationReentryTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TDreamTestControl<UDreamWidget> Root(NewObject<UDreamWidget>(TestWorld.World));
	UDreamMenuAnchor* Anchor = MakeAnchor(*this, TestWorld.World, Root.Get());
	if (Anchor == nullptr)return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(TestWorld.World));
	Probe->Anchor = Anchor;
	Probe->bDestroyInProvider = true;
	const TWeakObjectPtr<UDreamMenuAnchor> DestroyedAnchor(Anchor);
	const TWeakObjectPtr<UDreamWidget> DestroyedPopup(Anchor->PopupNode);
	Anchor->OnGetUserMenuContentEvent.BindDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::ProvideContent);
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	Anchor->Open(false);
	TestEqual(TEXT("the provider actually ran once"), Probe->ProviderCalls, 1);
	TestTrue(TEXT("the provider collected the destroyed anchor before returning"), Probe->bCollectedDestroyedAnchor);
	TestNull(TEXT("the anchor was actually collected"), DestroyedAnchor.GetEvenIfUnreachable());
	TestNull(TEXT("its popup was actually collected"), DestroyedPopup.GetEvenIfUnreachable());
	TestEqual(TEXT("the destroyed opening menu announced no state change"), Probe->OpenChanges.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorWakeNestedOpenTest,
	"DreamGUI.MenuAnchor.APopupWakingCallbackCanCloseAndReopenWithoutTheOldOpenAnnouncingAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorWakeNestedOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorOperationReentryTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TDreamTestControl<UDreamWidget> Root(NewObject<UDreamWidget>(TestWorld.World));
	UDreamMenuAnchor* Anchor = MakeAnchor(*this, TestWorld.World, Root.Get());
	if (Anchor == nullptr)return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(TestWorld.World));
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	int32 ReopenCount = 0;
	const FDelegateHandle Handler = Anchor->PopupNode->GetWidgetActiveChangedEvent().AddLambda([Anchor, &ReopenCount](bool bActive)
	{
		if (bActive && ReopenCount == 0)
		{
			++ReopenCount;
			Anchor->Close();
			Anchor->Open(false);
		}
	});
	Anchor->Open(false);
	Anchor->PopupNode->GetWidgetActiveChangedEvent().Remove(Handler);
	TestEqual(TEXT("the popup waking callback actually reopened once"), ReopenCount, 1);
	TestTrue(TEXT("the newest open survives"), Anchor->IsOpen());
	TestTrue(TEXT("the newest open keeps its popup awake"), Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("only the surviving open was announced"), Probe->OpenChanges.Num(), 1);
	Anchor->Close();
	TestEqual(TEXT("it then announces one matching close"), Probe->OpenChanges.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorSleepNestedOpenTest,
	"DreamGUI.MenuAnchor.APopupSleepingCallbackReopeningItsAnchorKeepsTheNewMenuAwake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorSleepNestedOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorOperationReentryTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	TDreamTestControl<UDreamWidget> Root(NewObject<UDreamWidget>(TestWorld.World));
	UDreamMenuAnchor* Anchor = MakeAnchor(*this, TestWorld.World, Root.Get());
	if (Anchor == nullptr)return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(TestWorld.World));
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	Anchor->Open(false);
	int32 ReopenCount = 0;
	const FDelegateHandle Handler = Anchor->PopupNode->GetWidgetActiveChangedEvent().AddLambda([Anchor, &ReopenCount](bool bActive)
	{
		if (!bActive && ReopenCount == 0)
		{
			++ReopenCount;
			Anchor->Open(false);
		}
	});
	Anchor->Close();
	Anchor->PopupNode->GetWidgetActiveChangedEvent().Remove(Handler);
	TestEqual(TEXT("the popup sleeping callback actually reopened once"), ReopenCount, 1);
	TestTrue(TEXT("the interrupted close leaves the new open in charge"), Anchor->IsOpen());
	TestTrue(TEXT("the new menu stays awake"), Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("the menu never completed a closed state to announce"), Probe->OpenChanges.Num(), 1);
	Anchor->Close();
	TestFalse(TEXT("a later close completes normally"), Anchor->IsOpen());
	TestFalse(TEXT("that close sleeps the popup"), Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("the completed close matches the original open"), Probe->OpenChanges.Num(), 2);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamMenuAnchorFocusReturnReopenTest,
	"DreamGUI.MenuAnchor.ReturningFocusCanReopenTheSamePopupWithoutTheOldDismissalTakingItHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamMenuAnchorFocusReturnReopenTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Anchor Close"));
	OutTestCommands.Add(TEXT("anchor"));
	OutBeautifiedNames.Add(TEXT("Popup layer dismissal"));
	OutTestCommands.Add(TEXT("layer"));
}

bool FDreamMenuAnchorFocusReturnReopenTest::RunTest(const FString& Parameters)
{
	const bool bDismissThroughLayer = Parameters == TEXT("layer");
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the popup reentry rig came up"), Rig.IsUsable()))return false;
	UDreamMenuAnchor* Anchor = Rig.MakeControl<UDreamMenuAnchor>(TEXT("ReopeningAnchor"), nullptr,
		FVector2D(160.0, 40.0), FVector2D(-300.0, 150.0));
	UDreamWidget* ReturnFocus = Rig.MakeWidget(TEXT("ReturnFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(250.0, 150.0));
	if (!TestNotNull(TEXT("the anchor was created"), Anchor) || !TestNotNull(TEXT("the return-focus target was created"), ReturnFocus))return false;
	UDreamWidget* MenuItem = Rig.MakeWidget(TEXT("MenuFocus"), Anchor->MenuNode, FVector2D(150.0, 50.0));
	if (!TestNotNull(TEXT("the menu's focus target was created"), MenuItem))return false;
	ReturnFocus->SetIsFocusable(true);
	MenuItem->SetIsFocusable(true);
	UDreamFocusReentryProbe* ReturnProbe = ReturnFocus->AddComponent<UDreamFocusReentryProbe>();
	if (!TestNotNull(TEXT("the returning focus has a real select callback"), ReturnProbe))return false;
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Anchor);
	if (!TestNotNull(TEXT("the world has its real popup layer"), Layer))return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(Rig.GetWorld()));
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("the eventual return target initially has focus"), ReturnFocus->SetFocus()))return false;
	Anchor->Open(false);
	UDreamWidget* Popup = Anchor->PopupNode;
	UDreamWidget* LiftedParent = Popup->GetParent();
	if (!TestTrue(TEXT("the opening really pushed the popup and lifted it off the anchor"),
		Layer->IsOpen(Popup) && IsValid(LiftedParent) && LiftedParent != Anchor))return false;
	if (!TestTrue(TEXT("focus moved into the open menu"), MenuItem->SetFocus()))return false;
	ReturnProbe->Trigger = EDreamFocusReentryCallback::Select;
	ReturnProbe->Action = [Anchor]()
	{
		Anchor->Close();
		Anchor->Open(false);
	};
	if (bDismissThroughLayer)Layer->Dismiss(Popup, EDreamPopupDismissReason::Explicit);
	else Anchor->Close();
	TestEqual(TEXT("returning focus actually reopened the anchor once"), ReturnProbe->MutationCount, 1);
	TestTrue(TEXT("the new menu owns the final open state"), Anchor->IsOpen());
	TestTrue(TEXT("the new menu keeps its popup awake"), Popup->GetWidgetActive());
	TestTrue(TEXT("the old dismissal did not restore the new popup home"), Popup->GetParent() == LiftedParent);
	TestTrue(TEXT("the new menu is still registered on the popup layer"), Layer->IsOpen(Popup));
	TestTrue(TEXT("it is the player's top popup"), Layer->GetTopPopup(0) == Popup);
	TArray<UDreamWidget*> OpenPopups;
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the popup stack has exactly the surviving new entry"), OpenPopups.Num(), 1);
	if (OpenPopups.Num() == 1)TestTrue(TEXT("that entry belongs to this same popup"), OpenPopups[0] == Popup);
	TestTrue(TEXT("the completed focus return keeps focus on its target"), ReturnFocus->HasFocus());
	TestEqual(TEXT("no stale dismissal announced a close after the new open"), Probe->OpenChanges.Num(), bDismissThroughLayer ? 3 : 1);
	Anchor->Close();
	TestFalse(TEXT("a fresh close completes normally"), Anchor->IsOpen());
	TestFalse(TEXT("the fresh close puts its popup asleep"), Popup->GetWidgetActive());
	TestTrue(TEXT("the fresh close finally restores the popup under its anchor"), Popup->GetParent() == Anchor);
	TestFalse(TEXT("the popup layer has no stale entry after the fresh close"), Layer->IsOpen(Popup));
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the player's popup stack is empty again"), OpenPopups.Num(), 0);
	TestEqual(TEXT("the fresh close announces exactly one close"), Probe->OpenChanges.Num(), bDismissThroughLayer ? 4 : 2);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamMenuAnchorInitialFocusReopenTest,
	"DreamGUI.MenuAnchor.InitialPopupFocusCanReopenTheSamePopupAndStillAcceptItsNextDismissal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamMenuAnchorInitialFocusReopenTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("Selecting the menu item"));
	OutTestCommands.Add(TEXT("select"));
	OutBeautifiedNames.Add(TEXT("Deselecting the previous focus"));
	OutTestCommands.Add(TEXT("deselect"));
}

bool FDreamMenuAnchorInitialFocusReopenTest::RunTest(const FString& Parameters)
{
	const bool bReopenFromDeselect = Parameters == TEXT("deselect");
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the initial-focus reentry rig came up"), Rig.IsUsable()))return false;
	UDreamMenuAnchor* Anchor = Rig.MakeControl<UDreamMenuAnchor>(TEXT("InitialFocusAnchor"), nullptr,
		FVector2D(160.0, 40.0), FVector2D(-300.0, 150.0));
	UDreamWidget* ReturnFocus = Rig.MakeWidget(TEXT("InitialReturnFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(250.0, 150.0));
	if (!TestNotNull(TEXT("the anchor was created"), Anchor) || !TestNotNull(TEXT("the previous focus was created"), ReturnFocus))return false;
	UDreamButton* MenuItem = Rig.MakeControl<UDreamButton>(TEXT("InitialMenuFocus"), Anchor->MenuNode, FVector2D(150.0, 50.0));
	if (!TestNotNull(TEXT("the menu has a real navigable button"), MenuItem))return false;
	ReturnFocus->SetIsFocusable(true);
	if (!TestNotNull(TEXT("the button owns its actual selectable face"), MenuItem->FaceNode.Get()))return false;
	UDreamWidget* CallbackWidget = bReopenFromDeselect ? ReturnFocus : MenuItem->FaceNode.Get();
	UDreamFocusReentryProbe* FocusProbe = CallbackWidget->AddComponent<UDreamFocusReentryProbe>();
	if (!TestNotNull(TEXT("the initial focus move has a real dispatched callback"), FocusProbe))return false;
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Anchor);
	if (!TestNotNull(TEXT("the world has its real popup layer"), Layer))return false;
	TStrongObjectPtr<UDreamMenuAnchorOperationReentryProbe> Probe(NewObject<UDreamMenuAnchorOperationReentryProbe>(Rig.GetWorld()));
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorOperationReentryProbe::RecordOpenChanged);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("the eventual return target initially has focus"), ReturnFocus->SetFocus()))return false;
	UDreamWidget* Popup = Anchor->PopupNode;
	bool bReopenedInsideInitialPush = false;
	FocusProbe->Trigger = bReopenFromDeselect ? EDreamFocusReentryCallback::Deselect : EDreamFocusReentryCallback::Select;
	FocusProbe->Action = [Anchor, Layer, Popup, &bReopenedInsideInitialPush, Probe = Probe.Get()]()
	{
		// The first push has inserted and elevated its popup, but Open has not announced it yet.
		bReopenedInsideInitialPush = Layer->IsOpen(Popup) && Popup->GetParent() != Anchor && Probe->OpenChanges.IsEmpty();
		Anchor->Close();
		Anchor->Open(false);
	};
	Anchor->Open(true);
	TestEqual(TEXT("the initial focus callback actually reopened once"), FocusProbe->MutationCount, 1);
	TestTrue(TEXT("the callback ran inside the very first popup push"), bReopenedInsideInitialPush);
	TestTrue(TEXT("the new menu owns the final open state"), Anchor->IsOpen());
	TestTrue(TEXT("the new menu keeps its popup awake"), Popup->GetWidgetActive());
	TestTrue(TEXT("the new menu remains lifted above its anchor"), IsValid(Popup->GetParent()) && Popup->GetParent() != Anchor);
	TestTrue(TEXT("the surviving popup remains registered on the layer"), Layer->IsOpen(Popup));
	TArray<UDreamWidget*> OpenPopups;
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the popup stack contains exactly the surviving open"), OpenPopups.Num(), 1);
	if (OpenPopups.Num() == 1)TestTrue(TEXT("the surviving entry owns this same popup"), OpenPopups[0] == Popup);
	TestEqual(TEXT("the surviving menu announced one open"), Probe->OpenChanges.Num(), 1);

	// Dismiss through the layer after the callback has returned: its stored callback must belong to the new open.
	Layer->Dismiss(Popup, EDreamPopupDismissReason::Explicit);
	TestFalse(TEXT("the next external dismissal closes the reopened anchor"), Anchor->IsOpen());
	TestFalse(TEXT("the next external dismissal puts its popup asleep"), Popup->GetWidgetActive());
	TestTrue(TEXT("the dismissed popup is restored under its anchor"), Popup->GetParent() == Anchor);
	TestFalse(TEXT("the dismissed popup has no remaining layer entry"), Layer->IsOpen(Popup));
	Layer->GetOpenPopups(0, OpenPopups);
	TestEqual(TEXT("the player's popup stack is empty after dismissal"), OpenPopups.Num(), 0);
	TestEqual(TEXT("the reopened menu announces one matching close"), Probe->OpenChanges.Num(), 2);
	if (Probe->OpenChanges.Num() == 2)
	{
		TestTrue(TEXT("the new open was announced first"), Probe->OpenChanges[0]);
		TestFalse(TEXT("the external dismissal then announced closed"), Probe->OpenChanges[1]);
	}
	return true;
}
#endif
