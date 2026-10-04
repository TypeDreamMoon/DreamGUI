// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamUIInputTypes.h"
#include "GenericPlatform/ICursor.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamEventSystem.generated.h"

class UDreamPointerEventData;
class UDreamGestureEventData;
class UDreamBaseInputModule;
class UDreamUIInputSubsystem;
class UDreamUIInputUser;
class APlayerController;
class ULocalPlayer;

/**
 * A player's input, as a level places it and a Blueprint reaches it.
 *
 * The input itself -- the pointers, what they hover and press, the device, the cursor, the dispatch -- belongs
 * to the player (UDreamUIInputUser), which the world's input subsystem owns and runs. This component speaks
 * for one player, UserIndex: its details panel is that player's settings, written into the player when it
 * begins play; its functions read and change the player's input; its events relay the player's. It no longer
 * ticks -- the input subsystem's tick function runs every player's frame -- and it keeps nothing a player could
 * lose with it: an event system destroyed with a sub-level lets its player's hovers and presses go, properly.
 *
 * About event bubble: if all interface of target component return true, then event will bubble up. if no
 * interface found on target, then event will bubble up
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), HideCategories = (Sockets, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation))
class DREAMGUIINPUT_API UDreamEventSystem : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamEventSystem();

	/**
	 * The event system for player UserIndex: the one placed for that player, or -- when none is -- one no actor
	 * carries that speaks for the player all the same.
	 */
	// static-checks: allow(ufunction-param) the Blueprint pin has always been UserIndex; renaming it would unhook every node
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Event System Instance"))
		static UDreamEventSystem* GetDreamEventSystemInstance(UObject* WorldContextObject, int UserIndex);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy()override;

	/** Drop this event system's registration from the world's input subsystem. Idempotent; called from both ends. */
	void UnregisterFromInputSubsystem();
	/**
	 * The input subsystem this registered with. Remembered rather than looked up again on the way out, because
	 * GetWorld() is routinely null by BeginDestroy and the lookup would silently find nothing.
	 */
	TWeakObjectPtr<UDreamUIInputSubsystem> RegisteredInputSubsystem;

protected:

	UPROPERTY(EditAnywhere, Category = DreamGUI, Getter)
	int UserIndex = 0;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bOutputLog = false;
#endif
	UPROPERTY(VisibleAnywhere, Category = DreamGUI)
		bool bRayEventEnable = true;
	/**
	 * Let hovered widgets drive the hardware cursor (UDreamWidget::Cursor).
	 *
	 * On by default, because a cursor that changes over a button is how a player learns what is clickable. Off
	 * for a project that owns the cursor itself -- an RTS with a build cursor, a game with a custom software
	 * cursor -- so the two do not write the same field on alternate frames.
	 */
	UPROPERTY(EditAnywhere, Getter = "GetApplyHoverCursor", Setter = "SetApplyHoverCursor", Category = DreamGUI)
		bool bApplyHoverCursor = true;

	/** The input module registered through this event system; its player's, once it has one. */
	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	TWeakObjectPtr<UDreamBaseInputModule> CurrentInputModule = nullptr;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetUserIndex()const{return UserIndex;}

	/**
	 * Point this event system at another player. Its registration moves with it, and the player it leaves lets go
	 * of every pointer -- after the event being dispatched, when called from a handler.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetUserIndex(int Value);

	/**
	 * The player controller this event system speaks for -- the local player at UserIndex, not "whoever is
	 * first". Null when that local player does not exist, except for index 0, which falls back to the first
	 * controller so a single-player game keeps working before any local player is registered.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	APlayerController* GetPlayerController()const;

	/**
	 * The player controller for a user index, without needing an event system in hand.
	 *
	 * This is the one place the "which player is this?" question is answered, so that a raycaster source, a
	 * tooltip or a rumble call cannot quietly disagree with the pointer they were handed. Every
	 * UDreamPointerEventData carries the index of the player that made it.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (WorldContext = "WorldContextObject"))
	static APlayerController* GetPlayerControllerForUser(const UObject* WorldContextObject, int InUserIndex);
	/** The local player for a user index. Same rule as GetPlayerControllerForUser. */
	static ULocalPlayer* GetLocalPlayerForUser(const UObject* WorldContextObject, int InUserIndex);

	/** The player this event system speaks for, found on first need. Null outside a world with DreamUI input. */
	UDreamUIInputUser* GetInputUser() const;
	/** Speak for InUser from now on. Called by the input subsystem; see also GetInputUser. */
	void BindToUser(UDreamUIInputUser* InUser);
	/** Stop speaking for the player this is bound to. */
	void UnbindFromUser();
	/** Write this event system's details-panel settings into InUser: what the placed one does when it registers. */
	void WriteSettingsToUser(UDreamUIInputUser* InUser) const;
	/** Make this the event system the input subsystem hands a Blueprint for InUser when none is placed. */
	void InitializeImplicit(UDreamUIInputSubsystem* InSubsystem, UDreamUIInputUser* InUser);

	/**
	 * Push the cursor a hovered widget asked for onto this player's controller, or put back whatever the project
	 * had when nothing asks for one. Turn bApplyHoverCursor off to leave the cursor entirely alone.
	 * @param bWidgetClaimedCursor	False when no widget in the hover stack claimed a cursor.
	 */
	void ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetApplyHoverCursor()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetApplyHoverCursor(bool Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamBaseInputModule* GetCurrentInputModule()const{return CurrentInputModule.Get();}
	void SetInputModule(UDreamBaseInputModule* InputModule);
	void ClearInputModule();

	/**
	 * Let go of every pointer: each hovered widget gets its Exit, each pressed one its Up, each drag its end.
	 * After the event being dispatched, when called from a handler.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void ClearEvent();
	/**
	 * SetRaycast enable or disable
	 * @param	bClearEvent		call ClearEvent after disable Raycast
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRaycastEnable(bool bEnable, bool bClearEvent = false);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSelectWidget(UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData);
	/** Select InSelectWidget through InEventSystem's player, or -- with no event system -- straight on the widgets. */
	static void SetSelectWidget(UDreamEventSystem* InEventSystem, UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSelectComponentWithDefault(UDreamWidget* InSelectWidget);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidget* GetCurrentSelectedComponent(int InPointerID)const;

	/**
	 * Get PointerEventData by given pointerID.
	 * @param	PointerID	0 for the mouse, 100 plus the finger for a touch (see DreamUIPointerIds), or a value of your own
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamPointerEventData* GetPointerEventData(int PointerID = 0, bool bCreateIfNotExist = false)const;
	/**
	 * Remove a PointerEventData. If you ensure that you will not use it anymore, then you can remove it. After the
	 * event being dispatched, when called from a handler.
	 * @param	PointerID	0 for the mouse, 100 plus the finger for a touch (see DreamUIPointerIds), or a value of your own
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void RemovePointerEventData(int PointerID);
protected:
	/** called for pointer hit anything */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName="RaycastHitEvent")
	FDreamUIRaycastHitDynamicDelegate RaycastHitEventBP;

	/** called for all pointer && navigation event */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName="InputEvent")
	FDreamUIBaseEventDataDynamicDelegate InputEventBP;
public:
	/** This player's pointers; empty when the event system speaks for no player. */
	const TMap<int32, TObjectPtr<UDreamPointerEventData>>& GetPointerEventDataMap()const;

	FDreamUIRaycastHitDelegate& GetRaycastHitEvent();
	FDreamUIMulticastDelegateBaseEventData& GetInputEvent();
	FDreamUIPointerInputTypeChangedDelegate& GetInputChangedEvent();

	void RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent);

	/** Relay the player's events to this event system's Blueprint delegates. Called by the player. */
	void BroadcastBlueprintInputEvent(UDreamBaseEventData* InEventData);
	void BroadcastBlueprintRaycastHit(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent);
	void BroadcastBlueprintInputDeviceChanged(EDreamUIInputDevice InDevice);
	void BroadcastBlueprintGamepadModelChanged(EDreamUIGamepadModel InModel);

	/**
	 * Tell if the pointer hovering on any UI object.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		bool IsPointerOverUIByPointerID(int PointerID = 0);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetHighlightedComponentForNavigation(UDreamWidget* InComp, int InPointerID);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidget* GetHighlightedComponentForNavigation(int InPointerID)const;

	/**
	 * Set input type of the pointer, can be pointer or navigation.
	 * @return true- input type changed, false- otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		bool SetPointerInputTypeByPointerID(int InPointerID, EDreamUIPointerInputType InInputType);
	/**
	 * Set input type of the pointer, can be pointer or navigation..
	 * @return true- input type changed, false- otherwise.
	 */
	bool SetPointerInputType(UDreamPointerEventData* InPointerEventData, EDreamUIPointerInputType InInputType);
	/**
	 * Set the pointer's inputType as navigation.
	 * @param InPointerID target pointer's ID.
	 * @param InDefaultHighlightedComponent default highlighted component for navigation input.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void ActivateNavigationInput(int InPointerID, UDreamWidget* InDefaultHighlightedComponent = nullptr);
private:
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI)
	EDreamUIPointerInputType DefaultInputType = EDreamUIPointerInputType::Pointer;
	/**
	 * If keep pressing the navigate button for a while, then will trigger the process of continuous navigation.
	 * This is the time to trigger the process.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float NavigateInputIntervalForFirstTime = 0.5f;
	/**
	 * If keep pressing the navigate button for a while, then will trigger the process of continuous navigation.
	 * This is the interval trigger time of continuous navigation
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float NavigateInputInterval = 0.2f;
	/**
	 * How long after a click a second press on the same widget still counts as a double click -- and a double
	 * click is delivered at that press, in place of its down, as Slate delivers one. The press also has to land
	 * within the pointer's drag threshold of the first (see UDreamBaseRaycaster::IsWithinDoubleClickDistance).
	 * Zero disables double clicks entirely, and makes every press a down.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DoubleClickTime = 0.3f;
	/**
	 * How long the trigger must be held on one widget before a long press is dispatched. Zero turns long press
	 * off. A press that has already become a drag never produces one -- see IDreamPointerLongPressInterface.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LongPressTime = 0.5f;
	/**
	 * How far a touch has to travel, in viewport pixels, before its release counts as a swipe, and how long it
	 * may take. A slow drag across the screen is a drag, not a swipe.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SwipeMinDistance = 80.0f;
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SwipeMaxDuration = 0.5f;
	/**
	 * How far the two fingers of a pinch must move apart or together, in viewport pixels, before the gesture is
	 * reported at all.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PinchMinDistanceChange = 12.0f;
	/**
	 * Scroll the containers around a navigated-to widget until it is on screen. Off, navigation can only reach
	 * what is already visible.
	 */
	UPROPERTY(EditAnywhere, Getter = "GetScrollNavigationTargetIntoView", Setter = "SetScrollNavigationTargetIntoView", Category = DreamGUI)
	bool bScrollNavigationTargetIntoView = true;
	/** Ease that reveal scroll instead of jumping to it. */
	UPROPERTY(EditAnywhere, Getter = "GetAnimateNavigationScroll", Setter = "SetAnimateNavigationScroll", Category = DreamGUI, meta = (EditCondition = "bScrollNavigationTargetIntoView"))
	bool bAnimateNavigationScroll = true;

public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIPointerInputType GetDefaultInputType()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetNavigateInputIntervalForFirstTime()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetNavigateInputInterval()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetDoubleClickTime()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetLongPressTime()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetSwipeMinDistance()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetSwipeMaxDuration()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetPinchMinDistanceChange()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetScrollNavigationTargetIntoView()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetAnimateNavigationScroll()const;

	/**
	 * The clock every timed pointer gesture is measured on: the PressTime, ReleaseTime and ClickTime stamps on
	 * UDreamPointerEventData, and everything measured against them -- long press, the double-click window, swipe
	 * duration, hold-to-drag -- plus navigation repeat.
	 *
	 * The world's REAL time: not stopped by a pause, not stretched by time dilation. The UI keeps working while
	 * the game is paused, so a pause menu is exactly where a held press still has to become a long press and two
	 * clicks a second apart still have to be two clicks. Slate times its own double clicks and key repeat in real
	 * time for the same reason.
	 *
	 * One function so that the code writing a stamp and the code measuring it cannot disagree about which clock
	 * it is: compare a stamp on the event data against this, never against the game clock.
	 * @return	Seconds on that clock, or 0 when the object is in no world.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (WorldContext = "WorldContextObject"))
	static double GetPointerClockSeconds(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDefaultInputType(EDreamUIPointerInputType Value);
	/**
	 * Both navigation intervals are floored rather than taken as given: they are the step of the
	 * continuous-navigation timer, and zero (or a negative) there is not "as fast as possible", it is a deadline
	 * that can never be pushed past the current time.
	 */
	static constexpr float MinNavigateInputInterval = 0.01f;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetNavigateInputIntervalForFirstTime(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetNavigateInputInterval(float Value);
	/** Floored at zero, which means "no double clicks" rather than "every click is one". */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDoubleClickTime(float Value);
	/** Floored at zero, which means "no long press" rather than "every press is one". */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetLongPressTime(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetSwipeMinDistance(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetSwipeMaxDuration(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetPinchMinDistanceChange(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetScrollNavigationTargetIntoView(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetAnimateNavigationScroll(bool Value);

#pragma region InputDevice
	/** What the player last used. Key prompts and cursor visibility both hang off this. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIInputDevice GetCurrentInputDevice()const;
	/**
	 * Tell the player a device was just used. Called for every key the input actor sees, so it must stay cheap
	 * and must only broadcast on an actual change.
	 * @return true when the device changed.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool ReportInputDevice(EDreamUIInputDevice InDevice);
	/** Classify a key. Gamepad and touch keys announce themselves; everything else is a keyboard. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	static EDreamUIInputDevice GetInputDeviceForKey(const FKey& InKey);

	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName = "InputDeviceChangedEvent")
	FDreamUIInputDeviceChangedDynamicDelegate InputDeviceChangedEventBP;
	FDreamUIInputDeviceChangedDelegate& GetInputDeviceChangedEvent();

	/**
	 * Which pad the player is holding. Generic until one is used and named by the platform. Read from the pad that sent
	 * the player's latest pad input, again whenever that is another pad, rather than polled.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIGamepadModel GetCurrentGamepadModel()const;
	/**
	 * Force the model, for a project that knows better than the platform does. Generic-with-override is still an
	 * override: pass bInOverride false to go back to detection.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel = EDreamUIGamepadModel::Generic);
	/** Ask the platform again, about the pad the player used last. Pad input from another pad asks by itself. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool RefreshGamepadModel();
	/**
	 * The model a platform device name implies, as a pure function so it can be tested without hardware. Both
	 * halves of the descriptor are consulted because platforms disagree about which one carries the brand.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	static EDreamUIGamepadModel GetGamepadModelForDeviceName(FName InInputDeviceName, FName InHardwareDeviceIdentifier);

	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName = "GamepadModelChangedEvent")
	FDreamUIGamepadModelChangedDynamicDelegate GamepadModelChangedEventBP;
	FDreamUIGamepadModelChangedDelegate& GetGamepadModelChangedEvent();
#pragma endregion
public:
	/**
	 * Dispatch EventData to every behaviour of Widget that implements InterfaceClass, bubbling to the parent while
	 * AllowEventBubbleUp holds and no handler said stop.
	 *
	 * A handler is game code, and the widget it was handed is not the same widget afterwards in every case: it may
	 * have destroyed it, or moved it under another parent. Bubbling stops at a level that is no longer valid, or
	 * whose parent is not the one it had when its handlers were called -- the event was meant for the hierarchy
	 * as it stood, and delivering the rest of it into a subtree the widget has just been moved into, or out of a
	 * dead one, is how handlers came to run against state nobody expected.
	 */
	template<class UEventData, class UInterfaceFunction>
	static void ExecuteDreamUIInterface(UDreamWidget* Widget,
		UEventData* EventData,
		UClass* InterfaceClass, UInterfaceFunction InterfaceFunction,
		bool AllowEventBubbleUp)
	{
		if (!IsValid(Widget))
		{
			return;
		}
		bool TempAllowEventBubbleUp = AllowEventBubbleUp;
		UDreamWidget* const ParentBeforeHandlers = Widget->GetParent();
		// A copy, not the live array: InterfaceFunction runs game code, and a handler that adds or removes a
		// behaviour on the widget it was just dispatched to reallocates the very array being walked.
		TArray<UDreamUIBehaviour*> ComponentArray = Widget->GetAllComponents();
		for (auto& Comp : ComponentArray)
		{
			if (!IsValid(Comp))continue;
			if (Comp->GetClass()->ImplementsInterface(InterfaceClass))
			{
				if (InterfaceFunction(Comp, EventData) == false)
				{
					TempAllowEventBubbleUp = false;
				}
			}
		}
		if (TempAllowEventBubbleUp && IsValid(Widget) && IsValid(ParentBeforeHandlers) && Widget->GetParent() == ParentBeforeHandlers)
		{
			ExecuteDreamUIInterface(ParentBeforeHandlers,
				EventData,
				InterfaceClass, InterfaceFunction, true);
		}
	}
	template<class UEventData, class UInterfaceFunction, class UBubbleUpFunction>
	static void BubbleDreamUIInterface(UDreamWidget* Widget,
		UEventData* EventData, UClass* InterfaceClass,
		UInterfaceFunction InterfaceFunction, UBubbleUpFunction BubbleUpFunction)
	{
		if (!IsValid(Widget))
		{
			return;
		}
		bool TempAllowEventBubbleUp = true;
		UDreamWidget* const ParentBeforeHandlers = Widget->GetParent();
		// Same reasons as ExecuteDreamUIInterface above.
		TArray<UDreamUIBehaviour*> ComponentArray = Widget->GetAllComponents();
		for (auto& Comp : ComponentArray)
		{
			if (!IsValid(Comp))continue;
			if (Comp->GetClass()->ImplementsInterface(InterfaceClass))
			{
				if (InterfaceFunction(Comp, EventData) == false)
				{
					TempAllowEventBubbleUp = false;
				}
			}
		}
		if (TempAllowEventBubbleUp && IsValid(Widget) && IsValid(ParentBeforeHandlers) && Widget->GetParent() == ParentBeforeHandlers)
		{
			BubbleUpFunction(ParentBeforeHandlers, EventData);
		}
	}

	// The handlers on a widget, with nothing else: no player, no broadcast. Blueprint tools for dispatching an event
	// by hand; a player's own events go through its UDreamUIInputUser, which dispatches with these and broadcasts.
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerEnter(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerExit(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerDown(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerUp(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerClick(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerDoubleClick(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerLongPress(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerPinch(UDreamWidget* TargetWidget, UDreamGestureEventData* GestureEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerSwipe(UDreamWidget* TargetWidget, UDreamGestureEventData* GestureEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerBeginDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerEndDrag(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerScroll(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerDragDrop(UDreamWidget* TargetWidget, UDreamPointerEventData* PointerEventData, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerSelect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void ExecuteEvent_OnPointerDeselect(UDreamWidget* TargetWidget, UDreamBaseEventData* EventData, bool AllowEventBubbleUp = false);

	// The player's dispatch -- the handlers, then the broadcasts. Kept here for code that dispatches through an
	// event system in hand.
	void CallOnPointerEnter(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerExit(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDown(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerUp(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerClick(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDoubleClick(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerLongPress(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerPinch(UDreamWidget* RootComponent, UDreamGestureEventData* EventData);
	void CallOnPointerSwipe(UDreamWidget* RootComponent, UDreamGestureEventData* EventData);
	void CallOnPointerBeginDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerEndDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerScroll(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDragDrop(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerSelect(UDreamWidget* RootComponent, UDreamBaseEventData* EventData);
	void CallOnPointerDeselect(UDreamWidget* RootComponent, UDreamBaseEventData* EventData);

#pragma region WorldTarget
	/** This pointer's world-target state; see FDreamUIPointerWorldTarget. Null when it has none and bCreateIfNotExist is false. */
	FDreamUIPointerWorldTarget* GetPointerWorldTarget(int InPointerID, bool bCreateIfNotExist);
	/** The actor behind the world hit this pointer is over, or null. */
	AActor* GetHoveredWorldTarget(int InPointerID)const;
	/** The actor this pointer's current press landed on, when it landed outside the widgets; null otherwise. */
	AActor* GetPressedWorldTarget(int InPointerID)const;

	/** The world-target counterparts of CallOnPointer*: the same event types, the same bubbling, the same broadcasts. */
	void CallOnWorldTargetEnter(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetExit(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetDown(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetUp(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetClick(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetDoubleClick(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetLongPress(AActor* InTarget, UDreamPointerEventData* EventData);
	void CallOnWorldTargetScroll(AActor* InTarget, UDreamPointerEventData* EventData);
#pragma endregion

	void LogEventData(UDreamBaseEventData* EventData);

private:
	/** The player this speaks for; see GetInputUser. */
	mutable TWeakObjectPtr<UDreamUIInputUser> BoundUser;
	/** Set on the event system the input subsystem hands a Blueprint when none is placed. */
	bool bIsImplicit = false;
	/** SetUserIndex's body, run once no event is being dispatched. */
	void SetUserIndexNow(int Value);

	// What the accessors answer when this speaks for no player: events that never fire, a map that stays empty.
	FDreamUIRaycastHitDelegate UnboundRaycastHitEvent;
	FDreamUIMulticastDelegateBaseEventData UnboundInputEvent;
	FDreamUIPointerInputTypeChangedDelegate UnboundPointerInputTypeChangedEvent;
	FDreamUIInputDeviceChangedDelegate UnboundInputDeviceChangedEvent;
	FDreamUIGamepadModelChangedDelegate UnboundGamepadModelChangedEvent;
};

/*
 * This is a preset actor that contains a DreamEventSystem component
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUIINPUT_API ADreamEventSystemActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamEventSystemActor();

	/** The component subclasses register their input module with. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	class UDreamEventSystem* GetEventSystem() const { return EventSystem; }
private:
	UPROPERTY(Category = "DreamGUI", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UDreamEventSystem> EventSystem;
};
