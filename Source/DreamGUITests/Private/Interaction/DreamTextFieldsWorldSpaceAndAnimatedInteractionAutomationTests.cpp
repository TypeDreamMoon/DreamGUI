// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamEditableText.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * THE THREE TEXT FIELDS -- THE BOXED ONE, THE BORDERLESS ONE AND THE MULTI-LINE ONE -- IN THE WORLD, AND A FINGER AND THE
 * KEYS ON THEM AS SHIPPED.
 *
 * UMG's UWidgetComponent hands what its interaction's ray lands on to Slate as an ordinary pointer, and an SEditableText
 * takes the keyboard focus from a click wherever the click came from: the characters typed after it go into it. The
 * press is matched to a character boundary in the text's own local space (FSlateEditableTextLayout::HandleMouseButtonDown),
 * so a click or a finger on a panel in the level puts the caret between the two characters under it exactly as on the
 * screen; a finger is the same press, FSlateApplication handing an unanswered touch on as a mouse press. A multi-line field
 * breaks the line on Enter rather than committing (SMultiLineEditableText), on a panel as on the screen.
 *
 * The D-pad on a panel is DreamGUI's directional scan among the world-space widgets, the confirm the pad's accept: the D-pad
 * down from a button reaches the field below it, and the confirm gives the field the keyboard, which is what editing it is
 * (SEditableText::OnFocusReceived starts the edit). Slate's own answer to a focusable widget below the focused one is the
 * same (FHittestGrid::FindNextFocusableWidget).
 *
 * Every edit begun on a panel or a surface is ended before the test does: neither is under the rig's root, and an edit left
 * open there is one the rig reports against the test.
 *
 * The screen-space tests here are the field as shipped, a finger tapping into its text and Tab walking into it, which the
 * boxed field already has and the other two had not.
 */
namespace DreamTextFieldsWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	const FVector2D SingleLineSize(360.0, 40.0);
	const FVector2D MultiLineSize(360.0, 160.0);

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	UDreamText* ShownTextOf(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->TextNode != nullptr ? Cast<UDreamText>(InField->TextNode->GetVisual()) : nullptr;
	}

	/** The pixel of the caret before caret InCaretIndex, off the text's own caret table, as the rig's camera sees it, or unset. */
	TOptional<FVector2D> CaretPixel(const FDreamDriverRig& InRig, UDreamText& InShown, int32 InCaretIndex)
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
		return LocalPointPixel(InRig, InShown.GetWidget(), FVector2D(CaretPosition.X, CaretPosition.Y));
	}

	/** The text typed before a tap, the caret it is aimed at, and what an X typed there makes of the whole text. */
	struct FTypedText
	{
		FString FirstLine;
		FString SecondLine;
		int32 AimedCharIndex = 0;
		FString WithX;

		FString Whole() const { return SecondLine.IsEmpty() ? FirstLine : FirstLine + TEXT("\n") + SecondLine; }
	};

	FTypedText TypedTextFor(bool bInMultiLine)
	{
		FTypedText Typed;
		if (bInMultiLine)
		{
			// "first line\nsecond |line": the caret before the second line's "line".
			Typed.FirstLine = TEXT("first line");
			Typed.SecondLine = TEXT("second line");
			Typed.AimedCharIndex = 18;
			Typed.WithX = TEXT("first line\nsecond Xline");
		}
		else
		{
			// "alpha |bravo": the caret before the second word.
			Typed.FirstLine = TEXT("alpha bravo");
			Typed.AimedCharIndex = 6;
			Typed.WithX = TEXT("alpha Xbravo");
		}
		return Typed;
	}

	/** Clicked into (through whatever camera the rig has) and typed into, a multi-line field's second line after an Enter. */
	bool ClickAndType(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamTextInput* InField, const FTypedText& InTyped)
	{
		FDreamElementRef FieldElement = InRig.Driver()->Find(FDreamBy::Widget(InField));
		bool bTyped = FieldElement->Type(InTyped.FirstLine);
		if (!InTyped.SecondLine.IsEmpty())
		{
			bTyped = bTyped && InRig.Driver()->Sequence().Key(EKeys::Enter).Perform() && FieldElement->Type(InTyped.SecondLine);
		}
		return InTest.TestTrue(TEXT("Clicking into the field and typing completes"), bTyped)
			&& InTest.TestEqual(TEXT("The field holds what was typed"), InField->GetText(), InTyped.Whole())
			&& InTest.TestTrue(TEXT("Waiting out the double-click time completes"), WaitOutDoubleClick(InRig));
	}

	/** The pixel of the caret the typed text is aimed at. A multi-line text counts each line's end caret too, so its index is asked of the text. */
	TOptional<FVector2D> AimedCaretPixel(const FDreamDriverRig& InRig, UDreamTextInput* InField, const FTypedText& InTyped, bool bInMultiLine)
	{
		UDreamText* Shown = ShownTextOf(InField);
		if (Shown == nullptr)
		{
			return TOptional<FVector2D>();
		}
		const int32 CaretIndex = bInMultiLine ? Shown->GetCaretIndexByCharIndex(InTyped.AimedCharIndex) : InTyped.AimedCharIndex;
		return CaretPixel(InRig, *Shown, CaretIndex);
	}

	/** Back ends an edit begun on a panel or a surface, keeping the text, before the rig goes. */
	void EndEdit(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const UDreamTextInput* InField, const FString& InKept)
	{
		if (!IsEditing(InField))
		{
			return;
		}
		InTest.TestTrue(TEXT("Back completes"), InRig.Driver()->Sequence().Back().WaitFrames(1).Perform());
		InTest.TestFalse(TEXT("Back ended the edit"), IsEditing(InField));
		InTest.TestEqual(TEXT("...keeping the text"), InField->GetText(), InKept);
	}

	/**
	 * A field on the panel: a click through the world pointer begins its edit and the characters typed go in. A single line
	 * ends its edit on Enter, keeping the text; a multi-line field breaks the line on Enter, stays in its edit, and takes the
	 * second line, until Back ends the edit.
	 */
	template<class TField>
	bool RunPanelClickAndType(FAutomationTestBase& InTest, const FVector2D& InSize, bool bInMultiLine)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		const FWorldStage Stage = SetUpPanel(InTest, Rig);
		TField* Field = Stage.IsReady() ? Rig.MakeControl<TField>(TEXT("Notes"), Stage.Panel, InSize) : nullptr;
		if (!InTest.TestNotNull(TEXT("A field on the panel"), Field))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("The camera sees the field on the panel"), PixelOf(Rig, Field).IsSet()))
		{
			return false;
		}

		FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Widget(Field));
		InTest.TestTrue(TEXT("Clicking into the field through the world pointer and typing completes"), FieldElement->Type(TEXT("first")));
		if (!InTest.TestTrue(TEXT("The click through the world pointer began the field's edit"), IsEditing(Field)))
		{
			return false;
		}
		InTest.TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("first")));

		InTest.TestTrue(TEXT("Enter completes"), Rig.Driver()->Sequence().Key(EKeys::Enter).WaitFrames(1).Perform());
		if (bInMultiLine)
		{
			InTest.TestTrue(TEXT("Enter broke the line and left the field in its edit"), IsEditing(Field));
			InTest.TestTrue(TEXT("Typing the second line completes"), Rig.Driver()->Sequence().Type(TEXT("second")).Perform());
			InTest.TestEqual(TEXT("The field holds both lines"), Field->GetText(), FString(TEXT("first\nsecond")));
			EndEdit(InTest, Rig, Field, FString(TEXT("first\nsecond")));
		}
		else
		{
			InTest.TestFalse(TEXT("Enter ended the edit"), IsEditing(Field));
			InTest.TestEqual(TEXT("...keeping the text"), Field->GetText(), FString(TEXT("first")));
		}
		return true;
	}

	/**
	 * A field on the panel, typed into and all of it selected with Ctrl+A, then a finger tapped on the caret before the aimed
	 * character through the world pointer. The selection is gone, the field is still being edited, and the X typed next goes
	 * in where the finger landed.
	 */
	template<class TField>
	bool RunPanelTapCaret(FAutomationTestBase& InTest, const FVector2D& InSize, bool bInMultiLine)
	{
		const FTypedText Typed = TypedTextFor(bInMultiLine);
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		const FWorldStage Stage = SetUpPanel(InTest, Rig);
		TField* Field = Stage.IsReady() ? Rig.MakeControl<TField>(TEXT("Notes"), Stage.Panel, InSize) : nullptr;
		if (!InTest.TestNotNull(TEXT("A field on the panel"), Field))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!ClickAndType(InTest, Rig, Field, Typed))
		{
			EndEdit(InTest, Rig, Field, Field->GetText());
			return false;
		}
		const TOptional<FVector2D> Aimed = AimedCaretPixel(Rig, Field, Typed, bInMultiLine);
		InTest.TestTrue(TEXT("Ctrl+A completes"), Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Perform());
		if (!InTest.TestTrue(TEXT("The caret aimed at is seen by the camera"), Aimed.IsSet())
			|| !InTest.TestTrue(TEXT("...and Ctrl+A selected everything"), Field->IsAnyTextSelected()))
		{
			EndEdit(InTest, Rig, Field, Field->GetText());
			return false;
		}

		InTest.TestTrue(TEXT("A finger tapped on the caret through the world pointer completes"),
			Rig.Driver()->Sequence().TouchDown(0, Aimed.GetValue()).TouchUp(0).Perform());
		InTest.TestTrue(TEXT("The field is still being edited"), IsEditing(Field));
		InTest.TestFalse(TEXT("The tap left nothing selected"), Field->IsAnyTextSelected());
		InTest.TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
		InTest.TestEqual(TEXT("The X went in where the finger landed on the panel"), Field->GetText(), Typed.WithX);
		EndEdit(InTest, Rig, Field, Typed.WithX);
		return true;
	}

	/**
	 * A button on the panel above the field and one further below it. With the focus on the upper button, the D-pad down
	 * reaches the field -- the nearer of the two below on the panel -- the pad's confirm starts its edit, and what is typed
	 * next goes in.
	 */
	template<class TField>
	bool RunPanelPadInto(FAutomationTestBase& InTest, const FVector2D& InSize)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		const FWorldStage Stage = SetUpPanel(InTest, Rig);
		// All on the panel, which reaches 160 cm above and below its middle.
		UDreamButton* Above = Stage.IsReady() ? Rig.MakeControl<UDreamButton>(TEXT("Above"), Stage.Panel, FVector2D(160.0, 40.0), FVector2D(0.0, 110.0)) : nullptr;
		TField* Field = Above != nullptr ? Rig.MakeControl<TField>(TEXT("Notes"), Stage.Panel, InSize, FVector2D(0.0, 30.0 - InSize.Y * 0.5)) : nullptr;
		UDreamButton* Far = Field != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Far"), Stage.Panel, FVector2D(160.0, 40.0), FVector2D(0.0, -140.0)) : nullptr;
		if (!InTest.TestTrue(TEXT("Two buttons and the field between them came up on the panel"), Far != nullptr && Above->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the upper button"), PutFocusOn(Rig, Above->FaceNode)))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		if (!InTest.TestTrue(TEXT("The D-pad down reached the field on the panel, not the button further below"), FocusIsIn(Rig, Field)))
		{
			return false;
		}
		InTest.TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
		InTest.TestTrue(TEXT("The confirm started the field's edit"), IsEditing(Field));
		InTest.TestTrue(TEXT("Typing completes"), Rig.Driver()->Sequence().Type(TEXT("ab")).Perform());
		InTest.TestEqual(TEXT("The characters went into the field on the panel"), Field->GetText(), FString(TEXT("ab")));
		EndEdit(InTest, Rig, Field, FString(TEXT("ab")));
		return true;
	}

	/**
	 * On the screen, as shipped: the field typed into and all of it selected with Ctrl+A, then a finger tapped on the caret
	 * before the aimed character. The selection is gone, the field is still being edited, and the X typed next goes in there.
	 */
	template<class TField>
	bool RunScreenTapCaret(FAutomationTestBase& InTest, const FVector2D& InSize, bool bInMultiLine)
	{
		const FTypedText Typed = TypedTextFor(bInMultiLine);
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		TField* Field = Rig.IsUsable() ? Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize) : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig and a field came up"), Field))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!ClickAndType(InTest, Rig, Field, Typed))
		{
			return false;
		}
		const TOptional<FVector2D> Aimed = AimedCaretPixel(Rig, Field, Typed, bInMultiLine);
		InTest.TestTrue(TEXT("Ctrl+A completes"), Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Perform());
		if (!InTest.TestTrue(TEXT("The caret aimed at is on the viewport"), Aimed.IsSet())
			|| !InTest.TestTrue(TEXT("...and Ctrl+A selected everything"), Field->IsAnyTextSelected()))
		{
			return false;
		}

		InTest.TestTrue(TEXT("A finger tapped on the caret completes"), Rig.Driver()->Sequence().TouchDown(0, Aimed.GetValue()).TouchUp(0).Perform());
		InTest.TestTrue(TEXT("The field is still being edited"), IsEditing(Field));
		InTest.TestFalse(TEXT("The tap left nothing selected"), Field->IsAnyTextSelected());
		InTest.TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
		InTest.TestEqual(TEXT("The X went in where the finger landed"), Field->GetText(), Typed.WithX);
		return true;
	}
}

// ------------------------------------------------------------------------------------------------ the borderless field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldEditableTextClickTest,
	"DreamGUI.EditableText.OnAWorldPanelAClickBeginsTheEditWhatIsTypedGoesInAndEnterEndsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldEditableTextClickTest, "DreamGUI.EditableText.OnAWorldPanelAClickBeginsTheEditWhatIsTypedGoesInAndEnterEndsIt", "[Pointer][Text][World]")

/* UMG's EditableText in a widget component: the click through the interaction focuses it and the keys go into it. */
bool FDreamWorldEditableTextClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelClickAndType<UDreamEditableText>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldEditableTextTapTest,
	"DreamGUI.EditableText.OnAWorldPanelAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldEditableTextTapTest, "DreamGUI.EditableText.OnAWorldPanelAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem", "[Touch][Text][World]")

/* HandleMouseButtonDown finds the boundary under the press in the text's own space, wherever on the panel it is drawn. */
bool FDreamWorldEditableTextTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelTapCaret<UDreamEditableText>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldEditableTextPadTest,
	"DreamGUI.EditableText.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldEditableTextPadTest, "DreamGUI.EditableText.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Text][World]")

bool FDreamWorldEditableTextPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelPadInto<UDreamEditableText>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedEditableTextTapTest,
	"DreamGUI.EditableText.AFingerTapInsideTheTextPutsTheCaretBetweenTheCharactersUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedEditableTextTapTest, "DreamGUI.EditableText.AFingerTapInsideTheTextPutsTheCaretBetweenTheCharactersUnderIt", "[Touch][Text][Animated]")

/* The borderless field as shipped, reached by a finger: the tap is the press HandleMouseButtonDown places the cursor from. */
bool FDreamAnimatedEditableTextTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunScreenTapCaret<UDreamEditableText>(*this, SingleLineSize, false);
}

// ------------------------------------------------------------------------------------------------ the multi-line field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldMultiLineClickTest,
	"DreamGUI.MultiLineEditableText.OnAWorldPanelAClickBeginsTheEditAndEnterBreaksTheLineItTypesInto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldMultiLineClickTest, "DreamGUI.MultiLineEditableText.OnAWorldPanelAClickBeginsTheEditAndEnterBreaksTheLineItTypesInto", "[Pointer][Text][World]")

/* SMultiLineEditableText on a panel: the click focuses it, Enter is a new line, and the second line goes in after it. */
bool FDreamWorldMultiLineClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelClickAndType<UDreamMultiLineEditableText>(*this, MultiLineSize, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceMultiLineTapTest,
	"DreamGUI.MultiLineEditableText.ShownOnARenderTargetSurfaceAFingerTapOnTheSecondLinePutsTheCaretWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSurfaceMultiLineTapTest, "DreamGUI.MultiLineEditableText.ShownOnARenderTargetSurfaceAFingerTapOnTheSecondLinePutsTheCaretWhereItLanded", "[Touch][Text][World]")

/*
 * The multi-line field drawn into a texture that a mesh in the level shows, as UMG's UWidgetComponent drawn to a render
 * target: two lines typed, all of them selected, then a finger tapped through the surface on the caret before the second
 * line's "line". The line as well as the column come out right -- the finger's hit on the mesh carried through the surface's
 * UV into the canvas -- so the X typed next goes in there.
 */
bool FDreamSurfaceMultiLineTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	const FTypedText Typed = TypedTextFor(true);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FSurfaceStage Stage = SetUpSurface(*this, Rig);
	UDreamMultiLineEditableText* Field = Stage.IsReady()
		? Rig.MakeControl<UDreamMultiLineEditableText>(TEXT("Notes"), Stage.Screen.CanvasRoot, MultiLineSize)
		: nullptr;
	if (!TestNotNull(TEXT("A multi-line field on the surface's canvas"), Field))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!ClickAndType(*this, Rig, Field, Typed))
	{
		EndEdit(*this, Rig, Field, Field->GetText());
		return false;
	}
	const TOptional<FVector2D> Aimed = AimedCaretPixel(Rig, Field, Typed, true);
	TestTrue(TEXT("Ctrl+A completes"), Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Perform());
	if (!TestTrue(TEXT("The caret aimed at is seen on the surface"), Aimed.IsSet())
		|| !TestTrue(TEXT("...and Ctrl+A selected everything"), Field->IsAnyTextSelected()))
	{
		EndEdit(*this, Rig, Field, Field->GetText());
		return false;
	}

	TestTrue(TEXT("A finger tapped on the caret through the surface completes"),
		Rig.Driver()->Sequence().TouchDown(0, Aimed.GetValue()).TouchUp(0).Perform());
	TestTrue(TEXT("The field is still being edited"), IsEditing(Field));
	TestFalse(TEXT("The tap left nothing selected"), Field->IsAnyTextSelected());
	TestTrue(TEXT("Typing X completes"), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
	TestEqual(TEXT("The X went in on the second line where the finger landed"), Field->GetText(), Typed.WithX);
	EndEdit(*this, Rig, Field, Typed.WithX);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldMultiLinePadTest,
	"DreamGUI.MultiLineEditableText.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldMultiLinePadTest, "DreamGUI.MultiLineEditableText.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Text][World]")

bool FDreamWorldMultiLinePadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	// Shorter than the other multi-line tests' field, so the button above and the one below fit the panel.
	return RunPanelPadInto<UDreamMultiLineEditableText>(*this, FVector2D(360.0, 100.0));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedMultiLineTapTest,
	"DreamGUI.MultiLineEditableText.AFingerTapOnTheSecondLinePutsTheCaretWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedMultiLineTapTest, "DreamGUI.MultiLineEditableText.AFingerTapOnTheSecondLinePutsTheCaretWhereItLanded", "[Touch][Text][Animated]")

/* As shipped, two lines: the finger's press is matched to a line and a column, as the mouse's is. */
bool FDreamAnimatedMultiLineTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunScreenTapCaret<UDreamMultiLineEditableText>(*this, MultiLineSize, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedMultiLineTabTest,
	"DreamGUI.MultiLineEditableText.TabFromTheButtonBeforeItStartsEditingTheFieldAndEnterThenBreaksTheLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedMultiLineTabTest, "DreamGUI.MultiLineEditableText.TabFromTheButtonBeforeItStartsEditingTheFieldAndEnterThenBreaksTheLine", "[Nav][Text][Animated]")

/*
 * UMG's MultiLineEditableText is a keyboard focus stop like the single-line one: Tab from the button before it gives it the
 * keyboard focus, which starts its edit, and the characters typed next go in. Inside it Enter is a new line, not a commit,
 * so the field stays in its edit and the next characters go on the second line.
 */
bool FDreamAnimatedMultiLineTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamButton* Before = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Before"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 140.0)) : nullptr;
	UDreamMultiLineEditableText* Field = Before != nullptr
		? Rig.MakeControl<UDreamMultiLineEditableText>(TEXT("Notes"), nullptr, MultiLineSize, FVector2D(0.0, -20.0))
		: nullptr;
	if (!TestTrue(TEXT("The rig, a button and a multi-line field came up"), Before != nullptr && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("The first Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	TestTrue(TEXT("The first Tab lands on the button"), FocusIsIn(Rig, Before));
	TestFalse(TEXT("...not in the field"), IsEditing(Field));
	TestTrue(TEXT("The second Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
	if (!TestTrue(TEXT("The second Tab started editing the field"), IsEditing(Field)))
	{
		return false;
	}
	TestTrue(TEXT("Typing, Enter and typing again complete"),
		Rig.Driver()->Sequence().Type(TEXT("ab")).Key(EKeys::Enter).Type(TEXT("cd")).Perform());
	TestTrue(TEXT("Enter left the field in its edit"), IsEditing(Field));
	TestEqual(TEXT("The characters went in, on two lines"), Field->GetText(), FString(TEXT("ab\ncd")));
	return true;
}

// ------------------------------------------------------------------------------------------------ the boxed field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTextInputTapTest,
	"DreamGUI.TextInput.OnAWorldPanelAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTextInputTapTest, "DreamGUI.TextInput.OnAWorldPanelAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem", "[Touch][Text][World]")

/* UMG's EditableTextBox in a widget component, reached by a finger (the mouse's is DreamGUI.Driver.WorldSpace.ClickingAField*). */
bool FDreamWorldTextInputTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelTapCaret<UDreamTextInput>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTextInputPadTest,
	"DreamGUI.TextInput.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldTextInputPadTest, "DreamGUI.TextInput.OnAWorldPanelTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Text][World]")

bool FDreamWorldTextInputPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsWorldTestLocal;
	return RunPanelPadInto<UDreamTextInput>(*this, SingleLineSize);
}

#endif
