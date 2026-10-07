// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamMenuAnchorOperationReentryTestTypes.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
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

#endif
