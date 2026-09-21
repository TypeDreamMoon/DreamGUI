// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamInputKeySelector.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"

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
	if (SelectedChord == InChord)
	{
		// Still re-label: this is also the path ApplyStyle-adjacent code takes after an authored
		// value, and a silent equal write must not leave the face saying something else. Both
		// spellings are re-mirrored, because an equal chord says nothing about a raw key write.
		SelectedKey = SelectedChord.Key;
		PushLabel();
		return;
	}
	SelectedChord = InChord;
	SelectedKey = SelectedChord.Key;
	PushLabel();
	OnChordSelected.Broadcast(SelectedChord);
	OnKeySelected.Broadcast(SelectedKey);
	OnValueChangedBP.Broadcast(SelectedKey);
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
		// Taken, but not bound: the caller must still treat it as consumed, or the same Escape would
		// also close the screen the player is rebinding on. A LIST rather than the single hard-coded
		// Escape this used to reserve, because a player rebinding on a pad has no Escape key and
		// therefore had no key at all that did not become the new binding.
		SetIsListening(false);
		return true;
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
	SetIsListening(false);
	SetSelectedChord(InChord);
	return true;
}

void UDreamInputKeySelector::HandleClicked()
{
	// A click on an armed selector disarms it: the button is the only thing the player can reach
	// while it is waiting, and a control with no way out of its own state is a trap.
	SetIsListening(!bIsListening);
}

void UDreamInputKeySelector::SetIsListening(bool bInIsListening)
{
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
	PushLabel();
	PushFaceColours();
	OnIsListeningChanged.Broadcast(bIsListening);
}

void UDreamInputKeySelector::BeginKeyCapture()
{
	if (InputAgent.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		// No world means no input stack -- an initialize-time arming, or a test. The control stays
		// armed and NotifyKeyPressed remains the way in, which is the contract without this flag.
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
	// AutoReceiveInput plus PreInitializeComponents is what actually builds the InputComponent and
	// pushes it on the player's stack; UUITextInput does the same two lines for the same reason.
	Agent->AutoReceiveInput = EAutoReceiveInput::Player0;
	Agent->PreInitializeComponents();
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
			// Mouse buttons excluded for the same reason as axes, one step later: the click that
			// arms this control is still going down when the agent appears, and the next click is
			// how the player disarms it. Binding them meant the first click after arming bound Left
			// Mouse Button -- and it was the only click the player could make, because there was no
			// longer any way to reach the button again.
			if (Key.IsMouseButton())
			{
				continue;
			}
			// No payload: an FInputActionHandlerSignature taking an FKey is handed the key that
			// fired, which is the shape UUITextInput's AnyKeyPressed already relies on.
			Input->BindKey(Key, EInputEvent::IE_Pressed,
				this, &UDreamInputKeySelector::HandleCapturedKey);
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
	// Through the public entry, so a captured key and a project-fed one take the same path and
	// cannot come to mean different things. The modifiers are read HERE and nowhere else: an
	// FInputKeyBinding hands over the key that fired and nothing about what was held with it.
	NotifyChordPressed(bAllowModifierKeys
		? DreamInputKeySelectorLocal::ChordFromLiveModifiers(InKey)
		: FInputChord(InKey));
}

void UDreamInputKeySelector::NativeOnDestruct()
{
	// Before Super, which clears the constructed flag: the capture agent is a live world actor and
	// releasing it is this control's last act, not something to leave to whatever collects it.
	// SetIsListening is the one writer of the armed state, so this is also the path that puts the
	// face back and tells anybody listening that the control stood down.
	CancelListening();
	Super::NativeOnDestruct();
}

void UDreamInputKeySelector::NativeOnDisable()
{
	// A selector nobody can see is a selector nobody can click, and clicking it is the documented way
	// out of the armed state. Standing it down here is what keeps "the agent's lifetime is exactly
	// the armed state" true for a screen that was merely put away rather than destroyed.
	CancelListening();
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
