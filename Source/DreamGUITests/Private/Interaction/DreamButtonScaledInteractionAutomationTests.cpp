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
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamButton on a canvas scaled to half and to double, clicked, tapped and walked to with the pad.
 *
 * UButton is SButton, and SButton is hit where it is painted (the hit-test grid holds its arranged, DPI-scaled geometry,
 * FHittestGrid::AddWidget, SlateCore/Private/Input/HittestGrid.cpp): a press and release just inside the painted edge is a
 * click (SButton::OnMouseButtonUp, Slate/Private/Widgets/Input/SButton.cpp), a finger's tap is the same press and release
 * (SButton.cpp:354, :410), and a press just past the edge is not the button's. The pad walks the same geometry
 * (FHittestGrid::FindNextFocusableWidget) and Accept presses the focused button (SButton::OnKeyDown, :296); the focus is
 * drawn on the button's own geometry, so the ring sits on it as drawn.
 */
namespace DreamButtonScaledTestLocal
{
	const FVector2D ButtonSize(160.0, 50.0);

	struct FScaledButton
	{
		UDreamButton* Button = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;
	};

	/** A button at InPosition (canvas units, Y up), its clicks counted. */
	FScaledButton PlaceButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition)
	{
		FScaledButton Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		Placed.Button = InRig.MakeControl<UDreamButton>(InName, nullptr, ButtonSize, InPosition);
		if (Placed.Button != nullptr && Placed.Button->FaceNode != nullptr)
		{
			Placed.Button->OnClicked.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
			Placed.Button->OnPressed.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
		}
		else
		{
			Placed.Button = nullptr;
		}
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonScaledHitTest,
	"DreamGUI.Button.OnACanvasScaledToHalfAndToDoubleAClickAndATapClickTheButtonExactlyWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonScaledHitTest, "DreamGUI.Button.OnACanvasScaledToHalfAndToDoubleAClickAndATapClickTheButtonExactlyWhereItIsDrawn", "[Pointer][Touch][Scaled]")

/*
 * A button off the middle, drawn ButtonSize * scale pixels. The mouse clicks three pixels inside its top-left corner and three
 * inside its bottom-right: two clicks. It clicks four pixels past its right edge and past its bottom edge: nothing, not even a
 * press. A finger does the same at the same four points.
 */
bool FDreamButtonScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonScaledTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FScaledButton Placed = PlaceButton(Rig, TEXT("Play"), FVector2D(-180.0, 90.0));
		if (!TestNotNull(*At(Scale, TEXT("A button with a face can be made")), Placed.Button))
		{
			return false;
		}
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
		Rig.PumpFrames(1);
		const TOptional<FBox2D> Face = DrawnRect(Placed.Button->FaceNode.Get());
		if (!TestTrue(At(Scale, TEXT("The button's face is on the viewport")), Face.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The face is drawn at the canvas's scale (%s)"), *Describe(Face.GetValue()))),
			IsDrawnAtScale(Face.GetValue(), ButtonSize, Scale));
		UDreamPressInteractionListener* Listener = Placed.Listener.Get();
		// Past the whole control as well as its face, so a press there is past everything the button draws.
		const FBox2D Outer = Covering(Face.GetValue(), DrawnRect(Placed.Button));

		TestTrue(At(Scale, TEXT("Clicks just inside the top-left and bottom-right corners complete")),
			ClickAt(Rig, InsideTopLeft(Face.GetValue(), 3.0)) && ClickAt(Rig, InsideBottomRight(Face.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("Each click just inside the drawn edge clicks the button")), Listener->ClickedCount, 2);
		TestTrue(At(Scale, TEXT("Clicks just past the right and bottom edges complete")),
			ClickAt(Rig, PastRightEdge(Outer, 4.0)) && ClickAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("A click just past the drawn edge does not press the button")), Listener->PressedCount, 2);
		TestEqual(*At(Scale, TEXT("...nor click it")), Listener->ClickedCount, 2);

		TestTrue(At(Scale, TEXT("Taps just inside the two corners complete")),
			TapAt(Rig, InsideTopLeft(Face.GetValue(), 3.0)) && TapAt(Rig, InsideBottomRight(Face.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("Each tap just inside the drawn edge clicks the button")), Listener->ClickedCount, 4);
		TestTrue(At(Scale, TEXT("Taps just past the two edges complete")),
			TapAt(Rig, PastRightEdge(Outer, 4.0)) && TapAt(Rig, PastBottomEdge(Outer, 4.0)));
		TestEqual(*At(Scale, TEXT("A tap just past the drawn edge does not press the button")), Listener->PressedCount, 4);
		TestEqual(*At(Scale, TEXT("...nor click it")), Listener->ClickedCount, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonScaledPadTest,
	"DreamGUI.Button.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextButtonWithTheRingOnItAndConfirmClicksIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonScaledPadTest, "DreamGUI.Button.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextButtonWithTheRingOnItAndConfirmClicksIt", "[Nav][Scaled]")

/*
 * Two buttons side by side, the left one focused as a screen focuses its first control. D-pad right takes the focus to the
 * right-hand button, the ring comes to sit on it where it is drawn and at its drawn size, and the pad's Accept clicks it --
 * and only it.
 */
bool FDreamButtonScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonScaledTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FScaledButton Left = PlaceButton(Rig, TEXT("Left"), FVector2D(-150.0, 100.0));
		const FScaledButton Right = PlaceButton(Rig, TEXT("Right"), FVector2D(150.0, 100.0));
		if (!TestTrue(At(Scale, TEXT("Two buttons with faces can be made")), Left.Button != nullptr && Right.Button != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!TestTrue(At(Scale, TEXT("The focus goes onto the left button")), PutFocusOn(Rig, Left.Button->FaceNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		if (!TestTrue(At(Scale, TEXT("The D-pad took the focus to the right-hand button")), FocusIsIn(Rig, Right.Button)))
		{
			return false;
		}
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the right-hand button where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept clicked the focused button")), Right.Listener->ClickedCount, 1);
		TestEqual(*At(Scale, TEXT("...and not the one the focus left")), Left.Listener->ClickedCount, 0);
	}
	return true;
}

#endif
