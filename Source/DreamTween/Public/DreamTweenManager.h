// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamTweener.h"
#include "DreamTweenManager.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMTWEEN_API UDreamTweenTickHelperComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamTweenTickHelperComponent();
	UPROPERTY() TWeakObjectPtr<class UDreamTweenManager> Target = nullptr;
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason)override;
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class DREAMTWEEN_API ADreamTweenTickHelperActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamTweenTickHelperActor();
	virtual void BeginPlay()override;
	virtual void Tick(float DeltaSeconds)override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason)override;
	UPROPERTY() TWeakObjectPtr<class UDreamTweenManager> Target = nullptr;
private:
	void OnDreamTweenManagerCreated(class UDreamTweenManager* DreamTweenManager);
	FDelegateHandle OnDreamTweenManagerCreatedDelegateHandle;

	void SetupTick(UDreamTweenManager* DreamTweenManager);
};

// This class is only for spawn ADreamTweenTickHelperActor for game world
UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMTWEEN_API UDreamTweenTickHelperWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void PostInitialize()override;
};

DECLARE_EVENT_OneParam(UDreamTweenManager, FDreamTweenManagerCreated, class UDreamTweenManager*);

UCLASS(NotBlueprintable, BlueprintType, Transient)
class DREAMTWEEN_API UDreamTweenManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:	

	//~USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~End of USubsystem interface
	
	void Tick(EDreamTweenTickType TickType, float DeltaTime);
	
	UFUNCTION(BlueprintPure, Category = DreamTween, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamTween Instance"))
	static UDreamTweenManager* GetDreamTweenInstance(UObject* WorldContextObject);
	static FDreamTweenManagerCreated OnDreamTweenManagerCreated;
private:
	/** current active tweener collection*/
	UPROPERTY(VisibleAnywhere, Category=DreamTween)TArray<TObjectPtr<UDreamTweener>> tweenerList;
	void OnTick(EDreamTweenTickType TickType, float DeltaTime, float UnscaledDeltaTime);
	FDreamTweenUpdateMulticastDelegate updateEvent;
	bool bTickPaused = false;
public:
	UE_DEPRECATED(5.1, "Use Tweener->SetTickType(EDreamTweenTickType::Manual) then call this->ManualTick.")
	/**
	 * Disable default Tick function, so you can pause all tween or use CustomTick to do your own tick and use your own DeltaTime.
	 * This will only pause the tick with current DreamTweenManager instance, so after load a new level, default Tick will work again, and you need to call DisableTick again if you want to disable tick.
	 */ 
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (DeprecatedFunction, DeprecationMessage = "Use Tweener->SetTickType(EDreamTweenTickType::Manual) then call this->ManualTick."))
	void DisableTick();
	UE_DEPRECATED(5.1, "Use Tweener->SetTickType(EDreamTweenTickType::Manual) then call this->ManualTick.")
	/**
	 * Enable default Tick if it is disabled.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamTween, meta = (DeprecatedFunction, DeprecationMessage = "Use Tweener->SetTickType(EDreamTweenTickType::Manual) then call this->ManualTick."))
	void EnableTick();


	UFUNCTION(BlueprintCallable, Category = DreamTween)
	void ManualTick(float DeltaTime);

	/**
	 * Kill all tweens
	 */
	UFUNCTION(BlueprintCallable, Category = DreamTween)
	void KillAllTweens(bool callComplete = false);

	/**
	 * Kill all tween animations created on TargetObject
	 * @param TargetObject The object which contains the tweener
	 * @param callComplete true- execute onComplete event.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamTween)
	static void KillAllTweensOnTarget(UObject* WorldContextObject, UObject* TargetObject, bool callComplete = false);

	/**
	 * Is the tweener is currently tweening? 
	 * @param item tweener item
	 */
	static bool IsTweening(UObject* WorldContextObject, UDreamTweener* item);
	bool IsTweening(UDreamTweener* item);
	/**
	 * Kill the tweener if it is tweening.
	 * @param item tweener item
	 * @param callComplete true-execute onComplete event.
	 */
	static void KillIfIsTweening(UObject* WorldContextObject, UDreamTweener* item, bool callComplete);
	void KillIfIsTweening(UDreamTweener* item, bool callComplete);
	/**
	 * Remove tweener from list, so the tweener will not be managed by this DreamTweenManager.
	 * @param item tweener item
	 */
	static void RemoveTweener(UObject* WorldContextObject, UDreamTweener* item);
	void RemoveTweener(UDreamTweener* item);

	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenFloatGetterFunction& getter, const FDreamTweenFloatSetterFunction& setter, float endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenDoubleGetterFunction& getter, const FDreamTweenDoubleSetterFunction& setter, double endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenIntGetterFunction& getter, const FDreamTweenIntSetterFunction& setter, int endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenPositionGetterFunction& getter, const FDreamTweenPositionSetterFunction& setter, const FVector& endValue, float duration, bool sweep = false, FHitResult* sweepHitResult = nullptr, ETeleportType teleportType = ETeleportType::None);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenVectorGetterFunction& getter, const FDreamTweenVectorSetterFunction& setter, const FVector& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenColorGetterFunction& getter, const FDreamTweenColorSetterFunction& setter, const FColor& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenLinearColorGetterFunction& getter, const FDreamTweenLinearColorSetterFunction& setter, const FLinearColor& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenVector2DGetterFunction& getter, const FDreamTweenVector2DSetterFunction& setter, const FVector2D& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenVector4GetterFunction& getter, const FDreamTweenVector4SetterFunction& setter, const FVector4& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenQuaternionGetterFunction& getter, const FDreamTweenQuaternionSetterFunction& setter, const FQuat& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenRotatorGetterFunction& getter, const FDreamTweenRotatorSetterFunction& setter, const FRotator& endValue, float duration);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenRotationQuatGetterFunction& getter, const FDreamTweenRotationQuatSetterFunction& setter, const FVector& eulerAngle, float duration, bool sweep = false, FHitResult* sweepHitResult = nullptr, ETeleportType teleportType = ETeleportType::None);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenRotationQuatGetterFunction& getter, const FDreamTweenRotationQuatSetterFunction& setter, const FQuat& endValue, float duration, bool sweep = false, FHitResult* sweepHitResult = nullptr, ETeleportType teleportType = ETeleportType::None);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenMaterialScalarGetterFunction& getter, const FDreamTweenMaterialScalarSetterFunction& setter, float endValue, float duration, int32 parameterIndex);
	static UDreamTweener* To(UObject* WorldContextObject, const FDreamTweenMaterialVectorGetterFunction& getter, const FDreamTweenMaterialVectorSetterFunction& setter, const FLinearColor& endValue, float duration, int32 parameterIndex);

	static UDreamTweener* VirtualTo(UObject* WorldContextObject, float duration);
	static UDreamTweener* DelayFrameCall(UObject* WorldContextObject, int delayFrame);
	static UDreamTweener* UpdateCall(UObject* WorldContextObject);

	static class UDreamTweenerSequence* CreateSequence(UObject* WorldContextObject);
};
