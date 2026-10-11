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
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * A SPIN BOX ON A PANEL IN THE LEVEL.
 *
 * SSpinBox is click-to-type and drag-to-scrub. The scrub adds the pointer's travel to the filled fraction of the box's width
 * in its own geometry (SSpinBox::OnMouseMove, Slate/Private/Widgets/Input/SSpinBox.cpp:449-536), so a unit of scrub is the
 * range over the box's width in units; in a UWidgetComponent the pointer's travel is where the interaction's ray lands on the
 * component's window, a finger's by its own pointer index. In text mode the editable text puts its caret at the character
 * under the press in its own local space (FSlateEditableTextLayout::HandleMouseButtonDown), and the arrow keys step a focused
 * spin box (SSpinBox::OnKeyDown) -- the keyboard reaches a widget component's widget as it reaches any focused widget.
 *
 * Every edit begun here is ended here: the panel is not under the rig's root, and a field still being edited when the rig
 * comes down would be reported against the test.
 */
namespace DreamSpinBoxWorldTestLocal
{
	const FVector2D SpinBoxSize(300.0, 40.0);

	/** A spin box InSize over 0..InMax at InStartValue, at InPosition on InPanel (panel units, Y up), laid out. */
	UDreamSpinBox* MakeSpinBox(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition, float InMax, float InStartValue)
	{
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Amount"), InPanel, SpinBoxSize, InPosition);
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

	/** A scrub from InLanded: across InFirstMove, then InPixels more to the right, over separate frames, and let go. */
	bool Scrub(FDreamDriverRig& InRig, const FVector2D& InLanded, double InFirstMove, double InPixels, bool bInWithFinger)
	{
		const FVector2D Crossed = InLanded + FVector2D(InFirstMove, 0.0);
		const FVector2D Halfway = InLanded + FVector2D(InFirstMove + InPixels * 0.5, 0.0);
		const FVector2D End = InLanded + FVector2D(InFirstMove + InPixels, 0.0);
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

	/** The pixel the caret before character InCaretIndex is seen at, off the shown text's own caret table, or unset. */
	TOptional<FVector2D> SeenCaretPixel(const FDreamDriverRig& InRig, UDreamText& InShown, int32 InCaretIndex)
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
		return DreamControlsWorldTest::SeenLocalPoint(InRig, InShown.GetWidget(), FVector2D(CaretPosition.X, CaretPosition.Y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxWorldScrubTest,
	"DreamGUI.SpinBox.OnAWorldPanelAScrubByMouseOrFingerMovesTheValueByTheRangeOverTheWidthForEveryUnitOfTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxWorldScrubTest, "DreamGUI.SpinBox.OnAWorldPanelAScrubByMouseOrFingerMovesTheValueByTheRangeOverTheWidthForEveryUnitOfTravel", "[Pointer][Touch][World]")

/*
 * A spin box 300 units wide over 0..600 at 100 on a panel three metres ahead: two units of value for every panel unit of
 * scrub, a panel unit being seen about two pixels across. The mouse scrubs it 90 pixels to the right past the world pointer's
 * drag distance: the value moves by those 90 pixels in the panel's units, twice over, and the release commits once. Put back
 * to 100, a finger does the same.
 */
bool FDreamSpinBoxWorldScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	const float StartValue = 100.0f;
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
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, Stage.Panel, FVector2D(-40.0, 60.0), 600.0f, StartValue);
	if (!TestNotNull(TEXT("A spin box with a field came up on the panel"), SpinBox))
	{
		return false;
	}
	const TOptional<FBox2D> Whole = SeenRect(Rig, SpinBox);
	const TOptional<FBox2D> Field = SeenRect(Rig, SpinBox->FieldNode.Get());
	if (!TestTrue(TEXT("The camera sees the spin box and its field"), Whole.IsSet() && Field.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The spin box is seen at its size in panel units (%s)"), *Describe(Whole.GetValue())),
		IsSeenAtItsSize(Whole.GetValue(), SpinBoxSize));
	// The range over the width the camera sees: SSpinBox's own measure, in the pixels the gesture is written in.
	const double ValuePerPixel = (SpinBox->GetSliderMaxValue() - SpinBox->GetSliderMinValue()) / FMath::Max(Whole->Max.X - Whole->Min.X, 100.0);
	const float Expected = StartValue + static_cast<float>(90.0 * ValuePerPixel);
	const double FirstMove = DreamControlsWorldTest::PastDragThreshold(Stage.Pointer);
	TStrongObjectPtr<UDreamDragInteractionProbe> Commits(NewObject<UDreamDragInteractionProbe>());
	SpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);

	TestTrue(TEXT("The mouse's scrub completes"), Scrub(Rig, Field->GetCenter(), FirstMove, 90.0, false));
	TestNearlyEqual(*FString::Printf(TEXT("The mouse moved the value by ninety pixels' worth of panel (%.3f a pixel)"), ValuePerPixel),
		SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
	TestEqual(TEXT("...and its release committed once"), Commits->NumFloats(), 1);
	TestFalse(TEXT("...without starting an edit"), SpinBox->InputBehaviour->IsInputActive());

	SpinBox->SetValue(StartValue);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The finger's scrub completes"), Scrub(Rig, Field->GetCenter(), FirstMove, 90.0, true));
	TestNearlyEqual(TEXT("The finger moved the value by ninety pixels' worth of panel"), SpinBox->GetValue(), Expected, static_cast<float>(ValuePerPixel));
	TestEqual(TEXT("...and its lift committed once"), Commits->NumFloats(), 2);
	TestFalse(TEXT("...without starting an edit"), SpinBox->InputBehaviour->IsInputActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxWorldCaretTest,
	"DreamGUI.SpinBox.OnAWorldPanelAClickBetweenTwoDigitsPutsTheCaretThereAndTheDigitTypedGoesIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxWorldCaretTest, "DreamGUI.SpinBox.OnAWorldPanelAClickBetweenTwoDigitsPutsTheCaretThereAndTheDigitTypedGoesIn", "[Pointer][Text][World]")

/*
 * A spin box at 1234 on the panel. A click on the field starts its text mode; after the double-click time a click on the caret
 * before the third digit -- found where the shown text's own caret table puts it and seen through the camera -- leaves
 * nothing selected, the 9 typed next goes in between, Backspace takes it out again, a second 9 goes back in, and Enter commits
 * 12934 -- and, as SSpinBox does, keeps the field in its text mode: SSpinBox::TextField_OnTextCommitted leaves text mode on
 * every commit but Enter's (Slate/Private/Widgets/Input/SSpinBox.cpp:977-982) and USpinBox does not clear the keyboard focus on
 * a commit by default (UMG/Private/Components/SpinBox.cpp:38), the defaults DreamGUI's spin box keeps
 * (DreamGUI.SpinBox.EnterKeepsTheFieldBeingEditedWithTheValueSelectedAndAStepLeavesNothingSelected). Back then ends the edit.
 */
bool FDreamSpinBoxWorldCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
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
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, Stage.Panel, FVector2D(-40.0, 60.0), 100000.0f, 1234.0f);
	if (!TestNotNull(TEXT("A spin box with a field came up on the panel"), SpinBox))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Commits(NewObject<UDreamDragInteractionProbe>());
	SpinBox->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);
	UUITextInput* Input = SpinBox->InputBehaviour.Get();
	const TOptional<FBox2D> Field = SeenRect(Rig, SpinBox->FieldNode.Get());
	if (!TestTrue(TEXT("The camera sees the field"), Field.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A click on the field completes"), ClickAt(Rig, Field->GetCenter()));
	const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
	// The next press a press of its own, not the second half of a double click that would select a word.
	TestTrue(TEXT("Waiting out the double-click time completes"), Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
	if (!TestTrue(TEXT("The click through the world pointer started the field's text mode"), Input->IsInputActive())
		|| !TestTrue(FString::Printf(TEXT("The field shows the value's digits (\"%s\")"), *Input->GetText()), Input->GetText().StartsWith(TEXT("1234"))))
	{
		if (Input->IsInputActive())
		{
			Rig.Driver()->Sequence().Key(EKeys::Enter).Perform();
		}
		return false;
	}
	UDreamText* Shown = ShownTextOf(SpinBox);
	const TOptional<FVector2D> BeforeThirdDigit = Shown != nullptr ? SeenCaretPixel(Rig, *Shown, 2) : TOptional<FVector2D>();
	const TOptional<FVector2D> BeforeFourthDigit = Shown != nullptr ? SeenCaretPixel(Rig, *Shown, 3) : TOptional<FVector2D>();
	if (!TestTrue(TEXT("The camera sees the carets before the third and fourth digits"), BeforeThirdDigit.IsSet() && BeforeFourthDigit.IsSet())
		|| !TestTrue(TEXT("...a digit's width apart"), BeforeFourthDigit->X > BeforeThirdDigit->X + 2.0))
	{
		Rig.Driver()->Sequence().Key(EKeys::Enter).Perform();
		return false;
	}

	TestTrue(TEXT("A click on the caret before the third digit completes"), ClickAt(Rig, BeforeThirdDigit.GetValue()));
	TestTrue(TEXT("The field is still being edited"), Input->IsInputActive());
	TestFalse(TEXT("...with nothing selected"), Input->IsAnyTextSelected());
	TestTrue(TEXT("Typing 9 completes"), Rig.Driver()->Sequence().Type(TEXT("9")).Perform());
	TestTrue(FString::Printf(TEXT("The 9 went in where the click was (\"%s\")"), *Input->GetText()), Input->GetText().StartsWith(TEXT("12934")));
	TestTrue(TEXT("Backspace completes"), Rig.Driver()->Sequence().Key(EKeys::BackSpace).Perform());
	TestTrue(FString::Printf(TEXT("Backspace took the 9 out again (\"%s\")"), *Input->GetText()), Input->GetText().StartsWith(TEXT("1234")));
	TestTrue(TEXT("Typing 9 again completes"), Rig.Driver()->Sequence().Type(TEXT("9")).Perform());
	TestTrue(TEXT("Enter completes"), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
	TestNearlyEqual(TEXT("Enter committed 12934"), SpinBox->GetValue(), 12934.0f, 1.0e-2f);
	TestEqual(TEXT("...once"), Commits->NumFloats(), 1);
	TestTrue(TEXT("...and kept the field being edited, as a spin box's Enter does"), Input->IsInputActive());
	TestTrue(TEXT("Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("Back ended the edit"), Input->IsInputActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxWorldPadTest,
	"DreamGUI.SpinBox.OnAWorldPanelTheDPadReachesTheSpinBoxWithTheRingOnItAsSeenAndTheArrowsStepIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxWorldPadTest, "DreamGUI.SpinBox.OnAWorldPanelTheDPadReachesTheSpinBoxWithTheRingOnItAsSeenAndTheArrowsStepIt", "[Nav][World]")

/*
 * A button left of a spin box over 0..400 at 100 on the panel, the focus on the button. D-pad right takes the focus into the
 * spin box, and the ring sits on the part it landed on where the camera sees it. The keyboard's Up then steps the value up by
 * its default step, one, and Left steps it back -- the focus staying in the spin box, as SSpinBox handles its arrows.
 */
bool FDreamSpinBoxWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxWorldTestLocal;
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
	UDreamSpinBox* SpinBox = MakeSpinBox(Rig, Stage.Panel, FVector2D(60.0, 60.0), 400.0f, 100.0f);
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), Stage.Panel, FVector2D(80.0, 40.0), FVector2D(-200.0, 60.0));
	if (!TestTrue(TEXT("A spin box and a button left of it came up on the panel"), SpinBox != nullptr && Left != nullptr && Left->FaceNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The focus goes onto the button"), PutFocusOn(Rig, Left->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad took the focus into the spin box"), FocusIsIn(Rig, SpinBox));
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the part of the spin box the focus is on, where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("Up completes"), PressKey(Rig, EKeys::Up));
	TestNearlyEqual(TEXT("Up stepped the value up by one"), SpinBox->GetValue(), 101.0f, 1.0e-3f);
	TestTrue(TEXT("Left completes"), PressKey(Rig, EKeys::Left));
	TestNearlyEqual(TEXT("Left stepped it back down by one"), SpinBox->GetValue(), 100.0f, 1.0e-3f);
	TestTrue(TEXT("...and the focus never left the spin box"), FocusIsIn(Rig, SpinBox));
	TestFalse(TEXT("...nor did the arrows start an edit"), SpinBox->InputBehaviour->IsInputActive());
	return true;
}

#endif
