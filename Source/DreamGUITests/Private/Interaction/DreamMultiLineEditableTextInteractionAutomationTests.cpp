// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamEditableText.h"
#include "Core/Components/DreamText.h"
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
 * A FIELD OF SEVERAL LINES, where Enter stops meaning "done".
 *
 * In a single-line field Enter commits; in a multi-line one it makes a new line (UMG's
 * MultiLineEditableText inserts a carriage return on a plain Enter). That leaves the question of
 * what commits, and this library's reference page answers it: Enter with one of
 * MultiLineSubmitFunctionKeys held. Both halves are pinned here, plus the one caret motion a
 * single-line field has no use for -- moving between lines.
 */
namespace DreamMultiLineEditableTextInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamMultiLineEditableText* MakeObservedField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamMultiLineEditableText* Field = InRig.MakeControl<UDreamMultiLineEditableText>(
			TEXT("Notes"), nullptr, FVector2D(360.0, 160.0));
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
		}
		return Field;
	}

	TArray<FString> LinesOf(const FString& InText)
	{
		TArray<FString> Lines;
		// Empty lines kept: a line the player made by pressing Enter twice is a line.
		InText.ParseIntoArray(Lines, TEXT("\n"), false);
		return Lines;
	}

	/**
	 * The caret that ends the first line the paragraph wrapped on its own, as a caret index, read off the
	 * paragraph's own caret table -- with the source offset the next line starts at. INDEX_NONE when no line
	 * wrapped. A soft wrap's end caret names no character; a hard break's names its newline.
	 */
	int32 FindFirstSoftWrapCaret(const UDreamText& InShown, int32& OutNextLineCharIndex)
	{
		const TArray<FDreamUITextLineProperty>& Lines = InShown.GetCacheTextGeometryData().GetLines();
		int32 CaretCount = 0;
		for (int32 LineIndex = 0; LineIndex + 1 < Lines.Num(); LineIndex++)
		{
			const TArray<FDreamUITextCaretProperty>& Carets = Lines[LineIndex].CaretPropertyList;
			CaretCount += Carets.Num();
			if (Carets.Num() > 0 && Carets.Last().CharIndex == -1 && Lines[LineIndex + 1].CaretPropertyList.Num() > 0)
			{
				OutNextLineCharIndex = Lines[LineIndex + 1].CaretPropertyList[0].CharIndex;
				return CaretCount - 1;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineEnterBreaksTheLineTest,
	"DreamGUI.MultiLineEditableText.EnterStartsANewLineInsteadOfCommitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMultiLineEnterBreaksTheLineTest, "DreamGUI.MultiLineEditableText.EnterStartsANewLineInsteadOfCommitting", "[Pointer][Text][Animated]")

bool FDreamMultiLineEnterBreaksTheLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamMultiLineEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamMultiLineEditableText* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	FieldElement->Type(TEXT("a"));
	FieldElement->Type(EKeys::Enter);
	FieldElement->Type(TEXT("b"));

	TestEqual(TEXT("Enter broke the line"), Field->GetText(), FString(TEXT("a\nb")));
	TestEqual(TEXT("And committed nothing"), Listener->TextCommittedCount, 0);
	TestTrue(TEXT("The field is still being edited"),
		Field->InputBehaviour != nullptr && Field->InputBehaviour->IsInputActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineSubmitChordTest,
	"DreamGUI.MultiLineEditableText.EnterWithASubmitKeyHeldCommitsInsteadOfBreakingTheLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMultiLineSubmitChordTest, "DreamGUI.MultiLineEditableText.EnterWithASubmitKeyHeldCommitsInsteadOfBreakingTheLine", "[Pointer][Text][Animated]")

bool FDreamMultiLineSubmitChordTest::RunTest(const FString& Parameters)
{
	using namespace DreamMultiLineEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamMultiLineEditableText* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	// The list ships empty, which is why the plain-Enter test above breaks the line; Ctrl is the
	// chord a multi-line editor conventionally submits on.
	Field->SetMultiLineSubmitFunctionKeys({ EKeys::LeftControl });
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	FieldElement->Type(TEXT("ab"));
	FieldElement->TypeChord(EKeys::LeftControl, EKeys::Enter);

	TestEqual(TEXT("Ctrl+Enter committed the field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With the text as typed"), Listener->LastCommittedText, FString(TEXT("ab")));
	TestEqual(TEXT("And broke no line"), Field->GetText(), FString(TEXT("ab")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineUpDownTest,
	"DreamGUI.MultiLineEditableText.UpAndDownMoveTheCaretBetweenLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMultiLineUpDownTest, "DreamGUI.MultiLineEditableText.UpAndDownMoveTheCaretBetweenLines", "[Pointer][Text][Animated]")

bool FDreamMultiLineUpDownTest::RunTest(const FString& Parameters)
{
	using namespace DreamMultiLineEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamMultiLineEditableText* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	FieldElement->Type(TEXT("ab"));
	FieldElement->Type(EKeys::Enter);
	FieldElement->Type(TEXT("cd"));

	// Which COLUMN the caret lands in depends on glyph widths; which LINE does not. So each check
	// asks only which line the next character went into.
	FieldElement->Type(EKeys::Up);
	FieldElement->Type(TEXT("X"));
	TArray<FString> Lines = LinesOf(Field->GetText());
	if (!TestEqual(TEXT("Still two lines"), Lines.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Up took the caret to the first line"), Lines[0].Contains(TEXT("X")));
	TestEqual(TEXT("Leaving the second as it was"), Lines[1], FString(TEXT("cd")));

	FieldElement->Type(EKeys::Down);
	FieldElement->Type(TEXT("Y"));
	Lines = LinesOf(Field->GetText());
	if (!TestEqual(TEXT("Still two lines after coming back down"), Lines.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("Down took it back to the second line"), Lines[1].Contains(TEXT("Y")));
	TestFalse(TEXT("And not into the first"), Lines[0].Contains(TEXT("Y")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineSoftWrapEditTest,
	"DreamGUI.MultiLineEditableText.TypingAndBackspaceAtALineTheTextWrappedOnItsOwnEditTheTextAtTheWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMultiLineSoftWrapEditTest, "DreamGUI.MultiLineEditableText.TypingAndBackspaceAtALineTheTextWrappedOnItsOwnEditTheTextAtTheWrap", "[Pointer][Text][Animated]")

/*
 * A line the paragraph wraps on its own ends in a caret that names no character (-1), and the field took
 * that -1 for an offset into its text: a character typed on it was inserted at index -1, in front of the
 * string's own buffer, and Backspace at the start of the next line removed everything from the start of
 * the text up to the caret -- "The quick brown |fox" became "ox". The end of a wrapped line and the start of
 * the next are one position in the text. Here a narrow field wraps a run of short words; the caret is walked
 * onto the end of the first wrapped line to type there and take it back out, then onto the start of the next
 * line, where Backspace has to delete the one space the wrap stands on.
 */
bool FDreamMultiLineSoftWrapEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamMultiLineEditableTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Narrow enough that a run of four-letter words wraps, wide enough that none of them has to break.
	UDreamMultiLineEditableText* Field = Rig.MakeControl<UDreamMultiLineEditableText>(TEXT("Notes"), nullptr, FVector2D(120.0, 160.0));
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr && Field->TextNode != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	const FString Words(TEXT("aaaa bbbb cccc dddd eeee ffff"));
	FieldElement->Type(Words);
	const UDreamText* Shown = Cast<UDreamText>(Field->TextNode->GetVisual());
	if (!TestEqualSensitive(TEXT("The field holds what was typed"), Field->GetText(), Words)
		|| !TestNotNull(TEXT("The field has a text part to draw with"), Shown))
	{
		return false;
	}
	int32 NextLineCharIndex = INDEX_NONE;
	const int32 SoftWrapCaret = FindFirstSoftWrapCaret(*Shown, NextLineCharIndex);
	if (!TestTrue(TEXT("The words wrapped onto another line on their own"), SoftWrapCaret != INDEX_NONE)
		|| !TestTrue(TEXT("At a space"), NextLineCharIndex > 0 && NextLineCharIndex < Words.Len() && Words[NextLineCharIndex - 1] == TEXT(' ')))
	{
		return false;
	}

	// Onto the caret that ends the first wrapped line, and type there.
	FieldElement->Type(EKeys::Home);
	for (int32 Step = 0; Step < SoftWrapCaret; Step++)
	{
		FieldElement->Type(EKeys::Right);
	}
	FieldElement->Type(TEXT("x"));
	FString Expected = Words;
	Expected.InsertAt(NextLineCharIndex, TEXT('x'));
	TestEqualSensitive(TEXT("A character typed at the end of a wrapped line goes in at the wrap"), Field->GetText(), Expected);
	// The caret went on past it, so Backspace takes it straight back out.
	FieldElement->Type(EKeys::BackSpace);
	if (!TestEqualSensitive(TEXT("Backspace after it removes just that character"), Field->GetText(), Words))
	{
		return false;
	}

	// Onto the first caret of the next line, one further on, and delete backwards from there.
	FieldElement->Type(EKeys::Home);
	for (int32 Step = 0; Step < SoftWrapCaret + 1; Step++)
	{
		FieldElement->Type(EKeys::Right);
	}
	FieldElement->Type(EKeys::BackSpace);
	Expected = Words;
	Expected.RemoveAt(NextLineCharIndex - 1);
	TestEqualSensitive(TEXT("Backspace at the start of a wrapped line removes the one space before it"), Field->GetText(), Expected);

	return true;
}

#endif
