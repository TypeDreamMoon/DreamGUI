// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUITooltip.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * TOOLTIPS ON A PANEL IN THE LEVEL, AND THE PAD'S TOOLTIP AS SHIPPED.
 *
 * Slate asks for a tooltip at each user's cursor (FSlateUser::UpdateTooltip, Slate/Private/Framework/Application/
 * SlateUser.cpp), on whatever the cursor's widget path is -- through a widget component, the path its interaction's ray finds
 * -- and brings it up once the cursor has rested there for the tooltip delay; the cursor leaving takes it down. A finger is a
 * pointer index of its own, never the cursor, so a finger brings no bubble up, wherever the widget it lands on is drawn.
 *
 * DreamGUI also brings a bubble up for the widget the gamepad's focus lands on, after the same delay (UDreamUITooltipSubsystem:
 * "Navigation arms a tooltip too"), and takes it down when the focus moves on to a widget without one. Slate has no such
 * bubble; what is asserted of it is DreamGUI's own word, on the screen and among the widgets of a panel.
 *
 * Where a world-space widget's bubble is drawn is DreamGUI's choice (DreamUITooltipPolicy::ResolveTooltipHost puts it on the
 * panel's own canvas, hung off the widget, where Slate would open a tooltip window at the cursor); these tests ask only that
 * it comes up, when, and for which widget.
 */
namespace DreamTooltipWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** A button with a tooltip under InParent (the rig's root when null), its clicks heard by InListener when there is one. */
	UDreamButton* MakeHelpButton(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize,
		const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, InSize, InPosition);
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

	float TooltipDelay()
	{
		return FMath::Max(UDreamGUISettings::Get()->TooltipDelaySeconds, 0.1f);
	}

	/** Long enough for any bubble to have come up three times over. */
	float LongerThanTheDelay()
	{
		return 3.0f * TooltipDelay();
	}

	/** Frames until player 0's tooltip is up for InWidget, for twice the delay and a second more. */
	bool WaitForTooltipOf(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const UDreamWidget* InWidget, const FString& InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * TooltipDelay() + 1.0);
		return InRig.Driver()->Wait(
			FDreamUntil::Condition([InTooltip, InWidget]() { return InTooltip->GetShownForUser(0) == InWidget && InTooltip->GetBubbleForUser(0) != nullptr; }, Timeout),
			Timeout, InWhat);
	}

	bool HasNoBubble(const UDreamUITooltipSubsystem* InTooltip)
	{
		return InTooltip->GetBubbleForUser(0) == nullptr && InTooltip->GetShownForUser(0) == nullptr;
	}

	/** Frames until player 0 has no bubble, for a second. */
	bool WaitForNoBubble(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const FString& InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InTooltip]() { return HasNoBubble(InTooltip); }, Timeout), Timeout, InWhat);
	}

	/**
	 * Three buttons in a row under InParent, only the middle one with a tooltip. The focus on the first, the D-pad right lands
	 * on the middle one: halfway through the delay there is no bubble, after it the middle one's bubble is up. The D-pad right
	 * again lands on the last, which has none, and the bubble goes.
	 */
	bool RunPadBringsBubble(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig and its tooltip service came up"), Tooltip))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		UDreamButton* First = Rig.MakeControl<UDreamButton>(TEXT("First"), Parent, FVector2D(140.0, 50.0), FVector2D(-200.0, 40.0));
		UDreamButton* Help = MakeHelpButton(Rig, TEXT("Help"), Parent, FVector2D(140.0, 50.0), FVector2D(0.0, 40.0), nullptr);
		UDreamButton* Last = Rig.MakeControl<UDreamButton>(TEXT("Last"), Parent, FVector2D(140.0, 50.0), FVector2D(200.0, 40.0));
		if (!InTest.TestTrue(TEXT("Three buttons came up, the middle one with a tooltip"), First != nullptr && Help != nullptr && Last != nullptr && First->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the first button"), PutFocusOn(Rig, First->FaceNode)))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The D-pad right and half the delay complete"), Rig.Driver()->Sequence()
			.Key(EKeys::Gamepad_DPad_Right)
			.WaitSeconds(TooltipDelay() * 0.5f)
			.Then([&InTest, Tooltip](FDreamDriverContext&)
			{
				InTest.TestTrue(TEXT("Halfway through the delay there is no bubble yet"), HasNoBubble(Tooltip));
			})
			.Perform());
		InTest.TestTrue(TEXT("The D-pad landed on the button with the tooltip"), FocusIsIn(Rig, Help));
		InTest.TestTrue(TEXT("After the delay its bubble is up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the focused button's tooltip after the D-pad")));

		InTest.TestTrue(TEXT("The D-pad right again completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		InTest.TestTrue(TEXT("The D-pad went on to the last button"), FocusIsIn(Rig, Last));
		InTest.TestTrue(TEXT("...and the bubble went with the focus"), WaitForNoBubble(Rig, Tooltip, TEXT("no bubble once the focus is on a button without one")));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTooltipMouseTest,
	"DreamGUI.Tooltip.OnAWorldPanelTheMouseRestingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOffTakesItDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTooltipMouseTest, "DreamGUI.Tooltip.OnAWorldPanelTheMouseRestingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOffTakesItDown", "[Pointer][World]")

/*
 * A button with a tooltip on a panel. The mouse moved onto it through the world pointer and rested: halfway through the delay
 * there is no bubble, after it the button's bubble is up. Moved off the button, the bubble goes.
 */
bool FDreamWorldTooltipMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig and its tooltip service came up"), Tooltip))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* Help = Stage.IsReady() ? MakeHelpButton(Rig, TEXT("Help"), Stage.Panel, FVector2D(200.0, 60.0), FVector2D(-60.0, 40.0), nullptr) : nullptr;
	if (!TestNotNull(TEXT("A button with a tooltip on the panel"), Help))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));

	TestTrue(TEXT("The mouse moving onto the button on the panel completes"), HelpElement->Hover());
	TestTrue(TEXT("Half the delay passing completes"), Rig.Driver()->Sequence().WaitSeconds(TooltipDelay() * 0.5f).Perform());
	TestTrue(TEXT("Halfway through the delay there is no bubble yet"), HasNoBubble(Tooltip));
	TestTrue(TEXT("The mouse resting there brings the button's bubble up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the panel button's tooltip under the mouse")));

	// Four hundred pixels right is 190 cm along the panel: off the button, onto bare panel, which takes no pointer.
	TestTrue(TEXT("Moving the mouse off the button completes"), HelpElement->MoveBy(FVector2D(400.0, 0.0)));
	TestTrue(TEXT("The mouse gone, so is the bubble"), WaitForNoBubble(Rig, Tooltip, TEXT("no bubble once the mouse has left the button")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTooltipFingerTest,
	"DreamGUI.Tooltip.OnAWorldPanelAFingerTappingOrRestingOnAButtonWithATooltipBringsNoBubbleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTooltipFingerTest, "DreamGUI.Tooltip.OnAWorldPanelAFingerTappingOrRestingOnAButtonWithATooltipBringsNoBubbleUp", "[Touch][World]")

/*
 * A button with a tooltip on a panel. A tap on it through the world pointer clicks it and brings nothing up; a finger held on
 * it past the delay brings nothing up. Last, the mouse resting on the same button does bring its bubble up, so the button's
 * tooltip was there to be shown all along.
 */
bool FDreamWorldTooltipFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig and its tooltip service came up"), Tooltip))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* Help = Stage.IsReady() ? MakeHelpButton(Rig, TEXT("Help"), Stage.Panel, FVector2D(200.0, 60.0), FVector2D(-60.0, 40.0), Listener.Get()) : nullptr;
	if (!TestNotNull(TEXT("A button with a tooltip on the panel"), Help))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamElementRef HelpElement = Rig.Driver()->Find(FDreamBy::Widget(Help));
	const TOptional<FVector2D> OnHelp = HelpElement->GetCentrePixel();
	if (!TestTrue(TEXT("The camera sees the button on the panel"), OnHelp.IsSet()))
	{
		return false;
	}
	const float Longer = LongerThanTheDelay();

	TestTrue(TEXT("A tap on the button through the world pointer completes"), HelpElement->Tap(0));
	TestEqual(TEXT("The tap clicked the button"), Listener->ClickedCount, 1);
	TestTrue(TEXT("Waiting out the delay completes"), Rig.Driver()->Sequence().WaitSeconds(Longer).Perform());
	TestTrue(TEXT("The tap brought no bubble up"), HasNoBubble(Tooltip));

	TestTrue(TEXT("A finger held on the button past the delay, then lifted, completes"), Rig.Driver()->Sequence()
		.TouchDown(1, OnHelp.GetValue())
		.WaitSeconds(Longer)
		.Then([this, Tooltip](FDreamDriverContext&)
		{
			TestTrue(TEXT("The held finger brought no bubble up"), HasNoBubble(Tooltip));
		})
		.TouchUp(1)
		.WaitFrames(1)
		.Perform());

	TestTrue(TEXT("The mouse moving onto the same button completes"), HelpElement->Hover());
	TestTrue(TEXT("The mouse resting there does bring its bubble up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the panel button's tooltip under the mouse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTooltipPadTest,
	"DreamGUI.Tooltip.OnAWorldPanelTheDPadLandingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOnTakesItDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTooltipPadTest, "DreamGUI.Tooltip.OnAWorldPanelTheDPadLandingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOnTakesItDown", "[Nav][World]")

bool FDreamWorldTooltipPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldTestLocal;
	return RunPadBringsBubble(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedTooltipPadTest,
	"DreamGUI.Tooltip.TheDPadLandingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOnTakesItDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedTooltipPadTest, "DreamGUI.Tooltip.TheDPadLandingOnAButtonBringsItsBubbleUpAfterTheDelayAndMovingOnTakesItDown", "[Nav][Animated]")

bool FDreamAnimatedTooltipPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldTestLocal;
	return RunPadBringsBubble(*this, false);
}

#endif
