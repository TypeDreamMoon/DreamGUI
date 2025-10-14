// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "LexDelegateDeclaration.h"
#include "LGUIDelegateHandleWrapper.h"
#include "LexEventSystem.generated.h"

class ULexPointerEventData;
class ULexBaseInputModule;

DECLARE_MULTICAST_DELEGATE_TwoParams(FLexUIPointerInputTypeChangedDelegate, int, ELexUIPointerInputType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLexUIPointerInputChangedDynamicDelegate, int, PointID, ELexUIPointerInputType, InputType);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FLexUIRaycastHitDelegate, bool, const FHitResult&, USceneComponent*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLexUIRaycastHitDynamicDelegate, bool, IsHit, const FHitResult&, HitResult, USceneComponent*, HitObject);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLexUIBaseEventDataDynamicDelegate, ULexBaseEventData*, Data);

/**
 * This is the place for manage LexUI's input/raycast/event.
 * InputTrigger and InputScroll need manually setup in InputModule.
 * About event bubble: if all interface of target component and actor return true, then event will bubble up. if no interface found on target, then event will bubble up
 */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexEventSystem : public UActorComponent
{
	GENERATED_BODY()

public:
	ULexEventSystem();

	UFUNCTION(BlueprintPure, Category = LGUI, meta = (WorldContext = "WorldContextObject", DisplayName = "Get Lex Event System Instance"))
		static ULexEventSystem* GetLexEventSystemInstance(UObject* WorldContextObject);
protected:
	/** a world should only have one LexUIEventSystem */
	static TMap<UWorld*, ULexEventSystem*> WorldToInstanceMap;
	bool bExistInInstanceMap = false;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy()override;

protected:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = LGUI)
		bool bOutputLog = false;
#endif
	UPROPERTY(VisibleAnywhere, Category = LGUI)
		bool bRayEventEnable = true;

	void ProcessInputEvent();
public:
	/** clear event. eg when mouse is hovering a UI and highlight, and then event is disabled, we can use this to clear the hover event */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void ClearEvent();
	/** 
	 * SetRaycast enable or disable
	 * @param	bClearEvent		call ClearEvent after disable Raycast
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRaycastEnable(bool bEnable, bool bClearEvent = false);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSelectComponent(USceneComponent* InSelectComp, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType);
	static void SetSelectComponent(ULexEventSystem* InEventSystem, USceneComponent* InSelectComp, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSelectComponentWithDefault(USceneComponent* InSelectComp);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		USceneComponent* GetCurrentSelectedComponent(int InPointerID)const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexBaseInputModule* GetCurrentInputModule();

	UPROPERTY(VisibleAnywhere, Category = LGUI)
		mutable TMap<int, TObjectPtr<ULexPointerEventData>> PointerEventDataMap;
	/**
	 * Get PointerEventData by given pointerID.
	 * @param	PointerID	0 for mouse input, touch-id for touch input, or other customized value
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexPointerEventData* GetPointerEventData(int PointerID = 0, bool bCreateIfNotExist = false)const;
	/**
	 * Remove a PointerEventData. If you ensure that you will not use it anymore, then you can remove it.
	 * @param	PointerID	0 for mouse input, touch-id for touch input, or other customized value
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void RemovePointerEventData(int PointerID);
protected:
	/** called for pointer hit anything */
	FLexUIRaycastHitDelegate RaycastHitEvent;
	UPROPERTY(BlueprintAssignable, Category = LGUI, DisplayName="RaycastHitEvent")
	FLexUIRaycastHitDynamicDelegate RaycastHitEventBP;
	
	/** called for all pointer && navigation event */
	FLexUIMulticastDelegateBaseEventData InputEvent;
	UPROPERTY(BlueprintAssignable, Category = LGUI, DisplayName="InputEvent")
	FLexUIBaseEventDataDynamicDelegate InputEventBP;
	
	/** called when any pointerEventData's input type is changed */
	FLexUIPointerInputTypeChangedDelegate PointerInputTypedChangedEvent;
public:
	FLexUIRaycastHitDelegate& GetRaycastHitEvent(){return RaycastHitEvent;}
	FLexUIMulticastDelegateBaseEventData& GetInputEvent(){return InputEvent;}
	FLexUIPointerInputTypeChangedDelegate& GetInputChangedEvent(){return PointerInputTypedChangedEvent;}
	
	void RaiseHitEvent(bool bHitOrNot, const FHitResult& HitResult, USceneComponent* HitComponent);
	
	/**
	 * Tell if the pointer hovering on any UI object.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool IsPointerOverUIByPointerID(int PointerID = 0);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetHighlightedComponentForNavigation(USceneComponent* InComp, int InPointerID);	
	UFUNCTION(BlueprintCallable, Category = LGUI)
		USceneComponent* GetHighlightedComponentForNavigation(int InPointerID)const;
	
	/**
	 * Set input type of the pointer, can be pointer or navigation.
	 * @return true- input type changed, false- otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool SetPointerInputTypeByPointerID(int InPointerID, ELexUIPointerInputType InInputType);
	/**
	 * Set input type of the pointer, can be pointer or navigation..
	 * @return true- input type changed, false- otherwise.
	 */
	bool SetPointerInputType(ULexPointerEventData* InPointerEventData, ELexUIPointerInputType InInputType);
	/**
	 * Set the pointer's inputType as navigation.
	 * @param InPointerID target pointer's ID.
	 * @param InDefaultHighlightedComponent default highlighted component for navigation input.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void ActivateNavigationInput(int InPointerID, USceneComponent* InDefaultHighlightedComponent = nullptr);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI)
		ELexUIPointerInputType DefaultInputType = ELexUIPointerInputType::Pointer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI)
		ELexUIEventFireType EventFireTypeForNavigation = ELexUIEventFireType::TargetActorAndAllItsComponents;
	/**
	 * If keep pressing the navigate button for a while, then will trigger the process of continuous navigation.
	 * This is the interval trigger time of continuous navigation
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI)
		float NavigateInputInterval = 0.2f;
	/**
	 * If keep pressing the navigate button for a while, then will trigger the process of continuous navigation.
	 * This is the time to trigger the process.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LGUI)
		float NavigateInputIntervalForFirstTime = 0.5f;
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerEnter(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerExit(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerDown(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerUp(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerClick(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerBeginDrag(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerDrag(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerEndDrag(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerScroll(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerDragDrop(USceneComponent* TargetRootComponent, ULexPointerEventData* PointerEventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = true);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerSelect(USceneComponent* TargetRootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = false);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		static void ExecuteEvent_OnPointerDeselect(USceneComponent* TargetRootComponent, ULexBaseEventData* EventData, ELexUIEventFireType EventFireType, bool AllowEventBubbleUp = false);

	void CallOnPointerEnter(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerExit(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerDown(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerUp(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerClick(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerBeginDrag(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerDrag(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerEndDrag(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerScroll(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerDragDrop(USceneComponent* component, ULexPointerEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerSelect(USceneComponent* component, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType);
	void CallOnPointerDeselect(USceneComponent* component, ULexBaseEventData* eventData, ELexUIEventFireType eventFireType);

	static void BubbleOnPointerEnter(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerExit(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerDown(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerUp(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerClick(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerBeginDrag(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerDrag(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerEndDrag(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerScroll(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerDragDrop(AActor* actor, ULexPointerEventData* eventData);
	static void BubbleOnPointerSelect(AActor* actor, ULexBaseEventData* eventData);
	static void BubbleOnPointerDeselect(AActor* actor, ULexBaseEventData* eventData);

	void LogEventData(ULexBaseEventData* eventData);
};

/*
 * This is a preset actor that contains a LexEventSystem component
 */
UCLASS(ClassGroup = LGUI)
class LGUI_API ALGUIEventSystemActor : public AActor
{
	GENERATED_BODY()

public:
	ALGUIEventSystemActor();
protected:
	UPROPERTY(Category = "LGUI", VisibleAnywhere, BlueprintReadOnly, Transient, meta = (AllowPrivateAccess = "true"))
		TObjectPtr<class ULexEventSystem> EventSystem;
};
