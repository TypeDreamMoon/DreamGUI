// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamFocusReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputUser.h"

namespace DreamFocusReentryTestLocal
{
	UDreamFocusReentryProbe* AddProbe(UDreamWidget* InWidget)
	{
		if (!IsValid(InWidget))return nullptr;
		InWidget->SetIsFocusable(true);
		UDreamFocusReentryProbe* Probe = InWidget->AddComponent<UDreamFocusReentryProbe>();
		if (Probe != nullptr)
		{
			InWidget->OnFocusReceived.AddDynamic(Probe, &UDreamFocusReentryProbe::OnReceived);
			InWidget->OnFocusLost.AddDynamic(Probe, &UDreamFocusReentryProbe::OnLost);
		}
		return Probe;
	}

	bool RunCase(FAutomationTestBase& InTest, EDreamFocusReentryCallback InCallback, bool bInClearFocus)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the focus-reentry rig came up"), Rig.IsUsable()))return false;
		UDreamWidget* Previous = Rig.MakeWidget(TEXT("PreviousFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(-300.0, 0.0));
		UDreamWidget* Requested = Rig.MakeWidget(TEXT("RequestedFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(0.0, 0.0));
		UDreamWidget* Redirected = Rig.MakeWidget(TEXT("RedirectedFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(300.0, 0.0));
		UDreamFocusReentryProbe* PreviousProbe = AddProbe(Previous);
		UDreamFocusReentryProbe* RequestedProbe = AddProbe(Requested);
		UDreamFocusReentryProbe* RedirectedProbe = AddProbe(Redirected);
		UDreamUIInputUser* User = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
		if (!InTest.TestTrue(TEXT("three focusable widgets and their player were made"),
			PreviousProbe != nullptr && RequestedProbe != nullptr && RedirectedProbe != nullptr && User != nullptr))return false;
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("the previous widget takes focus through the public API"), Previous->SetFocus(0, 0)))return false;
		UDreamFocusReentryProbe* ActingProbe = InCallback == EDreamFocusReentryCallback::Deselect ? PreviousProbe : RequestedProbe;
		ActingProbe->Trigger = InCallback;
		bool bNestedFocusSucceeded = false;
		ActingProbe->Action = [Requested, Redirected, bInClearFocus, &bNestedFocusSucceeded]()
		{
			if (bInClearFocus)
			{
				Requested->ClearFocus(0, 0);
				bNestedFocusSucceeded = !Requested->HasFocus(0, 0);
			}
			else
			{
				bNestedFocusSucceeded = Redirected->SetFocus(0, 0);
			}
		};
		InTest.TestTrue(TEXT("the outer focus request uses the public API"), Requested->SetFocus(0, 0));
		InTest.TestEqual(FString::Printf(TEXT("one nested mutation: callback=%d, clear=%d"),
			static_cast<int32>(InCallback), bInClearFocus), ActingProbe->MutationCount, 1);
		InTest.TestTrue(TEXT("the nested public focus operation succeeded"), bNestedFocusSucceeded);

		UDreamWidget* ExpectedFocus = bInClearFocus ? nullptr : Redirected;
		bool bValid = InTest.TestTrue(TEXT("the nested operation owns the player's final focus"), User->GetFocusedWidget() == ExpectedFocus);
		UDreamPointerEventData* EventData = User->FindPointerEventData(0);
		if (!InTest.TestNotNull(TEXT("the public focus operation has pointer data"), EventData))return false;
		bValid = InTest.TestTrue(TEXT("pointer selection mirrors the nested final focus"), EventData->SelectedComponent == ExpectedFocus) && bValid;
		bValid = InTest.TestTrue(TEXT("navigation highlight keeps the nested final focus"),
			EventData->GetHighlightedComponentForNavigation() == ExpectedFocus) && bValid;
		// Received runs after Select. A Select callback that redirects or clears has already ended
		// this widget's focus, so the interrupted outer request must not announce a late Received.
		const int32 ExpectedReceived = InCallback == EDreamFocusReentryCallback::Received ? 1 : 0;
		bValid = InTest.TestEqual(TEXT("the interrupted request sends no stale focus-received notification"),
			RequestedProbe->ReceivedCount, ExpectedReceived) && bValid;
		bValid = InTest.TestTrue(TEXT("every received notification was sent while its widget had focus"),
			RequestedProbe->bEveryReceivedHadFocus) && bValid;
		bValid = InTest.TestEqual(TEXT("the redirected widget receives focus exactly when requested"),
			RedirectedProbe->ReceivedCount, bInClearFocus ? 0 : 1) && bValid;
		return bValid;
	}

	bool RunCases(FAutomationTestBase& InTest, EDreamFocusReentryCallback InCallback)
	{
		bool bValid = true;
		for (bool bClear : {false, true})bValid = RunCase(InTest, InCallback, bClear) && bValid;
		return bValid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFocusSelectReentryTest,
	"DreamGUI.Input.Focus.ASelectCallbackCannotReceiveFocusAfterRedirectingOrClearingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFocusSelectReentryTest::RunTest(const FString& Parameters)
{
	return DreamFocusReentryTestLocal::RunCases(*this, EDreamFocusReentryCallback::Select);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFocusReceivedReentryTest,
	"DreamGUI.Input.Focus.AFocusReceivedCallbackKeepsItsNestedFocusAndNavigationHighlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFocusReceivedReentryTest::RunTest(const FString& Parameters)
{
	return DreamFocusReentryTestLocal::RunCases(*this, EDreamFocusReentryCallback::Received);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFocusDeselectReentryTest,
	"DreamGUI.Input.Focus.ADeselectCallbackKeepsItsNestedFocusAndNavigationHighlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFocusDeselectReentryTest::RunTest(const FString& Parameters)
{
	return DreamFocusReentryTestLocal::RunCases(*this, EDreamFocusReentryCallback::Deselect);
}

#endif
