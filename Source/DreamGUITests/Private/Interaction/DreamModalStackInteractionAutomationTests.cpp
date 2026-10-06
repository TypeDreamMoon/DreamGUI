// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/UIButton.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * Two modals stacked, and answered with the player's own input.
 *
 * The UMG arrangement is CommonUI's modal layer, a UCommonActivatableWidgetStack: pushing a widget deactivates the one beneath
 * it, only the top one takes input, and Back is the top one's (UCommonActivatableWidget's back handler, on the leaf-most
 * active widget). So a click or a Back reaches the newest modal and nothing under it, and the one beneath answers only once
 * the newest has gone.
 *
 * The first modal comes up from a click on a button on the screen; the second from code, as a second question arrives while
 * the first is up -- a message, a timer. Everything after that is the player's input.
 */
namespace DreamModalStackTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** What the test keeps across the rig's life: declared before the rig, so an answer a teardown gives lands somewhere alive. */
	struct FAnswers
	{
		int32 DeleteClicks = 0;
		TArray<FName> First;
		TArray<FName> Second;
	};

	/** A Delete button that brings up the first modal, clicked; then the second modal from code. False with a reason when either did not come up. */
	bool StackTwoModals(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FAnswers& InAnswers, UDreamButton*& OutDelete,
		UDreamDialog*& OutFirst, UDreamDialog*& OutSecond)
	{
		InRig.BindTest(&InTest);
		UDreamUIModalSubsystem* Modals = InRig.IsUsable() ? UDreamUIModalSubsystem::Get(InRig.GetWorld()) : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig came up with the modal subsystem"), Modals))
		{
			return false;
		}
		OutDelete = InRig.MakeControl<UDreamButton>(TEXT("Delete"), nullptr, FVector2D(200.0, 60.0), FVector2D(0.0, -250.0));
		if (!InTest.TestTrue(TEXT("A button with its behaviour can be made on the rig"), OutDelete != nullptr && OutDelete->ButtonBehaviour != nullptr))
		{
			return false;
		}
		FAnswers* Answers = &InAnswers;
		OutDelete->ButtonBehaviour->GetOnClickEvent().AddLambda([Modals, Answers]()
		{
			++Answers->DeleteClicks;
			Modals->ShowModalNative(UDreamDialog::StaticClass(), [Answers](FName InResult) { Answers->First.Add(InResult); }, 0);
		});
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);

		InTest.TestTrue(TEXT("Clicking the Delete button completes"), InRig.Driver()->Find(FDreamBy::Widget(OutDelete))->Click());
		InRig.PumpFrames(2);
		OutFirst = Cast<UDreamDialog>(Modals->GetActiveModalWidget(0));
		if (!InTest.TestNotNull(TEXT("The click brought the first modal up"), OutFirst))
		{
			return false;
		}
		Modals->ShowModalNative(UDreamDialog::StaticClass(), [Answers](FName InResult) { Answers->Second.Add(InResult); }, 0);
		InRig.PumpFrames(2);
		OutSecond = Cast<UDreamDialog>(Modals->GetActiveModalWidget(0));
		return InTest.TestTrue(TEXT("The second modal came up on top of the first"), OutSecond != nullptr && OutSecond != OutFirst)
			&& InTest.TestEqual(TEXT("...two deep"), Modals->GetModalDepth(0), 2);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamModalStackClicksTest,
	"DreamGUI.Modal.TheNewestOfTwoModalsTakesTheClickAndTheOneBeneathAnswersOnlyOnceItHasGone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamModalStackClicksTest, "DreamGUI.Modal.TheNewestOfTwoModalsTakesTheClickAndTheOneBeneathAnswersOnlyOnceItHasGone", "[Pointer][Animated]")

/*
 * With both up: a click on the Delete button behind them reaches nothing. A click on the newest dialog's confirm answers the
 * newest -- the first is still waiting, the stack one deep. Then a click on the first dialog's confirm answers the first, and
 * the stack is empty.
 */
bool FDreamModalStackClicksTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalStackTestLocal;
	FAnswers Answers;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamButton* Delete = nullptr;
	UDreamDialog* First = nullptr;
	UDreamDialog* Second = nullptr;
	if (!StackTwoModals(*this, Rig, Answers, Delete, First, Second))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
	UDreamButton* FirstConfirm = First->GetDefaultButton();
	UDreamButton* SecondConfirm = Second->GetDefaultButton();
	if (!TestTrue(TEXT("Both dialogs have a default button"), FirstConfirm != nullptr && SecondConfirm != nullptr))
	{
		return false;
	}

	TestTrue(TEXT("Clicking where the Delete button is completes"), Rig.Driver()->Find(FDreamBy::Widget(Delete))->Click());
	TestEqual(TEXT("A click behind two modals reaches nothing"), Answers.DeleteClicks, 1);
	TestEqual(TEXT("...and answers neither"), Answers.First.Num() + Answers.Second.Num(), 0);
	TestEqual(TEXT("...both still up"), Modals->GetModalDepth(0), 2);

	TestTrue(TEXT("Clicking the newest dialog's confirm completes"), Rig.Driver()->Find(FDreamBy::Widget(SecondConfirm))->Click());
	Rig.PumpFrames(1);
	if (TestEqual(TEXT("The newest modal answered"), Answers.Second.Num(), 1))
	{
		TestEqual(TEXT("...with its confirm"), Answers.Second[0], FName(TEXT("Confirm")));
	}
	TestEqual(TEXT("The first is still waiting"), Answers.First.Num(), 0);
	TestEqual(TEXT("...the stack one deep"), Modals->GetModalDepth(0), 1);
	TestEqual(TEXT("...the first on top again"), Modals->GetActiveModalWidget(0), static_cast<UDreamUserWidget*>(First));

	TestTrue(TEXT("Clicking the first dialog's confirm completes"), Rig.Driver()->Find(FDreamBy::Widget(FirstConfirm))->Click());
	Rig.PumpFrames(1);
	if (TestEqual(TEXT("The first modal answered"), Answers.First.Num(), 1))
	{
		TestEqual(TEXT("...with its confirm"), Answers.First[0], FName(TEXT("Confirm")));
	}
	TestFalse(TEXT("No modal is left"), Modals->IsModalActive(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamModalStackBackTest,
	"DreamGUI.Modal.BackClosesTheNewestOfTwoModalsAndTheNextBackTheOneBeneath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamModalStackBackTest, "DreamGUI.Modal.BackClosesTheNewestOfTwoModalsAndTheNextBackTheOneBeneath", "[Nav][Animated]")

/*
 * With both up, Back answers the newest with Back and leaves the first up and waiting; a second Back answers the first.
 */
bool FDreamModalStackBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalStackTestLocal;
	FAnswers Answers;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamButton* Delete = nullptr;
	UDreamDialog* First = nullptr;
	UDreamDialog* Second = nullptr;
	if (!StackTwoModals(*this, Rig, Answers, Delete, First, Second))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());

	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());
	if (TestEqual(TEXT("Back answered the newest modal"), Answers.Second.Num(), 1))
	{
		TestEqual(TEXT("...as Back"), Answers.Second[0], FName(TEXT("Back")));
	}
	TestEqual(TEXT("...and not the first"), Answers.First.Num(), 0);
	TestEqual(TEXT("...which is still up, alone"), Modals->GetModalDepth(0), 1);

	TestTrue(TEXT("Pressing Back again completes"), Rig.Driver()->Sequence().Back().Perform());
	if (TestEqual(TEXT("The second Back answered the first modal"), Answers.First.Num(), 1))
	{
		TestEqual(TEXT("...as Back"), Answers.First[0], FName(TEXT("Back")));
	}
	TestFalse(TEXT("No modal is left"), Modals->IsModalActive(0));
	return true;
}

#endif
