// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputTypes.h"
#include "UMG/DreamUMGWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamUMGHostInteractionTestTypes.h"

/*
 * A HOSTED UMG WIDGET BEHIND A DISABLED SURFACE, AND ON A SCALED CANVAS.
 *
 * UDreamUMGWidgetInteraction carries the pointer into a hosted UMG widget through a virtual Slate user (see the host's own
 * interaction tests next door for why the probe catches what it would send instead of routing it). A UMG widget inside a
 * disabled one is disabled with it -- SWidget::IsEnabled asks the parent, and Slate's hit test cuts a pointer's path at the
 * first disabled widget (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp) -- so a surface that is
 * disabled hands the widget it hosts no press, from the mouse or a finger. Enabled again, it does.
 *
 * On a canvas scaled to half and to double, a press just inside the surface's edge as drawn reaches the hosted widget, and one
 * just outside it does not.
 */
namespace DreamUMGHostCoverageTestLocal
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

	/** A surface whose visual is a UMG widget, carrying the bridge's probe; null, having said why, when it could not be made. */
	UDreamUMGDragInteractionProbe* MakeHostedSurface(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FVector2D& InPosition, UDreamWidget*& OutSurface)
	{
		OutSurface = InRig.MakeWidget(TEXT("Surface"), nullptr, FVector2D(300.0, 200.0), InPosition);
		if (!InTest.TestNotNull(TEXT("A surface can be made on the rig"), OutSurface))
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
		InRig.PumpFrames(1);
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
	FDreamCoverageUMGHostDisabledTest,
	"DreamGUI.Interaction.UMG.ADisabledSurfaceHandsTheHostedWidgetNoPressFromTheMouseOrAFingerUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageUMGHostDisabledTest, "DreamGUI.Interaction.UMG.ADisabledSurfaceHandsTheHostedWidgetNoPressFromTheMouseOrAFingerUntilItIsEnabled", "[Pointer][Touch][Disabled]")

/*
 * The surface disabled: a click on it and a finger's tap on it send the hosted widget no press. Enabled, the click sends one
 * as the mouse and the tap one as a touch.
 */
bool FDreamCoverageUMGHostDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGHostCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* Surface = nullptr;
	UDreamUMGDragInteractionProbe* Probe = Rig.IsUsable() ? MakeHostedSurface(*this, Rig, FVector2D::ZeroVector, Surface) : nullptr;
	if (!TestNotNull(TEXT("The rig and a hosting surface came up"), Probe))
	{
		return false;
	}
	const TOptional<FVector2D> Centre = FDreamDriverProjection::WidgetCentrePixel(Surface);
	if (!TestTrue(TEXT("The surface is somewhere the pointer can reach"), Centre.IsSet()))
	{
		Surface->DestroyWidget();
		return false;
	}
	Surface->SetIsEnabled(false);
	Rig.PumpFrames(1);
	int32 MouseDowns = 0;
	int32 TouchDowns = 0;

	TestTrue(TEXT("Clicking the disabled surface completes"), Rig.Driver()->Sequence().MoveToPixel(Centre.GetValue()).Press().Release().WaitFrames(1).Perform());
	TestTrue(TEXT("A finger tapped on it completes"), Rig.Driver()->Sequence().TouchDown(0, Centre.GetValue()).TouchUp(0).WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("The disabled surface sent the hosted widget no press from the mouse"), MouseDowns, 0);
	TestEqual(TEXT("...nor from the finger"), TouchDowns, 0);

	Surface->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the surface once it is enabled completes"), Rig.Driver()->Sequence().MoveToPixel(Centre.GetValue()).Press().Release().WaitFrames(1).Perform());
	TestTrue(TEXT("A finger tapped on it completes"), Rig.Driver()->Sequence().TouchDown(1, Centre.GetValue()).TouchUp(1).WaitFrames(1).Perform());
	CountDowns(Probe, MouseDowns, TouchDowns);
	TestEqual(TEXT("Enabled, the click reached the hosted widget as one press of the mouse"), MouseDowns, 1);
	TestEqual(TEXT("...and the tap as one touch"), TouchDowns, 1);
	Surface->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageUMGHostScaledTest,
	"DreamGUI.Interaction.UMG.OnACanvasScaledToHalfAndToDoubleAPressJustInsideTheSurfacesEdgeReachesTheHostedWidgetAndOneJustOutsideDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageUMGHostScaledTest, "DreamGUI.Interaction.UMG.OnACanvasScaledToHalfAndToDoubleAPressJustInsideTheSurfacesEdgeReachesTheHostedWidgetAndOneJustOutsideDoesNot", "[Pointer][Touch][Scaled]")

/*
 * At half and at double, a surface off the canvas's centre: a click six pixels inside its right edge as drawn sends the
 * hosted widget a press, a click six pixels outside sends nothing; a finger does the same.
 */
bool FDreamCoverageUMGHostScaledTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGHostCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamWidget* Surface = nullptr;
		UDreamUMGDragInteractionProbe* Probe = MakeHostedSurface(*this, Rig, FVector2D(100.0, 0.0), Surface);
		if (!TestNotNull(*At(Scale, TEXT("A hosting surface came up")), Probe))
		{
			return false;
		}
		const TOptional<FBox2D> Rect = FDreamDriverProjection::WidgetToPixelRect(Surface);
		if (!TestTrue(At(Scale, TEXT("The surface is on the viewport")), Rect.IsSet()))
		{
			Surface->DestroyWidget();
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("The surface is drawn at the canvas's scale")), Rect->GetSize().X, 300.0 * Scale, 1.0);
		const FVector2D JustInside(Rect->Max.X - 6.0, Rect->GetCenter().Y);
		const FVector2D JustOutside(Rect->Max.X + 6.0, Rect->GetCenter().Y);
		int32 MouseDowns = 0;
		int32 TouchDowns = 0;

		TestTrue(At(Scale, TEXT("Clicking just inside the edge completes")), Rig.Driver()->Sequence().MoveToPixel(JustInside).Press().Release().WaitFrames(1).Perform());
		CountDowns(Probe, MouseDowns, TouchDowns);
		TestEqual(*At(Scale, TEXT("The click just inside reached the hosted widget")), MouseDowns, 1);
		TestTrue(At(Scale, TEXT("Clicking just outside the edge completes")), Rig.Driver()->Sequence().MoveToPixel(JustOutside).Press().Release().WaitFrames(1).Perform());
		CountDowns(Probe, MouseDowns, TouchDowns);
		TestEqual(*At(Scale, TEXT("The click just outside did not")), MouseDowns, 1);

		TestTrue(At(Scale, TEXT("A finger tapped just inside the edge completes")), Rig.Driver()->Sequence().TouchDown(0, JustInside).TouchUp(0).WaitFrames(1).Perform());
		CountDowns(Probe, MouseDowns, TouchDowns);
		TestEqual(*At(Scale, TEXT("The tap just inside reached the hosted widget as a touch")), TouchDowns, 1);
		TestTrue(At(Scale, TEXT("A finger tapped just outside the edge completes")), Rig.Driver()->Sequence().TouchDown(1, JustOutside).TouchUp(1).WaitFrames(1).Perform());
		CountDowns(Probe, MouseDowns, TouchDowns);
		TestEqual(*At(Scale, TEXT("The tap just outside did not")), TouchDowns, 1);
		Surface->DestroyWidget();
	}
	return true;
}

#endif
