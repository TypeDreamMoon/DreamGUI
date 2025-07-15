// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/LexWidget.h"
#include "Components/ActorComponent.h"
#include "LGUILifeCycleBehaviour.generated.h"

class USceneComponent;

/**
 * Base class for LGUI's life cycle behviour related component.
 * I'm trying to make this ULGUILifeCycleBehaviour more like Unity's MonoBehaviour. You will see it contains function like Awake/Start/Update/OnDestroy/OnEnable/OnDisable.
 * So you should use Awake/Start instead of BeginPlay, Update instead of Tick, OnDestroy instead of EndPlay.
 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root actor will execute the first.
 */
UCLASS(ClassGroup = (LGUI), Abstract, Blueprintable, HideCategories = (Activation), DisplayName = "LGUI LifeCycle Behaviour")
class LGUI_API ULGUILifeCycleBehaviour : public UActorComponent
{
	GENERATED_BODY()
public:
	ULGUILifeCycleBehaviour();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason)override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	uint8 bIsAwakeCalled : 1 = false;
	uint8 bIsStartCalled : 1 = false;
	uint8 bCanExecuteUpdate : 1 = true;
	/** use this to tell if the class is compiled from blueprint, only blueprint can execute ReceiveXXX. */
	uint8 bCanExecuteBlueprintEvent : 1;
protected:
	friend class ULGUIManagerWorldSubsystem;
	UPROPERTY(Transient) mutable TWeakObjectPtr<USceneComponent> RootComp = nullptr;
	/**
	 * This function is always called before any Start functions and also after a prefab is instantiated.
	 * This is a good replacement for BeginPlay in LGUI's Prefab workflow. Because Awake will execute after all prefab serialization and object reference is done.
	 * NOTE!!! If RootComponent is UIItem: if UIItem is not "ActiveInHierarchy" during start up, then Awake is not called until "ActiveInHierarchy" becomes true.
	 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root actor will execute the first.
	 */
	virtual void Awake();
	/** Start is called before the first frame update only if GetIsActiveAndEnable is true. */
	virtual void Start();
	/** Update is called once per frame if GetIsActiveAndEnable is true. */
	virtual void Update(float DeltaTime);
	
	virtual void Call_Awake();
	void Call_Start();

	/**
	 * This function is always called before any Start functions and also after a prefab is instantiated.
	 * This is a good replacement for BeginPlay in LGUI's Prefab workflow. Because Awake will execute after all prefab serialization and object reference is done.
	 * NOTE!!! If RootComponent is UIItem: if UIItem is inactive during start up, then Awake is not called until it is made active.
	 * Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root actor will execute the first.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Awake"), Category = "LGUILifeCycleBehaviour")void ReceiveAwake();
	/** Start is called before the first frame update only if GetIsActiveAndEnable is true. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Start"), Category = "LGUILifeCycleBehaviour")void ReceiveStart();
	/** Update is called once per frame if GetIsActiveAndEnable is true. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Update"), Category = "LGUILifeCycleBehaviour")void ReceiveUpdate(float DeltaTime);

public:
	/**
	 * Set if this component can execute "Update" event or not. "CanExecuteUpdate" is true by default.
	 * NOTE!!! This will not immediately affect "Update" event, "Update" event's state will only change after "Awake" "Start" "OnEnable" "OnDisable".
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUILifeCycleBehaviour")
		void SetCanExecuteUpdate(bool value);

	UFUNCTION(BlueprintCallable, Category = "LGUILifeCycleBehaviour")
		USceneComponent* GetRootSceneComponent() const;
};