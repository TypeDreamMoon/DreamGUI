// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamInputKeySelector.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

#include "Core/DreamUISettings.h"
#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"

#define LOCTEXT_NAMESPACE "DreamInputKeySelector"

namespace DreamInputKeySelectorLocal
{
	/**
	 * Whether InKey is one of the four a chord can be MADE of.
	 *
	 * Asked by key rather than through FKey's own flags because there is no such flag: the modifier
	 * set is the one FInputChord itself is built from (shift, control, alt, command), and both the
	 * left and the right variant of each is the same modifier to a chord.
	 */
	bool IsModifierKey(const FKey& InKey)
	{
		return InKey == EKeys::LeftShift || InKey == EKeys::RightShift
			|| InKey == EKeys::LeftControl || InKey == EKeys::RightControl
			|| InKey == EKeys::LeftAlt || InKey == EKeys::RightAlt
			|| InKey == EKeys::LeftCommand || InKey == EKeys::RightCommand;
	}

	/**
	 * The modifiers held right now, or none when there is no Slate application to ask (a commandlet,
	 * a headless test). Only the CAPTURE path calls this -- the agent's bindings carry a key and
	 * nothing else, so the live state is the only place the other half of a chord can come from.
	 */
	FInputChord ChordFromLiveModifiers(const FKey& InKey)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return FInputChord(InKey);
		}
		const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
		return FInputChord(InKey,
			Modifiers.IsShiftDown(), Modifiers.IsControlDown(),
			Modifiers.IsAltDown(), Modifiers.IsCommandDown());
	}
}

void UDreamInputKeySelector::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Label"), LabelNode);
}

void UDreamInputKeySelector::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The button's tree, unchanged: the face IS the root -- a key binder is one rectangle, and a
	// separate background child would only manufacture a gap for the hit test to fall through.
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				DreamUI::Text("Label")
					.Visual([](UDreamText& InText)
					{
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
}

void UDreamInputKeySelector::WireParts()
{
	ButtonBehaviour = EnsureComponent<UUIButton>(FaceNode);
	if (ButtonBehaviour != nullptr && FaceNode != nullptr)
	{
		// Its own visual: the pointer transition tints the face it is standing on, and the listening
		// state rides those same three colours (see PushFaceColours).
		ButtonBehaviour->SetTransitionTarget(FaceNode->GetVisual());
		ButtonBehaviour->GetOnClickEvent().AddUObject(this, &UDreamInputKeySelector::HandleClicked);
	}
}

void UDreamInputKeySelector::ApplyStyle()
{
	const FDreamInputKeySelectorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::InputKeySelectorStyle);

	// Before anything reads the binding: an authored .dui line writes one spelling raw, and the label
	// below is pushed from the chord.
	ReconcileKeySpellings();

	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.FaceBrush);

	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetColor(Active.LabelColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}
	if (UDreamPanelSlot* LabelSlot = LabelNode != nullptr ? LabelNode->GetPanelSlot() : nullptr)
	{
		LabelSlot->SetPadding(Active.ContentPadding);
	}

	// The words and the colours both depend on the armed state, so both are pushed through the same
	// two functions the state transition uses -- there is no second copy of either rule.
	PushLabel();
	PushFaceColours();

	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SizeControlHeight(Active.Height);
}

FKey UDreamInputKeySelector::GetSelectedKey() const
{
	return SelectedKey;
}

void UDreamInputKeySelector::SetSelectedKey(FKey InKey)
{
	// A bare chord: the key-shaped setter says "bind exactly this key", so whatever modifiers the
	// binding carried are dropped rather than silently kept under a new key.
	SetSelectedChord(FInputChord(InKey));
}

FInputChord UDreamInputKeySelector::GetSelectedChord() const
{
	return SelectedChord;
}

void UDreamInputKeySelector::SetSelectedChord(const FInputChord& InChord)
{
	const TWeakObjectPtr<UDreamInputKeySelector> WeakThis(this);
	if (!WeakThis.IsValid() || bEndingLifetime)return;
	if (SelectedChord == InChord)
	{
		// Still mirror an equal write, including a raw authored key spelling.
		SelectedKey = SelectedChord.Key;
		PushLabel();
		return;
	}
	const FInputChord Chord = InChord;
	const uint64 ChordSerial = ++SelectedChordSerial;
	const auto IsCurrentValue = [WeakThis, ChordSerial, Chord]()
	{
		return WeakThis.IsValid() && !WeakThis->bEndingLifetime && WeakThis->SelectedChordSerial == ChordSerial
			&& WeakThis->SelectedChord == Chord && WeakThis->SelectedKey == Chord.Key;
	};
	SelectedChord = Chord;
	SelectedKey = Chord.Key;
	PushLabel();
	if (!IsCurrentValue())return;
	OnChordSelected.Broadcast(Chord);
	if (!IsCurrentValue())return;
	OnKeySelected.Broadcast(Chord.Key);
	if (!IsCurrentValue())return;
	OnValueChangedBP.Broadcast(Chord.Key);
}

void UDreamInputKeySelector::ReconcileKeySpellings()
{
	if (SelectedChord.Key == SelectedKey)
	{
		// Coherent -- including both empty, which is the untouched control.
		return;
	}
	if (SelectedChord.Key.IsValid())
	{
		// A chord somebody authored deliberately; the bare spelling follows it, modifiers and all.
		SelectedKey = SelectedChord.Key;
		return;
	}
	// A bare key against a still-empty chord: the compatibility path existing .dui takes.
	SelectedChord = FInputChord(SelectedKey);
}

bool UDreamInputKeySelector::GetIsListening() const
{
	return bIsListening;
}

void UDreamInputKeySelector::BeginListening()
{
	SetIsListening(true);
}

void UDreamInputKeySelector::CancelListening()
{
	SetIsListening(false);
}

bool UDreamInputKeySelector::NotifyKeyPressed(FKey InKey)
{
	// The bare-chord spelling, so a fed key and a fed chord take one road and cannot come to mean
	// different things. Nothing here reads the live modifier state: a caller of the key-shaped entry
	// said "this key", and second-guessing it would bind whatever the player happened to be holding.
	return NotifyChordPressed(FInputChord(InKey));
}

bool UDreamInputKeySelector::NotifyChordPressed(const FInputChord& InChord)
{
	const TWeakObjectPtr<UDreamInputKeySelector> WeakThis(this);
	if (!WeakThis.IsValid() || bEndingLifetime)return false;
	if (!bIsListening)
	{
		// Not armed, so this key is none of this control's business -- and saying so is what lets a
		// project route every key here without asking first.
		return false;
	}
	if (!InChord.Key.IsValid())
	{
		return false;
	}
	if (bEscapeCancels && EscapeKeys.Contains(InChord.Key))
	{
		// FIRST, and deliberately ahead of the gamepad filter below: the pad's B is one of these by
		// default, so a selector that refused to look at pad keys at all would have no way out on a
		// pad -- the exact trap this list was added to close.
		// Taken, but not bound: the caller must still treat it as consumed, or the same Escape would
		// also close the screen the player is rebinding on. A LIST rather than the single hard-coded
		// Escape this used to reserve, because a player rebinding on a pad has no Escape key and
		// therefore had no key at all that did not become the new binding.
		SetIsListening(false);
		return true;
	}
	if (!bAllowGamepadKeys && InChord.Key.IsGamepadKey())
	{
		// NOT consumed, unlike every other refusal here: a pad key this selector will not bind is a
		// pad key that was meant for something else -- closing the screen, moving the focus -- and
		// swallowing it would leave a controller doing nothing at all while a selector is armed.
		// The selector stays armed, waiting for the keyboard key it was told to accept.
		return false;
	}
	if (bAllowModifierKeys && DreamInputKeySelectorLocal::IsModifierKey(InChord.Key))
	{
		// A modifier pressed on its own is the first half of a chord, not a binding. Consumed so the
		// screen underneath does not act on it, and still armed, waiting for the key it modifies.
		// With modifiers turned OFF this falls through and a modifier binds itself like any key.
		return true;
	}
	// Disarm BEFORE the value moves, so a handler on OnKeySelected sees a settled control -- one that
	// re-opened a dialog from that handler would otherwise arm the next selector and immediately have
	// this same key still in flight.
	const uint64 ChordSerial = SelectedChordSerial;
	SetIsListening(false);
	// A handler may remove the selector or explicitly correct its binding. Re-arming
	// alone is allowed: it starts the next listen without canceling this key's value.
	if (!WeakThis.IsValid() || bEndingLifetime || SelectedChordSerial != ChordSerial)return true;
	SetSelectedChord(InChord);
	return true;
}

void UDreamInputKeySelector::SetStyle(const FDreamInputKeySelectorStyle& InStyle)
{
	Style = InStyle;
	// The whole push, because the face colours are picked from the style AND the armed state
	// together (PushFaceColours), and there is exactly one place that knows how.
	ApplyStyle();
}

void UDreamInputKeySelector::SetNoKeyText(const FText& InNoKeyText)
{
	NoKeyText = InNoKeyText;
	// Only the words, and only the one function that decides which words: re-labelling is not a
	// restyle, and the label already knows to show the prompt instead while the selector is armed.
	PushLabel();
}

void UDreamInputKeySelector::SetListeningText(const FText& InListeningText)
{
	ListeningText = InListeningText;
	PushLabel();
}

void UDreamInputKeySelector::SetEscapeCancels(bool bInEscapeCancels)
{
	// Read by NotifyChordPressed, so the next key asks -- and by the capture, which binds a refused pad
	// key only when it is a way out.
	bEscapeCancels = bInEscapeCancels;
	RefreshKeyCapture();
}

void UDreamInputKeySelector::SetEscapeKeys(const TArray<FKey>& InKeys)
{
	EscapeKeys = InKeys;
	RefreshKeyCapture();
}

void UDreamInputKeySelector::SetAllowModifierKeys(bool bInAllowModifierKeys)
{
	bAllowModifierKeys = bInAllowModifierKeys;
}

void UDreamInputKeySelector::SetAllowGamepadKeys(bool bInAllowGamepadKeys)
{
	bAllowGamepadKeys = bInAllowGamepadKeys;
	RefreshKeyCapture();
}

void UDreamInputKeySelector::RefreshKeyCapture()
{
	if (!InputAgent.IsValid())
	{
		// Nothing is capturing; the next arming binds by the knobs as they are then.
		return;
	}
	EndKeyCapture();
	BeginKeyCapture();
}

void UDreamInputKeySelector::SetCaptureKeysWhileListening(bool bInCaptureKeysWhileListening)
{
	if (bCaptureKeysWhileListening == bInCaptureKeysWhileListening)
	{
		return;
	}
	bCaptureKeysWhileListening = bInCaptureKeysWhileListening;
	if (!bIsListening)
	{
		// Nothing is running; the next arming reads the new answer.
		return;
	}
	// Armed right now, so the agent's existence has to follow the flag immediately. Leaving a live
	// capture standing after it was switched off is the state the class comment calls a trap: an
	// InputComponent at the top of the stack that nothing left in this class will ever destroy.
	if (bCaptureKeysWhileListening)
	{
		BeginKeyCapture();
	}
	else
	{
		EndKeyCapture();
	}
}

void UDreamInputKeySelector::SetTextBlockVisibility(EDreamWidgetVisibility InVisibility)
{
	if (LabelNode != nullptr)
	{
		// The LABEL only. The face keeps its own visibility, so the selector stays clickable -- which
		// is the documented way out of the armed state and must not be taken away by hiding words.
		LabelNode->SetVisibility(InVisibility);
	}
}

void UDreamInputKeySelector::HandleClicked()
{
	// A click on an armed selector disarms it: the button is the only thing the player can reach
	// while it is waiting, and a control with no way out of its own state is a trap.
	SetIsListening(!bIsListening);
}

void UDreamInputKeySelector::SetIsListening(bool bInIsListening)
{
	const TWeakObjectPtr<UDreamInputKeySelector> WeakThis(this);
	if (!WeakThis.IsValid() || (bInIsListening && bEndingLifetime))return;
	if (bIsListening == bInIsListening)
	{
		return;
	}
	bIsListening = bInIsListening;
	// The agent's lifetime is exactly the armed state, which is what keeps this control from
	// consuming a single key at any other moment.
	if (bIsListening)
	{
		if (bCaptureKeysWhileListening)
		{
			BeginKeyCapture();
		}
	}
	else
	{
		// Unconditionally, and deliberately NOT gated on the flag that armed it: an author who turned
		// bCaptureKeysWhileListening off while a selector was armed would otherwise leave the agent
		// standing with nothing left in this class that ever destroys it. Ending a capture that never
		// began is already a no-op.
		EndKeyCapture();
	}
	if (!WeakThis.IsValid())return;
	PushLabel();
	if (!WeakThis.IsValid())return;
	PushFaceColours();
	if (!WeakThis.IsValid())return;
	OnIsListeningChanged.Broadcast(bIsListening);
}

void UDreamInputKeySelector::BeginKeyCapture()
{
	if (InputAgent.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	// The player this selector belongs to, whose keys are the ones it is waiting for. It used to listen to
	// player 0 whoever owned it: in a split screen the second player's selector took the first player's
	// keys, and the second player's own reached it not at all.
	APlayerController* OwnerController = GetOwningPlayer();
	if (World == nullptr || !IsValid(OwnerController))
	{
		// No world means no input stack -- an initialize-time arming, or a test -- and no player means no
		// stack to push onto. The control stays armed and NotifyKeyPressed remains the way in, which is
		// the contract without this flag.
		return;
	}

	AActor* Agent = World->SpawnActor<AActor>();
	if (Agent == nullptr)
	{
		return;
	}
#if WITH_EDITOR
	Agent->SetActorLabel(FString::Printf(TEXT("%s_KeyCaptureAgent"), *GetName()));
#endif
	// EnableInput is what actually builds the InputComponent and pushes it on that player's stack -- the
	// call AutoReceiveInput makes, for whichever player it names, from PreInitializeComponents.
	Agent->EnableInput(OwnerController);
	InputAgent = Agent;

	if (UInputComponent* Input = Agent->InputComponent)
	{
		// Highest priority, and each binding consumes its own key (FInputKeyBinding does by default):
		// while a binder is armed the key the player presses is FOR the binder and must not also fire
		// whatever it is currently bound to.
		//
		// bBlockInput is deliberately NOT set. It blocks every component below this one on the
		// stack -- including the DreamGUI input actor -- so while a selector was armed the whole UI
		// went deaf: no pointer events reached anything, which took out the one escape route this
		// control documents (HandleClicked: "a click on an armed selector disarms it"). Consuming
		// the keys this component actually bound is the narrower statement, and the correct one.
		Input->Priority = TNumericLimits<int32>::Max();

		TArray<FKey> AllKeys;
		EKeys::GetAllKeys(AllKeys);
		for (const FKey& Key : AllKeys)
		{
			// Axes are excluded, not filtered later: an axis fires continuously from a resting stick
			// and would bind itself the instant anything is armed. Everything else a player can press
			// -- keyboard, gamepad face buttons -- is a bindable non-axis key.
			if (!Key.IsBindableInBlueprints() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D())
			{
				continue;
			}
			// Any Key is not a key but every key: UPlayerInput turns its binding into one per key held down, each of
			// them consumed -- the pad keys and mouse buttons the filters below leave out included. Every real key
			// is bound on its own.
			if (Key == EKeys::AnyKey)
			{
				continue;
			}
			// Mouse buttons excluded for the same reason as axes, one step later: the click that
			// arms this control is still going down when the agent appears, and the next click is
			// how the player disarms it. Binding them meant the first click after arming bound Left
			// Mouse Button -- and it was the only click the player could make, because there was no
			// longer any way to reach the button again.
			if (Key.IsMouseButton())
			{
				continue;
			}
			// A pad key this selector will not bind is not bound here either, the way-out keys apart. A
			// binding consumes its key whether or not its handler takes it, so binding them only for
			// NotifyChordPressed to refuse them swallowed every pad press -- the D-pad, A, the shoulders --
			// for as long as a keyboard-only selector waited, where the property promises they go on to
			// whatever else wanted them.
			if (!bAllowGamepadKeys && Key.IsGamepadKey() && !(bEscapeCancels && EscapeKeys.Contains(Key)))
			{
				continue;
			}
			// No payload: an FInputActionHandlerSignature taking an FKey is handed the key that
			// fired, which is the shape UUITextInput's AnyKeyPressed already relies on.
			//
			// Executed while the game is paused too, which is not the engine's default: a settings
			// screen in a pause menu is where a binder is used. Left at the default, every one of these
			// bindings still CONSUMED its key in a paused game -- UPlayerInput counts a consuming binding
			// whether or not its delegate runs -- so an armed selector swallowed every key and heard
			// none. Whether it answers while paused is asked per key, in HandleCapturedKey.
			Input->BindKey(Key, EInputEvent::IE_Pressed,
				this, &UDreamInputKeySelector::HandleCapturedKey).bExecuteWhenPaused = true;
		}
	}
}

void UDreamInputKeySelector::EndKeyCapture()
{
	if (AActor* Agent = InputAgent.Get())
	{
		Agent->Destroy();
	}
	InputAgent.Reset();
}

void UDreamInputKeySelector::HandleCapturedKey(FKey InKey)
{
	// The capture bindings execute while the game is paused (BeginKeyCapture), so the pause is weighed
	// here, as the key arrives, by the rule UDreamUIManagerWorldSubsystem ticks widgets by: the
	// screen-space setting for a selector drawn on the screen, the world-space one otherwise. One whose
	// UI the settings pause with the game ignores the key, as the engine's own gate would have.
	if (const UWorld* World = GetWorld(); World != nullptr && World->IsPaused())
	{
		const UDreamUISettings* const Settings = GetDefault<UDreamUISettings>();
		if (IsScreenSpaceOverlayUI() ? Settings->bScreenSpaceUIAffectByGamePause : Settings->bWorldSpaceUIAffectByGamePause)
		{
			return;
		}
	}

	// Through the public entry, so a captured key and a project-fed one take the same path and
	// cannot come to mean different things. The modifiers are read HERE and nowhere else: an
	// FInputKeyBinding hands over the key that fired and nothing about what was held with it.
	NotifyChordPressed(bAllowModifierKeys
		? DreamInputKeySelectorLocal::ChordFromLiveModifiers(InKey)
		: FInputChord(InKey));
}

void UDreamInputKeySelector::NativeOnConstruct()
{
	bEndingLifetime = false;
	Super::NativeOnConstruct();
}

void UDreamInputKeySelector::NativeOnDestruct()
{
	const TWeakObjectPtr<UDreamInputKeySelector> WeakThis(this);
	bEndingLifetime = true;
	// Before Super, which clears the constructed flag: the capture agent is a live world actor and
	// releasing it is this control's last act, not something to leave to whatever collects it.
	// SetIsListening is the one writer of the armed state, so this is also the path that puts the
	// face back and tells anybody listening that the control stood down.
	CancelListening();
	if (!WeakThis.IsValid())return;
	Super::NativeOnDestruct();
}

void UDreamInputKeySelector::NativeOnDisable()
{
	const TWeakObjectPtr<UDreamInputKeySelector> WeakThis(this);
	// A selector nobody can see is a selector nobody can click, and clicking it is the documented way
	// out of the armed state. Standing it down here is what keeps "the agent's lifetime is exactly
	// the armed state" true for a screen that was merely put away rather than destroyed.
	CancelListening();
	if (!WeakThis.IsValid())return;
	Super::NativeOnDisable();
}

#if WITH_EDITOR
void UDreamInputKeySelector::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Mirror in the direction of the EDIT before the base class re-applies everything: a details
	// panel writes the property raw, and without this, clearing SelectedKey on a control holding a
	// chord would lose to the non-empty chord in ReconcileKeySpellings and snap straight back.
	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamInputKeySelector, SelectedKey))
	{
		SelectedChord = FInputChord(SelectedKey);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamInputKeySelector, SelectedChord))
	{
		SelectedKey = SelectedChord.Key;
	}
	// The base runs ApplyStyle, which pushes the now-coherent pair onto the face.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamInputKeySelector::PushLabel()
{
	UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr;
	if (LabelVisual == nullptr)
	{
		return;
	}
	if (bIsListening)
	{
		// Empty keeps the built-in words, the same "an empty brush keeps the glyph" bargain the check
		// box struck -- so a project overrides the prompt without overriding the control.
		LabelVisual->SetText(ListeningText.IsEmpty()
			? LOCTEXT("Listening", "Press a key...")
			: ListeningText);
		return;
	}
	LabelVisual->SetText(SelectedChord.Key.IsValid()
		// The key's OWN display name, not a spelling this control keeps: the engine already
		// localizes it, and a second table would drift from the one the rest of the game shows.
		// A chord with modifiers spells all of them ("Ctrl+G"); one without is the bare key's name
		// rather than GetInputText's, so a binding that carries no modifiers reads exactly as it
		// always did.
		? (SelectedChord.HasAnyModifierKeys() ? SelectedChord.GetInputText() : SelectedChord.Key.GetDisplayName())
		: (NoKeyText.IsEmpty() ? LOCTEXT("NoKey", "Unbound") : NoKeyText));
}

void UDreamInputKeySelector::PushFaceColours()
{
	if (ButtonBehaviour == nullptr)
	{
		return;
	}
	const FDreamInputKeySelectorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::InputKeySelectorStyle);
	// A UUISelectable-hosted face renders WHITE without explicit colours: the transition is the only
	// writer of that visual's colour, and an unset transition colour is not "leave it alone".
	//
	// All three, not just Normal, while armed. The pointer is by definition still on the button the
	// player just clicked, so the selectable is sitting in Hovered or Pressed -- writing the listening
	// colour into Normal alone means the feedback appears only once the mouse is moved away, which is
	// precisely when the player has stopped looking for it.
	// Listening flattens the pointer states onto one colour: while the control is waiting for a key
	// press, hovering and pressing it mean nothing, and a face that still moved under the pointer
	// would say they did. Disabled and focused keep their own answers either way.
	PushSelectableState(ButtonBehaviour,
		bIsListening ? Active.Listening : Active.Normal,
		bIsListening ? Active.Listening : Active.Hovered,
		bIsListening ? Active.Listening : Active.Pressed,
		Active.Disabled, Active.Focused, Active.TransitionDuration);
}

#undef LOCTEXT_NAMESPACE

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "InputKeySelector", UDreamInputKeySelector)
