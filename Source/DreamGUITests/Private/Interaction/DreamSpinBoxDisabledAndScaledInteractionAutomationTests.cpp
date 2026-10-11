// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A SPIN BOX DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * SSpinBox is click-to-type and drag-to-scrub. The scrub adds the pointer's travel to the filled fraction of the box's DRAWN
 * width (SSpinBox::OnMouseMove, Slate/Private/Widgets/Input/SSpinBox.cpp:449-536: FractionFilled is kept against
 * MyGeometry.GetDrawSize().X, and the cursor's delta is in the same pixels), so a pixel of scrub is the range over the drawn
 * width at any scale; a finger reaches the same handlers through Slate's touch fallback. In text mode the editable text puts
 * its caret at the character under the press in its own local space (FSlateEditableTextLayout::HandleMouseButtonDown), and
 * the arrow keys step a focused spin box (SSpinBox::OnKeyDown). Disabled, it is off the hit path (FHittestGrid::
 * GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and hears no key (FSlateApplication routes OnKeyDown only to
 * enabled widgets, Slate/Private/Framework/Application/SlateApplication.cpp:5049): no scrub, no edit, nothing typed.
 */
namespace DreamSpinBoxConfigTestLocal
{
	const FVector2D SpinBoxSize(400.0, 40.0);

	/** A spin box InSize over 0..InMax at InStartValue, at InPosition (canvas units, Y up), laid out. */
	UDreamSpinBox* MakeSpinBox(FDreamDriverRig& InRig, const FVector2D& InPosition, float InMax, float InStartValue)
	{
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Amount"), nullptr, SpinBoxSize, InPosition);
		if (SpinBox == nullptr || SpinBox->FieldNode == nullptr || SpinBox->InputBehaviour == nullptr)
		{
			return nullptr;
		}
		// Range before value, so the value is clamped against the range it is meant for.
		SpinBox->SetMinValue(0.0f);
		SpinBox->SetMaxValue(InMax);
		SpinBox->SetValue(InStartValue);
		InRig.PumpFrames(2);
		return SpinBox;
	}

	/** A scrub from InLanded: across the drag distance, then InPixels more to the right, over separate frames, and let go. */
	bool Scrub(FDreamDriverRig& InRig, const FVector2D& InLanded, double InPixels, bool bInWithFinger)
	{
		const double FirstMove = DreamControlsConfigTest::PastDragThreshold(InRig);
		const FVector2D Crossed = InLanded + FVector2D(FirstMove, 0.0);
		const FVector2D Halfway = InLanded + FVector2D(FirstMove + InPixels * 0.5, 0.0);
		const FVector2D End = InLanded + FVector2D(FirstMove + InPixels, 0.0);
		if (bInWithFinger)
		{
			return InRig.Driver()->Sequence()
				.TouchDown(0, InLanded).TouchMoveTo(0, Crossed).TouchMoveTo(0, Halfway).TouchMoveTo(0, End).WaitFrames(1).TouchUp(0)
				.Perform();
		}
		return InRig.Driver()->Sequence()
			.MoveToPixel(InLanded).Press().MoveToPixel(Crossed).MoveToPixel(Halfway).MoveToPixel(End).WaitFrames(1).Release()
			.Perform();
	}

	UDreamText* ShownTextOf(const UDreamSpinBox* InSpinBox)
	{
		return InSpinBox != nullptr && InSpinBox->ValueTextNode != nullptr ? Cast<UDreamText>(InSpinBox->ValueTextNode->GetVisual()) : nullptr;
	}

	/** The viewport pixel of the caret before character InCaretIndex, off the shown text's own caret table, or unset. */
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxScaledScrubTest,
	"DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleAScrubByMouseOrFingerMovesTheValueByTheRangeOverTheDrawnWidthAPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxScaledScrubTest, "DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleAScrubByMouseOrFingerMovesTheValueByTheRangeOverTheDrawnWidthAPixel", "[Pointer][Touch][Scaled]")

/*
 * A spin box 400 units wide over 0..400 at 100: 200 pixels wide at half scale, two units of value a pixel; 800 at double, half
 * a unit a pixel. The mouse scrubs it 90 pixels to the right past the drag distance: the value moves by 90 pixels' worth and
 * the release commits once. Put back to 100, a finger does the same.
 */
bool FDreamSpinBoxScaledScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	const float StartValue = 100.0f;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamSpinBox* SpinBox = MakeSpinBox(Rig, FVector2D(-60.0, 80.0), 400.0f, StartValue);
		if (!TestNotNull(*At(Scale, TEXT("A spin box with a field came up")), SpinBox))
		{
			return false;
		}
		const TOptional<FBox2D> Whole = DrawnRect(SpinBox);
		const TOptional<FBox2D> Field = DrawnRect(SpinBox->FieldNode.Get());
		if (!TestTrue(At(Scale, TEXT("The spin box and its field are on the viewport")), Whole.IsSet() && Field.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The spin box is drawn at the canvas's scale (%s)"), *Describe(Whole.GetValue()))),
			IsDrawnAtScale(Whole.GetValue(), SpinBoxSize, Scale));
		const double ValuePerPixel = (SpinBox->GetSliderMaxValue() - SpinBox->GetSliderMinValue()) / FMath::Max(Whole->Max.X - Whole->Min.X, 100.0);
		const float Expected = StartValue + static_cast<float>(90.0 * ValuePerPixel);
		TStrongObjectPtr<UDreamDragInteractionProbe> Commits(NewObject<UDreamDragInteractionProbe>());
		SpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);

		TestTrue(At(Scale, TEXT("The mouse's scrub completes")), Scrub(Rig, Field->GetCenter(), 90.0, false));
		TestNearlyEqual(*At(Scale, FString::Printf(TEXT("The mouse moved the value by ninety pixels' worth (%.3f a pixel)"), ValuePerPixel)),
			SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
		TestEqual(*At(Scale, TEXT("...and its release committed once")), Commits->NumFloats(), 1);

		SpinBox->SetValue(StartValue);
		Rig.PumpFrames(1);
		TestTrue(At(Scale, TEXT("The finger's scrub completes")), Scrub(Rig, Field->GetCenter(), 90.0, true));
		TestNearlyEqual(*At(Scale, TEXT("The finger moved the value by ninety pixels' worth")), SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
		TestEqual(*At(Scale, TEXT("...and its lift committed once")), Commits->NumFloats(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxDisabledFingerTest,
	"DreamGUI.SpinBox.AFingerScrubbingOrTappingADisabledSpinBoxNeitherMovesItsValueNorStartsAnEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxDisabledFingerTest, "DreamGUI.SpinBox.AFingerScrubbingOrTappingADisabledSpinBoxNeitherMovesItsValueNorStartsAnEdit", "[Touch][Disabled]")

/*
 * A disabled spin box at 100: a finger scrubbing its field 120 pixels changes nothing and commits nothing, and a tap on the
 * field starts no edit. Enabled again, the same scrub moves the value.
 */
bool FDreamSpinBoxDisabledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const float StartValue = 100.0f;
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, FVector2D::ZeroVector, 400.0f, StartValue);
	if (!TestNotNull(TEXT("A spin box with a field came up"), SpinBox))
	{
		return false;
	}
	SpinBox->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamDragInteractionProbe> Changes(NewObject<UDreamDragInteractionProbe>());
	TStrongObjectPtr<UDreamDragInteractionProbe> Commits(NewObject<UDreamDragInteractionProbe>());
	SpinBox->OnValueChanged.AddDynamic(Changes.Get(), &UDreamDragInteractionProbe::RecordFloat);
	SpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);
	FDreamElementRef Field = Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()));
	const TOptional<FVector2D> FieldCentre = Field->GetCentrePixel();
	if (!TestTrue(TEXT("The field is on the viewport"), FieldCentre.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A finger's scrub across the disabled spin box completes"), Scrub(Rig, FieldCentre.GetValue(), 120.0, true));
	TestNearlyEqual(TEXT("The value is where it was"), SpinBox->GetValue(), StartValue, 1.0e-4f);
	TestEqual(TEXT("No change was reported"), Changes->NumFloats(), 0);
	TestEqual(TEXT("...and nothing was committed"), Commits->NumFloats(), 0);
	TestTrue(TEXT("A tap on the disabled field completes"), Field->Tap());
	TestFalse(TEXT("The tap started no edit"), SpinBox->InputBehaviour->IsInputActive());

	SpinBox->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same scrub with the spin box enabled again completes"), Scrub(Rig, FieldCentre.GetValue(), 120.0, true));
	TestTrue(TEXT("Enabled again, the scrub moves the value"), SpinBox->GetValue() > StartValue + 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxDisabledTypingTest,
	"DreamGUI.SpinBox.ADisabledSpinBoxIsNotEditedByAClickAndTheDigitsAndEnterTypedAfterChangeNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxDisabledTypingTest, "DreamGUI.SpinBox.ADisabledSpinBoxIsNotEditedByAClickAndTheDigitsAndEnterTypedAfterChangeNothing", "[Pointer][Text][Disabled]")

/*
 * A disabled spin box at 50: a click on its field starts no text mode and gives it no focus, and 4, 2 and Enter pressed after
 * it type nothing into it and commit nothing -- the value is still 50. Enabled again, the same click edits it, and 4, 2 and
 * Enter commit 42.
 */
bool FDreamSpinBoxDisabledTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, FVector2D::ZeroVector, 400.0f, 50.0f);
	if (!TestNotNull(TEXT("A spin box with a field came up"), SpinBox))
	{
		return false;
	}
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	SpinBox->OnValueCommitted.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleValueCommitted);
	SpinBox->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Field = Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()));

	TestTrue(TEXT("Clicking the disabled field completes"), Field->Click());
	TestFalse(TEXT("The click started no edit"), SpinBox->InputBehaviour->IsInputActive());
	TestFalse(TEXT("...and gave the spin box no focus"), FocusIsIn(Rig, SpinBox));
	TestTrue(TEXT("Pressing 4, 2 and Enter completes"),
		Rig.Driver()->Sequence().Key(EKeys::Four).Key(EKeys::Two).Key(EKeys::Enter).Perform());
	TestNearlyEqual(TEXT("The keys typed nothing into it: the value is still 50"), SpinBox->GetValue(), 50.0f, 1.0e-3f);
	TestEqual(TEXT("...and nothing was committed"), Listener->ValueCommittedCount, 0);

	SpinBox->SetIsEnabled(true);
	Rig.PumpFrames(1);
	const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
	TestTrue(TEXT("Clicking the field once it is enabled completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Click(FDreamBy::Widget(SpinBox->FieldNode.Get())).Perform());
	if (!TestTrue(TEXT("Enabled, the same click edits it"), SpinBox->InputBehaviour->IsInputActive()))
	{
		return false;
	}
	// Whatever the click left selected goes: the field holds only what is typed next.
	TestTrue(TEXT("Clearing the field and typing 42 completes"),
		Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Key(EKeys::BackSpace).Type(TEXT("42")).Key(EKeys::Enter).Perform());
	TestNearlyEqual(TEXT("...and Enter commits 42"), SpinBox->GetValue(), 42.0f, 1.0e-3f);
	TestEqual(TEXT("...once"), Listener->ValueCommittedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxScaledCaretTest,
	"DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoDigitsPutsTheCaretBetweenThemAndTheDigitTypedGoesThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxScaledCaretTest, "DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoDigitsPutsTheCaretBetweenThemAndTheDigitTypedGoesThere", "[Pointer][Text][Scaled]")

/*
 * A spin box at 1234, at each scale. A click on the field starts its text mode; after the double-click time a click on the
 * caret before the third digit -- found where the shown text's own caret table puts it and projected through the scaled
 * canvas -- leaves nothing selected, the 9 typed next goes in between, and Enter commits 12934.
 */
bool FDreamSpinBoxScaledCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamSpinBox* SpinBox = MakeSpinBox(Rig, FVector2D(-60.0, 80.0), 100000.0f, 1234.0f);
		if (!TestNotNull(*At(Scale, TEXT("A spin box with a field came up")), SpinBox))
		{
			return false;
		}
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		SpinBox->OnValueCommitted.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleValueCommitted);
		UUITextInput* Input = SpinBox->InputBehaviour.Get();

		TestTrue(At(Scale, TEXT("A click on the field completes")), Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()))->Click());
		const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
		// The next press a press of its own, not the second half of a double click that would select a word.
		TestTrue(At(Scale, TEXT("Waiting out the double-click time completes")), Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
		if (!TestTrue(At(Scale, TEXT("The click started the field's text mode")), Input->IsInputActive())
			|| !TestTrue(At(Scale, FString::Printf(TEXT("The field shows the value's digits (\"%s\")"), *Input->GetText())), Input->GetText().StartsWith(TEXT("1234"))))
		{
			return false;
		}
		UDreamText* Shown = ShownTextOf(SpinBox);
		const TOptional<FVector2D> BeforeThirdDigit = Shown != nullptr ? CaretPixel(*Shown, 2) : TOptional<FVector2D>();
		const TOptional<FVector2D> BeforeFourthDigit = Shown != nullptr ? CaretPixel(*Shown, 3) : TOptional<FVector2D>();
		if (!TestTrue(At(Scale, TEXT("The carets before the third and fourth digits are on the viewport")), BeforeThirdDigit.IsSet() && BeforeFourthDigit.IsSet())
			|| !TestTrue(At(Scale, TEXT("...a digit's width apart as drawn")), BeforeFourthDigit->X > BeforeThirdDigit->X + 2.0 * Scale))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("A click on the caret before the third digit completes")),
			Rig.Driver()->Sequence().MoveToPixel(BeforeThirdDigit.GetValue()).Press().Release().Perform());
		TestTrue(At(Scale, TEXT("The field is still being edited")), Input->IsInputActive());
		TestFalse(At(Scale, TEXT("...with nothing selected")), Input->IsAnyTextSelected());
		TestTrue(At(Scale, TEXT("Typing 9 completes")), Rig.Driver()->Sequence().Type(TEXT("9")).Perform());
		TestTrue(At(Scale, FString::Printf(TEXT("The 9 went in where the click was (\"%s\")"), *Input->GetText())), Input->GetText().StartsWith(TEXT("12934")));
		TestTrue(At(Scale, TEXT("Enter completes")), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
		TestNearlyEqual(*At(Scale, TEXT("Enter committed 12934")), SpinBox->GetValue(), 12934.0f, 1.0e-2f);
		TestEqual(*At(Scale, TEXT("...once")), Listener->ValueCommittedCount, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxScaledPadTest,
	"DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheSpinBoxWithTheRingOnItAndTheArrowsStepIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxScaledPadTest, "DreamGUI.SpinBox.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheSpinBoxWithTheRingOnItAndTheArrowsStepIt", "[Nav][Scaled]")

/*
 * A button left of a spin box over 0..400 at 100, the focus on the button. D-pad right takes the focus into the spin box, and
 * the ring sits on the part it landed on where that part is drawn. The keyboard's Up then steps the value up by its default
 * step, one, and Left steps it back -- the focus staying in the spin box, as SSpinBox handles its arrows.
 */
bool FDreamSpinBoxScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxConfigTestLocal;
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
		UDreamSpinBox* SpinBox = MakeSpinBox(Rig, FVector2D(60.0, 80.0), 400.0f, 100.0f);
		UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, FVector2D(80.0, 40.0), FVector2D(-220.0, 80.0));
		if (!TestTrue(At(Scale, TEXT("A spin box and a button left of it came up")), SpinBox != nullptr && Left != nullptr && Left->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!TestTrue(At(Scale, TEXT("The focus goes onto the button")), PutFocusOn(Rig, Left->FaceNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestTrue(At(Scale, TEXT("The D-pad took the focus into the spin box")), FocusIsIn(Rig, SpinBox));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the part of the spin box the focus is on, where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("Up completes")), PressKey(Rig, EKeys::Up));
		TestNearlyEqual(*At(Scale, TEXT("Up stepped the value up by one")), SpinBox->GetValue(), 101.0f, 1.0e-3f);
		TestTrue(At(Scale, TEXT("Left completes")), PressKey(Rig, EKeys::Left));
		TestNearlyEqual(*At(Scale, TEXT("Left stepped it back down by one")), SpinBox->GetValue(), 100.0f, 1.0e-3f);
		TestTrue(At(Scale, TEXT("...and the focus never left the spin box")), FocusIsIn(Rig, SpinBox));
	}
	return true;
}

#endif
