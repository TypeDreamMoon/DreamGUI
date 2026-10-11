// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamButton on a panel in the level, walked to with the pad.
 *
 * A UWidgetComponent's widget takes the keyboard and the pad the way a screen's does once it has the user's focus: the
 * D-pad walks its hit-test grid (FHittestGrid::FindNextFocusableWidget), Accept presses the focused button (SButton::
 * OnKeyDown, Slate/Private/Widgets/Input/SButton.cpp:296), and the focus is drawn on the button's own geometry -- in the
 * component's window, so where the button stands in the level. DreamGUI's directional scan walks a world panel's controls
 * among themselves (DreamUINavigationScan::ScanDirectional keeps world-space candidates with world-space ones), and the ring
 * of the panel's screen is the panel's own.
 */
namespace DreamButtonWorldTestLocal
{
	const FVector2D ButtonSize(140.0, 50.0);

	struct FWorldButton
	{
		UDreamButton* Button = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;
	};

	/** A button on InPanel at InPosition (panel units, Y up), its clicks counted. */
	FWorldButton PlaceButton(FDreamDriverRig& InRig, UDreamWidget* InPanel, const TCHAR* InName, const FVector2D& InPosition)
	{
		FWorldButton Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		Placed.Button = InRig.MakeControl<UDreamButton>(InName, InPanel, ButtonSize, InPosition);
		if (Placed.Button != nullptr && Placed.Button->FaceNode != nullptr)
		{
			Placed.Button->OnClicked.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
		}
		else
		{
			Placed.Button = nullptr;
		}
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonWorldPadTest,
	"DreamGUI.Button.OnAWorldPanelTheDPadReachesTheNextButtonWithTheRingOnItAsSeenAndConfirmClicksIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonWorldPadTest, "DreamGUI.Button.OnAWorldPanelTheDPadReachesTheNextButtonWithTheRingOnItAsSeenAndConfirmClicksIt", "[Nav][World]")

/*
 * Two buttons side by side on a panel three metres ahead, the left one focused as a screen focuses its first control. D-pad
 * right takes the focus to the right-hand button, the ring comes to sit on it where the camera sees it, and the pad's Accept
 * clicks it -- only it. D-pad left takes the focus back, and Accept then clicks the left-hand one.
 */
bool FDreamButtonWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FWorldButton Left = PlaceButton(Rig, Stage.Panel, TEXT("Left"), FVector2D(-120.0, 40.0));
	const FWorldButton Right = PlaceButton(Rig, Stage.Panel, TEXT("Right"), FVector2D(120.0, 40.0));
	if (!TestTrue(TEXT("Two buttons with faces can be made on the panel"), Left.Button != nullptr && Right.Button != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The focus goes onto the left button"), PutFocusOn(Rig, Left.Button->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	if (!TestTrue(TEXT("The D-pad took the focus to the right-hand button"), FocusIsIn(Rig, Right.Button)))
	{
		return false;
	}
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the right-hand button where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept clicked the focused button"), Right.Listener->ClickedCount, 1);
	TestEqual(TEXT("...and not the one the focus left"), Left.Listener->ClickedCount, 0);

	TestTrue(TEXT("D-pad left completes"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestTrue(TEXT("D-pad left took the focus back to the left-hand button"), FocusIsIn(Rig, Left.Button));
	TestTrue(TEXT("The pad's Accept completes again"), PadConfirm(Rig));
	TestEqual(TEXT("Accept clicked the left-hand button"), Left.Listener->ClickedCount, 1);
	TestEqual(TEXT("...and the right-hand one no more"), Right.Listener->ClickedCount, 1);
	return true;
}

#endif
