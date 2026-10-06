// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A SPIN BOX AND THE ARROW KEYS.
 *
 * SSpinBox::OnKeyDown (Slate/Private/Widgets/Input/SSpinBox.cpp) steps the value on the keyboard's four arrows: Up and
 * Right add a step, Down and Left take one away, each press committed. The step is Delta when there is one, and its
 * default step (one, a tenth across a drag range of ten or less) times the modifier multiplier when there is not; the
 * commit clamps to the drag's range and then the hard one, and snaps to Delta. The keys are handled, so they never
 * navigate away from a focused spin box. The field being edited is another matter: its arrows move its caret.
 *
 * Every key here goes through Key(), which delivers it the way the rig's input host does -- DreamUIKeyRouting for the
 * module, the player controller for the actor hosts, Slate's key events for the Slate source -- so the spin box hears it
 * where a game would hand it over: after the field being edited, ahead of the bindings and navigation. The spin box is
 * focused the way code focuses a control, without starting an edit (UDreamUIInputServices::FocusForNavigation): the
 * state UMG's spin box is in when it holds the keyboard but is not in text mode.
 */
namespace DreamSpinBoxKeyInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D SpinBoxSize(260.0, 40.0);
	const FVector2D ButtonSize(200.0, 60.0);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	/** Every arrangement a headless rig has. */
	const FHostCase EveryHost[] = {
		{ EDreamRigInputHost::ModuleOnly, TEXT("module only") },
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
		{ EDreamRigInputHost::SlateSource, TEXT("Slate source") },
	};

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	/** "[Slate source] what", so a failure says which host it failed under. */
	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	/** A spin box over 0..InMax holding InValue, its changes and commits heard by InListener. Laid out before it returns. */
	UDreamSpinBox* MakeSpinBox(FDreamDriverRig& InRig, float InMax, float InValue, UDreamTextInteractionListener* InListener)
	{
		UDreamSpinBox* SpinBox = InRig.MakeControl<UDreamSpinBox>(TEXT("Count"), nullptr, SpinBoxSize);
		if (SpinBox == nullptr)
		{
			return nullptr;
		}
		// Range before value, so the value is clamped against the range it is meant for.
		SpinBox->SetMinValue(0.0f);
		SpinBox->SetMaxValue(InMax);
		SpinBox->SetValue(InValue);
		InRig.PumpFrames(2);
		// Bound after the setup, so only what the keys do is counted.
		if (InListener != nullptr)
		{
			SpinBox->OnValueChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleValueChanged);
			SpinBox->OnValueCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleValueCommitted);
		}
		return SpinBox;
	}

	/** The keyboard on the spin box's field, with no edit begun: what UMG's SetKeyboardFocus on a spin box gives. */
	bool FocusWithoutEditing(FDreamDriverRig& InRig, UDreamSpinBox* InSpinBox)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr && InSpinBox != nullptr && InSpinBox->FieldNode != nullptr
			&& Services->FocusForNavigation(InSpinBox->FieldNode, 0)
			&& InSpinBox->InputBehaviour != nullptr && !InSpinBox->InputBehaviour->IsInputActive();
	}

	/** Player 0's focus now. */
	UDreamWidget* FocusOf(FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** One key, pressed and let go of through the rig's input host. */
	bool PressKey(FDreamDriverRig& InRig, const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None)
	{
		return InRig.Driver()->Sequence().Key(InKey, InModifiers).Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysStepTest,
	"DreamGUI.SpinBox.UpOrRightStepsAFocusedSpinBoxUpAndDownOrLeftStepsItDownWithoutMovingTheFocusUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysStepTest, "DreamGUI.SpinBox.UpOrRightStepsAFocusedSpinBoxUpAndDownOrLeftStepsItDownWithoutMovingTheFocusUnderEveryHost", "[Nav][Animated]")

/*
 * Four buttons around the spin box, one on each side, so an arrow that went on to navigation would have somewhere to take
 * the focus. Each arrow steps the value by the step and commits once, and the focus stays where it was: the keys are the
 * spin box's. The pad's D-pad is no arrow -- SSpinBox leaves it unhandled -- and still takes the focus to the button below.
 */
bool FDreamSpinBoxArrowKeysStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(Under(Case, TEXT("The rig came up")), Rig.IsUsable()))
		{
			continue;
		}
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, ButtonSize, FVector2D(0.0, 160.0));
		UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, ButtonSize, FVector2D(0.0, -160.0));
		UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, ButtonSize, FVector2D(-420.0, 0.0));
		UDreamButton* Right = Rig.MakeControl<UDreamButton>(TEXT("Right"), nullptr, ButtonSize, FVector2D(420.0, 0.0));
		UDreamSpinBox* SpinBox = MakeSpinBox(Rig, 100.0f, 50.0f, Listener.Get());
		if (!TestTrue(Under(Case, TEXT("A spin box with a button on each side of it")),
			SpinBox != nullptr && Above != nullptr && Below != nullptr && Left != nullptr && Right != nullptr))
		{
			continue;
		}
		if (!TestTrue(Under(Case, TEXT("The spin box takes the focus without starting an edit")), FocusWithoutEditing(Rig, SpinBox)))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("Up completes")), PressKey(Rig, EKeys::Up));
		TestEqual(Under(Case, TEXT("Up added one step")), SpinBox->GetValue(), 51.0f, 0.001f);
		TestEqual(Under(Case, TEXT("...committed once, as SSpinBox commits an arrow key")), Listener->ValueCommittedCount, 1);
		TestTrue(Under(Case, TEXT("Right completes")), PressKey(Rig, EKeys::Right));
		TestEqual(Under(Case, TEXT("Right added another")), SpinBox->GetValue(), 52.0f, 0.001f);
		TestTrue(Under(Case, TEXT("Down completes")), PressKey(Rig, EKeys::Down));
		TestEqual(Under(Case, TEXT("Down took one away")), SpinBox->GetValue(), 51.0f, 0.001f);
		TestTrue(Under(Case, TEXT("Left completes")), PressKey(Rig, EKeys::Left));
		TestEqual(Under(Case, TEXT("Left took another away")), SpinBox->GetValue(), 50.0f, 0.001f);
		TestEqual(Under(Case, TEXT("Four presses, four commits")), Listener->ValueCommittedCount, 4);
		TestEqual(Under(Case, TEXT("...and four changes")), Listener->ValueChangedCount, 4);
		TestEqual(Under(Case, TEXT("None of the four took the focus anywhere: it is still on the field")),
			FocusOf(Rig), SpinBox->FieldNode.Get());
		TestFalse(Under(Case, TEXT("...which is still not being edited")), SpinBox->InputBehaviour->IsInputActive());

		TestTrue(Under(Case, TEXT("The D-pad's down completes")),
			Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
		TestTrue(Under(Case, TEXT("The D-pad navigated, to the button below the spin box")), IsPartOf(FocusOf(Rig), Below));
		TestEqual(Under(Case, TEXT("...and stepped nothing")), SpinBox->GetValue(), 50.0f, 0.001f);
		TestEqual(Under(Case, TEXT("...nor committed")), Listener->ValueCommittedCount, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysRangeTest,
	"DreamGUI.SpinBox.AnArrowKeyStepStopsAtEitherEndOfTheRangeADragSweeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysRangeTest, "DreamGUI.SpinBox.AnArrowKeyStepStopsAtEitherEndOfTheRangeADragSweeps", "[Nav][Animated]")

/*
 * SSpinBox::CommitValue clamps an arrow-key commit to the drag's range (MinSliderValue..MaxSliderValue, which default to
 * the value's), then to the value's, and commits even when the press could not move it. A value half a step from an end
 * stops on the end, the next press stays there; and with the drag's range narrower than the value's, the arrows stop at
 * the drag's top though a typed value could go higher.
 */
bool FDreamSpinBoxArrowKeysRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = Rig.IsUsable() ? MakeSpinBox(Rig, 10.0f, 9.5f, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig and a spin box over 0..10 came up"), SpinBox != nullptr)
		|| !TestTrue(TEXT("The spin box takes the focus without starting an edit"), FocusWithoutEditing(Rig, SpinBox)))
	{
		return false;
	}

	TestTrue(TEXT("Up completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("Half a step short of the top, Up stops on the top"), SpinBox->GetValue(), 10.0f, 0.001f);
	TestTrue(TEXT("Up again completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("...and stays there"), SpinBox->GetValue(), 10.0f, 0.001f);
	TestEqual(TEXT("...a press that moved nothing changing nothing"), Listener->ValueChangedCount, 1);
	TestEqual(TEXT("...but committing, as every arrow-key press commits in SSpinBox"), Listener->ValueCommittedCount, 2);

	SpinBox->SetValue(0.5f);
	TestTrue(TEXT("Down completes"), PressKey(Rig, EKeys::Down));
	TestEqual(TEXT("Half a step above the bottom, Down stops on the bottom"), SpinBox->GetValue(), 0.0f, 0.001f);
	TestTrue(TEXT("Down again completes"), PressKey(Rig, EKeys::Down));
	TestEqual(TEXT("...and stays there"), SpinBox->GetValue(), 0.0f, 0.001f);

	// A drag that sweeps 0..20 of a value that may reach 100.
	SpinBox->SetMaxValue(100.0f);
	SpinBox->SetMaxSliderValue(20.0f);
	SpinBox->SetValue(19.0f);
	TestTrue(TEXT("Up from just under the drag's top completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("...and lands on it"), SpinBox->GetValue(), 20.0f, 0.001f);
	TestTrue(TEXT("Up at the drag's top completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("...and goes no further, though the value's own range does"), SpinBox->GetValue(), 20.0f, 0.001f);
	TestEqual(TEXT("The value's range is still the wider one"), SpinBox->GetMaxValue(), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysModifierTest,
	"DreamGUI.SpinBox.WithNoStepSizeTheArrowKeysTakeTheDefaultStepThatShiftCtrlAndAltScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysModifierTest, "DreamGUI.SpinBox.WithNoStepSizeTheArrowKeysTakeTheDefaultStepThatShiftCtrlAndAltScale", "[Nav][Animated]")

/*
 * UMG's Delta defaults to zero, and with no Delta SSpinBox steps by GetDefaultStepSize -- one, or a tenth across a drag
 * range no wider than ten -- times GetInputEventMultiplier: Shift ten, Shift+Alt a hundred, Ctrl a tenth, Ctrl+Alt a
 * hundredth. A zero StepSize is that case here.
 */
bool FDreamSpinBoxArrowKeysModifierTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = Rig.IsUsable() ? MakeSpinBox(Rig, 1000.0f, 500.0f, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig and a spin box over 0..1000 came up"), SpinBox != nullptr))
	{
		return false;
	}
	SpinBox->SetStepSize(0.0f);
	if (!TestTrue(TEXT("The spin box takes the focus without starting an edit"), FocusWithoutEditing(Rig, SpinBox)))
	{
		return false;
	}

	TestTrue(TEXT("Up completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("With no step and a wide range, Up adds one"), SpinBox->GetValue(), 501.0f, 0.001f);
	TestTrue(TEXT("Shift+Up completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	TestEqual(TEXT("Shift makes it ten"), SpinBox->GetValue(), 511.0f, 0.001f);
	TestTrue(TEXT("Shift+Alt+Up completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift | EDreamDriverModifierKeys::Alt));
	TestEqual(TEXT("Shift with Alt makes it a hundred"), SpinBox->GetValue(), 611.0f, 0.001f);
	TestTrue(TEXT("Ctrl+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl makes it a tenth"), SpinBox->GetValue(), 610.9f, 0.001f);
	TestTrue(TEXT("Ctrl+Alt+Down completes"), PressKey(Rig, EKeys::Down, EDreamDriverModifierKeys::Ctrl | EDreamDriverModifierKeys::Alt));
	TestEqual(TEXT("Ctrl with Alt makes it a hundredth"), SpinBox->GetValue(), 610.89f, 0.001f);
	TestEqual(TEXT("Five presses, five commits"), Listener->ValueCommittedCount, 5);

	// A drag range of five: SSpinBox's small step.
	SpinBox->SetMaxValue(5.0f);
	SpinBox->SetValue(2.0f);
	TestTrue(TEXT("Up across a narrow range completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("Across a range of ten or less the step is a tenth"), SpinBox->GetValue(), 2.1f, 0.001f);
	TestTrue(TEXT("Shift+Up across it completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	TestEqual(TEXT("...which Shift makes one"), SpinBox->GetValue(), 3.1f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysStatedStepTest,
	"DreamGUI.SpinBox.AStatedStepIsWhatAnArrowKeyTakesWhateverIsHeldAndItLandsOnTheStepGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysStatedStepTest, "DreamGUI.SpinBox.AStatedStepIsWhatAnArrowKeyTakesWhateverIsHeldAndItLandsOnTheStepGrid", "[Nav][Animated]")

/*
 * With a Delta, SSpinBox steps by exactly Delta -- the multiplier only ever scales its default step -- and snaps the
 * arrow-key commit to the Delta grid. A value off the grid (set by code here, typed or dragged in a game) is put back on
 * it by the first press: twelve, up a step of five, is fifteen.
 */
bool FDreamSpinBoxArrowKeysStatedStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamSpinBox* SpinBox = Rig.IsUsable() ? MakeSpinBox(Rig, 100.0f, 50.0f, nullptr) : nullptr;
	if (!TestTrue(TEXT("The rig and a spin box over 0..100 came up"), SpinBox != nullptr))
	{
		return false;
	}
	SpinBox->SetStepSize(5.0f);
	SpinBox->SetValue(12.0f);
	if (!TestEqual(TEXT("A value off the step grid is held as it was set, delta snapping being off"), SpinBox->GetValue(), 12.0f, 0.001f)
		|| !TestTrue(TEXT("The spin box takes the focus without starting an edit"), FocusWithoutEditing(Rig, SpinBox)))
	{
		return false;
	}

	TestTrue(TEXT("Shift+Up completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Shift));
	TestEqual(TEXT("Up a step of five from twelve lands on the grid at fifteen, Shift making no difference"),
		SpinBox->GetValue(), 15.0f, 0.001f);
	TestTrue(TEXT("Ctrl+Up completes"), PressKey(Rig, EKeys::Up, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("...and the next press is a whole step, Ctrl making none either"), SpinBox->GetValue(), 20.0f, 0.001f);
	TestTrue(TEXT("Down completes"), PressKey(Rig, EKeys::Down));
	TestEqual(TEXT("Down is a whole step back"), SpinBox->GetValue(), 15.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysWhileTypingTest,
	"DreamGUI.SpinBox.WhileTheFieldIsBeingEditedTheArrowKeysMoveTheCaretAndLeaveTheValueAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysWhileTypingTest, "DreamGUI.SpinBox.WhileTheFieldIsBeingEditedTheArrowKeysMoveTheCaretAndLeaveTheValueAlone", "[Text][Animated]")

/*
 * In text mode SSpinBox's editable text has the keys first, and Left and Right move its caret. Here the field being
 * edited takes the arrows before anything else hears them (DreamUIKeyRouting::RouteTextKey): "12" typed, Left, "3" typed
 * makes "132", and none of the arrows -- Up and Down included -- steps the value or commits it. Enter commits what was
 * typed.
 */
bool FDreamSpinBoxArrowKeysWhileTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = Rig.IsUsable() ? MakeSpinBox(Rig, 1000.0f, 0.0f, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig and a spin box came up"), SpinBox != nullptr && SpinBox->FieldNode != nullptr && SpinBox->InputBehaviour != nullptr))
	{
		return false;
	}

	FDreamElementRef Field = Rig.Driver()->Find(FDreamBy::Widget(SpinBox->FieldNode));
	TestTrue(TEXT("Typing 12 into the field completes"), Field->Type(TEXT("12")));
	if (!TestTrue(TEXT("The field is being edited"), SpinBox->InputBehaviour->IsInputActive()))
	{
		return false;
	}
	TestTrue(TEXT("Left completes"), PressKey(Rig, EKeys::Left));
	TestEqual(TEXT("Left stepped nothing"), SpinBox->GetValue(), 0.0f, 0.001f);
	TestTrue(TEXT("Typing 3 completes"), Field->Type(TEXT("3")));
	TestEqual(TEXT("Left moved the caret back one: the 3 went in between"), SpinBox->InputBehaviour->GetText(), FString(TEXT("132")));

	TestTrue(TEXT("Right completes"), PressKey(Rig, EKeys::Right));
	TestTrue(TEXT("Up completes"), PressKey(Rig, EKeys::Up));
	TestTrue(TEXT("Down completes"), PressKey(Rig, EKeys::Down));
	TestEqual(TEXT("No arrow changed the value"), Listener->ValueChangedCount, 0);
	TestEqual(TEXT("...or committed it"), Listener->ValueCommittedCount, 0);
	TestEqual(TEXT("...or the text"), SpinBox->InputBehaviour->GetText(), FString(TEXT("132")));
	TestTrue(TEXT("...and the field is still being edited"), SpinBox->InputBehaviour->IsInputActive());

	TestTrue(TEXT("Enter completes"), Field->Type(EKeys::Enter));
	TestEqual(TEXT("Enter committed what was typed"), SpinBox->GetValue(), 132.0f, 0.001f);
	TestEqual(TEXT("...once"), Listener->ValueCommittedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxArrowKeysDisabledTest,
	"DreamGUI.SpinBox.ADisabledSpinBoxTakesNoStepFromTheArrowKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSpinBoxArrowKeysDisabledTest, "DreamGUI.SpinBox.ADisabledSpinBoxTakesNoStepFromTheArrowKeys", "[Nav][Disabled]")

/*
 * Disabled while it holds the focus, the spin box hears no key at all -- the router offers keys only to a focus that is
 * interactable -- and the value stays. Enabled again, the same key steps it, which is what makes the first half mean
 * something.
 */
bool FDreamSpinBoxArrowKeysDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpinBoxKeyInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamSpinBox* SpinBox = Rig.IsUsable() ? MakeSpinBox(Rig, 100.0f, 50.0f, Listener.Get()) : nullptr;
	if (!TestTrue(TEXT("The rig and a spin box came up"), SpinBox != nullptr)
		|| !TestTrue(TEXT("The spin box takes the focus without starting an edit"), FocusWithoutEditing(Rig, SpinBox)))
	{
		return false;
	}

	SpinBox->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Up on the disabled spin box completes"), PressKey(Rig, EKeys::Up));
	TestTrue(TEXT("Left on it completes"), PressKey(Rig, EKeys::Left));
	TestEqual(TEXT("Neither stepped the value"), SpinBox->GetValue(), 50.0f, 0.001f);
	TestEqual(TEXT("...nor committed it"), Listener->ValueCommittedCount, 0);

	SpinBox->SetIsEnabled(true);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("Enabled again, it takes the focus"), FocusWithoutEditing(Rig, SpinBox)))
	{
		return false;
	}
	TestTrue(TEXT("Up completes"), PressKey(Rig, EKeys::Up));
	TestEqual(TEXT("...and steps it, so the disabled spin box's silence was the disabling's"), SpinBox->GetValue(), 51.0f, 0.001f);
	return true;
}

#endif
