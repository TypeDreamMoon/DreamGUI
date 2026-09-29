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
 * Only the mouse half differs from the legacy preset: the three buttons and the wheel arrive as Input
 * Actions from a mapping context this actor pushes, while navigation keys and touch stay on the
 * legacy bindings it inherits. Enhanced Input has no equivalent of the Mouse2D vector axis that the
 * legacy preset listens to, so mouse movement is polled on tick instead.
 *
 * The four actions and the context are empty on this class, and every one is EditDefaultsOnly. The
 * plugin fills them in the preset Blueprint /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput
 * -- a data-only subclass of this class, with IMC_DreamUIInputContext and the four IA_* actions under
 * /DreamGUI/EnhancedInput -- which is the class to spawn; a project with actions of its own points
 * them there in a subclass of its own.
 *
 * It binds the actions and pushes the context it is given, as they are, and never writes to them. A
 * paused game is decided per event, as the legacy preset decides it for its keys and the Slate input
 * source for everything it hears: while the game is paused and UDreamUISettings say the UI pauses with
 * it, the handlers drop what arrives (ShouldIgnoreInput). For a paused frame's click to arrive at all,
 * its action has to trigger while paused -- Enhanced Input drops a paused frame's triggers for an action
 * whose bTriggerWhenPaused is false -- and the shipped actions do; a project's own actions for this
 * actor want the flag set as well.
 *
 * Superseded by the Slate input source (UDreamGUISettings::bUseSlateInputSource), which hears every
 * pointer, key and stick before the viewport, with no context of the UI's own; this actor stands down
 * while the Slate source is on.
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUIINPUT_API ADreamEnhancedInputEventSystemActor : public ADreamStandaloneInputEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamEnhancedInputEventSystemActor();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	/** Takes the mapping context back off the local player: without this it outlives the level. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Replaces the legacy mouse bindings with the four Input Actions. Navigation and touch are inherited. */
	virtual void BindMouseInput() override;

	/** Pushed onto the local player on BeginPlay so the actions below resolve without project setup. */
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
	 * FWorldDelegates::OnWorldTickStart, registered for the time between BeginPlay and EndPlay: it follows the pointer
	 * through paused frames.
	 *
	 * Chosen over the actor's own tick and over a tick function of its own. The actor's tick keeps the engine's default
	 * and stands still while the game is paused -- and letting it run then would run a Blueprint subclass's Event Tick in
	 * a paused game too. A tick function is ordered within tick groups, which a pause frame does not have. This delegate
	 * is broadcast at the very start of every UWorld::Tick, paused or not.
	 */
	void HandleWorldTickStart(UWorld* InWorld, ELevelTick InTickType, float InDeltaSeconds);

	/** HandleWorldTickStart's registration, removed in EndPlay. */
	FDelegateHandle WorldTickStartHandle;

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
