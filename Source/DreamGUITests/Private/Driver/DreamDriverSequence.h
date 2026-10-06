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
struct FDreamDriverVirtualCamera;   // Driver/DreamDriverVirtualCamera.h
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
	/**
	 * Where input enters. The two actor hosts route buttons, wheel, navigation, keys and touch through the game host
	 * (DreamDriverGameHost); SlateSource sends them to the world's Slate input source as Slate's events
	 * (DreamDriverSlateHost); ModuleOnly puts them straight into the module.
	 */
	EDreamRigInputHost InputHost = EDreamRigInputHost::ModuleOnly;
	/**
	 * Under SlateSource, Slate's mouse as the driver last left it: where it is in viewport pixels, and the buttons held
	 * down on it. The module keeps no cursor of its own then, so this is where a relative move starts.
	 */
	FVector2D SlateMousePixel = FVector2D::ZeroVector;
	TSet<FKey> SlateMouseButtonsHeld;
	/** The eye a world-space pointer looks through, once one has been attached. Null keeps every pixel computation exactly as it was. */
	TSharedPtr<FDreamDriverVirtualCamera> Camera;
	/** Set by a rig whose frames are the ENGINE's (PIE). Perform and PumpOneFrame refuse to run then: the engine is already the pump. */
	bool bEnginePumped = false;

	/*
	 * Which player this context speaks for, on a rig with several (FDreamRigOptions::PlayerCount). A context is one
	 * player's: its event system, input module, raycaster, controller, local player and input actor above are that
	 * player's, and Root and RootCanvas are the screen it points at -- the rig's own, or under a split screen its own.
	 * Everything else -- the world, the manager, the game instance, the test -- is the world's and the same on all of them.
	 */
	/** The player's index, which is the UserIndex its event system and raycaster carry. 0 for the rig's own. */
	int32 PlayerIndex = 0;
	/**
	 * Every player's context in player order, this one among them, the same list on each; owned by the rig. Empty on a
	 * context nobody gave players -- a PIE rig, a designer adapter -- which is then the only player there is.
	 */
	TArray<FDreamDriverContext*> Players;
	/**
	 * The part of the viewport this player sees, as ULocalPlayer::Origin and Size put it: fractions of the viewport, the
	 * whole of it unless the rig is a split screen. A world-space pointer attached for this player looks through it.
	 */
	FVector2D ViewOrigin01 = FVector2D::ZeroVector;
	FVector2D ViewSize01 = FVector2D(1.0, 1.0);

	/** The context speaking for InPlayerIndex: this one for its own index, another of Players, or null when there is none. */
	FDreamDriverContext* FindPlayer(int32 InPlayerIndex);
	const FDreamDriverContext* FindPlayer(int32 InPlayerIndex) const;

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
	 * See the implementation for what it calls and why; in short, in UWorld::Tick's order: advance
	 * the world clock (pause and time dilation included), tick the world's Sequencer animations,
	 * let every player's controller process its input when input comes through one, tick the event
	 * system (which is what reaches the input module), step the tweens at the tick groups the tween
	 * helper actor uses, tick the tickable world subsystems, then tick the UI manager (layout,
	 * transforms and clip rectangles) last. Not called under the engine pump -- there the engine's
	 * own frame is the pump, and calling this as well would run everything twice; with
	 * bEnginePumped set it reports an error and does nothing.
	 *
	 * A frame is the world's, not a player's: asked of another player's context, it is player 0's
	 * context that pumps, once, for all of them.
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
	 * own press, hover and drag. No finger is the mouse: finger N is pointer DreamUIPointerIds::ForTouch(N),
	 * 100 + N, and the mouse is pointer 0. TouchUp lifts the finger where it is. Moving or lifting a finger
	 * that is not down fails.
	 */
	FDreamDriverSequence& TouchDown(int32 InFingerId, const FVector2D& InPixel);
	FDreamDriverSequence& TouchMoveTo(int32 InFingerId, const FVector2D& InPixel);
	FDreamDriverSequence& TouchUp(int32 InFingerId);

	/** Where a step aims, worked out by the step itself when it runs: see MoveToResolvedPixel. */
	using FPixelResolver = TFunction<TOptional<FVector2D>(FDreamDriverContext&)>;

	/**
	 * MoveToPixel, TouchDown and TouchMoveTo with the pixel worked out when the step runs rather than when the
	 * sequence is built -- for an aim nobody can know any earlier: a widget a play session builds in WhenReady, a
	 * point inside one wedge of a ring, a row a list opened a frame before. The resolver is handed the step's
	 * context, its camera included; an unset answer fails the step, naming InDescription, as a locator that finds
	 * nothing does. One step and one frame each, exactly as the fixed-pixel forms.
	 */
	FDreamDriverSequence& MoveToResolvedPixel(FPixelResolver InResolver, const FString& InDescription);
	FDreamDriverSequence& TouchDownAtResolvedPixel(int32 InFingerId, FPixelResolver InResolver, const FString& InDescription);
	FDreamDriverSequence& TouchMoveToResolvedPixel(int32 InFingerId, FPixelResolver InResolver, const FString& InDescription);

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
	 * for InSeconds, then lets it go: the stick reaches the player's controller as analog samples,
	 * which is where the cursor reads it, and the cursor moves the module's pointer. The press and
	 * release are its confirm button (SetConfirmPressedForUser), which it delivers as the left mouse
	 * button at the cursor. Each is the sequence's player's cursor (AsPlayer), and each fails,
	 * saying why, while that cursor is not active.
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

	/**
	 * A key pressed and let go of the way the rig's input host delivers a key, with InModifiers held: under ModuleOnly
	 * through DreamUIKeyRouting::RouteKey for the context's player, the chord as its FModifierKeysState; under an actor
	 * host through the player controller's input stack, the modifier keys pressed around it; under SlateSource as
	 * FKeyEvents to the world's Slate input source. So a field being edited, a binding, navigation, Back, paging and tab
	 * switching each get it in the order a game gives it to them, unlike Type. KeyDown then KeyUp: two steps, a frame each,
	 * as Navigate's, so the pipeline acts on the press before the release comes. Fails when the rig has no player to route for.
	 */
	FDreamDriverSequence& Key(const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None);
	/** Key's press alone, and its release alone, each a step of one frame: a key held across frames, which navigation repeats. */
	FDreamDriverSequence& KeyDown(const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None);
	FDreamDriverSequence& KeyUp(const FKey& InKey, EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None);
	/**
	 * Tab, and Shift+Tab, as a keyboard sends them: Key(EKeys::Tab) -- with Shift for ShiftTab -- and, in the press's step
	 * right after the key, the character '\t' through the road characters take in the rig's host (the viewport's
	 * characters, UDreamUIInputSubsystem::HandleViewportCharacter), which a field being edited might type and must not.
	 */
	FDreamDriverSequence& Tab();
	FDreamDriverSequence& ShiftTab();

	/** Let InFrameCount frames pass. */
	FDreamDriverSequence& WaitFrames(int32 InFrameCount);

	/**
	 * The steps added after this are player InPlayerIndex's, until the next AsPlayer: its mouse, its fingers, its keys and
	 * characters, its pad and its Back, through that player's own input entry, aimed through the screen it points at and
	 * read from its own pointers. A sequence starts as the player of the context it was made over -- player 0 for
	 * Rig.Driver(), player N for Rig.Driver(N) -- so a sequence that never says this is exactly what it was.
	 *
	 * Costs no step and no frame; frames are the world's whichever player asked for them. A step for a player the rig has
	 * not got fails, naming the player, rather than being sent as somebody else.
	 */
	FDreamDriverSequence& AsPlayer(int32 InPlayerIndex);

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
	 *
	 * Refused -- an error, and false -- on a context whose frames belong to the engine (bEnginePumped,
	 * a PIE rig): there the list has to be handed to PerformLatent. Every element action goes through
	 * here, so they are refused the same way.
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
	/** Whose the steps added now are (AsPlayer); INDEX_NONE for the context's own player. */
	int32 StepPlayerIndex = INDEX_NONE;
};
