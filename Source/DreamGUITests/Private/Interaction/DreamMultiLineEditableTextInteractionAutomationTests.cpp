// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamEditableText.h"
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMultiLineEnterBreaksTheLineTest,
	"DreamGUI.MultiLineEditableText.EnterStartsANewLineInsteadOfCommitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

#endif
