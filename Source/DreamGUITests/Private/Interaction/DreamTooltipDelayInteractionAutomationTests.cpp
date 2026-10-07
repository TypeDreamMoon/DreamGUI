// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUITooltip.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"

/*
 * A MOUSE'S TOOLTIP: WHEN IT COMES UP, AND WHERE IT GOES.
 *
 * Slate summons a tooltip for the widget under the cursor once the cursor has rested there (FSlateUser::UpdateTooltip, and
 * the summon delay, Slate/Private/Framework/Application/SlateUser.cpp:52-56, :1328-1329), and a non-interactive tooltip then
 * follows the cursor every frame, offset from it (:1491-1500). DreamGUI's delay is the project setting TooltipDelaySeconds,
 * counted from the frame the pointer arrives; every wait below is measured in it, so a project that tunes it does not change
 * what these mean.
 */
namespace DreamTooltipDelayTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A button with a tooltip in the middle of the screen, laid out. */
	UDreamButton* MakeHelpButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamUITooltipSubsystem*& OutTooltip)
	{
		InRig.BindTest(&InTest);
		OutTooltip = InRig.IsUsable() ? UDreamUITooltipSubsystem::Get(InRig.GetWorld()) : nullptr;
		UDreamButton* Button = OutTooltip != nullptr ? InRig.MakeControl<UDreamButton>(TEXT("Help"), nullptr, FVector2D(200.0, 60.0)) : nullptr;
		if (!InTest.TestTrue(TEXT("The rig, its tooltip service and a button came up"), Button != nullptr))
		{
			return nullptr;
		}
		Button->SetToolTipText(FText::AsCultureInvariant(TEXT("About help")));
		InRig.PumpFrames(2);
		return Button;
	}

	/** The frames the delay takes on the pump. */
	int32 DelayFrames(const FDreamDriverRig& InRig)
	{
		return FMath::CeilToInt(UDreamGUISettings::Get()->TooltipDelaySeconds / InRig.Context().FrameSeconds);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTooltipDelayTest,
	"DreamGUI.Tooltip.NoBubbleComesUpBeforeTheDelayHasPassedAndOneDoesOnceItHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTooltipDelayTest, "DreamGUI.Tooltip.NoBubbleComesUpBeforeTheDelayHasPassedAndOneDoesOnceItHas", "[Pointer][Animated]")

/*
 * The mouse moves onto the button and rests. Three frames short of the delay there is no bubble; within three frames after
 * it, there is one, for that button.
 */
bool FDreamTooltipDelayTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipDelayTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamUITooltipSubsystem* Tooltip = nullptr;
	UDreamButton* Help = MakeHelpButton(*this, Rig, Tooltip);
	if (Help == nullptr)
	{
		return false;
	}
	const int32 Delay = DelayFrames(Rig);
	if (!TestTrue(FString::Printf(TEXT("The delay is long enough to be early for (%d frames)"), Delay), Delay > 6))
	{
		return false;
	}

	TestTrue(TEXT("Moving onto the button completes"), Rig.Driver()->Find(FDreamBy::Widget(Help))->Hover());
	TestTrue(TEXT("Resting there until three frames short of the delay completes"), Rig.Driver()->Sequence().WaitFrames(Delay - 4).Perform());
	TestNull(TEXT("Short of the delay there is no bubble"), Tooltip->GetBubbleForUser(0));
	TestNull(TEXT("...and nothing it is shown for"), Tooltip->GetShownForUser(0));

	const FWaitTimeout Timeout = FWaitTimeout::InSeconds(6.0 * Rig.Context().FrameSeconds);
	TestTrue(TEXT("Within three frames after the delay the button's bubble is up"), Rig.Driver()->Wait(
		FDreamUntil::Condition([Tooltip, Help]() { return Tooltip->GetShownForUser(0) == Help; }, Timeout),
		Timeout, TEXT("the button's tooltip once the delay has passed")));
	TestNotNull(TEXT("...a bubble that is drawn"), Tooltip->GetBubbleForUser(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTooltipFollowsPointerTest,
	"DreamGUI.Tooltip.TheBubbleFollowsThePointerAsItMovesAcrossItsWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTooltipFollowsPointerTest, "DreamGUI.Tooltip.TheBubbleFollowsThePointerAsItMovesAcrossItsWidget", "[Pointer][Animated]")

/*
 * The bubble up, the mouse moves within the button -- right and down, then left and up -- and the bubble moves with it by the
 * same pixels each time, staying where it was relative to the pointer, as Slate's non-interactive tooltip does
 * (SlateUser.cpp:1491-1500). Moving within the widget is not leaving it: the bubble stays up throughout.
 */
bool FDreamTooltipFollowsPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipDelayTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamUITooltipSubsystem* Tooltip = nullptr;
	UDreamButton* Help = MakeHelpButton(*this, Rig, Tooltip);
	if (Help == nullptr)
	{
		return false;
	}
	FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
	TestTrue(TEXT("Moving onto the button completes"), HelpElement->Hover());
	const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * UDreamGUISettings::Get()->TooltipDelaySeconds + 1.0);
	if (!TestTrue(TEXT("Resting there brings the button's bubble up"), Rig.Driver()->Wait(
		FDreamUntil::Condition([Tooltip, Help]() { return Tooltip->GetShownForUser(0) == Help; }, Timeout),
		Timeout, TEXT("the button's tooltip"))))
	{
		return false;
	}
	// The bubble re-measures and re-places itself each frame while up; a few frames settle it.
	Rig.PumpFrames(3);

	const TOptional<FBox2D> Before = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
	const FVector2D FirstMove(40.0, 20.0);
	TestTrue(TEXT("Moving right and down within the button completes"), HelpElement->MoveBy(FirstMove));
	Rig.PumpFrames(1);
	const TOptional<FBox2D> AfterFirst = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
	if (!TestTrue(TEXT("The bubble is on the viewport before and after the move"), Before.IsSet() && AfterFirst.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("Moving within the button leaves its bubble up"), Tooltip->GetShownForUser(0), static_cast<UDreamWidget*>(Help));
	const FVector2D FirstShift = AfterFirst->Min - Before->Min;
	TestTrue(FString::Printf(TEXT("The bubble moved with the pointer (%.1f, %.1f for a move of %.1f, %.1f)"),
		FirstShift.X, FirstShift.Y, FirstMove.X, FirstMove.Y), FirstShift.Equals(FirstMove, 1.0));

	const FVector2D SecondMove(-80.0, -25.0);
	TestTrue(TEXT("Moving left and up within the button completes"), HelpElement->MoveBy(SecondMove));
	Rig.PumpFrames(1);
	const TOptional<FBox2D> AfterSecond = FDreamDriverProjection::WidgetToPixelRect(Tooltip->GetBubbleForUser(0));
	if (!TestTrue(TEXT("The bubble is on the viewport after the second move"), AfterSecond.IsSet()))
	{
		return false;
	}
	const FVector2D SecondShift = AfterSecond->Min - AfterFirst->Min;
	TestTrue(FString::Printf(TEXT("...and with it again (%.1f, %.1f for a move of %.1f, %.1f)"),
		SecondShift.X, SecondShift.Y, SecondMove.X, SecondMove.Y), SecondShift.Equals(SecondMove, 1.0));
	return true;
}

#endif
