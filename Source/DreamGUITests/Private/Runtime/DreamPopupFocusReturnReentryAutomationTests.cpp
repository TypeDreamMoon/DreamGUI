// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamFocusReentryTestTypes.h"
#include "Core/DreamUIInputServices.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamUIPopupLayer.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamPopupFocusReturnRedirectTest,
	"DreamGUI.Focus.APopupDismissKeepsTheFocusChosenByItsReturnCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamPopupFocusReturnRedirectTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands)const
{
	OutBeautifiedNames.Add(TEXT("The callback redirects focus"));
	OutTestCommands.Add(TEXT("redirect"));
	OutBeautifiedNames.Add(TEXT("The callback clears focus"));
	OutTestCommands.Add(TEXT("clear"));
	OutBeautifiedNames.Add(TEXT("The callback leaves and returns to the opener"));
	OutTestCommands.Add(TEXT("aba"));
}

bool FDreamPopupFocusReturnRedirectTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the popup's actual game-world rig came up"), Rig.IsUsable()))return false;
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(Rig.GetWorld());
	UDreamWidget* Previous = Rig.MakeWidget(TEXT("CapturedPrevious"), nullptr, FVector2D(140.0, 50.0), FVector2D(-450.0, 0.0));
	UDreamWidget* Opener = Rig.MakeWidget(TEXT("PopupOpener"), nullptr, FVector2D(140.0, 50.0), FVector2D(-150.0, 0.0));
	UDreamWidget* Popup = Rig.MakeWidget(TEXT("PopupContent"), nullptr, FVector2D(140.0, 50.0), FVector2D(150.0, 0.0));
	UDreamWidget* Outside = Rig.MakeWidget(TEXT("CallbackDestination"), nullptr, FVector2D(140.0, 50.0), FVector2D(450.0, 0.0));
	if (!TestTrue(TEXT("the actual popup layer, focus service and widgets exist"),
		Services != nullptr && Layer != nullptr && Previous != nullptr && Opener != nullptr && Popup != nullptr && Outside != nullptr))return false;
	Previous->SetIsFocusable(true);
	Popup->SetIsFocusable(true);
	Outside->SetIsFocusable(true);
	Opener->SetIsFocusable(false);
	Rig.PumpFrames(2);
	FDreamPopupParams Params;
	Params.Popup = Popup;
	Params.Opener = Opener;
	Params.InitialFocus = Popup;

	// A plain non-focusable opener is a legitimate refusal: the captured focus remains
	// the next usable choice. Keep that fallback working while guarding dispatched callbacks.
	if (!TestTrue(TEXT("the previous widget takes focus before the first open"), Services->FocusForNavigation(Previous, 0))
		|| !TestTrue(TEXT("the first popup is pushed through the real layer"), Layer->Push(Params))
		|| !TestTrue(TEXT("the first popup actually holds the player's focus"), Services->GetFocusedWidget(0) == Popup))return false;
	Layer->Dismiss(Popup);
	if (!TestFalse(TEXT("the first popup was dismissed"), Layer->IsOpen(Popup))
		|| !TestTrue(TEXT("a legitimately refused opener falls back to the captured widget"), Services->GetFocusedWidget(0) == Previous))return false;

	Opener->SetIsFocusable(true);
	UDreamFocusReentryProbe* Probe = Opener->AddComponent<UDreamFocusReentryProbe>();
	if (!TestNotNull(TEXT("the opener has an actual focus-received listener"), Probe))return false;
	Opener->OnFocusReceived.AddDynamic(Probe, &UDreamFocusReentryProbe::OnReceived);
	Probe->Trigger = EDreamFocusReentryCallback::Received;
	int32 CallbackCount = 0;
	bool bOpenerHeldFocusInsideCallback = false;
	bool bRedirectSucceeded = false;
	bool bOutsideHeldFocusInsideCallback = false;
	const bool bClear = Parameters == TEXT("clear");
	const bool bReturn = Parameters == TEXT("aba");
	UDreamWidget* Expected = bClear ? nullptr : bReturn ? Opener : Outside;
	Probe->Action = [Services, Opener, Outside, Expected, bClear, bReturn, &CallbackCount, &bOpenerHeldFocusInsideCallback,
		&bRedirectSucceeded, &bOutsideHeldFocusInsideCallback]()
	{
		++CallbackCount;
		bOpenerHeldFocusInsideCallback = Services->GetFocusedWidget(0) == Opener;
		if (bClear)
		{
			Services->ClearFocus(Opener, 0, 0);
			bRedirectSucceeded = Services->GetFocusedWidget(0) == nullptr;
		}
		else
		{
			bRedirectSucceeded = Services->FocusForNavigation(Outside, 0);
			if (bReturn)bRedirectSucceeded = Services->FocusForNavigation(Opener, 0) && bRedirectSucceeded;
		}
		bOutsideHeldFocusInsideCallback = Services->GetFocusedWidget(0) == Expected;
	};
	if (!TestTrue(TEXT("the second popup is pushed through the real layer"), Layer->Push(Params))
		|| !TestTrue(TEXT("the second popup actually holds the player's focus"), Services->GetFocusedWidget(0) == Popup))return false;
	Layer->Dismiss(Popup);
	TestFalse(TEXT("the actual popup stack is closed"), Layer->IsOpen(Popup));
	TestEqual(TEXT("returning focus ran the opener callback exactly once"), CallbackCount, 1);
	TestTrue(TEXT("the opener really received the returned focus"), bOpenerHeldFocusInsideCallback);
	TestTrue(TEXT("the callback's public focus request succeeded"), bRedirectSucceeded);
	TestTrue(TEXT("the explicit destination held focus before the callback returned"), bOutsideHeldFocusInsideCallback);
	TestTrue(TEXT("the captured fallback does not overwrite the callback's destination"), Services->GetFocusedWidget(0) == Expected);
	Rig.PumpFrames(1);
	TestTrue(TEXT("the callback's destination also survives the next real frame"), Services->GetFocusedWidget(0) == Expected);
	return true;
}

#endif
