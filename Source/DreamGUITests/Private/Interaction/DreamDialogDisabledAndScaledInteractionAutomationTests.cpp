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

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A STANDALONE DIALOG WITH A DISABLED BUTTON, AND ON A SCALED CANVAS.
 *
 * A dialog's buttons are buttons (see the dialog's own interaction tests for the arrangement, and why the dialog is given
 * the whole of its screen). A disabled one is a disabled SButton: Slate's hit test cuts a click's or a finger's path at it
 * (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp), so it answers nothing and the dialog stays up;
 * the D-pad passes over it (FHittestGrid::FindNextFocusableWidget skips a widget that is not enabled), so the pad's confirm
 * answers with the button it stayed on. Enabled again, it answers.
 *
 * On a canvas scaled to half and to double, the click and the tap answer with the button they land on where it is drawn,
 * and the D-pad moves between the dialog's buttons by where they are drawn.
 */
namespace DreamDialogCoverageTestLocal
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

	/**
	 * The dialog with its seeded pair of buttons (Cancel, then OK answering "Confirm"), its events going to InListener, given
	 * the whole of the rig's root -- in the root's own units, which on a scaled canvas are not the viewport's pixels.
	 */
	UDreamDialog* PlaceDialog(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		const FVector2D RootSize(InRig.Root()->GetWidth(), InRig.Root()->GetHeight());
		UDreamDialog* Dialog = InRig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr, RootSize);
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

	/** The dialog's button that is not the Confirm one, and its result. */
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

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	/** Which way the D-pad goes from InFrom to InTo, which sit side by side in the dialog's row. */
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
	FDreamCoverageDialogDisabledPressTest,
	"DreamGUI.Dialog.AClickOrATapOnADisabledButtonAnswersNothingAndLeavesTheDialogUpUntilTheButtonIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDialogDisabledPressTest, "DreamGUI.Dialog.AClickOrATapOnADisabledButtonAnswersNothingAndLeavesTheDialogUpUntilTheButtonIsEnabled", "[Pointer][Touch][Disabled]")

/*
 * The confirm button disabled: a click on it and a finger's tap on it answer nothing, and the dialog is still up. Enabled,
 * the click answers Confirm and the dialog closes once.
 */
bool FDreamCoverageDialogDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogCoverageTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamDialog* Dialog = Rig.IsUsable() ? PlaceDialog(*this, Rig, Listener.Get()) : nullptr;
	UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a button answering Confirm"), Confirm))
	{
		return false;
	}
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Confirm->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef ConfirmElement = Rig.Driver()->Find(FDreamBy::Widget(Confirm));

	TestTrue(TEXT("Clicking the disabled confirm button completes"), ConfirmElement->Click());
	TestTrue(TEXT("A finger tapped on it completes"), ConfirmElement->Tap(0));
	TestEqual(TEXT("Neither answered anything"), Listener->DialogButtonResults.Num(), 0);
	TestEqual(TEXT("...nor closed the dialog"), Listener->DialogClosedResults.Num(), 0);
	TestTrue(TEXT("The dialog is still up"), Dialog->GetWidgetActive());

	Confirm->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking it once it is enabled completes"), ConfirmElement->Click());
	if (TestEqual(TEXT("Enabled, the click closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("...with Confirm"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDialogDisabledPadTest,
	"DreamGUI.Dialog.TheDPadPassesOverADisabledButtonSoThePadsConfirmAnswersWithTheButtonItStayedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDialogDisabledPadTest, "DreamGUI.Dialog.TheDPadPassesOverADisabledButtonSoThePadsConfirmAnswersWithTheButtonItStayedOn", "[Nav][Disabled]")

/*
 * The pad moved from the default button onto the other one, then the default disabled. The D-pad back toward it finds
 * nothing it can stop on and the focus stays where it was; the confirm answers with that button's result.
 */
bool FDreamCoverageDialogDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogCoverageTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamDialog* Dialog = Rig.IsUsable() ? PlaceDialog(*this, Rig, Listener.Get()) : nullptr;
	UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
	FName OtherResult;
	UDreamButton* Other = Dialog != nullptr ? OtherButton(Dialog, OtherResult) : nullptr;
	if (!TestNotNull(TEXT("The dialog has a confirm button"), Confirm) || !TestNotNull(TEXT("...and another"), Other)
		|| !TestTrue(TEXT("The dialog opened with the focus on its default button"), IsPartOf(FocusOf(Rig), Confirm)))
	{
		return false;
	}
	const EDreamUINavigationDirection TowardOther = Toward(Rig, Confirm, Other);
	const EDreamUINavigationDirection TowardConfirm = Toward(Rig, Other, Confirm);
	TestTrue(TEXT("Pushing the stick toward the other button completes"), Rig.Driver()->Sequence().Navigate(TowardOther).Perform());
	if (!TestTrue(TEXT("The focus is on the other button"), IsPartOf(FocusOf(Rig), Other)))
	{
		return false;
	}
	Confirm->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Pushing the stick back toward the disabled confirm button completes"), Rig.Driver()->Sequence().Navigate(TowardConfirm).Perform());
	TestTrue(TEXT("The D-pad passed the disabled button over and the focus stayed on the other"), IsPartOf(FocusOf(Rig), Other));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	if (TestEqual(TEXT("The confirm closed the dialog once"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("...with the result of the button the focus stayed on"), Listener->DialogClosedResults[0], OtherResult);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDialogScaledPressTest,
	"DreamGUI.Dialog.OnACanvasScaledToHalfAndToDoubleAClickOrATapAnswersWithTheButtonItLandsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDialogScaledPressTest, "DreamGUI.Dialog.OnACanvasScaledToHalfAndToDoubleAClickOrATapAnswersWithTheButtonItLandsOn", "[Pointer][Touch][Scaled]")

/*
 * At half and at double, two dialogs one after the other: a click on the first's confirm closes it with Confirm, and a
 * finger's tap on the second's other button closes it with that button's result.
 */
bool FDreamCoverageDialogScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogCoverageTestLocal;
	for (const double Scale : Scales)
	{
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
			UDreamDialog* Dialog = ComeUp(*this, Rig, Scale) ? PlaceDialog(*this, Rig, Listener.Get()) : nullptr;
			UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
			if (!TestNotNull(*At(Scale, TEXT("The dialog has a button answering Confirm")), Confirm))
			{
				return false;
			}
			TestTrue(At(Scale, TEXT("Clicking the confirm button completes")), Rig.Driver()->Find(FDreamBy::Widget(Confirm))->Click());
			if (TestEqual(*At(Scale, TEXT("The click closed the dialog once")), Listener->DialogClosedResults.Num(), 1))
			{
				TestEqual(*At(Scale, TEXT("...with Confirm")), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
			}
		}
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
			UDreamDialog* Dialog = ComeUp(*this, Rig, Scale) ? PlaceDialog(*this, Rig, Listener.Get()) : nullptr;
			FName OtherResult;
			UDreamButton* Other = Dialog != nullptr ? OtherButton(Dialog, OtherResult) : nullptr;
			if (!TestNotNull(*At(Scale, TEXT("The dialog has a button besides Confirm")), Other))
			{
				return false;
			}
			TestTrue(At(Scale, TEXT("A finger tapped on the other button completes")), Rig.Driver()->Find(FDreamBy::Widget(Other))->Tap(0));
			if (TestEqual(*At(Scale, TEXT("The tap closed the dialog once")), Listener->DialogClosedResults.Num(), 1))
			{
				TestEqual(*At(Scale, TEXT("...with the tapped button's result")), Listener->DialogClosedResults[0], OtherResult);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageDialogScaledPadTest,
	"DreamGUI.Dialog.OnACanvasScaledToHalfAndToDoubleThePadMovedOntoTheOtherButtonAndConfirmedAnswersWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageDialogScaledPadTest, "DreamGUI.Dialog.OnACanvasScaledToHalfAndToDoubleThePadMovedOntoTheOtherButtonAndConfirmedAnswersWithIt", "[Nav][Scaled]")

/* At half and at double: from the default button the stick toward the other one moves the focus there, and the confirm answers with it. */
bool FDreamCoverageDialogScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamDialogCoverageTestLocal;
	for (const double Scale : Scales)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		UDreamDialog* Dialog = ComeUp(*this, Rig, Scale) ? PlaceDialog(*this, Rig, Listener.Get()) : nullptr;
		UDreamButton* Confirm = Dialog != nullptr ? ButtonAnswering(Dialog, TEXT("Confirm")) : nullptr;
		FName OtherResult;
		UDreamButton* Other = Dialog != nullptr ? OtherButton(Dialog, OtherResult) : nullptr;
		if (!TestNotNull(*At(Scale, TEXT("The dialog has a confirm button")), Confirm) || !TestNotNull(*At(Scale, TEXT("...and another")), Other)
			|| !TestTrue(At(Scale, TEXT("The dialog opened with the focus on its default button")), IsPartOf(FocusOf(Rig), Confirm)))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("Pushing the stick toward the other button completes")), Rig.Driver()->Sequence().Navigate(Toward(Rig, Confirm, Other)).Perform());
		if (!TestTrue(At(Scale, TEXT("The stick moved the focus onto the other button")), IsPartOf(FocusOf(Rig), Other)))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("Pressing and releasing the pad's confirm completes")),
			Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
		if (TestEqual(*At(Scale, TEXT("The confirm closed the dialog once")), Listener->DialogClosedResults.Num(), 1))
		{
			TestEqual(*At(Scale, TEXT("...with the result of the button the pad was on")), Listener->DialogClosedResults[0], OtherResult);
		}
	}
	return true;
}

#endif
