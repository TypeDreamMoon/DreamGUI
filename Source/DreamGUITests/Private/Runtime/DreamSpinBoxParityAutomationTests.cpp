// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UITextInput.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The spin box's range, in the shape UMG gives it: a bound is a number AND whether there is one.
 *
 * This control could only ever say "between these two numbers". USpinBox can also say "no bottom"
 * and "no top", and a settings screen genuinely wants that -- an offset that may be negative, a
 * count with no ceiling -- so the override pair is what closes the gap. Everything below is written
 * against the arithmetic rather than the panel, because an open range is exactly where the scrub
 * maths turns into a NaN if nobody guards it: float's floor to float's ceiling is a span that
 * overflows to infinity, and infinity times a fraction is not a number the value comes back from.
 *
 * Headless, like the rest of the control suite: no world, no layout pass. So nothing here drags --
 * a scrub needs pointer event data with a press transform on it -- and what is asserted about the
 * scrub is the question it asks BEFORE moving anything (HasFiniteSliderRange).
 */
namespace DreamSpinBoxParityTestLocal
{
	template<class T>
	T* Author(float InWidth = 220.0f, float InHeight = 34.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}

	/** What the value's own paragraph is spelling, which is where a reader of the box looks. */
	FString SpelledValue(const UDreamWidget* InNode)
	{
		const UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr;
		return TextVisual != nullptr ? TextVisual->GetText().ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxOpenRangeTest,
	"DreamGUI.SpinBox.Parity.ClearingABoundMeansThereIsNoBoundAndNotABoundOfZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxOpenRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Spin(Author<UDreamSpinBox>());
	Spin->Initialize();

	// The state every existing spin box is in, and the reason the override bits default to ticked:
	// an unbounded default would change what all of them do.
	TestEqual(TEXT("a fresh spin box is bounded below"), Spin->GetMinValue(), 0.0f);
	TestEqual(TEXT("and above"), Spin->GetMaxValue(), 100.0f);
	Spin->SetValue(-5.0f);
	TestEqual(TEXT("so a value under the bottom is clamped to it"), Spin->GetValue(), 0.0f);

	// Taking the bottom away. "No minimum" is float's floor, which is what every caller that clamps
	// with this answer needs -- a stored 0 returned for a bound that is switched off would clamp to
	// a limit the author explicitly removed.
	Spin->ClearMinValue();
	TestEqual(TEXT("clearing the bottom answers with float's floor"),
		Spin->GetMinValue(), TNumericLimits<float>::Lowest());
	Spin->SetValue(-5.0f);
	TestEqual(TEXT("and a negative value is now legal"), Spin->GetValue(), -5.0f);

	// The top is still there, and still clamps.
	Spin->SetValue(500.0f);
	TestEqual(TEXT("while the top still holds"), Spin->GetValue(), 100.0f);

	Spin->ClearMaxValue();
	TestEqual(TEXT("clearing the top answers with float's ceiling"),
		Spin->GetMaxValue(), TNumericLimits<float>::Max());
	Spin->SetValue(500.0f);
	TestEqual(TEXT("and a value over the old top is now legal"), Spin->GetValue(), 500.0f);

	// Stating a bound again clamps what is held, rather than leaving a value outside the range the
	// box now claims -- the bug a bare assignment to the property would leave behind.
	Spin->SetMaxValue(10.0f);
	TestEqual(TEXT("stating a top again pulls the held value into it"), Spin->GetValue(), 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxOpenRangeIsNotScrubbableTest,
	"DreamGUI.SpinBox.Parity.AnOpenRangeHasNoTravelToScrubAcross",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxOpenRangeIsNotScrubbableTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Spin(Author<UDreamSpinBox>());
	Spin->Initialize();

	TestTrue(TEXT("a bounded box can be scrubbed"), Spin->HasFiniteSliderRange());

	// The whole reason the guard exists: the span of float's floor to float's ceiling overflows to
	// infinity, and every line of the scrub divides by it. Before the guard this was a NaN in the
	// value, which nothing downstream recovers from.
	Spin->ClearMinValue();
	TestFalse(TEXT("a box with no bottom has no travel to sweep"), Spin->HasFiniteSliderRange());

	// A stated SCRUB range rescues it: the value may still be anything, and the drag sweeps the part
	// a player actually wants. That is exactly what the slider-value pair is for.
	Spin->SetMinSliderValue(-50.0f);
	Spin->SetMaxSliderValue(50.0f);
	TestTrue(TEXT("a stated scrub range makes an unbounded value draggable again"), Spin->HasFiniteSliderRange());
	TestEqual(TEXT("and it is that range the scrub sweeps"), Spin->GetSliderMinValue(), -50.0f);

	// Clearing BOTH ends of the scrub range falls back to the value range, which is open again.
	Spin->ClearMinSliderValue();
	Spin->ClearMaxSliderValue();
	TestFalse(TEXT("clearing it falls back to the open value range"), Spin->HasFiniteSliderRange());

	// A range of zero width is the other degenerate case, and was already guarded; asserted here so
	// the two live next to each other.
	Spin->SetMinValue(5.0f);
	Spin->SetMaxValue(5.0f);
	TestFalse(TEXT("a range with no width cannot be scrubbed either"), Spin->HasFiniteSliderRange());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxRuntimeSettersActTest,
	"DreamGUI.SpinBox.Parity.WritingAKnobAtRuntimeActsOnTheNumberAlreadyShowing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxRuntimeSettersActTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Spin(Author<UDreamSpinBox>());
	Spin->Initialize();

	// A digit count nobody applied is not a format: the setter has to re-spell what is on screen,
	// not merely decide how the NEXT value will be written. Values chosen so no assertion depends on
	// which way a halfway case rounds.
	Spin->SetValue(1.0f / 3.0f);
	TestEqual(TEXT("six digits is the shipped maximum"),
		SpelledValue(Spin->ValueTextNode), FString(TEXT("0.333333")));
	Spin->SetMaxFractionalDigits(2);
	TestEqual(TEXT("a maximum digit count re-spells the number already showing"),
		SpelledValue(Spin->ValueTextNode), FString(TEXT("0.33")));

	Spin->SetValue(2.5f);
	TestEqual(TEXT("with no minimum a trailing zero is dropped"),
		SpelledValue(Spin->ValueTextNode), FString(TEXT("2.5")));
	Spin->SetMinFractionalDigits(2);
	TestEqual(TEXT("and a minimum digit count re-spells it too"),
		SpelledValue(Spin->ValueTextNode), FString(TEXT("2.50")));

	// Same argument for the snap: turning it on has to bind the value being held, or the box shows a
	// number it says cannot exist.
	Spin->SetMinFractionalDigits(0);
	Spin->SetMaxFractionalDigits(6);
	Spin->SetValue(5.4f);
	Spin->SetStepSize(2.0f);
	Spin->SetAlwaysUsesDeltaSnap(true);
	TestEqual(TEXT("switching delta snap on snaps the value already held"), Spin->GetValue(), 6.0f);

	// And changing the step re-snaps against the new grid while the snap is on.
	Spin->SetDelta(5.0f);
	TestEqual(TEXT("a new step re-snaps against the new grid"), Spin->GetValue(), 5.0f);
	TestEqual(TEXT("and Delta is StepSize under UMG's name"), Spin->GetDelta(), Spin->GetStepSize());

	// The two that reach the typed-entry field rather than the number.
	if (TestNotNull(TEXT("the field's behaviour exists"), Spin->InputBehaviour.Get()))
	{
		Spin->SetKeyboardType(EVirtualKeyboardType::Web);
		TestEqual(TEXT("the keyboard type reaches the field"),
			(int32)Spin->InputBehaviour->GetKeyboardType().GetValue(), (int32)EVirtualKeyboardType::Web);
		Spin->SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction::TextChangeOnDismiss);
		TestEqual(TEXT("and so does the dismiss action"),
			(int32)Spin->InputBehaviour->GetVirtualKeyboardDismissAction(),
			(int32)EVirtualKeyboardDismissAction::TextChangeOnDismiss);
	}

	// Where the number sits, which the built-in tree used to decide once and never revisit.
	if (UDreamText* ValueVisual = Spin->ValueTextNode != nullptr ? Cast<UDreamText>(Spin->ValueTextNode->GetVisual()) : nullptr)
	{
		TestEqual(TEXT("the number starts centred"),
			(int32)ValueVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);
		Spin->SetJustification(EDreamUITextParagraphHorizontalAlign::Right);
		TestEqual(TEXT("and follows the justification"),
			(int32)ValueVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);
		Spin->ApplyStyle();
		TestEqual(TEXT("which survives a whole restyle"),
			(int32)ValueVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxMinDesiredWidthTest,
	"DreamGUI.SpinBox.Parity.AMinimumDesiredWidthIsAFloorAndNeverASize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxMinDesiredWidthTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Narrow(Author<UDreamSpinBox>(80.0f, 34.0f));
	Narrow->Initialize();
	Narrow->SetMinDesiredWidth(160.0f);
	TestEqual(TEXT("a box narrower than its minimum grows to it"), Narrow->GetWidth(), 160.0f);

	TDreamTestControl<UDreamSpinBox> Wide(Author<UDreamSpinBox>(400.0f, 34.0f));
	Wide->Initialize();
	Wide->SetMinDesiredWidth(160.0f);
	TestEqual(TEXT("a box already wider keeps the width it was placed at"), Wide->GetWidth(), 400.0f);

	// Zero is what every existing spin box says, so it has to mean "no opinion" rather than "zero".
	TDreamTestControl<UDreamSpinBox> Silent(Author<UDreamSpinBox>(60.0f, 34.0f));
	Silent->Initialize();
	TestEqual(TEXT("a box with no minimum is left alone"), Silent->GetWidth(), 60.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxEnableSliderEndsAScrubTest,
	"DreamGUI.SpinBox.Parity.SwitchingTheScrubOffDoesNotLeaveAGestureRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxEnableSliderEndsAScrubTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Spin(Author<UDreamSpinBox>());
	Spin->Initialize();
	TestTrue(TEXT("the scrub ships enabled"), Spin->GetEnableSlider());

	// Switching it off with nothing in flight is simply a flag write: no gesture to end, no value to
	// report, and in particular no end-of-scrub for a scrub that never began.
	Spin->SetEnableSlider(false);
	TestFalse(TEXT("the scrub is off"), Spin->GetEnableSlider());
	TestFalse(TEXT("and no scrub is in progress"), Spin->IsSliderMoving());

	Spin->SetEnableSlider(true);
	TestTrue(TEXT("and it can be switched back on"), Spin->GetEnableSlider());
	TestFalse(TEXT("still with nothing in flight"), Spin->IsSliderMoving());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxDefaultsAreTheOldBehaviourTest,
	"DreamGUI.SpinBox.Parity.TheNewOverrideBitsDefaultToTheRangeEveryExistingBoxHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxDefaultsAreTheOldBehaviourTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxParityTestLocal;

	// UMG's own override bits default to UNSET, which would make every spin box in this project
	// unbounded overnight. Ours default to set, and this is the test that says so out loud.
	TDreamTestControl<UDreamSpinBox> Spin(Author<UDreamSpinBox>());
	Spin->Initialize();
	TestTrue(TEXT("the bottom is stated by default"), Spin->bOverride_MinValue);
	TestTrue(TEXT("and so is the top"), Spin->bOverride_MaxValue);
	TestFalse(TEXT("while the scrub range is not, as it never was"), Spin->bOverride_MinSliderValue);
	TestFalse(TEXT("at either end"), Spin->bOverride_MaxSliderValue);

	// Which makes the effective scrub range the value range, the answer the control has always given.
	TestEqual(TEXT("so the scrub sweeps the value range"), Spin->GetSliderMinValue(), Spin->GetMinValue());
	TestEqual(TEXT("at both ends"), Spin->GetSliderMaxValue(), Spin->GetMaxValue());

	// The snap origin is the stated bottom, which is the rule a range of 3..10 stepped by 2 depends
	// on: it must offer 3 and 10, not 4, 6, 8, 10.
	Spin->SetMinValue(3.0f);
	Spin->SetMaxValue(10.0f);
	Spin->SetStepSize(2.0f);
	Spin->SetAlwaysUsesDeltaSnap(true);
	Spin->SetValue(3.4f);
	TestEqual(TEXT("a snap measures from the stated bottom"), Spin->GetValue(), 3.0f);

	// And with no bottom to measure from, zero is the origin -- float's floor as an origin would put
	// the grid nowhere near the value.
	Spin->ClearMinValue();
	Spin->SetValue(3.4f);
	TestEqual(TEXT("and from zero when there is no bottom"), Spin->GetValue(), 4.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
