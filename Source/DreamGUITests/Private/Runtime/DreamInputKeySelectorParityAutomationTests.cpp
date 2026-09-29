// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamInputKeySelector.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The key binder's answer to a key it will not bind, which is two different answers.
 *
 * A selector refuses a key for three reasons and they are not interchangeable. An escape key is
 * TAKEN and not bound (the caller must not also close the screen with it). A lone modifier is TAKEN
 * and not bound (it is half of a chord still being pressed). A gamepad key on a keyboard-only
 * selector is NOT taken at all -- it was meant for something else, and swallowing it would leave a
 * controller doing nothing whenever a selector is armed. NotifyChordPressed's return value is that
 * distinction, which is why every assertion below reads it.
 *
 * Headless: there is no world, so arming does not spawn the capture agent (BeginKeyCapture says so
 * itself and returns). NotifyKeyPressed is the documented entry point either way, so nothing here
 * is a stand-in for something a real session would do differently.
 */
namespace DreamInputKeySelectorParityTestLocal
{
	template<class T>
	T* Author(float InWidth = 200.0f, float InHeight = 34.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}

	FString LabelOf(const UDreamInputKeySelector* InSelector)
	{
		const UDreamText* LabelVisual = InSelector != nullptr && InSelector->LabelNode != nullptr
			? Cast<UDreamText>(InSelector->LabelNode->GetVisual())
			: nullptr;
		return LabelVisual != nullptr ? LabelVisual->GetText().ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorGamepadKeysTest,
	"DreamGUI.InputKeySelector.Parity.AKeyboardOnlySelectorLetsAPadKeyPastInsteadOfSwallowingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeySelectorGamepadKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorParityTestLocal;

	TDreamTestControl<UDreamInputKeySelector> Selector(Author<UDreamInputKeySelector>());
	Selector->Initialize();

	// The shipped state: pad keys bind, which is what the capture agent already did.
	TestTrue(TEXT("pad keys are bindable by default"), Selector->AllowGamepadKeys());
	Selector->BeginListening();
	TestTrue(TEXT("a pad key is taken"), Selector->NotifyKeyPressed(EKeys::Gamepad_FaceButton_Bottom));
	TestEqual(TEXT("and bound"), Selector->GetSelectedKey().ToString(), EKeys::Gamepad_FaceButton_Bottom.ToString());
	TestFalse(TEXT("and binding disarms"), Selector->GetIsSelectingKey());

	// Switched off, the same key is neither bound nor consumed -- and the selector is still armed,
	// waiting for the keyboard key it was told to accept.
	Selector->SetAllowGamepadKeys(false);
	Selector->SetSelectedKey(EKeys::G);
	Selector->BeginListening();
	TestFalse(TEXT("a pad key is not taken by a keyboard-only selector"),
		Selector->NotifyKeyPressed(EKeys::Gamepad_FaceButton_Bottom));
	TestEqual(TEXT("so the binding is untouched"), Selector->GetSelectedKey().ToString(), EKeys::G.ToString());
	TestTrue(TEXT("and the selector is still waiting"), Selector->GetIsSelectingKey());

	// The way out has to survive the filter, or a player rebinding with a pad in their hands is
	// stuck: the escape list is checked FIRST and the pad's B is in it by default.
	TestTrue(TEXT("the pad's escape key is still taken"),
		Selector->NotifyKeyPressed(EKeys::Gamepad_FaceButton_Right));
	TestFalse(TEXT("and stands the selector down"), Selector->GetIsSelectingKey());
	TestEqual(TEXT("without binding anything"), Selector->GetSelectedKey().ToString(), EKeys::G.ToString());

	// A keyboard key still binds, which is the whole point of the keyboard-only mode.
	Selector->BeginListening();
	TestTrue(TEXT("a keyboard key is still taken"), Selector->NotifyKeyPressed(EKeys::H));
	TestEqual(TEXT("and bound"), Selector->GetSelectedKey().ToString(), EKeys::H.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorLabelSettersTest,
	"DreamGUI.InputKeySelector.Parity.TheWordsOnTheFaceFollowTheSetterThatWroteThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeySelectorLabelSettersTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorParityTestLocal;

	TDreamTestControl<UDreamInputKeySelector> Selector(Author<UDreamInputKeySelector>());
	Selector->Initialize();
	if (!TestNotNull(TEXT("the selector built a label"), Selector->LabelNode.Get()))
	{
		return false;
	}

	// Unbound and not armed: the "no key" words, which were authored-only before this.
	Selector->SetNoKeySpecifiedText(FText::AsCultureInvariant(TEXT("Not set")));
	TestEqual(TEXT("the unbound words are re-labelled at once"), LabelOf(Selector.Get()), FString(TEXT("Not set")));

	// Armed: the prompt, and the same argument.
	Selector->BeginListening();
	Selector->SetKeySelectionText(FText::AsCultureInvariant(TEXT("Waiting...")));
	TestEqual(TEXT("the prompt is re-labelled while it is showing"),
		LabelOf(Selector.Get()), FString(TEXT("Waiting...")));

	// Writing the OTHER one while it is not showing must not disturb the face.
	Selector->SetNoKeySpecifiedText(FText::AsCultureInvariant(TEXT("Unbound key")));
	TestEqual(TEXT("and writing the hidden one leaves the prompt alone"),
		LabelOf(Selector.Get()), FString(TEXT("Waiting...")));

	Selector->CancelListening();
	TestEqual(TEXT("standing down shows the new unbound words"),
		LabelOf(Selector.Get()), FString(TEXT("Unbound key")));

	// And a bound key spells itself, which is the engine's own display name rather than a table this
	// control keeps -- so the words are the ones the rest of the game shows.
	Selector->SetSelectedKey(EKeys::SpaceBar);
	TestEqual(TEXT("a bound key spells itself"),
		LabelOf(Selector.Get()), EKeys::SpaceBar.GetDisplayName().ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorRuntimeKnobsTest,
	"DreamGUI.InputKeySelector.Parity.EveryAuthoredKnobCanBeWrittenAfterTheSelectorIsBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeySelectorRuntimeKnobsTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorParityTestLocal;

	TDreamTestControl<UDreamInputKeySelector> Selector(Author<UDreamInputKeySelector>());
	Selector->Initialize();

	// The escape list, which decides whether there is a way out at all.
	Selector->SetEscapeKeys({ EKeys::Tab });
	TestEqual(TEXT("the escape list is the one that was written"), Selector->GetEscapeKeys().Num(), 1);
	Selector->BeginListening();
	TestTrue(TEXT("the new escape key stands the selector down"), Selector->NotifyKeyPressed(EKeys::Tab));
	TestFalse(TEXT("-- without binding"), Selector->GetIsSelectingKey());
	TestFalse(TEXT("-- so nothing was bound"), Selector->GetSelectedKey().IsValid());

	// With escaping switched off entirely, the former escape key binds like any other -- which is
	// the state the header documents as a trap, and therefore worth pinning rather than assuming.
	Selector->SetEscapeCancels(false);
	Selector->BeginListening();
	TestTrue(TEXT("with escaping off the key is taken"), Selector->NotifyKeyPressed(EKeys::Tab));
	TestEqual(TEXT("and bound like any other"), Selector->GetSelectedKey().ToString(), EKeys::Tab.ToString());

	// Modifiers: with them allowed, one pressed alone is consumed and the selector stays armed.
	Selector->SetEscapeCancels(true);
	Selector->SetAllowModifierKeys(true);
	Selector->BeginListening();
	TestTrue(TEXT("a lone modifier is consumed"), Selector->NotifyKeyPressed(EKeys::LeftControl));
	TestTrue(TEXT("and the selector keeps waiting for the key it modifies"), Selector->GetIsSelectingKey());
	// With them off it binds itself like any key.
	Selector->SetAllowModifierKeys(false);
	TestTrue(TEXT("with modifiers off a modifier is taken"), Selector->NotifyKeyPressed(EKeys::LeftControl));
	TestEqual(TEXT("and bound"), Selector->GetSelectedKey().ToString(), EKeys::LeftControl.ToString());

	// The label's visibility, for a selector drawn as a glyph. The FACE keeps its own, because
	// clicking it is the documented way out of the armed state.
	Selector->SetTextBlockVisibility(EDreamWidgetVisibility::Collapsed);
	if (TestNotNull(TEXT("the label exists"), Selector->LabelNode.Get()))
	{
		TestEqual(TEXT("the label took the visibility"),
			(int32)Selector->LabelNode->GetVisibility(), (int32)EDreamWidgetVisibility::Collapsed);
	}
	if (TestNotNull(TEXT("the face exists"), Selector->FaceNode.Get()))
	{
		TestEqual(TEXT("and the face kept its own"),
			(int32)Selector->FaceNode->GetVisibility(), (int32)EDreamWidgetVisibility::Visible);
	}

	// Turning the capture off while armed has to release it there and then: an InputComponent left
	// at the top of the stack with nothing in this class that would ever destroy it is the trap the
	// armed-state comment is about. With no world there is no agent to begin with, so what this
	// pins is that the two roads agree about the state rather than about the actor.
	Selector->BeginListening();
	Selector->SetCaptureKeysWhileListening(false);
	TestFalse(TEXT("the capture flag is off"), Selector->GetCaptureKeysWhileListening());
	TestTrue(TEXT("while the selector is still armed"), Selector->GetIsSelectingKey());
	Selector->CancelListening();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeySelectorStyleSetterTest,
	"DreamGUI.InputKeySelector.Parity.AStyleWrittenAtRuntimeRepaintsTheFaceInWhicheverStateItIsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeySelectorStyleSetterTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputKeySelectorParityTestLocal;

	TDreamTestControl<UDreamInputKeySelector> Selector(Author<UDreamInputKeySelector>());
	Selector->Initialize();
	if (!TestNotNull(TEXT("the button behaviour exists"), Selector->ButtonBehaviour.Get()))
	{
		return false;
	}

	FDreamInputKeySelectorStyle Restyled = Selector->GetStyle();
	Restyled.Normal = FColor(11, 22, 33, 255);
	Restyled.Listening = FColor(44, 55, 66, 255);
	Selector->SetStyle(Restyled);
	TestEqual(TEXT("the resting colour reached the face"),
		Selector->ButtonBehaviour->GetNormalColor(), FColor(11, 22, 33, 255));

	// Armed, ALL THREE pointer states flatten onto the listening colour -- the pointer is by
	// definition still on the button the player just clicked, so a colour written into Normal alone
	// would appear only once the mouse moved away.
	Selector->BeginListening();
	TestEqual(TEXT("arming paints the resting state with the listening colour"),
		Selector->ButtonBehaviour->GetNormalColor(), FColor(44, 55, 66, 255));
	TestEqual(TEXT("and the hovered one"),
		Selector->ButtonBehaviour->GetHoveredColor(), FColor(44, 55, 66, 255));
	TestEqual(TEXT("and the pressed one"),
		Selector->ButtonBehaviour->GetPressedColor(), FColor(44, 55, 66, 255));

	// A style written WHILE armed still has to come out armed, which is why SetStyle is the whole
	// push rather than a field copy: the face's colours are picked from the style and the state
	// together, and only one function knows how.
	Restyled.Listening = FColor(77, 88, 99, 255);
	Selector->SetStyle(Restyled);
	TestEqual(TEXT("a restyle while armed keeps the armed colour"),
		Selector->ButtonBehaviour->GetNormalColor(), FColor(77, 88, 99, 255));

	Selector->CancelListening();
	TestEqual(TEXT("and standing down goes back to the resting colour"),
		Selector->ButtonBehaviour->GetNormalColor(), FColor(11, 22, 33, 255));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
