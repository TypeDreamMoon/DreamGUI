// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GenericPlatform/ICursor.h"
#include "InputCoreTypes.h"
#include "Event/DreamUIInputTypes.h"
#include "DreamUIInputUser.generated.h"

class AActor;
class APlayerController;
class ULocalPlayer;
class UDreamBaseEventData;
class UDreamBaseInputModule;
class UDreamBaseRaycaster;
class UDreamEventSystem;
class UDreamGestureEventData;
class UDreamPointerEventData;
class UDreamUIInputSubsystem;
class UDreamWidget;
class UInputComponent;

/**
 * One player's input: their pointers, what each pointer is doing to the world, the device in their hands,
 * the cursor the UI lent their controller, their settings, and the dispatch of every event their pointers
 * produce.
 *
 * Owned by the world's UDreamUIInputSubsystem, one per local player -- made when the player joins, taken
 * down (every hover exited, every press let go) when the player leaves or the world ends -- and one per
 * script user, a player with no local player behind it that a test or a headless rig drives by hand.
 *
 * An event system (UDreamEventSystem) is what a level places for a player. It used to be where all of this
 * lived, which tied a player's input to an actor: a second player had none, an event system destroyed with
 * a sub-level took its player's hovers and presses with it in silence, and a service had to find "the" event
 * system in its tick to hear anything. The event system is now the player's face in the level -- its
 * details panel writes these settings, its Blueprint functions and events read and write them here -- and
 * the input module an input source feeds is the player's.
 */
UCLASS(Transient, NotBlueprintable, BlueprintType, ClassGroup = DreamGUI)
class DREAMGUIINPUT_API UDreamUIInputUser : public UObject
{
	GENERATED_BODY()

public:
	/** Called once by the subsystem that made it. */
	void InitializeUser(int32 InUserIndex, bool bInIsScriptUser);

	UFUNCTION(BlueprintPure, Category = DreamGUI)
	int32 GetUserIndex() const { return UserIndex; }
	/** A player with no local player behind it: a test, a headless rig, a script driving input by hand. */
	UFUNCTION(BlueprintPure, Category = DreamGUI)
	bool IsScriptUser() const { return bIsScriptUser; }
	/** Whether this user has been taken down: its player left, or its world ended. It takes no more input. */
	bool IsShutDown() const { return bShutDown; }

	UDreamUIInputSubsystem* GetInputSubsystem() const;
	virtual UWorld* GetWorld() const override;

	/** This player's controller; see UDreamEventSystem::GetPlayerControllerForUser. */
	UFUNCTION(BlueprintPure, Category = DreamGUI)
	APlayerController* GetPlayerController() const;
	ULocalPlayer* GetLocalPlayer() const;

	// ---------------------------------------------------------------- settings

	FDreamUIInputUserConfig& GetConfig() { return Config; }
	const FDreamUIInputUserConfig& GetConfig() const { return Config; }

	/**
	 * Turn pointer tracing and dispatch on or off. Turning it off with bClearEvent lets go of every pointer
	 * too -- after the event being dispatched, when called from a handler. While it is off a press is not
	 * taken, and a release ends the press it lets go of without a click or a drop (CancelPointerPress).
	 */
	void SetRaycastEnable(bool bEnable, bool bClearEvent);
	/** Let hovered widgets drive the controller's cursor, or give the cursor back. */
	void SetApplyHoverCursor(bool bInApply);

	// ---------------------------------------------------------------- the event system, and the input module

	/** The event system placed for this player, which its settings came from; null when none was. */
	UDreamEventSystem* GetEventSystem() const { return EventSystem.Get(); }
	/**
	 * Called by the subsystem's registry. Defined in the .cpp: storing the pointer converts it to a UObject, which takes
	 * UDreamEventSystem's definition, and this header only declares it.
	 */
	void SetEventSystem(UDreamEventSystem* InEventSystem);
	/**
	 * Every event system that speaks for this player -- the placed one, one a Blueprint was handed when none was
	 * placed, a second one placed by mistake -- each of which relays this player's events to its Blueprint
	 * delegates.
	 */
	void AddEventSystemFacade(UDreamEventSystem* InEventSystem);
	void RemoveEventSystemFacade(UDreamEventSystem* InEventSystem);
	TArray<UDreamEventSystem*, TInlineAllocator<2>> GetEventSystemFacades();

	/** The input module every source for this player feeds. The last one registered wins. */
	UDreamBaseInputModule* GetInputModule() const { return InputModule.Get(); }
	void SetInputModule(UDreamBaseInputModule* InModule);
	/** Forget InModule, if it is the one this player has. */
	void ClearInputModule(const UDreamBaseInputModule* InModule);

	// ---------------------------------------------------------------- pointers

	/**
	 * Pointer InPointerID, or null. Only bCreateIfNotExist mints one: every pointer is traced every frame, so
	 * a query for an id that was never pressed must not leave one behind.
	 */
	UDreamPointerEventData* GetPointerEventData(int32 InPointerID, bool bCreateIfNotExist);
	UDreamPointerEventData* FindPointerEventData(int32 InPointerID) const;
	const TMap<int32, TObjectPtr<UDreamPointerEventData>>& GetPointerEventDataMap() const { return PointerEventDataMap; }
	/**
	 * Take pointer InPointerID away: its drag cancelled or ended, its press let go of, its hovers exited, then
	 * the pointer itself forgotten. After the event being dispatched, when called from a handler.
	 */
	void RetirePointer(int32 InPointerID);
	/**
	 * Forget pointer InPointerID without telling anything -- for a caller that has already let go of what it
	 * held. After the event being dispatched, when called from a handler.
	 */
	void RemovePointerEventData(int32 InPointerID);
	/** Set a pointer's input type, broadcasting the change. @return true when it changed. */
	bool SetPointerInputType(UDreamPointerEventData* InEventData, EDreamUIPointerInputType InInputType);

	/** InPointerID's world-target state; null when it has none and bCreateIfNotExist is false. Map storage: not to be held across a dispatch. */
	FDreamUIPointerWorldTarget* GetPointerWorldTarget(int32 InPointerID, bool bCreateIfNotExist);
	AActor* GetHoveredWorldTarget(int32 InPointerID) const;
	AActor* GetPressedWorldTarget(int32 InPointerID) const;

	/**
	 * Focus InWidget, on behalf of InEventData's pointer: Deselect to what the player had focused, Select to InWidget.
	 * The focus is the player's, not the pointer's -- whichever pointer moved it, and it stays when that pointer goes:
	 * a finger lifted from a field leaves the field focused, as a click does. Every pointer's record of it
	 * (SelectedComponent, which Blueprint reads) follows.
	 */
	void SetSelectWidget(UDreamWidget* InWidget, UDreamBaseEventData* InEventData);
	/** What this player has focused: the widget their keys, characters and sticks go to. */
	UFUNCTION(BlueprintPure, Category = DreamGUI)
	UDreamWidget* GetFocusedWidget() const { return FocusedWidget.Get(); }

	// ---------------------------------------------------------------- text

	/**
	 * The field this player is typing into, or null. It claims the player's keyboard when its edit starts and lets go
	 * when the edit ends. While it holds it, the keys it takes are bound on the player's controller above everything
	 * else the controller listens to, so they reach nothing else -- and only this player's keys: another player's
	 * field is not touched.
	 */
	UObject* GetTextTarget() const { return TextTarget.Get(); }
	/** InTarget, which implements IDreamUITextInputTarget, takes this player's keyboard. */
	void SetTextTarget(UObject* InTarget);
	/** InTarget lets go of this player's keyboard; nothing happens when it does not hold it. */
	void ClearTextTarget(const UObject* InTarget);
	/** Bind the text target's keys on the player's controller again, for a target whose keys changed. */
	void RefreshTextKeys();

	// ---------------------------------------------------------------- keys

	/** Remember where the press of InKey went, until its release asks (DreamUIKeyRouting). A second press overwrites the first. */
	void NoteKeyPress(const FKey& InKey, const FDreamUIKeyPress& InPress);
	/** Where the press of InKey went, forgotten as it is read. False for a key whose press this player did not route. */
	bool TakeKeyPress(const FKey& InKey, FDreamUIKeyPress& OutPress);
	/** Where the press of InKey went, or null when this player routed no press of it whose release has not come. */
	const FDreamUIKeyPress* FindKeyPress(const FKey& InKey) const { return KeyPresses.Find(InKey); }

	// ---------------------------------------------------------------- device and cursor

	EDreamUIInputDevice GetCurrentInputDevice() const { return CurrentInputDevice; }
	/** Tell this player which device was just used. Broadcasts only a change. @return true when it changed. */
	bool ReportInputDevice(EDreamUIInputDevice InDevice);
	EDreamUIGamepadModel GetCurrentGamepadModel() const { return CurrentGamepadModel; }
	void SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel);
	bool RefreshGamepadModel();

	/** Push the cursor a hovered widget asked for onto this player's controller, or put back the project's. */
	void ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor);
	/** Give back the cursor the UI took over, if it took one. */
	void RestoreHoverCursor();

	// ---------------------------------------------------------------- events

	/** Every pointer and navigation event this player's pointers produce, after its handlers ran. */
	FDreamUIMulticastDelegateBaseEventData& GetInputEvent() { return InputEvent; }
	FDreamUIRaycastHitDelegate& GetRaycastHitEvent() { return RaycastHitEvent; }
	FDreamUIPointerInputTypeChangedDelegate& GetInputChangedEvent() { return PointerInputTypeChangedEvent; }
	FDreamUIInputDeviceChangedDelegate& GetInputDeviceChangedEvent() { return InputDeviceChangedEvent; }
	FDreamUIGamepadModelChangedDelegate& GetGamepadModelChangedEvent() { return GamepadModelChangedEvent; }

	void RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& InHitResult, UDreamWidget* InHitWidget);

	// ---------------------------------------------------------------- dispatch

	void CallOnPointerEnter(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerExit(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerDown(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerUp(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerClick(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerDoubleClick(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerLongPress(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerPinch(UDreamWidget* InWidget, UDreamGestureEventData* InEventData);
	void CallOnPointerSwipe(UDreamWidget* InWidget, UDreamGestureEventData* InEventData);
	void CallOnPointerBeginDrag(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerDrag(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerEndDrag(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerScroll(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerDragDrop(UDreamWidget* InWidget, UDreamPointerEventData* InEventData);
	void CallOnPointerSelect(UDreamWidget* InWidget, UDreamBaseEventData* InEventData);
	void CallOnPointerDeselect(UDreamWidget* InWidget, UDreamBaseEventData* InEventData);

	void CallOnWorldTargetEnter(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetExit(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetDown(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetUp(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetClick(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetDoubleClick(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetLongPress(AActor* InTarget, UDreamPointerEventData* InEventData);
	void CallOnWorldTargetScroll(AActor* InTarget, UDreamPointerEventData* InEventData);

	void LogEventData(UDreamBaseEventData* InEventData) const;

	/**
	 * While an event is being dispatched to game code -- or a pointer's frame is being worked through -- the
	 * calls that change pointer state (letting pointers go, retiring one, turning tracing off with a clear,
	 * moving the event system to another player) do not happen in the middle of it: they run once it is over,
	 * in the order they were made. A handler used to reach them re-entrantly and empty the very arrays the
	 * pipeline was walking. Queries answer from the state as it stands.
	 */
	bool IsDispatching() const { return DispatchDepth > 0; }
	/** Run InCommand now, or after the event being dispatched when one is. */
	void RunOrDefer(TFunction<void()>&& InCommand);

	// ---------------------------------------------------------------- the frame

	/** One frame of this player's input, from the subsystem's tick: nothing while tracing is off, else the module's frame. */
	void ProcessFrame(float InDeltaSeconds);
	/**
	 * The pipeline itself: the queued presses and releases in the order they arrived, then every pointer's
	 * trace and state, then the queued wheel turns, then the pinch. What UDreamBaseInputModule::ProcessInput
	 * runs. Refused, with an ensure, when a handler calls it from inside itself.
	 */
	void RunPipeline();
	bool IsInPipeline() const { return bInPipeline; }

	/**
	 * Move pointer InPointerID to InPosition: state the next frame reads. A pointer that actually moves is in pointer
	 * mode, not navigation -- but only an actual move, so a source reporting the position every frame does not keep
	 * taking navigation away.
	 */
	void MovePointer(int32 InPointerID, const FVector& InPosition);
	/** A navigation direction pressed -- the pointer goes into navigation mode, stepping that way -- or released. */
	void InputNavigation(EDreamUINavigationDirection InDirection, bool bInPressed, int32 InPointerID);
	/** The navigation confirm pressed or released, for the navigation cursor on InPointerID. */
	void InputTriggerForNavigation(bool bInPressed, int32 InPointerID);
	/** A press or release to be dispatched on this player's next frame, at InPosition. */
	void QueuePointerButton(int32 InPointerID, const FVector& InPosition, bool bInPressed, EDreamUIMouseButtonType InButton, bool bInIsTouch);
	/** A wheel turn for InPointerID, dispatched on the next frame to what the pointer is over by then. */
	void QueuePointerScroll(int32 InPointerID, const FVector2D& InAxisValue);
	/** Whether presses, releases or wheel turns are waiting for the next frame. */
	bool HasQueuedInput() const { return QueuedButtons.Num() > 0 || QueuedScrolls.Num() > 0; }

	/**
	 * Let go of pointer InPointerID: its drag ended or cancelled, its press let go of without a click, its
	 * hovers exited -- the widgets' and the world target's. It stays a pointer.
	 */
	void ReleasePointer(int32 InPointerID);
	/**
	 * End pointer InPointerID's press without a click or a drop -- its up, its drag ended or cancelled -- for a button let
	 * go of where this player could not see it: the application lost the focus with it held, or tracing was off. A press
	 * or release of it still waiting for the next frame is dropped with it, and so is a navigation confirm the next frame
	 * has not read yet. Its hovers are left as they are. After the event being dispatched, when called from a handler.
	 */
	void CancelPointerPress(int32 InPointerID);
	/** ReleasePointer for every pointer. After the event being dispatched, when called from a handler. */
	void ReleaseAllPointers();
	/**
	 * Let go of every pointer and forget them: what an input source leaving this player does, so that no pointer it
	 * was feeding is left behind to be traced, from where it last was, as if still in the player's hand. After the
	 * event being dispatched, when called from a handler.
	 */
	void RetireAllPointers();
	/**
	 * The player is going away -- they left, or their world is ending: every pointer released and retired, the
	 * selection cleared, the cursor given back. Runs while the world is still whole, so every handler still
	 * has a world to act in. Nothing is dispatched afterwards.
	 */
	void Shutdown();

	/** How many traces this player's pointers have made. A pointer that has not moved over UI that has not changed makes none. */
	int32 GetLineTraceCount() const { return LineTraceCount; }

	/** The navigation interval floor; see UDreamEventSystem::SetNavigateInputInterval. */
	static constexpr float MinNavigateInputInterval = 0.01f;

	// ---------------------------------------------------------------- pipeline steps (the pointer module's state machine calls back into these)

	/** Trace InEventData's pointer through this player's raycasters. @return true when it hit something. */
	bool LineTrace(UDreamPointerEventData* InEventData, FDreamUIHitResultContainer& OutHitResult);
	/** One navigation step or confirm edge for a pointer in navigation mode. */
	void ProcessInputForNavigation(UDreamPointerEventData* InEventData);
	/** Resolve where a navigation step lands. @return true when it found something. */
	bool Navigate(EDreamUINavigationDirection InDirection, UDreamPointerEventData* InEventData, FDreamUIHitResultContainer& OutHitResult);
	/** Two pressed fingers moving apart or together. */
	void ProcessPinchGesture();

private:
	friend struct FDreamUIInputDispatchScope;

	/** Broadcast InEventData after its handlers ran: this player's own event, the world's, and the event system's Blueprint one. */
	void BroadcastInputEvent(UDreamBaseEventData* InEventData);
	/** Run what RunOrDefer held back, once nothing is being dispatched. */
	void DrainDeferredCommands();
	/** RunPipeline's body. */
	void RunPipelineBody();
	/** ReleasePointer's body, never deferred: the pipeline itself calls it. */
	void ReleasePointerNow(int32 InPointerID);
	/** The press half of ReleasePointerNow: the drag ended or cancelled, the up, the actor let go of, and no click. */
	void EndPressNow(UDreamPointerEventData* InEventData);
	/** Keep a lifted finger's click run for the next tap of that finger, which comes as a pointer of its own. */
	void KeepLiftedFingerClickRun(const UDreamPointerEventData* InEventData);
	/** Give a finger's new pointer the click run its last tap left, if it left one. */
	void RestoreLiftedFingerClickRun(UDreamPointerEventData* InEventData);
	/** Remember the raycaster InEventData's press went through while the press is held, and forget it once it is not. */
	void NotePressRaycaster(const UDreamPointerEventData* InEventData);
	/** Let go of every press whose raycaster has gone since: its up, and no click. */
	void ReleasePressesWhoseRaycasterWent();
	/** Every pointer's record of the focus follows the player's. */
	void MirrorFocusOntoPointers();
	/** One of the text target's keys, pressed or repeating on this player's keyboard. */
	void HandleTextKey(FKey InKey);
	/** One of the text target's keys going down: typed, and noted as the field's, so its release is the field's too. */
	void HandleTextKeyPressed(FKey InKey);
	/**
	 * One of the text target's keys coming up. The text keys take their keys from everything below them on the
	 * controller, the release included, so the release is sent from here to whatever took the press -- a key held since
	 * before the field began its edit is still the navigation's or the bindings'. A release routed already -- its press
	 * taken off the books by the source that heard it first -- or one whose press was never routed is left alone.
	 */
	void HandleTextKeyReleased(FKey InKey);
	/** Take the text keys off the controller they are on. */
	void PopTextKeys();

	int32 UserIndex = 0;
	bool bIsScriptUser = false;
	bool bShutDown = false;
	bool bInPipeline = false;
	int32 DispatchDepth = 0;
	bool bDrainingDeferredCommands = false;
	TArray<TFunction<void()>> DeferredCommands;

	FDreamUIInputUserConfig Config;

	TWeakObjectPtr<UDreamEventSystem> EventSystem;
	TArray<TWeakObjectPtr<UDreamEventSystem>> EventSystemFacades;
	TWeakObjectPtr<UDreamBaseInputModule> InputModule;

	UPROPERTY(VisibleAnywhere, Category = DreamGUI)
	TMap<int32, TObjectPtr<UDreamPointerEventData>> PointerEventDataMap;
	/** See FDreamUIPointerWorldTarget. Not reflected: weak pointers and a time. */
	TMap<int32, FDreamUIPointerWorldTarget> PointerWorldTargetMap;

	EDreamUIInputDevice CurrentInputDevice = EDreamUIInputDevice::MouseAndKeyboard;
	EDreamUIGamepadModel CurrentGamepadModel = EDreamUIGamepadModel::Generic;
	bool bGamepadModelOverridden = false;

	/** What the controller's cursor was before a widget first claimed it, so it can be put back. */
	TEnumAsByte<EMouseCursor::Type> CursorBeforeHoverOverride = EMouseCursor::Default;
	bool bHoverCursorOverrideActive = false;

	FDreamUIMulticastDelegateBaseEventData InputEvent;
	FDreamUIRaycastHitDelegate RaycastHitEvent;
	FDreamUIPointerInputTypeChangedDelegate PointerInputTypeChangedEvent;
	FDreamUIInputDeviceChangedDelegate InputDeviceChangedEvent;
	FDreamUIGamepadModelChangedDelegate GamepadModelChangedEvent;

	struct FQueuedButton
	{
		int32 PointerID = 0;
		FVector Position = FVector::ZeroVector;
		bool bPressed = false;
		EDreamUIMouseButtonType Button = EDreamUIMouseButtonType::Left;
		bool bIsTouch = false;
		/** On the pointer clock (UDreamEventSystem::GetPointerClockSeconds), when it arrived. */
		double ClockSeconds = 0.0;
	};
	TArray<FQueuedButton> QueuedButtons;
	struct FQueuedScroll
	{
		int32 PointerID = 0;
		FVector2D AxisValue = FVector2D::ZeroVector;
	};
	TArray<FQueuedScroll> QueuedScrolls;

	/**
	 * The raycaster each held press went through. Weak, and only ever set to a live one: a raycaster that is gone by
	 * the next frame -- a render-target surface destroyed while one of its buttons was held -- is how the press is
	 * known to have nothing left to be released over.
	 */
	TMap<int32, TWeakObjectPtr<UDreamBaseRaycaster>> PressRaycasters;

	/** The player's focus. Weak: a focused widget destroyed is simply no longer focused. */
	TWeakObjectPtr<UDreamWidget> FocusedWidget;
	TWeakObjectPtr<UObject> TextTarget;
	/** The text target's keys, bound for this player and pushed on their controller while there is a target. */
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TextKeys;
	TWeakObjectPtr<APlayerController> TextKeysController;
	/** Where each held key's press went; see NoteKeyPress. */
	TMap<FKey, FDreamUIKeyPress> KeyPresses;

	/**
	 * A lifted finger's click run. A finger's pointer goes when the finger lifts, and the next tap of that finger is a
	 * pointer of its own; without its predecessor's run it starts a run of its own, and two quick taps were never a
	 * double tap. Weak: a run does not keep the widget or the actor it landed on alive.
	 */
	struct FLiftedFingerClickRun
	{
		int32 ClickCount = 0;
		double ClickTime = 0.0;
		TWeakObjectPtr<UDreamWidget> LastClickWidget;
		EDreamUIMouseButtonType LastClickMouseButtonType = EDreamUIMouseButtonType::Left;
		FVector LastClickPressPointerPosition = FVector::ZeroVector;
		FVector LastClickPressWorldPoint = FVector::ZeroVector;
		TWeakObjectPtr<AActor> LastClickedActor;
		double LastClickedActorTime = 0.0;
	};
	/** By the finger's pointer id. */
	TMap<int32, FLiftedFingerClickRun> LiftedFingerClickRuns;

	/** One pointer's last trace, reused while nothing that could change it has. */
	struct FTraceCache
	{
		FVector Position = FVector::ZeroVector;
		uint64 Generation = 0;
		bool bPaused = false;
		bool bHit = false;
		/** The trace's answer, its object pointers held weakly: the cache outlives the frame it was made in. */
		FDreamUIHitResult Hit;
		FVector RayOrigin = FVector::ZeroVector;
		FVector RayDirection = FVector(1, 0, 0);
		FVector RayEnd = FVector(1, 0, 0);
		TWeakObjectPtr<UDreamBaseRaycaster> Raycaster;
		TArray<TWeakObjectPtr<UDreamWidget>> Hovered;
	};
	TMap<int32, FTraceCache> TraceCache;
	int32 LineTraceCount = 0;
	/** Temporaries of LineTrace, kept so their capacity survives the frame. */
	TArray<FDreamUIHitResultContainer> MultiHitResult;
	TArray<FDreamUIHitResult> HitResultArray;

	/** Pinch state. Lives only between the frame the second finger lands and the frame one leaves. */
	bool bPinchActive = false;
	double PinchStartDistance = 0.0;
	double PinchLastReportedDistance = 0.0;
	UPROPERTY(Transient)
	TObjectPtr<UDreamGestureEventData> PinchEventData;
};

/**
 * Counts a dispatch on InUser for as long as it lives, and runs the commands handlers deferred once the
 * outermost one ends.
 */
struct DREAMGUIINPUT_API FDreamUIInputDispatchScope
{
	explicit FDreamUIInputDispatchScope(UDreamUIInputUser* InUser);
	~FDreamUIInputDispatchScope();
	UE_NONCOPYABLE(FDreamUIInputDispatchScope);
private:
	TWeakObjectPtr<UDreamUIInputUser> User;
};
