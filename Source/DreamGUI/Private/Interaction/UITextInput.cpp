// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UITextInput.h"
#include "DreamGUI.h"
#include "Core/Components/DreamText.h"
#include "InputCoreTypes.h"
#include "Event/DreamEventSystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/DreamUISpriteData.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Extensions/DreamGameViewportClient.h"
#include "Interaction/UIButton.h"
#include "Misc/Char.h"
#include "SceneView.h"
#include "Widgets/SViewport.h"



namespace DreamTextInputLocal
{
	/**
	 * True while the text visual has actually been laid out, which is the only state in which the
	 * caret-index <-> source-offset mapping can answer.
	 *
	 * UDreamText::UpdateCacheTextGeometry gives up without a render canvas -- the state of every
	 * text in a headless test and in a Blueprint authoring tree -- and leaves no lines behind, and
	 * the mapping functions read the last line of that empty array. Every caller here has a sane
	 * answer for that state (a caret index IS the source offset until a surrogate pair shows up),
	 * so they ask this first instead of walking into it.
	 */
	static bool CanMapCaretIndices(const TWeakObjectPtr<UDreamText>& InTextVisual)
	{
		if (!InTextVisual.IsValid())return false;
		const UDreamWidget* Widget = InTextVisual->GetWidget();
		return Widget != nullptr && Widget->GetRenderCanvas() != nullptr;
	}
}

UDreamTextInputCustomValidation::UDreamTextInputCustomValidation()
{
	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
}
bool UDreamTextInputCustomValidation::OnValidateInput(UUITextInput* InTextInput, const FString& InString, int InIndexOfInsertedChar)
{
	if (bCanExecuteBlueprintEvent)
	{
		return ReceiveOnValidateInput(InTextInput, InString, InIndexOfInsertedChar);
	}
	return false;
}

void UUITextInput::Awake()
{
	Super::Awake();
	
	if (!VirtualKeyboardEntry.IsValid())
	{
		VirtualKeyboardEntry = FVirtualKeyboardEntry::Create(this);
	}
	if (!TextInputMethodContext.IsValid())
	{
		TextInputMethodContext = FTextInputMethodContext::Create(this);
	}
	// The tick's only job is the caret blink, which only exists while input is active -- so the
	// tick does too. Activate/deactivate toggle it; an inactive input costs the frame nothing.
	this->SetCanExecuteTick(false);
}
void UUITextInput::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bInputActive)
	{
		//blink caret
		if (CaretWidget.IsValid())
		{
			ElapseTime += DeltaTime;
			if (NextCaretBlinkTime < ElapseTime)
			{
				CaretWidget->SetRenderOpacity(1.0f - CaretWidget->GetRenderOpacity());
				NextCaretBlinkTime = ElapseTime + CaretBlinkRate;
			}
		}
		// The long press maturing. Timed here rather than with a timer because the tick already runs
		// for exactly as long as the edit does, and a timer would outlive a field torn down mid-hold.
		if (bPointerHeldForContextMenu
			&& (FPlatformTime::Seconds() - PointerHeldStartTime) >= (double)ContextMenuLongPressTime)
		{
			bPointerHeldForContextMenu = false;
			ShowContextMenu();
		}
	}
}

void UUITextInput::OnUnregister()
{
	// Before Super, while the field's parts are still registered (DestroyWidget unregisters a subtree
	// top-down, so the caret, the text and the placeholder under this node go after it) and before the
	// selectable leaves the manager. Without events, for OnDestroy's reason: a field being torn down is
	// not a player committing a value. Nothing to do for a field that is not being edited.
	DeactivateInput(false);
	Super::OnUnregister();
}

void UUITextInput::OnDestroy()
{
	Super::OnDestroy();
	//no events on the way out: a field being torn down is not a player committing a value, and the
	//deactivate road now submits when an edit ends without an Enter
	DeactivateInput(false);
	//the menu is parented to the screen root, not to this field, so it does not go with the field
	HideContextMenu();
	if (TextInputMethodContext.IsValid())
	{
		TextInputMethodContext->Dispose();
	}
}

#if WITH_EDITOR
bool UUITextInput::CanEditChange(const FProperty* InProperty)const
{
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UUITextInput, PasswordChar))
	{
		if (InputType != EUITextInputType::Password
			&& DisplayType != EUITextInputDisplayType::Password)
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
void UUITextInput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto propertyName = Property->GetFName();
		if (propertyName == GET_MEMBER_NAME_CHECKED(UUITextInput, PasswordChar))
		{
			if (PasswordChar.Len() > 1)
			{
				auto firstChar = PasswordChar[0];
				PasswordChar.Empty();
				PasswordChar.AppendChar(firstChar);
			}
			else if (PasswordChar.Len() == 0)
			{
				PasswordChar.AppendChar('*');
			}
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(UUITextInput, MaxLength)
			|| propertyName == GET_MEMBER_NAME_CHECKED(UUITextInput, InputType)
			|| propertyName == GET_MEMBER_NAME_CHECKED(UUITextInput, Text))
		{
			//the rules changed under the authored text; apply them to it instead of leaving the
			//field holding a value its own type says cannot exist
			MaxLength = FMath::Max(0, MaxLength);
			RevalidateText();
		}
		else if (propertyName == GET_MEMBER_NAME_CHECKED(UUITextInput, MultiLineSubmitFunctionKeys))
		{
			for (int i = 0; i < MultiLineSubmitFunctionKeys.Num(); i++)
			{
				if (MultiLineSubmitFunctionKeys[i] == EKeys::Enter)
				{
					MultiLineSubmitFunctionKeys[i] = EKeys::LeftControl;
				}
			}
			if (MultiLineSubmitFunctionKeys.Num() == 1
				&& MultiLineSubmitFunctionKeys[0] == FKey(NAME_None)
				)
			{
				MultiLineSubmitFunctionKeys[0] = EKeys::LeftControl;//first one set to LeftControl as default
			}
		}
	}
	if (TextVisual != nullptr)
	{
		PushOverflowToVisual();
		if (!TextVisual->GetText().IsCultureInvariant())
		{
			TextVisual->SetText(FText::AsCultureInvariant(Text));
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Input text should not change by culture, set it to not localizable."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	UpdateUITextComponent();
	UpdatePlaceHolderComponent();
}
#endif
bool UUITextInput::CheckPlayerController()
{
	if (PlayerController != nullptr)return true;
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return false;
	PlayerController = World->GetFirstPlayerController();
	if (PlayerController != nullptr)return true;
	return false;
}
void UUITextInput::AnyKeyPressed(FKey Key)
{
	if (bInputActive == false)return;
	// While a composition is open the IME owns the text: it edits through SetTextInRange, and the same raw
	// key presses that drive it are also delivered here, because the bound InputComponent reads key state
	// straight from the message pump and TSF never consumed them. Acting on both is what types every
	// character twice; it is also what lets an arrow key drag the caret out from under the candidate window
	// mid-composition.
	if (TextInputMethodContext.IsValid() && TextInputMethodContext->IsComposing())return;
	if (!CheckPlayerController())return;
	if (TextVisual == nullptr)return;

	// The bound road's held keys come from the player's input state; what a key then does to the edit
	// is ProcessKeyPressed, shared with HandleKeyInput so the two roads cannot drift apart.
	UPlayerInput* const HeldKeys = PlayerController->PlayerInput;
	ProcessKeyPressed(Key, HeldKeys->IsCtrlPressed(), HeldKeys->IsShiftPressed(), HeldKeys->IsAltPressed(),
		[HeldKeys](const FKey& InHeldKey) { return HeldKeys->IsPressed(InHeldKey); });
}

bool UUITextInput::HandleKeyInput(const FKey& InKey, bool bInPressed)
{
	return HandleKeyInput(InKey, bInPressed, FModifierKeysState());
}

bool UUITextInput::HandleKeyInput(const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifierKeys)
{
	// The bound road listens for press and repeat only, and never binds a key in IgnoreKeys.
	if (!bInPressed)return false;
	if (IgnoreKeys.Contains(InKey))return false;
	// AnyKeyPressed's own gates, minus the player controller: standing in for what the controller
	// would have supplied is the whole of what this entry is for.
	if (bInputActive == false)return false;
	if (TextInputMethodContext.IsValid() && TextInputMethodContext->IsComposing())return false;
	if (TextVisual == nullptr)return false;

	ProcessKeyPressed(InKey, InModifierKeys.IsControlDown(), InModifierKeys.IsShiftDown(), InModifierKeys.IsAltDown(),
		[&InModifierKeys](const FKey& InHeldKey)
		{
			// A host's key event carries the modifier state and nothing more, so a modifier is the only
			// kind of key that can be reported as held alongside the one being delivered.
			if (InHeldKey == EKeys::LeftShift) return InModifierKeys.IsLeftShiftDown();
			if (InHeldKey == EKeys::RightShift) return InModifierKeys.IsRightShiftDown();
			if (InHeldKey == EKeys::LeftControl) return InModifierKeys.IsLeftControlDown();
			if (InHeldKey == EKeys::RightControl) return InModifierKeys.IsRightControlDown();
			if (InHeldKey == EKeys::LeftAlt) return InModifierKeys.IsLeftAltDown();
			if (InHeldKey == EKeys::RightAlt) return InModifierKeys.IsRightAltDown();
			if (InHeldKey == EKeys::LeftCommand) return InModifierKeys.IsLeftCommandDown();
			if (InHeldKey == EKeys::RightCommand) return InModifierKeys.IsRightCommandDown();
			return false;
		});
	return true;
}

void UUITextInput::ProcessKeyPressed(const FKey& InKey, bool bInCtrl, bool bInShift, bool bInAlt, TFunctionRef<bool(const FKey&)> InIsKeyHeld)
{
	// The names the key table below has always used.
	const FKey& Key = InKey;
	TCHAR inputChar = 127;
	bool ctrl = bInCtrl;
	bool shift = bInShift;
	bool alt = bInAlt;
	bool ctrlOnly = ctrl && !alt && !shift;
	bool shiftOnly = !ctrl && !alt && shift;

	//Function key
	if (Key == EKeys::BackSpace)
	{
		BackSpace();
		return;
	}
	else if (Key == EKeys::Delete)
	{
		ForwardSpace();
		return;
	}
	else if (Key == EKeys::Home)
	{
		MoveCaret(4, shiftOnly);
		return;
	}
	else if (Key == EKeys::End)
	{
		MoveCaret(5, shiftOnly);
		return;
	}
	//Select all
	else if (Key == EKeys::A)
	{
		if (ctrlOnly)
		{
			SelectAll();
			return;
		}
	}
	//Copy
	else if (Key == EKeys::C)
	{
		if (ctrlOnly)
		{
			Copy();
			return;
		}
	}
	//Paste
	else if (Key == EKeys::V)
	{
		if (ctrlOnly)
		{
			Paste();
			return;
		}
	}
	//Cut
	else if (Key == EKeys::X)
	{
		if (ctrlOnly)
		{
			if (SelectionPropertyArray.Num() != 0)
			{
				Cut();
			}
			return;
		}
	}
	//Undo
	else if (Key == EKeys::Z)
	{
		if (ctrl && !alt)
		{
			//Ctrl+Shift+Z is the other half of the same gesture on every platform that has one
			if (shift) Redo(); else Undo();
			return;
		}
	}
	//Redo
	else if (Key == EKeys::Y)
	{
		if (ctrlOnly)
		{
			Redo();
			return;
		}
	}
	//Arrows. Ctrl moves by whole words; AltGr (ctrl+alt on Windows) is a character modifier, not a
	//motion one, so it is excluded and falls through to the character road below.
	else if (Key == EKeys::Left)
	{
		if (ctrl && !alt) MoveCaretByWord(-1, shift);
		else MoveCaret(0, shiftOnly);
		return;
	}
	else if (Key == EKeys::Right)
	{
		if (ctrl && !alt) MoveCaretByWord(1, shift);
		else MoveCaret(1, shiftOnly);
		return;
	}
	else if (Key == EKeys::Up)
	{
		MoveCaret(2, shiftOnly);
		return;
	}
	else if (Key == EKeys::Down)
	{
		MoveCaret(3, shiftOnly);
		return;
	}
	// The edit menu without a pointer. Shift+F10 is Windows' own keyboard shortcut for the context
	// menu, and the pad's Menu button is the same gesture on a controller.
	else if (Key == EKeys::F10)
	{
		if (shiftOnly)
		{
			ShowContextMenu();
			return;
		}
	}
	else if (Key == EKeys::Gamepad_Special_Right)
	{
		bHasContextMenuAnchor = false;//no pointer said where; the field's own corner is the answer
		ShowContextMenu();
		return;
	}
	else if (Key == EKeys::PageUp)
	{
		MoveCaretByPage(-1, shiftOnly);
		return;
	}
	else if (Key == EKeys::PageDown)
	{
		MoveCaretByPage(1, shiftOnly);
		return;
	}
	//Submit
	else if (Key == EKeys::Enter)
	{
		if (bAllowMultiLine)//multiline mode
		{
			if (MultiLineSubmitFunctionKeys.Num() > 0
				&& !MultiLineSubmitFunctionKeys.Contains(EKeys::Enter)//Enter is not allowed
				)
			{
					bool isSubmit = false;
					for (auto& SubmitFunctionKey : MultiLineSubmitFunctionKeys)
					{
						if (InIsKeyHeld(SubmitFunctionKey))
						{
							isSubmit = true;
						}
					}
					if (isSubmit)//enter submit
					{
						Submit();
						FinishCommitFromEnter();
						return;
					}
			}
			inputChar = '\n';//enter as new line
		}
		else//single line mode, enter means submit
		{
			Submit();
			FinishCommitFromEnter();
			return;
		}
	}
	//space
	else if (Key == EKeys::SpaceBar)
	{
		inputChar = ' ';
	}

	// Everything below this line is the US-QWERTY guess: a hand-written FKey -> TCHAR table, right on
	// exactly one keyboard layout. Two things switch it off, and both leave the function keys above
	// untouched because those really are key-shaped.
	//
	// (1) A host that delivers real platform characters (see HandleCharacterInput) owns every
	//     printable character from its first one onwards; guessing alongside it double-types.
	// (2) A Ctrl chord that reached this far is an unhandled shortcut, not text, and every real text
	//     field drops it -- producing '1' for Ctrl+Shift+1 was the letters-use-shift /
	//     punctuation-uses-shiftOnly inconsistency. AltGr is Ctrl+Alt on Windows and IS a character
	//     modifier, so a chord with Alt in it is not caught here.
	if (inputChar == 127 && (bHostDeliversCharacterEvents || (ctrl && !alt)))
	{
		return;
	}

	//caps lock
	bool upperCase = false;
	if (FSlateApplication::Get().GetModifierKeys().AreCapsLocked())
	{
		if (!shift)
		{
			upperCase = true;
		}
	}
	else
	{
		if (shift)
		{
			upperCase = true;
		}
	}
	//input char
	if (Key == EKeys::A)
		inputChar = upperCase ? 'A' : 'a';
	else if (Key == EKeys::B)
		inputChar = upperCase ? 'B' : 'b';
	else if (Key == EKeys::C)
		inputChar = upperCase ? 'C' : 'c';
	else if (Key == EKeys::D)
		inputChar = upperCase ? 'D' : 'd';
	else if (Key == EKeys::E)
		inputChar = upperCase ? 'E' : 'e';
	else if (Key == EKeys::F)
		inputChar = upperCase ? 'F' : 'f';
	else if (Key == EKeys::G)
		inputChar = upperCase ? 'G' : 'g';
	else if (Key == EKeys::H)
		inputChar = upperCase ? 'H' : 'h';
	else if (Key == EKeys::I)
		inputChar = upperCase ? 'I' : 'i';
	else if (Key == EKeys::J)
		inputChar = upperCase ? 'J' : 'j';
	else if (Key == EKeys::K)
		inputChar = upperCase ? 'K' : 'k';
	else if (Key == EKeys::L)
		inputChar = upperCase ? 'L' : 'l';
	else if (Key == EKeys::M)
		inputChar = upperCase ? 'M' : 'm';
	else if (Key == EKeys::N)
		inputChar = upperCase ? 'N' : 'n';
	else if (Key == EKeys::O)
		inputChar = upperCase ? 'O' : 'o';
	else if (Key == EKeys::P)
		inputChar = upperCase ? 'P' : 'p';
	else if (Key == EKeys::Q)
		inputChar = upperCase ? 'Q' : 'q';
	else if (Key == EKeys::R)
		inputChar = upperCase ? 'R' : 'r';
	else if (Key == EKeys::S)
		inputChar = upperCase ? 'S' : 's';
	else if (Key == EKeys::T)
		inputChar = upperCase ? 'T' : 't';
	else if (Key == EKeys::U)
		inputChar = upperCase ? 'U' : 'u';
	else if (Key == EKeys::V)
		inputChar = upperCase ? 'V' : 'v';
	else if (Key == EKeys::W)
		inputChar = upperCase ? 'W' : 'w';
	else if (Key == EKeys::X)
		inputChar = upperCase ? 'X' : 'x';
	else if (Key == EKeys::Y)
		inputChar = upperCase ? 'Y' : 'y';
	else if (Key == EKeys::Z)
		inputChar = upperCase ? 'Z' : 'z';

	else if (Key == EKeys::Tilde)
	{
		if (shiftOnly)
			inputChar = '~';
		else
			inputChar = '`';
	}
	else if (Key == EKeys::One)
	{
		if (shiftOnly)
			inputChar = '!';
		else
			inputChar = '1';
	}
	else if (Key == EKeys::Two)
	{
		if (shiftOnly)
			inputChar = '@';
		else
			inputChar = '2';
	}
	else if (Key == EKeys::Three)
	{
		if (shiftOnly)
			inputChar = '#';
		else
			inputChar = '3';
	}
	else if (Key == EKeys::Four)
	{
		if (shiftOnly)
			inputChar = '$';
		else
			inputChar = '4';
	}
	else if (Key == EKeys::Five)
	{
		if (shiftOnly)
			inputChar = '%';
		else
			inputChar = '5';
	}
	else if (Key == EKeys::Six)
	{
		if (shiftOnly)
			inputChar = '^';
		else
			inputChar = '6';
	}
	else if (Key == EKeys::Seven)
	{
		if (shiftOnly)
			inputChar = '&';
		else
			inputChar = '7';
	}
	else if (Key == EKeys::Eight)
	{
		if (shiftOnly)
			inputChar = '*';
		else
			inputChar = '8';
	}
	else if (Key == EKeys::Nine)
	{
		if (shiftOnly)
			inputChar = '(';
		else
			inputChar = '9';
	}
	else if (Key == EKeys::Zero)
	{
		if (shiftOnly)
			inputChar = ')';
		else
			inputChar = '0';
	}
	else if (Key == EKeys::Hyphen)
	{
		if (shiftOnly)
			inputChar = '_';
		else
			inputChar = '-';
	}
	else if (Key == EKeys::Equals)
	{
		if (shiftOnly)
			inputChar = '+';
		else
			inputChar = '=';
	}

	else if (Key == EKeys::NumPadZero)
		inputChar = '0';
	else if (Key == EKeys::NumPadOne)
		inputChar = '1';
	else if (Key == EKeys::NumPadTwo)
		inputChar = '2';
	else if (Key == EKeys::NumPadThree)
		inputChar = '3';
	else if (Key == EKeys::NumPadFour)
		inputChar = '4';
	else if (Key == EKeys::NumPadFive)
		inputChar = '5';
	else if (Key == EKeys::NumPadSix)
		inputChar = '6';
	else if (Key == EKeys::NumPadSeven)
		inputChar = '7';
	else if (Key == EKeys::NumPadEight)
		inputChar = '8';
	else if (Key == EKeys::NumPadNine)
		inputChar = '9';

	else if (Key == EKeys::Multiply)
		inputChar = '*';
	else if (Key == EKeys::Add)
		inputChar = '+';
	else if (Key == EKeys::Subtract)
		inputChar = '-';
	else if (Key == EKeys::Decimal)
		inputChar = '.';
	else if (Key == EKeys::Divide)
		inputChar = '/';

	else if (Key == EKeys::LeftBracket)
	{
		if (shiftOnly)
			inputChar = '{';
		else
			inputChar = '[';
	}
	else if (Key == EKeys::RightBracket)
	{
		if (shiftOnly)
			inputChar = '}';
		else
			inputChar = ']';
	}
	else if (Key == EKeys::Backslash)
	{
		if (shiftOnly)
			inputChar = '|';
		else
			inputChar = '\\';
	}
	else if (Key == EKeys::Semicolon)
	{
		if (shiftOnly)
			inputChar = ':';
		else
			inputChar = ';';
	}
	else if (Key == EKeys::Apostrophe)
	{
		if (shiftOnly)
			inputChar = '\"';
		else
			inputChar = '\'';
	}
	else if (Key == EKeys::Comma)
	{
		if (shiftOnly)
			inputChar = '<';
		else
			inputChar = ',';
	}
	else if (Key == EKeys::Period)
	{
		if (shiftOnly)
			inputChar = '>';
		else
			inputChar = '.';
	}
	else if (Key == EKeys::Slash)
	{
		if (shiftOnly)
			inputChar = '?';
		else
			inputChar = '/';
	}


	if (inputChar == 127)return;//no character came out of the table; nothing to insert
	VerifyAndInsertCharAtCaretPosition(inputChar);
}

bool UUITextInput::HandleCharacterInput(TCHAR InCharacter)
{
	if (!bInputActive)return false;
	if (TextVisual == nullptr)return false;
	// From here on the key table stops guessing printable characters for EVERY field: the platform
	// is telling us what the player typed, on whatever layout they have, and two sources type twice.
	// Set only once a live edit has actually received one, so a host probing an idle field does not
	// switch the fallback off for the whole process.
	bHostDeliversCharacterEvents = true;
	//control characters are not text; the platform sends \b, \r, \x1b and friends through the same
	//road and the key table above is what turns those into edits
	if (InCharacter < 32 && !(InCharacter == '\n' && bAllowMultiLine))return false;
	return VerifyAndInsertCharAtCaretPosition(InCharacter);
}
bool UUITextInput::HandleCharacterInputString(const FString& InCharacters)
{
	bool bAnyAccepted = false;
	for (int32 i = 0; i < InCharacters.Len(); i++)
	{
		bAnyAccepted |= HandleCharacterInput(InCharacters[i]);
	}
	return bAnyAccepted;
}
TWeakObjectPtr<UUITextInput> UUITextInput::ActiveTextInput = nullptr;
bool UUITextInput::bHostDeliversCharacterEvents = false;
bool UUITextInput::RouteCharacterInputToActiveInput(TCHAR InCharacter)
{
	if (UUITextInput* Input = ActiveTextInput.Get())
	{
		return Input->HandleCharacterInput(InCharacter);
	}
	return false;
}
UUITextInput* UUITextInput::GetActiveTextInput()
{
	return ActiveTextInput.Get();
}
void UUITextInput::WarnOnceIfNoCharacterEventSource()
{
	// Said once per process, the first time a field is edited on a platform that types with a real
	// keyboard. There is nothing this plugin can do from inside the field: the engine's only landing
	// place for a character in a game is UGameViewportClient::InputChar, and it is a virtual with no
	// delegate, so SOMEBODY has to own that class. UDreamGameViewportClient is the one-line answer.
	static bool bWarned = false;
	if (bWarned)return;
	if (bHostDeliversCharacterEvents)return;//a host is already feeding characters; nothing to say
	bWarned = true;
	if (GEngine == nullptr)return;

	// The live client is the truth when there is one -- a project can set the class in config, in
	// C++, or per-world -- and the configured class is the answer before one exists.
	const UClass* ViewportClientClass = nullptr;
	if (IsValid(GEngine->GameViewport))
	{
		ViewportClientClass = GEngine->GameViewport->GetClass();
	}
	else if (GEngine->GameViewportClientClass != nullptr)
	{
		ViewportClientClass = GEngine->GameViewportClientClass;
	}
	if (ViewportClientClass != nullptr && ViewportClientClass->IsChildOf(UDreamGameViewportClient::StaticClass()))
	{
		return;//properly wired
	}
	UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This project's game viewport client is '%s', which does not route character input to DreamGUI. ")
		TEXT("Text fields will fall back to their own FKey-to-character table, which is only correct on a US QWERTY layout -- AZERTY, QWERTZ, Dvorak, Cyrillic, dead keys and AltGr will type the wrong character. ")
		TEXT("Fix by setting GameViewportClientClassName=/Script/DreamGUI.DreamGameViewportClient in [/Script/Engine.Engine] of DefaultEngine.ini, by deriving the project's own viewport client from UDreamGameViewportClient, or by calling UUITextInput::RouteCharacterInputToActiveInput(Character) from its InputChar override.")
		, ANSI_TO_TCHAR(__FUNCTION__), __LINE__
		, ViewportClientClass != nullptr ? *ViewportClientClass->GetName() : TEXT("(none yet)"));
}

bool UUITextInput::IsValidChar(TCHAR c, const FString& InAgainstText, int32 InAgainstCaretIndex)
{
	//Deliberately NOT the member Text/CaretPositionIndex: see the header. Every rule below is
	//positional, so they must all read the one string the caller is actually building.
	const FString& AgainstText = InAgainstText;
	const int32 AgainstCaret = InAgainstCaretIndex;

	auto StringContainsChar = [](TCHAR testChar, const FString& string, int stringLength)
	{
		for (int i = 0; i < stringLength; i++)
		{
			if (string[i] == testChar)
			{
				return true;
			}
		}
		return false;
	};
	//delete key on mac
	if ((int)c == 127)
		return false;
	// Control characters are not text, whatever the input type says. Standard answers "true" to
	// everything below, so before this a paste carried NUL, ESC and vertical tab straight into Text
	// -- and a string with an embedded NUL has undefined length and renders as anyone's guess.
	// The two exceptions are the two this component itself produces: a newline, and only in a field
	// that has lines, and a tab.
	if (c < 32 && c != '\t' && !(c == '\n' && bAllowMultiLine))
		return false;
	//input type
	switch (InputType)
	{
	case EUITextInputType::Standard:
		return true;
	case EUITextInputType::IntegerNumber:
	{
		if (c >= '0' && c <= '9')
		{
			if (AgainstCaret == 0)
			{
				if (StringContainsChar('-', AgainstText, AgainstText.Len()))
				{
					return false;
				}
			}
			return true;
		}
		if (c == '-')
		{
			if (AgainstCaret == 0 && !StringContainsChar('-', AgainstText, AgainstText.Len()))
			{
				return true;
			}
		}
		return false;
	}
	case EUITextInputType::DecimalNumber:
	{
		if (c >= '0' && c <= '9')
		{
			if (AgainstCaret == 0)
			{
				if (StringContainsChar('-', AgainstText, AgainstText.Len()))
				{
					return false;
				}
			}
			return true;
		}
		if (c == '.')
		{
			if (StringContainsChar('.', AgainstText, AgainstText.Len()))
			{
				return false;
			}
			else
			{
				if (AgainstCaret == 0)
				{
					if (!StringContainsChar('-', AgainstText, AgainstText.Len()))
					{
						return true;
					}
					else
					{
						return false;
					}
				}
				return true;
			}
		}
		if (c == '-')
		{
			if (AgainstCaret == 0 && !StringContainsChar('-', AgainstText, AgainstText.Len()))
			{
				return true;
			}
		}
		return false;
	}
	case EUITextInputType::Alphanumeric:
	{
		if (c >= 'A' && c <= 'Z') return true;
		if (c >= 'a' && c <= 'z') return true;
		if (c >= '0' && c <= '9') return true;
		return false;
	}
	case EUITextInputType::EmailAddress:
	{
		if (c >= 'A' && c <= 'Z') return true;
		if (c >= 'a' && c <= 'z') return true;
		if (c >= '0' && c <= '9') return true;
		if (c == '@')
		{
			return !StringContainsChar('@', AgainstText, AgainstText.Len());
		}
		static FString kEmailSpecialCharacters = "!#$%&'*+-/=?^_`{|}~";
		if (StringContainsChar(c, kEmailSpecialCharacters, kEmailSpecialCharacters.Len()))
		{
			//true: this list is the set of specials an email address ALLOWS, exactly as
			//EUITextInputType::EmailAddress documents it -- being in it was rejecting them
			return true;
		}
		if (c == '.')
		{
			// "more than one dot in a row are not allowed", so the two characters that matter are the
			// ones the dot would land BETWEEN. The old code named a variable LastChar and then read
			// the character AT the caret -- which is the one to the RIGHT of the insertion point --
			// so the character actually to the left was never looked at and "a..b" typed straight in.
			const int32 ClampedCaret = FMath::Clamp(AgainstCaret, 0, AgainstText.Len());
			const TCHAR PrevChar = (ClampedCaret > 0) ? AgainstText[ClampedCaret - 1] : TEXT('\0');
			const TCHAR NextChar = (ClampedCaret < AgainstText.Len()) ? AgainstText[ClampedCaret] : TEXT('\0');
			return PrevChar != '.' && NextChar != '.';
		}
		return false;
	}
	case EUITextInputType::Password:
		//handled when UpdateUITextComponent
		return true;
	case EUITextInputType::Custom:
	{
		if (IsValid(CustomValidation))
		{
			// The open selection is already gone from AgainstText -- the caller removes it before
			// asking, which is the only way the positional rules above can agree with each other.
			// This branch used to simulate that deletion itself, with raw caret indices, and so
			// disagreed with both the real deletion and every other case in this switch.
			FString TempText = AgainstText;
			const int32 TempCaretIndex = FMath::Clamp(AgainstCaret, 0, TempText.Len());
			TempText.InsertAt(TempCaretIndex, c);

			return CustomValidation->OnValidateInput(this, TempText, TempCaretIndex);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d InputType use CustomValidation, but the object is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return true;
		}
	}
	}
	////new line and tab
	//if (c == '\n' || c == '\t')
	//	return true;
	return true;
}
bool UUITextInput::GetSelectionCharRange(int32& OutStartCharIndex, int32& OutCharCount)
{
	if (SelectionPropertyArray.Num() == 0)return false;
	int32 StartCharIndex = FMath::Min(PressCaretPositionIndex, CaretPositionIndex);
	int32 EndCharIndex = FMath::Max(PressCaretPositionIndex, CaretPositionIndex);
	if (DreamTextInputLocal::CanMapCaretIndices(TextVisual))
	{
		//the same mapping every other edit road in this file takes: caret index -> source offset
		TextVisual->SetText(FText::FromString(GetReplaceText()));
		const int32 MappedStart = TextVisual->GetCharIndexByCaretIndex(StartCharIndex);
		const int32 MappedEnd = TextVisual->GetCharIndexByCaretIndex(EndCharIndex);
		StartCharIndex = FMath::Min(MappedStart, MappedEnd);
		EndCharIndex = FMath::Max(MappedStart, MappedEnd);
	}
	StartCharIndex = FMath::Clamp(StartCharIndex, 0, Text.Len());
	EndCharIndex = FMath::Clamp(EndCharIndex, StartCharIndex, Text.Len());
	OutStartCharIndex = StartCharIndex;
	OutCharCount = EndCharIndex - StartCharIndex;
	return OutCharCount > 0;
}
bool UUITextInput::DeleteSelection(bool InFireEvent)
{
	if (bReadOnly)return false;
	if (SelectionPropertyArray.Num() != 0)//delete selection frist
	{
		int32 StartCharIndex = 0, CharCount = 0;
		if (GetSelectionCharRange(StartCharIndex, CharCount))
		{
			PushUndoSnapshot();
			Text.RemoveAt(StartCharIndex, CharCount);
		}
		CaretPositionIndex = FMath::Min(PressCaretPositionIndex, CaretPositionIndex);
		PressCaretPositionIndex = CaretPositionIndex;
		UpdateAfterTextChange(InFireEvent);
		return true;
	}
	return false;
}
void UUITextInput::InsertCharAtCaretPosition(TCHAR c)
{
	if (bReadOnly)return;
	// Without a laid-out text there is no caret map to ask, and asking anyway reads the last line of
	// an empty array. A caret index and a source offset are the same number until a surrogate pair
	// appears, so that is the answer for the unlaid-out case rather than a crash.
	if (!DreamTextInputLocal::CanMapCaretIndices(TextVisual))
	{
		const int32 CharIndex = FMath::Clamp(CaretPositionIndex, 0, Text.Len());
		Text.InsertAt(CharIndex, c);
		CaretPositionIndex = CharIndex + 1;
		PressCaretPositionIndex = CaretPositionIndex;
		if (TextVisual.IsValid())
		{
			TextVisual->SetText(FText::FromString(GetReplaceText()));
		}
		return;
	}
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	auto CharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex);
	Text.InsertAt(CharIndex, c);
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	UDreamWidget::RebuildLayoutImmediately(TextVisual->GetWidget());
	CaretPositionIndex = TextVisual->GetCaretIndexByCharIndex(CharIndex) + 1;
	PressCaretPositionIndex = CaretPositionIndex;
}
void UUITextInput::InsertStringAtCaretPosition(const FString& value)
{
	if (bReadOnly)return;
	//same unlaid-out fallback as the single-character road above
	if (!DreamTextInputLocal::CanMapCaretIndices(TextVisual))
	{
		const int32 CharIndex = FMath::Clamp(CaretPositionIndex, 0, Text.Len());
		Text.InsertAt(CharIndex, value);
		CaretPositionIndex = CharIndex + value.Len();
		PressCaretPositionIndex = CaretPositionIndex;
		if (TextVisual.IsValid())
		{
			TextVisual->SetText(FText::FromString(GetReplaceText()));
		}
		return;
	}
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	auto CharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex);
	Text.InsertAt(CharIndex, value);
	//CharIndex now names the position just PAST the inserted string, so the caret index for it is
	//already the answer -- the trailing +1 the single-character version needs (its CharIndex still
	//names the position before the one character it inserted) put this one a glyph too far right
	CharIndex += value.Len();
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	// The other half of the same defect: GetCaretIndexByCharIndex reads the laid-out text, so
	// without the rebuild the SetText above is still pending and the mapping answers against the
	// PREVIOUS layout. The single-character version has always had this line.
	UDreamWidget::RebuildLayoutImmediately(TextVisual->GetWidget());
	CaretPositionIndex = TextVisual->GetCaretIndexByCharIndex(CharIndex);
	PressCaretPositionIndex = CaretPositionIndex;
}

void UUITextInput::BackSpace()
{
	if (bReadOnly)return;
	if (SelectionPropertyArray.Num() == 0)//no selection mask, use caret
	{
		if (CaretPositionIndex > 0)
		{
			PushUndoSnapshot();//before the caret moves: an undo lands where the edit began
			CaretPositionIndex--;
			int CharIndex = CaretPositionIndex;
			int RemoveCount = 1;
			if (DreamTextInputLocal::CanMapCaretIndices(TextVisual))
			{
				TextVisual->SetText(FText::FromString(GetReplaceText()));
				CharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex);
				if (CharIndex + 1 < Text.Len())//not end char, could be rich text, so check delete count
				{
					auto NextCharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex + 1);
					RemoveCount = NextCharIndex - CharIndex;
				}
			}
			CharIndex = FMath::Clamp(CharIndex, 0, Text.Len());
			RemoveCount = FMath::Clamp(RemoveCount, 0, Text.Len() - CharIndex);
			if (RemoveCount > 0)
			{
				Text.RemoveAt(CharIndex, RemoveCount);
			}
			UpdateAfterTextChange(true);
			PressCaretPositionIndex = CaretPositionIndex;
		}
	}
	else//selection mask, delete
	{
		DeleteSelection(true);
	}
}
void UUITextInput::ForwardSpace()
{
	if (bReadOnly)return;
	if (SelectionPropertyArray.Num() == 0)//no selection mask, use caret
	{
		int CharIndex = CaretPositionIndex;
		int RemoveCount = 1;
		if (DreamTextInputLocal::CanMapCaretIndices(TextVisual))
		{
			TextVisual->SetText(FText::FromString(GetReplaceText()));
			CharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex);
			if (CharIndex + 1 < Text.Len())//not end char, could be rich text, so check delete count
			{
				auto NextCharIndex = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex + 1);
				RemoveCount = NextCharIndex - CharIndex;
			}
		}
		if (CharIndex >= 0 && CharIndex < Text.Len() && RemoveCount > 0 && CharIndex + RemoveCount <= Text.Len())
		{
			PushUndoSnapshot();
			Text.RemoveAt(CharIndex, RemoveCount);
			UpdateAfterTextChange(true);
			PressCaretPositionIndex = CaretPositionIndex;
		}
	}
	else//selection mask, delete
	{
		DeleteSelection(true);
	}
}
void UUITextInput::Copy()
{
	if (InputType == EUITextInputType::Password
		|| DisplayType == EUITextInputDisplayType::Password
		)return;//not allow copy password
	if (SelectionPropertyArray.Num() != 0)//have selection
	{
		TextVisual->SetText(FText::FromString(Text));
		auto CharIndexAtPressCaretPosition = TextVisual->GetCharIndexByCaretIndex(PressCaretPositionIndex);
		auto CharIndexAtCaretPosition = TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex);
		int32 TempCharIndex = CharIndexAtPressCaretPosition > CharIndexAtCaretPosition ? CharIndexAtCaretPosition : CharIndexAtPressCaretPosition;
		auto CopyText = Text.Mid(TempCharIndex, FMath::Abs(CharIndexAtPressCaretPosition - CharIndexAtCaretPosition));
		FPlatformApplicationMisc::ClipboardCopy(*CopyText);
		
		UpdateUITextComponent();
	}
}
void UUITextInput::Paste()
{
	if (bReadOnly)return;
	FString pasteString;
	FPlatformApplicationMisc::ClipboardPaste(pasteString);
	if (pasteString.Len() <= 0)return;
	pasteString.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	if (!bAllowMultiLine)
	{
		for (int i = 0; i < pasteString.Len(); i++)
		{
			if (pasteString[i] == '\n' || pasteString[i] == '\r')
			{
				pasteString[i] = ' ';
			}
		}
	}

	// One road for "verify this string and put it in at the caret", so the paste answers to the same
	// selection handling, the same MaxLength and the same undo step as typing does. Pasting nothing
	// valid over a selection still replaces it, which is what every text field does.
	if (!VerifyAndInsertStringAtCaretPosition(pasteString))
	{
		//nothing in the clipboard survived the filter, so the replaced selection is the whole edit
		DeleteSelection(true);
	}
}
void UUITextInput::Cut()
{
	if (bReadOnly)return;
	if (InputType == EUITextInputType::Password
		|| DisplayType == EUITextInputDisplayType::Password
		)return;//not allow copy password
	if (SelectionPropertyArray.Num() != 0)//have selection
	{
		Copy();
		DeleteSelection(true);
	}
}
void UUITextInput::SelectAll()
{
	if (!TextVisual.IsValid())return;
	// Ask for the last caret instead of guessing "Text.Len() * 2, big enough". The guess survived
	// only because UpdateCaretPosition's FindCaretByIndex clamps a copy of it -- the unclamped
	// number was then handed straight to GetSelectionProperty below.
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	CaretPositionIndex = FMath::Max(TextVisual->GetLastCaret(), 0);
	PressCaretPositionIndex = 0;
	UpdateCaretPosition(false);
	// ...and subtract the visible start, which every OTHER caller of GetSelectionProperty does
	// (MoveCaret, OnPointerDrag). Select-all was the one that passed raw indices, so in a scrolled
	// field the highlight bars landed on a different run of characters than the selection.
	TextVisual->GetSelectionProperty(PressCaretPositionIndex - VisibleCaretStartIndex, CaretPositionIndex - VisibleCaretStartIndex, SelectionPropertyArray);
	UpdateSelection();
	UpdateUITextComponent();
}
void UUITextInput::SelectWordAtCaret()
{
	if (!DreamTextInputLocal::CanMapCaretIndices(TextVisual))return;
	if (Text.Len() == 0)return;
	TextVisual->SetText(FText::FromString(GetReplaceText()));
	const int32 CaretCharIndex = FMath::Clamp(TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex), 0, Text.Len());
	auto IsWordChar = [](TCHAR c) { return FChar::IsAlnum(c) || c == '_'; };

	int32 StartCharIndex = CaretCharIndex;
	int32 EndCharIndex = CaretCharIndex;
	// A double click on a space selects the run of spaces, on a word the whole word: the run under
	// the caret is whatever the character to its right is made of, falling back to the one on its
	// left at the end of the text.
	const int32 ProbeIndex = (CaretCharIndex < Text.Len()) ? CaretCharIndex : CaretCharIndex - 1;
	const bool bWantWordChars = IsWordChar(Text[ProbeIndex]);
	while (StartCharIndex > 0 && IsWordChar(Text[StartCharIndex - 1]) == bWantWordChars)
	{
		StartCharIndex--;
	}
	while (EndCharIndex < Text.Len() && IsWordChar(Text[EndCharIndex]) == bWantWordChars)
	{
		EndCharIndex++;
	}
	if (StartCharIndex == EndCharIndex)return;

	PressCaretPositionIndex = TextVisual->GetCaretIndexByCharIndex(StartCharIndex);
	CaretPositionIndex = TextVisual->GetCaretIndexByCharIndex(EndCharIndex);
	UpdateCaretPosition(false);
	TextVisual->GetSelectionProperty(PressCaretPositionIndex - VisibleCaretStartIndex, CaretPositionIndex - VisibleCaretStartIndex, SelectionPropertyArray);
	UpdateSelection();
	UpdateUITextComponent();
}
void UUITextInput::ShowContextMenu()
{
	if (!bAllowContextMenu)return;
	UDreamWidget* OwnWidget = GetWidget();
	if (!IsValid(OwnWidget))return;
	if (!IsValid(OwnWidget->GetOuter()))return;
	HideContextMenu();//a second open replaces the first; two menus is never the answer

	const bool bIsPassword = InputType == EUITextInputType::Password || DisplayType == EUITextInputDisplayType::Password;
	const bool bHasSelection = SelectionPropertyArray.Num() > 0;
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	// Only what can act right now, which is the honest version of UMG's greying-out: an entry that
	// does nothing when pressed is worse than one that is not offered.
	const bool bCanUndo = bAllowUndoRedo && !bReadOnly && UndoStack.Num() > 0;
	const bool bCanRedo = bAllowUndoRedo && !bReadOnly && RedoStack.Num() > 0;
	const bool bCanCut = !bReadOnly && !bIsPassword && bHasSelection;
	const bool bCanCopy = !bIsPassword && bHasSelection;
	const bool bCanPaste = !bReadOnly && ClipboardText.Len() > 0;
	const bool bCanSelectAll = Text.Len() > 0;
	if (!bCanUndo && !bCanRedo && !bCanCut && !bCanCopy && !bCanPaste && !bCanSelectAll)return;

	// Under the screen root when there is one, so an ancestor's clip area cannot cut the menu in
	// half -- the same reason UUIDropdown lifts its list. Without a canvas (a headless tree, an
	// authoring preview) the field itself is the only parent there is.
	UDreamWidget* MenuParent = OwnWidget;
	UDreamCanvas* RootCanvas = OwnWidget->GetRootCanvas();
	if (IsValid(RootCanvas) && IsValid(RootCanvas->GetWidget()))
	{
		MenuParent = RootCanvas->GetWidget();
	}

	UDreamWidget* MenuRoot = NewObject<UDreamWidget>(OwnWidget->GetOuter());
	MenuRoot->SetParent(MenuParent, false);
	MenuRoot->SetDisplayName(TEXT("UITextInput_ContextMenu"));
	//top-left pivot, so the menu hangs down and to the right of the point that opened it, and the
	//root's own local origin IS that corner (GetLocalSpaceLeft/Top are pivot-relative)
	MenuRoot->SetPivot(FVector2D(0, 1));
	MenuRoot->SetWidth(ContextMenuWidth);
	if (auto MenuVisual = MenuRoot->CreateNewVisual<UDreamImage>())
	{
		MenuVisual->SetColor(ContextMenuBackgroundColor);
		MenuVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
	}

	int32 EntryCount = 0;
	AddContextMenuEntry(MenuRoot, bCanUndo, NSLOCTEXT("DreamGUI", "TextInputContextMenu_Undo", "Undo"), EContextMenuAction::Undo, EntryCount);
	AddContextMenuEntry(MenuRoot, bCanRedo, NSLOCTEXT("DreamGUI", "TextInputContextMenu_Redo", "Redo"), EContextMenuAction::Redo, EntryCount);
	AddContextMenuEntry(MenuRoot, bCanCut, NSLOCTEXT("DreamGUI", "TextInputContextMenu_Cut", "Cut"), EContextMenuAction::Cut, EntryCount);
	AddContextMenuEntry(MenuRoot, bCanCopy, NSLOCTEXT("DreamGUI", "TextInputContextMenu_Copy", "Copy"), EContextMenuAction::Copy, EntryCount);
	AddContextMenuEntry(MenuRoot, bCanPaste, NSLOCTEXT("DreamGUI", "TextInputContextMenu_Paste", "Paste"), EContextMenuAction::Paste, EntryCount);
	AddContextMenuEntry(MenuRoot, bCanSelectAll, NSLOCTEXT("DreamGUI", "TextInputContextMenu_SelectAll", "Select All"), EContextMenuAction::SelectAll, EntryCount);

	const float EntryHeight = FMath::Max(1.0f, GetContextMenuEntryHeight());
	MenuRoot->SetHeight(EntryCount * EntryHeight + ContextMenuPadding * 2.0f);
	if (MenuParent == OwnWidget)
	{
		//no canvas to lift to: sit under the field's own top-left corner
		MenuRoot->SetRelativeLocation(FVector(0, OwnWidget->GetLocalSpaceLeft(), OwnWidget->GetLocalSpaceBottom()));
	}
	else
	{
		MenuRoot->SetWorldLocation(bHasContextMenuAnchor ? ContextMenuAnchorWorldPoint : OwnWidget->GetWorldTransform().GetLocation());
	}

	//over everything: the menu is the thing being interacted with for as long as it is open
	if (IsValid(RootCanvas))
	{
		auto MenuCanvas = MenuRoot->GetComponent<UDreamCanvas>();
		if (MenuCanvas == nullptr)
		{
			MenuCanvas = MenuRoot->AddComponent<UDreamCanvas>();
		}
		if (MenuCanvas != nullptr)
		{
			MenuCanvas->SetOverrideSorting(true);
			MenuCanvas->SetSortOrderToHighestOfHierarchy(true);
		}
		//and a full-screen sheet behind it, so a click anywhere else closes it rather than falling
		//through to whatever is under the menu -- UUIDropdown's blocker, same shape
		UDreamWidget* Blocker = NewObject<UDreamWidget>(OwnWidget->GetOuter());
		Blocker->SetDisplayName(TEXT("UITextInput_ContextMenu_Blocker"));
		Blocker->SetParent(RootCanvas->GetWidget(), false);
		Blocker->SetSizeDelta(FVector2D::ZeroVector);
		Blocker->SetAnchorMin(FVector2D(0.0f, 0.0f));
		Blocker->SetAnchorMax(FVector2D(1.0f, 1.0f));
		Blocker->CreateNewVisual<UDreamVisualEmpty>();//needs a visual to be raycast at all
		if (auto BlockerCanvas = Blocker->AddComponent<UDreamCanvas>())
		{
			BlockerCanvas->SetOverrideSorting(true);
			BlockerCanvas->SetSortOrderToHighestOfHierarchy();
			BlockerCanvas->SetTraceChannel(RootCanvas->GetTraceChannel());
			//one above the sheet, so the menu is in front of the thing that catches clicks past it
			if (auto MenuCanvasToRaise = MenuRoot->GetComponent<UDreamCanvas>())
			{
				MenuCanvasToRaise->SetSortOrder(BlockerCanvas->GetSortOrder() + 1, true);
			}
		}
		if (auto BlockerButton = Blocker->AddComponent<UUIButton>())
		{
			BlockerButton->GetOnClickEvent().AddWeakLambda(this, [this] { this->HideContextMenu(); });
		}
		ContextMenuBlocker = Blocker;
	}
	ContextMenuRoot = MenuRoot;
}
UDreamWidget* UUITextInput::AddContextMenuEntry(UDreamWidget* InMenuRoot, bool InbApplicable, const FText& InLabel, EContextMenuAction InAction, int32& InOutEntryCount)
{
	if (!InbApplicable)return nullptr;
	if (!IsValid(InMenuRoot))return nullptr;
	const float EntryHeight = FMath::Max(1.0f, GetContextMenuEntryHeight());
	const int32 EntryIndex = InOutEntryCount++;

	UDreamWidget* EntryWidget = NewObject<UDreamWidget>(InMenuRoot->GetOuter());
	EntryWidget->SetParent(InMenuRoot, false);
	//named by what it does, not by where it landed: which entries a field offers depends on the
	//clipboard, the selection and the history, so the position of any one of them is not a fact
	EntryWidget->SetDisplayName(FString::Printf(TEXT("ContextMenuEntry_%s"), GetContextMenuActionName(InAction)));
	EntryWidget->SetPivot(FVector2D(0, 0.5f));
	EntryWidget->SetWidth(FMath::Max(1.0f, ContextMenuWidth - ContextMenuPadding * 2.0f));
	EntryWidget->SetHeight(EntryHeight);
	//the root's origin is its top-left corner, so entries march downwards from it
	EntryWidget->SetRelativeLocation(FVector(0, ContextMenuPadding, -(ContextMenuPadding + EntryIndex * EntryHeight + EntryHeight * 0.5f)));
	UDreamImage* EntryVisual = EntryWidget->CreateNewVisual<UDreamImage>();
	if (EntryVisual != nullptr)
	{
		EntryVisual->SetColor(ContextMenuBackgroundColor);
		EntryVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
	}

	UDreamWidget* LabelWidget = NewObject<UDreamWidget>(InMenuRoot->GetOuter());
	LabelWidget->SetParent(EntryWidget, false);
	LabelWidget->SetDisplayName(TEXT("Label"));
	LabelWidget->SetPivot(FVector2D(0, 0.5f));
	LabelWidget->SetWidth(FMath::Max(1.0f, EntryWidget->GetWidth() - ContextMenuPadding * 2.0f));
	LabelWidget->SetHeight(EntryHeight);
	LabelWidget->SetRelativeLocation(FVector(0, ContextMenuPadding, 0));
	if (auto LabelVisual = LabelWidget->CreateNewVisual<UDreamText>())
	{
		//the field's own font, so the menu reads as part of the same UI without a style of its own
		if (TextVisual.IsValid())
		{
			LabelVisual->SetFont(TextVisual->GetFont());
			LabelVisual->SetFontSize(TextVisual->GetFontSize());
		}
		LabelVisual->SetColor(ContextMenuTextColor);
		LabelVisual->SetText(InLabel);
		LabelVisual->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
		LabelVisual->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
	}

	if (auto EntryButton = EntryWidget->AddComponent<UUIButton>())
	{
		// Explicit colours, because a selectable's transition colours default to WHITE and it tints
		// the widget's own visual with them -- an unstyled entry is a white bar the moment the
		// pointer touches it. The hover and press shades are derived from the menu's own colour so
		// one authored value still themes the whole menu.
		auto Shade = [](const FColor& InColor, int32 InDelta)
		{
			return FColor(
				(uint8)FMath::Clamp((int32)InColor.R + InDelta, 0, 255),
				(uint8)FMath::Clamp((int32)InColor.G + InDelta, 0, 255),
				(uint8)FMath::Clamp((int32)InColor.B + InDelta, 0, 255),
				InColor.A);
		};
		if (EntryVisual != nullptr)
		{
			EntryButton->SetTransitionTarget(EntryVisual);
		}
		EntryButton->SetNormalColor(ContextMenuBackgroundColor);
		EntryButton->SetHoveredColor(Shade(ContextMenuBackgroundColor, 22));
		EntryButton->SetPressedColor(Shade(ContextMenuBackgroundColor, -14));
		EntryButton->SetDisabledColor(Shade(ContextMenuBackgroundColor, 8));

		const EContextMenuAction Action = InAction;
		EntryButton->GetOnClickEvent().AddWeakLambda(this, [this, Action] { this->ExecuteContextMenuAction(Action); });
	}
	return EntryWidget;
}
const TCHAR* UUITextInput::GetContextMenuActionName(EContextMenuAction InAction)
{
	switch (InAction)
	{
	case EContextMenuAction::Undo: return TEXT("Undo");
	case EContextMenuAction::Redo: return TEXT("Redo");
	case EContextMenuAction::Cut: return TEXT("Cut");
	case EContextMenuAction::Copy: return TEXT("Copy");
	case EContextMenuAction::Paste: return TEXT("Paste");
	case EContextMenuAction::SelectAll: return TEXT("SelectAll");
	}
	return TEXT("Unknown");
}
void UUITextInput::ExecuteContextMenuAction(EContextMenuAction InAction)
{
	//the menu closes first: every one of these changes the selection or the text the menu described
	HideContextMenu();
	switch (InAction)
	{
	case EContextMenuAction::Undo: Undo(); break;
	case EContextMenuAction::Redo: Redo(); break;
	case EContextMenuAction::Cut: Cut(); break;
	case EContextMenuAction::Copy: Copy(); break;
	case EContextMenuAction::Paste: Paste(); break;
	case EContextMenuAction::SelectAll: SelectAll(); break;
	}
}
void UUITextInput::HideContextMenu()
{
	if (ContextMenuBlocker.IsValid())
	{
		ContextMenuBlocker->DestroyWidget();
	}
	ContextMenuBlocker = nullptr;
	if (ContextMenuRoot.IsValid())
	{
		ContextMenuRoot->DestroyWidget();
	}
	ContextMenuRoot = nullptr;
}
float UUITextInput::GetContextMenuEntryHeight()const
{
	//one line of the field's own text plus breathing room, so the menu scales with the font
	const float FontSize = TextVisual.IsValid() ? TextVisual->GetFontSize() : 15.0f;
	return FMath::Max(1.0f, FontSize * 1.8f);
}

void UUITextInput::Submit()
{
	bSubmittedThisActivation = true;
	OnSubmitCPP.Broadcast(Text);
	OnSubmitBP.Broadcast(Text);
	OnSubmit.FireEvent(Text);
}

void UUITextInput::FinishCommitFromEnter()
{
	if (bClearKeyboardFocusOnCommit)
	{
		// Which is what Enter has always done here, and stays the default. No second submit: the
		// Enter already fired one, and bSubmittedThisActivation is what tells the end of the edit so.
		DeactivateInput();
		return;
	}
	if (bSelectAllTextOnCommit)
	{
		// The edit continues, so the value is offered back ready to be typed over -- the state a row
		// of fields a player is filling in one Enter at a time wants to be left in.
		SelectAll();
	}
}

FString UUITextInput::GetTextWithoutSelection(int32& OutCaretCharIndex)
{
	FString Result = Text;
	OutCaretCharIndex = FMath::Clamp(DreamTextInputLocal::CanMapCaretIndices(TextVisual)
		? TextVisual->GetCharIndexByCaretIndex(CaretPositionIndex) : CaretPositionIndex, 0, Text.Len());
	int32 StartCharIndex = 0, CharCount = 0;
	if (GetSelectionCharRange(StartCharIndex, CharCount))
	{
		Result.RemoveAt(StartCharIndex, CharCount);
		OutCaretCharIndex = StartCharIndex;
	}
	return Result;
}
bool UUITextInput::VerifyAndInsertStringAtCaretPosition(const FString& Value)
{
	if (bReadOnly)return false;
	// Validated against the text the insertion actually lands in -- the field's text with the open
	// selection already taken out, growing by one character at a time -- rather than against the
	// text as it stands now. A positional rule ("only one dot") asked against a stale string refuses
	// characters the result would have been perfectly happy with.
	int32 AgainstCaret = 0;
	FString AgainstText = GetTextWithoutSelection(AgainstCaret);
	FString verifiedString;
	for (int i = 0; i < Value.Len(); i++)
	{
		TCHAR c = Value[i];
		if (!HasRoomForMoreChars(AgainstText.Len(), 1))break;
		if (IsValidChar(c, AgainstText, AgainstCaret))
		{
			verifiedString.AppendChar(c);
			AgainstText.InsertAt(AgainstCaret, c);
			AgainstCaret++;
		}
	}
	const bool bAnyDeleted = (verifiedString.Len() > 0) ? DeleteSelection(false) : false;
	if (verifiedString.Len() > 0)
	{
		if (!bAnyDeleted)PushUndoSnapshot();//DeleteSelection already pushed one for this edit
		InsertStringAtCaretPosition(verifiedString);
		UpdateAfterTextChange(true);
		return true;
	}
	return false;
}
bool UUITextInput::VerifyAndInsertCharAtCaretPosition(TCHAR Value)
{
	if (bReadOnly)return false;
	int32 AgainstCaret = 0;
	const FString AgainstText = GetTextWithoutSelection(AgainstCaret);
	if (!HasRoomForMoreChars(AgainstText.Len(), 1))return false;
	if (IsValidChar(Value, AgainstText, AgainstCaret))
	{
		const bool bAnyDeleted = DeleteSelection(false);
		if (!bAnyDeleted)PushUndoSnapshot();
		InsertCharAtCaretPosition(Value);
		UpdateAfterTextChange(true);
		return true;
	}
	return false;
}

void UUITextInput::UpdateAfterTextChange(bool InFireEvent)
{
	UpdateCaretPosition();
	UpdateUITextComponent();
	UpdatePlaceHolderComponent();
	//the composition run moves with every character the IME writes, so it is re-measured here rather
	//than only when the IME announces a new range
	if (CompositionCharLength > 0)
	{
		UpdateCompositionUnderline();
	}
	if (InFireEvent)
	{
		FireOnValueChangedEvent();
	}
}

FString UUITextInput::GetReplaceText()const
{
	FString replaceText;
	if (InputType == EUITextInputType::Password
		|| DisplayType == EUITextInputDisplayType::Password)
	{
		int len = Text.Len();
		replaceText.Reset(len);
		auto psChar = PasswordChar[0];
		for (int i = 0; i < len; i++)
		{
			replaceText.AppendChar(psChar);
		}
	}
	else
	{
		replaceText = Text;
	}
	return replaceText;
}

void UUITextInput::MoveCaret(int32 moveType, bool withSelection)
{
	auto uiText = TextVisual;
	auto originText = uiText->GetText();
	auto replaceText = GetReplaceText();
	uiText->SetText(FText::FromString(replaceText));

	auto CaretPosition3D = CaretWidget->GetRelativeLocation();
	auto CaretPosition = FVector2f(CaretPosition3D.Y, CaretPosition3D.Z);
	if (uiText->MoveCaret(moveType, CaretPositionIndex, CaretPositionLineIndex, CaretPosition))
	{
		UpdateCaretPosition(!withSelection);
		UpdateUITextComponent();

		if (withSelection)
		{
			uiText->GetSelectionProperty(PressCaretPositionIndex - VisibleCaretStartIndex, CaretPositionIndex - VisibleCaretStartIndex, SelectionPropertyArray);
			UpdateSelection();
		}
		else
		{
			PressCaretPositionIndex = CaretPositionIndex;
		}
	}
	else
	{
		uiText->SetText(originText);
	}
}

void UUITextInput::MoveCaretByWord(int32 InDirection, bool withSelection)
{
	if (!DreamTextInputLocal::CanMapCaretIndices(TextVisual))return;
	// Built on the single-step move rather than beside it, so word motion inherits the caret /
	// line-index / scroll bookkeeping MoveCaret already does instead of keeping a second copy of it.
	const int32 MoveType = (InDirection < 0) ? 0 : 1;
	auto IsWordChar = [](TCHAR c) { return FChar::IsAlnum(c) || c == '_'; };
	auto CharAcross = [&](int32 InCaretIndex)->TCHAR
	{
		//the character the caret would step over, which is the one BEFORE it when moving left
		const int32 CharIndex = TextVisual->GetCharIndexByCaretIndex(InCaretIndex) + (InDirection < 0 ? -1 : 0);
		return (CharIndex >= 0 && CharIndex < Text.Len()) ? Text[CharIndex] : TEXT('\0');
	};

	TextVisual->SetText(FText::FromString(GetReplaceText()));
	//first skip the run of separators next to the caret, then the run of word characters: this is
	//what every editor's Ctrl+Arrow does, and it is why one press can cross " , " and land on a word
	bool bSeenWordChar = false;
	for (int32 Step = 0; Step < Text.Len() + 1; Step++)
	{
		const int32 BeforeIndex = CaretPositionIndex;
		const TCHAR Across = CharAcross(CaretPositionIndex);
		if (Across == TEXT('\0') && Step > 0)break;
		const bool bIsWordChar = IsWordChar(Across);
		if (bIsWordChar)
		{
			bSeenWordChar = true;
		}
		else if (bSeenWordChar)
		{
			break;//the word ended
		}
		MoveCaret(MoveType, withSelection);
		if (CaretPositionIndex == BeforeIndex)break;//hit an edge
	}
}
int32 UUITextInput::GetPageLineCount()const
{
	if (!bAllowMultiLine)return 1;
	if (!TextVisual.IsValid())return 1;
	//one screenful is however many lines the clip area shows; the field is the clip area's parent
	const float LineHeight = (TextVisual->GetFont() != nullptr) ? TextVisual->GetFont()->GetLineHeight(TextVisual->GetFontSize()) : 0.0f;
	if (LineHeight <= 0.0f)return 1;
	const UDreamWidget* ClipWidget = TextVisual->GetWidget() != nullptr ? TextVisual->GetWidget()->GetParent() : nullptr;
	const float VisibleHeight = (ClipWidget != nullptr) ? ClipWidget->GetHeight() : 0.0f;
	return FMath::Max(1, FMath::FloorToInt(VisibleHeight / LineHeight));
}
void UUITextInput::MoveCaretByPage(int32 InDirection, bool withSelection)
{
	if (!TextVisual.IsValid())return;
	//a single-line field has nowhere to page to; Home/End is the whole of its vertical world
	if (!bAllowMultiLine)
	{
		MoveCaret(InDirection < 0 ? 4 : 5, withSelection);
		return;
	}
	const int32 LineCount = GetPageLineCount();
	const int32 MoveType = (InDirection < 0) ? 2 : 3;
	for (int32 i = 0; i < LineCount; i++)
	{
		const int32 BeforeLineIndex = CaretPositionLineIndex;
		MoveCaret(MoveType, withSelection);
		if (CaretPositionLineIndex == BeforeLineIndex)break;//top or bottom
	}
}

void UUITextInput::PushUndoSnapshot()
{
	if (!bAllowUndoRedo)return;
	//an edit branches the history: whatever was redoable belonged to the branch just abandoned
	RedoStack.Reset();
	if (UndoStack.Num() > 0 && UndoStack.Last().Text == Text)return;//nothing moved
	UndoStack.Add(FTextSnapshot{ Text, CaretPositionIndex });
	const int32 Limit = FMath::Max(1, UndoHistoryLength);
	while (UndoStack.Num() > Limit)
	{
		UndoStack.RemoveAt(0);
	}
}
void UUITextInput::ApplySnapshot(const FTextSnapshot& InSnapshot)
{
	Text = InSnapshot.Text;
	CaretPositionIndex = FMath::Max(0, InSnapshot.CaretPositionIndex);
	PressCaretPositionIndex = CaretPositionIndex;
	UpdateAfterTextChange(true);
}
bool UUITextInput::Undo()
{
	if (!bAllowUndoRedo || bReadOnly)return false;
	if (UndoStack.Num() == 0)return false;
	RedoStack.Add(FTextSnapshot{ Text, CaretPositionIndex });
	const FTextSnapshot Snapshot = UndoStack.Pop(EAllowShrinking::No);
	ApplySnapshot(Snapshot);
	return true;
}
bool UUITextInput::Redo()
{
	if (!bAllowUndoRedo || bReadOnly)return false;
	if (RedoStack.Num() == 0)return false;
	UndoStack.Add(FTextSnapshot{ Text, CaretPositionIndex });
	const FTextSnapshot Snapshot = RedoStack.Pop(EAllowShrinking::No);
	ApplySnapshot(Snapshot);
	return true;
}
void UUITextInput::ClearUndoHistory()
{
	UndoStack.Reset();
	RedoStack.Reset();
}
bool UUITextInput::EnforceMaxLength()
{
	if (MaxLength <= 0)return false;
	if (Text.Len() <= MaxLength)return false;
	Text = Text.Left(MaxLength);
	CaretPositionIndex = FMath::Min(CaretPositionIndex, Text.Len());
	PressCaretPositionIndex = FMath::Min(PressCaretPositionIndex, Text.Len());
	return true;
}

void UUITextInput::FireOnValueChangedEvent()
{
	OnValueChangedCPP.Broadcast(Text);
	OnValueChangedBP.Broadcast(Text);
	OnValueChanged.FireEvent(Text);
}
void UUITextInput::UpdateUITextComponent()
{
	if (TextVisual.IsValid())
	{
		auto Widget = TextVisual->GetWidget();
		if (!Widget->GetRenderCanvas())return;//need render canvas to calculate geometry
		auto replaceText = GetReplaceText();
		//set to replaced text
		TextVisual->SetText(FText::FromString(replaceText));
		
		if (bAllowMultiLine)//multi line, handle out of range chars
		{
			if (auto ClipWidget = TextVisual->GetWidget()->GetParent())
			{
				auto TextWidget = TextVisual->GetWidget();
				auto TextAnchoredPos = TextWidget->GetAnchoredPosition();
				//move TextWidget to visible area
				{
					auto TextBottomPoint = TextWidget->GetLocalSpaceBottom() + TextWidget->GetRelativeLocation().Z;
					auto TextTopPoint = TextWidget->GetLocalSpaceTop() + TextWidget->GetRelativeLocation().Z;
					auto BottomDiff = ClipWidget->GetLocalSpaceBottom() - TextBottomPoint;
					auto TopDiff = TextTopPoint - ClipWidget->GetLocalSpaceTop();
					if (BottomDiff > 0 && TopDiff < 0)
					{
						TextAnchoredPos.Y += BottomDiff;
					}
					else if (TopDiff > 0 && BottomDiff < 0)
					{
						TextAnchoredPos.Y -= TopDiff;
					}
				}
				//move CaretWidget to visible area
				if (CaretWidget.IsValid())
				{
					//use line height instead of caret height, because we want the whole line be visible
					auto LineHeight = TextVisual->GetFont()->GetLineHeight(TextVisual->GetFontSize());
					auto CaretBottomPoint = CaretWidget->GetRelativeLocation().Z - LineHeight * 0.5f;
					auto CaretBottomPointInClipSpace = CaretBottomPoint + TextWidget->GetRelativeLocation().Z;
					auto CaretTopPoint = CaretWidget->GetRelativeLocation().Z + LineHeight * 0.5f;
					auto CaretTopPointInClipSpace = CaretTopPoint + TextWidget->GetRelativeLocation().Z;
					//check bottom edge
					if (CaretBottomPointInClipSpace < ClipWidget->GetLocalSpaceBottom())
					{
						TextAnchoredPos.Y += ClipWidget->GetLocalSpaceBottom() - CaretBottomPointInClipSpace;
					}
					//check top edge, but only when text's size is bigger than clip-area
					else if (TextWidget->GetHeight() > ClipWidget->GetHeight() && CaretTopPointInClipSpace > ClipWidget->GetLocalSpaceTop())
					{
						TextAnchoredPos.Y -= CaretTopPointInClipSpace - ClipWidget->GetLocalSpaceTop();
					}
				}
				TextWidget->SetAnchoredPosition(TextAnchoredPos);
			}
		}
		else//single line, handle out of range chars
		{
			if (auto ClipWidget = TextVisual->GetWidget()->GetParent())
			{
				auto TextWidget = TextVisual->GetWidget();
				auto TextAnchoredPos = TextWidget->GetHorizontalAnchoredPosition();
				//move TextWidget to visible area
				{
					auto TextLeftPoint = TextWidget->GetLocalSpaceLeft() + TextWidget->GetRelativeLocation().Y;
					auto TextRightPoint = TextWidget->GetLocalSpaceRight() + TextWidget->GetRelativeLocation().Y;
					auto LeftDiff = ClipWidget->GetLocalSpaceLeft() - TextLeftPoint;
					auto RightDiff = TextRightPoint - ClipWidget->GetLocalSpaceRight();
					if (LeftDiff > 0 && RightDiff < 0)
					{
						TextAnchoredPos += LeftDiff;
					}
					else if (RightDiff > 0 && LeftDiff < 0)
					{
						TextAnchoredPos -= RightDiff;
					}
				}
				//move CaretWidget to visible area
				if (CaretWidget.IsValid())
				{
					auto CaretCenterPoint = CaretWidget->GetLocalSpaceCenter().X + CaretWidget->GetRelativeLocation().Y;
					auto CaretCenterPointInClipSpace = CaretCenterPoint + TextWidget->GetRelativeLocation().Y;
					//check left edge
					if (CaretCenterPointInClipSpace < ClipWidget->GetLocalSpaceLeft())
					{
						TextAnchoredPos += ClipWidget->GetLocalSpaceLeft() - CaretCenterPointInClipSpace;
					}
					//check right edge, but only when text's size is less than clip-area
					else if (TextWidget->GetWidth() > ClipWidget->GetWidth() && CaretCenterPointInClipSpace > ClipWidget->GetLocalSpaceRight())
					{
						TextAnchoredPos -= CaretCenterPointInClipSpace - ClipWidget->GetLocalSpaceRight();
					}
				}
				TextWidget->SetHorizontalAnchoredPosition(TextAnchoredPos);
			}
		}
	}
}
void UUITextInput::UpdatePlaceHolderComponent()
{
	if (bInputActive || !Text.IsEmpty())
	{
		if (PlaceHolder.IsValid())
		{
			PlaceHolder->SetWidgetActive(false);
		}
	}
	else
	{
		if (PlaceHolder.IsValid())
		{
			PlaceHolder->SetWidgetActive(true);
		}
	}
}

void UUITextInput::UpdateCaretPosition(bool InHideSelection)
{
	if (!bInputActive)
	{
		if (CaretWidget.IsValid())
		{
			CaretWidget->SetWidgetActive(false);
		}
	}
	else
	{
		FVector2f caretPos;
		int tempCaretPositionLineIndex = 0;
		int tempVisibleCaretStartIndex = 0;
		int tempCaretPositionIndex = CaretPositionIndex - VisibleCaretStartIndex;
		TextVisual->FindCaretByIndex(tempCaretPositionIndex, caretPos, tempCaretPositionLineIndex, tempVisibleCaretStartIndex);
		CaretPositionLineIndex = tempCaretPositionLineIndex + VisibleCaretStartLineIndex;

		UpdateCaretPosition(caretPos, InHideSelection);
	}
}
void UUITextInput::UpdateCaretPosition(FVector2f InCaretPosition, bool InHideSelection)
{
	if (!TextVisual.IsValid())return;
	if (!CaretWidget.IsValid())
	{
		CaretWidget = NewObject<UDreamWidget>(this->GetWidget()->GetOuter());
		CaretWidget->SetParent(TextVisual->GetWidget(), false);
		CaretWidget->SetDisplayName(TEXT("Caret"));
		CaretWidget->SetAnchorData(FDreamUIAnchorData{FVector2D(0.5, 0.5)
			, FVector2D(0, 0.5), FVector2D(0, 0.5)
			, FVector2D::Zero(), FVector2D(CaretWidth, TextVisual->GetFontSize())});
		auto CaretVisual = CaretWidget->CreateNewVisual<UDreamImage>();
		CaretVisual->SetColor(CaretColor);
		CaretVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
	}
	CaretWidget->SetRelativeLocation(FVector(0, InCaretPosition.X, InCaretPosition.Y));
	CaretWidget->SetWidgetActive(true);
	
	//force display caret
	NextCaretBlinkTime = 0.8f;
	ElapseTime = 0.0f;
	CaretWidget->SetRenderOpacity(1.0f);

	if (InHideSelection) HideSelectionMask();//if use caret, then hide selection mask
}
void UUITextInput::UpdateSelection()
{
	if (!TextVisual.IsValid())return;
	int32 createdSelectionMaskCount = SelectionMaskObjectArray.Num();
	if (SelectionPropertyArray.Num() > createdSelectionMaskCount)//need more selection mask object
	{
		int32 needToCreateSelectionMaskCount = SelectionPropertyArray.Num() - createdSelectionMaskCount;
		for (int32 i = 0; i < needToCreateSelectionMaskCount; i++)
		{
			auto SpriteWidget = NewObject<UDreamWidget>(this->GetWidget()->GetOuter());
			SpriteWidget->SetParent(TextVisual->GetWidget(), false);
			SpriteWidget->SetDisplayName(FString::Printf(TEXT("Selection%d"), i + createdSelectionMaskCount));
			SpriteWidget->SetHeight(TextVisual->GetFontSize());
			SpriteWidget->SetPivot(FVector2D(0, 0.5f));
			auto SelectionVisual = SpriteWidget->CreateNewVisual<UDreamImage>();
			SelectionVisual->SetColor(SelectionColor);
			SelectionVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
			SelectionMaskObjectArray.Add(SelectionVisual);
		}
	}
	else if (SelectionPropertyArray.Num() < createdSelectionMaskCount)//hide extra selection mask object
	{
		int32 needToHideTextureCount = createdSelectionMaskCount - SelectionPropertyArray.Num();
		for (int32 i = 0; i < needToHideTextureCount; i++)
		{
			auto SelectionVisual = SelectionMaskObjectArray[i + SelectionPropertyArray.Num()];
			if (SelectionVisual.IsValid())
			{
				SelectionVisual->GetWidget()->SetWidgetActive(false);
			}
		}
	}

	for (int32 i = 0; i < SelectionPropertyArray.Num(); i++)
	{
		auto SelectionVisual = SelectionMaskObjectArray[i];
		if (SelectionVisual.IsValid())
		{
			SelectionVisual->GetWidget()->SetWidgetActive(true);
			auto& selectionProperty = SelectionPropertyArray[i];
			SelectionVisual->GetWidget()->SetRelativeLocation(FVector(0, selectionProperty.Pos.X, selectionProperty.Pos.Y));
			SelectionVisual->GetWidget()->SetWidth(selectionProperty.Size);
		}
	}
}
void UUITextInput::SetCompositionRange(int32 InBeginCharIndex, int32 InLength)
{
	CompositionBeginCharIndex = FMath::Clamp(InBeginCharIndex, 0, Text.Len());
	CompositionCharLength = FMath::Clamp(InLength, 0, Text.Len() - CompositionBeginCharIndex);
	UpdateCompositionUnderline();
}
void UUITextInput::UpdateCompositionUnderline()
{
	if (!TextVisual.IsValid())return;
	// Composition text is written straight into Text, so without this it is pixel-identical to text
	// the player has already committed -- and with an IME open that is most of what is on screen.
	// The run geometry comes from the same GetSelectionProperty the selection highlight uses; the
	// only difference is the strip's height and where in the line it sits.
	CompositionPropertyArray.Reset();
	if (CompositionCharLength > 0 && bInputActive && DreamTextInputLocal::CanMapCaretIndices(TextVisual))
	{
		TextVisual->SetText(FText::FromString(GetReplaceText()));
		const int32 BeginCaretIndex = TextVisual->GetCaretIndexByCharIndex(CompositionBeginCharIndex) - VisibleCaretStartIndex;
		const int32 EndCaretIndex = TextVisual->GetCaretIndexByCharIndex(CompositionBeginCharIndex + CompositionCharLength) - VisibleCaretStartIndex;
		TextVisual->GetSelectionProperty(BeginCaretIndex, EndCaretIndex, CompositionPropertyArray);
	}

	const int32 CreatedCount = CompositionUnderlineObjectArray.Num();
	if (CompositionPropertyArray.Num() > CreatedCount)
	{
		for (int32 i = CreatedCount; i < CompositionPropertyArray.Num(); i++)
		{
			auto StripWidget = NewObject<UDreamWidget>(this->GetWidget()->GetOuter());
			StripWidget->SetParent(TextVisual->GetWidget(), false);
			StripWidget->SetDisplayName(FString::Printf(TEXT("CompositionUnderline%d"), i));
			StripWidget->SetHeight(CompositionUnderlineThickness);
			StripWidget->SetPivot(FVector2D(0, 0.5f));
			auto StripVisual = StripWidget->CreateNewVisual<UDreamImage>();
			StripVisual->SetColor(CompositionUnderlineColor);
			StripVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
			CompositionUnderlineObjectArray.Add(StripVisual);
		}
	}
	else if (CompositionPropertyArray.Num() < CreatedCount)
	{
		for (int32 i = CompositionPropertyArray.Num(); i < CreatedCount; i++)
		{
			if (CompositionUnderlineObjectArray[i].IsValid())
			{
				CompositionUnderlineObjectArray[i]->GetWidget()->SetWidgetActive(false);
			}
		}
	}
	//half a line below the run's centre, which is where the glyphs' baseline area ends
	const float VerticalOffset = TextVisual->GetFontSize() * -0.5f;
	for (int32 i = 0; i < CompositionPropertyArray.Num(); i++)
	{
		auto StripVisual = CompositionUnderlineObjectArray[i];
		if (!StripVisual.IsValid())continue;
		auto& Property = CompositionPropertyArray[i];
		StripVisual->GetWidget()->SetWidgetActive(true);
		StripVisual->GetWidget()->SetHeight(CompositionUnderlineThickness);
		StripVisual->SetColor(CompositionUnderlineColor);
		StripVisual->GetWidget()->SetRelativeLocation(FVector(0, Property.Pos.X, Property.Pos.Y + VerticalOffset));
		StripVisual->GetWidget()->SetWidth(Property.Size);
	}
}
void UUITextInput::HideCompositionUnderline()
{
	CompositionBeginCharIndex = 0;
	CompositionCharLength = 0;
	CompositionPropertyArray.Reset();
	for (auto& StripVisual : CompositionUnderlineObjectArray)
	{
		if (StripVisual.IsValid())
		{
			StripVisual->GetWidget()->SetWidgetActive(false);
		}
	}
}
void UUITextInput::HideSelectionMask()
{
	for (int i = 0; i < SelectionMaskObjectArray.Num(); i++)
	{
		auto SelectionVisual = SelectionMaskObjectArray[i];
		if (SelectionVisual.IsValid())
		{
			SelectionMaskObjectArray[i]->GetWidget()->SetWidgetActive(false);
		}
	}
	SelectionPropertyArray.Reset();//clear selection mask
	//TextInputMethodContext->SetSelectionRange(0, 0, ITextInputMethodContext::ECaretPosition::Beginning);
}

void UUITextInput::OnEnable()
{
	Super::OnEnable();
	DeactivateInput();
}

void UUITextInput::OnInteractableChanged(bool Interactable)
{
	Super::OnInteractableChanged(Interactable);
	DeactivateInput();
}

void UUITextInput::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	this->UpdateAfterTextChange(false);//if size change, need to recalculate text input area
}

bool UUITextInput::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	Super::OnPointerEnter_Implementation(EventData);
	if (bAutoActivateInputWhenNavigateIn)
	{
		if (EventData->InputType == EDreamUIPointerInputType::Navigation)
		{
			ActivateInput(EventData);
		}
	}
	// Claimed on the widget, not written straight into the player controller.
	// UDreamPointerInputModule::ProcessPointerEnterExit resolves the cursor from the hover stack in
	// an ON_SCOPE_EXIT -- so AFTER this handler -- and writes the result unconditionally, which
	// meant a direct write here was replaced by Default before the frame was out and the I-beam
	// never appeared at all. The widget's Cursor property is what that resolver reads.
	//
	// Nothing to undo on exit, and OnPointerExit accordingly no longer touches the cursor: a widget
	// the pointer has left is not in the stack the resolver walks.
	if (UDreamWidget* OwnWidget = this->GetWidget())
	{
		if (OwnWidget->GetCursor() == EMouseCursor::Default)//Default is "no opinion"; an authored cursor wins
		{
			OwnWidget->SetCursor(EMouseCursor::TextEditBeam);
		}
	}
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	Super::OnPointerExit_Implementation(EventData);
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerSelect_Implementation(UDreamBaseEventData* EventData)
{
	Super::OnPointerSelect_Implementation(EventData);
	//ActivateInput(EventData);//handled at PointerClick
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	Super::OnPointerDeselect_Implementation(EventData);
	DeactivateInput();
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	// A right click on a field that is not being edited starts editing it AND opens the menu, which
	// is what every text field does -- a player who right-clicks a field wants the menu, not two
	// clicks. The left-click road is unchanged.
	const bool bIsSecondaryClick = IsValid(EventData) && EventData->MouseButtonType == EDreamUIMouseButtonType::Right;
	if (!bInputActive)//need active input
	{
		ActivateInput(EventData);
	}
	if (bIsSecondaryClick)
	{
		bPointerHeldForContextMenu = false;
		ShowContextMenu();
	}
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	// Double click selects the word it lands on, the one text-field gesture every other editor has.
	// The event system already decides what counts as a double click (its own DoubleClickTime, same
	// widget, same button, the second press within the drag threshold of the first), so this asks for
	// that answer rather than keeping a second clock. It arrives at the SECOND press and in place of
	// that press's down (Slate's routing), so nothing has put the caret under this press: it is still
	// where the FIRST press left it. The two presses of a double click are near each other, not on one
	// spot, and a drag threshold's worth apart can already be the next word over. Slate looks the word
	// up at the double click's own position (SEditableText::OnMouseButtonDoubleClick ->
	// FSlateEditableTextLayout::HandleMouseButtonDoubleClick -> SelectWordAt(the event's screen
	// position)), so the caret is put under this press first, exactly as a down would put it, and the
	// word is taken from there.
	if (bInputActive)
	{
		if (TextVisual != nullptr && IsValid(EventData))
		{
			//caret position at this press, UIText space
			auto PressCaretPosition = FVector2f(0, 0);
			TextVisual->FindCaretByWorldPosition(EventData->GetWorldPointInPlane(), PressCaretPosition, PressCaretPositionLineIndex, PressCaretPositionIndex);
			PressCaretPositionIndex = PressCaretPositionIndex + VisibleCaretStartIndex;
			CaretPositionIndex = PressCaretPositionIndex;
			PressCaretPositionLineIndex = PressCaretPositionLineIndex + VisibleCaretStartLineIndex;
			CaretPositionLineIndex = PressCaretPositionLineIndex;
			UpdateCaretPosition(PressCaretPosition);
			UpdateUITextComponent();
		}
		SelectWordAtCaret();
	}
	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	bPointerHeldForContextMenu = false;//a press that moved is a selection drag, not a hold
	if (bInputActive)
	{
		return AllowEventBubbleUp;
	}
	else
	{
		return true;
	}
}
bool UUITextInput::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (bInputActive)
	{
		if (TextVisual != nullptr)
		{
			FVector2f caretPosition;
			int tempCaretPositionLineIndex;
			TextVisual->FindCaretByWorldPosition(EventData->GetWorldPointInPlane(), caretPosition, tempCaretPositionLineIndex, CaretPositionIndex);
			auto displayCaretCount = TextVisual->GetLastCaret() + 1;

			//@todo:caret move speed depend on drag distance
			if (CaretPositionIndex == 0)//caret position at left most
			{
				CaretPositionIndex = VisibleCaretStartIndex - 1;//move caret to left
				CaretPositionIndex = FMath::Max(CaretPositionIndex, 0);
				if (CaretPositionIndex < VisibleCaretStartIndex)
				{
					if (CaretPositionLineIndex > 0)
					{
						CaretPositionLineIndex--;
					}
				}
			}
			else if (CaretPositionIndex + 1 >= displayCaretCount)//caret position at right most
			{
				CaretPositionIndex = CaretPositionIndex + VisibleCaretStartIndex + 1;//move caret to right
				CaretPositionIndex = FMath::Min(CaretPositionIndex, VisibleCaretStartIndex + displayCaretCount);
				if (CaretPositionIndex - VisibleCaretStartIndex > displayCaretCount)//if caret is more than visible text
				{
					CaretPositionLineIndex++;
				}
			}
			else//not drag out-of-range
			{
				CaretPositionIndex += VisibleCaretStartIndex;
			}

			//selectionStartCaretIndex may out of range, need clamp.
			TextVisual->GetSelectionProperty(PressCaretPositionIndex - VisibleCaretStartIndex, CaretPositionIndex - VisibleCaretStartIndex, SelectionPropertyArray);
			UpdateSelection();
			UpdateCaretPosition(false);
			UpdateUITextComponent();
		}
		return AllowEventBubbleUp;
	}
	else
	{
		return true;
	}
}
bool UUITextInput::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (bInputActive)
	{
		return AllowEventBubbleUp;
	}
	else
	{
		return true;
	}
}
bool UUITextInput::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	Super::OnPointerDown_Implementation(EventData);
	// Where a menu would open, remembered on every press so it is still right when the menu is asked
	// for later -- by a long press, or by a right click whose own event the module reports as a click.
	if (IsValid(EventData))
	{
		ContextMenuAnchorWorldPoint = EventData->GetWorldPointInPlane();
		bHasContextMenuAnchor = true;
		// A held press is touch's right click: there is no second button to press, so the gesture is
		// time. The pointer module reports a press and a release, not a hold, so the field times it.
		bPointerHeldForContextMenu = bAllowContextMenu && EventData->MouseButtonType == EDreamUIMouseButtonType::Left;
		PointerHeldStartTime = FPlatformTime::Seconds();
	}
	if (bInputActive)//if already active, then put caret position at mouse position
	{
		if (TextVisual != nullptr)
		{
			//caret position when press, UIText space
			auto PressCaretPosition = FVector2f(0, 0);
			TextVisual->FindCaretByWorldPosition(EventData->GetWorldPointInPlane(), PressCaretPosition, PressCaretPositionLineIndex, PressCaretPositionIndex);
			PressCaretPositionIndex = PressCaretPositionIndex + VisibleCaretStartIndex;
			CaretPositionIndex = PressCaretPositionIndex;
			PressCaretPositionLineIndex = PressCaretPositionLineIndex + VisibleCaretStartLineIndex;
			CaretPositionLineIndex = PressCaretPositionLineIndex;
			UpdateCaretPosition(PressCaretPosition);
			UpdateUITextComponent();
		}
	}

	return AllowEventBubbleUp;
}
bool UUITextInput::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	Super::OnPointerUp_Implementation(EventData);
	bPointerHeldForContextMenu = false;//released before the hold matured: an ordinary click
	return AllowEventBubbleUp;
}
void UUITextInput::ActivateInput(UDreamPointerEventData* EventData)
{
	if (TextVisual == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d TextActor is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	else
	{
		UpdateUITextComponent();
	}
	if (bInputActive)
	{
		//if already active, then update caret position
		TextVisual->SetText(FText::FromString(GetReplaceText()));
		CaretPositionIndex = TextVisual->GetLastCaret();
		UpdateCaretPosition();
		UpdateUITextComponent();
		return;
	}
	const bool bActivatedByPointer = IsValid(EventData) && EventData->InputType == EDreamUIPointerInputType::Pointer;
	if (FPlatformApplicationMisc::RequiresVirtualKeyboard())
	{
		// OnFocusByPointer means exactly that: an activation nobody touched the field for -- navigated
		// into with a pad, or a Blueprint calling ActivateInput -- leaves the keyboard down, which is
		// what a screen driving its own entry asked for by choosing that trigger. The edit itself
		// still begins, so a host feeding characters (HandleCharacterInput) keeps working.
		if (VirtualKeyboardTrigger == EVirtualKeyboardTrigger::OnAllFocusEvents || bActivatedByPointer)
		{
			if (!VirtualKeyboardEntry.IsValid())
			{
				VirtualKeyboardEntry = FVirtualKeyboardEntry::Create(this);
			}
			FSlateApplication::Get().ShowVirtualKeyboard(true, 0, VirtualKeyboardEntry);
		}
	}
	else
	{
		ITextInputMethodSystem* const TextInputMethodSystem = FSlateApplication::Get().GetTextInputMethodSystem();
		if (TextInputMethodSystem)
		{
			if (!TextInputMethodContext.IsValid())
			{
				TextInputMethodContext = FTextInputMethodContext::Create(this);
			}
			TextInputMethodChangeNotifier = TextInputMethodSystem->RegisterContext(TextInputMethodContext.ToSharedRef());
			TextInputMethodSystem->ActivateContext(TextInputMethodContext.ToSharedRef());
		}
		if (TextInputMethodChangeNotifier.IsValid())
		{
			TextInputMethodChangeNotifier->NotifyLayoutChanged(ITextInputMethodChangeNotifier::ELayoutChangeType::Changed);
		}
		WarnOnceIfNoCharacterEventSource();
	}
	bInputActive = true;
	// The edit takes the paragraph's overflow back off the display policy: an ellipsis in the middle
	// of a value somebody is typing into would hide the very characters the caret is standing on.
	PushOverflowToVisual();
	bSubmittedThisActivation = false;
	// What a cancel puts back. Taken here, before a single character of this session exists, because
	// "the value before the edit" is a fact about the MOMENT the edit started and nothing later in
	// the session can reconstruct it.
	TextAtActivation = Text;
	//the target of RouteCharacterInputToActiveInput: the one field that owns the keyboard right now
	ActiveTextInput = this;
	SetCanExecuteTick(true);
	//caret and selection
	if (Text.Len() == 0)//if no text, use caret
	{
		CaretPositionIndex = 0;
		PressCaretPositionIndex = 0;
		UpdateCaretPosition();
		UpdateUITextComponent();
	}
	else if (bSelectAllWhenActivateInput)//select all
	{
		SelectAll();
	}
	else if (IsValid(EventData) && EventData->InputType == EDreamUIPointerInputType::Pointer)
	{
		// The click that activates a field is also the click that says where in it to type. OnPointerDown saw
		// that press first but skipped its own placement because the field was not active yet, so this is the
		// last place that still holds the press position. Navigation events are excluded on purpose: they
		// carry no meaningful world point, so asking one where the caret goes gives a position off the text.
		//caret position when press, UIText space
		auto PressCaretPosition = FVector2f(0, 0);
		TextVisual->FindCaretByWorldPosition(EventData->GetWorldPointInPlane(), PressCaretPosition, PressCaretPositionLineIndex, PressCaretPositionIndex);
		PressCaretPositionIndex = PressCaretPositionIndex + VisibleCaretStartIndex;
		CaretPositionIndex = PressCaretPositionIndex;
		PressCaretPositionLineIndex = PressCaretPositionLineIndex + VisibleCaretStartLineIndex;
		CaretPositionLineIndex = PressCaretPositionLineIndex;
		UpdateCaretPosition(PressCaretPosition);
		UpdateUITextComponent();
	}
	else
	{
		// Activated with nobody naming a position -- navigated into, or a Blueprint calling this directly.
		// The chain has to end in a caret anyway: with no final branch, turning bSelectAllWhenActivateInput
		// off leaves the caret on whatever index the previous session abandoned it at, and the first key
		// typed lands in the middle of the text. End of text is the same answer the already-active path
		// above gives.
		// Unless the field was asked NOT to move the caret on gaining focus (UMG's
		// IsCaretMovedWhenGainFocus), in which case the abandoned index IS the answer -- clamped,
		// because the text may have been replaced from code since the caret was last meaningful.
		CaretPositionIndex = bIsCaretMovedWhenGainFocus
			? TextVisual->GetLastCaret()
			: FMath::Clamp(CaretPositionIndex, 0, TextVisual->GetLastCaret());
		PressCaretPositionIndex = CaretPositionIndex;
		UpdateCaretPosition();
		UpdateUITextComponent();
	}

	BindKeys();
	UpdatePlaceHolderComponent();
	//set is selected
	if (auto EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, IsValid(EventData) ? EventData->UserIndex : 0))
	{
		if (auto Widget = GetWidget())
		{
			if (IsValid(EventData))
			{
				EventSystem->SetSelectWidget(Widget, EventData);
			}
			else
			{
				EventSystem->SetSelectComponentWithDefault(Widget);
			}
		}
	}
	//fire event
	OnInputActivateCPP.Broadcast(bInputActive);
	OnInputActivateBP.Broadcast(bInputActive);
	OnInputActivate.FireEvent(bInputActive);
}

void UUITextInput::BindKeys()
{
	if (!InputComponentAgent.IsValid())
	{
		InputComponentAgent = this->GetWidget()->GetOuter()->GetWorld()->SpawnActor(AActor::StaticClass());
#if WITH_EDITOR
		InputComponentAgent->SetActorLabel(FString::Printf(TEXT("%s_InputComponentAgent"), *GetWidget()->GetDisplayName()));
#endif
		InputComponentAgent->AutoReceiveInput = EAutoReceiveInput::Player0;
		InputComponentAgent->PreInitializeComponents();
	}

	static TArray<FKey> AllKeys = {
	EKeys::BackSpace,
	EKeys::Tab,
	EKeys::Enter,
	EKeys::Pause,

	EKeys::CapsLock,
	// Escape is deliberately absent. This InputComponent is pushed on top of the stack and consumes what it
	// binds, so binding Escape sinks it for everything below while any field is being edited -- not just the
	// built-in Back, but whatever action a project put on Escape, since the action router is offered every
	// key before Back is. Nothing is lost by leaving it out: UDreamUINavigationStack::HandleBack already
	// ends the edit on the focused field first and only pops a screen if there was no edit to end.
	EKeys::SpaceBar,
	EKeys::PageUp,
	EKeys::PageDown,
	EKeys::End,
	EKeys::Home,

	EKeys::Left,
	EKeys::Up,
	EKeys::Right,
	EKeys::Down,

	EKeys::Insert,
	EKeys::Delete,

	EKeys::Zero,
	EKeys::One,
	EKeys::Two,
	EKeys::Three,
	EKeys::Four,
	EKeys::Five,
	EKeys::Six,
	EKeys::Seven,
	EKeys::Eight,
	EKeys::Nine,

	EKeys::A,
	EKeys::B,
	EKeys::C,
	EKeys::D,
	EKeys::E,
	EKeys::F,
	EKeys::G,
	EKeys::H,
	EKeys::I,
	EKeys::J,
	EKeys::K,
	EKeys::L,
	EKeys::M,
	EKeys::N,
	EKeys::O,
	EKeys::P,
	EKeys::Q,
	EKeys::R,
	EKeys::S,
	EKeys::T,
	EKeys::U,
	EKeys::V,
	EKeys::W,
	EKeys::X,
	EKeys::Y,
	EKeys::Z,

	EKeys::NumPadZero,
	EKeys::NumPadOne,
	EKeys::NumPadTwo,
	EKeys::NumPadThree,
	EKeys::NumPadFour,
	EKeys::NumPadFive,
	EKeys::NumPadSix,
	EKeys::NumPadSeven,
	EKeys::NumPadEight,
	EKeys::NumPadNine,

	EKeys::Multiply,
	EKeys::Add,
	EKeys::Subtract,
	EKeys::Decimal,
	EKeys::Divide,

	EKeys::LeftShift,
	EKeys::RightShift,
	EKeys::LeftControl,
	EKeys::RightControl,
	EKeys::LeftAlt,
	EKeys::RightAlt,
	EKeys::LeftCommand,
	EKeys::RightCommand,

	EKeys::Semicolon,
	EKeys::Equals,
	EKeys::Comma,
	EKeys::Underscore,
	EKeys::Hyphen,
	EKeys::Period,
	EKeys::Slash,
	EKeys::Tilde,
	EKeys::LeftBracket,
	EKeys::Backslash,
	EKeys::RightBracket,
	EKeys::Apostrophe,

	// The two keys that ask for the edit menu with no pointer: Shift+F10 (Windows' own keyboard
	// shortcut for a context menu) and the pad's Menu button. Both go through IgnoreKeys like the
	// rest, so a project that has its own use for either simply lists it there.
	EKeys::F10,
	EKeys::Gamepad_Special_Right,

	EKeys::Ampersand,
	EKeys::Asterix,
	EKeys::Caret,
	EKeys::Colon,
	EKeys::Dollar,
	EKeys::Exclamation,
	EKeys::LeftParantheses,
	EKeys::RightParantheses,
	EKeys::Quote,
	};

	auto InputComp = InputComponentAgent->InputComponent;
	for (auto& Key : AllKeys)
	{
		if (!IgnoreKeys.Contains(Key))
		{
			InputComp->BindKey(Key, EInputEvent::IE_Pressed, this, &UUITextInput::AnyKeyPressed);
			InputComp->BindKey(Key, EInputEvent::IE_Repeat, this, &UUITextInput::AnyKeyPressed);
		}
	}
}
void UUITextInput::UnbindKeys()
{
	if (!InputComponentAgent.IsValid())
		return;
	InputComponentAgent->Destroy();
}
void UUITextInput::DeactivateInput(bool InFireEvent)
{
	if (!bInputActive)return;
	ITextInputMethodSystem* const TextInputMethodSystem = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetTextInputMethodSystem() : nullptr;
	if (TextInputMethodSystem)
	{
		if (TextInputMethodContext.IsValid())
		{
			TSharedRef<FTextInputMethodContext> TextInputMethodContextRef = TextInputMethodContext.ToSharedRef();

			if (TextInputMethodSystem->IsActiveContext(TextInputMethodContextRef))
			{
				TextInputMethodSystem->DeactivateContext(TextInputMethodContextRef);
			}

			TextInputMethodSystem->UnregisterContext(TextInputMethodContextRef);
		}
	}
	if (FSlateApplication::IsInitialized() && FPlatformApplicationMisc::RequiresVirtualKeyboard())
	{
		FSlateApplication::Get().ShowVirtualKeyboard(false, 0);
	}
	bInputActive = false;
	// And hands it back to the display policy, which is the state a field spends nearly all its life
	// in -- the only state an ellipsis was ever meant to describe.
	PushOverflowToVisual();
	if (ActiveTextInput.Get() == this)
	{
		ActiveTextInput = nullptr;
	}
	SetCanExecuteTick(false);
	//hide caret
	if (CaretWidget.IsValid())
	{
		CaretWidget->SetWidgetActive(false);
	}
	//hide selection
	HideSelectionMask();
	HideCompositionUnderline();
	HideContextMenu();

	UnbindKeys();
	UpdatePlaceHolderComponent();
	// The edit ended without an Enter: clicked away, navigated away, Back/Escape ended it, the
	// virtual keyboard was dismissed. UMG reports that moment through OnTextCommitted and DreamGUI
	// reported nothing, so a field the player filled in and clicked out of never told anyone. An
	// Enter that already submitted this activation does not submit twice.
	if (InFireEvent && bSubmitWhenDeactivate && !bSubmittedThisActivation)
	{
		Submit();
	}
	bSubmittedThisActivation = false;
	//fire event
	if (InFireEvent)
	{
		OnInputActivateCPP.Broadcast(bInputActive);
		OnInputActivateBP.Broadcast(bInputActive);
		OnInputActivate.FireEvent(bInputActive);
	}
}
UDreamText* UUITextInput::GetTextComponent()const
{
	if (TextVisual != nullptr)
	{
		return TextVisual.Get();
	}
	return nullptr;
}
const FString& UUITextInput::GetText()const
{
	return Text;
}
void UUITextInput::SetText(const FString& InText, bool InFireEvent)
{
	if (Text != InText)
	{
		// Each character is checked against the string being BUILT, at its own end, not against the
		// text the field happens to hold right now. Every type rule is positional -- "only one dot",
		// "only one @", "minus only at the front" -- so validating a wholesale replacement against
		// the old value refuses characters the new value has every right to: a DecimalNumber field
		// showing "3.5" turned SetText("1.2") into "12", because the OLD text already had a dot.
		// Callers used to work around it by pushing an empty string first (UDreamSpinBox did); that
		// workaround is now redundant rather than required.
		FString TempText;
		for (int i = 0; i < InText.Len(); i++)
		{
			TCHAR c = InText[i];
			if (!HasRoomForMoreChars(TempText.Len(), 1))break;
			if (IsValidChar(c, TempText, TempText.Len()))
			{
				TempText.AppendChar(c);
			}
		}
		if (bAllowMultiLine)
		{
			Text = TempText;
		}
		else
		{
			Text = TempText.Replace(TEXT("\n"), TEXT("")).Replace(TEXT("\t"), TEXT(""));
		}

		CaretPositionIndex = 0;
		PressCaretPositionIndex = 0;
		//a wholesale replacement is not an edit step: the history described a different string
		ClearUndoHistory();
		UpdateAfterTextChange(InFireEvent);
	}
}

void UUITextInput::SetText(const FString& InText)
{
	SetText(InText, true);
}

void UUITextInput::SetTextWithoutNotify(const FString& InText)
{
	SetText(InText, false);
}

void UUITextInput::RevalidateText()
{
	// A rule change has to be applied to what is already in the field, or the field holds text its
	// own rules forbid: switching a field holding "abc" to IntegerNumber left "abc" sitting there,
	// and the very next keystroke was validated against a string the type says cannot exist.
	FString TempText;
	for (int i = 0; i < Text.Len(); i++)
	{
		const TCHAR c = Text[i];
		if (!HasRoomForMoreChars(TempText.Len(), 1))break;
		if (IsValidChar(c, TempText, TempText.Len()))
		{
			TempText.AppendChar(c);
		}
	}
	const bool bChanged = TempText != Text;
	Text = TempText;
	CaretPositionIndex = 0;
	PressCaretPositionIndex = 0;
	ClearUndoHistory();
	UpdateAfterTextChange(bChanged);
}
void UUITextInput::SetInputType(EUITextInputType Value)
{
	if (InputType != Value)
	{
		InputType = Value;
		RevalidateText();
	}
}
void UUITextInput::SetCustomValidation(UDreamTextInputCustomValidation* Value)
{
	if (CustomValidation != Value)
	{
		CustomValidation = Value;
		if (InputType == EUITextInputType::Custom)
		{
			RevalidateText();
		}
	}
}
void UUITextInput::SetDisplayType(EUITextInputDisplayType Value)
{
	if (DisplayType != Value)
	{
		DisplayType = Value;
		CaretPositionIndex = 0;
		UpdateUITextComponent();
	}
}
void UUITextInput::SetPasswordChar(const FString& Value)
{
	if (PasswordChar != Value)
	{
		if (Value.Len() != 1)
		{
			return;
		}
		PasswordChar = Value;
		if (InputType == EUITextInputType::Password || DisplayType == EUITextInputDisplayType::Password)
		{
			UpdateUITextComponent();
		}
	}
}
void UUITextInput::SetAllowMultiLine(bool Value)
{
	if (bAllowMultiLine != Value)
	{
		bAllowMultiLine = Value;
		// The overflow type IS the wrap switch: DreamTextLayout only computes break opportunities
		// when OverflowType == VerticalOverflow. Only SetTextVisual used to push it, so whether a
		// multiline field wrapped came down to which of the two setters a caller happened to call
		// last -- and UDreamTextInput calls SetTextVisual first (WireParts) and SetAllowMultiLine
		// second (ApplyStyle), so its multiline fields never wrapped at all. Now there is one writer.
		PushOverflowToVisual();
		UpdateAfterTextChange(false);
	}
}
void UUITextInput::SetMultiLineSubmitFunctionKeys(const TArray<FKey>& Value)
{
	MultiLineSubmitFunctionKeys = Value;
}
void UUITextInput::SetPlaceHolder(UDreamWidget* Value)
{
	PlaceHolder = Value;
}
void UUITextInput::SetTextVisual(UDreamText* Value)
{
	if (TextVisual != Value)
	{
		TextVisual = Value;
		if (TextVisual != nullptr)
		{
			// The same normalization PostEditChangeProperty applies when the designer rewires it:
			// overflow follows the line mode, and the text a field edits must not vary by culture.
			PushOverflowToVisual();
		}
		UpdateUITextComponent();
		UpdatePlaceHolderComponent();
	}
}

void UUITextInput::PushOverflowToVisual()
{
	if (!TextVisual.IsValid())
	{
		return;
	}
	// The line mode's own overflow, which is the only thing the wrap and the caret's visible window
	// will accept. It wins whenever the field is being edited, and it is the whole answer for a field
	// that states no policy -- which is every field that exists today.
	const EDreamUITextOverflowType LineMode = bAllowMultiLine
		? EDreamUITextOverflowType::VerticalOverflow
		: EDreamUITextOverflowType::HorizontalOverflow;
	const bool bPolicySpeaks = !bInputActive && OverflowPolicy != ETextOverflowPolicy::Clip;
	TextVisual->SetOverflowType(bPolicySpeaks ? EDreamUITextOverflowType::Ellipsis : LineMode);
}

void UUITextInput::SetOverflowPolicy(ETextOverflowPolicy Value)
{
	if (OverflowPolicy == Value)
	{
		return;
	}
	OverflowPolicy = Value;
	// At once, because the state it speaks in -- nobody editing -- is the state the field is almost
	// always in, and a policy that waited for the next edit to take effect would look broken.
	PushOverflowToVisual();
}
void UUITextInput::SetCaretBlinkRate(float Value)
{
	CaretBlinkRate = Value;
}
void UUITextInput::SetCaretWidth(float Value)
{
	if (CaretWidth != Value)
	{
		CaretWidth = Value;
		if (CaretWidget.IsValid())
		{
			CaretWidget->SetWidth(CaretWidth);
		}
	}
}
void UUITextInput::SetCaretColor(FColor Value)
{
	if (CaretColor != Value)
	{
		CaretColor = Value;
		if (CaretWidget.IsValid())
		{
			CaretWidget->GetVisual()->SetColor(CaretColor);
		}
	}
}
void UUITextInput::SetSelectionColor(FColor Value)
{
	if (SelectionColor != Value)
	{
		SelectionColor = Value;
		if (SelectionMaskObjectArray.Num() > 0)
		{
			for (auto& SelectionVisual : SelectionMaskObjectArray)
			{
				if (SelectionVisual.IsValid())
				{
					SelectionVisual->SetColor(SelectionColor);
				}
			}
		}
	}
}
void UUITextInput::SetVirtualKeyboradOptions(FVirtualKeyboardOptions Value)
{
	// The misspelling, kept because callers spell it: it forwards rather than assigning again.
	SetVirtualKeyboardOptions(Value);
}
void UUITextInput::SetIgnoreKeys(const TArray<FKey>& Value)
{
	IgnoreKeys = Value;
}
void UUITextInput::SetAutoActivateInputWhenNavigateIn(bool Value)
{
	bAutoActivateInputWhenNavigateIn = Value;
}
void UUITextInput::SetReadOnly(bool Value)
{
	if (bReadOnly == Value)
	{
		return;
	}
	bReadOnly = Value;
	if (bReadOnly && bInputActive)
	{
		// A field that went read-only while it was being typed into kept its caret blinking and its
		// keyboard bound, and the next keystroke was simply swallowed -- a field that looks live and
		// is not. Ending the edit is what "read only" says, and it is the moment the caret, the
		// selection and the key bindings all come down.
		// Without firing: turning the knob is not the player finishing an entry, and a submit here
		// would report a value nobody committed.
		DeactivateInput(false);
	}
}
void UUITextInput::SetMaxLength(int32 Value)
{
	Value = FMath::Max(0, Value);
	if (MaxLength != Value)
	{
		MaxLength = Value;
		if (EnforceMaxLength())
		{
			ClearUndoHistory();
			UpdateAfterTextChange(true);
		}
	}
}
void UUITextInput::SetSubmitWhenDeactivate(bool Value)
{
	bSubmitWhenDeactivate = Value;
}
void UUITextInput::SetSelectAllWhenActivateInput(bool Value)
{
	bSelectAllWhenActivateInput = Value;
}
void UUITextInput::SetAllowContextMenu(bool Value)
{
	if (bAllowContextMenu != Value)
	{
		bAllowContextMenu = Value;
		if (!bAllowContextMenu)
		{
			HideContextMenu();//a menu already open is not grandfathered in
		}
	}
}
void UUITextInput::SetRevertTextOnEscape(bool Value)
{
	bRevertTextOnEscape = Value;
}
void UUITextInput::SetClearKeyboardFocusOnCommit(bool Value)
{
	bClearKeyboardFocusOnCommit = Value;
}
void UUITextInput::SetSelectAllTextOnCommit(bool Value)
{
	bSelectAllTextOnCommit = Value;
}
void UUITextInput::SetIsCaretMovedWhenGainFocus(bool Value)
{
	bIsCaretMovedWhenGainFocus = Value;
}
void UUITextInput::SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> Value)
{
	// Only read while a virtual keyboard is being summoned, so there is nothing live to re-push: the
	// next activation asks GetVirtualKeyboardType for it.
	KeyboardType = Value;
}
void UUITextInput::SetVirtualKeyboardTrigger(EVirtualKeyboardTrigger Value)
{
	VirtualKeyboardTrigger = Value;
}
void UUITextInput::SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction Value)
{
	VirtualKeyboardDismissAction = Value;
}
void UUITextInput::SetVirtualKeyboardOptions(FVirtualKeyboardOptions Value)
{
	// The one implementation; the misspelled name below forwards here rather than carrying a second
	// copy of the assignment.
	VirtualKeyboardOptions = Value;
}

void UUITextInput::CancelInput()
{
	if (!bInputActive)
	{
		return;
	}
	if (!bRevertTextOnEscape)
	{
		// Nothing is being thrown away, so this is just the end of an edit -- and what the end of an
		// edit means is bSubmitWhenDeactivate's answer, exactly as it was before a cancel road existed.
		DeactivateInput();
		return;
	}
	// Whether there is anything to put back, asked before anything is put back. The same comparison
	// SetText makes, so "changed" here means exactly "SetText will restore something". SEditableText
	// likewise reverts, and so reports, only when the edit changed the text (HasTextChangedFromOriginal).
	const bool bEditChangedTheText = !Text.Equals(TextAtActivation, ESearchCase::IgnoreCase);
	if (bEditChangedTheText)
	{
		// The value goes back FIRST, so what is reported below is the restored value and not the
		// abandoned one. With notify, unlike SEditableText's silent SetEditableText: the text really did
		// change back, and this library's two-way bindings (`<->`) take their reverse route from
		// OnValueChanged -- a silent restore would leave every bound model holding the thrown-away edit.
		SetText(TextAtActivation, true);
		// Then the revert is reported as a commit, once, carrying the RESTORED text: UMG's
		// RestoreOriginalText calls OnTextCommitted(OriginalText, ETextCommit::OnCleared) right after
		// putting the text back, so anyone who stores the value on commit stores what the field now
		// holds. Submit also marks this activation as having had its say, which is what keeps the end
		// of the edit below from committing a second time.
		Submit();
	}
	else
	{
		// Nothing to revert, so nothing to report -- UMG's Escape does nothing either when the text is
		// unchanged -- and the end of the edit must not report on the revert's behalf.
		bSubmittedThisActivation = true;
	}
	DeactivateInput();
}

TSharedRef<UUITextInput::FVirtualKeyboardEntry> UUITextInput::FVirtualKeyboardEntry::Create(UUITextInput* Input)
{
	return MakeShareable(new FVirtualKeyboardEntry(Input));
}
UUITextInput::FVirtualKeyboardEntry::FVirtualKeyboardEntry(UUITextInput* InInput)
{
	InputComp = InInput;
}
void UUITextInput::FVirtualKeyboardEntry::SetTextFromVirtualKeyboard(const FText& InNewText, ETextEntryType TextEntryType)
{
	InputComp->SetText(InNewText.ToString());
	// The mobile keyboard's Done button is that platform's Enter, and it was reaching nobody: the
	// field took the text and never reported a commit, so a mobile player filling in a field looked
	// to the game exactly like one who had typed nothing.
	//
	// WHICH dismissals report a value is UMG's VirtualKeyboardDismissAction, and the three answers
	// differ only here. TextChangeOnDismiss says the text changed and nothing else happened, so
	// neither road submits -- including the end-of-edit submit, which is silenced by claiming this
	// activation has already had its say.
	const EVirtualKeyboardDismissAction DismissAction = InputComp->VirtualKeyboardDismissAction;
	if (TextEntryType == ETextEntryType::TextEntryAccepted)
	{
		if (DismissAction != EVirtualKeyboardDismissAction::TextChangeOnDismiss)
		{
			InputComp->Submit();
		}
		else
		{
			InputComp->bSubmittedThisActivation = true;
		}
		InputComp->DeactivateInput();
	}
	else if (TextEntryType == ETextEntryType::TextEntryCanceled)
	{
		// Cancelling is only a commit under TextCommitOnDismiss, which is the field's own historical
		// behaviour and therefore the default. Under the other two the edit simply ends.
		if (DismissAction != EVirtualKeyboardDismissAction::TextCommitOnDismiss)
		{
			InputComp->bSubmittedThisActivation = true;
		}
		InputComp->DeactivateInput();
	}
}
void UUITextInput::FVirtualKeyboardEntry::SetSelectionFromVirtualKeyboard(int InSelStart, int SelEnd)
{
	//@todo
}
bool UUITextInput::FVirtualKeyboardEntry::GetSelection(int& OutSelStart, int& OutSelEnd)
{
	//check(IsInGameThread());

	//const FTextLocation CursorInteractionPosition = OwnerLayout->CursorInfo.GetCursorInteractionLocation();
	//FTextLocation SelectionLocation = OwnerLayout->SelectionStart.Get(CursorInteractionPosition);
	//FTextSelection Selection(SelectionLocation, CursorInteractionPosition);

	//OutSelStart = Selection.GetBeginning().GetOffset();
	//OutSelEnd = Selection.GetEnd().GetOffset();
	return true;
}
FText UUITextInput::FVirtualKeyboardEntry::GetText() const
{
	return FText::FromString(InputComp->GetText());
}
FText UUITextInput::FVirtualKeyboardEntry::GetHintText() const
{
	if (InputComp->PlaceHolder.IsValid())
	{
		if (auto uiText = Cast<UDreamText>(InputComp->PlaceHolder->GetVisual()))
		{
			return uiText->GetText();
		}
	}
	return FText::FromString(TEXT(""));
}
EKeyboardType UUITextInput::FVirtualKeyboardEntry::GetVirtualKeyboardType() const
{
	if (InputComp->KeyboardType.GetValue() != EVirtualKeyboardType::Default)
	{
		// Stated outright. Default is not "the default keyboard" here but "whatever this field's
		// input type implies", which is the derivation below and the only answer the field had before
		// the knob existed -- so naming a keyboard is the new thing, and naming none changes nothing.
		return EVirtualKeyboardType::AsKeyboardType(InputComp->KeyboardType.GetValue());
	}
	if (InputComp->DisplayType == EUITextInputDisplayType::Password)
	{
		return EKeyboardType::Keyboard_Password;
	}
	switch (InputComp->InputType)
	{
	default:
	case EUITextInputType::Standard:
		return EKeyboardType::Keyboard_Default;
		break;
	case EUITextInputType::Password:
		return EKeyboardType::Keyboard_Password;
		break;
	case EUITextInputType::DecimalNumber:
		return EKeyboardType::Keyboard_Number;
		break;
	}
}
FVirtualKeyboardOptions UUITextInput::FVirtualKeyboardEntry::GetVirtualKeyboardOptions() const
{
	return InputComp->VirtualKeyboardOptions;
}
bool UUITextInput::FVirtualKeyboardEntry::IsMultilineEntry() const
{
	return InputComp->bAllowMultiLine;
}


#define DreamGUI_LOG_TextInputMethodContext 0
TSharedRef<UUITextInput::FTextInputMethodContext> UUITextInput::FTextInputMethodContext::Create(UUITextInput* Input)
{
	return MakeShareable(new FTextInputMethodContext(Input));
}
void UUITextInput::FTextInputMethodContext::Dispose()
{
	if (CachedWindow.IsValid())
	{
		if (IsValid(GEngine))
		{
			if (IsValid(GEngine->GameViewport))
			{
				GEngine->GameViewport->RemoveViewportWidgetContent(CachedWindow.ToSharedRef());
			}
		}
		// ...and forget it. The widget has just been taken out of the viewport's overlay, so the
		// next GetWindow() would have asked Slate which window holds a widget that is in no window
		// at all -- and then dereferenced the null that comes back.
		CachedWindow.Reset();
	}
}
bool UUITextInput::FTextInputMethodContext::ProjectUIPointToScreen(const FVector& InWorldPosition, FVector2D& OutScreenPosition)
{
	if (InputComp == nullptr)return false;
	if (!FSlateApplication::IsInitialized())return false;
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport))return false;
	UDreamWidget* Widget = InputComp->GetWidget();
	if (Widget == nullptr)return false;
	UDreamCanvas* Canvas = Widget->GetRenderCanvas();
	if (Canvas == nullptr)return false;
	TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
	if (!ViewportWidget.IsValid())return false;

	const FGeometry ViewportGeometry = ViewportWidget->GetCachedGeometry();
	const FVector2D ViewportSize = FVector2D(ViewportGeometry.GetLocalSize());
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)return false;

	const FVector4 ClipPosition = Canvas->GetViewProjectionMatrix().TransformFVector4(FVector4(InWorldPosition, 1.0f));
	if (ClipPosition.W <= 0.0f)return false;//behind the view
	const FVector2D NormalizedDeviceCoordinate(ClipPosition.X / ClipPosition.W, ClipPosition.Y / ClipPosition.W);
	//NDC (y up, -1..1) to viewport-local Slate units (y down, 0..size)
	const FVector2D LocalPosition(
		(NormalizedDeviceCoordinate.X * 0.5 + 0.5) * ViewportSize.X,
		(0.5 - NormalizedDeviceCoordinate.Y * 0.5) * ViewportSize.Y);
	OutScreenPosition = FVector2D(ViewportGeometry.LocalToAbsolute(LocalPosition));
	return true;
}
bool UUITextInput::FTextInputMethodContext::DeprojectScreenPointToUI(const FVector2D& InScreenPosition, FVector& OutWorldPosition)
{
	if (InputComp == nullptr)return false;
	if (!FSlateApplication::IsInitialized())return false;
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport))return false;
	UDreamText* TextVisualObject = InputComp->TextVisual.Get();
	UDreamWidget* TextWidget = (TextVisualObject != nullptr) ? TextVisualObject->GetWidget() : nullptr;
	if (TextWidget == nullptr)return false;
	UDreamCanvas* Canvas = TextWidget->GetRenderCanvas();
	if (Canvas == nullptr)return false;
	TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
	if (!ViewportWidget.IsValid())return false;

	const FGeometry ViewportGeometry = ViewportWidget->GetCachedGeometry();
	const FVector2D ViewportSize = FVector2D(ViewportGeometry.GetLocalSize());
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)return false;
	const FVector2D LocalPosition = FVector2D(ViewportGeometry.AbsoluteToLocal(InScreenPosition));

	FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector;
	FSceneView::DeprojectScreenToWorld(LocalPosition, FIntRect(0, 0, (int32)ViewportSize.X, (int32)ViewportSize.Y)
		, Canvas->GetViewProjectionMatrix().Inverse(), RayOrigin, RayDirection);

	//a DreamGUI widget's plane is its local YZ, so its own X axis is the plane normal
	const FTransform& WidgetTransform = TextWidget->GetWorldTransform();
	const FVector PlaneOrigin = WidgetTransform.GetLocation();
	const FVector PlaneNormal = WidgetTransform.TransformVectorNoScale(FVector(1, 0, 0));
	const double Denominator = FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::Abs(Denominator) < UE_KINDA_SMALL_NUMBER)return false;//ray runs along the plane
	const double Distance = FVector::DotProduct(PlaneOrigin - RayOrigin, PlaneNormal) / Denominator;
	if (Distance <= 0.0)return false;//the plane is behind the view
	OutWorldPosition = RayOrigin + RayDirection * Distance;
	return true;
}
int32 UUITextInput::FTextInputMethodContext::CaretIndexFromCharIndex(int32 InCharIndex)const
{
	if (InputComp == nullptr)return 0;
	if (!DreamTextInputLocal::CanMapCaretIndices(InputComp->TextVisual))return InCharIndex;
	return InputComp->TextVisual->GetCaretIndexByCharIndex(FMath::Clamp(InCharIndex, 0, InputComp->Text.Len()));
}
UUITextInput::FTextInputMethodContext::FTextInputMethodContext(UUITextInput* InInput)
{
	InputComp = InInput;
}
bool UUITextInput::FTextInputMethodContext::IsReadOnly()
{
#if DreamGUI_LOG_TextInputMethodContext
	//UE_LOG(DreamGUI, Log, TEXT("IsReadOnly"));
#endif
	return InputComp->GetReadOnly();
}
uint32 UUITextInput::FTextInputMethodContext::GetTextLength()
{
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(DreamGUI, Log, TEXT("GetTextLength, Text:%s, Length:%d"), *InputComp->Text, InputComp->Text.Len());
#endif
	return InputComp->Text.Len();
}
void UUITextInput::FTextInputMethodContext::GetSelectionRange(uint32& BeginIndex, uint32& Length, ECaretPosition& OutCaretPosition)
{
	Length = FMath::Abs(InputComp->CaretPositionIndex - InputComp->PressCaretPositionIndex);
	OutCaretPosition = InputComp->PressCaretPositionIndex <= InputComp->CaretPositionIndex ? ECaretPosition::Ending : ECaretPosition::Beginning;
	if (OutCaretPosition == ECaretPosition::Beginning)
	{
		BeginIndex = InputComp->CaretPositionIndex;
	}
	else
	{
		BeginIndex = InputComp->PressCaretPositionIndex;
	}

#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetSelectionRange, BeginIndex:%d, Length:%d, InCaretPosition:%d, Text:%s"), BeginIndex, Length, (int32)OutCaretPosition, *InputComp->Text);
#endif
}
void UUITextInput::FTextInputMethodContext::SetSelectionRange(const uint32 BeginIndex, const uint32 Length, const ECaretPosition InCaretPosition)
{
	// Clamped like CaretPositionIndex below. This one was never clamped, and it is read straight back
	// as a string offset by GetSelectionRange, DeleteSelection and Copy.
	InputComp->PressCaretPositionIndex = FMath::Clamp((int32)BeginIndex, 0, InputComp->Text.Len());
	if (Length > 0)
	{
		if (InCaretPosition == ECaretPosition::Beginning)
		{
			InputComp->CaretPositionIndex = (int32)BeginIndex - (int32)Length;
		}
		else
		{
			InputComp->CaretPositionIndex = (int32)BeginIndex + (int32)Length;
		}
	}
	else
	{
		InputComp->CaretPositionIndex = (int32)BeginIndex;
	}
	if (InputComp->Text.Len() == 0)
	{
		InputComp->CaretPositionIndex = 0;
	}
	else
	{
		InputComp->CaretPositionIndex = FMath::Clamp(InputComp->CaretPositionIndex, 0, InputComp->Text.Len());
	}
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(DreamGUI, Warning, TEXT("SetSelectionRange, BeginIndex:%d, Length:%d, InCaretPosition:%d, CaretPositionIndex:%d, PressCaretPositionIndex:%d"), BeginIndex, Length, (int32)InCaretPosition, InputComp->CaretPositionIndex, InputComp->PressCaretPositionIndex)
#endif
}
void UUITextInput::FTextInputMethodContext::GetTextInRange(const uint32 BeginIndex, const uint32 Length, FString& OutString)
{
	OutString = InputComp->Text.Mid(BeginIndex, Length);
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetTextInRange, BeginIndex:%d, Length:%d, OutString:%s"), BeginIndex, Length, *(OutString));
#endif
}
void UUITextInput::FTextInputMethodContext::SetTextInRange(const uint32 BeginIndex, const uint32 Length, const FString& InString)
{
	// The IME holds its OWN idea of the range and hands it back whenever it likes. Nothing stops the
	// game from rewriting Text mid-composition -- a timer, a replication update, any SetText caller
	// -- and an unclamped RemoveAt against a shorter string is TArray's RangeCheck, i.e. a crash.
	// Read-only is checked here too: IsReadOnly() only TELLS the IME, it does not stop it writing.
	if (InputComp->bReadOnly)return;
	const int32 TextLength = InputComp->Text.Len();
	const int32 ClampedBegin = FMath::Clamp((int32)BeginIndex, 0, TextLength);
	const int32 ClampedLength = FMath::Clamp((int32)Length, 0, TextLength - ClampedBegin);
	if (ClampedLength > 0)
	{
		InputComp->Text.RemoveAt(ClampedBegin, ClampedLength);
	}
	int InsertCharCount = 0;
	for (int i = 0; i < InString.Len(); i++)
	{
		TCHAR c = InString[i];
		//the composition string answers to MaxLength like every other road into the text
		if (!InputComp->HasRoomForMoreChars(InputComp->Text.Len(), 1))break;
		if (InputComp->IsValidChar(c, InputComp->Text, ClampedBegin + InsertCharCount))
		{
			InputComp->Text.InsertAt(ClampedBegin + InsertCharCount, c);
			InsertCharCount++;
		}
	}
	InputComp->CaretPositionIndex = ClampedBegin + InsertCharCount;
	InputComp->PressCaretPositionIndex = InputComp->CaretPositionIndex;
	InputComp->UpdateAfterTextChange(false);
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(DreamGUI, Log, TEXT("SetTextInRange, BeginIndex:%d, Length:%d, InString:%s"), BeginIndex, Length, *(InString));
#endif
}
int32 UUITextInput::FTextInputMethodContext::GetCharacterIndexFromPoint(const FVector2D& Point)
{
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetCharacterIndexFromPoint:%s"), *(Point.ToString()));
#endif
	// Was a flat "return 0", which told every IME that any point in the field is the very start of
	// the text -- so reconversion and mouse positioning inside a composition both aimed at index 0.
	// The text already knows how to answer this from a world point; the work is getting there.
	if (InputComp == nullptr)return 0;
	if (!DreamTextInputLocal::CanMapCaretIndices(InputComp->TextVisual))return 0;
	UDreamText* TextVisualObject = InputComp->TextVisual.Get();
	FVector WorldPosition = FVector::ZeroVector;
	if (!DeprojectScreenPointToUI(Point, WorldPosition))
	{
		//no canvas, no viewport, or the point misses the plane: the live caret is the honest answer
		return FMath::Clamp(TextVisualObject->GetCharIndexByCaretIndex(InputComp->CaretPositionIndex), 0, InputComp->Text.Len());
	}
	FVector2f CaretPosition = FVector2f::ZeroVector;
	int32 CaretLineIndex = 0;
	int32 CaretIndex = 0;
	TextVisualObject->FindCaretByWorldPosition(WorldPosition, CaretPosition, CaretLineIndex, CaretIndex);
	CaretIndex += InputComp->VisibleCaretStartIndex;
	return FMath::Clamp(TextVisualObject->GetCharIndexByCaretIndex(CaretIndex), 0, InputComp->Text.Len());
}
bool UUITextInput::FTextInputMethodContext::GetTextBounds(const uint32 BeginIndex, const uint32 Length, FVector2D& Position, FVector2D& Size)
{
	// The IME puts its candidate window directly under this rect, so a hard-coded (0,0)-(1000,1000)
	// is why every CJK candidate list appeared glued to the top-left of the screen instead of under
	// the caret. The rect wanted is the range's bounds in absolute desktop pixels: take the caret
	// position at each end of the range, lift each one half a line up and half a line down, and
	// project all four through the canvas that draws the field.
	Position = FVector2D::ZeroVector;
	Size = FVector2D::ZeroVector;
	UDreamText* TextVisualObject = (InputComp != nullptr) ? InputComp->TextVisual.Get() : nullptr;
	UDreamWidget* TextWidget = (TextVisualObject != nullptr) ? TextVisualObject->GetWidget() : nullptr;
	if (TextWidget == nullptr)return false;

	const float HalfLineHeight = TextVisualObject->GetFontSize() * 0.5f;
	auto CaretCornerWorldPosition = [&](int32 InCharIndex, float InVerticalOffset)->FVector
	{
		int32 CaretIndex = CaretIndexFromCharIndex(InCharIndex);
		FVector2f LocalCaretPosition = FVector2f::ZeroVector;
		int32 CaretLineIndex = 0, VisibleStartIndex = 0;
		TextVisualObject->FindCaretByIndex(CaretIndex, LocalCaretPosition, CaretLineIndex, VisibleStartIndex);
		return TextWidget->GetWorldTransform().TransformPosition(
			FVector(0, LocalCaretPosition.X, LocalCaretPosition.Y + InVerticalOffset));
	};

	const int32 RangeBegin = (int32)BeginIndex;
	const int32 RangeEnd = (int32)BeginIndex + (int32)Length;
	FVector2D Corners[4];
	if (!ProjectUIPointToScreen(CaretCornerWorldPosition(RangeBegin, HalfLineHeight), Corners[0])
		|| !ProjectUIPointToScreen(CaretCornerWorldPosition(RangeBegin, -HalfLineHeight), Corners[1])
		|| !ProjectUIPointToScreen(CaretCornerWorldPosition(RangeEnd, HalfLineHeight), Corners[2])
		|| !ProjectUIPointToScreen(CaretCornerWorldPosition(RangeEnd, -HalfLineHeight), Corners[3]))
	{
		//nothing to project through yet; fall back to the field's rect so the candidates at least
		//land on the field rather than on the desktop's corner
		GetScreenBounds(Position, Size);
		return false;
	}
	FVector2D Min = Corners[0], Max = Corners[0];
	for (int32 i = 1; i < 4; i++)
	{
		Min = FVector2D(FMath::Min(Min.X, Corners[i].X), FMath::Min(Min.Y, Corners[i].Y));
		Max = FVector2D(FMath::Max(Max.X, Corners[i].X), FMath::Max(Max.Y, Corners[i].Y));
	}
	Position = Min;
	Size = Max - Min;
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetTextBounds:%s"), *(Position.ToString()));
#endif
	// The return is "is this range drawn clipped", not "is it visible": it goes straight through to
	// ITextStoreACP::GetTextExt's pfClipped. False is the answer for a range that is wholly on screen, and is
	// what Slate's own context returns. Do not flip it to true -- that tells the IME the rect below is only
	// part of the range.
	return false;
}
void UUITextInput::FTextInputMethodContext::GetScreenBounds(FVector2D& Position, FVector2D& Size)
{
	// The field's own box in absolute desktop pixels -- the area the IME is allowed to treat as the
	// text control. The four corners are projected rather than the centre plus a size, because a
	// canvas may be rotated in the world and an axis-aligned rect is all the IME can hold.
	Position = FVector2D::ZeroVector;
	Size = FVector2D::ZeroVector;
	UDreamWidget* Widget = (InputComp != nullptr) ? InputComp->GetWidget() : nullptr;
	if (Widget == nullptr)return;

	const float Left = Widget->GetLocalSpaceLeft();
	const float Right = Widget->GetLocalSpaceRight();
	const float Bottom = Widget->GetLocalSpaceBottom();
	const float Top = Widget->GetLocalSpaceTop();
	const FVector LocalCorners[4] = {
		FVector(0, Left, Top), FVector(0, Right, Top),
		FVector(0, Left, Bottom), FVector(0, Right, Bottom) };
	FVector2D Min = FVector2D::ZeroVector, Max = FVector2D::ZeroVector;
	bool bAnyProjected = false;
	for (const FVector& LocalCorner : LocalCorners)
	{
		FVector2D ScreenCorner = FVector2D::ZeroVector;
		if (!ProjectUIPointToScreen(Widget->GetWorldTransform().TransformPosition(LocalCorner), ScreenCorner))continue;
		if (!bAnyProjected)
		{
			Min = Max = ScreenCorner;
			bAnyProjected = true;
			continue;
		}
		Min = FVector2D(FMath::Min(Min.X, ScreenCorner.X), FMath::Min(Min.Y, ScreenCorner.Y));
		Max = FVector2D(FMath::Max(Max.X, ScreenCorner.X), FMath::Max(Max.Y, ScreenCorner.Y));
	}
	if (!bAnyProjected)return;
	Position = Min;
	Size = Max - Min;
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetScreenBounds, Position:%s, Size:%s"), *(Position.ToString()), *(Size.ToString()));
#endif
}
TSharedPtr<FGenericWindow> UUITextInput::FTextInputMethodContext::GetWindow()
{
	// Three unchecked dereferences used to live here. Dispose() already guarded GEngine and the
	// viewport for the same two calls; this one did not, and FindWidgetWindow legitimately returns
	// null for a widget that has not been arranged into a window yet -- which is the state of a
	// widget added to the viewport overlay in this very function.
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport))return nullptr;
	if (!FSlateApplication::IsInitialized())return nullptr;
	if (!CachedWindow.IsValid())
	{
		CachedWindow = SNew(SBox);
		GEngine->GameViewport->AddViewportWidgetContent(CachedWindow.ToSharedRef());
	}
	const TSharedPtr<SWindow> SlateWindow = FSlateApplication::Get().FindWidgetWindow(CachedWindow.ToSharedRef());
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("GetWindow, Text:%s"), *InputComp->Text);
#endif
	if (!SlateWindow.IsValid())return nullptr;
	return SlateWindow->GetNativeWindow();
}
void UUITextInput::FTextInputMethodContext::BeginComposition()
{
	bIsComposing = true;
	OriginString = InputComp->Text;
	InputComp->HideCompositionUnderline();//a new composition starts with nothing marked
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(DreamGUI, Log, TEXT("BeginComposition"));
#endif
}
void UUITextInput::FTextInputMethodContext::UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength)
{
	// The IME telling us which part of the text it is still working on. Drawn as the third sprite
	// stack beside the caret and the selection mask -- the text pipeline paints one colour for a
	// whole string, so a per-run strip is how a mesh UI says "this is not committed yet".
	if (InputComp != nullptr)
	{
		InputComp->SetCompositionRange(InBeginIndex, (int32)InLength);
	}
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(LogTemp, Log, TEXT("UpdateCompositionRange"));
#endif
}
void UUITextInput::FTextInputMethodContext::EndComposition()
{
	bIsComposing = false;
	//whatever is left is committed text now, so the "still being typed" mark comes off
	InputComp->HideCompositionUnderline();
	if (OriginString != InputComp->Text)
	{
		InputComp->UpdateAfterTextChange(true);
	}
#if DreamGUI_LOG_TextInputMethodContext
	UE_LOG(DreamGUI, Log, TEXT("EndComposition, ResultString:%s"), *(InputComp->Text));
#endif
}



