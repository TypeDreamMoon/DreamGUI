// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "DreamEnhancedInputEventSystemActor.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;
struct FInputActionInstance;

/**
 * The event system preset for projects on Enhanced Input.
 *
 * This is the C++ form of the DreamEventSystemActor_EnhancedInput preset Blueprint. Only the mouse
 * half differs from the legacy preset: the three buttons and the wheel arrive as Input Actions from
 * a mapping context this actor pushes, while navigation keys and touch stay on the legacy bindings
 * it inherits. Enhanced Input has no equivalent of the Mouse2D vector axis that the legacy preset
 * listens to, so mouse movement is polled on tick instead -- which is what the Blueprint did too.
 *
 * The four actions and the context default to the ones shipped in the plugin, and every one is
 * EditDefaultsOnly so a project can point them at its own.
 *
 * It keeps listening while the game is paused, like the legacy preset, but Enhanced Input keeps its
 * pause gate on the action, not the binding: UEnhancedPlayerInput drops a paused frame's triggers for
 * every action whose bTriggerWhenPaused is false, and the shipped actions leave it false. That flag
 * lives on an asset, and an asset is shared by everything that loads it, so BeginPlay makes this
 * actor its own runtime copies -- of the four actions and of the context, which is copied so that its
 * mappings point at the copied actions -- pushes the copied context, and keeps each copy's
 * bTriggerWhenPaused equal to what UDreamUISettings::bScreenSpaceUIAffectByGamePause asks for, from
 * the start of every world tick, paused ones included. The loaded assets are never written to. An
 * action the actor's own context does not map is left as it is, since a copy of it could never be
 * reached.
 *
 * The originals stay bound beside their copies, to the same handlers: a key a project's own context
 * maps to one of these actions still reaches the UI, and pauses as that asset says, while the keys of
 * this actor's context reach the copy. When one input comes down both roads in the same frame, the
 * button's press (or release, or the wheel's notch in that direction) is forwarded once. Code
 * elsewhere that queries or removes the original context itself no longer finds it on the player,
 * which has this actor's copy instead; GetOriginalAction answers which original a copy stands for.
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamEnhancedInputEventSystemActor : public ADreamStandaloneInputEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamEnhancedInputEventSystemActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * The action InAction is a runtime copy of, or InAction itself when it is not one of this actor's
	 * copies (nothing was copied yet, or the action is one its own context does not map).
	 *
	 * From BeginPlay on the four action properties hold copies (see MakeRuntimeInputCopies). Code that
	 * needs the object the project set -- to map one more key to it in a context of its own, which the
	 * actor binds too -- asks here.
	 */
	const UInputAction* GetOriginalAction(const UInputAction* InAction) const;

protected:
	virtual void BeginPlay() override;
	/**
	 * Takes the mapping context back off the local player -- without this it outlives the level -- and
	 * then puts the originals back in the properties (RestoreOriginalInputObjects).
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Replaces the legacy mouse bindings with the four Input Actions. Navigation and touch are inherited. */
	virtual void BindMouseInput() override;

	/**
	 * Pushed onto the local player on BeginPlay so the actions below resolve without project setup.
	 *
	 * Between BeginPlay and EndPlay, this and the four actions hold the actor's runtime copies rather
	 * than the objects set here (see MakeRuntimeInputCopies), so code reading them in play -- a
	 * subclass's BindMouseInput adding a binding of its own, say -- is looking at what this actor's
	 * context really maps and really put on the player. EndPlay puts the originals back.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/**
	 * Priority the context is pushed at. 0 is Enhanced Input's own default, the priority a gameplay
	 * context usually has too. A context of higher priority is applied first, and a key one of its
	 * actions consumes is taken from the contexts below it -- raise this when the UI's buttons have to
	 * win a key that a gameplay context also maps.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	int32 MappingContextPriority = 0;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerLeftAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerRightAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerMiddleAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> MouseWheelAction;

private:
	/**
	 * Replace the four actions and the context with copies of this actor's own, before anything binds
	 * or pushes them. Called first thing in BeginPlay.
	 *
	 * The copies are what let the pause setting be honoured without touching an asset: the flag that
	 * decides is UInputAction::bTriggerWhenPaused, read afresh by Enhanced Input every input frame. The
	 * context is copied too, with every reference to an original action -- in its default mappings and
	 * in each profile's -- turned into a reference to the copy; otherwise the keys would still be
	 * mapped to the originals and nothing bound to the copies would ever fire. An action set on two
	 * properties gets one copy, so it stays one action; an action the context does not map is not
	 * copied at all, because whatever does map it maps the original. The originals are remembered, for
	 * BindMouseInput to bind beside the copies and for EndPlay to put back.
	 */
	void MakeRuntimeInputCopies();

	/**
	 * Put the objects the project set back into the five properties once the copies are out of play.
	 * Called from EndPlay after the copied context has left the player, so a later BeginPlay -- a
	 * streamed level shown again ends and begins its actors' play -- starts from the originals exactly
	 * as the first one did.
	 */
	void RestoreOriginalInputObjects();

	/** The action InAction was copied from when it is one of RuntimeActionCopies; null when it is not a copy. */
	UInputAction* FindOriginalOfCopy(const UInputAction* InAction) const;

	/**
	 * Keep every copied action's bTriggerWhenPaused equal to ShouldReceiveInputWhilePaused. Run when the
	 * copies are made and at the start of every world tick after (HandleWorldTickStart), so a setting
	 * changed while the game runs reaches the actions before that frame's input is read -- the setting
	 * is read at use everywhere else, and Enhanced Input reads the flag at use.
	 */
	void ApplyPauseSettingToActionCopies();

	/**
	 * FWorldDelegates::OnWorldTickStart, registered for the time between BeginPlay and EndPlay.
	 *
	 * Chosen over the actor's own tick and over a tick function of its own. The actor's tick keeps the
	 * engine's default and stands still while the game is paused -- and letting it run then would run a
	 * Blueprint subclass's Event Tick in a paused game too. A tick function is ordered within tick
	 * groups, which a pause frame does not have. This delegate is broadcast at the very start of every
	 * UWorld::Tick, paused or not, before any tick group and so before the player controller reads the
	 * frame's input: the copies' flags are right for the frame they are read in. It also follows the
	 * pointer through paused frames, which the actor's tick does the rest of the time.
	 */
	void HandleWorldTickStart(UWorld* InWorld, ELevelTick InTickType, float InDeltaSeconds);

	/**
	 * True the first time InBit is claimed in the current frame, false after -- for the second road of
	 * one input. A copy and its original are bound to the same handlers, and one key that both this
	 * actor's context and a project's own map (with an action that lets a key through to lower
	 * contexts) triggers both in the same frame. See ForwardTrigger and OnMouseWheelAction for the bits.
	 */
	bool ClaimInputThisFrame(uint32 InBit);

	/** The actions MakeRuntimeInputCopies made, and the only ones this actor ever writes a flag on. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> RuntimeActionCopies;

	/** The action each of RuntimeActionCopies was made from, at the same index. Kept alive and bound. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> OriginalsOfRuntimeCopies;

	/** The context the project set, while this actor's copy of it is the one in play. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> OriginalMappingContext;

	/** HandleWorldTickStart's registration, removed in EndPlay. */
	FDelegateHandle WorldTickStartHandle;

	/**
	 * The frame ForwardedInputThisFrame belongs to. A frame is told from the next by the engine's frame
	 * counter AND the world's real time: a game moves both on every frame, and a test that pumps a
	 * world by hand moves only the second.
	 */
	uint64 ForwardedInputFrameCounter = 0;
	double ForwardedInputRealTime = -1.0;
	/** What ClaimInputThisFrame has handed out this frame, one bit each. */
	uint32 ForwardedInputThisFrame = 0;

	void AddMappingContextToLocalPlayer();
	void RemoveMappingContextFromLocalPlayer();
	/** The Enhanced Input subsystem of the local player this actor's event system speaks for. */
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem()const;

	/**
	 * The three button handlers take the action INSTANCE rather than the value: the instance carries
	 * which trigger event fired, which is the only thing that says press or release without depending
	 * on a console variable (FInputActionInstance::GetValue is zero outside Triggered unless
	 * EnhancedInput.bAlwaysGetRealValueFromActionInstanceData is on, and it is only on by default).
	 */
	void OnTriggerLeft(const FInputActionInstance& Instance);
	void OnTriggerRight(const FInputActionInstance& Instance);
	void OnTriggerMiddle(const FInputActionInstance& Instance);
	void OnMouseWheelAction(const FInputActionValue& Value);

	/** Shared by the three button actions; each one only differs by which button type it reports. */
	void ForwardTrigger(const FInputActionInstance& Instance, EDreamUIMouseButtonType ButtonType);
	/** Report the device from the keys mapped to InAction, since an Input Action has no key of its own. */
	void ReportDeviceForAction(const UInputAction* InAction);
};
