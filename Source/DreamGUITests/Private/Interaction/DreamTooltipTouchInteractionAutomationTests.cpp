// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Interaction/DreamUITooltip.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A FINGER IS NOT A CURSOR.
 *
 * Slate asks for tooltips at each user's cursor and nowhere else: FSlateUser::UpdateTooltip looks under GetCursorPosition,
 * the cursor's pointer index (Slate/Private/Framework/Application/SlateUser.cpp), and a touch is a pointer index of its
 * own. So a finger never brings a bubble up -- not with a tap, not held past the delay, not dragged onto a widget and left
 * there -- and the widget it lands on takes the tap as a click. The mouse's bubble, which a finger cannot summon, cannot
 * stop one either: it is drawn raycast-disabled, and a tap where it is drawn reaches what is under it.
 *
 * Every wait is measured in the settings' own delay, so a project that tunes it does not change what these mean.
 */
namespace DreamTooltipTouchInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A button with a tooltip, its clicks heard by InListener. */
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
	bool WaitForTooltipOf(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const UDreamWidget* InWidget, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * UDreamGUISettings::Get()->TooltipDelaySeconds + 1.0);
		return InRig.Driver()->Wait(
			FDreamUntil::Condition([InTooltip, InWidget]() { return InTooltip->GetShownForUser(0) == InWidget; }, Timeout),
			Timeout, InWhat);
	}

	/** Neither a bubble nor a widget it is shown for, for player 0. */
	bool HasNoBubble(const UDreamUITooltipSubsystem* InTooltip)
	{
		return InTooltip->GetBubbleForUser(0) == nullptr && InTooltip->GetShownForUser(0) == nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTooltipFingerBringsNoBubbleTest,
	"DreamGUI.Tooltip.AFingerTappingHoldingOrRestingOnAWidgetWithATooltipBringsNoBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTooltipFingerBringsNoBubbleTest, "DreamGUI.Tooltip.AFingerTappingHoldingOrRestingOnAWidgetWithATooltipBringsNoBubbleUp", "[Touch][Animated]")

/*
 * A tap clicks the button and brings nothing up; a finger held on it past the delay brings nothing up; a finger pressed on
 * empty space and dragged onto it, then left there past the delay, brings nothing up. Last, the mouse resting on the same
 * button does bring its bubble up, so the button's tooltip was there to be shown all along.
 */
bool FDreamTooltipFingerBringsNoBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	UDreamButton* Help = Tooltip != nullptr ? MakeHelpButton(Rig, TEXT("Help"), FVector2D(200.0, 60.0), FVector2D::ZeroVector, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig, its tooltip service and a button with a tooltip came up"), Help != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
	const TOptional<FVector2D> OnHelp = HelpElement->GetCentrePixel();
	const TOptional<FBox2D> HelpRect = HelpElement->GetPixelRect();
	if (!TestTrue(TEXT("The button is on the viewport"), OnHelp.IsSet() && HelpRect.IsSet()))
	{
		return false;
	}
	const FVector2D OnNothing(OnHelp->X, HelpRect->Max.Y + 150.0);
	const float Longer = LongerThanTheDelay();

	TestTrue(TEXT("A tap on the button completes"), HelpElement->Tap());
	TestEqual(TEXT("The tap clicked the button"), Listener->ClickedCount, 1);
	TestTrue(TEXT("Waiting out the delay completes"), Rig.Driver()->Sequence().WaitSeconds(Longer).Perform());
	TestTrue(TEXT("The tap brought no bubble up"), HasNoBubble(Tooltip));

	TestTrue(TEXT("A finger held on the button past the delay, then lifted, completes"), Rig.Driver()->Sequence()
		.TouchDown(0, OnHelp.GetValue())
		.WaitSeconds(Longer)
		.Then([this, Tooltip](FDreamDriverContext&)
		{
			TestTrue(TEXT("The held finger brought no bubble up"), HasNoBubble(Tooltip));
		})
		.TouchUp(0)
		.WaitSeconds(Longer)
		.Perform());
	TestTrue(TEXT("...nor did its lifting"), HasNoBubble(Tooltip));

	TestTrue(TEXT("A finger pressed on nothing, dragged onto the button and left there completes"), Rig.Driver()->Sequence()
		.TouchDown(0, OnNothing)
		.TouchMoveTo(0, (OnNothing + OnHelp.GetValue()) * 0.5)
		.TouchMoveTo(0, OnHelp.GetValue())
		.WaitSeconds(Longer)
		.Then([this, Tooltip](FDreamDriverContext&)
		{
			TestTrue(TEXT("The finger resting on the button brought no bubble up"), HasNoBubble(Tooltip));
		})
		.TouchUp(0)
		.WaitFrames(1)
		.Perform());

	TestTrue(TEXT("The mouse moving onto the button completes"), HelpElement->Hover());
	TestTrue(TEXT("The mouse resting there brings the button's bubble up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the button's tooltip under the mouse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTooltipFingerTapsThroughBubbleTest,
	"DreamGUI.Tooltip.AFingerTapWhereTheMousesBubbleIsDrawnReachesTheButtonUnderItAndTheBubbleComesBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTooltipFingerTapsThroughBubbleTest, "DreamGUI.Tooltip.AFingerTapWhereTheMousesBubbleIsDrawnReachesTheButtonUnderItAndTheBubbleComesBack", "[Pointer][Touch][Animated]")

/*
 * The mouse rests on one button until its bubble is up; the bubble hangs below and to the right of the pointer, over a
 * wide button laid out just under the first. A finger taps a point that is inside both the bubble and that button: the
 * button under the bubble is clicked, the one the mouse is on is not. The press takes the bubble down, as a press of the
 * player's does, and with the mouse still resting it comes back after its delay -- the finger took nothing over.
 */
bool FDreamTooltipFingerTapsThroughBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipTouchInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamPressInteractionListener> HelpListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> UnderListener(NewObject<UDreamPressInteractionListener>());
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	// The help button spans y 70..130 in canvas units (Y up); the wide one below it spans y -130..70, touching it.
	UDreamButton* Help = Tooltip != nullptr
		? MakeHelpButton(Rig, TEXT("Help"), FVector2D(200.0, 60.0), FVector2D(-200.0, 100.0), HelpListener.Get())
		: nullptr;
	UDreamButton* Under = Tooltip != nullptr
		? Rig.MakeControl<UDreamButton>(TEXT("Under"), nullptr, FVector2D(1000.0, 200.0), FVector2D(0.0, -30.0))
		: nullptr;
	if (!TestTrue(TEXT("The rig, its tooltip service and two buttons came up"), Help != nullptr && Under != nullptr))
	{
		return false;
	}
	Under->OnClicked.AddDynamic(UnderListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);

	FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
	TestTrue(TEXT("The mouse moving onto the help button completes"), HelpElement->Hover());
	if (!TestTrue(TEXT("The mouse resting there brings its bubble up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the help button's tooltip"))))
	{
		return false;
	}
	// The bubble re-measures and re-places itself each frame while up; a few frames settle it.
	Rig.PumpFrames(3);
	const TOptional<FBox2D> BubbleRect = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
	const TOptional<FBox2D> UnderRect = Rig.Driver()->Find(FDreamBy::Widget(Under))->GetPixelRect();
	if (!TestTrue(TEXT("The bubble and the button below are both on the viewport"), BubbleRect.IsSet() && UnderRect.IsSet()))
	{
		return false;
	}
	const FBox2D Overlap(
		FVector2D(FMath::Max(BubbleRect->Min.X, UnderRect->Min.X), FMath::Max(BubbleRect->Min.Y, UnderRect->Min.Y)),
		FVector2D(FMath::Min(BubbleRect->Max.X, UnderRect->Max.X), FMath::Min(BubbleRect->Max.Y, UnderRect->Max.Y)));
	if (!TestTrue(TEXT("The bubble is drawn over part of the button below (the layout this test needs)"),
		Overlap.Min.X + 2.0 < Overlap.Max.X && Overlap.Min.Y + 2.0 < Overlap.Max.Y))
	{
		return false;
	}

	TestTrue(TEXT("A tap where the bubble is drawn completes"), Rig.Driver()->Sequence()
		.TouchDown(0, Overlap.GetCenter())
		.TouchUp(0)
		.WaitFrames(1)
		.Perform());
	TestEqual(TEXT("The tap reached the button under the bubble"), UnderListener->ClickedCount, 1);
	TestEqual(TEXT("...and not the button the mouse rests on"), HelpListener->ClickedCount, 0);
	TestTrue(TEXT("With the mouse still resting on the help button, its bubble comes back"),
		WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the help button's tooltip after the tap")));
	return true;
}

#endif
