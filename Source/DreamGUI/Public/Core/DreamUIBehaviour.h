// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/ActorComponent.h"
#include "DreamUIBehaviour.generated.h"

class UDreamWidget;
class UDreamWidgetAnimationComponent;
class USceneComponent;

/**
 * Base class for DreamUI's life cycle behaviour related component.
 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root widget will execute the first.
 */
UCLASS(ClassGroup = (DreamGUI), DefaultToInstanced, Abstract, Blueprintable, DisplayName = "DreamUI Behaviour")
class DREAMGUI_API UDreamUIBehaviour : public UObject
{
	GENERATED_BODY()
public:
	UDreamUIBehaviour();
	friend class UDreamWidget;
	virtual UWorld* GetWorld() const override final;
	/** INDEX_NONE means this behaviour does not impose a child-count limit on its widget. */
	virtual int32 GetMaxWidgetChildren() const { return INDEX_NONE; }

	int32 GetComponentIndexInWidget()const;
private:
	void BeginPlay();
	void EndPlay();

protected:
	virtual void OnRegister();
	virtual void OnUnregister();
	virtual void OnWidgetChildAttached(UDreamWidget* Child) {}
	virtual void OnWidgetChildDetached(UDreamWidget* Child) {}
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	
	enum class ECallbackFunctionType :int32
	{
		OnWidgetActiveChanged,
		OnTransformChanged,
		OnDimensionsChanged,
		OnChildDimensionsChanged,
		OnAttachmentChanged,
		OnSiblingIndexChanged,
		OnInteractableChanged,
		OnRaycastableChanged,
		COUNT,
	};
	/** Some UI callback functions want to execute before Awake, but most behaviours should executed inside or after Awake. So use this array to cache these callbacks and execute when Awake called. */
	TArray<TFunction<void()>> CallbacksBeforeAwake;

	uint8 bIsAwakeCalled : 1 = false;
	uint8 bIsStartCalled : 1 = false;
	uint8 bIsEnableCalled : 1 = false;
	uint8 bCanExecuteTick : 1 = true;
	/** use this to tell if the class is compiled from blueprint, only blueprint can execute ReceiveXXX. */
	uint8 bCanExecuteBlueprintEvent : 1;
	/** The Blueprint wrote a Tick event, as opposed to merely being a Blueprint. Set once in the ctor. */
	uint8 bHasBlueprintTick : 1 = false;

	UPROPERTY(EditAnywhere, Category=DreamUIBehaviour)
	uint8 bStartWithTickEnabled : 1 = true;
	UPROPERTY(EditAnywhere, Category=DreamUIBehaviour)
	uint8 bTickEvenWhenPaused : 1 = false;
private:
	friend class UDreamUIManagerWorldSubsystem;
	void Call_Awake();
	void Call_OnEnable();
	void Call_Start();
	void Call_OnDisable();
	void Call_OnDestroy();
	UPROPERTY(Transient, Getter=GetWidget, DisplayName=Widget, BlueprintReadOnly, Category=DreamUIBehaviour, meta=(AllowPrivateAccess=true))
	mutable TObjectPtr<UDreamWidget> CacheWidget = nullptr;
	UPROPERTY(Transient, Getter=GetAnimationPlayer, DisplayName="Animation Player", BlueprintReadOnly, Category="DreamUI|Animation", meta=(AllowPrivateAccess=true))
	mutable TObjectPtr<UDreamWidgetAnimationComponent> AnimationPlayer = nullptr;
protected:

	/**
	 * Awake is called when widget is created, just like BeginPlay.
	 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root widget will execute the first.
	 */
	virtual void Awake();
	/** OnEnable is called after Awake if WidgetActiveInHierarchy is true, or when WidgetActiveInHierarchy become true. */
	virtual void OnEnable();
	/** Start is called before the first Tick. */
	virtual void Start();
	/** Tick is called once per frame. */
	virtual void Tick(float DeltaTime);
	/** OnDisable is called when WidgetActiveInHierarchy become false, or before OnDestroy. */
	virtual void OnDisable();
	/** OnDestroy is called when Widget destroy */
	virtual void OnDestroy();

	virtual void OnTransformChanged();
	/** Called when RootUIComp->AnchorData is changed or scale is changed. */
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	virtual void OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	/** Called when RootUIComp attach to a new parent */
	virtual void OnAttachmentChanged();
	virtual void OnSiblingIndexChanged();
	/** Called when RootUIComp Interactable state is changed */
	virtual void OnInteractableChanged(bool Interactable);
	virtual void OnRaycastableChanged(bool Raycastable);

	void Call_OnWidgetActiveChanged(bool WidgetActive);
	void Call_OnTransformChanged();
	void Call_OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	void Call_OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	void Call_OnAttachmentChanged();
	void Call_OnSiblingIndexChanged();
	void Call_OnInteractableChanged(bool Interactable);
	void Call_OnRaycastableChanged(bool Raycastable);

	/**
	 * This function is always called before any Start functions and also after a prefab is loaded.
	 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root widget will execute the first.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Awake"), Category = "DreamUIBehaviour")void ReceiveAwake();
	/** Executed after Awake when WidgetActiveInHierarchy is true, or when WidgetActiveInHierarchy become true. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnEnable"), Category = "DreamUIBehaviour")void ReceiveOnEnable();
	/** Start is called before the first Tick. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Start"), Category = "DreamUIBehaviour")void ReceiveStart();
	/** Tick is called once per frame. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Tick"), Category = "DreamUIBehaviour")void ReceiveTick(float DeltaTime);
	/** OnDisable is called when WidgetActiveInHierarchy become false, or before OnDestroy. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnDisable"), Category = "DreamUIBehaviour")void ReceiveOnDisable();
	/** OnDestroy is called when Widget destroy */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnDestroy"), Category = "DreamUIBehaviour")void ReceiveOnDestroy();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransformChanged"), Category = "DreamUIBehaviour") void ReceiveOnTransformChanged();
	/** Called when Widget->AnchorData is changed. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnDimensionsChanged"), Category = "DreamUIBehaviour") void ReceiveOnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnChildDimensionsChanged"), Category = "DreamUIBehaviour") void ReceiveOnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	/** Called when Widget attach to a new parent */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnAttachmentChanged"), Category = "DreamUIBehaviour") void ReceiveOnAttachmentChanged();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnSiblingIndexChanged"), Category = "DreamUIBehaviour") void ReceiveOnSiblingIndexChanged();
	/** Called when Widget IsActiveInHierarchy state is changed */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnInteractableChanged"), Category = "DreamUIBehaviour") void ReceiveOnInteractableChanged(bool Interactable);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnRaycastableChanged"), Category = "DreamUIBehaviour") void ReceiveOnRaycastableChanged(bool Raycastable);
public:
	/**
	 * Set if this component can execute "Update" event or not. "CanExecuteUpdate" is true by default.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamUIBehaviour")
	void SetCanExecuteTick(bool Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamUIBehaviour")
	UDreamWidget* GetWidget() const;
	/** Finds the prefab animation host on this widget or the nearest parent widget. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	UDreamWidgetAnimationComponent* GetAnimationPlayer() const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FString GetPathDisplayName()const;
	
	UFUNCTION(BlueprintCallable, Category = "DreamUIBehaviour")
	void DestroyComponent();
};
