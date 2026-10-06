// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * THE CLIPBOARD, AS UMG'S EDITABLE TEXT USES IT.
 *
 * FSlateEditableTextLayout maps FGenericCommands Cut, Copy and Paste on its command list (SlateEditableTextLayout.cpp,
 * the MapAction calls of its constructor) and offers a key to them before anything else (HandleKeyDown,
 * ProcessCommandBindings): Ctrl+C puts the selection on the clipboard (CopySelectedTextToClipboard), Ctrl+X puts it there
 * and takes it out of the text (CutSelectedTextToClipboard), Ctrl+V types what the clipboard holds at the caret, over the
 * selection (PasteTextFromClipboard). The context menu it builds (BuildDefaultContextMenu) lists the same three commands,
 * and choosing one runs it on the field the menu was opened on -- the field still counts as focused while its menu is up.
 *
 * Every key here goes in through the rig's key road (DreamUIKeyRouting::RouteKey), the chord carried with it. The
 * clipboard is the desktop's own, so each test puts back what it found there.
 */
namespace DreamTextClipboardInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The desktop's clipboard as the test found it, put back when the test is done with it. */
	struct FClipboardKeeper
	{
		FString Found;

		FClipboardKeeper()
		{
			FPlatformApplicationMisc::ClipboardPaste(Found);
		}
		~FClipboardKeeper()
		{
			FPlatformApplicationMisc::ClipboardCopy(*Found);
		}
	};

	FString ReadClipboard()
	{
		FString Contents;
		FPlatformApplicationMisc::ClipboardPaste(Contents);
		return Contents;
	}

	/** A field on the rig, clicked into and typed into, with the caret left after the last character. */
	UDreamTextInput* MakeTypedField(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InText)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(360.0, 40.0));
		if (Field == nullptr || Field->InputBehaviour == nullptr)
		{
			return nullptr;
		}
		InRig.PumpFrames(1);
		InTest.TestTrue(TEXT("Clicking into the field and typing completes"), InRig.Driver()->Find(FDreamBy::Name(TEXT("Notes")))->Type(InText));
		InTest.TestEqual(TEXT("The field holds what was typed"), Field->GetText(), InText);
		return Field;
	}

	/** One key through the rig's key road, with InModifiers held. */
	bool PressKey(FDreamDriverRig& InRig, const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None)
	{
		return InRig.Driver()->Sequence().Key(InKey, InModifiers).Perform();
	}

	/** Shift+InKey InTimes times: the selection grown one character a press. */
	bool ShiftArrow(FDreamDriverRig& InRig, const FKey& InKey, int32 InTimes)
	{
		FDreamDriverSequence Steps = InRig.Driver()->Sequence();
		for (int32 Press = 0; Press < InTimes; ++Press)
		{
			Steps.Key(InKey, EDreamDriverModifierKeys::Shift);
		}
		return Steps.Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCopyPasteKeysTest,
	"DreamGUI.TextInput.CtrlCCopiesTheSelectionAndCtrlVPastesItWhereTheCaretIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputCopyPasteKeysTest, "DreamGUI.TextInput.CtrlCCopiesTheSelectionAndCtrlVPastesItWhereTheCaretIs", "[Pointer][Text][Animated]")

/*
 * "hello world", Home, then Shift+Right five times: "hello" selected. Ctrl+C leaves the text alone and puts exactly the
 * selection on the clipboard, over what was there; End, then Ctrl+V types it in at the caret.
 */
bool FDreamTextInputCopyPasteKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextClipboardInteractionTestLocal;
	FClipboardKeeper Keeper;
	FPlatformApplicationMisc::ClipboardCopy(TEXT("something copied before"));
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? MakeTypedField(*this, Rig, TEXT("hello world")) : nullptr;
	if (!TestNotNull(TEXT("The rig and a field being edited came up"), Field))
	{
		return false;
	}

	TestTrue(TEXT("Home completes"), PressKey(Rig, EKeys::Home));
	TestTrue(TEXT("Shift+Right five times completes"), ShiftArrow(Rig, EKeys::Right, 5));
	TestTrue(TEXT("...and selects something"), Field->IsAnyTextSelected());
	TestTrue(TEXT("Ctrl+C completes"), PressKey(Rig, EKeys::C, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl+C put the selection on the clipboard"), ReadClipboard(), FString(TEXT("hello")));
	TestEqual(TEXT("...and left the text as it was"), Field->GetText(), FString(TEXT("hello world")));

	TestTrue(TEXT("End completes"), PressKey(Rig, EKeys::End));
	TestTrue(TEXT("Ctrl+V completes"), PressKey(Rig, EKeys::V, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl+V typed the clipboard in at the caret"), Field->GetText(), FString(TEXT("hello worldhello")));
	TestTrue(TEXT("The field is still being edited"), Field->InputBehaviour->IsInputActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputCutKeyTest,
	"DreamGUI.TextInput.CtrlXCutsTheSelectionOutOfTheFieldAndOntoTheClipboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputCutKeyTest, "DreamGUI.TextInput.CtrlXCutsTheSelectionOutOfTheFieldAndOntoTheClipboard", "[Pointer][Text][Animated]")

/*
 * "hello world" with "world" selected from the end (Shift+Left five times): Ctrl+X takes it out of the text and puts it
 * on the clipboard, and Ctrl+V straight after puts it back where it came from.
 */
bool FDreamTextInputCutKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextClipboardInteractionTestLocal;
	FClipboardKeeper Keeper;
	FPlatformApplicationMisc::ClipboardCopy(TEXT("something copied before"));
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? MakeTypedField(*this, Rig, TEXT("hello world")) : nullptr;
	if (!TestNotNull(TEXT("The rig and a field being edited came up"), Field))
	{
		return false;
	}

	TestTrue(TEXT("Shift+Left five times from the end completes"), ShiftArrow(Rig, EKeys::Left, 5));
	TestTrue(TEXT("Ctrl+X completes"), PressKey(Rig, EKeys::X, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl+X took the selection out of the text"), Field->GetText(), FString(TEXT("hello ")));
	TestEqual(TEXT("...and put it on the clipboard"), ReadClipboard(), FString(TEXT("world")));
	TestFalse(TEXT("...leaving nothing selected"), Field->IsAnyTextSelected());

	TestTrue(TEXT("Ctrl+V completes"), PressKey(Rig, EKeys::V, EDreamDriverModifierKeys::Ctrl));
	TestEqual(TEXT("Ctrl+V put it back where the caret was left"), Field->GetText(), FString(TEXT("hello world")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextInputMenuCopyPasteTest,
	"DreamGUI.TextInput.CopyAndPasteChosenFromTheEditMenuCopyTheSelectionAndPasteItAtTheCaret",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTextInputMenuCopyPasteTest, "DreamGUI.TextInput.CopyAndPasteChosenFromTheEditMenuCopyTheSelectionAndPasteItAtTheCaret", "[Pointer][Text][Animated]")

/*
 * The menu's entries are the field's commands. "hello" selected, the menu opened with Shift+F10 (the field keeps the
 * selection the keyboard made), and its Copy entry clicked: the selection is on the clipboard, and the field is still the
 * one being edited, as a Slate text field counts as focused while its menu is up. End, the menu again, its Paste entry:
 * the clipboard is typed in at the caret.
 */
bool FDreamTextInputMenuCopyPasteTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextClipboardInteractionTestLocal;
	FClipboardKeeper Keeper;
	FPlatformApplicationMisc::ClipboardCopy(TEXT("something copied before"));
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTextInput* Field = Rig.IsUsable() ? MakeTypedField(*this, Rig, TEXT("hello world")) : nullptr;
	UUITextInput* Behaviour = Field != nullptr ? Field->InputBehaviour.Get() : nullptr;
	if (!TestNotNull(TEXT("The rig and a field being edited came up"), Behaviour))
	{
		return false;
	}

	TestTrue(TEXT("Home and Shift+Right five times complete"), PressKey(Rig, EKeys::Home) && ShiftArrow(Rig, EKeys::Right, 5));
	TestTrue(TEXT("Shift+F10 completes"), PressKey(Rig, EKeys::F10, EDreamDriverModifierKeys::Shift));
	if (!TestTrue(TEXT("Shift+F10 opened the edit menu"), Behaviour->IsContextMenuOpen()))
	{
		return false;
	}
	// The pointer on Copy first, then the press held a frame, so what the press does to the field and its menu is seen
	// before the release chooses the entry.
	FDreamElementRef CopyEntry = Rig.Driver()->Find(FDreamBy::Name(TEXT("ContextMenuEntry_Copy")));
	TestTrue(TEXT("The pointer can rest on the menu's Copy"), CopyEntry->Hover());
	TestTrue(TEXT("...and is over it, not over the sheet behind the menu"), CopyEntry->IsHovered());
	bool bMenuUpWhileHeld = false;
	bool bEditingWhileHeld = false;
	bool bCopyPressedWhileHeld = false;
	TestTrue(TEXT("Clicking the menu's Copy completes"), Rig.Driver()->Sequence()
		.Press()
		.Then([&](FDreamDriverContext&)
		{
			bMenuUpWhileHeld = Behaviour->IsContextMenuOpen();
			bEditingWhileHeld = Behaviour->IsInputActive();
			// The entry's button, which the press on its label went to.
			const UDreamWidget* Entry = CopyEntry->GetWidget();
			const UUIButton* EntryButton = Entry != nullptr ? Entry->GetComponent<UUIButton>() : nullptr;
			bCopyPressedWhileHeld = EntryButton != nullptr && EntryButton->GetCurrentSelectionState() == EUISelectableSelectionState::Pressed;
		})
		.Release()
		.Perform());
	TestTrue(TEXT("While Copy was held the menu was still up"), bMenuUpWhileHeld);
	TestTrue(TEXT("...the field still being edited, as a Slate field counts as focused while its menu is up"), bEditingWhileHeld);
	TestTrue(TEXT("...and it was Copy that the press went down on"), bCopyPressedWhileHeld);
	TestEqual(TEXT("Copy put the selection on the clipboard"), ReadClipboard(), FString(TEXT("hello")));
	TestFalse(TEXT("...and the menu closed"), Behaviour->IsContextMenuOpen());
	TestTrue(TEXT("...with the field still being edited"), Behaviour->IsInputActive());

	TestTrue(TEXT("End completes"), PressKey(Rig, EKeys::End));
	TestTrue(TEXT("Shift+F10 again completes"), PressKey(Rig, EKeys::F10, EDreamDriverModifierKeys::Shift));
	if (!TestTrue(TEXT("Shift+F10 opened the edit menu again"), Behaviour->IsContextMenuOpen()))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the menu's Paste completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("ContextMenuEntry_Paste")))->Click());
	TestEqual(TEXT("Paste typed the clipboard in at the caret"), Field->GetText(), FString(TEXT("hello worldhello")));
	return true;
}

#endif
