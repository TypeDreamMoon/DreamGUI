// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "DreamDelegateDeclaration.h"
#include "GenericPlatform/ICursor.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamEventSystem.generated.h"

class UDreamPointerEventData;
class UDreamGestureEventData;
class UDreamBaseInputModule;
class UDreamUIManagerWorldSubsystem;
class APlayerController;
class ULocalPlayer;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIPointerInputTypeChangedDelegate, int, EDreamUIPointerInputType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamUIPointerInputChangedDynamicDelegate, int, PointID, EDreamUIPointerInputType, InputType);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDreamUIRaycastHitDelegate, bool, const FDreamUIHitResult&, UDreamWidget*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamUIRaycastHitDynamicDelegate, bool, IsHit, const FDreamUIHitResult&, HitResult, UDreamWidget*, HitObject);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIBaseEventDataDynamicDelegate, UDreamBaseEventData*, Data);

/**
 * What the player last touched. Not what the game is configured for -- what their hands are on right
 * now, which is the only thing a key prompt can honestly be drawn from.
 */
UENUM(BlueprintType)
enum class EDreamUIInputDevice : uint8
{
	MouseAndKeyboard,
	Gamepad,
	Touch,
};

/**
 * Which pad the player is holding, for prompts that need to say A or Cross.
 *
 * Deliberately separate from EDreamUIInputDevice rather than more entries in it: the device answers
 * "which prompt table", the model answers "which glyph within it", and folding the two together would
 * renumber an enum that is already saved in project assets. Generic is the honest answer whenever the
 * platform does not name the hardware, and every icon lookup falls back to it.
 */
UENUM(BlueprintType)
enum class EDreamUIGamepadModel : uint8
{
	/** A pad the platform did not name, or no pad at all. Xbox-style glyphs are the usual fallback. */
	Generic,
	Xbox,
	PlayStation,
	Switch,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDynamicDelegate, EDreamUIInputDevice, Device);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDelegate, EDreamUIInputDevice);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIGamepadModelChangedDynamicDelegate, EDreamUIGamepadModel, Model);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIGamepadModelChangedDelegate, EDreamUIGamepadModel);

/**
 * This is the place for manage DreamUI's input/raycast/event.
 * InputTrigger and InputScroll need manually setup in InputModule.
 * About event bubble: if all interface of target component return true, then event will bubble up. if no interface found on target, then event will bubble up
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), HideCategories = (Sockets, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation))
class DREAMGUI_API UDreamEventSystem : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamEventSystem();

	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Event System Instance"))
		static UDreamEventSystem* GetDreamEventSystemInstance(UObject* WorldContextObject, int UserIndex);
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy()override;

	/** Drop this event system's registration from the UI manager. Idempotent; called from both ends. */
	void UnregisterFromManager();
	/**
	 * The manager this registered with. Remembered rather than looked up again on the way out, because
	 * GetWorld() is routinely null by BeginDestroy and the lookup would silently find nothing.
	 */
	TWeakObjectPtr<UDreamUIManagerWorldSubsystem> RegisteredManager;

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
	 * On by default, because a cursor that changes over a button is how a player learns what is
	 * clickable. Off for a project that owns the cursor itself -- an RTS with a build cursor, a game
	 * with a custom software cursor -- so the two do not write the same field on alternate frames.
	 */
	UPROPERTY(EditAnywhere, Getter = "GetApplyHoverCursor", Setter = "SetApplyHoverCursor", Category = DreamGUI)
		bool bApplyHoverCursor = true;
	/** What the controller's cursor was before a widget first claimed it, so it can be put back. */
	TEnumAsByte<EMouseCursor::Type> CursorBeforeHoverOverride = EMouseCursor::Default;
	bool bHoverCursorOverrideActive = false;

	void ProcessInputEvent();

	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	TWeakObjectPtr<UDreamBaseInputModule> CurrentInputModule = nullptr;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetUserIndex()const{return UserIndex;}

	/**
	 * Point this event system at another player. Re-keys its registration with the UI manager, because
	 * that map is keyed by user index -- changing the field without moving the entry would leave the
	 * event system findable under the old player and invisible under the new one.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetUserIndex(int Value);

	/**
	 * The player controller this event system speaks for -- the local player at UserIndex, not "whoever
	 * is first". Null when that local player does not exist, except for index 0, which falls back to the
	 * first controller so a single-player game keeps working before any local player is registered.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	APlayerController* GetPlayerController()const;

	/**
	 * The player controller for a user index, without needing an event system in hand.
	 *
	 * This is the one place the "which player is this?" question is answered, so that a raycaster
	 * source, a tooltip or a rumble call cannot quietly disagree with the pointer they were handed.
	 * Every UDreamPointerEventData carries the index of the event system that made it.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (WorldContext = "WorldContextObject"))
	static APlayerController* GetPlayerControllerForUser(const UObject* WorldContextObject, int InUserIndex);
	/** The local player for a user index. Same rule as GetPlayerControllerForUser. */
	static ULocalPlayer* GetLocalPlayerForUser(const UObject* WorldContextObject, int InUserIndex);

	/**
	 * Push the cursor a hovered widget asked for onto this player's controller, or put back whatever
	 * the project had when nothing asks for one.
	 *
	 * Restoring rather than writing EMouseCursor::Default is what keeps this from fighting the project:
	 * a game that sets its own cursor for aiming, building or anything else outside the UI had it
	 * erased on the next frame the pointer was over nothing. Turn bApplyHoverCursor off to leave the
	 * cursor entirely alone.
	 * @param bWidgetClaimedCursor	False when no widget in the hover stack claimed a cursor.
	 */
	void ApplyHoverCursorToPlayer(bool bWidgetClaimedCursor, EMouseCursor::Type InCursor);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetApplyHoverCursor()const{ return bApplyHoverCursor; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetApplyHoverCursor(bool Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamBaseInputModule* GetCurrentInputModule()const{return CurrentInputModule.Get();}
	void SetInputModule(UDreamBaseInputModule* InputModule);
	void ClearInputModule();
	
	/** clear event. eg when mouse is hovering a UI and highlight, and then event is disabled, we can use this to clear the hover event */
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
	static void SetSelectWidget(UDreamEventSystem* InEventSystem, UDreamWidget* InSelectWidget, UDreamBaseEventData* EventData);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSelectComponentWithDefault(UDreamWidget* InSelectWidget);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidget* GetCurrentSelectedComponent(int InPointerID)const;
	
	/**
	 * Get PointerEventData by given pointerID.
	 * @param	PointerID	0 for mouse input, touch-id for touch input, or other customized value
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamPointerEventData* GetPointerEventData(int PointerID = 0, bool bCreateIfNotExist = false)const;
	/**
	 * Remove a PointerEventData. If you ensure that you will not use it anymore, then you can remove it.
	 * @param	PointerID	0 for mouse input, touch-id for touch input, or other customized value
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void RemovePointerEventData(int PointerID);
protected:
	UPROPERTY(VisibleAnywhere, Category = DreamGUI)
	mutable TMap<int, TObjectPtr<UDreamPointerEventData>> PointerEventDataMap;
	
	/** called for pointer hit anything */
	FDreamUIRaycastHitDelegate RaycastHitEvent;
	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName="RaycastHitEvent")
	FDreamUIRaycastHitDynamicDelegate RaycastHitEventBP;
	
	/** called for all pointer && navigation event */
	FDreamUIMulticastDelegateBaseEventData InputEvent;
	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName="InputEvent")
	FDreamUIBaseEventDataDynamicDelegate InputEventBP;
	
	/** called when any pointerEventData's input type is changed */
	FDreamUIPointerInputTypeChangedDelegate PointerInputTypedChangedEvent;

	/**
	 * Mouse and keyboard until told otherwise: it is the device present on every platform, and being
	 * wrong about it costs a stale prompt for one keypress rather than a wrong one forever.
	 */
	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	EDreamUIInputDevice CurrentInputDevice = EDreamUIInputDevice::MouseAndKeyboard;
	FDreamUIInputDeviceChangedDelegate InputDeviceChangedEvent;

	/** Which pad, once one has been used. Generic until then, and whenever the platform does not say. */
	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	EDreamUIGamepadModel CurrentGamepadModel = EDreamUIGamepadModel::Generic;
	/** While set, detection is not consulted at all -- see SetGamepadModelOverride. */
	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	bool bGamepadModelOverridden = false;
	FDreamUIGamepadModelChangedDelegate GamepadModelChangedEvent;
public:
	const TMap<int, TObjectPtr<UDreamPointerEventData>>& GetPointerEventDataMap()const{return PointerEventDataMap;}
	
	FDreamUIRaycastHitDelegate& GetRaycastHitEvent(){return RaycastHitEvent;}
	FDreamUIMulticastDelegateBaseEventData& GetInputEvent(){return InputEvent;}
	FDreamUIPointerInputTypeChangedDelegate& GetInputChangedEvent(){return PointerInputTypedChangedEvent;}
	
	void RaiseHitEvent(bool bHitOrNot, const FDreamUIHitResult& HitResult, UDreamWidget* HitComponent);
	
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
	 * How long after a click a second press on the same widget still counts as a double click -- and a
	 * double click is delivered at that press, in place of its down, as Slate delivers one. The press
	 * also has to land within the pointer's drag threshold of the first (see
	 * UDreamBaseRaycaster::IsWithinDoubleClickDistance); there is no separate distance to set. The
	 * platform default is around a third of a second and this matches it; zero disables double clicks
	 * entirely, which is a legitimate thing for a project to want, and makes every press a down.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DoubleClickTime = 0.3f;
	/**
	 * How long the trigger must be held on one widget before a long press is dispatched. Zero turns
	 * long press off. Half a second is the platform convention for press-and-hold on touch.
	 *
	 * A press that has already become a drag never produces one -- see IDreamPointerLongPressInterface.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LongPressTime = 0.5f;
	/**
	 * How far a touch has to travel, in viewport pixels, before its release counts as a swipe, and how
	 * long it may take. A slow drag across the screen is a drag, not a swipe, which is why there is a
	 * time limit at all.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SwipeMinDistance = 80.0f;
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SwipeMaxDuration = 0.5f;
	/**
	 * How far the two fingers of a pinch must move apart or together, in viewport pixels, before the
	 * gesture is reported at all. Below it, two fingers resting on the glass would emit a stream of
	 * noise events.
	 */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = DreamGUI, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PinchMinDistanceChange = 12.0f;
	/**
	 * Scroll the containers around a navigated-to widget until it is on screen. Off, navigation can
	 * only reach what is already visible, which turns any list longer than its viewport into a wall.
	 */
	UPROPERTY(EditAnywhere, Getter = "GetScrollNavigationTargetIntoView", Setter = "SetScrollNavigationTargetIntoView", Category = DreamGUI)
	bool bScrollNavigationTargetIntoView = true;
	/** Ease that reveal scroll instead of jumping to it. */
	UPROPERTY(EditAnywhere, Getter = "GetAnimateNavigationScroll", Setter = "SetAnimateNavigationScroll", Category = DreamGUI, meta = (EditCondition = "bScrollNavigationTargetIntoView"))
	bool bAnimateNavigationScroll = true;

public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIPointerInputType GetDefaultInputType()const{return DefaultInputType;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetNavigateInputIntervalForFirstTime()const{return NavigateInputIntervalForFirstTime;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetNavigateInputInterval()const{return NavigateInputInterval;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetDoubleClickTime()const{return DoubleClickTime;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetLongPressTime()const{return LongPressTime;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetSwipeMinDistance()const{return SwipeMinDistance;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetSwipeMaxDuration()const{return SwipeMaxDuration;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetPinchMinDistanceChange()const{return PinchMinDistanceChange;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetScrollNavigationTargetIntoView()const{return bScrollNavigationTargetIntoView;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetAnimateNavigationScroll()const{return bAnimateNavigationScroll;}

	/**
	 * The clock every timed pointer gesture is measured on: the PressTime, ReleaseTime and ClickTime
	 * stamps on UDreamPointerEventData, and everything measured against them -- long press, the
	 * double-click window, swipe duration, hold-to-drag -- plus navigation repeat.
	 *
	 * The world's REAL time: not stopped by a pause, not stretched by time dilation. The UI keeps
	 * working while the game is paused -- this component ticks when paused, and a screen-space
	 * raycaster ignores the pause by default -- so a pause menu is exactly where a held press still has
	 * to become a long press and two clicks a second apart still have to be two clicks. Slate times its
	 * own double clicks and key repeat in real time for the same reason. These were timed on
	 * GetTimeSeconds, which stands still while the game is paused: every held press in a pause menu
	 * waited forever, and every pair of clicks on one widget was a gap of zero, a double click however
	 * slowly it was made.
	 *
	 * One function so that the code writing a stamp and the code measuring it cannot disagree about
	 * which clock it is: compare a stamp on the event data against this, never against the game clock.
	 * @return	Seconds on that clock, or 0 when the object is in no world. Zero is a defined value, not a
	 *			measurement; a caller measuring a duration should not measure without a world at all.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (WorldContext = "WorldContextObject"))
	static double GetPointerClockSeconds(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDefaultInputType(EDreamUIPointerInputType Value){ DefaultInputType = Value;}
	/**
	 * Both navigation intervals are floored rather than taken as given: they are the step of the
	 * continuous-navigation timer, and zero (or a negative) there is not "as fast as possible", it is
	 * a deadline that can never be pushed past the current time.
	 */
	static constexpr float MinNavigateInputInterval = 0.01f;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetNavigateInputIntervalForFirstTime(float Value){ NavigateInputIntervalForFirstTime = FMath::Max(Value, MinNavigateInputInterval);}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetNavigateInputInterval(float Value){ NavigateInputInterval = FMath::Max(Value, MinNavigateInputInterval);}
	/** Floored at zero, which means "no double clicks" rather than "every click is one". */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDoubleClickTime(float Value){ DoubleClickTime = FMath::Max(Value, 0.0f);}
	/** Floored at zero, which means "no long press" rather than "every press is one". */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetLongPressTime(float Value){ LongPressTime = FMath::Max(Value, 0.0f);}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetSwipeMinDistance(float Value){ SwipeMinDistance = FMath::Max(Value, 0.0f);}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetSwipeMaxDuration(float Value){ SwipeMaxDuration = FMath::Max(Value, 0.0f);}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetPinchMinDistanceChange(float Value){ PinchMinDistanceChange = FMath::Max(Value, 0.0f);}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetScrollNavigationTargetIntoView(bool Value){ bScrollNavigationTargetIntoView = Value;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetAnimateNavigationScroll(bool Value){ bAnimateNavigationScroll = Value;}

#pragma region InputDevice
	/** What the player last used. Key prompts and cursor visibility both hang off this. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIInputDevice GetCurrentInputDevice()const{ return CurrentInputDevice; }
	/**
	 * Tell the event system a device was just used. Called for every key the input actor sees, so it
	 * must stay cheap and must only broadcast on an actual change -- a prompt bar rebuilding itself
	 * once per mouse-move frame is exactly the failure this guards against.
	 * @return true when the device changed.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool ReportInputDevice(EDreamUIInputDevice InDevice);
	/** Classify a key. Gamepad and touch keys announce themselves; everything else is a keyboard. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	static EDreamUIInputDevice GetInputDeviceForKey(const FKey& InKey);

	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName = "InputDeviceChangedEvent")
	FDreamUIInputDeviceChangedDynamicDelegate InputDeviceChangedEventBP;
	FDreamUIInputDeviceChangedDelegate& GetInputDeviceChangedEvent(){ return InputDeviceChangedEvent; }

	/**
	 * Which pad the player is holding. Generic until one is used and named by the platform.
	 *
	 * Re-detected when the device becomes Gamepad rather than polled: the answer only changes when the
	 * player picks up a different controller, which is exactly when a device change is reported.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamUIGamepadModel GetCurrentGamepadModel()const{ return CurrentGamepadModel; }
	/**
	 * Force the model, for a project that knows better than the platform does -- a console build, or a
	 * game with a "controller type" option in its settings. Generic-with-override is still an override:
	 * pass bInOverride false to go back to detection.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetGamepadModelOverride(bool bInOverride, EDreamUIGamepadModel InModel = EDreamUIGamepadModel::Generic);
	/** Ask the platform again. Called automatically when the input device becomes Gamepad. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool RefreshGamepadModel();
	/**
	 * The model a platform device name implies, as a pure function so it can be tested without hardware.
	 *
	 * Both halves of the descriptor are consulted because platforms disagree about which one carries the
	 * brand: the interface name is "XInputInterface" on Windows while the hardware identifier is the
	 * specific pad, and on other platforms it is the other way round.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	static EDreamUIGamepadModel GetGamepadModelForDeviceName(FName InInputDeviceName, FName InHardwareDeviceIdentifier);

	UPROPERTY(BlueprintAssignable, Category = DreamGUI, DisplayName = "GamepadModelChangedEvent")
	FDreamUIGamepadModelChangedDynamicDelegate GamepadModelChangedEventBP;
	FDreamUIGamepadModelChangedDelegate& GetGamepadModelChangedEvent(){ return GamepadModelChangedEvent; }
#pragma endregion
public:
	template<class UEventData, class UInterfaceFunction>
	static void ExecuteDreamUIInterface(UDreamWidget* Widget,
		UEventData* EventData,
		UClass* InterfaceClass, UInterfaceFunction InterfaceFunction,
		bool AllowEventBubbleUp)
	{
		bool TempAllowEventBubbleUp = AllowEventBubbleUp;
		// A copy, not the live array: InterfaceFunction runs game code, and a handler that adds or
		// removes a behaviour on the widget it was just dispatched to -- a click that swaps a
		// component, a widget that destroys itself -- reallocates the very array being walked.
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
		if (TempAllowEventBubbleUp)
		{
			if (auto ParentWidget = Widget->GetParent())
			{
				ExecuteDreamUIInterface(ParentWidget,
					EventData,
					InterfaceClass, InterfaceFunction, true);
			}
		}
	}
	template<class UEventData, class UInterfaceFunction, class UBubbleUpFunction>
	static void BubbleDreamUIInterface(UDreamWidget* Widget,
		UEventData* EventData, UClass* InterfaceClass,
		UInterfaceFunction InterfaceFunction, UBubbleUpFunction BubbleUpFunction)
	{
		bool TempAllowEventBubbleUp = true;
		// Same reason as ExecuteDreamUIInterface above: InterfaceFunction dispatches into game code
		// that is free to change this widget's component list while we are walking it.
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
		if (TempAllowEventBubbleUp)
		{
			if (auto ParentActor = Widget->GetParent())
			{
				BubbleUpFunction(ParentActor, EventData);
			}
		}
	}
	
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
	/**
	 * What one pointer is doing to the world beyond the widgets.
	 *
	 * A world raycaster's hit on a primitive carries no widget (UDreamBaseRaycaster::RaycastWorld), so
	 * none of UDreamPointerEventData's widget fields can hold it. The actor behind that hit is still
	 * something the pointer is over, presses and clicks, and it is told so through the same pointer
	 * interfaces a widget's behaviours implement: on the actor itself and on each of its components
	 * that implements them -- a render-target surface's UDreamUIRenderTargetInteraction is one -- and
	 * bubbling to the actor it is attached to when every handler allows it, as a widget's event
	 * bubbles to its parent. It is what LGUI did with a hit component's actor.
	 *
	 * Hover, press, release, click, double click, long press and scroll follow the widget's rules and
	 * the widget's moments in UDreamPointerInputModule::ProcessPointerEvent: every Enter is matched by
	 * one Exit, exits go out before enters whichever kind of target is being left, a press is released
	 * to the actor it landed on wherever the pointer is by then, and the second press of a quick pair is
	 * the double click in place of its down. Drags, drops, swipes and pinches stay widgets' only: each
	 * is about what lies under the pointer on a canvas, and a render-target surface -- the reason this
	 * exists -- hands the pointer on to its own canvas, where they happen.
	 *
	 * Kept per pointer id beside PointerEventDataMap, and dropped with it.
	 */
	struct FDreamPointerWorldTarget
	{
		/** The actor behind the world hit the pointer is over. */
		TWeakObjectPtr<AActor> Hovered;
		/** The actor the pointer's current press landed on. */
		TWeakObjectPtr<AActor> Pressed;
		/**
		 * The actor the pointer's last click landed on, and that click's ClickTime. A second press
		 * continues the click run only when the pointer's latest click is still this one -- a widget
		 * clicked in between took the run over, even if that widget has since been destroyed.
		 */
		TWeakObjectPtr<AActor> LastClicked;
		double LastClickedTime = 0.0;
	};
	/**
	 * This pointer's world-target state: null when it has none and bCreateIfNotExist is false. The
	 * result points into map storage, so it is not to be held across anything that dispatches -- a
	 * handler is game code and can make another pointer's state, which may move this one.
	 */
	FDreamPointerWorldTarget* GetPointerWorldTarget(int InPointerID, bool bCreateIfNotExist);
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
	/** See FDreamPointerWorldTarget. Not reflected: it holds nothing but weak pointers and a time. */
	TMap<int, FDreamPointerWorldTarget> PointerWorldTargetMap;
};

/*
 * This is a preset actor that contains a DreamEventSystem component
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamEventSystemActor : public AActor
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
