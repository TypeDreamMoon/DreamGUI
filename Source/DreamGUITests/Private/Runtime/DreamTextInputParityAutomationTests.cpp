// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamEditableText.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The text field's knobs, asked the one question a knob can fail: does writing it at RUNTIME reach
 * the thing that acts on it.
 *
 * Every property here was already authored and already pushed once, at build, so a .dui line and a
 * Blueprint default have always worked. What did not work was a Set node: nothing in this family
 * re-derives a control from a property that moved, so writing the variable changed the panel and
 * left the field behaving exactly as it did before. That is what each test below reads the
 * BEHAVIOUR for rather than the control's own field -- asserting the control remembers what it was
 * told would pass with the push deleted.
 *
 * Headless, like the rest of the control suite: no world, no registration, no layout pass. So
 * nothing here begins an EDIT -- ActivateInput spawns an actor to own an InputComponent and asks
 * Slate for a text input method system, neither of which exists under -nullrhi -unattended. The
 * roads that need a live edit (read-only ending one, a cancel reverting the value, Enter keeping
 * the keyboard) are named in the ledger instead of being faked with a mock that would test the mock.
 */
namespace DreamTextInputParityTestLocal
{
	/** A field with its OWN style, so the assertions do not depend on whether a project sheet exists. */
	template<class T>
	T* Author(float InWidth = 220.0f, float InHeight = 34.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputRuntimeSettersReachTheBehaviourTest,
	"DreamGUI.TextInput.Parity.EveryInputSettingWrittenAtRuntimeReachesTheFieldAndNotJustTheControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputRuntimeSettersReachTheBehaviourTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Initialize();
	if (!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour.Get();

	// Each of these is written AFTER the control was built, which is the case the authored-property
	// road never covered: the build-time push has already happened by now.
	Field->SetInputType(EUITextInputType::Alphanumeric);
	TestEqual(TEXT("the input type reached the field"),
		(int32)Behaviour->GetInputType(), (int32)EUITextInputType::Alphanumeric);

	Field->SetIsPassword(true);
	TestEqual(TEXT("the boolean password spelling reached the field as a display type"),
		(int32)Behaviour->GetDisplayType(), (int32)EUITextInputDisplayType::Password);
	TestTrue(TEXT("and the control agrees with itself about it"), Field->GetIsPassword());
	Field->SetIsPassword(false);
	TestEqual(TEXT("and back again"),
		(int32)Behaviour->GetDisplayType(), (int32)EUITextInputDisplayType::Standard);

	Field->SetPasswordChar(TEXT("#"));
	TestEqual(TEXT("the password character reached the field"), Behaviour->GetPasswordChar(), FString(TEXT("#")));

	Field->SetIsReadOnly(true);
	TestTrue(TEXT("read only reached the field"), Behaviour->GetReadOnly());

	Field->SetMaxLength(6);
	TestEqual(TEXT("the length cap reached the field"), Behaviour->GetMaxLength(), 6);

	Field->SetIgnoreKeys({ EKeys::Tab, EKeys::Up });
	TestEqual(TEXT("the ignore list reached the field"), Behaviour->GetIgnoreKeys().Num(), 2);

	Field->SetMultiLineSubmitFunctionKeys({ EKeys::LeftControl });
	TestEqual(TEXT("the submit chord reached the field"), Behaviour->GetMultiLineSubmitFunctionKeys().Num(), 1);

	Field->SetSelectAllWhenActivateInput(false);
	TestFalse(TEXT("select-all-on-focus reached the field"), Behaviour->GetSelectAllWhenActivateInput());

	Field->SetAutoActivateInputWhenNavigateIn(true);
	TestTrue(TEXT("activate-on-navigate reached the field"), Behaviour->GetAutoActivateInputWhenNavigateIn());

	Field->SetSubmitWhenDeactivate(false);
	TestFalse(TEXT("commit-on-leaving reached the field"), Behaviour->GetSubmitWhenDeactivate());

	Field->SetAllowContextMenu(false);
	TestFalse(TEXT("the edit menu switch reached the field"), Behaviour->GetAllowContextMenu());

	// The four UMG knobs that had no home at all before this.
	Field->SetRevertTextOnEscape(true);
	TestTrue(TEXT("revert-on-escape reached the field"), Behaviour->GetRevertTextOnEscape());

	Field->SetClearKeyboardFocusOnCommit(false);
	TestFalse(TEXT("keep-the-keyboard-on-commit reached the field"), Behaviour->GetClearKeyboardFocusOnCommit());

	Field->SetSelectAllTextOnCommit(true);
	TestTrue(TEXT("select-all-on-commit reached the field"), Behaviour->GetSelectAllTextOnCommit());

	Field->SetIsCaretMovedWhenGainFocus(false);
	TestFalse(TEXT("leave-the-caret-where-it-was reached the field"), Behaviour->GetIsCaretMovedWhenGainFocus());

	// And the three mobile ones, which have no way of being observed on a desktop test except here.
	Field->SetKeyboardType(EVirtualKeyboardType::Email);
	TestEqual(TEXT("the keyboard type reached the field"),
		(int32)Behaviour->GetKeyboardType().GetValue(), (int32)EVirtualKeyboardType::Email);

	Field->SetVirtualKeyboardTrigger(EVirtualKeyboardTrigger::OnFocusByPointer);
	TestEqual(TEXT("the keyboard trigger reached the field"),
		(int32)Behaviour->GetVirtualKeyboardTrigger(), (int32)EVirtualKeyboardTrigger::OnFocusByPointer);

	Field->SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction::TextChangeOnDismiss);
	TestEqual(TEXT("the dismiss action reached the field"),
		(int32)Behaviour->GetVirtualKeyboardDismissAction(), (int32)EVirtualKeyboardDismissAction::TextChangeOnDismiss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputDefaultsAreTheOldBehaviourTest,
	"DreamGUI.TextInput.Parity.TheNewKnobsDefaultToWhatTheFieldAlreadyDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputDefaultsAreTheOldBehaviourTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	// Three of these disagree with UMG's own defaults on purpose, and that is the whole point of the
	// test: Enter has always ended the edit here, every activation has always raised the mobile
	// keyboard, and dismissing it has always committed. A default that matched UMG would change what
	// every screen already shipped does, which is not a default but a migration.
	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Initialize();
	if (!TestNotNull(TEXT("the behaviour exists"), Field->InputBehaviour.Get()))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour.Get();

	TestTrue(TEXT("Enter still ends the edit"), Behaviour->GetClearKeyboardFocusOnCommit());
	TestEqual(TEXT("every activation still raises the mobile keyboard"),
		(int32)Behaviour->GetVirtualKeyboardTrigger(), (int32)EVirtualKeyboardTrigger::OnAllFocusEvents);
	TestEqual(TEXT("and dismissing it still commits"),
		(int32)Behaviour->GetVirtualKeyboardDismissAction(), (int32)EVirtualKeyboardDismissAction::TextCommitOnDismiss);
	// These two match UMG and the old behaviour at once, which is the easy case.
	TestFalse(TEXT("Escape still keeps what was typed"), Behaviour->GetRevertTextOnEscape());
	TestTrue(TEXT("gaining the edit still moves the caret to the end"), Behaviour->GetIsCaretMovedWhenGainFocus());
	// Default means "derive the keyboard from the input type", which is what the field did before it
	// could be told one.
	TestEqual(TEXT("and no keyboard is stated outright"),
		(int32)Behaviour->GetKeyboardType().GetValue(), (int32)EVirtualKeyboardType::Default);

	// The one that is not a behaviour flag: a field with no stated minimum keeps the width it was
	// placed at, rather than being widened to some default.
	TestEqual(TEXT("a field with no stated minimum keeps its placed width"), Field->GetWidth(), 220.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputErrorStateTest,
	"DreamGUI.TextInput.Parity.AnErrorGetsItsOwnParagraphAndTakesNoRoomWhenThereIsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputErrorStateTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Style.ErrorColor = FColor(200, 20, 20, 255);
	Field->Initialize();

	if (!TestNotNull(TEXT("the field built an error paragraph"), Field->ErrorNode.Get()))
	{
		return false;
	}
	UDreamText* ErrorVisual = Cast<UDreamText>(Field->ErrorNode->GetVisual());
	if (!TestNotNull(TEXT("and it is a text node"), ErrorVisual))
	{
		return false;
	}

	// The state a field is in almost all of the time. Asleep rather than empty-but-awake, because an
	// awake paragraph still lays out and a healthy field must cost what it always did.
	TestFalse(TEXT("a field with nothing wrong has no error"), Field->HasError());
	TestFalse(TEXT("and its error paragraph is asleep"), Field->ErrorNode->GetWidgetActive());

	Field->SetError(FText::AsCultureInvariant(TEXT("Name is taken")));
	TestTrue(TEXT("a reported error is reported"), Field->HasError());
	TestTrue(TEXT("and wakes the paragraph"), Field->ErrorNode->GetWidgetActive());
	TestEqual(TEXT("which carries the message"),
		ErrorVisual->GetText().ToString(), FString(TEXT("Name is taken")));
	TestEqual(TEXT("in the style's error colour"), ErrorVisual->GetColor(), FColor(200, 20, 20, 255));
	// At the right end of the box whatever the value's alignment is -- the message annotates the
	// field, it is not part of the value.
	TestEqual(TEXT("and at the right end of the field"),
		(int32)ErrorVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);

	Field->ClearError();
	TestFalse(TEXT("clearing it clears it"), Field->HasError());
	TestFalse(TEXT("and puts the paragraph away again"), Field->ErrorNode->GetWidgetActive());

	// UMG's SetError treats an empty message as a clear, and so does this.
	Field->SetError(FText::GetEmpty());
	TestFalse(TEXT("an empty message is a clear"), Field->HasError());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputJustificationTest,
	"DreamGUI.TextInput.Parity.TheHintSitsWhereTypingWillAppear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputJustificationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Initialize();

	UDreamText* TextVisual = Field->TextNode != nullptr ? Cast<UDreamText>(Field->TextNode->GetVisual()) : nullptr;
	UDreamText* HintVisual = Field->PlaceholderNode != nullptr ? Cast<UDreamText>(Field->PlaceholderNode->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the value's paragraph exists"), TextVisual) ||
		!TestNotNull(TEXT("and the hint's"), HintVisual))
	{
		return false;
	}

	// The style push used to write Left unconditionally, which is the default here for that reason.
	TestEqual(TEXT("a field still starts left aligned"),
		(int32)TextVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Left);

	Field->SetJustification(EDreamUITextParagraphHorizontalAlign::Center);
	TestEqual(TEXT("the value follows the justification"),
		(int32)TextVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);
	// The half worth a test: a hint at the other end of the box is a hint about a different field
	// than the one that is about to be typed into.
	TestEqual(TEXT("and so does the hint"),
		(int32)HintVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);

	// And it survives a whole style push, which is the road an editor property edit takes.
	Field->ApplyStyle();
	TestEqual(TEXT("a restyle keeps the value's alignment"),
		(int32)TextVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);
	TestEqual(TEXT("and the hint's"),
		(int32)HintVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMinimumDesiredWidthTest,
	"DreamGUI.TextInput.Parity.AMinimumDesiredWidthIsAFloorAndNeverASize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputMinimumDesiredWidthTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	// Narrower than the minimum it is about to be given: the field is widened to the floor.
	TDreamTestControl<UDreamTextInput> Narrow(Author<UDreamTextInput>(80.0f, 34.0f));
	Narrow->Initialize();
	Narrow->SetMinimumDesiredWidth(150.0f);
	TestEqual(TEXT("a field narrower than its minimum grows to it"), Narrow->GetWidth(), 150.0f);
	if (UDreamText* NarrowText = Narrow->TextNode != nullptr ? Cast<UDreamText>(Narrow->TextNode->GetVisual()) : nullptr)
	{
		// The other consumer: a parent that measures the PARAGRAPH rather than the control's rect.
		TestEqual(TEXT("and the paragraph asks its parent for the same floor"),
			NarrowText->GetMinDesiredWidth(), 150.0f);
	}

	// Wider already: nothing moves. A minimum that resized a field that already satisfied it would be
	// a size, and whoever placed the control decided the width.
	TDreamTestControl<UDreamTextInput> Wide(Author<UDreamTextInput>(400.0f, 34.0f));
	Wide->Initialize();
	Wide->SetMinimumDesiredWidth(150.0f);
	TestEqual(TEXT("a field already wider than its minimum keeps its width"), Wide->GetWidth(), 400.0f);

	// Zero is "no opinion", and it has to be, because it is what every field that already exists says.
	TDreamTestControl<UDreamTextInput> Silent(Author<UDreamTextInput>(60.0f, 34.0f));
	Silent->Initialize();
	TestEqual(TEXT("a field with no minimum is left at the width it was placed at"), Silent->GetWidth(), 60.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputOverflowPolicyTest,
	"DreamGUI.TextInput.Parity.TheOverflowPolicyOnlySpeaksWhileNobodyIsEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputOverflowPolicyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	/*
	 * One field on the paragraph, two things that want to write it.
	 *
	 * DreamTextLayout only looks for break opportunities under VerticalOverflow, so on an editable
	 * field the overflow type IS the line-mode switch -- and the caret's visible-window arithmetic
	 * reads the same field. That is why UMG's OverflowPolicy cannot simply be assigned through: it
	 * would fight the wrap. The rule instead is that the line mode owns the field during an edit and
	 * the policy owns it in between, with PushOverflowToVisual as the single writer.
	 *
	 * The during-an-edit half cannot be asserted here: ActivateInput spawns an actor to own an
	 * InputComponent (BindKeys dereferences GetWorld() unguarded) and asks Slate for a text input
	 * method system. What IS asserted is the half that proves the two never overwrite each other --
	 * the line mode's answer survives the policy being switched on and off over it.
	 */
	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Initialize();

	UDreamText* TextVisual = Field->TextNode != nullptr ? Cast<UDreamText>(Field->TextNode->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the value's paragraph exists"), TextVisual) ||
		!TestNotNull(TEXT("and the behaviour"), Field->InputBehaviour.Get()))
	{
		return false;
	}

	// Clip is the shipped answer, and under it the line mode is the only voice -- which is exactly
	// what every field does today.
	TestEqual(TEXT("a field ships on Clip"),
		(int32)Field->GetTextOverflowPolicy(), (int32)ETextOverflowPolicy::Clip);
	TestEqual(TEXT("so a single-line field sits on the overflow that does not wrap"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::HorizontalOverflow);

	// Idle and asked for an ellipsis: the policy speaks.
	Field->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	TestEqual(TEXT("an idle field takes the ellipsis at once"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::Ellipsis);
	TestEqual(TEXT("and the behaviour holds the same policy"),
		(int32)Field->InputBehaviour->GetOverflowPolicy(), (int32)ETextOverflowPolicy::Ellipsis);

	// The load-bearing half: switching the LINE MODE while the policy is on must not lose either
	// answer. The policy still wins while idle...
	Field->SetMultiLine(true);
	TestEqual(TEXT("going multi-line keeps the ellipsis while nobody is editing"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::Ellipsis);
	TestTrue(TEXT("and the behaviour really is in multi-line mode"), Field->InputBehaviour->GetAllowMultiLine());

	// ...and the line mode's own answer comes back the moment the policy stops speaking. This is the
	// assertion that would fail if the policy had simply overwritten the field: the wrap would be
	// gone for good.
	Field->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
	TestEqual(TEXT("dropping the policy gives the multi-line wrap back"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::VerticalOverflow);

	Field->SetMultiLine(false);
	TestEqual(TEXT("and back to one line"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::HorizontalOverflow);

	// MultilineEllipsis is UMG's third answer and means the same thing to this renderer, which has
	// one ellipsis and no per-line variant of it.
	Field->SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);
	TestEqual(TEXT("UMG's multiline ellipsis is the same ellipsis here"),
		(int32)TextVisual->GetOverflowType(), (int32)EDreamUITextOverflowType::Ellipsis);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputFlowDirectionAndFontTest,
	"DreamGUI.TextInput.Parity.TheReadingDirectionReachesEveryParagraphAndAnUnstatedFontChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputFlowDirectionAndFontTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	TDreamTestControl<UDreamTextInput> Field(Author<UDreamTextInput>());
	Field->Initialize();

	UDreamText* TextVisual = Field->TextNode != nullptr ? Cast<UDreamText>(Field->TextNode->GetVisual()) : nullptr;
	UDreamText* HintVisual = Field->PlaceholderNode != nullptr ? Cast<UDreamText>(Field->PlaceholderNode->GetVisual()) : nullptr;
	UDreamText* ErrorVisual = Field->ErrorNode != nullptr ? Cast<UDreamText>(Field->ErrorNode->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the value's paragraph exists"), TextVisual) ||
		!TestNotNull(TEXT("and the hint's"), HintVisual))
	{
		return false;
	}

	// Auto is the default and asks the bidi algorithm, which is what the field always did.
	TestEqual(TEXT("a field starts on the direction the text itself implies"),
		(int32)Field->GetTextFlowDirection(), (int32)EDreamTextFlowDirection::Auto);

	Field->SetTextFlowDirection(EDreamTextFlowDirection::RightToLeft);
	TestEqual(TEXT("the reading direction reaches the value"),
		(int32)TextVisual->GetFlowDirection(), (int32)EDreamTextFlowDirection::RightToLeft);
	// All three, because a hint that read the other way round from the value it stands in for would
	// sit at the opposite end of the box the moment the field was empty.
	TestEqual(TEXT("and the hint"),
		(int32)HintVisual->GetFlowDirection(), (int32)EDreamTextFlowDirection::RightToLeft);
	if (ErrorVisual != nullptr)
	{
		TestEqual(TEXT("and the error message"),
			(int32)ErrorVisual->GetFlowDirection(), (int32)EDreamTextFlowDirection::RightToLeft);
	}

	// And it survives a whole restyle, which is the road an editor property edit takes.
	Field->ApplyStyle();
	TestEqual(TEXT("a restyle keeps it"),
		(int32)TextVisual->GetFlowDirection(), (int32)EDreamTextFlowDirection::RightToLeft);

	/*
	 * The font, whose compatibility guarantee is entirely about NULL.
	 *
	 * A style that names no typeface must leave the paragraph on the one it already has -- the
	 * built-in tree never states a font, so every field in the project is sitting on the project
	 * default, and a style push that wrote null would take it away from all of them. Hence the
	 * guard in ApplyStyle rather than an unconditional SetFont.
	 *
	 * The non-null half needs a real font data asset (the base class is abstract and the concrete
	 * ones own atlases), so it belongs in the gallery rather than in a headless test.
	 */
	TestNull(TEXT("a field states no typeface of its own"), Field->GetFont());
	UDreamUIFontData_BaseObject* FontBefore = TextVisual->GetFont();
	Field->ApplyStyle();
	TestTrue(TEXT("and a restyle leaves the paragraph on the font it had"),
		TextVisual->GetFont() == FontBefore);
	Field->SetFont(nullptr);
	TestTrue(TEXT("as does stating null outright"), TextVisual->GetFont() == FontBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputKnobsSurviveDuplicationTest,
	"DreamGUI.TextInput.Parity.ACopiedFieldPushesTheSameSettingsIntoItsOwnBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputKnobsSurviveDuplicationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	// Authored, then copied, then built: the copy has to arrive at the same field settings from the
	// same properties, because a knob that only works on the instance somebody typed into is a knob
	// that does not survive being put in a prefab.
	TStrongObjectPtr<UDreamTextInput> Source(Author<UDreamTextInput>());
	Source->MaxLength = 12;
	Source->bRevertTextOnEscape = true;
	Source->bClearKeyboardFocusOnCommit = false;
	Source->bSelectAllTextOnCommit = true;
	Source->Justification = EDreamUITextParagraphHorizontalAlign::Right;
	// Wider than the field a style builds, or the floor has nothing to push against and the width
	// read back below would be the style's own number whether or not the setting came across.
	Source->MinimumDesiredWidth = 400.0f;
	Source->KeyboardType = EVirtualKeyboardType::Number;

	TDreamTestControl<UDreamTextInput> Copy(DuplicateObject<UDreamTextInput>(Source.Get(), GetTransientPackage()));
	if (!TestTrue(TEXT("the copy exists"), Copy.IsValid()))
	{
		return false;
	}
	Copy->Initialize();
	if (!TestNotNull(TEXT("and built its own behaviour"), Copy->InputBehaviour.Get()))
	{
		return false;
	}
	UUITextInput* Behaviour = Copy->InputBehaviour.Get();
	TestEqual(TEXT("the length cap came across"), Behaviour->GetMaxLength(), 12);
	TestTrue(TEXT("revert-on-escape came across"), Behaviour->GetRevertTextOnEscape());
	TestFalse(TEXT("keep-the-keyboard came across"), Behaviour->GetClearKeyboardFocusOnCommit());
	TestTrue(TEXT("select-all-on-commit came across"), Behaviour->GetSelectAllTextOnCommit());
	TestEqual(TEXT("the keyboard type came across"),
		(int32)Behaviour->GetKeyboardType().GetValue(), (int32)EVirtualKeyboardType::Number);
	if (UDreamText* TextVisual = Copy->TextNode != nullptr ? Cast<UDreamText>(Copy->TextNode->GetVisual()) : nullptr)
	{
		TestEqual(TEXT("and the justification reached the copy's own paragraph"),
			(int32)TextVisual->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);
	}
	TestEqual(TEXT("as did the minimum width"), Copy->GetWidth(), 400.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputBorderlessFieldKeepsTheParityKnobsTest,
	"DreamGUI.TextInput.Parity.TheBorderlessFieldInheritsEveryKnobAndOnlyDropsTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextInputBorderlessFieldKeepsTheParityKnobsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputParityTestLocal;

	// UMG splits the field into four classes on two axes; this library splits it into two classes on
	// one axis and a property on the other. So the borderless field's claim to UMG parity is entirely
	// inherited, and the thing worth pinning is that clearing the face does not clear anything else.
	TDreamTestControl<UDreamEditableText> Bare(Author<UDreamEditableText>());
	Bare->Style.ErrorColor = FColor(200, 20, 20, 255);
	Bare->Initialize();
	if (!TestNotNull(TEXT("the borderless field has the behaviour"), Bare->InputBehaviour.Get()))
	{
		return false;
	}

	Bare->SetError(FText::AsCultureInvariant(TEXT("Required")));
	TestTrue(TEXT("a borderless field can still report an error"), Bare->HasError());
	if (TestNotNull(TEXT("and has a paragraph to report it on"), Bare->ErrorNode.Get()))
	{
		TestTrue(TEXT("which wakes with the message"), Bare->ErrorNode->GetWidgetActive());
	}

	Bare->SetIsReadOnly(true);
	TestTrue(TEXT("and its read-only flag still reaches the field"), Bare->InputBehaviour->GetReadOnly());

	// The one thing it does differ by, restated here because the error paragraph is new geometry
	// inside a control whose whole job is to draw nothing behind the text.
	const FColor Clear(0, 0, 0, 0);
	TestEqual(TEXT("while the box is still not drawn in any state"),
		Bare->InputBehaviour->GetHoveredColor(), Clear);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
