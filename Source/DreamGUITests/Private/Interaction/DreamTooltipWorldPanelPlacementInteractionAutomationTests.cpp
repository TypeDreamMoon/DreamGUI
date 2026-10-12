// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

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
#include "Driver/DreamDriverUntil.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * WHERE A TOOLTIP'S BUBBLE IS ON A PANEL IN THE LEVEL.
 *
 * Slate opens a tooltip window at the cursor (FSlateUser::UpdateTooltip, Slate/Private/Framework/Application/SlateUser.cpp),
 * which through a widget component is a window inside the component's own (SVirtualWindow::OnVisualizeTooltip hosts it in
 * the virtual window's tooltip presenter, Slate/Private/Widgets/SVirtualWindow.cpp). DreamGUI keeps its world-space bubbles
 * on the panel too, and hangs them off the widget rather than off the pointer -- its own rule, with a comment saying why
 * (DreamUITooltipPolicy::ResolveTooltipHost puts the bubble on the panel's own canvas; UDreamUITooltipSubsystem::
 * ResolveTooltipAnchor anchors it at the widget's bottom-right corner, as for the pad on a screen, since a viewport pixel says
 * nothing about a plane in the level). What these tests hold is that rule as it stands:
 *  - the bubble is a child of the panel's root, drawn by the panel's canvas, in the panel's plane as the camera sees it, and
 *    inside the panel;
 *  - its top-left corner is the widget's bottom-right corner moved by the project's TooltipOffset, in panel units, where the
 *    bubble fits without flipping (DreamUITooltipPolicy::ComputeTooltipTopLeft);
 *  - the mouse moving about on the widget leaves it where it is.
 */
namespace DreamTooltipWorldPlacementTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** A button with a tooltip on InPanel at InPosition, InSize across. */
	UDreamButton* MakeHelpButton(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InPanel, const FVector2D& InSize, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InPanel, InSize, InPosition);
		if (Button != nullptr)
		{
			Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("About %s"), InName)));
		}
		return Button;
	}

	float TooltipDelay()
	{
		return FMath::Max(UDreamGUISettings::Get()->TooltipDelaySeconds, 0.1f);
	}

	/** Frames until player 0's tooltip is up for InWidget, for twice the delay and a second more. */
	bool WaitForTooltipOf(FDreamDriverRig& InRig, const UDreamUITooltipSubsystem* InTooltip, const UDreamWidget* InWidget, const FString& InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * TooltipDelay() + 1.0);
		return InRig.Driver()->Wait(
			FDreamUntil::Condition([InTooltip, InWidget]() { return InTooltip->GetShownForUser(0) == InWidget && InTooltip->GetBubbleForUser(0) != nullptr; }, Timeout),
			Timeout, InWhat);
	}

	/**
	 * Whether player 0's bubble is on InPanel -- the panel root's child, drawn by its canvas, its corners on the panel's plane
	 * -- and seen inside the panel. OutWhere says what it is on and where it was seen.
	 */
	bool BubbleIsOnThePanel(const FDreamDriverRig& InRig, const UDreamWidget* InBubble, const UDreamWidget* InPanel, FString& OutWhere)
	{
		const TOptional<FBox2D> BubbleRect = RectOf(InRig, InBubble);
		const TOptional<FBox2D> PanelRect = RectOf(InRig, InPanel);
		if (InBubble == nullptr || !BubbleRect.IsSet() || !PanelRect.IsSet())
		{
			OutWhere = TEXT("no bubble, or the camera sees no bubble or no panel");
			return false;
		}
		// Each corner of the bubble carried into the panel's own space: off the plane is its X there.
		double MostOffThePlane = 0.0;
		const FVector2D Centre = InBubble->GetLocalSpaceCenter();
		const FVector2D Half(InBubble->GetWidth() * 0.5, InBubble->GetHeight() * 0.5);
		for (const FVector2D& Sign : { FVector2D(-1.0, -1.0), FVector2D(-1.0, 1.0), FVector2D(1.0, -1.0), FVector2D(1.0, 1.0) })
		{
			const FVector2D Corner = Centre + Half * Sign;
			const FVector World = InBubble->GetWorldTransform().TransformPosition(FVector(0.0, Corner.X, Corner.Y));
			MostOffThePlane = FMath::Max(MostOffThePlane, FMath::Abs(InPanel->GetWorldTransform().InverseTransformPosition(World).X));
		}
		OutWhere = FString::Printf(TEXT("the bubble under '%s', %s its panel's canvas, %.3f cm off the panel's plane at most, seen at %s; the panel seen at %s"),
			InBubble->GetParent() != nullptr ? *InBubble->GetParent()->GetDisplayName() : TEXT("nothing"),
			InBubble->GetRootCanvas() == InPanel->GetRootCanvas() ? TEXT("drawn by") : TEXT("not drawn by"),
			MostOffThePlane, *Describe(BubbleRect.GetValue()), *Describe(PanelRect.GetValue()));
		return InBubble->GetParent() == InPanel
			&& InBubble->GetRootCanvas() == InPanel->GetRootCanvas()
			&& InBubble->IsWorldSpaceUI()
			&& MostOffThePlane <= 0.01
			&& PanelRect->IsInside(BubbleRect->Min) && PanelRect->IsInside(BubbleRect->Max);
	}

	/**
	 * The pixel the bubble's top-left corner should be seen at: InSource's bottom-right corner moved by TooltipOffset, in its
	 * own units -- the panel's, the button sitting on the panel unturned and unscaled -- as the camera sees that point.
	 */
	TOptional<FVector2D> ExpectedTopLeft(const FDreamDriverRig& InRig, const UDreamWidget* InSource)
	{
		const FVector2D Centre = InSource->GetLocalSpaceCenter();
		const FVector2D BottomRight = Centre + FVector2D(InSource->GetWidth() * 0.5, -InSource->GetHeight() * 0.5);
		return LocalPointPixel(InRig, InSource, BottomRight + UDreamGUISettings::Get()->TooltipOffset);
	}

	/** Whether InBubble's top-left corner is seen at InExpected, to within a pixel and a half. OutWhere says both. */
	bool HangsAt(const FDreamDriverRig& InRig, const UDreamWidget* InBubble, const FVector2D& InExpected, FString& OutWhere)
	{
		const TOptional<FBox2D> BubbleRect = RectOf(InRig, InBubble);
		if (!BubbleRect.IsSet())
		{
			OutWhere = TEXT("the camera sees no bubble");
			return false;
		}
		OutWhere = FString::Printf(TEXT("its top-left seen at (%.1f, %.1f), expected (%.1f, %.1f)"),
			BubbleRect->Min.X, BubbleRect->Min.Y, InExpected.X, InExpected.Y);
		return BubbleRect->Min.Equals(InExpected, 1.5);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTooltipPlacementMouseTest,
	"DreamGUI.Tooltip.OnAWorldPanelTheMousesBubbleIsOnThePanelHungOffTheButtonsCornerAndStaysThereAsTheMouseMovesOnTheButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTooltipPlacementMouseTest, "DreamGUI.Tooltip.OnAWorldPanelTheMousesBubbleIsOnThePanelHungOffTheButtonsCornerAndStaysThereAsTheMouseMovesOnTheButton", "[Pointer][World]")

/*
 * A button with a tooltip on a panel, left of the middle. The mouse moved onto the button's left part through the world
 * pointer and rested there brings its bubble up: on the panel, in its plane, its top-left corner where the button's
 * bottom-right corner moved by TooltipOffset is seen -- not at the mouse. The mouse moved twice more across the button, right
 * and down, leaves the bubble up for the button and seen where it was, to within half a pixel.
 */
bool FDreamWorldTooltipPlacementMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldPlacementTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig and its tooltip service came up"), Tooltip))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* Help = Stage.IsReady() ? MakeHelpButton(Rig, TEXT("Help"), Stage.Panel, FVector2D(200.0, 60.0), FVector2D(-120.0, 60.0)) : nullptr;
	if (!TestNotNull(TEXT("A button with a tooltip on the panel"), Help))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TOptional<FBox2D> HelpRect = RectOf(Rig, Help);
	const TOptional<FVector2D> Expected = ExpectedTopLeft(Rig, Help);
	if (!TestTrue(TEXT("The camera sees the button, and where its bubble should hang"), HelpRect.IsSet() && Expected.IsSet()))
	{
		return false;
	}
	// A quarter of the way across, level with its middle: the mouse well inside the button and well away from the corner the
	// bubble hangs from, so a bubble at the mouse could not pass for one at the corner.
	const FVector2D Rest(FMath::Lerp(HelpRect->Min.X, HelpRect->Max.X, 0.25), HelpRect->GetCenter().Y);

	TestTrue(TEXT("The mouse moving onto the button on the panel completes"), Rig.Driver()->Sequence().MoveToPixel(Rest).WaitFrames(1).Perform());
	if (!TestTrue(TEXT("The mouse resting there brings the button's bubble up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the panel button's tooltip under the mouse"))))
	{
		return false;
	}
	UDreamWidget* Bubble = Tooltip->GetBubbleForUser(0);
	FString Where;
	TestTrue(TEXT("The bubble is on the panel: the panel root's child, drawn by its canvas, in its plane, seen inside it"),
		BubbleIsOnThePanel(Rig, Bubble, Stage.Panel, Where));
	AddInfo(Where);
	TestTrue(TEXT("The bubble hangs off the button's bottom-right corner by the tooltip offset, as the camera sees it"),
		HangsAt(Rig, Bubble, Expected.GetValue(), Where));
	AddInfo(Where);
	const TOptional<FBox2D> Before = RectOf(Rig, Bubble);
	TestFalse(TEXT("...and not at the mouse"), Before.IsSet() && Before->IsInside(Rest));

	TestTrue(TEXT("The mouse moving right and then down across the button completes"),
		Rig.Driver()->Sequence().MoveBy(FVector2D(60.0, 0.0)).WaitFrames(2).MoveBy(FVector2D(20.0, 30.0)).WaitFrames(2).Perform());
	TestTrue(TEXT("The mouse is still on the button"), HelpRect->IsInside(Rest + FVector2D(80.0, 30.0)));
	TestTrue(TEXT("The bubble is still up for the button"), Tooltip->GetShownForUser(0) == Help && Tooltip->GetBubbleForUser(0) == Bubble);
	const TOptional<FBox2D> After = RectOf(Rig, Tooltip->GetBubbleForUser(0));
	if (TestTrue(TEXT("The camera still sees the bubble"), Before.IsSet() && After.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("The bubble did not follow the mouse: seen at %s before, %s after"), *Describe(Before.GetValue()), *Describe(After.GetValue())),
			After->Min.Equals(Before->Min, 0.5) && After->Max.Equals(Before->Max, 0.5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTooltipPlacementPadTest,
	"DreamGUI.Tooltip.OnAWorldPanelThePadsBubbleIsOnThePanelHungOffTheFocusedButtonsCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTooltipPlacementPadTest, "DreamGUI.Tooltip.OnAWorldPanelThePadsBubbleIsOnThePanelHungOffTheFocusedButtonsCorner", "[Nav][World]")

/*
 * Two buttons side by side on a panel, the right one with a tooltip, the focus on the left. The D-pad right lands on the
 * button with the tooltip, and after the delay its bubble is up: on the panel, in its plane, its top-left corner where the
 * button's bottom-right corner moved by TooltipOffset is seen.
 */
bool FDreamWorldTooltipPlacementPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTooltipWorldPlacementTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamUITooltipSubsystem* Tooltip = Rig.IsUsable() ? UDreamUITooltipSubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig and its tooltip service came up"), Tooltip))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* First = Stage.IsReady() ? Rig.MakeControl<UDreamButton>(TEXT("First"), Stage.Panel, FVector2D(140.0, 50.0), FVector2D(-180.0, 60.0)) : nullptr;
	UDreamButton* Help = Stage.IsReady() ? MakeHelpButton(Rig, TEXT("Help"), Stage.Panel, FVector2D(140.0, 50.0), FVector2D(0.0, 60.0)) : nullptr;
	if (!TestTrue(TEXT("Two buttons on the panel, the second with a tooltip"), First != nullptr && First->FaceNode != nullptr && Help != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TOptional<FVector2D> Expected = ExpectedTopLeft(Rig, Help);
	if (!TestTrue(TEXT("The camera sees where the bubble should hang"), Expected.IsSet())
		|| !TestTrue(TEXT("Player 0's focus can be put on the first button"), PutFocusOn(Rig, First->FaceNode)))
	{
		return false;
	}

	TestTrue(TEXT("The D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad landed on the button with the tooltip"), FocusIsIn(Rig, Help));
	if (!TestTrue(TEXT("After the delay its bubble is up"), WaitForTooltipOf(Rig, Tooltip, Help, TEXT("the focused button's tooltip after the D-pad"))))
	{
		return false;
	}
	UDreamWidget* Bubble = Tooltip->GetBubbleForUser(0);
	FString Where;
	TestTrue(TEXT("The bubble is on the panel: the panel root's child, drawn by its canvas, in its plane, seen inside it"),
		BubbleIsOnThePanel(Rig, Bubble, Stage.Panel, Where));
	AddInfo(Where);
	TestTrue(TEXT("The bubble hangs off the focused button's bottom-right corner by the tooltip offset, as the camera sees it"),
		HangsAt(Rig, Bubble, Expected.GetValue(), Where));
	AddInfo(Where);
	return true;
}

#endif
