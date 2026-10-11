// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "UMG/DreamUMGWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"
#include "Interaction/DreamUMGHostInteractionTestTypes.h"

/*
 * A HOSTED UMG WIDGET ON A PANEL IN THE LEVEL.
 *
 * UDreamUMGWidgetInteraction carries the pointer into a hosted UMG widget through a virtual Slate user, as UWidgetComponent's
 * virtual window takes a UWidgetInteractionComponent's pointer (see the host's own interaction tests next door for why the
 * probe catches what it would send instead of routing it). The point it hands on is where the pointer's ray met the surface
 * (UDreamUMGWidgetInteraction's local hit location, from the hit's world point in the surface's plane), so a surface on a panel
 * in the level takes a press through the world pointer as one on the screen takes the mouse's: a press on it reaches the
 * hosted widget -- from the mouse as the mouse, from a finger as a touch of its own -- and a press beside it does not.
 */
namespace DreamUMGHostWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** A surface on InParent whose visual is a UMG widget, carrying the bridge's probe; null, having said why, when it could not be made. */
	UDreamUMGDragInteractionProbe* MakeHostedSurface(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent,
		const FVector2D& InPosition, UDreamWidget*& OutSurface)
	{
		OutSurface = InRig.MakeWidget(TEXT("Surface"), InParent, FVector2D(240.0, 160.0), InPosition);
		if (!InTest.TestNotNull(TEXT("A surface can be made on the panel"), OutSurface))
		{
			return nullptr;
		}
		// The bridge forwards into a UMG widget's visual and nothing else, so the surface wears one.
		OutSurface->RemoveVisual();
		if (!InTest.TestNotNull(TEXT("The surface's visual is a UMG widget"), OutSurface->CreateNewVisual<UDreamUMGWidget>()))
		{
			OutSurface->DestroyWidget();
			return nullptr;
		}
		UDreamUMGDragInteractionProbe* Probe = OutSurface->AddComponent<UDreamUMGDragInteractionProbe>();
		if (!InTest.TestNotNull(TEXT("The surface carries the bridge"), Probe))
		{
			OutSurface->DestroyWidget();
			return nullptr;
		}
		InRig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("The bridge has a virtual Slate user to forward through"), Probe->IsEnrolled()))
		{
			OutSurface->DestroyWidget();
			return nullptr;
		}
		return Probe;
	}

	/** How many presses the bridge sent, from the mouse and from fingers. */
	void CountDowns(const UDreamUMGDragInteractionProbe* InProbe, int32& OutMouse, int32& OutTouch)
	{
		OutMouse = 0;
		OutTouch = 0;
		for (const FDreamUMGSentPointerEvent& Event : InProbe->SentOfKind(FDreamUMGSentPointerEvent::EKind::Down))
		{
			++(Event.bTouch ? OutTouch : OutMouse);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldUMGHostPressTest,
	"DreamGUI.Interaction.UMG.OnAWorldPanelAPressOnTheSurfaceReachesTheHostedWidgetFromTheMouseAndAFingerAndOneBesideItDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldUMGHostPressTest, "DreamGUI.Interaction.UMG.OnAWorldPanelAPressOnTheSurfaceReachesTheHostedWidgetFromTheMouseAndAFingerAndOneBesideItDoesNot", "[Pointer][Touch][World]")

/*
 * A hosting surface on a panel, off the panel's middle. A click through the world pointer on the surface sends the hosted
 * widget one press of the mouse, and a click thirty centimetres past its right edge sends nothing; a finger tapped on it sends
 * one touch, and one tapped past its edge nothing.
 */
bool FDreamWorldUMGHostPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGHostWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamWidget* Surface = nullptr;
	UDreamUMGDragInteractionProbe* Probe = Stage.IsReady() ? MakeHostedSurface(*this, Rig, Stage.Panel, FVector2D(-100.0, 0.0), Surface) : nullptr;
	if (!TestNotNull(TEXT("A hosting surface came up on the panel"), Probe))
	{
		return false;
	}
	if (Rig.EventSystem() != nullptr)
	{
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
	}
	const TOptional<FVector2D> OnSurface = PixelOf(Rig, Surface);
	const TOptional<FBox2D> SurfaceRect = RectOf(Rig, Surface);
	if (!TestTrue(TEXT("The camera sees the surface on the panel"), OnSurface.IsSet() && SurfaceRect.IsSet()))
	{
		Surface->DestroyWidget();
		return false;
	}
	const FVector2D PastItsEdge(SurfaceRect->Max.X + 30.0 * PixelsPerCentimetre(), SurfaceRect->GetCenter().Y);
	int32 MouseDowns = 0;
	int32 TouchDowns = 0;

	TestTrue(TEXT("Clicking the surface through the world pointer completes"), Rig.Driver()->Sequence().MoveToPixel(OnSurface.GetValue()).Press().Release().WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("The click reached the hosted widget as one press of the mouse"), MouseDowns, 1);
	TestTrue(TEXT("Clicking past the surface's edge completes"), Rig.Driver()->Sequence().MoveToPixel(PastItsEdge).Press().Release().WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("The click past its edge did not"), MouseDowns, 1);

	TestTrue(TEXT("A finger tapped on the surface completes"), Rig.Driver()->Sequence().TouchDown(0, OnSurface.GetValue()).TouchUp(0).WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("The tap reached the hosted widget as one touch"), TouchDowns, 1);
	TestTrue(TEXT("A finger tapped past the surface's edge completes"), Rig.Driver()->Sequence().TouchDown(1, PastItsEdge).TouchUp(1).WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("The tap past its edge did not"), TouchDowns, 1);
	TestEqual(TEXT("...and no finger was ever taken for the mouse"), MouseDowns, 1);
	Surface->DestroyWidget();
	return true;
}

#endif
