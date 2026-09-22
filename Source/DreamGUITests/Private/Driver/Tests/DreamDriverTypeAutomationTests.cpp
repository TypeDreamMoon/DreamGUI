// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * WHERE A KEYSTROKE GOES.
 *
 * A keyboard has no aim. It types into whatever has focus, and a driver that typed into a field it
 * was handed would be testing a different thing from the one a player does: a field the player
 * never clicked, or clicked and then clicked away from, must not receive the letters. So Type looks
 * the target up the way the runtime does -- the event system's selection, carrying a text field
 * that is being edited -- and fails, saying why, when there is none.
 *
 * These pin the driver's half: that characters and keys reach the focused field through the
 * field's own host entries (HandleCharacterInput for characters, HandleKeyInput for keys), and that
 * nothing reaches anything when nothing is focused. What each key then DOES is the field's business,
 * and the TextInput interaction tests own it.
 */
namespace DreamDriverTypeTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamTextInput* MakeField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0));
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
		}
		return Field;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTypeCharactersTest,
	"DreamGUI.Driver.Type.TypingIntoAClickedFieldPutsEveryCharacterInIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTypeCharactersTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTypeTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The element's Type clicks first, because that is how a player gives a field the keyboard.
	TestTrue(TEXT("Typing into the field completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")))->Type(TEXT("abc")));
	TestEqual(TEXT("Every character landed in the field"), Field->GetText(), FString(TEXT("abc")));
	TestEqual(TEXT("And each one was announced as it arrived"), Listener->TextChangedCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTypeBackspaceTest,
	"DreamGUI.Driver.Type.BackspaceReachesTheFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTypeBackspaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTypeTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef NameField = Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")));
	NameField->Type(TEXT("abc"));
	// A key rather than a character: Backspace lives on the field's key road, which in a game is bound
	// on a player controller's input stack and here arrives through the field's HandleKeyInput.
	TestTrue(TEXT("Pressing Backspace completes"), NameField->Type(EKeys::BackSpace));
	TestEqual(TEXT("Backspace took the last character off"), Field->GetText(), FString(TEXT("ab")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTypeEnterTest,
	"DreamGUI.Driver.Type.EnterCommitsTheFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTypeEnterTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTypeTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	FDreamElementRef NameField = Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")));
	NameField->Type(TEXT("abc"));
	TestEqual(TEXT("Nothing is committed while the player is still typing"), Listener->TextCommittedCount, 0);
	TestTrue(TEXT("Pressing Enter completes"), NameField->Type(EKeys::Enter));
	TestEqual(TEXT("Enter committed the field once"), Listener->TextCommittedCount, 1);
	TestEqual(TEXT("With what was typed"), Listener->LastCommittedText, FString(TEXT("abc")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTypeWithoutFocusTest,
	"DreamGUI.Driver.Type.TypingWithNothingFocusedFailsAndSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTypeWithoutFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverTypeTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	UDreamTextInput* Field = MakeField(Rig, Listener.Get());
	if (!TestTrue(TEXT("The rig and the field came up"), Rig.IsUsable() && Field != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The field exists and is on screen, but nobody clicked it. A keyboard in that state types
	// into nothing, and a driver that quietly picked the only field in sight would be hiding the
	// exact bug -- focus not taken -- that a test of focus is looking for.
	AddExpectedErrorPlain(TEXT("there is no text field to type into"));
	const bool bTyped = Rig.Driver()->Sequence().Type(TEXT("x")).Perform();

	TestFalse(TEXT("Typing with nothing focused does not report success"), bTyped);
	TestEqual(TEXT("And the field was left alone"), Field->GetText(), FString());
	TestEqual(TEXT("Without being told anything changed"), Listener->TextChangedCount, 0);

	return true;
}

#endif
