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
#include "Interaction/DreamUITextInputTarget.h"
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

	/** The first highlight bar a selection draws, under the field's text; null while no selection ever drew one. */
	const UDreamWidget* FirstSelectionBar(const UDreamTextInput* InField)
	{
		if (InField == nullptr || InField->TextNode == nullptr)
		{
			return nullptr;
		}
		return InField->TextNode->FindChildByDisplayName(TEXT("Selection0"));
	}

	/** The field's IME context -- what an IME reads the selection from and writes through -- or null. */
	TSharedPtr<ITextInputMethodContext> ImeOf(const UDreamTextInput* InField)
	{
		if (InField == nullptr || InField->InputBehaviour == nullptr)
		{
			return nullptr;
		}
		return InField->InputBehaviour->GetTextInputMethodContextForTesting();
	}

	/** An emoji past the Basic Multilingual Plane: one character, two UTF-16 code units. */
	const TCHAR* const EmojiText = TEXT("\U0001F600");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTypeHelloTest,
	"DreamGUI.TextInput.ClickingInAndTypingEntersEachCharacterWhereTheCaretIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTypeHelloTest, "DreamGUI.TextInput.ClickingInAndTypingEntersEachCharacterWhereTheCaretIs", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputEnterCommitsTest, "DreamGUI.TextInput.EnterCommitsTheTextOnceAndEndsTheEdit", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputClickAwayCommitsTest, "DreamGUI.TextInput.ClickingSomewhereElseCommitsTheTextOnce", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputEscapeKeepsTest, "DreamGUI.TextInput.EscapeEndsTheEditKeepingTheTextAndCommitsItOnce", "[Pointer][Text][Nav][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputEscapeRevertsTest, "DreamGUI.TextInput.EscapeWithRevertPutsTheOriginalTextBackAndCommitsIt", "[Pointer][Text][Nav][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputCaretKeysTest, "DreamGUI.TextInput.HomeEndAndLeftMoveTheCaretToWhereTheNextCharacterLands", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputDeleteKeysTest, "DreamGUI.TextInput.BackspaceAndDeleteRemoveTheCharacterOnEitherSideOfTheCaret", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputShiftSelectTest, "DreamGUI.TextInput.ShiftLeftSelectsACharacterThatTheNextKeystrokeReplaces", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputReadOnlyTest, "DreamGUI.TextInput.AReadOnlyFieldTakesTheFocusButNoTyping", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputPasswordTest, "DreamGUI.TextInput.APasswordFieldHoldsThePlainTextAndDrawsTheMask", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTypingPastMaxLengthTest, "DreamGUI.TextInput.TypingStopsAtTheMaximumLength", "[Pointer][Text][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTeardownEndsTheEditTest, "DreamGUI.TextInput.TearingDownAFieldThatIsBeingEditedLeavesNoFieldBeingEdited", "[Pointer][Animated]")

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

	// Which field has a player's keyboard is kept on that player's input, in the field's world, and goes
	// with it. A field that left itself named anywhere would keep taking the characters a host routes,
	// into a widget that no longer exists in any hierarchy -- and every later test would find a field
	// "being edited" that is not.
	TestNull(TEXT("Once the field is gone, no field is being edited"), UUITextInput::GetActiveTextInput());

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputDoubleClickSelectsWordTest, "DreamGUI.TextInput.ADoubleClickSelectsTheWordUnderItsSecondPress", "[Pointer][Text][Animated]")

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEmojiIsOneCharacterTest,
	"DreamGUI.TextInput.AnEmojiIsTypedDeletedAndTypedOverWholeNeverHalfASurrogatePair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputEmojiIsOneCharacterTest, "DreamGUI.TextInput.AnEmojiIsTypedDeletedAndTypedOverWholeNeverHalfASurrogatePair", "[Pointer][Text][Animated]")

/*
 * A single-line field's carets were numbered one per laid-out character, and the field read those numbers
 * as offsets into its UTF-16 string. An emoji is one character and two code units, so every caret after one
 * stood a unit short of where it was drawn: a character typed after an emoji went in between its two
 * halves, Backspace after an emoji and a letter took the emoji's second half instead of the letter, and a
 * field holding only an emoji, clicked into -- which selects all of it -- kept half of it when typed over.
 * Each of the three is done here, and the text is compared code unit for code unit.
 */
bool FDreamTextInputEmojiIsOneCharacterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	const FString Emoji(EmojiText);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr)
		|| !TestEqual(TEXT("The emoji is two code units"), Emoji.Len(), 2))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The driver hands a string over one code unit at a time, as a platform's character events do.
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(Emoji);
	FieldElement->Type(TEXT("b"));
	TestEqualSensitive(TEXT("A character typed after an emoji goes in after the whole of it"), Field->GetText(), Emoji + TEXT("b"));

	FieldElement->Type(EKeys::BackSpace);
	TestEqualSensitive(TEXT("Backspace after it takes the letter, not half the emoji"), Field->GetText(), Emoji);
	FieldElement->Type(EKeys::BackSpace);
	TestEqualSensitive(TEXT("And the next Backspace takes the whole emoji"), Field->GetText(), FString());

	// Enter ends the edit, so the next keystroke clicks in again -- which selects everything.
	FieldElement->Type(EKeys::Enter);
	Field->SetText(Emoji);
	FieldElement->Type(TEXT("a"));
	TestEqualSensitive(TEXT("Typing over a field that holds only an emoji replaces the whole of it"), Field->GetText(), FString(TEXT("a")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputImeCountsInCodeUnitsTest,
	"DreamGUI.TextInput.AnImeIsToldWhereTheCaretIsAndWritesThereInCodeUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputImeCountsInCodeUnitsTest, "DreamGUI.TextInput.AnImeIsToldWhereTheCaretIsAndWritesThereInCodeUnits", "[Pointer][Text][Animated]")

/*
 * An IME counts in UTF-16 offsets, and the field's IME context answered it in caret indices: TSF asks for
 * the selection (GetSelectionRange) and writes the composed text into exactly that range (SetTextInRange),
 * so with an emoji before the caret the composed text went in between the emoji's two halves. The setters
 * stored the IME's offsets as caret indices in turn, and SetSelectionRange's Beginning case selected the
 * stretch BEFORE the range it was given. Here the calls are made in the order TSF's InsertTextAtSelection
 * makes them, after an emoji typed into the field, and then the IME selects the emoji itself.
 */
bool FDreamTextInputImeCountsInCodeUnitsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	const FString Emoji(EmojiText);
	const FString Composed(TEXT("あ"));
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(Emoji);
	const TSharedPtr<ITextInputMethodContext> Ime = ImeOf(Field);
	if (!TestTrue(TEXT("The field is being edited, with an IME context"), IsEditing(Field) && Ime.IsValid()))
	{
		return false;
	}

	uint32 Begin = 99;
	uint32 Length = 99;
	ITextInputMethodContext::ECaretPosition CaretPosition = ITextInputMethodContext::ECaretPosition::Beginning;
	Ime->GetSelectionRange(Begin, Length, CaretPosition);
	TestEqual(TEXT("The IME is told the caret stands after both of the emoji's code units"), static_cast<int32>(Begin), 2);
	TestEqual(TEXT("With nothing selected"), static_cast<int32>(Length), 0);

	Ime->BeginComposition();
	Ime->SetTextInRange(Begin, Length, Composed);
	Ime->SetSelectionRange(Begin + static_cast<uint32>(Composed.Len()), 0, ITextInputMethodContext::ECaretPosition::Ending);
	Ime->EndComposition();
	TestEqualSensitive(TEXT("The composed text went in after the whole emoji"), Field->GetText(), Emoji + Composed);

	// The caret the IME placed is where typing carries on.
	FieldElement->Type(TEXT("b"));
	TestEqualSensitive(TEXT("A character typed next follows the composed text"), Field->GetText(), Emoji + Composed + TEXT("b"));

	// Beginning: the range given, with the caret at its start -- so typing replaces the emoji and nothing else.
	Ime->SetSelectionRange(0, 2, ITextInputMethodContext::ECaretPosition::Beginning);
	Ime->GetSelectionRange(Begin, Length, CaretPosition);
	TestEqual(TEXT("The IME's selection starts where it said"), static_cast<int32>(Begin), 0);
	TestEqual(TEXT("And covers the emoji"), static_cast<int32>(Length), 2);
	TestTrue(TEXT("With the caret at its beginning"), CaretPosition == ITextInputMethodContext::ECaretPosition::Beginning);
	FieldElement->Type(TEXT("c"));
	TestEqualSensitive(TEXT("Typing replaces exactly the range the IME selected"), Field->GetText(), FString(TEXT("c")) + Composed + TEXT("b"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputImeCompositionIsOneUndoStepTest,
	"DreamGUI.TextInput.UndoTakesBackAWholeImeCompositionAsOneStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputImeCompositionIsOneUndoStepTest, "DreamGUI.TextInput.UndoTakesBackAWholeImeCompositionAsOneStep", "[Pointer][Text][Animated]")

/*
 * Text an IME wrote never entered the undo history, so Ctrl+Z after a composition skipped straight past it
 * and undid the keystroke typed before it, leaving the composed text where it was. A composition is one
 * edit however many times the IME rewrites its working text on the way -- here twice, the way a kana IME
 * turns a typed letter into a syllable -- and one undo takes the whole of it back.
 */
bool FDreamTextInputImeCompositionIsOneUndoStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("ab"));
	const TSharedPtr<ITextInputMethodContext> Ime = ImeOf(Field);
	if (!TestTrue(TEXT("The field is being edited, with an IME context"), IsEditing(Field) && Ime.IsValid()))
	{
		return false;
	}

	uint32 Begin = 0;
	uint32 Length = 0;
	ITextInputMethodContext::ECaretPosition CaretPosition = ITextInputMethodContext::ECaretPosition::Ending;
	Ime->GetSelectionRange(Begin, Length, CaretPosition);
	Ime->BeginComposition();
	Ime->SetTextInRange(Begin, Length, TEXT("k"));
	Ime->SetTextInRange(Begin, 1, TEXT("か"));
	Ime->EndComposition();
	if (!TestEqualSensitive(TEXT("The composition wrote its syllable"), Field->GetText(), FString(TEXT("abか"))))
	{
		return false;
	}

	FieldElement->TypeChord(EKeys::LeftControl, EKeys::Z);
	TestEqualSensitive(TEXT("One undo takes back the whole composition"), Field->GetText(), FString(TEXT("ab")));
	FieldElement->TypeChord(EKeys::LeftControl, EKeys::Z);
	TestEqualSensitive(TEXT("And the next undo the keystroke before it"), Field->GetText(), FString(TEXT("a")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputUnfinishedCompositionTest,
	"DreamGUI.TextInput.ACompositionLeftOpenWhenTheEditEndsDoesNotHoldBackTheNextEditsKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputUnfinishedCompositionTest, "DreamGUI.TextInput.ACompositionLeftOpenWhenTheEditEndsDoesNotHoldBackTheNextEditsKeys", "[Pointer][Text][Animated]")

/*
 * Only EndComposition cleared the field's "composing" flag, and the platform does not always make that call:
 * IMM completes a composition only once the context has stopped being the active one, and that end never
 * reaches the field. The flag outlived the edit, and the key road defers to an open composition, so the next
 * edit refused Backspace, the arrows, Enter -- every key that is not a character. Here a composition is begun
 * and written into, the edit is ended by a click somewhere else with no end of the composition ever coming,
 * and the field is clicked into again and edited with keys.
 */
bool FDreamTextInputUnfinishedCompositionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr, FVector2D(0.0, 120.0));
	UDreamWidget* Elsewhere = Rig.MakeWidget(TEXT("Elsewhere"), nullptr, FVector2D(300.0, 120.0), FVector2D(0.0, -150.0));
	if (!TestTrue(TEXT("The rig, the field and the other widget came up"), Rig.IsUsable() && Field != nullptr && Elsewhere != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("ab"));
	const TSharedPtr<ITextInputMethodContext> Ime = ImeOf(Field);
	if (!TestTrue(TEXT("The field is being edited, with an IME context"), IsEditing(Field) && Ime.IsValid()))
	{
		return false;
	}
	uint32 Begin = 0;
	uint32 Length = 0;
	ITextInputMethodContext::ECaretPosition CaretPosition = ITextInputMethodContext::ECaretPosition::Ending;
	Ime->GetSelectionRange(Begin, Length, CaretPosition);
	Ime->BeginComposition();
	Ime->SetTextInRange(Begin, Length, TEXT("x"));

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Elsewhere")))->Click();
	TestFalse(TEXT("Clicking somewhere else ended the edit"), IsEditing(Field));
	TestFalse(TEXT("And the composition with it"), Ime->IsComposing());

	// Clicked into again (which selects everything), then End and Backspace: keys, not characters.
	FieldElement->Type(EKeys::End);
	FieldElement->Type(EKeys::BackSpace);
	TestEqualSensitive(TEXT("The keys of the next edit reached the field"), Field->GetText(), FString(TEXT("ab")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEmptySelectionIsNoSelectionTest,
	"DreamGUI.TextInput.ASelectionTakenBackToWhereItBeganSelectsNothingAndBackspaceDeletesOneCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputEmptySelectionIsNoSelectionTest, "DreamGUI.TextInput.ASelectionTakenBackToWhereItBeganSelectsNothingAndBackspaceDeletesOneCharacter", "[Pointer][Text][Animated]")

/*
 * Shift+Left then Shift+Right brings the caret back onto its anchor, and nothing is selected. The field asked
 * whether there was a selection by counting highlight bars, and a selection brought back to where it began
 * still has one, zero wide: the Backspace after it went to delete the selection, deleted nothing, and still
 * announced a change; the next character typed got no undo step of its own; Ctrl+C put an empty string on
 * the clipboard. Here the Backspace has to take the character before the caret, and announce that once.
 */
bool FDreamTextInputEmptySelectionIsNoSelectionTest::RunTest(const FString& Parameters)
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
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Right);
	TestFalse(TEXT("Taken back to where it began, the selection selects nothing"), Field->IsAnyTextSelected());

	const int32 ChangesBefore = Listener->TextChangedCount;
	FieldElement->Type(EKeys::BackSpace);
	TestEqualSensitive(TEXT("Backspace removed the character before the caret"), Field->GetText(), FString(TEXT("ab")));
	TestEqual(TEXT("And announced that one change"), Listener->TextChangedCount, ChangesBefore + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMaxLengthKeepsEmojiWholeTest,
	"DreamGUI.TextInput.AMaximumLengthTakesAnEmojiWholeOrNotAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputMaxLengthKeepsEmojiWholeTest, "DreamGUI.TextInput.AMaximumLengthTakesAnEmojiWholeOrNotAtAll", "[Pointer][Text][Animated]")

/*
 * MaxLength counts code units, and an emoji is two of them. Checked a unit at a time, a limit one short of an
 * emoji let its first half in and refused the second: SetText of two letters and an emoji under a limit of
 * three kept the letters and the emoji's high surrogate, which is not a character at all. Typing it went the
 * same way, and so did lowering the limit under a text that ends in one. All three roads are taken here, and
 * each has to keep the emoji whole or leave it out.
 */
bool FDreamTextInputMaxLengthKeepsEmojiWholeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	const FString Emoji(EmojiText);
	const FString Letters(TEXT("ab"));

	Field->SetMaxLength(3);
	Field->SetText(Letters + Emoji);
	TestEqualSensitive(TEXT("Set from code, an emoji that does not fit is left out whole"), Field->GetText(), Letters);
	Rig.PumpFrames(1);

	// Clicked into (which selects everything), End to stand after the letters, then the emoji typed.
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(EKeys::End);
	FieldElement->Type(Emoji);
	TestEqualSensitive(TEXT("Typed, it is refused whole too"), Field->GetText(), Letters);

	Field->SetMaxLength(4);
	FieldElement->Type(Emoji);
	TestEqualSensitive(TEXT("With room for both halves it goes in"), Field->GetText(), Letters + Emoji);

	Field->SetMaxLength(3);
	TestEqualSensitive(TEXT("Lowering the limit under it takes it out whole"), Field->GetText(), Letters);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputLeftwardSelectionIsHighlightedTest,
	"DreamGUI.TextInput.ASelectionMadeLeftwardIsHighlighted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputLeftwardSelectionIsHighlightedTest, "DreamGUI.TextInput.ASelectionMadeLeftwardIsHighlighted", "[Pointer][Text][Animated]")

/*
 * The highlight of a selection on one line was measured from its anchor to its caret, in that order, so a
 * selection made leftward -- Shift+Left, the commonest way there is to select the end of what was just
 * typed -- measured a negative width, which the bar's widget clamps to nothing. The text was selected and
 * nothing on screen said so. Here the last character is selected with Shift+Left and the bar is looked at.
 */
bool FDreamTextInputLeftwardSelectionIsHighlightedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);
	if (!TestTrue(TEXT("Shift+Left selected something"), Field->IsAnyTextSelected()))
	{
		return false;
	}
	const UDreamWidget* Bar = FirstSelectionBar(Field);
	if (!TestNotNull(TEXT("The selection drew a highlight bar"), Bar))
	{
		return false;
	}
	TestTrue(TEXT("The bar is showing"), Bar->GetWidgetActive());
	TestTrue(TEXT("And it is as wide as the character it covers, not nothing"), Bar->GetWidth() > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCodeChangesKeepTheCaretTest,
	"DreamGUI.TextInput.TextOrMaskingChangedFromCodeMidEditLeavesTheCaretWhereTheTypingWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputCodeChangesKeepTheCaretTest, "DreamGUI.TextInput.TextOrMaskingChangedFromCodeMidEditLeavesTheCaretWhereTheTypingWas", "[Pointer][Text][Animated]")

/*
 * SetText from code while the player was typing put the caret back at the start of the text, and masking
 * the field as a password did the same without even drawing the caret there: either way the player's next
 * character went in front of everything. Slate's editable text keeps the caret where it was, pulled back to
 * the end of a shorter text. Here the player types, the code replaces the text with a shorter one, the
 * player types on; then the code masks the field, and the player types once more.
 */
bool FDreamTextInputCodeChangesKeepTheCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("abc"));
	Field->SetText(TEXT("x"));
	FieldElement->Type(TEXT("y"));
	TestEqualSensitive(TEXT("After a shorter text from code, typing carries on at its end"), Field->GetText(), FString(TEXT("xy")));

	Field->SetIsPassword(true);
	FieldElement->Type(TEXT("z"));
	TestEqualSensitive(TEXT("After the field is masked, typing carries on where it was"), Field->GetText(), FString(TEXT("xyz")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCaseOnlyChangesAreChangesTest,
	"DreamGUI.TextInput.AChangeOfCaseAloneIsAChangeOfTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputCaseOnlyChangesAreChangesTest, "DreamGUI.TextInput.AChangeOfCaseAloneIsAChangeOfTheText", "[Pointer][Text][Nav][Animated]")

/*
 * FString's == ignores case, and the field compared texts with it. SetText of the same letters in capitals
 * was taken for no change and did nothing, and Escape with RevertTextOnEscape kept an edit that had only
 * changed the case of the text, because that text "had not changed". Both are done here, and the text is
 * compared code unit for code unit.
 */
bool FDreamTextInputCaseOnlyChangesAreChangesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Field->SetText(TEXT("start"));
	Field->SetText(TEXT("START"));
	TestEqualSensitive(TEXT("The same letters in capitals, set from code, are the field's text"), Field->GetText(), FString(TEXT("START")));

	Field->SetText(TEXT("start"));
	Field->SetRevertTextOnEscape(true);
	Rig.PumpFrames(1);

	// Clicked into, which selects everything, and typed over in capitals.
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("START"));
	if (!TestEqualSensitive(TEXT("Typing over it in capitals changed it"), Field->GetText(), FString(TEXT("START"))))
	{
		return false;
	}
	FieldElement->Type(EKeys::Escape);
	TestEqualSensitive(TEXT("Escape with revert puts the original back, though only its case had changed"), Field->GetText(), FString(TEXT("start")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputActivatingAgainSelectsNothingTest,
	"DreamGUI.TextInput.ActivatingAFieldAlreadyBeingEditedLeavesNothingSelected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputActivatingAgainSelectsNothingTest, "DreamGUI.TextInput.ActivatingAFieldAlreadyBeingEditedLeavesNothingSelected", "[Pointer][Text][Animated]")

/*
 * ActivateInput on a field that is already being edited moves the caret to the end, and left the selection's
 * anchor where it was. The highlight came down, so nothing looked selected -- but the anchor and the caret are
 * the selection, and the next character typed replaced everything from the old anchor to the end. Here the
 * first character is selected, the edit is activated again from code -- what a Blueprint calling
 * ActivateInput on a focused field does -- and one more character is typed.
 */
bool FDreamTextInputActivatingAgainSelectsNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr && Field->InputBehaviour != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	FieldElement->Type(EKeys::Home);
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Right);
	if (!TestTrue(TEXT("Shift+Right selected the first character"), Field->IsAnyTextSelected()))
	{
		return false;
	}

	Field->InputBehaviour->ActivateInput();
	TestFalse(TEXT("Activating the edit again left nothing selected"), Field->IsAnyTextSelected());
	FieldElement->Type(TEXT("!"));
	TestEqualSensitive(TEXT("The next character went in at the end and replaced nothing"), Field->GetText(), FString(TEXT("hello!")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputResizeKeepsTheSelectionTest,
	"DreamGUI.TextInput.ResizingAFieldWithSomethingSelectedKeepsTheSelectionHighlighted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputResizeKeepsTheSelectionTest, "DreamGUI.TextInput.ResizingAFieldWithSomethingSelectedKeepsTheSelectionHighlighted", "[Pointer][Text][Animated]")

/*
 * A field measures itself again when its size changes, and doing so put the caret back and took the highlight
 * down with it -- while the selection stayed: the anchor and the caret still selected the text, and the next
 * keystroke replaced text nobody could see was selected. Here two characters are selected, the field is made
 * wider, and the highlight has to still be over them when the next character replaces them.
 */
bool FDreamTextInputResizeKeepsTheSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);
	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);

	Field->SetWidth(static_cast<float>(FieldSize.X) + 80.0f);
	Rig.PumpFrames(1);

	TestTrue(TEXT("The selection survived the resize"), Field->IsAnyTextSelected());
	const UDreamWidget* Bar = FirstSelectionBar(Field);
	TestTrue(TEXT("And is still highlighted"), Bar != nullptr && Bar->GetWidgetActive() && Bar->GetWidth() > 0.0f);
	FieldElement->Type(TEXT("p"));
	TestEqualSensitive(TEXT("The next character replaced the highlighted characters"), Field->GetText(), FString(TEXT("help")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDragPastTheEndTest,
	"DreamGUI.TextInput.ADragPastTheEndOfTheTextStopsTheCaretOnTheLastCaret",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputDragPastTheEndTest, "DreamGUI.TextInput.ADragPastTheEndOfTheTextStopsTheCaretOnTheLastCaret", "[Pointer][Text][Animated]")

/*
 * A drag that reaches the right end of the text set the caret one past the last caret there is. It was drawn
 * on the last one, so nothing looked wrong, but the next Shift+Left only stepped back onto the last caret --
 * a keystroke that visibly did nothing, and a selection one character short of the one the player made. Here
 * a press lands past the end of the text (the field is far wider than "hello"), the pointer drags further
 * right, and Shift+Left has to select the last character.
 */
bool FDreamTextInputDragPastTheEndTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")));
	FieldElement->Type(TEXT("hello"));
	// The drag has to start on a field being edited with nothing selected. Typing can leave the field asleep, and the
	// press that wakes one selects all of it -- the anchor at the start, which no drag moves -- so the field is woken
	// first and the selection taken back to the end.
	FieldElement->Click();
	FieldElement->Type(EKeys::End);
	TestFalse(TEXT("Woken, with the caret taken to the end, the field has nothing selected"), Field->IsAnyTextSelected());
	TestTrue(TEXT("A drag to the right from the middle of the field completes"), FieldElement->DragBy(FVector2D(60.0, 0.0)));
	TestFalse(TEXT("A drag that stayed past the end of the text selected nothing"), Field->IsAnyTextSelected());

	FieldElement->TypeChord(EKeys::LeftShift, EKeys::Left);
	FieldElement->Type(TEXT("X"));
	TestEqualSensitive(TEXT("Shift+Left after it selected the last character"), Field->GetText(), FString(TEXT("hellX")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTextVisualTakenAwayTest,
	"DreamGUI.TextInput.AFieldWhoseTextIsTakenAwayMidEditStopsEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTextVisualTakenAwayTest, "DreamGUI.TextInput.AFieldWhoseTextIsTakenAwayMidEditStopsEditing", "[Pointer][Text][Animated]")

/*
 * SetTextVisual(nullptr) while the field was being edited left the edit open with nothing to show it in:
 * the field kept the player's keyboard and refused every key, and the next re-measure -- a resize -- drew
 * the caret through the null visual. Taking the text away ends the edit, without reporting a commit nobody
 * made; and a field resized afterwards has nothing left to fall over.
 */
bool FDreamTextInputTextVisualTakenAwayTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr && Field->InputBehaviour != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc"));
	Field->InputBehaviour->SetTextVisual(nullptr);
	TestFalse(TEXT("Taking the text away ended the edit"), IsEditing(Field));
	TestEqual(TEXT("Without reporting a commit"), Listener->TextCommittedCount, 0);

	Field->SetWidth(static_cast<float>(FieldSize.X) + 40.0f);
	Rig.PumpFrames(1);
	TestEqualSensitive(TEXT("And the field kept its text through a resize"), Field->InputBehaviour->GetText(), FString(TEXT("abc")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputImeContextOutlivesItsFieldTest,
	"DreamGUI.TextInput.AnImeContextThatOutlivesItsFieldAnswersForNoText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputImeContextOutlivesItsFieldTest, "DreamGUI.TextInput.AnImeContextThatOutlivesItsFieldAnswersForNoText", "[Pointer][Text][Animated]")

/*
 * The platform can hold a field's IME context past the field -- the Windows text store keeps a strong
 * reference to its context -- and the context reached the field through a raw pointer it never let go of.
 * Disposing of it now lets go, as Slate kills its own context with its widget: whatever calls in afterwards
 * finds no field and does nothing. Here the context is kept past the rig, whose teardown destroys the field.
 */
bool FDreamTextInputImeContextOutlivesItsFieldTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputInteractionTestLocal;
	TSharedPtr<ITextInputMethodContext> Ime;
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		UDreamTextInput* Field = MakeObservedField(Rig, nullptr);
		if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Type(TEXT("abc"));
		Ime = ImeOf(Field);
		if (!TestTrue(TEXT("The field had an IME context"), Ime.IsValid()) || !TestEqual(TEXT("Which saw its text"), static_cast<int32>(Ime->GetTextLength()), 3))
		{
			return false;
		}
		// The rig goes out of scope here, taking the field with it.
	}

	TestEqual(TEXT("Once the field is gone, the context has no text to report"), static_cast<int32>(Ime->GetTextLength()), 0);
	TestTrue(TEXT("And takes no writing"), Ime->IsReadOnly());
	Ime->BeginComposition();
	Ime->SetTextInRange(0, 0, TEXT("x"));
	Ime->EndComposition();
	FString Contents(TEXT("unread"));
	Ime->GetTextInRange(0, 3, Contents);
	TestTrue(TEXT("And reads back nothing"), Contents.IsEmpty());

	return true;
}

#endif
