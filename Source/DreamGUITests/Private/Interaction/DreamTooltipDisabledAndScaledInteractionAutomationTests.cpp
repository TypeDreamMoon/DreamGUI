// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUITooltip.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * TOOLTIPS ON A DISABLED BUTTON, AND ON A SCALED CANVAS.
 *
 * Slate looks for a tooltip under the cursor INCLUDING disabled widgets: FSlateUser::UpdateTooltip locates the widgets under
 * the cursor with bIgnoreEnabledStatus true ("including disabled widgets", Slate/Private/Framework/Application/SlateUser.cpp:
 * 1350-1351) -- which is why CommonUI's buttons carry a DisabledTooltipText. So the mouse resting on a disabled button brings
 * its bubble up after the delay. A finger is no cursor and brings none, disabled or not (the finger's own tests next door).
 *
 * DreamGUI also brings a bubble up for the widget the gamepad's focus lands on, hung off the widget's corner rather than off
 * a pointer. Slate has no such bubble; what can be asked of it is what navigation itself does: the D-pad passes over a
 * disabled widget (FHittestGrid::FindNextFocusableWidget), so no bubble comes up for one, and the bubble comes up for the
 * widget the focus does land on.
 *
 * On a canvas scaled to half and to double: the bubble comes up beside the pointer and moves with it by the pixels the pointer
 * moved; a tap where it is drawn reaches what is under it; and a bubble the D-pad brings up hangs off the focused button where
 * it is drawn.
 */
namespace DreamTooltipCoverageTestLocal
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

	/** A button with a tooltip, InSize at InPosition in canvas units, its clicks heard by InListener when there is one. */
	UDreamButton* MakeHelpButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InSize, const FVector2D& InPosition,
		UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, InSize, InPosition);
		if (Button != nullptr)
		{
			Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("About %s"), InName)));
			if (InListener != nullptr)
			{
				Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			}
		}
		return Button;
	}

	/** Long enough for any bubble to have come up three times over, and never no time at all. */
	float LongerThanTheDelay()
	{
		return 3.0f * FMath::Max(UDreamGUISettings::Get()->TooltipDelaySeconds, 0.1f);
	}

	/** Frames until player 0's tooltip is up for InWidget, for twice the delay and a second more. */
	bool WaitForTooltipOf(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const UDreamWidget* InWidget, const FString& InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * UDreamGUISettings::Get()->TooltipDelaySeconds + 1.0);
		return InRig.Driver()->Wait(
			FDreamUntil::Condition([InTooltip, InWidget]() { return InTooltip->GetShownForUser(0) == InWidget; }, Timeout),
			Timeout, InWhat);
	}

	bool HasNoBubble(const UDreamUITooltipSubsystem* InTooltip)
	{
		return InTooltip->GetBubbleForUser(0) == nullptr && InTooltip->GetShownForUser(0) == nullptr;
	}

	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const FString& InWhat)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(InWhat, Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipDisabledMouseTest,
	"DreamGUI.Tooltip.TheMouseRestingOnADisabledButtonBringsItsBubbleUpAfterTheDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipDisabledMouseTest, "DreamGUI.Tooltip.TheMouseRestingOnADisabledButtonBringsItsBubbleUpAfterTheDelay", "[Pointer][Disabled]")

/*
 * A disabled button with a tooltip: the mouse moves onto it and rests. Slate shows a disabled widget's tooltip as it shows an
 * enabled one's, so its bubble comes up -- while a click there clicks nothing.
 */
bool FDreamCoverageTooltipDisabledMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	UDreamButton* Help = Tooltip != nullptr ? MakeHelpButton(Rig, TEXT("Help"), FVector2D(200.0, 60.0), FVector2D::ZeroVector, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig, its tooltip service and a button with a tooltip came up"), Help != nullptr))
	{
		return false;
	}
	Help->SetIsEnabled(false);
	Rig.PumpFrames(2);

	TestTrue(TEXT("The mouse moving onto the disabled button completes"), Rig.Driver()->Find(FDreamBy::Widget(Help))->Hover());
	TestTrue(TEXT("The mouse resting there brings the disabled button's bubble up"),
		WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the disabled button's tooltip under the mouse")));
	TestNotNull(TEXT("...a bubble that is drawn"), Tooltip->GetBubbleForUser(0));
	TestEqual(TEXT("The button was not clicked by any of it"), Listener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipDisabledFingerTest,
	"DreamGUI.Tooltip.AFingerTappingOrRestingOnADisabledButtonWithATooltipBringsNoBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipDisabledFingerTest, "DreamGUI.Tooltip.AFingerTappingOrRestingOnADisabledButtonWithATooltipBringsNoBubbleUp", "[Touch][Disabled]")

/*
 * A disabled button with a tooltip. A tap on it, then a finger held on it past the delay: neither brings a bubble up -- a
 * finger is no cursor, and the button being disabled, which lets the mouse bring its bubble up, does not let a finger.
 */
bool FDreamCoverageTooltipDisabledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	UDreamButton* Help = Tooltip != nullptr ? MakeHelpButton(Rig, TEXT("Help"), FVector2D(200.0, 60.0), FVector2D::ZeroVector, nullptr) : nullptr;
	if (!TestTrue(TEXT("The rig, its tooltip service and a button with a tooltip came up"), Help != nullptr))
	{
		return false;
	}
	Help->SetIsEnabled(false);
	Rig.PumpFrames(2);
	const TOptional<FVector2D> OnHelp = Rig.Driver()->Find(FDreamBy::Widget(Help))->GetCentrePixel();
	if (!TestTrue(TEXT("The button is on the viewport"), OnHelp.IsSet()))
	{
		return false;
	}
	const float Longer = LongerThanTheDelay();

	TestTrue(TEXT("A tap on the disabled button and a wait past the delay complete"),
		Rig.Driver()->Sequence().TouchDown(0, OnHelp.GetValue()).TouchUp(0).WaitSeconds(Longer).Perform());
	TestTrue(TEXT("The tap brought no bubble up"), HasNoBubble(Tooltip));
	TestTrue(TEXT("A finger held on the disabled button past the delay, then lifted, completes"), Rig.Driver()->Sequence()
		.TouchDown(1, OnHelp.GetValue())
		.WaitSeconds(Longer)
		.Then([this, Tooltip](FDreamDriverContext&)
		{
			TestTrue(TEXT("The finger resting on the disabled button brought no bubble up"), HasNoBubble(Tooltip));
		})
		.TouchUp(1)
		.WaitFrames(1)
		.Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipDisabledPadTest,
	"DreamGUI.Tooltip.TheDPadPassesOverADisabledButtonSoItsBubbleNeverComesUpAndTheOneItLandsOnDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipDisabledPadTest, "DreamGUI.Tooltip.TheDPadPassesOverADisabledButtonSoItsBubbleNeverComesUpAndTheOneItLandsOnDoes", "[Nav][Disabled]")

/*
 * Three buttons with tooltips in a row, the middle one disabled. The D-pad right from the first lands on the last, and after
 * the delay the last one's bubble is up; the disabled one's never was.
 */
bool FDreamCoverageTooltipDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	UDreamButton* First = Tooltip != nullptr ? MakeHelpButton(Rig, TEXT("First"), FVector2D(160.0, 50.0), FVector2D(-300.0, 0.0), nullptr) : nullptr;
	UDreamButton* Middle = First != nullptr ? MakeHelpButton(Rig, TEXT("Middle"), FVector2D(160.0, 50.0), FVector2D(0.0, 0.0), nullptr) : nullptr;
	UDreamButton* Last = Middle != nullptr ? MakeHelpButton(Rig, TEXT("Last"), FVector2D(160.0, 50.0), FVector2D(300.0, 0.0), nullptr) : nullptr;
	if (!TestTrue(TEXT("The rig, its tooltip service and three buttons with tooltips came up"), Last != nullptr))
	{
		return false;
	}
	Middle->SetIsEnabled(false);
	Rig.PumpFrames(2);
	if (!FocusOn(*this, Rig, First, TEXT("Player 0's focus can be put on the first button")))
	{
		return false;
	}

	bool bMiddleEverShown = false;
	TestTrue(TEXT("The D-pad right and a wait past the delay complete"), Rig.Driver()->Sequence()
		.Key(EKeys::Gamepad_DPad_Right)
		.Then([&bMiddleEverShown, Tooltip, Middle](FDreamDriverContext&) { if (Tooltip->GetShownForUser(0) == Middle) { bMiddleEverShown = true; } })
		.WaitSeconds(LongerThanTheDelay() * 0.5f)
		.Then([&bMiddleEverShown, Tooltip, Middle](FDreamDriverContext&) { if (Tooltip->GetShownForUser(0) == Middle) { bMiddleEverShown = true; } })
		.Perform());
	TestTrue(TEXT("The bubble that came up is the last button's, where the D-pad landed"),
		WaitForTooltipOf(Rig, Tooltip, Last, TEXT("the last button's tooltip after the D-pad")));
	TestFalse(TEXT("The disabled button's bubble never came up"), bMiddleEverShown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipScaledFollowTest,
	"DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleTheBubbleComesUpBesideThePointerAndMovesWithItByTheSamePixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipScaledFollowTest, "DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleTheBubbleComesUpBesideThePointerAndMovesWithItByTheSamePixels", "[Pointer][Scaled]")

/*
 * At half and at double: the mouse rests on a button 300 by 120 pixels as drawn. The bubble comes up below and to the right
 * of the pointer, not under it and not off somewhere else, and a move of the pointer within the button moves the bubble by
 * the same pixels.
 */
bool FDreamCoverageTooltipScaledFollowTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
		UDreamButton* Help = Tooltip != nullptr ? MakeHelpButton(Rig, TEXT("Help"), FVector2D(300.0, 120.0) / Scale, FVector2D(-100.0, 40.0) / Scale, nullptr) : nullptr;
		if (!TestTrue(At(Scale, TEXT("The tooltip service and a button with a tooltip came up")), Help != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
		const TOptional<FVector2D> Pointer = HelpElement->GetCentrePixel();
		TestTrue(At(Scale, TEXT("The mouse moving onto the button completes")), HelpElement->Hover());
		if (!TestTrue(At(Scale, TEXT("The button is on the viewport")), Pointer.IsSet())
			|| !TestTrue(At(Scale, TEXT("The mouse resting there brings the bubble up")), WaitForTooltipOf(Rig, Tooltip, Help, At(Scale, TEXT("the button's tooltip")))))
		{
			return false;
		}
		Rig.PumpFrames(3);
		const TOptional<FBox2D> Before = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
		if (!TestTrue(At(Scale, TEXT("The bubble is on the viewport")), Before.IsSet()))
		{
			return false;
		}
		const FVector2D FromPointer = Before->Min - Pointer.GetValue();
		TestTrue(*At(Scale, FString::Printf(TEXT("The bubble's corner is below and to the right of the pointer, near it (%.1f, %.1f)"), FromPointer.X, FromPointer.Y)),
			FromPointer.X >= -1.0 && FromPointer.Y >= -1.0 && FromPointer.X < 150.0 && FromPointer.Y < 150.0);

		const FVector2D Move(40.0, 20.0);
		TestTrue(At(Scale, TEXT("Moving right and down within the button completes")), HelpElement->MoveBy(Move));
		Rig.PumpFrames(1);
		const TOptional<FBox2D> After = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
		if (!TestTrue(At(Scale, TEXT("The bubble is still up and on the viewport")), After.IsSet()))
		{
			return false;
		}
		const FVector2D Shift = After->Min - Before->Min;
		TestTrue(*At(Scale, FString::Printf(TEXT("The bubble moved by the pointer's pixels (%.1f, %.1f for %.1f, %.1f)"), Shift.X, Shift.Y, Move.X, Move.Y)),
			Shift.Equals(Move, 1.5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipScaledTapThroughTest,
	"DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleAFingerTapWhereTheMousesBubbleIsDrawnReachesTheButtonUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipScaledTapThroughTest, "DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleAFingerTapWhereTheMousesBubbleIsDrawnReachesTheButtonUnderIt", "[Pointer][Touch][Scaled]")

/*
 * At half and at double, laid out in pixels: the mouse rests near the bottom of a help button until its bubble is up, the
 * bubble hanging over a wide button just below. A finger taps where the bubble and that button overlap: the button under
 * the bubble is clicked, the help button is not.
 */
bool FDreamCoverageTooltipScaledTapThroughTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	for (const double Scale : Scales)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> HelpListener(NewObject<UDreamPressInteractionListener>());
		TStrongObjectPtr<UDreamPressInteractionListener> UnderListener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
		// In pixels: the help button spans y 70..130 above the centre; the wide one below spans y -130..70, touching it.
		UDreamButton* Help = Tooltip != nullptr
			? MakeHelpButton(Rig, TEXT("Help"), FVector2D(200.0, 60.0) / Scale, FVector2D(-200.0, 100.0) / Scale, HelpListener.Get())
			: nullptr;
		UDreamButton* Under = Tooltip != nullptr
			? Rig.MakeControl<UDreamButton>(TEXT("Under"), nullptr, FVector2D(1000.0, 200.0) / Scale, FVector2D(0.0, -30.0) / Scale)
			: nullptr;
		if (!TestTrue(At(Scale, TEXT("The tooltip service and two buttons came up")), Help != nullptr && Under != nullptr))
		{
			return false;
		}
		Under->OnClicked.AddDynamic(UnderListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(2);

		FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
		// Near the help button's lower edge, so the bubble below the pointer reaches over the wide button at either scale.
		TestTrue(At(Scale, TEXT("The mouse moving near the bottom of the help button completes")), HelpElement->Hover() && HelpElement->MoveBy(FVector2D(0.0, 24.0)));
		if (!TestTrue(At(Scale, TEXT("The mouse resting there brings its bubble up")), WaitForTooltipOf(Rig, Tooltip, Help, At(Scale, TEXT("the help button's tooltip")))))
		{
			return false;
		}
		Rig.PumpFrames(3);
		const TOptional<FBox2D> BubbleRect = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
		const TOptional<FBox2D> UnderRect = Rig.Driver()->Find(FDreamBy::Widget(Under))->GetPixelRect();
		if (!TestTrue(At(Scale, TEXT("The bubble and the button below are both on the viewport")), BubbleRect.IsSet() && UnderRect.IsSet()))
		{
			return false;
		}
		const FBox2D Overlap(
			FVector2D(FMath::Max(BubbleRect->Min.X, UnderRect->Min.X), FMath::Max(BubbleRect->Min.Y, UnderRect->Min.Y)),
			FVector2D(FMath::Min(BubbleRect->Max.X, UnderRect->Max.X), FMath::Min(BubbleRect->Max.Y, UnderRect->Max.Y)));
		if (!TestTrue(At(Scale, TEXT("The bubble is drawn over part of the button below (the layout this test needs)")),
			Overlap.Min.X + 2.0 < Overlap.Max.X && Overlap.Min.Y + 2.0 < Overlap.Max.Y))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("A tap where the bubble is drawn completes")),
			Rig.Driver()->Sequence().TouchDown(0, Overlap.GetCenter()).TouchUp(0).WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The tap reached the button under the bubble")), UnderListener->ClickedCount, 1);
		TestEqual(*At(Scale, TEXT("...and not the button the mouse rests on")), HelpListener->ClickedCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTooltipScaledPadTest,
	"DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleABubbleTheDPadBringsUpHangsOffTheFocusedButtonWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTooltipScaledPadTest, "DreamGUI.Tooltip.OnACanvasScaledToHalfAndToDoubleABubbleTheDPadBringsUpHangsOffTheFocusedButtonWhereItIsDrawn", "[Nav][Scaled]")

/*
 * At half and at double: the D-pad moves the focus from one button onto a second one with a tooltip, and after the delay the
 * second one's bubble is up, its corner just below and to the right of the button's own corner as drawn -- not over the
 * button, and not where the button would be at another scale.
 */
bool FDreamCoverageTooltipScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
		UDreamButton* From = Tooltip != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("From"), nullptr, FVector2D(140.0, 50.0), FVector2D(-220.0, 60.0)) : nullptr;
		UDreamButton* Help = From != nullptr ? MakeHelpButton(Rig, TEXT("Help"), FVector2D(140.0, 50.0), FVector2D(-40.0, 60.0), nullptr) : nullptr;
		if (!TestTrue(At(Scale, TEXT("The tooltip service and two buttons came up")), Help != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!FocusOn(*this, Rig, From, At(Scale, TEXT("Player 0's focus can be put on the first button"))))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("The D-pad right completes")), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
		if (!TestTrue(At(Scale, TEXT("After the delay the focused button's bubble is up")), WaitForTooltipOf(Rig, Tooltip, Help, At(Scale, TEXT("the focused button's tooltip")))))
		{
			return false;
		}
		Rig.PumpFrames(3);
		const TOptional<FBox2D> BubbleRect = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
		const TOptional<FBox2D> HelpRect = Rig.Driver()->Find(FDreamBy::Widget(Help))->GetPixelRect();
		if (!TestTrue(At(Scale, TEXT("The bubble and the button are on the viewport")), BubbleRect.IsSet() && HelpRect.IsSet()))
		{
			return false;
		}
		const FVector2D FromCorner = BubbleRect->Min - HelpRect->Max;
		TestTrue(*At(Scale, FString::Printf(TEXT("The bubble hangs off the button's lower right corner as drawn (%.1f, %.1f from it)"), FromCorner.X, FromCorner.Y)),
			FromCorner.X >= -1.0 && FromCorner.Y >= -1.0 && FromCorner.X < 150.0 && FromCorner.Y < 150.0);
	}
	return true;
}

#endif
