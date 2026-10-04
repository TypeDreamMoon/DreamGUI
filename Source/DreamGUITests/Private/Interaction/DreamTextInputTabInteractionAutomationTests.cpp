// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * A FORM, TABBED THROUGH THE WAY A BROWSER'S IS.
 *
 * Tab in a field being edited used to be a dead key: taken as typing, typing nothing, and stepping nowhere, so a
 * form could not be filled in from the keyboard. Here each Tab commits the field it leaves -- the end of an edit
 * that navigating away has always committed (bSubmitWhenDeactivate) -- and starts the edit of the field it lands
 * on, its text selected, so the next characters replace it; Shift+Tab walks back; and the tab character the
 * keystroke also sends is typed nowhere. A multi-line field types tabs only when it was asked to, and Ctrl+Tab is
 * then the way out. While an IME composes, Tab is the IME's. The pad's confirm button, which types nothing, ends an
 * edit as Enter does.
 *
 * Every key goes through Key/Tab/ShiftTab, which under the rig's default host is DreamUIKeyRouting::RouteKey for
 * player 0 -- the order a game offers a key in: the field being edited first, then the bindings, then navigation --
 * with the '\t' character after Tab's press, as a keyboard sends it. Fields are stacked top to bottom and made in
 * that order, which is Tab's: the hierarchy's.
 */
namespace DreamTextInputTabTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D FieldSize(320.0, 40.0);
	const FVector2D ButtonSize(160.0, 40.0);

	/** A field and the listener counting what it announced. */
	struct FObservedField
	{
		UDreamTextInput* Field = nullptr;
		TStrongObjectPtr<UDreamTextInteractionListener> Listener;
	};

	FObservedField MakeObservedField(FDreamDriverRig& InRig, const FString& InName, const FString& InText,
		const FVector2D& InPosition, bool bInMultiLine = false, bool bInTabTypesTabCharacter = false)
	{
		FObservedField Made;
		Made.Listener.Reset(NewObject<UDreamTextInteractionListener>());
		Made.Field = InRig.MakeControl<UDreamTextInput>(InName, nullptr, FieldSize, InPosition);
		if (Made.Field != nullptr)
		{
			Made.Field->SetMultiLine(bInMultiLine);
			Made.Field->SetTabTypesTabCharacter(bInTabTypesTabCharacter);
			Made.Field->SetText(InText);
			Made.Field->OnTextCommitted.AddDynamic(Made.Listener.Get(), &UDreamTextInteractionListener::HandleTextCommitted);
			Made.Field->OnSubmitted.AddDynamic(Made.Listener.Get(), &UDreamTextInteractionListener::HandleSubmitted);
		}
		return Made;
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	/** Whether player 0's focus is on InWidget or on a part inside it -- a field's focus is on its face. */
	bool IsFocusOnOrIn(FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
		return Focused != nullptr && InWidget != nullptr && (Focused == InWidget || Focused->IsChildOf(InWidget));
	}

	bool HasTabCharacter(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->GetText().Contains(TEXT("\t"));
	}

	/** One Tab, as a keyboard sends it, and a frame for the step it asks for. */
	bool PressTab(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().Tab().WaitFrames(1).Perform();
	}

	bool PressShiftTab(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().ShiftTab().WaitFrames(1).Perform();
	}

	/** The field's IME context -- what an IME reads the selection from and writes through -- or null. */
	TSharedPtr<ITextInputMethodContext> ImeOf(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr
			? InField->InputBehaviour->GetTextInputMethodContextForTesting() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTabThroughAFormTest,
	"DreamGUI.TextInput.TabCommitsEachFieldAndStartsEditingTheNextAndShiftTabGoesBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTabThroughAFormTest, "DreamGUI.TextInput.TabCommitsEachFieldAndStartsEditingTheNextAndShiftTabGoesBack", "[Pointer][Text][Nav][Animated]")

/*
 * Three fields and a button. The first field is clicked into and typed over; every move after that is Tab or
 * Shift+Tab. Each Tab commits the field it leaves once, with what it holds, and the field it lands on is being
 * edited with its text selected -- typing replaces it. The last Tab lands on the button and edits nothing;
 * Shift+Tab walks back up. No field ever holds a tab character.
 */
bool FDreamTextInputTabThroughAFormTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	FObservedField Name = MakeObservedField(Rig, TEXT("Name"), TEXT("one"), FVector2D(0.0, 150.0));
	FObservedField Email = MakeObservedField(Rig, TEXT("Email"), TEXT("two"), FVector2D(0.0, 90.0));
	FObservedField City = MakeObservedField(Rig, TEXT("City"), TEXT("three"), FVector2D(0.0, 30.0));
	UDreamButton* Submit = Rig.MakeControl<UDreamButton>(TEXT("Submit"), nullptr, ButtonSize, FVector2D(0.0, -40.0));
	if (!TestTrue(TEXT("The rig, the three fields and the button came up"),
		Rig.IsUsable() && Name.Field != nullptr && Email.Field != nullptr && City.Field != nullptr && Submit != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking into the first field and typing over it completes"),
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")))->Type(TEXT("Ada")));
	if (!TestTrue(TEXT("The first field is being edited"), IsEditing(Name.Field)))
	{
		return false;
	}

	TestTrue(TEXT("The first Tab completes"), PressTab(Rig));
	TestFalse(TEXT("Tab ended the first field's edit"), IsEditing(Name.Field));
	TestEqual(TEXT("...committing it once"), Name.Listener->TextCommittedCount, 1);
	TestEqual(TEXT("...with what it held"), Name.Listener->LastCommittedText, FString(TEXT("Ada")));
	TestTrue(TEXT("The focus went on to the second field"), IsFocusOnOrIn(Rig, Email.Field));
	TestTrue(TEXT("...whose edit Tab started"), IsEditing(Email.Field));

	// Arriving by Tab selects the whole value, as a browser's field does: what is typed next replaces it.
	TestTrue(TEXT("Typing into the field Tab landed on completes"), Rig.Driver()->Sequence().Type(TEXT("Bo")).Perform());
	TestEqual(TEXT("The typing replaced the second field's text"), Email.Field->GetText(), FString(TEXT("Bo")));

	TestTrue(TEXT("The second Tab completes"), PressTab(Rig));
	TestEqual(TEXT("The second field was committed once"), Email.Listener->TextCommittedCount, 1);
	TestEqual(TEXT("...with its new value"), Email.Listener->LastCommittedText, FString(TEXT("Bo")));
	TestTrue(TEXT("The third field is being edited"), IsEditing(City.Field));

	TestTrue(TEXT("The third Tab completes"), PressTab(Rig));
	TestFalse(TEXT("The third field's edit is over"), IsEditing(City.Field));
	TestEqual(TEXT("...and committed"), City.Listener->TextCommittedCount, 1);
	TestTrue(TEXT("The button has the focus"), IsFocusOnOrIn(Rig, Submit));
	TestTrue(TEXT("...and no field is being edited"), !IsEditing(Name.Field) && !IsEditing(Email.Field) && !IsEditing(City.Field));

	TestTrue(TEXT("Shift+Tab from the button completes"), PressShiftTab(Rig));
	TestTrue(TEXT("Shift+Tab went back to the third field"), IsFocusOnOrIn(Rig, City.Field));
	TestTrue(TEXT("...and started its edit"), IsEditing(City.Field));

	TestTrue(TEXT("Shift+Tab from a field being edited completes"), PressShiftTab(Rig));
	TestFalse(TEXT("It ended the third field's edit"), IsEditing(City.Field));
	TestEqual(TEXT("...committing it a second time, for the second edit"), City.Listener->TextCommittedCount, 2);
	TestTrue(TEXT("...and went back to the second field"), IsFocusOnOrIn(Rig, Email.Field));
	TestTrue(TEXT("...whose edit it started"), IsEditing(Email.Field));

	TestFalse(TEXT("No field took a tab character"),
		HasTabCharacter(Name.Field) || HasTabCharacter(Email.Field) || HasTabCharacter(City.Field));
	TestEqual(TEXT("The first field still says what it was committed with"), Name.Field->GetText(), FString(TEXT("Ada")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMultiLineTabTest,
	"DreamGUI.TextInput.AMultiLineFieldTypesATabOnlyWhenAskedToAndCtrlTabThenLeavesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputMultiLineTabTest, "DreamGUI.TextInput.AMultiLineFieldTypesATabOnlyWhenAskedToAndCtrlTabThenLeavesIt", "[Pointer][Text][Nav][Animated]")

/*
 * Two multi-line fields and a button. The first types tabs (bTabTypesTabCharacter): Tab puts exactly one tab
 * character in -- the key and the character the keystroke sends are one tab, not two -- and the edit goes on;
 * Ctrl+Tab is what leaves it, starting the second field's edit. The second does not type tabs, as a browser's
 * textarea does not: Tab leaves it, with no tab typed, for the button.
 */
bool FDreamTextInputMultiLineTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	FObservedField Code = MakeObservedField(Rig, TEXT("Code"), FString(), FVector2D(0.0, 150.0), true, true);
	FObservedField Notes = MakeObservedField(Rig, TEXT("Notes"), FString(), FVector2D(0.0, 60.0), true, false);
	UDreamButton* Done = Rig.MakeControl<UDreamButton>(TEXT("Done"), nullptr, ButtonSize, FVector2D(0.0, -40.0));
	if (!TestTrue(TEXT("The rig, the two fields and the button came up"),
		Rig.IsUsable() && Code.Field != nullptr && Notes.Field != nullptr && Done != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking into the first field and typing completes"),
		Rig.Driver()->Find(FDreamBy::Name(TEXT("Code")))->Type(TEXT("ab")));
	TestTrue(TEXT("Tab in the field that types tabs completes"), PressTab(Rig));
	TestEqual(TEXT("It typed exactly one tab"), Code.Field->GetText(), FString(TEXT("ab\t")));
	TestTrue(TEXT("...and the edit goes on"), IsEditing(Code.Field));
	TestTrue(TEXT("...with the focus where it was"), IsFocusOnOrIn(Rig, Code.Field));
	TestEqual(TEXT("...and nothing committed"), Code.Listener->TextCommittedCount, 0);

	TestTrue(TEXT("Ctrl+Tab completes"),
		Rig.Driver()->Sequence().Key(EKeys::Tab, EDreamDriverModifierKeys::Ctrl).WaitFrames(1).Perform());
	TestFalse(TEXT("Ctrl+Tab left the field that types tabs"), IsEditing(Code.Field));
	TestEqual(TEXT("...committing it"), Code.Listener->TextCommittedCount, 1);
	TestEqual(TEXT("...with no second tab"), Code.Field->GetText(), FString(TEXT("ab\t")));
	TestTrue(TEXT("...for the next field"), IsFocusOnOrIn(Rig, Notes.Field));
	TestTrue(TEXT("...whose edit it started"), IsEditing(Notes.Field));

	TestTrue(TEXT("Typing into the second field completes"), Rig.Driver()->Sequence().Type(TEXT("cd")).Perform());
	TestTrue(TEXT("Tab in the field that does not type tabs completes"), PressTab(Rig));
	TestEqual(TEXT("It typed no tab"), Notes.Field->GetText(), FString(TEXT("cd")));
	TestFalse(TEXT("...and left the field"), IsEditing(Notes.Field));
	TestTrue(TEXT("...for the button"), IsFocusOnOrIn(Rig, Done));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputTabDuringImeCompositionTest,
	"DreamGUI.TextInput.TabDuringAnImeCompositionStaysWithTheIme",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputTabDuringImeCompositionTest, "DreamGUI.TextInput.TabDuringAnImeCompositionStaysWithTheIme", "[Pointer][Text][Nav][Animated]")

/*
 * An IME drives its working text with the very keys a player presses -- some move between candidates with Tab --
 * so while it composes, Tab neither ends the edit nor types: the field keeps the focus and the composition stays
 * open. Once the composition ends, Tab leaves as it always does.
 */
bool FDreamTextInputTabDuringImeCompositionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	FObservedField Name = MakeObservedField(Rig, TEXT("Name"), FString(), FVector2D(0.0, 120.0));
	UDreamButton* Next = Rig.MakeControl<UDreamButton>(TEXT("Next"), nullptr, ButtonSize, FVector2D(0.0, -40.0));
	if (!TestTrue(TEXT("The rig, the field and the button came up"), Rig.IsUsable() && Name.Field != nullptr && Next != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking into the field and typing completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")))->Type(TEXT("ab")));
	const TSharedPtr<ITextInputMethodContext> Ime = ImeOf(Name.Field);
	if (!TestTrue(TEXT("The field is being edited, with an IME context"), IsEditing(Name.Field) && Ime.IsValid()))
	{
		return false;
	}
	uint32 Begin = 0;
	uint32 Length = 0;
	ITextInputMethodContext::ECaretPosition CaretPosition = ITextInputMethodContext::ECaretPosition::Ending;
	Ime->GetSelectionRange(Begin, Length, CaretPosition);
	Ime->BeginComposition();
	Ime->SetTextInRange(Begin, Length, TEXT("k"));

	TestTrue(TEXT("Tab during the composition completes"), PressTab(Rig));
	TestTrue(TEXT("The edit goes on"), IsEditing(Name.Field));
	TestTrue(TEXT("...with the focus on the field"), IsFocusOnOrIn(Rig, Name.Field));
	TestTrue(TEXT("...and the composition still open"), Ime->IsComposing());
	TestEqual(TEXT("Nothing was typed or committed"), Name.Field->GetText(), FString(TEXT("abk")));
	TestEqual(TEXT("...not even a commit"), Name.Listener->TextCommittedCount, 0);

	Ime->EndComposition();
	TestTrue(TEXT("Tab after the composition completes"), PressTab(Rig));
	TestFalse(TEXT("Now Tab left the field"), IsEditing(Name.Field));
	TestTrue(TEXT("...for the button"), IsFocusOnOrIn(Rig, Next));
	TestEqual(TEXT("...committing what the composition wrote"), Name.Listener->LastCommittedText, FString(TEXT("abk")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputPadConfirmEndsEditTest,
	"DreamGUI.TextInput.ThePadsConfirmButtonEndsAnEditAsASubmit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputPadConfirmEndsEditTest, "DreamGUI.TextInput.ThePadsConfirmButtonEndsAnEditAsASubmit", "[Pointer][Text][Nav][Animated]")

/*
 * A pad types nothing, so its confirm button is the only way it has to say "done": on a field being edited it
 * submits the value and ends the edit, once -- even on a multi-line field, where Enter would add a line, and on a
 * field told to keep the keyboard after Enter. Not the press that began the edit, though: still held, it repeats
 * into the field, and the player's input has it on its books as the navigation's confirm, not the field's.
 */
bool FDreamTextInputPadConfirmEndsEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextInputTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	FObservedField Note = MakeObservedField(Rig, TEXT("Note"), FString(), FVector2D(0.0, 60.0), true, false);
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Note.Field != nullptr))
	{
		return false;
	}
	Note.Field->SetClearKeyboardFocusOnCommit(false);
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking into the field and typing completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Note")))->Type(TEXT("hi")));
	if (!TestTrue(TEXT("The field is being edited"), IsEditing(Note.Field)))
	{
		return false;
	}
	const FKey AcceptKey = DreamUIKeyRouting::GetGamepadAcceptKey();
	TestTrue(TEXT("The pad's accept key is a pad key"), AcceptKey.IsValid() && AcceptKey.IsGamepadKey());

	// The confirm that pressed the field, still down: on the player's books as the navigation's, and repeating into the
	// field -- which is what the Slate source and the field's own key bindings hand it -- it ends nothing.
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(0) : nullptr;
	if (TestNotNull(TEXT("The editing player's input"), User))
	{
		FDreamUIKeyPress HeldConfirm;
		HeldConfirm.Taker = EDreamUIKeyPressTaker::NavigationConfirm;
		User->NoteKeyPress(AcceptKey, HeldConfirm);
		Note.Field->InputBehaviour->HandleKeyInput(AcceptKey, true);
		TestTrue(TEXT("A repeat of the press that began the edit leaves it going"), IsEditing(Note.Field));
		TestEqual(TEXT("...and submits nothing"), Note.Listener->SubmittedCount, 0);
		FDreamUIKeyPress LetGo;
		User->TakeKeyPress(AcceptKey, LetGo);
	}

	TestTrue(TEXT("Pressing the pad's confirm completes"), Rig.Driver()->Sequence().Key(AcceptKey).WaitFrames(1).Perform());
	TestFalse(TEXT("It ended the edit"), IsEditing(Note.Field));
	TestEqual(TEXT("...submitting once"), Note.Listener->SubmittedCount, 1);
	TestEqual(TEXT("...committing once, not again for the end of the edit"), Note.Listener->TextCommittedCount, 1);
	TestEqual(TEXT("...with what the field held, no line added"), Note.Listener->LastCommittedText, FString(TEXT("hi")));
	TestEqual(TEXT("The value is what was typed"), Note.Field->GetText(), FString(TEXT("hi")));
	return true;
}

#endif
