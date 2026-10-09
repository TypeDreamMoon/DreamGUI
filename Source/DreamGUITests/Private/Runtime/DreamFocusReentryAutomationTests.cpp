// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamFocusReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputUser.h"
#include "UObject/GarbageCollection.h"

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

	bool RunCase(FAutomationTestBase& InTest, EDreamFocusReentryCallback InCallback, bool bInClearFocus, int32 InPointerId)
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
		if (!InTest.TestTrue(TEXT("the previous widget takes focus through the public API"), Previous->SetFocus(0, InPointerId)))return false;
		const int32 HoverPointerId = DreamUIPointerIds::ScriptBase + 90;
		User->GetPointerEventData(HoverPointerId, true)->SetHighlightedWidgetForNavigation(Previous);
		UDreamFocusReentryProbe* ActingProbe = InCallback == EDreamFocusReentryCallback::Deselect ? PreviousProbe : RequestedProbe;
		ActingProbe->Trigger = InCallback;
		bool bNestedFocusSucceeded = false;
		ActingProbe->Action = [Requested, Redirected, bInClearFocus, InPointerId, &bNestedFocusSucceeded]()
		{
			if (bInClearFocus)
			{
				Requested->ClearFocus(0, InPointerId);
				bNestedFocusSucceeded = !Requested->HasFocus(0, InPointerId);
			}
			else
			{
				bNestedFocusSucceeded = Redirected->SetFocus(0, InPointerId);
			}
		};
		InTest.TestTrue(TEXT("the outer focus request uses the public API"), Requested->SetFocus(0, InPointerId));
		InTest.TestEqual(FString::Printf(TEXT("one nested mutation: callback=%d, clear=%d, pointer=%d"),
			static_cast<int32>(InCallback), bInClearFocus, InPointerId), ActingProbe->MutationCount, 1);
		InTest.TestTrue(TEXT("the nested public focus operation succeeded"), bNestedFocusSucceeded);

		UDreamWidget* ExpectedFocus = bInClearFocus ? nullptr : Redirected;
		bool bValid = InTest.TestTrue(TEXT("the nested operation owns the player's final focus"), User->GetFocusedWidget() == ExpectedFocus);
		UDreamPointerEventData* EventData = User->FindPointerEventData(InPointerId);
		if (!InTest.TestNotNull(TEXT("the public focus operation has pointer data"), EventData))return false;
		bValid = InTest.TestTrue(TEXT("pointer selection mirrors the nested final focus"), EventData->SelectedComponent == ExpectedFocus) && bValid;
		bValid = InTest.TestTrue(TEXT("navigation highlight keeps the nested final focus"),
			EventData->GetHighlightedComponentForNavigation() == ExpectedFocus) && bValid;
		// Received runs after Select. A Select callback that redirects or clears has already ended
		// this widget's focus, so the interrupted outer request must not announce a late Received.
		const int32 ExpectedReceived = InCallback == EDreamFocusReentryCallback::Received ? 1 : 0;
		bValid = InTest.TestEqual(TEXT("the interrupted request sends no stale focus-received notification"),
			RequestedProbe->ReceivedCount, ExpectedReceived) && bValid;
		bValid = InTest.TestEqual(TEXT("the previous focus loses its original tenure exactly once"), PreviousProbe->LostCount, 1) && bValid;
		bValid = InTest.TestTrue(TEXT("every received notification was sent while its widget had focus"),
			RequestedProbe->bEveryReceivedHadFocus) && bValid;
		bValid = InTest.TestEqual(TEXT("the redirected widget receives focus exactly when requested"),
			RedirectedProbe->ReceivedCount, bInClearFocus ? 0 : 1) && bValid;
		bValid = InTest.TestTrue(TEXT("an unrelated pointer keeps its independent hover highlight"),
			User->FindPointerEventData(HoverPointerId)->GetHighlightedComponentForNavigation() == Previous) && bValid;
		return bValid;
	}

	bool RunCases(FAutomationTestBase& InTest, EDreamFocusReentryCallback InCallback)
	{
		bool bValid = true;
		for (bool bClear : {false, true})
		{
			for (int32 PointerId : {0, DreamUIPointerIds::ScriptBase + 1})
			{
				bValid = RunCase(InTest, InCallback, bClear, PointerId) && bValid;
			}
		}
		return bValid;
	}
	enum class EAdditionalCase : uint8
	{
		ReturnToRequested,
		ReturnToPrevious,
		ReturnToPreviousThenRedirect,
		ClearThenReselect,
		ReplacePointer,
		ReplacePointerAndCollect
	};

	bool RunAdditionalCase(FAutomationTestBase& InTest, EAdditionalCase InCase)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("the additional focus-reentry rig came up"), Rig.IsUsable()))return false;
		UDreamWidget* Previous = Rig.MakeWidget(TEXT("PreviousFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(-300.0, 0.0));
		UDreamWidget* Requested = Rig.MakeWidget(TEXT("RequestedFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(0.0, 0.0));
		UDreamWidget* Redirected = Rig.MakeWidget(TEXT("RedirectedFocus"), nullptr, FVector2D(150.0, 50.0), FVector2D(300.0, 0.0));
		UDreamFocusReentryProbe* PreviousProbe = AddProbe(Previous);
		UDreamFocusReentryProbe* RequestedProbe = AddProbe(Requested);
		UDreamFocusReentryProbe* RedirectedProbe = AddProbe(Redirected);
		UDreamUIInputUser* User = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
		if (!InTest.TestTrue(TEXT("the additional widgets and player were made"),
			PreviousProbe != nullptr && RequestedProbe != nullptr && RedirectedProbe != nullptr && User != nullptr))return false;
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("the previous widget takes focus"), Previous->SetFocus(0, 0)))return false;
		const TWeakObjectPtr<UDreamPointerEventData> OriginalPointer(User->FindPointerEventData(0));
		TWeakObjectPtr<UDreamPointerEventData> ReplacementPointer;
		bool bReplacementSurvivedCollection = false;
		bool bNestedFocusSucceeded = false;
		const bool bReplacePointer = InCase == EAdditionalCase::ReplacePointer || InCase == EAdditionalCase::ReplacePointerAndCollect;
		if (InCase == EAdditionalCase::ReturnToRequested)
		{
			RequestedProbe->Trigger = EDreamFocusReentryCallback::Select;
			RequestedProbe->Action = [Requested, Redirected, &bNestedFocusSucceeded]()
			{
				bNestedFocusSucceeded = Redirected->SetFocus(0, 0) && Requested->SetFocus(0, 0);
			};
		}
		else if (InCase == EAdditionalCase::ReturnToPrevious)
		{
			PreviousProbe->Trigger = EDreamFocusReentryCallback::Deselect;
			PreviousProbe->Action = [Previous, &bNestedFocusSucceeded]() { bNestedFocusSucceeded = Previous->SetFocus(0, 0); };
		}
		else if (InCase == EAdditionalCase::ReturnToPreviousThenRedirect)
		{
			PreviousProbe->Trigger = EDreamFocusReentryCallback::Deselect;
			PreviousProbe->Action = [Previous, Redirected, &bNestedFocusSucceeded]()
			{
				bNestedFocusSucceeded = Previous->SetFocus(0, 0) && Redirected->SetFocus(0, 0);
			};
		}
		else if (InCase == EAdditionalCase::ClearThenReselect)
		{
			if (!InTest.TestTrue(TEXT("the widget is focused before the public clear"), Requested->SetFocus(0, 0)))return false;
			RequestedProbe->Trigger = EDreamFocusReentryCallback::Deselect;
			RequestedProbe->Action = [Requested, &bNestedFocusSucceeded]() { bNestedFocusSucceeded = Requested->SetFocus(0, 0); };
		}
		else
		{
			PreviousProbe->Trigger = EDreamFocusReentryCallback::Deselect;
			PreviousProbe->Action = [User, &ReplacementPointer]()
			{
				User->RetirePointer(0);
				User->RunOrDefer([User, &ReplacementPointer]()
				{
					ReplacementPointer = User->GetPointerEventData(0, true);
					// A different allocation of the map and a new object for the same ID: the outer
					// transition must re-query, and every added pointer must mirror its final focus.
					for (int32 Index = 1; Index <= 32; ++Index)User->GetPointerEventData(DreamUIPointerIds::ScriptBase + Index, true);
				});
			};
			if (InCase == EAdditionalCase::ReplacePointerAndCollect)
			{
				RequestedProbe->Trigger = EDreamFocusReentryCallback::Select;
				RequestedProbe->Action = [User, &ReplacementPointer, &bReplacementSurvivedCollection]()
				{
					User->RetirePointer(0);
					User->RunOrDefer([&ReplacementPointer, &bReplacementSurvivedCollection]()
					{
						CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
						bReplacementSurvivedCollection = ReplacementPointer.GetEvenIfUnreachable() != nullptr;
					});
				};
			}
		}
		if (InCase == EAdditionalCase::ClearThenReselect)Requested->ClearFocus(0, 0);
		else InTest.TestTrue(TEXT("the additional outer focus request succeeds"), Requested->SetFocus(0, 0));
		bool bValid = true;
		if (!bReplacePointer)bValid = InTest.TestTrue(TEXT("the nested focus operation succeeded"), bNestedFocusSucceeded);
		const int32 ExpectedMutations = InCase == EAdditionalCase::ReplacePointerAndCollect ? 2 : 1;
		bValid = InTest.TestEqual(TEXT("the configured callbacks each acted once"),
			PreviousProbe->MutationCount + RequestedProbe->MutationCount, ExpectedMutations) && bValid;
		UDreamWidget* ExpectedFocus = InCase == EAdditionalCase::ReturnToPrevious ? Previous
			: InCase == EAdditionalCase::ReturnToPreviousThenRedirect ? Redirected : Requested;
		bValid = InTest.TestTrue(TEXT("the newest transition owns the final focus"), User->GetFocusedWidget() == ExpectedFocus) && bValid;
		for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Pair : User->GetPointerEventDataMap())
		{
			bValid = InTest.TestTrue(TEXT("every existing pointer mirrors the newest focus"), Pair.Value->SelectedComponent == ExpectedFocus) && bValid;
		}
		if (InCase != EAdditionalCase::ReplacePointerAndCollect)
		{
			UDreamPointerEventData* CurrentPointer = User->FindPointerEventData(0);
			if (!InTest.TestNotNull(TEXT("a current focus pointer remains"), CurrentPointer))return false;
			bValid = InTest.TestTrue(TEXT("the newest transition owns the current pointer's highlight"),
				CurrentPointer->GetHighlightedComponentForNavigation() == ExpectedFocus) && bValid;
		}
		const int32 ExpectedReceived = InCase == EAdditionalCase::ReturnToPrevious || InCase == EAdditionalCase::ReturnToPreviousThenRedirect
			? 0 : InCase == EAdditionalCase::ClearThenReselect ? 2 : 1;
		bValid = InTest.TestEqual(TEXT("only current transitions notify focus received"), RequestedProbe->ReceivedCount, ExpectedReceived) && bValid;
		bValid = InTest.TestTrue(TEXT("no received notification belongs to a widget already unfocused"), RequestedProbe->bEveryReceivedHadFocus) && bValid;
		if (InCase == EAdditionalCase::ReturnToPrevious)
		{
			bValid = InTest.TestEqual(TEXT("the reselected old focus got no stale lost notification"), PreviousProbe->LostCount, 0) && bValid;
			bValid = InTest.TestEqual(TEXT("the old focus was received again exactly once"), PreviousProbe->ReceivedCount, 2) && bValid;
		}
		if (InCase == EAdditionalCase::ReturnToPreviousThenRedirect)
		{
			bValid = InTest.TestEqual(TEXT("the reselected old focus loses its newer tenure exactly once"), PreviousProbe->LostCount, 1) && bValid;
			bValid = InTest.TestEqual(TEXT("the old focus was received again exactly once before redirecting"), PreviousProbe->ReceivedCount, 2) && bValid;
			bValid = InTest.TestEqual(TEXT("the final redirected focus is received exactly once"), RedirectedProbe->ReceivedCount, 1) && bValid;
		}
		if (InCase == EAdditionalCase::ClearThenReselect)
		{
			bValid = InTest.TestEqual(TEXT("a clear callback that reselected the widget got no stale lost notification"), RequestedProbe->LostCount, 0) && bValid;
		}
		if (bReplacePointer)
		{
			bValid = InTest.TestTrue(TEXT("the callback made a genuinely different pointer object"),
				ReplacementPointer.GetEvenIfUnreachable() != nullptr && ReplacementPointer.GetEvenIfUnreachable() != OriginalPointer.GetEvenIfUnreachable()) && bValid;
			bValid = InTest.TestTrue(TEXT("Select received the replacement event data"),
				RequestedProbe->LastSelectEventData.GetEvenIfUnreachable() == ReplacementPointer.GetEvenIfUnreachable()) && bValid;
			if (InCase == EAdditionalCase::ReplacePointerAndCollect)
			{
				bValid = InTest.TestNull(TEXT("the selected replacement pointer was retired"), User->FindPointerEventData(0)) && bValid;
				bValid = InTest.TestTrue(TEXT("replacement event data survived GC while its transition was still dispatching"), bReplacementSurvivedCollection) && bValid;
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
				bValid = InTest.TestNull(TEXT("replacement event data is collected after its transition ends"), ReplacementPointer.GetEvenIfUnreachable()) && bValid;
			}
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFocusTransitionOwnershipReentryTest,
	"DreamGUI.Input.Focus.NewerTransitionsAndRecreatedPointersSurviveInterruptedFocusRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFocusTransitionOwnershipReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusReentryTestLocal;
	bool bValid = true;
	for (EAdditionalCase Case : {EAdditionalCase::ReturnToRequested, EAdditionalCase::ReturnToPrevious,
		EAdditionalCase::ReturnToPreviousThenRedirect, EAdditionalCase::ClearThenReselect, EAdditionalCase::ReplacePointer, EAdditionalCase::ReplacePointerAndCollect})
	{
		bValid = RunAdditionalCase(*this, Case) && bValid;
	}
	return bValid;
}

#endif
