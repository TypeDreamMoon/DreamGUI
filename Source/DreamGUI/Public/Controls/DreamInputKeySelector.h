// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Controls/DreamUIControl.h"
#include "DreamInputKeySelector.generated.h"

class UDreamWidget;
class UUIButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamInputKeySelectorKeyEvent, FKey, SelectedKey);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamInputKeySelectorListeningEvent, bool, bIsListening);

/**
 * A button whose label is a bound key, and which listens for the next one when clicked.
 *
 * Two nodes and one behaviour, the button's anatomy exactly -- the face IS the root, with the key's
 * name centred on it -- because that is what this control is: a button carrying a value. Clicking it
 * ARMS it (bIsListening), the next key it is told about becomes the value, and it disarms.
 *
 * WHERE THE KEY COMES FROM, plainly, because this plugin has no hook that would supply it and this
 * class does not pretend otherwise. What exists here is:
 *   - UDreamUIActionRouter, which routes NAMED actions from a data table. It resolves a key to an
 *     action and fires that action's callback; it cannot report the key itself, and a rebinder needs
 *     the key, not the meaning.
 *   - ADreamStandaloneInputEventSystemActor, which does hold real FKeys -- its AnyKey binding is
 *     where they arrive -- but forwards them straight into navigation and the router and broadcasts
 *     nothing. OnAnyKeyPressed is private and not virtual.
 *   - UDreamUserWidget's Blueprint surface, which is pointer, drag, focus and navigation. No keys.
 * (UUITextInput does capture raw keys, by spawning an actor with AutoReceiveInput and binding every
 * key on it. That is a text field's bargain -- one actor per active field, consuming what it binds --
 * and inventing a second copy of it inside a control is not something this class does quietly.)
 *
 * So the arming, the state, the visual and the value live here, and the KEY IS FED IN: while this is
 * listening, the project's input layer calls NotifyKeyPressed with whatever the player pressed, and
 * it returns true when it took it. That layer is wherever the project already sees keys -- its
 * PlayerController, an Enhanced Input action, or a subclass of ADreamStandaloneInputEventSystemActor
 * overriding BindActionRouting (which is virtual for exactly this kind of extension). SetSelectedKey
 * writes the value with no listening involved, for a settings screen restoring saved bindings.
 *
 * UMG parity is UInputKeySelector's core: SelectedKey, bIsListening, OnKeySelected, OnIsSelectingKey
 * -- plus the library's OnValueChangedBP, because the key is a value and `<->` binds against it.
 *
 *     /Script/DreamGUI.DreamInputKeySelector JumpBinding {
 *         SelectedKey = (KeyName="SpaceBar")
 *         OnKeySelected -> HandleJumpRebound
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Input Key Selector")
class DREAMGUI_API UDreamInputKeySelector : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	FDreamInputKeySelectorStyle Style;

	/** The bound key, and the control's value. The label is this key's display name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	FKey SelectedKey;

	/** Shown while nothing is bound. Empty keeps the built-in words, as an empty brush keeps a glyph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	FText NoKeyText;

	/** Shown while armed. Empty keeps the built-in words. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	FText ListeningText;

	/**
	 * Escape disarms instead of binding. On by default and for the reason UMG reserves its escape
	 * keys: a player who opened this by accident needs one key that is guaranteed to be a way out,
	 * and a control that will bind ANY key does not have one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	bool bEscapeCancels = true;

	/** Fired when a key is bound, from a listen or from SetSelectedKey. */
	UPROPERTY(BlueprintAssignable, Category = "Input Key Selector")
	FDreamInputKeySelectorKeyEvent OnKeySelected;

	/** Fired when the armed state moves, so a screen can dim the rest of itself while one is armed. */
	UPROPERTY(BlueprintAssignable, Category = "Input Key Selector")
	FDreamInputKeySelectorListeningEvent OnIsListeningChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Input Key Selector")
	FDreamInputKeySelectorKeyEvent OnValueChangedBP;

	UFUNCTION(BlueprintCallable, Category = "Input Key Selector")
	FKey GetSelectedKey() const;

	/** Writes the value and re-labels; broadcasts only when the key actually moves. Never listens. */
	UFUNCTION(BlueprintCallable, Category = "Input Key Selector")
	void SetSelectedKey(FKey InKey);

	UFUNCTION(BlueprintPure, Category = "Input Key Selector")
	bool GetIsListening() const;

	/** Arm it: the next key fed to NotifyKeyPressed is the new binding. What a click does. */
	UFUNCTION(BlueprintCallable, Category = "Input Key Selector")
	void BeginListening();

	/** Disarm without binding anything. What Escape does, and what a closing screen should do. */
	UFUNCTION(BlueprintCallable, Category = "Input Key Selector")
	void CancelListening();

	/**
	 * Offer a key to this selector. THE ENTRY POINT the class comment is about: nothing in this
	 * plugin calls it, the project's own input layer does.
	 *
	 * @return true when the selector took the key -- it bound it, or it was the cancel key -- which
	 *         the caller must read as "do not also treat this as anything else", the same contract
	 *         UDreamUIActionRouter::HandleKey states. False while it is not listening, so a project
	 *         may route every key here unconditionally.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input Key Selector")
	bool NotifyKeyPressed(FKey InKey);

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Input Key Selector")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Input Key Selector")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Input Key Selector")
	TObjectPtr<UUIButton> ButtonBehaviour = nullptr;

	/**
	 * Whether it is armed. Transient and read-only, never authored: arming is something the player
	 * did a moment ago, and a .dui that could author it would author a control that eats the next
	 * key of whatever screen it is on. SetIsListening is the only writer.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Input Key Selector")
	bool bIsListening = false;

	/**
	 * Whether arming also listens for the key itself.
	 *
	 * On, the control spawns an input agent for exactly as long as it is armed -- the arrangement
	 * UUITextInput already uses to get raw keys (an actor with AutoReceiveInput, its InputComponent
	 * pushed on top of the stack, every key bound). It is created on arming and destroyed on
	 * disarming, so nothing is intercepted at any other moment: a key binder that consumed keys
	 * while idle would be a key binder nobody could play past.
	 *
	 * Off, the control is state and visuals only and the project calls NotifyKeyPressed from
	 * wherever it already sees keys. Both were true before this flag; only the default changed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector")
	bool bCaptureKeysWhileListening = true;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleClicked();

	/** The armed flag's one writer, so the visual and the event can never disagree with the state. */
	void SetIsListening(bool bInIsListening);

	/** Spawn the input agent and bind every bindable key to HandleCapturedKey. */
	void BeginKeyCapture();

	/** Destroy it. Called from every path that disarms, including the one a key took. */
	void EndKeyCapture();

	/** What the agent's bindings call. Routes to NotifyKeyPressed, which owns the decision. */
	void HandleCapturedKey(FKey InKey);

	/**
	 * Lives only while armed. Weak because the world owns it: a level transition mid-arming takes
	 * it, and this control must not be the thing that keeps a dead actor alive.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> InputAgent;

	/** The words on the face: the listening prompt while armed, the key's name otherwise. */
	void PushLabel();

	/**
	 * The face's three transition colours. Armed, ALL THREE become the listening colour: the pointer
	 * is still on the button that was just clicked, so a state written only into Normal would be
	 * invisible for exactly as long as the player leaves the mouse where it is.
	 */
	void PushFaceColours();
};
