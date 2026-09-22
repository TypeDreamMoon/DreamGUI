// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UITextInput.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The text field, which had no tests at all -- the one control whose behaviour class is bigger than
 * most of the controls put together, and the one place where a silent filter decision turns a value
 * the game set into a different value the player never typed.
 *
 * Everything here runs headless: no world, no registration, no render canvas, therefore NO LAYOUT.
 * That is not a limitation to work around, it is the state that matters: the caret-index <-> source
 * -offset mapping lives in the laid-out text, so every road tested below is a road that has to
 * answer without it. What is deliberately NOT here, because it cannot be reached without a world:
 * anything behind ActivateInput (it spawns an actor to own an InputComponent), which means the
 * selection-mask roads, the IME context, and the character-event entry point that requires an active
 * edit. Those are named in the ledger rather than faked with a mock that would test the mock.
 */
namespace DreamTextInputTestLocal
{
	static UUITextInput* MakeBehaviour()
	{
		return NewObject<UUITextInput>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputSetTextValidatesAgainstTheNewTextTest,
	"DreamGUI.TextInput.SetTextIsCheckedAgainstTheStringItIsBuildingNotTheOneItReplaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputSetTextValidatesAgainstTheNewTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	// The one on the books: a DecimalNumber field's "only one dot" rule was asked of the text the
	// field ALREADY held, so replacing "3.5" with "1.2" had its dot refused by the dot of the value
	// being thrown away, and the field ended up spelling "12" -- a different number, silently.
	Input->SetInputType(EUITextInputType::DecimalNumber);
	Input->SetText(TEXT("3.5"));
	TestEqual(TEXT("the first fractional value goes in whole"), Input->GetText(), FString(TEXT("3.5")));
	Input->SetText(TEXT("1.2"));
	TestEqual(TEXT("and a second fractional value keeps its dot"), Input->GetText(), FString(TEXT("1.2")));

	// The same rule still bites inside ONE value, which is the half that must not be lost: two dots
	// in one string is still one dot too many.
	Input->SetText(TEXT("1.2.3"));
	TestEqual(TEXT("a second dot in the same value is still refused"), Input->GetText(), FString(TEXT("1.23")));

	// The minus keeps its position rule against the string being built, not against the old one.
	Input->SetText(TEXT("-4.5"));
	TestEqual(TEXT("a leading minus survives a replacement"), Input->GetText(), FString(TEXT("-4.5")));

	// EmailAddress has the same shape of rule for '@'.
	TStrongObjectPtr<UUITextInput> Mail(MakeBehaviour());
	Mail->SetInputType(EUITextInputType::EmailAddress);
	Mail->SetText(TEXT("a@b.com"));
	Mail->SetText(TEXT("c@d.com"));
	TestEqual(TEXT("a replacement address keeps its at sign"), Mail->GetText(), FString(TEXT("c@d.com")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputEmailDotNeighbourTest,
	"DreamGUI.TextInput.AnEmailFieldLooksAtTheCharacterBeforeTheDotItIsAbouttoAccept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputEmailDotNeighbourTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());
	Input->SetInputType(EUITextInputType::EmailAddress);

	// "more than one dot in a row are not allowed" is the documented rule. The variable that was
	// meant to hold the character before the caret read the character AT it, so the character to
	// the left was never looked at and a run of dots typed straight through.
	Input->SetText(TEXT("a..b"));
	TestEqual(TEXT("two dots in a row collapse to one"), Input->GetText(), FString(TEXT("a.b")));
	Input->SetText(TEXT("x.y.z"));
	TestEqual(TEXT("dots that are not adjacent are all kept"), Input->GetText(), FString(TEXT("x.y.z")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputControlCharactersTest,
	"DreamGUI.TextInput.ControlCharactersNeverReachTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputControlCharactersTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	// Standard accepts everything, which used to include NUL and ESC straight off the clipboard --
	// and a string with an embedded NUL has a length nobody agrees on.
	FString WithControls;
	WithControls.AppendChar(TEXT('a'));
	WithControls.AppendChar(TCHAR(27));//ESC
	WithControls.AppendChar(TEXT('b'));
	WithControls.AppendChar(TCHAR(11));//vertical tab
	WithControls.AppendChar(TEXT('c'));
	Input->SetText(WithControls);
	TestEqual(TEXT("only the printable characters survive"), Input->GetText(), FString(TEXT("abc")));

	// A single-line field has no lines to make, so a newline is not text for it either.
	Input->SetText(TEXT("one\ntwo"));
	TestEqual(TEXT("a single line field has no newline in it"), Input->GetText(), FString(TEXT("onetwo")));

	TStrongObjectPtr<UUITextInput> MultiLine(MakeBehaviour());
	MultiLine->SetAllowMultiLine(true);
	MultiLine->SetText(TEXT("one\ntwo"));
	TestEqual(TEXT("a multiline field keeps its newline"), MultiLine->GetText(), FString(TEXT("one\ntwo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMaxLengthTest,
	"DreamGUI.TextInput.AMaxLengthStopsTheTextAtItsLimitAndTrimsWhatIsAlreadyThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputMaxLengthTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	TestEqual(TEXT("no limit is the default, as it always was"), Input->GetMaxLength(), 0);
	Input->SetText(TEXT("abcdefghij"));
	TestEqual(TEXT("and an unlimited field takes the lot"), Input->GetText().Len(), 10);

	// Lowering the limit under text that is already there trims it, rather than leaving the field
	// holding a value it would now refuse.
	Input->SetMaxLength(4);
	TestEqual(TEXT("the existing text is cut to the new limit"), Input->GetText(), FString(TEXT("abcd")));

	// ...and the limit holds on the way in, too.
	Input->SetText(TEXT("zyxwvut"));
	TestEqual(TEXT("a wholesale push stops at the limit"), Input->GetText(), FString(TEXT("zyxw")));

	// Typing one character at a time is the fourth road in, and answers to the same number.
	TestFalse(TEXT("a character past the limit is refused"), Input->VerifyAndInsertCharAtCaretPosition(TEXT('q')));
	TestEqual(TEXT("and the text did not grow"), Input->GetText(), FString(TEXT("zyxw")));

	Input->SetMaxLength(6);
	TestTrue(TEXT("room made is room used"), Input->VerifyAndInsertCharAtCaretPosition(TEXT('q')));
	TestEqual(TEXT("and the character landed at the caret"), Input->GetText(), FString(TEXT("qzyxw")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputRevalidateOnTypeChangeTest,
	"DreamGUI.TextInput.ChangingTheInputTypeRerunsTheRulesOverTheTextAlreadyInTheField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputRevalidateOnTypeChangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	// Switching a field to IntegerNumber used to leave whatever letters were in it sitting there,
	// and the very next keystroke was then checked against a string the type says cannot exist.
	Input->SetText(TEXT("ab12cd34"));
	Input->SetInputType(EUITextInputType::IntegerNumber);
	TestEqual(TEXT("the letters are gone the moment the rules change"), Input->GetText(), FString(TEXT("1234")));

	Input->SetInputType(EUITextInputType::Standard);
	Input->SetText(TEXT("hello world"));
	Input->SetInputType(EUITextInputType::Alphanumeric);
	TestEqual(TEXT("and the space goes when the rules say letters and digits"), Input->GetText(), FString(TEXT("helloworld")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputUndoRedoTest,
	"DreamGUI.TextInput.UndoPutsBackTheTextFromBeforeTheEditAndRedoTakesItAwayAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputUndoRedoTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	Input->SetText(TEXT("ab"));
	TestFalse(TEXT("a value pushed from code is not an edit to undo"), Input->Undo());

	TestTrue(TEXT("the first character goes in"), Input->VerifyAndInsertCharAtCaretPosition(TEXT('x')));
	TestTrue(TEXT("and the second"), Input->VerifyAndInsertCharAtCaretPosition(TEXT('y')));
	TestEqual(TEXT("both characters are in the text"), Input->GetText(), FString(TEXT("xyab")));

	TestTrue(TEXT("one undo steps back one edit"), Input->Undo());
	TestEqual(TEXT("leaving the text as it was before that edit"), Input->GetText(), FString(TEXT("xab")));
	TestTrue(TEXT("a second undo steps back the first edit"), Input->Undo());
	TestEqual(TEXT("and the text is back where the edits started"), Input->GetText(), FString(TEXT("ab")));
	TestFalse(TEXT("there is nothing before the beginning"), Input->Undo());

	TestTrue(TEXT("redo walks forward again"), Input->Redo());
	TestEqual(TEXT("one edit at a time"), Input->GetText(), FString(TEXT("xab")));
	TestTrue(TEXT("and again"), Input->Redo());
	TestEqual(TEXT("to where the undos started"), Input->GetText(), FString(TEXT("xyab")));
	TestFalse(TEXT("and no further"), Input->Redo());

	// A fresh edit after an undo abandons the branch that was redoable: keeping it would let a redo
	// jump to a string that no longer has anything to do with what is in the field.
	Input->Undo();
	TestTrue(TEXT("a new edit lands on the undone text"), Input->VerifyAndInsertCharAtCaretPosition(TEXT('z')));
	TestFalse(TEXT("and the abandoned branch is not redoable"), Input->Redo());

	// A read-only field has no edits, so it has no history to walk either.
	TStrongObjectPtr<UUITextInput> ReadOnly(MakeBehaviour());
	ReadOnly->SetText(TEXT("fixed"));
	ReadOnly->SetReadOnly(true);
	TestFalse(TEXT("a read only field refuses characters"), ReadOnly->VerifyAndInsertCharAtCaretPosition(TEXT('q')));
	TestFalse(TEXT("and has nothing to undo"), ReadOnly->Undo());
	TestEqual(TEXT("its text is untouched"), ReadOnly->GetText(), FString(TEXT("fixed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCharacterRouterTest,
	"DreamGUI.TextInput.APlatformCharacterWithNoFieldBeingEditedGoesNowhere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputCharacterRouterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());

	// The entry point a host (a UGameViewportClient::InputChar override, a Slate host) uses to hand
	// this component real platform characters instead of leaving it to guess them from key codes.
	// With nothing being edited there is no field to hand them to, and saying so is the whole
	// contract -- the host must not have to track which field has the keyboard.
	TestNull(TEXT("nothing is being edited"), UUITextInput::GetActiveTextInput());
	TestFalse(TEXT("so a character is not taken"), UUITextInput::RouteCharacterInputToActiveInput(TEXT('a')));
	TestFalse(TEXT("and an inactive field takes none either"), Input->HandleCharacterInput(TEXT('a')));
	TestEqual(TEXT("its text is untouched"), Input->GetText(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMultiLineWrapTest,
	"DreamGUI.Controls.TextInput.AMultiLineFieldsTextIsSetToTheOverflowThatWraps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputMultiLineWrapTest::RunTest(const FString& Parameters)
{
	// Wrapping is not a switch of its own: DreamTextLayout only looks for break opportunities when
	// the text's OverflowType is VerticalOverflow. Only SetTextVisual pushed it, and the control
	// calls SetTextVisual (WireParts) BEFORE it knows the line mode (ApplyStyle) -- so a multiline
	// field built by the control sat on the single-line overflow and never wrapped a long line.
	TDreamTestControl<UDreamTextInput> Field(NewObject<UDreamTextInput>(GetTransientPackage()));
	Field->bMultiLine = true;
	Field->Text = TEXT("a line long enough to need somewhere to go");
	Field->Initialize();

	if (!TestNotNull(TEXT("the text node exists"), Field->TextNode.Get()) ||
		!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	UDreamText* TextVisual = Cast<UDreamText>(Field->TextNode->GetVisual());
	if (!TestNotNull(TEXT("the text node carries a text visual"), TextVisual))
	{
		return false;
	}
	TestTrue(TEXT("the behaviour is in multiline mode"), Field->InputBehaviour->GetAllowMultiLine());
	TestEqual(TEXT("and its text is on the overflow that wraps"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::VerticalOverflow);

	// The other direction has to keep working from the same one writer.
	Field->InputBehaviour->SetAllowMultiLine(false);
	TestEqual(TEXT("back to one line puts the overflow back"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::HorizontalOverflow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputControlSurfacesBehaviourSettingsTest,
	"DreamGUI.Controls.TextInput.TheControlHandsTheFieldEveryInputSettingItWasAuthoredWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputControlSurfacesBehaviourSettingsTest::RunTest(const FString& Parameters)
{
	// A password box could not be written as Native.TextInput at all: the behaviour has had
	// InputType, DisplayType, PasswordChar, read-only, the validator and the ignore list since the
	// beginning, and the control exposed Text, Placeholder and bMultiLine. Authored here the way a
	// .dui line would leave them -- before Initialize -- because that is the order that broke.
	TDreamTestControl<UDreamTextInput> Field(NewObject<UDreamTextInput>(GetTransientPackage()));
	Field->InputType = EUITextInputType::DecimalNumber;
	Field->DisplayType = EUITextInputDisplayType::Password;
	Field->PasswordChar = TEXT("#");
	Field->bReadOnly = true;
	Field->MaxLength = 6;
	Field->IgnoreKeys = { EKeys::Tab };
	Field->bSelectAllWhenActivateInput = false;
	Field->bAutoActivateInputWhenNavigateIn = true;
	Field->Text = TEXT("12.5");
	Field->Initialize();

	if (!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour.Get();
	TestEqual(TEXT("the input type reached the field"), (int32)Behaviour->GetInputType(), (int32)EUITextInputType::DecimalNumber);
	TestEqual(TEXT("the display type reached the field"), (int32)Behaviour->GetDisplayType(), (int32)EUITextInputDisplayType::Password);
	TestEqual(TEXT("the password character reached the field"), Behaviour->GetPasswordChar(), FString(TEXT("#")));
	TestTrue(TEXT("read only reached the field"), Behaviour->GetReadOnly());
	TestEqual(TEXT("the length cap reached the field"), Behaviour->GetMaxLength(), 6);
	TestEqual(TEXT("the ignore list reached the field"), Behaviour->GetIgnoreKeys().Num(), 1);
	TestFalse(TEXT("select-all-on-focus reached the field"), Behaviour->GetSelectAllWhenActivateInput());
	TestTrue(TEXT("activate-on-navigate reached the field"), Behaviour->GetAutoActivateInputWhenNavigateIn());

	// And the authored value survived the trip: it is pushed AFTER the rules, so the rules it is
	// checked against are the ones that were authored, not the ones the field shipped with.
	TestEqual(TEXT("the authored value kept its dot"), Behaviour->GetText(), FString(TEXT("12.5")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCaretIsVisibleOnItsOwnThemeTest,
	"DreamGUI.Controls.TextInput.TheCaretIsNotTheSameColourAsTheBoxItBlinksIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputCaretIsVisibleOnItsOwnThemeTest::RunTest(const FString& Parameters)
{
	// The library's own dark theme with the behaviour's own caret default: (50,50,50) on (38,42,52)
	// is a caret nobody can see, in the one state every project starts in. The style had no caret
	// field at all, so there was nowhere to fix it either.
	const FDreamTextInputStyle Default;
	const int32 CaretToBackgroundDistance =
		FMath::Abs((int32)Default.CaretColor.R - (int32)Default.Background.R)
		+ FMath::Abs((int32)Default.CaretColor.G - (int32)Default.Background.G)
		+ FMath::Abs((int32)Default.CaretColor.B - (int32)Default.Background.B);
	TestTrue(TEXT("the default caret stands well clear of the default box"), CaretToBackgroundDistance > 200);
	TestTrue(TEXT("the caret is opaque"), Default.CaretColor.A == 255);

	TDreamTestControl<UDreamTextInput> Field(NewObject<UDreamTextInput>(GetTransientPackage()));
	Field->Initialize();
	if (!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	// ...and the style is what drives it, rather than the behaviour's own default sitting untouched.
	TestEqual(TEXT("the style's caret colour reached the field"), Field->InputBehaviour->GetCaretColor(), Default.CaretColor);
	TestEqual(TEXT("the style's selection colour reached the field"), Field->InputBehaviour->GetSelectionColor(), Default.SelectionColor);
	TestEqual(TEXT("the style's caret width reached the field"), Field->InputBehaviour->GetCaretWidth(), Default.CaretWidth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCompositionRangeTest,
	"DreamGUI.TextInput.AnImeCompositionRunIsRememberedAndClampedToTheTextItMarks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputCompositionRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTestLocal;
	TStrongObjectPtr<UUITextInput> Input(MakeBehaviour());
	Input->SetText(TEXT("abcdef"));

	// UpdateCompositionRange was an empty function body, so composition text went into Text and was
	// pixel-identical to text the player had already committed. The range is the bookkeeping the
	// underline is drawn from; the strips themselves need a laid-out text, which a headless tree has
	// no such thing as, so what is asserted here is the range -- including the clamping, because the
	// IME keeps its own idea of the range and hands it back after the game has changed the text.
	TestEqual(TEXT("nothing is being composed to begin with"), Input->GetCompositionLength(), 0);

	Input->SetCompositionRange(2, 3);
	TestEqual(TEXT("the run starts where the IME said"), Input->GetCompositionBeginIndex(), 2);
	TestEqual(TEXT("and is as long as it said"), Input->GetCompositionLength(), 3);

	// A range that runs off the end of the text is cut to the text, not trusted.
	Input->SetCompositionRange(4, 99);
	TestEqual(TEXT("an overlong run stops at the end of the text"), Input->GetCompositionLength(), 2);
	Input->SetCompositionRange(99, 3);
	TestEqual(TEXT("a run starting past the end starts at the end"), Input->GetCompositionBeginIndex(), 6);
	TestEqual(TEXT("and marks nothing"), Input->GetCompositionLength(), 0);

	Input->SetCompositionRange(1, 2);
	Input->SetCompositionRange(0, 0);
	TestEqual(TEXT("a zero length clears the mark"), Input->GetCompositionLength(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputContextMenuTest,
	"DreamGUI.Controls.TextInput.TheEditMenuOffersOnlyTheEntriesThatCanActRightNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputContextMenuTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamTextInput> Field(NewObject<UDreamTextInput>(GetTransientPackage()));
	Field->Text = TEXT("hello");
	Field->Initialize();
	if (!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()) ||
		!TestNotNull(TEXT("the field's own widget exists"), Field->BackgroundNode.Get()))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour.Get();
	auto HasEntry = [&Field](const TCHAR* InName)
	{
		return Field->BackgroundNode->FindChildArrayByDisplayName(FString::Printf(TEXT("ContextMenuEntry_%s"), InName), true).Num() > 0;
	};

	TestFalse(TEXT("no menu before anything asks for one"), Behaviour->IsContextMenuOpen());
	Behaviour->ShowContextMenu();
	TestTrue(TEXT("a field with text can at least offer Select All"), Behaviour->IsContextMenuOpen());
	TestTrue(TEXT("and does"), HasEntry(TEXT("SelectAll")));
	// Only what can act: an entry that does nothing when pressed is worse than one not offered.
	// There is no selection in a field nobody has dragged in, so there is nothing to cut or copy.
	TestFalse(TEXT("nothing is selected, so nothing can be cut"), HasEntry(TEXT("Cut")));
	TestFalse(TEXT("or copied"), HasEntry(TEXT("Copy")));
	TestFalse(TEXT("and nothing has been edited, so there is nothing to undo"), HasEntry(TEXT("Undo")));
	TestFalse(TEXT("or redo"), HasEntry(TEXT("Redo")));

	// The white trap this suite keeps finding: an entry is a selectable, and a selectable with no
	// explicit transition colours tints its own visual WHITE the moment a pointer touches it.
	TArray<UDreamWidget*> SelectAllEntries = Field->BackgroundNode->FindChildArrayByDisplayName(TEXT("ContextMenuEntry_SelectAll"), true);
	if (TestTrue(TEXT("the Select All entry is a widget of its own"), SelectAllEntries.Num() > 0))
	{
		if (UUIButton* EntryButton = SelectAllEntries[0]->GetComponent<UUIButton>())
		{
			TestTrue(TEXT("and its normal colour is the menu's, not white"), EntryButton->GetNormalColor() != FColor::White);
			TestTrue(TEXT("as is its hovered colour"), EntryButton->GetHoveredColor() != FColor::White);
		}
	}

	Behaviour->HideContextMenu();
	TestFalse(TEXT("closing it closes it"), Behaviour->IsContextMenuOpen());
	TestFalse(TEXT("and takes its entries with it"), HasEntry(TEXT("SelectAll")));

	// An edit makes Undo an answer, and the menu is rebuilt from the state at the moment it opens.
	Behaviour->VerifyAndInsertCharAtCaretPosition(TEXT('x'));
	Behaviour->ShowContextMenu();
	TestTrue(TEXT("an edit gives the menu something to undo"), HasEntry(TEXT("Undo")));
	Behaviour->HideContextMenu();

	// Turned off is turned off, which is UMG's AllowContextMenu.
	TDreamTestControl<UDreamTextInput> NoMenuField(NewObject<UDreamTextInput>(GetTransientPackage()));
	NoMenuField->Text = TEXT("hello");
	NoMenuField->Initialize();
	if (TestNotNull(TEXT("the second field's behaviour exists"), NoMenuField->InputBehaviour.Get()))
	{
		NoMenuField->InputBehaviour->SetAllowContextMenu(false);
		NoMenuField->InputBehaviour->ShowContextMenu();
		TestFalse(TEXT("a field that forbids the menu does not open one"), NoMenuField->InputBehaviour->IsContextMenuOpen());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
