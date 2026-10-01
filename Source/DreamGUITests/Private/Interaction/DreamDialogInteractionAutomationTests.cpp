// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamDialog in its standalone arrangement -- placed on a screen as an ordinary widget, darkening
 * it with its own dimmer -- answered and dismissed through the real pointer pipeline.
 *
 * UMG has no dialog widget, so what is asserted is what this control's header promises: a button
 * answers with its own result and the dialog closes with it (OnDialogClosed, exactly once); while the
 * dialog is up its dimmer eats every click aimed at the screen behind; a click on the dimmer itself
 * dismisses the dialog only when bCloseOnDimmerClick asks for that, and with the cancel result.
 *
 * The dialog is given the whole viewport, which is what a standalone dialog stretches to fill. A
 * headless world never begins play, so the construct-time stretch never runs and the size has to be
 * stated -- the only difference from a dialog on a real screen.
 */
namespace DreamPressDialogTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The top-left corner: on the dimmer, well off the centred panel. */
	const FVector2D OnTheDimmer(40.0, 40.0);

	struct FPlacedDialog
	{
		UDreamDialog* Dialog = nullptr;

		bool IsReady() const { return Dialog != nullptr && Dialog->ButtonWidgets.Num() == 2; }
	};

	/**
	 * The dialog, with its seeded pair of buttons (Cancel, then OK answering "Confirm"), its two
	 * events going to InListener. Made AFTER anything a test wants behind it, so it is drawn -- and
	 * hit-tested -- on top.
	 */
	FPlacedDialog PlaceDialog(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedDialog Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamDialog* Dialog = InRig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr,
			FVector2D(ViewportSize.X, ViewportSize.Y));
		if (!InTest.TestNotNull(TEXT("A dialog can be made on the rig"), Dialog))
		{
			return Placed;
		}
		Dialog->SetTitle(FText::AsCultureInvariant(TEXT("Delete the save?")));
		Dialog->OnDialogClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogClosed);
		Dialog->OnButtonClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogButtonClicked);
		InRig.PumpFrames(2);

		Placed.Dialog = Dialog;
		InTest.TestTrue(TEXT("The dialog offers its two seeded buttons"), Placed.IsReady());
		return Placed;
	}

	/** The button in the dialog's row that answers with InResult, or null. */
	UDreamButton* ButtonAnswering(const UDreamDialog* InDialog, FName InResult)
	{
		const TArray<FDreamDialogButton> Specs = InDialog->GetButtons();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (Specs[Index].Result == InResult && InDialog->ButtonWidgets.IsValidIndex(Index))
			{
				return InDialog->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogConfirmTest,
	"DreamGUI.Dialog.ClickingTheConfirmButtonClosesTheDialogWithItsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDialogConfirmTest, "DreamGUI.Dialog.ClickingTheConfirmButtonClosesTheDialogWithItsResult", "[Pointer][Animated]")

bool FDreamPressDialogConfirmTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamButton* Confirm = ButtonAnswering(Placed.Dialog, TEXT("Confirm"));
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the confirm button completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm))->Click());

	if (TestEqual(TEXT("The dialog closed once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the result of the button that was clicked"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	if (TestEqual(TEXT("The click itself was announced once"), Listener->DialogButtonResults.Num(), 1))
	{
		TestEqual(TEXT("Naming the same result"), Listener->DialogButtonResults[0], FName(TEXT("Confirm")));
	}
	TestFalse(TEXT("A standalone dialog puts itself away when it closes"), Placed.Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogBlocksBehindTest,
	"DreamGUI.Dialog.WhileTheDialogIsUpAClickOnAButtonBehindItDoesNothingUntilItCloses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDialogBlocksBehindTest, "DreamGUI.Dialog.WhileTheDialogIsUpAClickOnAButtonBehindItDoesNothingUntilItCloses", "[Pointer][Animated]")

bool FDreamPressDialogBlocksBehindTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// A button in the top-left, well off the dialog's centred panel but under its full-screen dimmer.
	UDreamButton* Behind = Rig.MakeControl<UDreamButton>(TEXT("Behind"), nullptr, FVector2D(160.0, 50.0), FVector2D(-500.0, 280.0));
	if (!TestNotNull(TEXT("A button can be made behind where the dialog will be"), Behind))
	{
		return false;
	}
	Behind->OnClicked.AddDynamic(BehindListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	FDreamElementRef BehindElement = Rig.Driver()->Find(FDreamBy::Widget(Behind));

	TestTrue(TEXT("Clicking the button behind the dialog completes"), BehindElement->Click());
	TestEqual(TEXT("While the dialog is up the button behind it is not clicked"), BehindListener->ClickedCount, 0);

	UDreamButton* Cancel = ButtonAnswering(Placed.Dialog, TEXT("Cancel"));
	if (!TestNotNull(TEXT("The dialog has a button answering Cancel"), Cancel))
	{
		return false;
	}
	TestTrue(TEXT("Answering the dialog completes"), Rig.Driver()->Find(FDreamBy::Widget(Cancel))->Click());
	Rig.PumpFrames(1);
	TestEqual(TEXT("Answering closed the dialog"), Listener->DialogClosedResults.Num(), 1);

	TestTrue(TEXT("Clicking the button behind again completes"), BehindElement->Click());
	TestEqual(TEXT("With the dialog gone the same click reaches it"), BehindListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogDimmerDefaultTest,
	"DreamGUI.Dialog.ByDefaultClickingTheDimmerLeavesTheDialogUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDialogDimmerDefaultTest, "DreamGUI.Dialog.ByDefaultClickingTheDimmerLeavesTheDialogUp", "[Pointer][Animated]")

bool FDreamPressDialogDimmerDefaultTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	// The premise, stated: a question that matters must not be dismissable by a stray click.
	TestFalse(TEXT("Dismissing by clicking the dimmer is off by default"), Placed.Dialog->bCloseOnDimmerClick);

	TestTrue(TEXT("Clicking the dimmer completes"),
		Rig.Driver()->Sequence().MoveToPixel(OnTheDimmer).Press().Release().Perform());

	TestEqual(TEXT("The dialog did not close"), Listener->DialogClosedResults.Num(), 0);
	TestTrue(TEXT("And is still up"), Placed.Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogDimmerDismissTest,
	"DreamGUI.Dialog.WithCloseOnDimmerClickClickingTheDimmerCancelsTheDialog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDialogDimmerDismissTest, "DreamGUI.Dialog.WithCloseOnDimmerClickClickingTheDimmerCancelsTheDialog", "[Pointer][Animated]")

bool FDreamPressDialogDimmerDismissTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Dialog->SetCloseOnDimmerClick(true);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the dimmer completes"),
		Rig.Driver()->Sequence().MoveToPixel(OnTheDimmer).Press().Release().Perform());

	if (TestEqual(TEXT("A click on the dimmer closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the cancel result, as a dismissal rather than an answer"),
			Listener->DialogClosedResults[0], Placed.Dialog->ResolveCancelResult());
	}
	TestEqual(TEXT("No button was clicked to do it"), Listener->DialogButtonResults.Num(), 0);
	TestFalse(TEXT("And the dialog put itself away"), Placed.Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogDisabledDefaultTest,
	"DreamGUI.Dialog.SubmittingTheDefaultButtonWhileItIsDisabledLeavesTheDialogUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * SubmitDefaultButton answers as the default button would -- and it broadcast that button's click
 * whether or not the button could be clicked, so a dialog whose confirm was greyed out until the
 * player had done something still closed with "Confirm" on Enter. The default is disabled here and
 * submitted: the dialog stays up and nothing is announced. Enabled again, the same call answers.
 */
bool FDreamPressDialogDisabledDefaultTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamButton* Confirm = ButtonAnswering(Placed.Dialog, TEXT("Confirm"));
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm)
		|| !TestEqual(TEXT("Which is its default"), Placed.Dialog->GetDefaultButton(), Confirm))
	{
		return false;
	}
	// UMG's SetIsEnabled, which is what a game flips to grey a button out.
	Confirm->SetIsEnabled(false);
	Rig.PumpFrames(1);

	Placed.Dialog->SubmitDefaultButton();

	TestEqual(TEXT("Submitting a disabled default closes nothing"), Listener->DialogClosedResults.Num(), 0);
	TestEqual(TEXT("And announces no answer"), Listener->DialogButtonResults.Num(), 0);
	TestTrue(TEXT("The dialog is still up"), Placed.Dialog->GetWidgetActive());

	Confirm->SetIsEnabled(true);
	Rig.PumpFrames(1);
	Placed.Dialog->SubmitDefaultButton();
	if (TestEqual(TEXT("Enabled again, submitting closes the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the default's answer"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogBackRearmedTest,
	"DreamGUI.Dialog.BackClosesTheDialogAgainOnceCloseOnBackIsTurnedOffAndBackOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressDialogBackRearmedTest, "DreamGUI.Dialog.BackClosesTheDialogAgainOnceCloseOnBackIsTurnedOffAndBackOn", "[Nav][Animated]")

/*
 * Turning bCloseOnBack off releases the dialog's Back scope by clearing the dialog it answers for, and
 * turning it back on only did anything when no scope existed yet -- which one did. The scope stayed
 * released, Back went on to the screen underneath, and the dialog could no longer be backed out of.
 * Off and on again, Back cancels the dialog with its cancel result.
 */
bool FDreamPressDialogBackRearmedTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedDialog Placed = PlaceDialog(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Dialog->SetCloseOnBack(false);
	Placed.Dialog->SetCloseOnBack(true);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());

	if (TestEqual(TEXT("Back closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("As a cancel"), Listener->DialogClosedResults[0], FName(TEXT("Cancel")));
	}
	TestFalse(TEXT("And the dialog put itself away"), Placed.Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressDialogFocusOnWakeTest,
	"DreamGUI.Dialog.ADialogPlacedAsleepTakesFocusWhenItIsWokenAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A standalone dialog waits asleep until there is something to ask, and puts focus on its default
 * button when it appears. It did that at construct, which is begin play whether or not the dialog is
 * showing: an asleep dialog took focus onto a button nobody could see, away from the screen the
 * player was on. And when it was woken, the Back scope it wears was pushed and put focus on the first
 * control in reading order -- the cancel button. Placed asleep here, it leaves focus alone; woken,
 * focus is on its default button.
 */
bool FDreamPressDialogFocusOnWakeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressDialogTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Asleep from the start: the dialog begins play under a host that is not active.
	UDreamWidget* Host = Rig.MakeWidget(TEXT("Host"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A host for the dialog can be made"), Host))
	{
		return false;
	}
	Host->SetWidgetActive(false);
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("Ask"), Host, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A dialog can be made under it"), Dialog))
	{
		return false;
	}
	Rig.PumpFrames(1);
	UDreamButton* Confirm = ButtonAnswering(Dialog, TEXT("Confirm"));
	if (!TestNotNull(TEXT("The dialog has its default button"), Confirm)
		|| !TestNotNull(TEXT("Which has a face to take focus"), Confirm->FaceNode.Get())
		|| !TestTrue(TEXT("And the dialog focuses its default when it appears"), Dialog->bFocusDefaultButton))
	{
		return false;
	}

	TestNotEqual(TEXT("Asleep, the dialog has not taken focus"), Rig.EventSystem()->GetCurrentSelectedComponent(0), Confirm->FaceNode.Get());

	Host->SetWidgetActive(true);
	Rig.PumpFrames(1);

	TestEqual(TEXT("Woken, focus is on its default button"), Rig.EventSystem()->GetCurrentSelectedComponent(0), Confirm->FaceNode.Get());
	return true;
}

#endif
