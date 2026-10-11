// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamBorder's pointer events on a canvas scaled to half and to double.
 *
 * UBorder hands SBorder's mouse handlers on, and SBorder is hit where it is painted: Slate's hit-test grid holds each widget
 * at its arranged, DPI-scaled geometry (FHittestGrid::AddWidget, SlateCore/Private/Input/HittestGrid.cpp), so a press just
 * inside the painted edge reaches the border and a press just past it does not, whatever the layer's scale. A tap is the
 * same press through Slate's touch fallback (bTouchFallbackToMouse). The border keeps no keyboard focus of its own (SBorder
 * overrides none of SWidget's focus answers, SWidget::SupportsKeyboardFocus, SlateCore/Private/Widgets/SWidget.cpp:977),
 * so navigation has nothing on it to scale.
 */
namespace DreamBorderScaledTestLocal
{
	const FVector2D BorderSize(300.0, 200.0);
	/** Off the middle, so the scale has to reach the position as well as the size. Canvas units, Y up. */
	const FVector2D BorderPosition(-200.0, 100.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderScaledHitTest,
	"DreamGUI.Border.OnACanvasScaledToHalfAndToDoubleAClickAndATapAreHeardExactlyWhereTheBorderIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamBorderScaledHitTest, "DreamGUI.Border.OnACanvasScaledToHalfAndToDoubleAClickAndATapAreHeardExactlyWhereTheBorderIsDrawn", "[Pointer][Touch][Scaled]")

/*
 * A border that reports its pointer events, 300 by 200 units, off the middle. At each scale it is drawn 300 * scale pixels
 * across; a click three pixels inside its top-left corner and one three pixels inside its bottom-right corner are each one
 * button down and one button up, and clicks four pixels past its right edge and past its bottom edge are heard by nothing.
 * Then the same four points with a finger.
 */
bool FDreamBorderScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderScaledTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamBorder* Border = Rig.MakeControl<UDreamBorder>(TEXT("Frame"), nullptr, BorderSize, BorderPosition);
		if (!TestTrue(At(Scale, TEXT("A border with a face can be made")), Border != nullptr && Border->FaceNode != nullptr))
		{
			return false;
		}
		Border->SetReportMouseEvents(true);
		Border->OnMouseButtonDownEvent.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleBorderButtonDown);
		Border->OnMouseButtonUpEvent.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleBorderButtonUp);
		// Every press below is a press of its own, never the second half of a double click.
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
		Rig.PumpFrames(1);

		const TOptional<FBox2D> Face = DrawnRect(Border->FaceNode.Get());
		if (!TestTrue(At(Scale, TEXT("The border's face is on the viewport")), Face.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The face is drawn at the canvas's scale (%s)"), *Describe(Face.GetValue()))),
			IsDrawnAtScale(Face.GetValue(), BorderSize, Scale));
		// Past the whole control as well as its face, so a press there is past everything the border draws.
		const FBox2D Outer = Covering(Face.GetValue(), DrawnRect(Border));

		TestTrue(At(Scale, TEXT("Clicks just inside the top-left and bottom-right corners complete")),
			ClickAt(Rig, InsideTopLeft(Face.GetValue(), 3.0)) && ClickAt(Rig, InsideBottomRight(Face.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("Each click just inside the drawn edge is a button down")), Listener->BorderButtonDownCount, 2);
		TestEqual(*At(Scale, TEXT("...and a button up")), Listener->BorderButtonUpCount, 2);
		TestTrue(At(Scale, TEXT("Clicks just past the right and bottom edges complete")),
			ClickAt(Rig, PastRightEdge(Outer, 4.0)) && ClickAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("A click just past the drawn edge reaches nothing")), Listener->BorderButtonDownCount, 2);

		TestTrue(At(Scale, TEXT("Taps just inside the two corners complete")),
			TapAt(Rig, InsideTopLeft(Face.GetValue(), 3.0)) && TapAt(Rig, InsideBottomRight(Face.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("Each tap just inside the drawn edge is a button down")), Listener->BorderButtonDownCount, 4);
		TestEqual(*At(Scale, TEXT("...and a button up")), Listener->BorderButtonUpCount, 4);
		TestTrue(At(Scale, TEXT("Taps just past the two edges complete")),
			TapAt(Rig, PastRightEdge(Outer, 4.0)) && TapAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("A tap just past the drawn edge reaches nothing")), Listener->BorderButtonDownCount, 4);
		TestEqual(*At(Scale, TEXT("...down or up")), Listener->BorderButtonUpCount, 4);
	}
	return true;
}

#endif
