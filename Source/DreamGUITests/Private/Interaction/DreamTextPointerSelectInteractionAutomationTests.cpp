// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamEditableText.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/UnrealType.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * A POINTER AND A FINGER IN A TEXT FIELD, AS UMG'S EDITABLE TEXT TAKES THEM.
 *
 * FSlateEditableTextLayout::HandleMouseButtonDown (SlateEditableTextLayout.cpp) clears the selection and moves the cursor to
 * the press, and starts a drag selection; HandleMouseMove, while the press holds the capture, selects from there to the
 * pointer, across lines too. A finger reaches the same code: FSlateApplication hands an unanswered touch on to the widget
 * as a mouse press, so a tap puts the cursor where it lands. A disabled field is not hit and takes no focus. The edit menu,
 * which Slate opens on a secondary click (HandleMouseButtonUp), is opened on a field held down by a finger -- a finger has
 * no second button -- once the field's hold time is up and not before.
 *
 * Where a selection ends up is read off what the next keystroke replaces, which is what a player sees of it.
 */
namespace DreamTextPointerSelectInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * The viewport pixel of the caret that stands before character InCaretIndex, read off the text's own caret table --
	 * the table a press is matched against -- or unset when there is no such caret.
	 */
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

	/** Clicked into and typed into, then left alone past the double-click time, so the next press is a press of its own. */
	bool TypeAndWait(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName, const FString& InText)
	{
		const bool bTyped = InRig.Driver()->Find(FDreamBy::Name(InName))->Type(InText);
		const float DoubleClickTime = InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetDoubleClickTime() : 0.0f;
		return InTest.TestTrue(TEXT("Clicking into the field and typing completes"), bTyped)
			&& InTest.TestTrue(TEXT("Waiting out the double-click time completes"),
				InRig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
	}

	/** How far past the press the first move has to go to be a drag: past the raycaster's threshold, with a margin. */
	double PastDragThreshold(const FDreamDriverRig& InRig)
	{
		return InRig.Raycaster() != nullptr ? FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0 : 8.0;
	}

	/** The field's hold threshold, read the way the details panel reads it: a protected property with no getter. */
	TOptional<float> ReadContextMenuLongPressTime(const UUITextInput* InBehaviour)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(UUITextInput::StaticClass(), TEXT("ContextMenuLongPressTime"));
		if (Property == nullptr || InBehaviour == nullptr)
		{
			return TOptional<float>();
		}
		return Property->GetPropertyValue_InContainer(InBehaviour);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDragSelectsTest,
	"DreamGUI.TextInput.DraggingThePointerAcrossAWordSelectsThatWord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputDragSelectsTest, "DreamGUI.TextInput.DraggingThePointerAcrossAWordSelectsThatWord", "[Pointer][Text][Animated]")

/*
 * "alpha bravo charlie": pressed on the caret before "bravo", dragged past the drag threshold and on to the caret after
 * it, and let go. HandleMouseButtonDown put the cursor at the press, HandleMouseMove selected from there to where the
 * pointer went: "bravo" is selected, which the X typed next replaces.
 */
bool FDreamTextInputDragSelectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? Rig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a field came up"), Field))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FString Value(TEXT("alpha bravo charlie"));
	if (!TypeAndWait(*this, Rig, TEXT("Notes"), Value))
	{
		return false;
	}
	UDreamText* Shown = ShownTextOf(Field);
	const TOptional<FVector2D> BeforeWord = Shown != nullptr ? CaretPixel(*Shown, 6) : TOptional<FVector2D>();
	const TOptional<FVector2D> AfterWord = Shown != nullptr ? CaretPixel(*Shown, 11) : TOptional<FVector2D>();
	if (!TestEqual(TEXT("The field holds what was typed"), Field->GetText(), Value)
		|| !TestTrue(TEXT("The carets either side of \"bravo\" are on screen"), BeforeWord.IsSet() && AfterWord.IsSet())
		|| !TestTrue(TEXT("...the one after it to the right of the one before it"), AfterWord->X > BeforeWord->X + 1.0))
	{
		return false;
	}

	TestTrue(TEXT("Pressing before the word, dragging to after it and letting go completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(BeforeWord.GetValue())
			.Press()
			.MoveBy(FVector2D(PastDragThreshold(Rig), 0.0))
			.MoveToPixel(AfterWord.GetValue())
			.WaitFrames(1)
			.Release()
			.Perform());
	TestTrue(TEXT("The drag selected something"), Field->IsAnyTextSelected());
	TestTrue(TEXT("...and the field is still being edited"), IsEditing(Field));
	TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
	TestEqual(TEXT("What the drag selected was the word it crossed"), Field->GetText(), FString(TEXT("alpha X charlie")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineDragSelectsAcrossLinesTest,
	"DreamGUI.MultiLineEditableText.DraggingFromOneLineIntoTheNextSelectsEverythingBetweenThePressAndTheRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMultiLineDragSelectsAcrossLinesTest, "DreamGUI.MultiLineEditableText.DraggingFromOneLineIntoTheNextSelectsEverythingBetweenThePressAndTheRelease", "[Pointer][Text][Animated]")

/*
 * Two lines, "first line" and "second line": pressed on the caret before "line" on the first, dragged down to the caret
 * before "line" on the second. Everything from the press to the release is selected -- the end of the first line, the
 * line break and the start of the second -- and the X typed next replaces all of it.
 */
bool FDreamMultiLineDragSelectsAcrossLinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamMultiLineEditableText* Field = Rig.IsUsable()
		? Rig.MakeControl<UDreamMultiLineEditableText>(TEXT("Notes"), nullptr, FVector2D(360.0, 160.0))
		: nullptr;
	if (!TestNotNull(TEXT("The rig and a multi-line field came up"), Field))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	TestTrue(TEXT("Typing the first line completes"), FieldElement->Type(TEXT("first line")));
	TestTrue(TEXT("Enter completes"), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
	TestTrue(TEXT("Typing the second line completes"), FieldElement->Type(TEXT("second line")));
	const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
	TestTrue(TEXT("Waiting out the double-click time completes"), Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
	const FString Value(TEXT("first line\nsecond line"));
	UDreamText* Shown = ShownTextOf(Field);
	if (!TestEqual(TEXT("The field holds the two lines"), Field->GetText(), Value) || !TestNotNull(TEXT("The field has a text part"), Shown))
	{
		return false;
	}
	// "first |line" and "second |line", as carets: a caret index counts a line's end caret too, so ask the text.
	const TOptional<FVector2D> OnFirstLine = CaretPixel(*Shown, Shown->GetCaretIndexByCharIndex(6));
	const TOptional<FVector2D> OnSecondLine = CaretPixel(*Shown, Shown->GetCaretIndexByCharIndex(18));
	if (!TestTrue(TEXT("Both carets are on screen"), OnFirstLine.IsSet() && OnSecondLine.IsSet())
		|| !TestTrue(TEXT("...the second a line below the first"), OnSecondLine->Y > OnFirstLine->Y + 1.0))
	{
		return false;
	}

	TestTrue(TEXT("Pressing on the first line, dragging down to the second and letting go completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(OnFirstLine.GetValue())
			.Press()
			.MoveBy(FVector2D(0.0, PastDragThreshold(Rig)))
			.MoveToPixel(OnSecondLine.GetValue())
			.WaitFrames(1)
			.Release()
			.Perform());
	TestTrue(TEXT("The drag selected something"), Field->IsAnyTextSelected());
	TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
	TestEqual(TEXT("What the drag selected ran from the press across the line break to the release"), Field->GetText(), FString(TEXT("first Xline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputFingerTapPutsCaretTest,
	"DreamGUI.TextInput.AFingerTapInsideTheTextPutsTheCaretBetweenTheCharactersUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputFingerTapPutsCaretTest, "DreamGUI.TextInput.AFingerTapInsideTheTextPutsTheCaretBetweenTheCharactersUnderIt", "[Touch][Text][Animated]")

/*
 * "alpha bravo" being edited, all of it selected with Ctrl+A, then a finger tapped on the caret before "bravo". The touch
 * reaches HandleMouseButtonDown as a press: the selection is cleared and the cursor goes where the finger landed, so the
 * field is still being edited, nothing is selected, and the X typed next goes in between "alpha " and "bravo".
 */
bool FDreamTextInputFingerTapPutsCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? Rig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a field came up"), Field))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!TypeAndWait(*this, Rig, TEXT("Notes"), TEXT("alpha bravo")))
	{
		return false;
	}
	UDreamText* Shown = ShownTextOf(Field);
	const TOptional<FVector2D> BeforeSecondWord = Shown != nullptr ? CaretPixel(*Shown, 6) : TOptional<FVector2D>();
	if (!TestTrue(TEXT("The caret before \"bravo\" is on screen"), BeforeSecondWord.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("Ctrl+A completes"), Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Perform());
	if (!TestTrue(TEXT("...and selects everything"), Field->IsAnyTextSelected()))
	{
		return false;
	}

	TestTrue(TEXT("A finger tapped on the caret before \"bravo\" completes"),
		Rig.Driver()->Sequence().TouchDown(0, BeforeSecondWord.GetValue()).TouchUp(0).Perform());
	TestTrue(TEXT("The field is still being edited"), IsEditing(Field));
	TestFalse(TEXT("The tap left nothing selected"), Field->IsAnyTextSelected());
	TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
	TestEqual(TEXT("The X went in where the finger landed"), Field->GetText(), FString(TEXT("alpha Xbravo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputFingerHoldMenuTest,
	"DreamGUI.TextInput.AFingerHeldOnAFieldBeingEditedOpensItsEditMenuAtTheHoldTimeAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputFingerHoldMenuTest, "DreamGUI.TextInput.AFingerHeldOnAFieldBeingEditedOpensItsEditMenuAtTheHoldTimeAndNotBefore", "[Touch][Text][Animated]")

/*
 * Slate opens a text field's edit menu on the secondary click's release (HandleMouseButtonUp); a finger has no secondary
 * button, and a held finger is its stand-in, timed by the field (ContextMenuLongPressTime) as the mouse's held press is.
 * Halfway through the hold there is no menu; past the hold time, with the finger still down, there is.
 */
bool FDreamTextInputFingerHoldMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? Rig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0)) : nullptr;
	UUITextInput* Behaviour = Field != nullptr ? Field->InputBehaviour.Get() : nullptr;
	if (!TestNotNull(TEXT("The rig and a field came up"), Behaviour))
	{
		return false;
	}
	Rig.PumpFrames(1);
	// Typed text, because the menu offers only what can act and opens nothing when nothing can: with text in the field,
	// Select All always can.
	if (!TypeAndWait(*this, Rig, TEXT("Notes"), TEXT("abc")))
	{
		return false;
	}
	const TOptional<float> HoldTime = ReadContextMenuLongPressTime(Behaviour);
	const TOptional<FVector2D> FieldCentre = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")))->GetCentrePixel();
	if (!TestTrue(TEXT("The field's hold time can be read"), HoldTime.IsSet())
		|| !TestTrue(TEXT("...and is longer than a few frames"), HoldTime.GetValue() > 4.0f * Rig.Context().FrameSeconds)
		|| !TestTrue(TEXT("The field projects to a pixel"), FieldCentre.IsSet()))
	{
		return false;
	}
	TestFalse(TEXT("No menu is open to begin with"), Behaviour->IsContextMenuOpen());

	TestTrue(TEXT("A finger held on the field past its hold time completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, FieldCentre.GetValue())
			.WaitSeconds(HoldTime.GetValue() * 0.5f)
			.Then([this, Behaviour](FDreamDriverContext&)
			{
				TestFalse(TEXT("Halfway through the hold there is no menu yet"), Behaviour->IsContextMenuOpen());
			})
			.WaitSeconds(HoldTime.GetValue() * 0.5f + 3.0f * Rig.Context().FrameSeconds)
			.Then([this, Behaviour](FDreamDriverContext&)
			{
				TestTrue(TEXT("Past the hold time, with the finger still down, the edit menu is open"), Behaviour->IsContextMenuOpen());
			})
			.TouchUp(0)
			.Perform());
	TestTrue(TEXT("The field is still being edited"), IsEditing(Field));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDisabledTakesNothingTest,
	"DreamGUI.TextInput.ADisabledFieldIsNotEditedByAClickAndTakesNoKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputDisabledTakesNothingTest, "DreamGUI.TextInput.ADisabledFieldIsNotEditedByAClickAndTakesNoKeys", "[Pointer][Text][Disabled]")

/*
 * UWidget::SetIsEnabled(false) disables the field's Slate widget: Slate's hit test cuts the path at the first disabled
 * widget (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp), and a disabled widget takes no keyboard
 * focus, so a click on it starts no edit and the keys pressed after it type nothing.
 * Enabled again, the same click edits it.
 */
bool FDreamTextInputDisabledTakesNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? Rig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0)) : nullptr;
	if (!TestNotNull(TEXT("The rig and a field came up"), Field))
	{
		return false;
	}
	Field->SetText(TEXT("kept"));
	Field->SetIsEnabled(false);
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	TestTrue(TEXT("Clicking the disabled field completes"), FieldElement->Click());
	TestFalse(TEXT("The click started no edit"), IsEditing(Field));
	const UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	const UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	TestFalse(TEXT("...and did not give the field the focus"), Focused != nullptr && (Focused == Field || Focused->IsChildOf(Field)));
	TestTrue(TEXT("Pressing A and Backspace completes"),
		Rig.Driver()->Sequence().Key(EKeys::A).Key(EKeys::BackSpace).Perform());
	TestEqual(TEXT("The keys typed nothing into it and took nothing out"), Field->GetText(), FString(TEXT("kept")));

	Field->SetIsEnabled(true);
	Rig.PumpFrames(1);
	const float DoubleClickTime = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetDoubleClickTime() : 0.0f;
	TestTrue(TEXT("Clicking it again once it is enabled completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Click(FDreamBy::Widget(Field)).Perform());
	TestTrue(TEXT("Enabled, the same click edits it"), IsEditing(Field));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEditableTextTabIntoTest,
	"DreamGUI.EditableText.TabFromTheButtonBeforeItStartsEditingTheFieldAndTheNextCharactersGoIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamEditableTextTabIntoTest, "DreamGUI.EditableText.TabFromTheButtonBeforeItStartsEditingTheFieldAndTheNextCharactersGoIn", "[Nav][Text][Animated]")

/*
 * UMG's EditableText is a keyboard focus stop: Tab from the button before it gives it the keyboard focus, which is what
 * editing an SEditableText is (SEditableText::OnFocusReceived starts the edit), and the characters typed next go into it.
 */
bool FDreamEditableTextTabIntoTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPointerSelectInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Before = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Before"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 100.0)) : nullptr;
	UDreamEditableText* Field = Rig.IsUsable() ? Rig.MakeControl<UDreamEditableText>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0), FVector2D(0.0, 0.0)) : nullptr;
	if (!TestTrue(TEXT("The rig, a button and an editable text came up"), Before != nullptr && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("The first Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestFalse(TEXT("The first Tab lands on the button, not the field"), IsEditing(Field));
	TestTrue(TEXT("The second Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	if (!TestTrue(TEXT("The second Tab started editing the field"), IsEditing(Field)))
	{
		return false;
	}
	TestTrue(TEXT("Typing completes"), Rig.Driver()->Sequence().Type(TEXT("ab")).Perform());
	TestEqual(TEXT("The characters went into the field"), Field->GetText(), FString(TEXT("ab")));
	return true;
}

#endif
