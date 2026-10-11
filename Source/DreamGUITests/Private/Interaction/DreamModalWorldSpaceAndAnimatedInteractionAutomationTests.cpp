// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIModal.h"

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
 * A MODAL OVER A PANEL IN THE LEVEL, AND A FINGER ON A MODAL AS SHIPPED.
 *
 * The UMG arrangement is CommonUI's modal layer: while a modal is up only it takes input. It is a layer of the player's own
 * screen, in front of everything the viewport shows -- and a widget component in the level is something the viewport shows,
 * reached through the game viewport only after the screen's own widgets have been hit-tested (Slate hit-tests the viewport's
 * widgets before the game viewport ever sees the pointer). So while the modal is up a click or a finger on a panel in the
 * level reaches nothing there, and once its question is answered the panel answers again. DreamGUI says the same in its own
 * words: the modal layer is a full-screen scrim carrying a blocker, and the screen raycasters' hits are taken ahead of every
 * world-space hit (UDreamPointerInputModule::LineTrace).
 *
 * The focus: a modal takes the pad's focus as it comes up, wherever it was, and gives it back to what had it -- here a button
 * on a panel -- when it closes over a screen that asks for none of its own (UDreamUINavigationStack, the focus before the
 * push; DreamGUI.Modal.ClosingAModalOverAPageWithoutAScopeReturnsFocusToItsOpener on the screen).
 */
namespace DreamModalWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** What the modal answered. Declared before the rig, so an answer a teardown gives lands somewhere alive. */
	struct FAnswers
	{
		TArray<FName> Results;
	};

	/** A dialog brought up as a modal for player 0, answering into InAnswers. Null, having said why, when it did not come up. */
	UDreamDialog* ShowModal(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FAnswers& InAnswers)
	{
		UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(InRig.GetWorld());
		if (!InTest.TestNotNull(TEXT("The rig's world has the modal subsystem"), Modals))
		{
			return nullptr;
		}
		FAnswers* Answers = &InAnswers;
		Modals->ShowModalNative(UDreamDialog::StaticClass(), [Answers](FName InResult) { Answers->Results.Add(InResult); }, 0);
		InRig.PumpFrames(2);
		UDreamDialog* Modal = Cast<UDreamDialog>(Modals->GetActiveModalWidget(0));
		return InTest.TestNotNull(TEXT("The modal came up"), Modal)
			&& InTest.TestEqual(TEXT("...with its two buttons"), Modal->ButtonWidgets.Num(), 2) ? Modal : nullptr;
	}

	/** The modal's button that is not its default, and its result. */
	UDreamButton* OtherButton(const UDreamDialog* InDialog, FName& OutResult)
	{
		const TArray<FDreamDialogButton> Specs = InDialog->GetButtons();
		const UDreamButton* Default = InDialog->GetDefaultButton();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (InDialog->ButtonWidgets.IsValidIndex(Index) && InDialog->ButtonWidgets[Index].Get() != Default)
			{
				OutResult = Specs[Index].Result;
				return InDialog->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}

	/** Which way the D-pad goes from InFrom to InTo, which sit side by side in the dialog's row. */
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
	FDreamAnimatedModalTapTest,
	"DreamGUI.Modal.WhileAModalIsUpATapOnThePageBehindReachesNothingAndATapOnItsButtonAnswersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedModalTapTest, "DreamGUI.Modal.WhileAModalIsUpATapOnThePageBehindReachesNothingAndATapOnItsButtonAnswersIt", "[Touch][Animated]")

/*
 * As shipped on the screen: a button on the page and a modal brought up over it. A finger tapped where the page's button is
 * reaches nothing and leaves the modal up; a finger tapped on the modal's button that is not its default answers it with that
 * button's result, once. With the modal gone, a tap on the page's button clicks it.
 */
bool FDreamAnimatedModalTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalWorldTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamButton* Behind = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Behind"), nullptr, FVector2D(200.0, 50.0), FVector2D(0.0, -140.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a button on the page came up"), Behind))
	{
		return false;
	}
	Behind->OnClicked.AddDynamic(BehindListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);
	UDreamDialog* Modal = ShowModal(*this, Rig, Answers);
	FName OtherResult;
	UDreamButton* Other = Modal != nullptr ? OtherButton(Modal, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The modal has a button besides its default"), Other))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
	FDreamElementRef BehindElement = Rig.Driver()->Find(FDreamBy::Widget(Behind));

	TestTrue(TEXT("A finger tapped where the page's button is completes"), BehindElement->Tap(0));
	TestEqual(TEXT("While the modal is up the tap reached nothing behind it"), BehindListener->ClickedCount, 0);
	TestEqual(TEXT("...and the modal is still up"), Modals->GetModalDepth(0), 1);

	TestTrue(TEXT("A finger tapped on the modal's other button completes"), Rig.Driver()->Find(FDreamBy::Widget(Other))->Tap(1));
	Rig.PumpFrames(1);
	if (TestEqual(TEXT("The tap answered the modal once"), Answers.Results.Num(), 1))
	{
		TestEqual(TEXT("...with the tapped button's result"), Answers.Results[0], OtherResult);
	}
	TestFalse(TEXT("...and the modal is gone"), Modals->IsModalActive(0));

	TestTrue(TEXT("A finger tapped on the page's button completes"), BehindElement->Tap(2));
	TestEqual(TEXT("With the modal gone the page's button takes the tap"), BehindListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldModalPressTest,
	"DreamGUI.Modal.WhileAModalIsUpAClickOrATapOnAWorldPanelReachesNothingUntilItIsAnswered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldModalPressTest, "DreamGUI.Modal.WhileAModalIsUpAClickOrATapOnAWorldPanelReachesNothingUntilItIsAnswered", "[Pointer][Touch][World]")

/*
 * A button on a panel in the level, and a modal brought up on the player's screen. A click and a finger's tap through the
 * world pointer where the panel's button is seen reach nothing, and the modal is still up. The modal answered with a click on
 * its default button, the same click on the panel's button clicks it, and so does the same tap.
 */
bool FDreamWorldModalPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalWorldTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> PanelListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* OnPanel = Stage.IsReady() ? Rig.MakeControl<UDreamButton>(TEXT("OnPanel"), Stage.Panel, FVector2D(160.0, 60.0), FVector2D(-150.0, 80.0)) : nullptr;
	if (!TestNotNull(TEXT("A button on the panel came up"), OnPanel))
	{
		return false;
	}
	OnPanel->OnClicked.AddDynamic(PanelListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(2);
	FDreamElementRef PanelElement = Rig.Driver()->Find(FDreamBy::Widget(OnPanel));
	TestTrue(TEXT("Before any modal, a click on the panel's button through the world pointer clicks it"), PanelElement->Click());
	if (!TestEqual(TEXT("...once"), PanelListener->ClickedCount, 1))
	{
		return false;
	}
	UDreamDialog* Modal = ShowModal(*this, Rig, Answers);
	UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
	if (!TestNotNull(TEXT("The modal has a default button"), Default))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());

	TestTrue(TEXT("Clicking where the panel's button is seen completes"), PanelElement->Click());
	TestTrue(TEXT("A finger tapped there completes"), PanelElement->Tap(0));
	TestEqual(TEXT("While the modal is up neither reached the panel"), PanelListener->ClickedCount, 1);
	TestEqual(TEXT("...and the modal is still up"), Modals->GetModalDepth(0), 1);
	TestEqual(TEXT("...unanswered"), Answers.Results.Num(), 0);

	TestTrue(TEXT("Clicking the modal's default button completes"), Rig.Driver()->Find(FDreamBy::Widget(Default))->Click());
	Rig.PumpFrames(1);
	TestEqual(TEXT("The click answered the modal once"), Answers.Results.Num(), 1);
	TestFalse(TEXT("...and the modal is gone"), Modals->IsModalActive(0));

	TestTrue(TEXT("Clicking the panel's button again completes"), PanelElement->Click());
	TestEqual(TEXT("With the modal gone the click reaches the panel's button"), PanelListener->ClickedCount, 2);
	TestTrue(TEXT("A finger tapped on it completes"), PanelElement->Tap(1));
	TestEqual(TEXT("...and so does a tap"), PanelListener->ClickedCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldModalPadTest,
	"DreamGUI.Modal.AModalTakesThePadsFocusFromAButtonOnAWorldPanelAndGivesItBackWhenItIsAnswered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldModalPadTest, "DreamGUI.Modal.AModalTakesThePadsFocusFromAButtonOnAWorldPanelAndGivesItBackWhenItIsAnswered", "[Nav][World]")

/*
 * The pad's focus on a button on a panel, then a modal brought up. The focus is in the modal; the D-pad moves it from the
 * modal's default button onto the other -- never out onto the panel -- and the confirm answers with that button's result.
 * The modal gone, the focus is back on the panel's button, and the pad's confirm clicks it.
 */
bool FDreamWorldModalPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalWorldTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> PanelListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* OnPanel = Stage.IsReady() ? Rig.MakeControl<UDreamButton>(TEXT("OnPanel"), Stage.Panel, FVector2D(160.0, 60.0), FVector2D(-150.0, 80.0)) : nullptr;
	if (!TestTrue(TEXT("A button on the panel came up"), OnPanel != nullptr && OnPanel->FaceNode != nullptr))
	{
		return false;
	}
	OnPanel->OnClicked.AddDynamic(PanelListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("Player 0's focus can be put on the panel's button"), PutFocusOn(Rig, OnPanel->FaceNode)))
	{
		return false;
	}
	UDreamDialog* Modal = ShowModal(*this, Rig, Answers);
	UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
	FName OtherResult;
	UDreamButton* Other = Modal != nullptr ? OtherButton(Modal, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The modal has a default button"), Default) || !TestNotNull(TEXT("...and another"), Other))
	{
		return false;
	}
	if (!TestTrue(TEXT("The modal took the focus into itself, off the panel"), FocusIsIn(Rig, Modal)))
	{
		return false;
	}
	if (!FocusIsIn(Rig, Default))
	{
		// Where the dialog puts it as it appears, or put there the way a directional move would: what follows starts from it.
		TestTrue(TEXT("The focus can be put on the modal's default button"), PutFocusOn(Rig, Default->FaceNode));
	}

	TestTrue(TEXT("Pushing the stick toward the other button completes"), Rig.Driver()->Sequence().Navigate(Toward(Rig, Default, Other)).Perform());
	TestTrue(TEXT("The stick moved the focus onto the modal's other button"), FocusIsIn(Rig, Other));
	TestFalse(TEXT("...and not out onto the panel"), FocusIsIn(Rig, OnPanel));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
	Rig.PumpFrames(1);
	if (TestEqual(TEXT("The confirm answered the modal once"), Answers.Results.Num(), 1))
	{
		TestEqual(TEXT("...with the result of the button the pad was on"), Answers.Results[0], OtherResult);
	}
	TestEqual(TEXT("The panel's button was not clicked by any of it"), PanelListener->ClickedCount, 0);

	TestTrue(TEXT("The modal gone, the focus is back on the panel's button"), FocusIsIn(Rig, OnPanel));
	TestTrue(TEXT("Pressing and releasing the pad's confirm again completes"), PadConfirm(Rig));
	TestEqual(TEXT("...and the confirm clicked the panel's button"), PanelListener->ClickedCount, 1);
	return true;
}

#endif
