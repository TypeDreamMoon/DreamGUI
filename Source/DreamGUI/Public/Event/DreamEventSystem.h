// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "DreamDelegateDeclaration.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamEventSystem.generated.h"

class UDreamPointerEventData;
class UDreamBaseInputModule;

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDynamicDelegate, EDreamUIInputDevice, Device);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDelegate, EDreamUIInputDevice);

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

protected:

	UPROPERTY(EditAnywhere, Category = DreamGUI, Getter)
	int UserIndex = 0;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		bool bOutputLog = false;
#endif
	UPROPERTY(VisibleAnywhere, Category = DreamGUI)
		bool bRayEventEnable = true;

	void ProcessInputEvent();

	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	TWeakObjectPtr<UDreamBaseInputModule> CurrentInputModule = nullptr;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetUserIndex()const{return UserIndex;}

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
	bool GetScrollNavigationTargetIntoView()const{return bScrollNavigationTargetIntoView;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetAnimateNavigationScroll()const{return bAnimateNavigationScroll;}

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
	void CallOnPointerBeginDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerEndDrag(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerScroll(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerDragDrop(UDreamWidget* RootComponent, UDreamPointerEventData* EventData);
	void CallOnPointerSelect(UDreamWidget* RootComponent, UDreamBaseEventData* EventData);
	void CallOnPointerDeselect(UDreamWidget* RootComponent, UDreamBaseEventData* EventData);
	
	void LogEventData(UDreamBaseEventData* EventData);
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
