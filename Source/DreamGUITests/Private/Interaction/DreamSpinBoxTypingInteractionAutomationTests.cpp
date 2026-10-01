// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A SPIN BOX, TYPED INTO. (Dragging to scrub it is the other half of the control and is tested
 * with the other drags.)
 *
 * SSpinBox is click-to-type, drag-to-scrub: a click on the field turns it into an editable text,
 * and Enter commits what was typed as the new value, clamped into the range, reported through
 * OnValueCommitted. This control does the same with a UUITextInput on its field part, so the
 * typing goes through exactly the roads a text field's does and the value comes out of the control.
 */
namespace DreamSpinBoxTypingInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamSpinBox* MakeObservedSpinBox(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Count"), nullptr, FVector2D(260.0, 40.0));
		if (SpinBox != nullptr && InListener != nullptr)
		{
			SpinBox->OnValueChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleValueChanged);
			SpinBox->OnValueCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleValueCommitted);
		}
		return SpinBox;
	}

	/** The field part -- the middle of [-] field [+] -- as something to click and type into. */
	FDreamElementRef FieldOf(FDreamDriverRig& InRig, UDreamSpinBox* InSpinBox)
	{
		return InRig.Driver()->Find(FDreamBy::Widget(InSpinBox->FieldNode));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxTypeAndEnterTest,
	"DreamGUI.SpinBox.TypingANumberIntoTheFieldAndPressingEnterCommitsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxTypeAndEnterTest, "DreamGUI.SpinBox.TypingANumberIntoTheFieldAndPressingEnterCommitsIt", "[Pointer][Text][Animated]")

bool FDreamSpinBoxTypeAndEnterTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTypingInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = MakeObservedSpinBox(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the spin box came up"), Rig.IsUsable() && SpinBox != nullptr && SpinBox->FieldNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef Field = FieldOf(Rig, SpinBox);
	Field->Type(TEXT("42"));
	TestEqual(TEXT("Typing is not committing"), Listener->ValueCommittedCount, 0);
	Field->Type(EKeys::Enter);

	TestEqual(TEXT("Enter made the typed number the value"), SpinBox->GetValue(), 42.0f, 0.001f);
	TestEqual(TEXT("And committed it once"), Listener->ValueCommittedCount, 1);
	TestEqual(TEXT("Reporting the committed value"), Listener->LastCommittedValue, 42.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxRefusesLettersTest,
	"DreamGUI.SpinBox.ALetterTypedIntoTheFieldIsRefusedAndTheNumberAroundItStillCommits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxRefusesLettersTest, "DreamGUI.SpinBox.ALetterTypedIntoTheFieldIsRefusedAndTheNumberAroundItStillCommits", "[Pointer][Text][Animated]")

bool FDreamSpinBoxRefusesLettersTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTypingInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = MakeObservedSpinBox(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the spin box came up"), Rig.IsUsable() && SpinBox != nullptr && SpinBox->FieldNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef Field = FieldOf(Rig, SpinBox);
	Field->Type(TEXT("4a2"));

	// SSpinBox has two ways to keep a letter out of a number -- refuse it as it is typed, or fail to
	// parse it on commit and keep the old value. This control's field is a DecimalNumber input, so
	// it takes the first: the letter never reaches the text, and the digits either side of it are
	// the number the player meant.
	TestFalse(TEXT("The letter never reached the field"),
		SpinBox->InputBehaviour != nullptr && SpinBox->InputBehaviour->GetText().Contains(TEXT("a")));
	Field->Type(EKeys::Enter);
	TestEqual(TEXT("The digits either side of it committed as the value"), SpinBox->GetValue(), 42.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxClampsTypedValueTest,
	"DreamGUI.SpinBox.ATypedValueAboveTheMaximumIsClampedWhenCommitted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxClampsTypedValueTest, "DreamGUI.SpinBox.ATypedValueAboveTheMaximumIsClampedWhenCommitted", "[Pointer][Text][Animated]")

bool FDreamSpinBoxClampsTypedValueTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTypingInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = MakeObservedSpinBox(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the spin box came up"), Rig.IsUsable() && SpinBox != nullptr && SpinBox->FieldNode != nullptr))
	{
		return false;
	}
	SpinBox->SetMaxValue(100.0f);
	Rig.PumpFrames(1);

	FDreamElementRef Field = FieldOf(Rig, SpinBox);
	Field->Type(TEXT("500"));
	Field->Type(EKeys::Enter);

	// SSpinBox clamps a committed value into [MinValue, MaxValue] whichever road it arrived by.
	TestEqual(TEXT("A typed value past the maximum becomes the maximum"), SpinBox->GetValue(), 100.0f, 0.001f);
	TestEqual(TEXT("And the commit reports the clamped value, not the typed one"),
		Listener->LastCommittedValue, 100.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxEnterKeepsEditingTest,
	"DreamGUI.SpinBox.EnterKeepsTheFieldBeingEditedWithTheValueSelectedAndAStepLeavesNothingSelected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxEnterKeepsEditingTest, "DreamGUI.SpinBox.EnterKeepsTheFieldBeingEditedWithTheValueSelectedAndAStepLeavesNothingSelected", "[Pointer][Text][Animated]")

/*
 * A spin box's commit knobs default to "keep editing, select the value" (ClearKeyboardFocusOnCommit off,
 * SelectAllTextOnCommit on), and neither reached the field. Enter is the field's to finish, by its own
 * settings, so a typed value always ended the edit. The control's own select-all ran on every commit
 * instead, edit or no edit: a step face clicked, a scrub let go of, a field clicked away from each left a
 * highlight on a field nobody was editing. Here a value is typed and entered -- the edit has to stay open
 * with the value selected -- and typed over in the same edit; then the [+] face is clicked, which ends the
 * edit, commits what was typed after the Enter, steps from it, and must leave nothing selected behind it.
 */
bool FDreamSpinBoxEnterKeepsEditingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxTypingInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = MakeObservedSpinBox(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the spin box came up"), Rig.IsUsable() && SpinBox != nullptr && SpinBox->FieldNode != nullptr
		&& SpinBox->IncrementNode != nullptr && SpinBox->InputBehaviour != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef Field = FieldOf(Rig, SpinBox);
	Field->Type(TEXT("42"));
	Field->Type(EKeys::Enter);
	TestEqual(TEXT("Enter committed the typed value"), SpinBox->GetValue(), 42.0f, 0.001f);
	TestTrue(TEXT("And the field is still being edited, as the spin box's defaults say"), SpinBox->InputBehaviour->IsInputActive());
	TestTrue(TEXT("With the value selected, ready to be typed over"), SpinBox->InputBehaviour->IsAnyTextSelected());

	// Typed over in the same edit: the field now holds a value the Enter did not commit.
	Field->Type(TEXT("7"));
	TestTrue(TEXT("Clicking the [+] face completes"), Rig.Driver()->Find(FDreamBy::Widget(SpinBox->IncrementNode))->Click());
	TestEqual(TEXT("Leaving the field committed what was typed after the Enter, and the step counted up from it"),
		SpinBox->GetValue(), 8.0f, 0.001f);
	TestFalse(TEXT("Clicking the face ended the field's edit"), SpinBox->InputBehaviour->IsInputActive());
	TestFalse(TEXT("And the step's commit left nothing selected on a field nobody is editing"), SpinBox->InputBehaviour->IsAnyTextSelected());

	return true;
}

#endif
