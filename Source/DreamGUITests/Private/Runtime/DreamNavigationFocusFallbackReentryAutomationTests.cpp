// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamFocusReentryTestTypes.h"
#include "Controls/DreamExpandableArea.h"
#include "Core/DreamUIInputServices.h"
#include "Driver/DreamDriverRig.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UISelectable.h"

namespace DreamNavigationFocusFallbackTestLocal
{
	UDreamWidget* MakeSelectable(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent = nullptr)
	{
		UDreamWidget* Widget = InRig.MakeWidget(InName, InParent, FVector2D(140.0, 50.0));
		if (Widget != nullptr)
		{
			Widget->SetIsFocusable(true);
			Widget->AddComponent<UUISelectable>();
		}
		return Widget;
	}

	UDreamUINavigationScope* MakeManualScope(UDreamWidget* InWidget)
	{
		UDreamUINavigationScope* Scope = InWidget != nullptr ? InWidget->AddComponent<UDreamUINavigationScope>() : nullptr;
		if (Scope != nullptr)
		{
			Scope->SetActivateWhenEnabled(false);
			Scope->DeactivateScope();
		}
		return Scope;
	}

	UDreamFocusReentryProbe* AddReceivedProbe(UDreamWidget* InWidget)
	{
		UDreamFocusReentryProbe* Probe = InWidget != nullptr ? InWidget->AddComponent<UDreamFocusReentryProbe>() : nullptr;
		if (Probe != nullptr)
		{
			Probe->Trigger = EDreamFocusReentryCallback::Received;
			InWidget->OnFocusReceived.AddDynamic(Probe, &UDreamFocusReentryProbe::OnReceived);
		}
		return Probe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPopupInitialFocusRedirectTest,
	"DreamGUI.Focus.APopupInitialFocusCallbackKeepsItsExplicitDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPopupInitialFocusRedirectTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationFocusFallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real popup rig came up"), Rig.IsUsable()))return false;
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Rig.GetWorld());
	UDreamWidget* Popup = Rig.MakeWidget(TEXT("InitialFocusPopup"), nullptr, FVector2D(400.0, 200.0));
	UDreamWidget* First = MakeSelectable(Rig, TEXT("OrdinaryFallback"), Popup);
	UDreamWidget* Initial = MakeSelectable(Rig, TEXT("AuthoredInitialFocus"), Popup);
	UDreamWidget* Outside = MakeSelectable(Rig, TEXT("ExplicitOutside"));
	UDreamFocusReentryProbe* Probe = AddReceivedProbe(Initial);
	if (!TestTrue(TEXT("the layer, services and real focus candidates exist"),
		Services != nullptr && Layer != nullptr && Popup != nullptr && First != nullptr && Initial != nullptr && Outside != nullptr && Probe != nullptr))return false;
	Rig.PumpFrames(2);
	bool bRedirectSucceeded = false;
	Probe->Action = [Services, Outside, &bRedirectSucceeded]() { bRedirectSucceeded = Services->FocusForNavigation(Outside, 0); };
	FDreamPopupParams Params;
	Params.Popup = Popup;
	Params.InitialFocus = Initial;
	TestTrue(TEXT("the popup remains open after its focus callback"), Layer->Push(Params));
	TestEqual(TEXT("the initial focus callback acted once"), Probe->MutationCount, 1);
	TestTrue(TEXT("the callback's outside choice actually succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the first navigable fallback does not steal the explicit choice"), Services->GetFocusedWidget(0) == Outside);
	Layer->Dismiss(Popup);

	// An actually unavailable initial target still falls through to the first navigable child.
	Initial->SetWidgetActive(false);
	TestTrue(TEXT("the popup can open with an unavailable initial target"), Layer->Push(Params));
	TestTrue(TEXT("an ordinary refusal still selects the first usable child"), Services->GetFocusedWidget(0) == First);
	Layer->Dismiss(Popup);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamScopePopFocusRedirectTest,
	"DreamGUI.Focus.AScopePopReturnCallbackKeepsItsExplicitDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScopePopFocusRedirectTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationFocusFallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real scope rig came up"), Rig.IsUsable()))return false;
	UDreamWidget* Page = Rig.MakeWidget(TEXT("UnderlyingPage"), nullptr, FVector2D(500.0, 300.0));
	UDreamWidget* Preferred = MakeSelectable(Rig, TEXT("PreferredReturn"), Page);
	UDreamWidget* Previous = MakeSelectable(Rig, TEXT("CapturedBeforeDialog"), Page);
	UDreamWidget* Dialog = Rig.MakeWidget(TEXT("DialogPage"), nullptr, FVector2D(300.0, 200.0));
	UDreamWidget* DialogFocus = MakeSelectable(Rig, TEXT("DialogFocus"), Dialog);
	UDreamWidget* Outside = MakeSelectable(Rig, TEXT("ExplicitScopeDestination"));
	UDreamUINavigationScope* PageScope = MakeManualScope(Page);
	UDreamUINavigationScope* DialogScope = MakeManualScope(Dialog);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("the actual scopes and targets exist"), Services != nullptr && PageScope != nullptr && DialogScope != nullptr
		&& Preferred != nullptr && Previous != nullptr && DialogFocus != nullptr && Outside != nullptr))return false;
	PageScope->SetRestoreLastFocus(false);
	PageScope->SetDesiredFocusTarget(Preferred->GetComponent<UUISelectable>());
	DialogScope->SetDesiredFocusTarget(DialogFocus->GetComponent<UUISelectable>());
	Rig.PumpFrames(2);
	PageScope->ActivateScope();
	if (!TestTrue(TEXT("the underlying page initially focuses its desired target"), Services->GetFocusedWidget(0) == Preferred)
		|| !TestTrue(TEXT("the player then picks a different control"), Services->FocusForNavigation(Previous, 0)))return false;
	DialogScope->ActivateScope();
	if (!TestTrue(TEXT("the actual dialog scope took focus"), Services->GetFocusedWidget(0) == DialogFocus))return false;
	UDreamFocusReentryProbe* Probe = AddReceivedProbe(Preferred);
	if (!TestNotNull(TEXT("the preferred target has its real listener"), Probe))return false;
	bool bRedirectSucceeded = false;
	Probe->Action = [Services, Outside, &bRedirectSucceeded]() { bRedirectSucceeded = Services->FocusForNavigation(Outside, 0); };
	DialogScope->DeactivateScope();
	TestEqual(TEXT("the returned focus ran its callback once"), Probe->MutationCount, 1);
	TestTrue(TEXT("the callback's explicit choice succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the before-push fallback does not overwrite the new choice"), Services->GetFocusedWidget(0) == Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCollapsingBodyFocusRedirectTest,
	"DreamGUI.Focus.ACollapsingBodyReturnCallbackKeepsItsExplicitDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCollapsingBodyFocusRedirectTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationFocusFallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the actual collapsing-body rig came up"), Rig.IsUsable()))return false;
	UDreamWidget* Page = Rig.MakeWidget(TEXT("ScopedPage"), nullptr, FVector2D(600.0, 400.0));
	UDreamWidget* ScopeTarget = MakeSelectable(Rig, TEXT("ScopeFallback"), Page);
	UDreamUINavigationScope* Scope = MakeManualScope(Page);
	UDreamExpandableArea* Area = Rig.MakeControl<UDreamExpandableArea>(TEXT("Details"), Page, FVector2D(300.0, 200.0));
	UDreamWidget* BodyTarget = Area != nullptr ? MakeSelectable(Rig, TEXT("BodyFocus"), Area->ContentNode.Get()) : nullptr;
	UDreamWidget* Outside = MakeSelectable(Rig, TEXT("ExplicitCollapseDestination"));
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("the scope, expandable area and real targets exist"), Services != nullptr && Scope != nullptr && ScopeTarget != nullptr
		&& Area != nullptr && Area->HeaderNode != nullptr && BodyTarget != nullptr && Outside != nullptr))return false;
	Scope->SetRestoreLastFocus(false);
	Scope->SetDesiredFocusTarget(ScopeTarget->GetComponent<UUISelectable>());
	Area->SetExpansionDuration(0.0f);
	Area->SetIsExpanded(true);
	Rig.PumpFrames(2);
	Scope->ActivateScope();
	if (!TestTrue(TEXT("the actual body takes focus before collapse"), Services->FocusForNavigation(BodyTarget, 0)))return false;
	UDreamFocusReentryProbe* Probe = AddReceivedProbe(Area->HeaderNode.Get());
	if (!TestNotNull(TEXT("the header has a real focus callback"), Probe))return false;
	bool bRedirectSucceeded = false;
	Probe->Action = [Services, Outside, &bRedirectSucceeded]() { bRedirectSucceeded = Services->FocusForNavigation(Outside, 0); };
	Area->SetIsExpanded(false);
	TestFalse(TEXT("the section actually collapsed"), Area->GetIsExpanded());
	TestEqual(TEXT("returning focus to the header ran its callback once"), Probe->MutationCount, 1);
	TestTrue(TEXT("the header callback's outside choice succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the scope fallback does not overwrite the explicit choice"), Services->GetFocusedWidget(0) == Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWakingScopeFocusRedirectTest,
	"DreamGUI.Focus.AWakingScopeRememberedFocusCallbackKeepsItsExplicitDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWakingScopeFocusRedirectTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationFocusFallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the actual waking-scope rig came up"), Rig.IsUsable()))return false;
	UDreamWidget* Page = Rig.MakeWidget(TEXT("WakingPage"), nullptr, FVector2D(500.0, 300.0));
	UDreamWidget* Remembered = MakeSelectable(Rig, TEXT("Remembered"), Page);
	UDreamWidget* Desired = MakeSelectable(Rig, TEXT("DesiredFallback"), Page);
	UDreamWidget* Outside = MakeSelectable(Rig, TEXT("ExplicitWakeDestination"));
	UDreamUINavigationScope* Scope = MakeManualScope(Page);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("the scope and actual focus targets exist"), Services != nullptr && Page != nullptr && Scope != nullptr
		&& Remembered != nullptr && Desired != nullptr && Outside != nullptr))return false;
	Scope->SetDesiredFocusTarget(Desired->GetComponent<UUISelectable>());
	Scope->RememberFocus(Remembered->GetComponent<UUISelectable>());
	Page->SetWidgetActive(false);
	Scope->SetActivateWhenEnabled(true);
	Rig.PumpFrames(1);
	UDreamFocusReentryProbe* Probe = AddReceivedProbe(Remembered);
	if (!TestNotNull(TEXT("the remembered target has a focus listener"), Probe))return false;
	bool bRedirectSucceeded = false;
	bool bRanDuringWake = false;
	Probe->Action = [Services, Outside, Remembered, Scope, &bRedirectSucceeded, &bRanDuringWake]()
	{
		bRanDuringWake = !Remembered->GetWidgetActiveInHierarchy() && Scope->ResolveFocusTarget() == nullptr;
		bRedirectSucceeded = Services->FocusForNavigation(Outside, 0);
	};
	Page->SetWidgetActive(true);
	TestEqual(TEXT("waking the page focused its remembered target once"), Probe->MutationCount, 1);
	TestTrue(TEXT("the callback ran on the real waking fallback path"), bRanDuringWake);
	TestTrue(TEXT("the remembered target's explicit choice succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the authored fallback does not overwrite that choice"), Services->GetFocusedWidget(0) == Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFocusRevisionPlayerIdentityTest,
	"DreamGUI.Focus.ReplacingAPlayerAtTheSameIndexChangesItsFocusRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFocusRevisionPlayerIdentityTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the actual player rig came up"), Rig.IsUsable()))return false;
	UDreamUIInputSubsystem* Input = Cast<UDreamUIInputSubsystem>(UDreamUIInputServices::Get(Rig.GetWorld()));
	if (!TestNotNull(TEXT("the actual input subsystem exists"), Input))return false;
	constexpr int32 UserIndex = 17;
	const FDreamUIFocusRevision Missing = Input->GetFocusRevision(UserIndex);
	TestNull(TEXT("reading a revision does not create a player"), Input->GetUser(UserIndex));
	if (!TestNotNull(TEXT("the first real script player is created"), Input->GetOrCreateUser(UserIndex)))return false;
	const FDreamUIFocusRevision First = Input->GetFocusRevision(UserIndex);
	Input->RemoveUser(UserIndex);
	TestTrue(TEXT("removing a player changes its revision even without focus"), Input->GetFocusRevision(UserIndex) != First);
	if (!TestNotNull(TEXT("a replacement player can use the same index"), Input->GetOrCreateUser(UserIndex)))return false;
	const FDreamUIFocusRevision Replacement = Input->GetFocusRevision(UserIndex);
	TestEqual(TEXT("both players start with the same transition count"), First.Serial, Replacement.Serial);
	TestTrue(TEXT("their distinct identities keep their focus revisions different"), First != Replacement);
	TestTrue(TEXT("an existing unfocused player differs from an absent one"), Replacement != Missing);
	Input->RemoveUser(UserIndex);
	return true;
}

#endif
