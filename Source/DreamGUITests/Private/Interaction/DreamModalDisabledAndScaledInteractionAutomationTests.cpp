// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamUIModal.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A MODAL WITH A DISABLED BUTTON, AND ON A SCALED CANVAS.
 *
 * The UMG arrangement is CommonUI's modal layer (see the modal stack's own interaction tests): while a modal is up only it
 * takes input. Its buttons are buttons, so a disabled one is a disabled SButton -- Slate's hit test cuts a click's or a
 * finger's path at it (FHittestGrid::GetBubblePath) and the D-pad passes over it (FHittestGrid::FindNextFocusableWidget) --
 * and a disabled button in the modal does not open a way through to the page behind it: the click still lands on the modal,
 * and the D-pad stays in it. Once its question is answered, the page answers again.
 *
 * On a canvas scaled to half and to double, the modal still covers the page where the page is drawn, its buttons take the
 * click, the tap and the D-pad where they are drawn, and the page answers once the modal has gone.
 */
namespace DreamModalCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	/** What the modal answered. Declared before the rig, so an answer a teardown gives lands somewhere alive. */
	struct FAnswers
	{
		TArray<FName> Results;
	};

	/**
	 * A button on the page, its clicks heard by InBehindListener, and a dialog brought up over it as a modal for player 0,
	 * answering into InAnswers. Null, having said why, when either did not come up.
	 */
	UDreamDialog* ShowModalOverPage(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FAnswers& InAnswers,
		UDreamPressInteractionListener* InBehindListener, UDreamButton*& OutBehind)
	{
		UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(InRig.GetWorld());
		OutBehind = InRig.MakeControl<UDreamButton>(TEXT("Behind"), nullptr, FVector2D(200.0, 50.0), FVector2D(0.0, -140.0));
		if (!InTest.TestNotNull(TEXT("The rig's world has the modal subsystem"), Modals)
			|| !InTest.TestNotNull(TEXT("A button on the page came up"), OutBehind))
		{
			return nullptr;
		}
		OutBehind->OnClicked.AddDynamic(InBehindListener, &UDreamPressInteractionListener::HandleClicked);
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
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

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	/**
	 * The focus on the modal's default button: where the dialog puts it as it appears, or else put there the way a directional
	 * move would -- what these tests are about starts from it, wherever the modal layer leaves the focus on opening.
	 */
	bool FocusOnDefault(FDreamDriverRig& InRig, UDreamButton* InDefault)
	{
		if (IsPartOf(FocusOf(InRig), InDefault))
		{
			return true;
		}
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr && InDefault != nullptr && Services->FocusForNavigation(InDefault->FaceNode, 0);
	}

	EDreamUINavigationDirection Toward(FDreamDriverRig& InRig, UDreamButton* InFrom, UDreamButton* InTo)
	{
		const TOptional<FVector2D> FromAt = InRig.Driver()->Find(FDreamBy::Widget(InFrom))->GetCentrePixel();
		const TOptional<FVector2D> ToAt = InRig.Driver()->Find(FDreamBy::Widget(InTo))->GetCentrePixel();
		if (!FromAt.IsSet() || !ToAt.IsSet())
		{
			return EDreamUINavigationDirection::None;
		}
		return ToAt->X < FromAt->X ? EDreamUINavigationDirection::Left : EDreamUINavigationDirection::Right;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageModalDisabledPressTest,
	"DreamGUI.Modal.AClickOrATapOnTheModalsDisabledButtonAnswersNothingAndThePageBehindStaysOutOfReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageModalDisabledPressTest, "DreamGUI.Modal.AClickOrATapOnTheModalsDisabledButtonAnswersNothingAndThePageBehindStaysOutOfReach", "[Pointer][Touch][Disabled]")

/*
 * The modal's default button disabled: a click and a finger's tap on it answer nothing, the modal is still up, and a click on
 * the page's button behind it reaches nothing. Enabled, the click answers the modal; then the page's button takes a click.
 */
bool FDreamCoverageModalDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalCoverageTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Behind = nullptr;
	UDreamDialog* Modal = Rig.IsUsable() ? ShowModalOverPage(*this, Rig, Answers, BehindListener.Get(), Behind) : nullptr;
	UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
	if (!TestNotNull(TEXT("The modal has a default button"), Default))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
	Default->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef DefaultElement = Rig.Driver()->Find(FDreamBy::Widget(Default));

	TestTrue(TEXT("Clicking the modal's disabled button completes"), DefaultElement->Click());
	TestTrue(TEXT("A finger tapped on it completes"), DefaultElement->Tap(0));
	TestEqual(TEXT("Neither answered the modal"), Answers.Results.Num(), 0);
	TestEqual(TEXT("...which is still up"), Modals->GetModalDepth(0), 1);
	TestTrue(TEXT("Clicking where the page's button is completes"), Rig.Driver()->Find(FDreamBy::Widget(Behind))->Click());
	TestEqual(TEXT("A disabled button in the modal opened no way through to the page"), BehindListener->ClickedCount, 0);

	Default->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the modal's button once it is enabled completes"), DefaultElement->Click());
	Rig.PumpFrames(1);
	TestEqual(TEXT("Enabled, the click answered the modal once"), Answers.Results.Num(), 1);
	TestFalse(TEXT("...and the modal is gone"), Modals->IsModalActive(0));
	TestTrue(TEXT("Clicking the page's button completes"), Rig.Driver()->Find(FDreamBy::Widget(Behind))->Click());
	TestEqual(TEXT("With the modal gone the page's button takes the click"), BehindListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageModalDisabledPadTest,
	"DreamGUI.Modal.TheDPadPassesOverTheModalsDisabledButtonWithoutLeavingTheModalAndConfirmAnswersWithTheButtonItIsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageModalDisabledPadTest, "DreamGUI.Modal.TheDPadPassesOverTheModalsDisabledButtonWithoutLeavingTheModalAndConfirmAnswersWithTheButtonItIsOn", "[Nav][Disabled]")

/*
 * The modal up, the focus moved from its default button onto the other, the default then disabled. The D-pad back toward it
 * finds nothing to stop on: the focus stays on the other button -- not on the page's button behind -- and the confirm
 * answers with the other button's result. The page's button was never clicked.
 */
bool FDreamCoverageModalDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalCoverageTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Behind = nullptr;
	UDreamDialog* Modal = Rig.IsUsable() ? ShowModalOverPage(*this, Rig, Answers, BehindListener.Get(), Behind) : nullptr;
	UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
	FName OtherResult;
	UDreamButton* Other = Modal != nullptr ? OtherButton(Modal, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The modal has a default button"), Default) || !TestNotNull(TEXT("...and another"), Other)
		|| !TestTrue(TEXT("The focus is on the modal's default button"), FocusOnDefault(Rig, Default)))
	{
		return false;
	}
	const EDreamUINavigationDirection TowardOther = Toward(Rig, Default, Other);
	const EDreamUINavigationDirection TowardDefault = Toward(Rig, Other, Default);
	TestTrue(TEXT("Pushing the stick toward the other button completes"), Rig.Driver()->Sequence().Navigate(TowardOther).Perform());
	if (!TestTrue(TEXT("The focus is on the other button"), IsPartOf(FocusOf(Rig), Other)))
	{
		return false;
	}
	Default->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Pushing the stick back toward the disabled button completes"), Rig.Driver()->Sequence().Navigate(TowardDefault).Perform());
	TestTrue(TEXT("The focus stayed on the other button"), IsPartOf(FocusOf(Rig), Other));
	TestFalse(TEXT("...and did not go out to the page behind"), IsPartOf(FocusOf(Rig), Behind));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	if (TestEqual(TEXT("The confirm answered the modal once"), Answers.Results.Num(), 1))
	{
		TestEqual(TEXT("...with the button the focus stayed on"), Answers.Results[0], OtherResult);
	}
	TestEqual(TEXT("The page's button was never clicked"), BehindListener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageModalScaledPressTest,
	"DreamGUI.Modal.OnACanvasScaledToHalfAndToDoubleAClickOrATapReachesTheModalAndNotThePageBehindItUntilItIsAnswered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageModalScaledPressTest, "DreamGUI.Modal.OnACanvasScaledToHalfAndToDoubleAClickOrATapReachesTheModalAndNotThePageBehindItUntilItIsAnswered", "[Pointer][Touch][Scaled]")

/*
 * At half and at double: with the modal up, a click and a tap where the page's button is drawn reach nothing; a click on the
 * modal's default button answers it; then a tap on the page's button clicks it.
 */
bool FDreamCoverageModalScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FAnswers Answers;
		TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		UDreamButton* Behind = nullptr;
		UDreamDialog* Modal = ComeUp(*this, Rig, Scale) ? ShowModalOverPage(*this, Rig, Answers, BehindListener.Get(), Behind) : nullptr;
		UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
		if (!TestNotNull(*At(Scale, TEXT("The modal has a default button")), Default))
		{
			return false;
		}
		UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
		FDreamElementRef BehindElement = Rig.Driver()->Find(FDreamBy::Widget(Behind));

		TestTrue(At(Scale, TEXT("Clicking where the page's button is completes")), BehindElement->Click());
		TestTrue(At(Scale, TEXT("A finger tapped there completes")), BehindElement->Tap(0));
		TestEqual(*At(Scale, TEXT("With the modal up the page's button took neither")), BehindListener->ClickedCount, 0);
		TestEqual(*At(Scale, TEXT("...and the modal was not answered")), Answers.Results.Num(), 0);

		TestTrue(At(Scale, TEXT("Clicking the modal's default button completes")), Rig.Driver()->Find(FDreamBy::Widget(Default))->Click());
		Rig.PumpFrames(1);
		TestEqual(*At(Scale, TEXT("The click answered the modal once")), Answers.Results.Num(), 1);
		TestFalse(At(Scale, TEXT("...and it is gone")), Modals->IsModalActive(0));
		TestTrue(At(Scale, TEXT("A finger tapped on the page's button completes")), BehindElement->Tap(1));
		TestEqual(*At(Scale, TEXT("With the modal gone the page's button took the tap")), BehindListener->ClickedCount, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageModalScaledPadTest,
	"DreamGUI.Modal.OnACanvasScaledToHalfAndToDoubleThePadMovesBetweenTheModalsButtonsAndConfirmAnswersWithTheOneItIsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageModalScaledPadTest, "DreamGUI.Modal.OnACanvasScaledToHalfAndToDoubleThePadMovesBetweenTheModalsButtonsAndConfirmAnswersWithTheOneItIsOn", "[Nav][Scaled]")

/* At half and at double: the stick from the default button toward the other moves the focus there, and the confirm answers with it. */
bool FDreamCoverageModalScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamModalCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FAnswers Answers;
		TStrongObjectPtr<UDreamPressInteractionListener> BehindListener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		UDreamButton* Behind = nullptr;
		UDreamDialog* Modal = ComeUp(*this, Rig, Scale) ? ShowModalOverPage(*this, Rig, Answers, BehindListener.Get(), Behind) : nullptr;
		UDreamButton* Default = Modal != nullptr ? Modal->GetDefaultButton() : nullptr;
		FName OtherResult;
		UDreamButton* Other = Modal != nullptr ? OtherButton(Modal, OtherResult) : nullptr;
		if (!TestNotNull(*At(Scale, TEXT("The modal has a default button")), Default) || !TestNotNull(*At(Scale, TEXT("...and another")), Other)
			|| !TestTrue(At(Scale, TEXT("The focus is on the modal's default button")), FocusOnDefault(Rig, Default)))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("Pushing the stick toward the other button completes")), Rig.Driver()->Sequence().Navigate(Toward(Rig, Default, Other)).Perform());
		if (!TestTrue(At(Scale, TEXT("The stick moved the focus onto the other button")), IsPartOf(FocusOf(Rig), Other)))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Pressing and releasing the pad's confirm completes")),
			Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		if (TestEqual(*At(Scale, TEXT("The confirm answered the modal once")), Answers.Results.Num(), 1))
		{
			TestEqual(*At(Scale, TEXT("...with the button the pad was on")), Answers.Results[0], OtherResult);
		}
		TestEqual(*At(Scale, TEXT("The page's button was never clicked")), BehindListener->ClickedCount, 0);
	}
	return true;
}

#endif
