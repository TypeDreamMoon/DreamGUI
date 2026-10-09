// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamMenuAnchorProviderReentryTestTypes.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamMenuAnchorProviderReentryTestLocal
{
	bool RunCase(FAutomationTestBase& InTest, bool bInReturnContent)
	{
		DreamTests::FScopedGameWorld TestWorld;
		TDreamTestControl<UDreamWidget> Root(NewObject<UDreamWidget>(TestWorld.World));
		Root->SetWidth(1000.0f);
		Root->SetHeight(600.0f);
		Root->OnRegister();
		// A registered hierarchy without a screen root uses the anchor's documented in-place
		// popup path. No input player or operating-system focus is needed to cancel its provider.
		UDreamMenuAnchor* Anchor = NewObject<UDreamMenuAnchor>(TestWorld.World);
		Anchor->StyleSource = EDreamUIStyleSource::Inline;
		Anchor->SetWidth(160.0f);
		Anchor->SetHeight(40.0f);
		Anchor->Initialize();
		Anchor->OnRegister();
		if (!InTest.TestTrue(TEXT("the initialized anchor belongs to the registered root"), Anchor->TrySetParent(Root.Get(), false)))return false;
		UDreamWidget* Popup = Anchor->FindPart(TEXT("Popup"));
		if (!InTest.TestNotNull(TEXT("the anchor realized a popup"), Popup))return false;
		InTest.TestFalse(TEXT("a new anchor starts with its popup asleep"), Popup->GetWidgetActive());

		UDreamWidget* Content = NewObject<UDreamWidget>(TestWorld.World);
		Content->SetWidth(120.0f);
		Content->SetHeight(80.0f);
		Content->OnRegister();
		if (!InTest.TestTrue(TEXT("the provider's real content is owned by the same registered hierarchy"), Content->TrySetParent(Root.Get(), false)))return false;
		TStrongObjectPtr<UDreamMenuAnchorProviderReentryProbe> Probe(NewObject<UDreamMenuAnchorProviderReentryProbe>(TestWorld.World));
		Probe->Anchor = Anchor;
		Probe->Content = Content;
		Probe->bReturnContentOnFirstCall = bInReturnContent;
		Anchor->OnGetUserMenuContentEvent.BindDynamic(Probe.Get(), &UDreamMenuAnchorProviderReentryProbe::ProvideContent);
		Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamMenuAnchorProviderReentryProbe::RecordOpenChanged);

		Anchor->Open(false);
		bool bValid = InTest.TestEqual(TEXT("the first open called its provider once"), Probe->ProviderCalls, 1);
		bValid = InTest.TestEqual(TEXT("the provider actually closed the opening menu"), Probe->CloseCalls, 1) && bValid;
		bValid = InTest.TestFalse(TEXT("the provider's close owns the final open state"), Anchor->IsOpen()) && bValid;
		bValid = InTest.TestFalse(TEXT("the canceled open must not wake its popup after the provider returns"), Popup->GetWidgetActive()) && bValid;
		bValid = InTest.TestEqual(TEXT("an open canceled before completion announced neither open nor close"), Probe->OpenChanges.Num(), 0) && bValid;
		Anchor->Close();
		bValid = InTest.TestFalse(TEXT("repeating Close keeps the canceled popup asleep"), Popup->GetWidgetActive()) && bValid;

		// The first callback acts once. A fresh open may reuse valid provided content or ask the
		// provider again: either path must open a working menu after the canceled attempt.
		Anchor->Open(false);
		bValid = InTest.TestTrue(TEXT("a later fresh open succeeds"), Anchor->IsOpen()) && bValid;
		bValid = InTest.TestTrue(TEXT("the fresh open wakes its popup"), Popup->GetWidgetActive()) && bValid;
		bValid = InTest.TestTrue(TEXT("the supplied content is alive and adopted into the menu"),
			IsValid(Content) && Content->GetParent() == Anchor->MenuNode.Get()) && bValid;
		bValid = InTest.TestEqual(TEXT("only the fresh open is announced"), Probe->OpenChanges.Num(), 1) && bValid;
		if (Probe->OpenChanges.Num() == 1)bValid = InTest.TestTrue(TEXT("that announcement says open"), Probe->OpenChanges[0]) && bValid;
		bValid = InTest.TestEqual(TEXT("the provider did not close another activation"), Probe->CloseCalls, 1) && bValid;
		Anchor->Close();
		bValid = InTest.TestFalse(TEXT("the fresh menu closes normally"), Anchor->IsOpen()) && bValid;
		bValid = InTest.TestFalse(TEXT("closing the fresh menu sleeps its popup"), Popup->GetWidgetActive()) && bValid;
		bValid = InTest.TestEqual(TEXT("the successful open has one matching close"), Probe->OpenChanges.Num(), 2) && bValid;
		if (Probe->OpenChanges.Num() == 2)bValid = InTest.TestFalse(TEXT("the matching announcement says closed"), Probe->OpenChanges[1]) && bValid;
		return bValid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorNullProviderCloseReentryTest,
	"DreamGUI.MenuAnchor.AContentProviderReturningNothingCanCancelItsOpeningMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorNullProviderCloseReentryTest::RunTest(const FString& Parameters)
{
	return DreamMenuAnchorProviderReentryTestLocal::RunCase(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMenuAnchorWidgetProviderCloseReentryTest,
	"DreamGUI.MenuAnchor.AContentProviderReturningAWidgetCannotReopenTheMenuItClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorWidgetProviderCloseReentryTest::RunTest(const FString& Parameters)
{
	return DreamMenuAnchorProviderReentryTestLocal::RunCase(*this, true);
}

#endif
