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
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * A STANDALONE DIALOG ON A PANEL IN THE LEVEL.
 *
 * UMG has no dialog widget, so what is asserted is what this control's header promises (see the dialog's own interaction
 * tests): a button answers with its own result and the dialog closes with it (OnDialogClosed, exactly once); while the dialog
 * is up its dimmer eats every press aimed at what is behind it; and the dialog puts the pad's focus on its default button as it
 * appears, from where the D-pad moves between its buttons and the confirm answers. Placed on a panel -- given the whole panel,
 * as on a screen it is given the whole screen -- it promises the same: a widget component's widgets are pressed through the
 * interaction's ray as a screen's are through the mouse, and DreamGUI's D-pad on a panel is its directional scan among the
 * world-space widgets.
 */
namespace DreamDialogWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** The dialog and, made before it on the panel, a button behind its dimmer, off the dialog's centred panel. */
	struct FPanelDialog
	{
		UDreamDialog* Dialog = nullptr;
		UDreamButton* Behind = nullptr;

		bool IsReady() const { return Dialog != nullptr && Behind != nullptr && Dialog->ButtonWidgets.Num() == 2; }
	};

	/**
	 * On a panel: a small button in its corner, then the dialog with its seeded pair of buttons (Cancel, then OK answering
	 * "Confirm"), its events going to InListener, given the whole panel. The button is under the dimmer, outside the dialog's
	 * own centred panel (420 by 200 by default).
	 */
	FPanelDialog PlaceDialogOnPanel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		UDreamPressInteractionListener* InBehindListener)
	{
		FPanelDialog Placed;
		const FWorldStage Stage = SetUpPanel(InTest, InRig);
		if (!Stage.IsReady())
		{
			return Placed;
		}
		Placed.Behind = InRig.MakeControl<UDreamButton>(TEXT("Behind"), Stage.Panel, FVector2D(50.0, 30.0), FVector2D(-245.0, 130.0));
		Placed.Dialog = InRig.MakeControl<UDreamDialog>(TEXT("Ask"), Stage.Panel, PanelSize());
		if (!InTest.TestNotNull(TEXT("A button behind where the dialog will be"), Placed.Behind)
			|| !InTest.TestNotNull(TEXT("A dialog on the panel"), Placed.Dialog))
		{
			return FPanelDialog();
		}
		Placed.Behind->OnClicked.AddDynamic(InBehindListener, &UDreamPressInteractionListener::HandleClicked);
		Placed.Dialog->SetTitle(FText::AsCultureInvariant(TEXT("Delete the save?")));
		Placed.Dialog->OnDialogClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogClosed);
		Placed.Dialog->OnButtonClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleDialogButtonClicked);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		InTest.TestTrue(TEXT("The dialog offers its two seeded buttons"), Placed.IsReady());
		return Placed;
	}

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

	UDreamButton* OtherButton(const UDreamDialog* InDialog, FName& OutResult)
	{
		const TArray<FDreamDialogButton> Specs = InDialog->GetButtons();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (Specs[Index].Result != FName(TEXT("Confirm")) && InDialog->ButtonWidgets.IsValidIndex(Index))
			{
				OutResult = Specs[Index].Result;
				return InDialog->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}

	EDreamUINavigationDirection Toward(const FDreamDriverRig& InRig, const UDreamButton* InFrom, const UDreamButton* InTo)
	{
		const TOptional<FVector2D> FromAt = PixelOf(InRig, InFrom);
		const TOptional<FVector2D> ToAt = PixelOf(InRig, InTo);
		if (!FromAt.IsSet() || !ToAt.IsSet())
		{
			return EDreamUINavigationDirection::None;
		}
		return ToAt->X < FromAt->X ? EDreamUINavigationDirection::Left : EDreamUINavigationDirection::Right;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldDialogClickTest,
	"DreamGUI.Dialog.OnAWorldPanelAClickBehindTheDialogReachesNothingAndAClickOnConfirmClosesItWithItsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldDialogClickTest, "DreamGUI.Dialog.OnAWorldPanelAClickBehindTheDialogReachesNothingAndAClickOnConfirmClosesItWithItsResult", "[Pointer][World]")

/*
 * The dialog on a panel, a button behind its dimmer. A click through the world pointer on the button behind reaches nothing.
 * A click on the confirm button closes the dialog once with Confirm, announced once; with the dialog put away, the same click
 * on the button behind reaches it.
 */
bool FDreamWorldDialogClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPanelDialog Placed = PlaceDialogOnPanel(*this, Rig, Listener.Get(), BehindListener.Get());
	UDreamButton* Confirm = Placed.IsReady() ? ButtonAnswering(Placed.Dialog, TEXT("Confirm")) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm))
	{
		return false;
	}
	FDreamElementRef BehindElement = Rig.Driver()->Find(FDreamBy::Widget(Placed.Behind));

	TestTrue(TEXT("Clicking the button behind the dialog completes"), BehindElement->Click());
	TestEqual(TEXT("While the dialog is up the button behind it is not clicked"), BehindListener->ClickedCount, 0);
	TestEqual(TEXT("...and the dialog is not answered"), Listener->DialogClosedResults.Num(), 0);

	TestTrue(TEXT("Clicking the confirm button on the panel completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm))->Click());
	if (TestEqual(TEXT("The dialog closed once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("...with the result of the button that was clicked"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	TestEqual(TEXT("The click itself was announced once"), Listener->DialogButtonResults.Num(), 1);
	TestFalse(TEXT("A standalone dialog puts itself away when it closes"), Placed.Dialog->GetWidgetActive());
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the button behind again completes"), BehindElement->Click());
	TestEqual(TEXT("With the dialog gone the same click reaches it"), BehindListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldDialogTapTest,
	"DreamGUI.Dialog.OnAWorldPanelATapBehindTheDialogReachesNothingAndATapOnTheOtherButtonClosesItWithThatResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldDialogTapTest, "DreamGUI.Dialog.OnAWorldPanelATapBehindTheDialogReachesNothingAndATapOnTheOtherButtonClosesItWithThatResult", "[Touch][World]")

/* The same with a finger, whose ray from the camera lands on the panel where the finger is. */
bool FDreamWorldDialogTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPanelDialog Placed = PlaceDialogOnPanel(*this, Rig, Listener.Get(), BehindListener.Get());
	FName OtherResult;
	UDreamButton* Other = Placed.IsReady() ? OtherButton(Placed.Dialog, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a button besides Confirm"), Other))
	{
		return false;
	}
	FDreamElementRef BehindElement = Rig.Driver()->Find(FDreamBy::Widget(Placed.Behind));

	TestTrue(TEXT("A finger tapped on the button behind the dialog completes"), BehindElement->Tap(0));
	TestEqual(TEXT("While the dialog is up the tap reached nothing behind it"), BehindListener->ClickedCount, 0);

	TestTrue(TEXT("A finger tapped on the other button completes"), Rig.Driver()->Find(FDreamBy::Widget(Other))->Tap(1));
	if (TestEqual(TEXT("The tap closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("...with the tapped button's result"), Listener->DialogClosedResults[0], OtherResult);
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("A finger tapped on the button behind again completes"), BehindElement->Tap(2));
	TestEqual(TEXT("With the dialog gone the tap reaches it"), BehindListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldDialogPadTest,
	"DreamGUI.Dialog.OnAWorldPanelTheDialogTakesThePadsFocusAndThePadMovedOntoTheOtherButtonAnswersWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldDialogPadTest, "DreamGUI.Dialog.OnAWorldPanelTheDialogTakesThePadsFocusAndThePadMovedOntoTheOtherButtonAnswersWithIt", "[Nav][World]")

/*
 * The dialog on a panel comes up with the pad's focus on its default button. The stick toward the other button moves the
 * focus there -- not onto the button behind the dimmer -- and the confirm closes the dialog once with that button's result.
 */
bool FDreamWorldDialogPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPanelDialog Placed = PlaceDialogOnPanel(*this, Rig, Listener.Get(), BehindListener.Get());
	UDreamButton* Confirm = Placed.IsReady() ? ButtonAnswering(Placed.Dialog, TEXT("Confirm")) : nullptr;
	FName OtherResult;
	UDreamButton* Other = Placed.IsReady() ? OtherButton(Placed.Dialog, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a confirm button"), Confirm) || !TestNotNull(TEXT("...and another"), Other))
	{
		return false;
	}
	if (!TestTrue(TEXT("The dialog on the panel came up with the focus on its default button"), FocusIsIn(Rig, Confirm)))
	{
		return false;
	}

	TestTrue(TEXT("Pushing the stick toward the other button completes"), Rig.Driver()->Sequence().Navigate(Toward(Rig, Confirm, Other)).Perform());
	TestTrue(TEXT("The stick moved the focus onto the other button"), FocusIsIn(Rig, Other));
	TestFalse(TEXT("...not onto the button behind the dimmer"), FocusIsIn(Rig, Placed.Behind));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
	if (TestEqual(TEXT("The confirm closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("...with the result of the button the pad was on"), Listener->DialogClosedResults[0], OtherResult);
	}
	TestEqual(TEXT("The button behind was not clicked"), BehindListener->ClickedCount, 0);
	return true;
}

#endif
