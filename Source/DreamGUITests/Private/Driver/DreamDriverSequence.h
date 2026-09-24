// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverTypes.h"

class FAutomationTestBase;
class UDreamInputKeySelector;
class UUITextInput;
class UDreamCanvas;
class UDreamEventSystem;
class UDreamPointerEventData;
class UDreamDriverInputModule;
class UDreamScreenSpaceRaycaster;
class UDreamUIManagerWorldSubsystem;
class UDreamWidget;
class UClass;
class UWorld;
class UGameInstance;
class APlayerController;
class ULocalPlayer;
class AActor;
enum class EDreamUIMouseButtonType : uint8;
enum class EDreamUINavigationDirection : uint8;

/**
 * Everything a driver step needs in order to happen: the world it happens in, the pieces that carry
 * input through it, and the tree it is aimed at.
 *
 * The pointers are raw and not kept alive from here. What keeps them alive is what would keep them
 * alive in a game: actors belong to the world, widgets are held by the UI manager's registered-widget
 * list, and the rig that built all of it outlives every context it hands out. A second owner here
 * would only disagree with those.
 */
struct FDreamDriverContext
{
	UWorld* World = nullptr;
	UDreamEventSystem* EventSystem = nullptr;
	UDreamDriverInputModule* InputModule = nullptr;
	UDreamUIManagerWorldSubsystem* Manager = nullptr;
	UDreamScreenSpaceRaycaster* Raycaster = nullptr;
	UDreamWidget* Root = nullptr;
	UDreamCanvas* RootCanvas = nullptr;

	/*
	 * The game side of the world, for a rig that has one. All of these are optional: a bare-world
	 * rig has none of them, and a ModuleOnly rig has a player controller only once EnsureGameInputHost
	 * made one. Kept on the context rather than on the rig because the pump and the steps need them,
	 * and the steps only ever see the context.
	 */
	/** The GameInstance that owns World. Null for a bare UWorld::CreateWorld world. */
	UGameInstance* GameInstance = nullptr;
	/** The world's player 0, once something has made or found one. EnsureGameInputHost reuses it rather than spawning a second. */
	APlayerController* PlayerController = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	/** The ADream*InputEventSystemActor the input goes through, when the input host is an actor. */
	AActor* InputActor = nullptr;
	/** Where input enters. Anything but ModuleOnly routes buttons, wheel, navigation, keys and touch through the game host. */
	EDreamRigInputHost InputHost = EDreamRigInputHost::ModuleOnly;

	/** The test currently running, so a step that fails can say so where a report will show it. Optional. */
	FAutomationTestBase* CurrentTest = nullptr;

	/**
	 * How long a pumped frame is. Fixed rather than measured, because a headless pump has no frame
	 * rate to measure -- and because timeouts are expressed in seconds while the only thing that
	 * advances here is frames, so the conversion between the two has to be a constant.
	 */
	float FrameSeconds = 1.0f / 60.0f;

	/** Whether this context has the pieces a step needs. A half-built rig should fail loudly, not act. */
	bool IsUsable() const;

	/**
	 * One frame of the headless pump.
	 *
	 * See the implementation for what it calls and why; in short: advance the world clock, tick the
	 * event system (which is what reaches the input module), then tick the UI manager (which is
	 * layout, transforms and clip rectangles). Not called under the engine pump -- there the engine's
	 * own frame is the pump, and calling this as well would run everything twice.
	 */
	void PumpOneFrame(float InDeltaSeconds);

	/** PumpOneFrame InFrameCount times at FrameSeconds each. */
	void PumpFrames(int32 InFrameCount);

	/**
	 * The tickable world subsystem classes the pump drives, in the order it drives them. The UI
	 * manager is the last entry; it is driven through TickDreamUI rather than Tick.
	 *
	 * The single list: PumpOneFrame walks it, and the coverage guard compares it with every tickable
	 * world subsystem the plugin declares, so a new one cannot be missed silently.
	 */
	static TArray<UClass*> GetPumpedTickableWorldSubsystems();

	/** The pointer's own state, which is where hover, press and drag are readable from. */
	UDreamPointerEventData* GetPointerEventData(int32 InPointerID = 0) const;

	/** The one widget this locator finds under the root, or null when it finds none or several. */
	UDreamWidget* FindOne(const FDreamLocatorRef& InLocator) const;

	/**
	 * The text field that owns the keyboard: the event system's selection for pointer 0, carrying a
	 * UUITextInput that is being edited. Null otherwise, with OutWhyNot saying which link was missing.
	 *
	 * The same lookup UDreamUINavigationStack::HandleBack makes to decide whether Back cancels an
	 * edit, so what the driver types into is what the runtime would consider focused. Not the
	 * process-wide UUITextInput::GetActiveTextInput: that belongs to whichever world edited last, and
	 * a driver acts on its own.
	 */
	UUITextInput* FindEditingTextInput(FString& OutWhyNot) const;

	/**
	 * A key selector under the root that is armed and listening, or null.
	 *
	 * Found by walking the tree rather than through the selection, because that is how an armed
	 * selector gets its keys in a game: its capture agent's InputComponent sits at the top of the
	 * player's input stack at the highest priority, so it hears the next key whatever is selected.
	 */
	UDreamInputKeySelector* FindListeningKeySelector() const;
};

/** What a step says about itself after being given a frame. */
enum class EDreamDriverStepResult : uint8
{
	/** Finished. The executor moves on to the next step. */
	Done,
	/** Not finished. The executor gives it another frame. */
	Again,
	/** Cannot finish. The executor reports and stops; the sequence has failed. */
	Failed,
};

/**
 * One thing a sequence does, executed at most once per frame.
 *
 * The kinds are few: input, wait (N frames, N seconds, or for a condition), run a lambda (which is
 * where assertions live), and fail outright. The two pumps differ only in what gives a step its
 * frame, which is why there is one step list rather than two.
 */
class IDreamDriverStep
{
public:
	virtual ~IDreamDriverStep() = default;

	/** InDeltaSeconds is the length of the frame this step is being given, for steps that measure time. */
	virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) = 0;

	/** What this step was trying to do, for the error a failure produces. */
	virtual FString Describe() const = 0;

	/** Why the last Execute returned Failed. Empty unless it did. */
	virtual FString GetFailureReason() const { return FString(); }
};

using FDreamDriverStepRef = TSharedRef<IDreamDriverStep>;

/**
 * A list of steps, built by chaining, run by one of two pumps.
 *
 * Perform runs it headlessly and synchronously: the step list is walked here and now, with this
 * object's own PumpOneFrame between steps that asked for another frame. That keeps a driver test the
 * same shape as the 1115 tests already in this module -- set up, act, assert, return.
 *
 * PerformLatent hands the same list to the automation framework as a latent command, one step per
 * ENGINE frame. That is the only way to drive anything the engine itself has to advance: the designer
 * viewport, a Blueprint recompile settling, a render graph. Nothing in the list knows which pump it
 * is under.
 */
class FDreamDriverSequence
{
public:
	explicit FDreamDriverSequence(FDreamDriverContext& InContext);

	/** Pointer moves. MoveTo aims at a located widget's centre; MoveBy and MoveToPixel take raw pixels. */
	FDreamDriverSequence& MoveTo(const FDreamLocatorRef& InLocator);
	FDreamDriverSequence& MoveToPixel(const FVector2D& InPixel);
	FDreamDriverSequence& MoveBy(const FVector2D& InPixelDelta);

	/** Trigger down and up where the pointer already is. */
	FDreamDriverSequence& Press(EDreamUIMouseButtonType InButton);
	FDreamDriverSequence& Release(EDreamUIMouseButtonType InButton);
	FDreamDriverSequence& Press();
	FDreamDriverSequence& Release();

	/** Move to the located widget's centre, then press and release there. */
	FDreamDriverSequence& Click(const FDreamLocatorRef& InLocator);
	FDreamDriverSequence& Click(const FDreamLocatorRef& InLocator, EDreamUIMouseButtonType InButton);

	/**
	 * Press on the first widget's centre, cross the raycaster's drag threshold on the first move, and
	 * release on the second's. Spread over frames because a drag that happened inside one frame is a
	 * teleport, and the press-to-drag decision is made between frames.
	 */
	FDreamDriverSequence& DragTo(const FDreamLocatorRef& InFrom, const FDreamLocatorRef& InTo);
	/** The same, ending at an offset from where the press began rather than at another widget. */
	FDreamDriverSequence& DragBy(const FDreamLocatorRef& InFrom, const FVector2D& InPixelDelta);

	/** A wheel turn, delivered to whatever the pointer is over on the next frame. */
	FDreamDriverSequence& ScrollBy(const FVector2D& InAxisValue);

	/** Gamepad or keyboard navigation: a direction pressed and released, or the accept button. */
	FDreamDriverSequence& Navigate(EDreamUINavigationDirection InDirection);
	FDreamDriverSequence& NavigationTrigger(bool bInTriggerPress);

	/**
	 * Back, the way a gamepad or keyboard sends it: under an actor host the gamepad's Back button
	 * through the player controller (so a bound action, then a drag in flight, then the navigation
	 * stack get it, in the standalone actor's order); under ModuleOnly exactly Type(EKeys::Escape).
	 * One step, one frame. Cancelling an edit in progress is the thing it is most often for.
	 */
	FDreamDriverSequence& Back();

	/**
	 * A finger, one step and one frame per phase, like every other input. Each finger index is its
	 * own pointer -- the module keys touches by index -- so two fingers are two pointers with their
	 * own press, hover and drag; finger 0 is pointer 0, which it shares with the mouse, as in a game.
	 * TouchUp lifts the finger where it is. Moving or lifting a finger that is not down fails.
	 */
	FDreamDriverSequence& TouchDown(int32 InFingerId, const FVector2D& InPixel);
	FDreamDriverSequence& TouchMoveTo(int32 InFingerId, const FVector2D& InPixel);
	FDreamDriverSequence& TouchUp(int32 InFingerId);

	/**
	 * Let a span of time pass. Under the headless pump that is ceil(InSeconds / FrameSeconds) frames
	 * -- a long press is "hold for N seconds", and the pipeline times it on the world clock the pump
	 * advances -- and under the engine pump it is however many real frames the span took.
	 */
	FDreamDriverSequence& WaitSeconds(float InSeconds);

	/**
	 * The virtual cursor, driven the way a gamepad drives it.
	 *
	 * ActivateVirtualCursor turns it on from wherever the pointer is (what a screen that needs one
	 * does). VirtualCursorStick holds the left stick at InStick -- X right, Y up, each in [-1, 1] --
	 * for InSeconds, then lets it go: the stick reaches player 0's controller as analog samples,
	 * which is where the cursor reads it, and the cursor moves the module's pointer. The press and
	 * release are its confirm button (SetConfirmPressed), which it delivers as the left mouse button
	 * at the cursor. Each fails, saying why, while the cursor is not active.
	 */
	FDreamDriverSequence& ActivateVirtualCursor();
	FDreamDriverSequence& VirtualCursorStick(const FVector2D& InStick, float InSeconds);
	FDreamDriverSequence& VirtualCursorPress();
	FDreamDriverSequence& VirtualCursorRelease();

	/**
	 * Characters, one step and one frame each, into the text field that owns the keyboard -- through
	 * UUITextInput::HandleCharacterInput, the road a host that owns real character events uses.
	 *
	 * Nothing is clicked first: a sequence does what it is told, and "type into whatever has focus"
	 * is exactly what a keyboard does. A step with no field being edited fails and says why. A
	 * character the field REFUSES -- read-only, full, not a digit in a number field -- is not a
	 * failure: refusing it is the field's decision, and the one a test is usually there to watch.
	 *
	 * The TCHAR* overload exists because FKey converts from a string literal too, and without it
	 * Type(TEXT("abc")) would not know which of the other two it meant.
	 */
	FDreamDriverSequence& Type(const FString& InText);
	FDreamDriverSequence& Type(const TCHAR* InText);

	/**
	 * One key, one step, one frame -- routed where a game would route it:
	 *  - an armed key selector takes it first (NotifyKeyPressed), as its capture agent sits at the top
	 *    of the input stack;
	 *  - Escape is Back, and goes through UDreamUINavigationStack::HandleBack, which is where the
	 *    standalone input actor sends a Back key nobody bound -- and is what cancels an edit;
	 *  - anything else goes to the text field being edited, through UUITextInput::HandleKeyInput.
	 * With none of them there to take it, the step fails.
	 */
	FDreamDriverSequence& Type(const FKey& InKey);

	/**
	 * A key with a modifier held: Ctrl+A, Shift+Left, Ctrl+Enter. InModifier is one of the eight
	 * modifier keys (Left/Right Shift, Control, Alt, Command); anything else fails the step. The
	 * modifier travels as state with the key -- FModifierKeysState to a text field, FInputChord to a
	 * key selector -- which is how a real key event carries it.
	 */
	FDreamDriverSequence& TypeChord(const FKey& InModifier, const FKey& InKey);

	/** Let InFrameCount frames pass. */
	FDreamDriverSequence& WaitFrames(int32 InFrameCount);

	/**
	 * Give frames to a wait delegate until it passes, fails, or the timeout elapses. See FDreamUntil.
	 *
	 * InDescription is what the step calls itself when it gives up. A wait delegate carries no
	 * description of its own -- it is a bare function -- so without one a timeout can only report the
	 * number of seconds it waited, which tells a reader nothing about what never happened.
	 */
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription);
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout);
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate);

	/** Run something in the middle of the sequence. This is where assertions go. */
	FDreamDriverSequence& Then(TFunction<void(FDreamDriverContext&)> InAction);

	/** Fail here, unconditionally. For a branch a test has decided must not be reached. */
	FDreamDriverSequence& Fail(const FString& InReason);

	/**
	 * Run the whole list now, pumping frames as steps ask for them. Returns false on the first
	 * failure, having reported it through the context's test if it has one.
	 */
	bool Perform();

	/**
	 * Hand the list to the automation framework, one step per engine frame. Returns immediately; the
	 * test body that called it should return true and let the framework finish the work.
	 */
	void PerformLatent();

	/** How many steps are queued. Mostly so a test can assert it built the sequence it meant to. */
	int32 Num() const { return Steps.Num(); }

	/**
	 * Push a step of a kind this class does not know about.
	 *
	 * Public because the five kinds above are the five the RUNTIME needs, and an adapter for something
	 * else -- a designer viewport, a Blueprint recompile -- is a step of its own rather than a reason
	 * to widen this class's vocabulary. Implement IDreamDriverStep and push it here.
	 */
	FDreamDriverSequence& Add(const FDreamDriverStepRef& InStep);

private:
	FDreamDriverContext* Context = nullptr;
	TArray<FDreamDriverStepRef> Steps;
};
