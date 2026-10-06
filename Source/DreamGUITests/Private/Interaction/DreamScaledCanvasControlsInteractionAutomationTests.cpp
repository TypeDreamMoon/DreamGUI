// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBox.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * A SLIDER, A SCROLL BOX AND A TEXT FIELD ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * UMG lays a widget out in the DPI-scaled units of its layer and hands it pointer events in desktop pixels, and every
 * widget turns those into its own space through its geometry (FGeometry::AbsoluteToLocal): SSlider's PositionToValue
 * compares the pointer with the length of its track in local units, SScrollBox scrolls by the pointer's delta divided by
 * the geometry's scale, and SEditableText puts its cursor at the character under the local point. So whatever the scale,
 * a drag to the middle of a slider's travel is a value of one half, a drag of the content keeps it under the pointer --
 * the offset is the travel in the canvas's units, not in pixels -- and a click between two characters puts the caret
 * between them. The rig's canvas scales with the screen from a reference resolution chosen to make 720 pixels come out
 * at 0.5 and at 2 (DreamGUI.Driver.CanvasScale.* checks the scale itself); every test runs at both.
 */
namespace DreamScaledCanvasControlsTestLocal
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

	/** "[scale 0.5] what", so a failure says which scale it failed at. */
	FString At(double InScale, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, InWhat);
	}

	/** The rig at InScale, bound, its scale checked; false when it did not come up as asked. */
	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	/** The viewport pixel of the caret before character InCaretIndex, off the text's own caret table, or unset. */
	TOptional<FVector2D> CaretPixel(UDreamText& InShown, int32 InCaretIndex)
	{
		int32 CaretIndex = InCaretIndex;
		FVector2f CaretPosition(0.0f, 0.0f);
		int32 LineIndex = 0;
		int32 VisibleStartIndex = 0;
		InShown.FindCaretByIndex(CaretIndex, CaretPosition, LineIndex, VisibleStartIndex);
		if (CaretIndex != InCaretIndex)
		{
			return TOptional<FVector2D>();
		}
		return FDreamDriverProjection::WidgetLocalPointToPixel(InShown.GetWidget(), FVector2D(CaretPosition.X, CaretPosition.Y));
	}

	UDreamText* ShownTextOf(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->TextNode != nullptr ? Cast<UDreamText>(InField->TextNode->GetVisual()) : nullptr;
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaledSliderHalfwayTest,
	"DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleDraggingTheHandleHalfwayAlongItsTravelMovesTheValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScaledSliderHalfwayTest, "DreamGUI.Slider.OnACanvasScaledToHalfAndToDoubleDraggingTheHandleHalfwayAlongItsTravelMovesTheValueHalfway", "[Pointer][Scaled]")

/*
 * SSlider::PositionToValue: the pointer's local position along the length the handle's centre can reach, over that
 * length. A 400-unit slider is 200 pixels long at half scale and 800 at double; the handle dragged to the middle of its
 * travel as drawn is one half either way, to within a pixel's worth of value.
 */
bool FDreamScaledSliderHalfwayTest::RunTest(const FString& Parameters)
{
	using namespace DreamScaledCanvasControlsTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamSlider* Slider = Rig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0));
		Rig.PumpFrames(2);
		if (!TestTrue(At(Scale, TEXT("The slider came up with a handle and a handle area")),
			Slider != nullptr && Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
		{
			return false;
		}
		FDreamDriverRef Driver = Rig.Driver();
		FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()));
		const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
		const TOptional<FVector2D> Grip = Handle->GetCentrePixel();
		if (!TestTrue(At(Scale, TEXT("The handle and its travel are on the viewport")), Travel.IsSet() && Grip.IsSet()))
		{
			return false;
		}
		const double TravelLength = Travel->Max.X - Travel->Min.X;
		// The travel itself scales: the same 400 units are half or twice as many pixels.
		TestNearlyEqual(*At(Scale, TEXT("The handle's travel is drawn at the canvas's scale")),
			TravelLength, Slider->HandleAreaNode->GetWidth() * Scale, 1.0);
		const float OnePixel = static_cast<float>(1.0 / FMath::Max(TravelLength, 1.0));
		TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
		Slider->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

		const double TargetX = Travel->Min.X + 0.5 * TravelLength;
		TestTrue(At(Scale, TEXT("The drag to the middle of the travel completes")), Handle->DragBy(FVector2D(TargetX - Grip->X, 0.0)));
		TestNearlyEqual(*At(Scale, TEXT("The value is halfway")), Slider->GetValue(), 0.5f, 1.5f * OnePixel);
		TestTrue(At(Scale, TEXT("...and the drag said so")), Values->NumFloats() >= 1);
		TestNearlyEqual(*At(Scale, TEXT("...the last change carrying the value it ended on")), Values->LastFloat(-1.0f), 0.5f, 1.5f * OnePixel);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaledScrollBoxDragTest,
	"DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleADragScrollsByThePointersTravelInTheCanvassUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScaledScrollBoxDragTest, "DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleADragScrollsByThePointersTravelInTheCanvassUnits", "[Pointer][Scaled]")

/*
 * SScrollBox scrolls by the pointer's delta in its own space (the cursor delta divided by the geometry's scale), so the
 * content stays under the pointer that grabbed it: 150 pixels of right-button drag is 300 units of offset at half scale
 * and 75 at double. A box that scrolled in pixels would get one of the two wrong by a factor of four.
 */
bool FDreamScaledScrollBoxDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScaledCanvasControlsTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale)
			|| !TestTrue(At(Scale, TEXT("Its UI has begun play, as a game's has")), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
		{
			return false;
		}
		// 320 pixels tall at either scale -- 640 units at half, 160 at double -- so the whole drag stays on the box.
		UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 320.0 / Scale));
		if (!TestTrue(At(Scale, TEXT("The box came up with a viewport and a content node")),
			Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			return false;
		}
		for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
		{
			Rig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		Rig.PumpFrames(2);
		const double Travel = 150.0;
		const float Expected = static_cast<float>(Travel / Scale);
		if (!TestTrue(FString::Printf(TEXT("[scale %.1f] There is more than %.0f units to scroll (end %.1f)"), Scale, Expected, Box->GetScrollOffsetOfEnd()),
			Box->GetScrollOffsetOfEnd() > Expected + 10.0f))
		{
			return false;
		}
		const TOptional<FBox2D> BoxRect = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->GetPixelRect();
		if (!TestTrue(At(Scale, TEXT("The box's viewport is on the viewport, taller than the drag")),
			BoxRect.IsSet() && BoxRect->Max.Y - BoxRect->Min.Y > Travel + 60.0))
		{
			return false;
		}
		TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
		Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

		// Grabbed near the bottom, past the raycaster's threshold -- which is in canvas units and so in pixels scales with
		// the canvas -- on the first move, then the rest in two halves.
		const FVector2D Grab(BoxRect->GetCenter().X, BoxRect->Max.Y - 40.0);
		const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
		const double LaterMove = (Travel - FirstMove) * 0.5;
		TestTrue(At(Scale, TEXT("The right-button drag up the box completes")),
			Rig.Driver()->Sequence()
				.MoveToPixel(Grab)
				.Press(EDreamUIMouseButtonType::Right)
				.MoveBy(FVector2D(0.0, -FirstMove))
				.MoveBy(FVector2D(0.0, -LaterMove))
				.MoveBy(FVector2D(0.0, -LaterMove))
				.WaitFrames(1)
				.Perform());
		const float Offset = Box->GetScrollOffset();
		TestTrue(At(Scale, TEXT("Letting go completes")), Rig.Driver()->Sequence().Release(EDreamUIMouseButtonType::Right).Perform());
		TestNearlyEqual(*FString::Printf(TEXT("[scale %.1f] 150 pixels of drag scrolled %.0f units, the travel in the canvas's units (got %.1f)"),
			Scale, Expected, Offset), Offset, Expected, static_cast<float>(1.5 / Scale));
		TestTrue(At(Scale, TEXT("The drag was reported as the user scrolling")), UserScrolled->NumFloats() >= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaledTextCaretTest,
	"DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScaledTextCaretTest, "DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoCharactersPutsTheCaretBetweenThem", "[Pointer][Text][Scaled]")

/*
 * FSlateEditableTextLayout::HandleMouseButtonDown moves the cursor to the character boundary nearest the press, found in
 * the text's local space. "alpha bravo" being edited, a click on the caret before "bravo" -- found where the text's own
 * caret table puts it and projected through the scaled canvas -- leaves nothing selected and the X typed next goes in
 * there, at either scale.
 */
bool FDreamScaledTextCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamScaledCanvasControlsTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0));
		if (!TestNotNull(*At(Scale, TEXT("A field came up")), Field))
		{
			return false;
		}
		Rig.PumpFrames(1);
		const bool bTyped = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")))->Type(TEXT("alpha bravo"));
		const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
		// The next press a press of its own, not the second half of a double click that would select a word.
		const bool bWaited = Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform();
		if (!TestTrue(At(Scale, TEXT("Clicking into the field, typing and waiting out the double-click time complete")), bTyped && bWaited)
			|| !TestEqual(*At(Scale, TEXT("The field holds what was typed")), Field->GetText(), FString(TEXT("alpha bravo"))))
		{
			return false;
		}
		UDreamText* Shown = ShownTextOf(Field);
		const TOptional<FVector2D> BeforeSecondWord = Shown != nullptr ? CaretPixel(*Shown, 6) : TOptional<FVector2D>();
		const TOptional<FVector2D> AtTheEnd = Shown != nullptr ? CaretPixel(*Shown, 11) : TOptional<FVector2D>();
		if (!TestTrue(At(Scale, TEXT("The carets before \"bravo\" and at the end are on the viewport")), BeforeSecondWord.IsSet() && AtTheEnd.IsSet())
			|| !TestTrue(At(Scale, TEXT("...the end well to the right of the first")), AtTheEnd->X > BeforeSecondWord->X + 4.0 * Scale))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("A click on the caret before \"bravo\" completes")),
			Rig.Driver()->Sequence().MoveToPixel(BeforeSecondWord.GetValue()).Press().Release().Perform());
		TestTrue(At(Scale, TEXT("The field is still being edited")), IsEditing(Field));
		TestFalse(At(Scale, TEXT("...with nothing selected")), Field->IsAnyTextSelected());
		TestTrue(At(Scale, TEXT("Typing X completes")), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
		TestEqual(*At(Scale, TEXT("The X went in where the click was")), Field->GetText(), FString(TEXT("alpha Xbravo")));
		TestTrue(At(Scale, TEXT("Enter ends the edit")), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
	}
	return true;
}

#endif
