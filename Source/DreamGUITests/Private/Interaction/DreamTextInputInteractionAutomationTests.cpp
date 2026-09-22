// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A TEXT FIELD, TYPED INTO THE WAY A PLAYER TYPES.
 *
 * The field's own tests (Runtime/DreamTextInput*) are headless in the strict sense -- no world, no
 * edit -- because an edit needs a world with a player in it: ActivateInput binds the field's keys on
 * a player's input stack and selects the field through the player's event system. That left the one
 * thing a text field is FOR untested: click, type, press a key, look at what came out.
 *
 * Here every keystroke goes through the road a host would use -- characters through
 * UUITextInput::HandleCharacterInput, keys through UUITextInput::HandleKeyInput, Escape through
 * UDreamUINavigationStack::HandleBack, which is where a game sends Back -- and every assertion is
 * about what the CONTROL shows or says: GetText, OnTextChanged, OnTextCommitted. The expected
 * semantics are UMG's editable text box (UEditableTextBox over SEditableText, 5.8), except where
 * this library's reference page documents a different design; each such place says so.
 */
namespace DreamTextInputInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D FieldSize(320.0, 40.0);

	/** A text input on the rig, every public event it has counted by the listener. */
	UDreamTextInput* MakeObservedField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener,
		const FVector2D& InPosition = FVector2D::ZeroVector)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FieldSize, InPosition);
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
			Field->OnSubmitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleSubmitted);
		}
		return Field;
	}

	/** Whether the field is being edited: the public state a keyboard's destination is decided by. */
	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	/**
	 * The viewport pixel of the caret that stands before character InCaretIndex, read off the text's
	 * own caret table -- the table a press is matched against -- or unset when there is no such caret.
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
		// The table is in the text widget's own 2D space, which is the space WidgetLocalPointToPixel reads.
		return FDreamDriverProjection::WidgetLocalPointToPixel(InShown.GetWidget(), FVector2D(CaretPosition.X, CaretPosition.Y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTypeHelloTest,
	"DreamGUI.TextInput.ClickingInAndTypingEntersEachCharacterWhereTheCaretIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputTypeHelloTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	TestTrue(TEXT("Clicking in and typing completes"), FieldElement->Type(TEXT("hello")));
	TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("hello")));
	// SEditableText raises OnTextChanged once per edit, and each character is one edit.
	TestEqual(TEXT("Each character was announced on its own"), Listener->TextChangedCount, 5);
	TestEqual(TEXT("Typing is not committing"), Listener->TextCommittedCount, 0);

	// Where the caret was left is observable only through where the NEXT character goes: after typing
	// it stands past the last character, so one more lands at the end rather than anywhere inside.
	FieldElement->Type(TEXT("!"));
	TestEqual(TEXT("The caret followed the typing to the end"), Field->GetText(), FString(TEXT("hello!")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEnterCommitsTest,
	"DreamGUI.TextInput.EnterCommitsTheTextOnceAndEndsTheEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputEnterCommitsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	FieldElement->Type(EKeys::Enter);

	// Once. UMG with ClearKeyboardFocusOnCommit raises a second OnTextCommitted as the focus it just
	// cleared is lost; this library's reference page states the other design -- the end of an edit
	// commits only when it ended WITHOUT an Enter -- so one Enter is one commit here.
	TestEqual(TEXT("Enter committed the field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With the text that was typed"), Listener->LastCommittedText, FString(TEXT("hello")));
	TestEqual(TEXT("And the compatibility spelling of the same moment fired with it"), Listener->SubmittedCount, 1);

	// ClearKeyboardFocusOnCommit is on by default (as it is in UMG): the edit is over, so the next key
	// has nowhere to go. The driver says so; the field is left exactly as it was.
	TestFalse(TEXT("The edit ended with the commit"), IsEditing(Field));
	AddExpectedErrorPlain(TEXT("there is no text field to type into"));
	TestFalse(TEXT("A key pressed after the edit ended reaches no field"),
		Rig.Driver()->Sequence().Type(TEXT("x")).Perform());
	TestEqual(TEXT("And the field kept its committed text"), Field->GetText(), FString(TEXT("hello")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputClickAwayCommitsTest,
	"DreamGUI.TextInput.ClickingSomewhereElseCommitsTheTextOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputClickAwayCommitsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get(), FVector2D(0.0, 120.0));
	// Something else on the screen to click -- the way a player ends an edit without pressing
	// anything, which UMG reports as a commit with ETextCommit::OnUserMovedFocus.
	UDreamWidget* Elsewhere = Rig.MakeWidget(TEXT("Elsewhere"), nullptr, FVector2D(300.0, 120.0), FVector2D(0.0, -150.0));
	if (!TestTrue(TEXT("The rig, the field and the other widget came up"), Rig.IsUsable() && Field != nullptr && Elsewhere != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("hi"));
	TestEqual(TEXT("Nothing is committed while the field still has the keyboard"), Listener->TextCommittedCount, 0);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Elsewhere")))->Click();

	TestEqual(TEXT("Moving away committed the field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With what had been typed"), Listener->LastCommittedText, FString(TEXT("hi")));
	TestFalse(TEXT("And the field is no longer being edited"), IsEditing(Field));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEscapeKeepsTest,
	"DreamGUI.TextInput.EscapeEndsTheEditKeepingTheTextAndCommitsItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputEscapeKeepsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("abc"));
	FieldElement->Type(EKeys::Escape);

	// RevertTextOnEscape is off by default, here as in UMG, so nothing is thrown away. What Escape
	// DOES is this library's documented design rather than UMG's: Escape is Back, Back reaches a field
	// being edited through UDreamUINavigationStack::HandleBack -> CancelInput, and ending an edit
	// commits it (SubmitWhenDeactivate). UMG instead leaves an unhandled Escape with the field.
	TestEqual(TEXT("Escape without revert keeps what was typed"), Field->GetText(), FString(TEXT("abc")));
	TestFalse(TEXT("Escape ends the edit"), IsEditing(Field));
	TestEqual(TEXT("And the edit that ended committed once"), Listener->TextCommittedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEscapeRevertsTest,
	"DreamGUI.TextInput.EscapeWithRevertPutsTheOriginalTextBackAndCommitsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputEscapeRevertsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Field->SetText(TEXT("start"));
	Field->SetRevertTextOnEscape(true);
	Rig.PumpFrames(1);
	Listener->TextCommittedCount = 0;

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("abc"));
	if (!TestNotEqual(TEXT("Typing changed the text"), Field->GetText(), FString(TEXT("start"))))
	{
		return false;
	}
	FieldElement->Type(EKeys::Escape);

	TestEqual(TEXT("Escape with revert puts back the text the edit began with"), Field->GetText(), FString(TEXT("start")));
	// SEditableText's RestoreOriginalText reports the restored value through OnTextCommitted
	// (ETextCommit::OnCleared), so anyone storing the field's value on commit stores the right one.
	// The reference page here says only that the edit is "thrown away", which does not say the
	// commit is skipped, so UMG's answer is the expected one.
	TestEqual(TEXT("The revert is reported as a commit"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("Carrying the restored text"), Listener->LastCommittedText, FString(TEXT("start")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCaretKeysTest,
	"DreamGUI.TextInput.HomeEndAndLeftMoveTheCaretToWhereTheNextCharacterLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputCaretKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));

	FieldElement->Type(EKeys::Home);
	FieldElement->Type(TEXT("X"));
	TestEqual(TEXT("Home put the caret before the first character"), Field->GetText(), FString(TEXT("Xhello")));

	FieldElement->Type(EKeys::End);
	FieldElement->Type(TEXT("Y"));
	TestEqual(TEXT("End put it after the last"), Field->GetText(), FString(TEXT("XhelloY")));

	FieldElement->Type(EKeys::Left);
	FieldElement->Type(TEXT("Z"));
	TestEqual(TEXT("Left stepped it back over one character"), Field->GetText(), FString(TEXT("XhelloZY")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDeleteKeysTest,
	"DreamGUI.TextInput.BackspaceAndDeleteRemoveTheCharacterOnEitherSideOfTheCaret",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputDeleteKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("abcd"));
	FieldElement->Type(EKeys::Left);
	FieldElement->Type(EKeys::Left);

	// The caret stands between b and c.
	FieldElement->Type(EKeys::BackSpace);
	TestEqual(TEXT("Backspace removed the character before the caret"), Field->GetText(), FString(TEXT("acd")));
	FieldElement->Type(EKeys::Delete);
	TestEqual(TEXT("Delete removed the character after it"), Field->GetText(), FString(TEXT("ad")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputShiftSelectTest,
	"DreamGUI.TextInput.ShiftLeftSelectsACharacterThatTheNextKeystrokeReplaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputShiftSelectTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	// Shift travels as modifier state with the arrow, as it does on a real key event.
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);
	TestTrue(TEXT("Shift+Left left something selected"), Field->IsAnyTextSelected());
	FieldElement->Type(TEXT("X"));
	TestEqual(TEXT("Typing over the selection replaced it"), Field->GetText(), FString(TEXT("hellX")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputReadOnlyTest,
	"DreamGUI.TextInput.AReadOnlyFieldTakesTheFocusButNoTyping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputReadOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Field->SetText(TEXT("fixed"));
	Field->SetIsReadOnly(true);
	Rig.PumpFrames(1);
	Listener->TextChangedCount = 0;

	// UMG's IsReadOnly: the field can still be focused -- to select and copy -- but nothing typed
	// reaches the text. Typing is delivered; refusing it is the field's decision.
	TestTrue(TEXT("Typing at a read-only field is still delivered"),
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc")));
	TestEqual(TEXT("The text did not change"), Field->GetText(), FString(TEXT("fixed")));
	TestEqual(TEXT("And no change was announced"), Listener->TextChangedCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputPasswordTest,
	"DreamGUI.TextInput.APasswordFieldHoldsThePlainTextAndDrawsTheMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputPasswordTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Field->SetIsPassword(true);
	Rig.PumpFrames(1);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc"));

	// UMG's IsPassword changes what is DRAWN, never what is held: GetText is the real value.
	TestEqual(TEXT("The field holds the plain text"), Field->GetText(), FString(TEXT("abc")));
	const UDreamText* Shown = Field->TextNode != nullptr ? Cast<UDreamText>(Field->TextNode->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("The field has a text part to draw with"), Shown))
	{
		return false;
	}
	TestEqual(TEXT("And draws one mask character per character typed"),
		Shown->GetText().ToString(), FString(TEXT("***")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTypingPastMaxLengthTest,
	"DreamGUI.TextInput.TypingStopsAtTheMaximumLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputTypingPastMaxLengthTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Field->SetMaxLength(3);
	Rig.PumpFrames(1);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abcdef"));

	TestEqual(TEXT("The field holds only as many characters as it may"), Field->GetText(), FString(TEXT("abc")));
	TestEqual(TEXT("And announced only the characters it took"), Listener->TextChangedCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTeardownEndsTheEditTest,
	"DreamGUI.TextInput.TearingDownAFieldThatIsBeingEditedLeavesNoFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputTeardownEndsTheEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
		if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr && Field->InputBehaviour != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc"));
		if (!TestSamePtr(TEXT("While it is being typed into, the field is the one the keyboard is routed to"),
			UUITextInput::GetActiveTextInput(), Field->InputBehaviour.Get()))
		{
			return false;
		}
		// The rig goes out of scope here with the edit still open -- a level ending, a screen torn down
		// while the player is mid-word -- and nothing ends the edit first.
	}

	// The record of which field has the keyboard is process-wide. A field that left it behind would
	// keep taking every platform character a host routes through it, into a widget that no longer
	// exists in any hierarchy -- and every later test would find a field "being edited" that is not.
	TestNull(TEXT("Once the field is gone, no field is being edited"), UUITextInput::GetActiveTextInput());
	TestFalse(TEXT("So a platform character goes nowhere"), UUITextInput::RouteCharacterInputToActiveInput(TEXT('x')));

	return true;
}

/**
 * A double click selects the word under its SECOND press. Slate delivers the second press of a double
 * click as the double click itself, and SEditableText looks the word up at that event's position
 * (FSlateEditableTextLayout::HandleMouseButtonDoubleClick -> SelectWordAt). The two presses only have
 * to be within the pointer's drag threshold of each other, so they can be on two different words:
 * here the first is inside "alpha" and the second inside "bravo", and "bravo" is what the next
 * keystroke replaces. Taking the word from where the FIRST press left the caret would replace "alpha".
 *
 * The threshold is widened for this. At the default (5 canvas units) and the field's default font
 * size, the last spot that still names "alpha" and the first that names "bravo" are half a letter
 * and half a space apart -- about the threshold itself -- so whether such a double click can happen
 * at all would come down to the font's exact metrics, and only for presses near a letter's middle,
 * where a fraction of a pixel decides which caret a press finds. The threshold is the raycaster's
 * own public setting (touch layouts commonly set it wider); here it is twice the distance between two
 * presses that are each squarely inside their word, and the test asserts that the presses are within
 * it and were one double click before it looks at what was selected.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDoubleClickSelectsWordTest,
	"DreamGUI.TextInput.ADoubleClickSelectsTheWordUnderItsSecondPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputDoubleClickSelectsWordTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	UDreamScreenSpaceRaycaster* Raycaster = Rig.Raycaster();
	UDreamEventSystem* EventSystem = Rig.EventSystem();
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr && Field->TextNode != nullptr)
		|| !TestNotNull(TEXT("The rig has a raycaster, whose drag threshold is how far apart a double click's presses may be"), Raycaster)
		|| !TestNotNull(TEXT("The rig has an event system, whose clock says how quick a double click is"), EventSystem))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	const FString Value(TEXT("alpha bravo charlie"));
	FieldElement->Type(Value);
	UDreamText* Shown = Cast<UDreamText>(Field->TextNode->GetVisual());
	if (!TestEqual(TEXT("The field holds what was typed"), Field->GetText(), Value)
		|| !TestNotNull(TEXT("The field has a text part to draw with"), Shown)
		|| !TestEqual(TEXT("All of it is on show, so a caret index is a character index"), Shown->GetText().ToString(), Value))
	{
		return false;
	}

	// On carets rather than on letters: a press exactly on a caret finds that caret and no other, where
	// a press on a letter's middle sits halfway between two. "alph|a" is inside the first word and
	// "b|ravo" inside the second, whichever side of the caret a word is measured from.
	const TOptional<FVector2D> InFirstWord = CaretPixel(*Shown, 4);
	const TOptional<FVector2D> InSecondWord = CaretPixel(*Shown, 7);
	const TOptional<FBox2D> FieldRect = FDreamDriverProjection::WidgetToPixelRect(FieldElement->GetWidget());
	if (!TestTrue(TEXT("Both presses and the field are on screen"), InFirstWord.IsSet() && InSecondWord.IsSet() && FieldRect.IsSet())
		|| !TestTrue(TEXT("The text is laid out: the second word is to the right of the first"), InSecondWord->X > InFirstWord->X + 1.0)
		|| !TestTrue(TEXT("Both presses land on the field"), FieldRect->IsInside(InFirstWord.GetValue()) && FieldRect->IsInside(InSecondWord.GetValue())))
	{
		return false;
	}

	// Twice the distance between the presses, in the canvas units the threshold is authored in.
	const double PressDistance = FVector2D::Distance(InFirstWord.GetValue(), InSecondWord.GetValue());
	const float CanvasScale = Rig.RootCanvas() != nullptr ? Rig.RootCanvas()->GetCanvasScale() : 1.0f;
	Raycaster->SetDragThreshold(static_cast<float>(2.0 * PressDistance / FMath::Max(CanvasScale, UE_KINDA_SMALL_NUMBER)));
	if (!TestTrue(TEXT("The second press is within the drag threshold of the first"),
			FVector2D::DistSquared(InFirstWord.GetValue(), InSecondWord.GetValue()) <= static_cast<double>(Raycaster->GetScaledDragThresholdSquare()))
		|| !TestTrue(TEXT("The double-click time is longer than a pair of clicks takes"), EventSystem->GetDoubleClickTime() > 0.1f))
	{
		return false;
	}

	// Past the double-click time since the click that started the edit, so the pair is a run of its own.
	Rig.PumpFrames(FMath::CeilToInt(EventSystem->GetDoubleClickTime() / Rig.Context().FrameSeconds) + 1);

	// One sequence of six frames: the second press lands two frames after the first release.
	TestTrue(TEXT("A click inside the first word and a quick second press inside the next complete"),
		Rig.Driver()->Sequence()
			.MoveToPixel(InFirstWord.GetValue()).Press().Release()
			.MoveToPixel(InSecondWord.GetValue()).Press().Release()
			.Perform());
	const UDreamPointerEventData* Pointer = Rig.Context().GetPointerEventData(0);
	if (!TestNotNull(TEXT("The pointer has a state to read"), Pointer)
		|| !TestEqual(TEXT("The two presses were one double click"), Pointer->ClickCount, 2))
	{
		return false;
	}
	TestTrue(TEXT("The double click selected something"), Field->IsAnyTextSelected());

	// What is selected shows in what the next keystroke replaces.
	FieldElement->Type(TEXT("X"));
	TestEqual(TEXT("The word under the second press was the one selected"), Field->GetText(), FString(TEXT("alpha X charlie")));

	return true;
}

#endif
