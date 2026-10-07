// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamDialog answered with a finger and with the pad, in its standalone arrangement (see the dialog's own interaction
 * tests next door for the arrangement and why the dialog is given the whole viewport).
 *
 * A dialog's buttons are buttons: a touch is a click of one (SButton.cpp:354, :410), and the pad's Accept presses whichever
 * the pad's focus is on (SButton::OnKeyDown / OnKeyUp, SButton.cpp:296-340). The focus a dialog opens with is its default
 * button's, as a CommonUI activatable widget gives its desired focus target the focus when it activates.
 */
namespace DreamDialogTouchPadTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The dialog with its seeded pair of buttons (Cancel, then OK answering "Confirm"), its events going to InListener. */
	UDreamDialog* PlaceDialog(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return nullptr;
		}
		UDreamDialog* Dialog = InRig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y));
		if (!InTest.TestNotNull(TEXT("A dialog can be made on the rig"), Dialog))
		{
			return nullptr;
		}
		Dialog->SetTitle(FText::AsCultureInvariant(TEXT("Delete the save?")));
		Dialog->OnDialogClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogClosed);
		Dialog->OnButtonClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogButtonClicked);
		InRig.PumpFrames(2);
		return InTest.TestEqual(TEXT("The dialog offers its two seeded buttons"), Dialog->ButtonWidgets.Num(), 2) ? Dialog : nullptr;
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

	/** Whether InWidget is InControl or one of its parts -- a button's selectable lives on its face. */
	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDialogTapTest,
	"DreamGUI.Dialog.ATapOnTheConfirmButtonClosesTheDialogWithItsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDialogTapTest, "DreamGUI.Dialog.ATapOnTheConfirmButtonClosesTheDialogWithItsResult", "[Touch][Animated]")

bool FDreamDialogTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogTouchPadTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamDialog* Dialog = PlaceDialog(*this, Rig, Listener.Get());
	UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the confirm button completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm))->Tap());

	if (TestEqual(TEXT("The dialog closed once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the tapped button's result"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	TestEqual(TEXT("The tap was announced as that button's answer, once"), Listener->DialogButtonResults.Num(), 1);
	TestFalse(TEXT("The dialog put itself away"), Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDialogPadConfirmDefaultTest,
	"DreamGUI.Dialog.ThePadsConfirmAnswersWithTheButtonTheDialogGaveTheFocusAsItOpened",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDialogPadConfirmDefaultTest, "DreamGUI.Dialog.ThePadsConfirmAnswersWithTheButtonTheDialogGaveTheFocusAsItOpened", "[Nav][Animated]")

/*
 * The dialog opens with the focus on its default button, the pad's cursor with it; the player presses Accept and nothing
 * else. That presses the focused button -- the default, OK -- and the dialog closes with its result.
 */
bool FDreamDialogPadConfirmDefaultTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogTouchPadTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamDialog* Dialog = PlaceDialog(*this, Rig, Listener.Get());
	UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm)
		|| !TestEqual(TEXT("...which is its default"), Dialog->GetDefaultButton(), Confirm))
	{
		return false;
	}
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The dialog opened with the focus on its default button"), IsPartOf(Services->GetFocusedWidget(0), Confirm)))
	{
		return false;
	}

	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());

	if (TestEqual(TEXT("The confirm closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the default button's result"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	TestFalse(TEXT("The dialog put itself away"), Dialog->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDialogPadMoveAndConfirmTest,
	"DreamGUI.Dialog.ThePadMovedOntoTheOtherButtonAndConfirmedAnswersWithThatButtonsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDialogPadMoveAndConfirmTest, "DreamGUI.Dialog.ThePadMovedOntoTheOtherButtonAndConfirmedAnswersWithThatButtonsResult", "[Nav][Animated]")

/*
 * From the default button the stick moves left onto the button beside it, and Accept presses that one: the dialog closes
 * with the result of the button the pad was on, not the default's -- moving the focus is not answering.
 */
bool FDreamDialogPadMoveAndConfirmTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogTouchPadTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamDialog* Dialog = PlaceDialog(*this, Rig, Listener.Get());
	if (Dialog == nullptr)
	{
		return false;
	}
	const TArray<FDreamDialogButton> Specs = Dialog->GetButtons();
	UDreamButton* Confirm = ButtonAnswering(Dialog, TEXT("Confirm"));
	UDreamButton* Other = nullptr;
	FName OtherResult;
	for (int32 Index = 0; Index < Specs.Num(); ++Index)
	{
		if (Specs[Index].Result != FName(TEXT("Confirm")) && Dialog->ButtonWidgets.IsValidIndex(Index))
		{
			Other = Dialog->ButtonWidgets[Index].Get();
			OtherResult = Specs[Index].Result;
		}
	}
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The dialog has a confirm button and another"), Confirm) || !TestNotNull(TEXT("...the other"), Other)
		|| !TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The dialog opened with the focus on its default button"), IsPartOf(Services->GetFocusedWidget(0), Confirm)))
	{
		return false;
	}
	const TOptional<FVector2D> ConfirmAt = Rig.Driver()->Find(FDreamBy::Widget(Confirm))->GetCentrePixel();
	const TOptional<FVector2D> OtherAt = Rig.Driver()->Find(FDreamBy::Widget(Other))->GetCentrePixel();
	if (!TestTrue(TEXT("Both buttons are on the viewport"), ConfirmAt.IsSet() && OtherAt.IsSet()))
	{
		return false;
	}
	const EDreamUINavigationDirection TowardOther = OtherAt->X < ConfirmAt->X ? EDreamUINavigationDirection::Left : EDreamUINavigationDirection::Right;

	TestTrue(TEXT("Pushing the stick toward the other button completes"), Rig.Driver()->Sequence().Navigate(TowardOther).Perform());
	if (!TestTrue(TEXT("The stick moved the focus onto the other button"), IsPartOf(Services->GetFocusedWidget(0), Other)))
	{
		return false;
	}
	TestEqual(TEXT("Moving the focus answered nothing"), Listener->DialogClosedResults.Num(), 0);

	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	if (TestEqual(TEXT("The confirm closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("With the result of the button the pad was on"), Listener->DialogClosedResults[0], OtherResult);
	}
	return true;
}

#endif
