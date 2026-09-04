// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamTweenManager.h"
#include "DreamTweenerSpring.h"
#include "Tweener/DreamTweenerFloat.h"
#include "Tweener/DreamTweenerDouble.h"
#include "Tweener/DreamTweenerInteger.h"
#include "Tweener/DreamTweenerVector.h"
#include "Tweener/DreamTweenerColor.h"
#include "Tweener/DreamTweenerLinearColor.h"
#include "Tweener/DreamTweenerVector2D.h"
#include "Tweener/DreamTweenerVector4.h"
#include "Tweener/DreamTweenerPosition.h"
#include "Tweener/DreamTweenerQuaternion.h"
#include "Tweener/DreamTweenerRotator.h"
#include "Tweener/DreamTweenerRotationEuler.h"
#include "Tweener/DreamTweenerRotationQuat.h"
#include "Tweener/DreamTweenerMaterialScalar.h"
#include "Tweener/DreamTweenerMaterialVector.h"

#include "Tweener/DreamTweenerFrame.h"
#include "Tweener/DreamTweenerVirtual.h"
#include "Tweener/DreamTweenerUpdate.h"

#include "DreamTweenerSequence.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

UDreamTweenTickHelperComponent::UDreamTweenTickHelperComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_DuringPhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}
void UDreamTweenTickHelperComponent::BeginPlay()
{
	Super::BeginPlay();
}
void UDreamTweenTickHelperComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Target.IsValid())
	{
		Target->Tick((EDreamTweenTickType)((uint8)PrimaryComponentTick.TickGroup), DeltaTime);
	}
}
void UDreamTweenTickHelperComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

ADreamTweenTickHelperActor::ADreamTweenTickHelperActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_DuringPhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
}
void ADreamTweenTickHelperActor::BeginPlay()
{
	Super::BeginPlay();
	if (auto DreamTweenManager = UDreamTweenManager::GetDreamTweenInstance(this))
	{
		SetupTick(DreamTweenManager);
	}
	else
	{
		//If GameInstance subsystem not created yet, then register a event to wait it create
		OnDreamTweenManagerCreatedDelegateHandle = UDreamTweenManager::OnDreamTweenManagerCreated.AddUObject(this, &ADreamTweenTickHelperActor::OnDreamTweenManagerCreated);
	}
}
void ADreamTweenTickHelperActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Target.IsValid())
	{
		Target->Tick(EDreamTweenTickType::DuringPhysics, DeltaSeconds);
	}
}
void ADreamTweenTickHelperActor::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (OnDreamTweenManagerCreatedDelegateHandle.IsValid())
	{
		UDreamTweenManager::OnDreamTweenManagerCreated.Remove(OnDreamTweenManagerCreatedDelegateHandle);
	}
}
void ADreamTweenTickHelperActor::OnDreamTweenManagerCreated(UDreamTweenManager* DreamTweenManager)
{
	SetupTick(DreamTweenManager);
}
void ADreamTweenTickHelperActor::SetupTick(UDreamTweenManager* DreamTweenManager)
{
	auto CreateComp = [DreamTweenManager, this](ETickingGroup TickingGroup, FName Name) {
		auto TickComp_DuringPhysics = NewObject<UDreamTweenTickHelperComponent>(this, Name);
		TickComp_DuringPhysics->SetTickGroup(TickingGroup);
		TickComp_DuringPhysics->RegisterComponent();
		TickComp_DuringPhysics->Target = DreamTweenManager;
		this->AddInstanceComponent(TickComp_DuringPhysics);
		};
	CreateComp(ETickingGroup::TG_PrePhysics, TEXT("PrePhysics"));
	CreateComp(ETickingGroup::TG_PostPhysics, TEXT("PostPhysics"));
	CreateComp(ETickingGroup::TG_PostUpdateWork, TEXT("PostUpdateWork"));
	this->Target = DreamTweenManager;
}



bool UDreamTweenTickHelperWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const 
{
	if (auto World = Outer->GetWorld())
	{
		if (World->IsGameWorld())
		{
			return true;
		}
	}
	return false;
}
void UDreamTweenTickHelperWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();
	if (auto World = GetWorld())
	{
		if (World->IsGameWorld())
		{
			World->SpawnActor<ADreamTweenTickHelperActor>();
		}
	}
}


DECLARE_CYCLE_STAT(TEXT("DreamTween Update"), STAT_Update, STATGROUP_DreamTween);

FDreamTweenManagerCreated UDreamTweenManager::OnDreamTweenManagerCreated;
//~USubsystem interface
void UDreamTweenManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UGameInstance* LocalGameInstance = GetGameInstance();
	check(LocalGameInstance);
}

void UDreamTweenManager::Deinitialize()
{
	Super::Deinitialize();
	tweenerList.Empty();
}

bool UDreamTweenManager::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}
//~End of USubsystem interface

void UDreamTweenManager::Tick(EDreamTweenTickType TickType, float DeltaTime)
{
	if (bTickPaused)return;
	if (TickType == EDreamTweenTickType::Manual)
	{
		OnTick(TickType, DeltaTime, DeltaTime);
	}
	else
	{
		if (auto World = GetWorld())
		{
			OnTick(TickType, World->DeltaTimeSeconds, World->DeltaRealTimeSeconds);
		}
		else
		{
			OnTick(TickType, DeltaTime, DeltaTime);
		}
	}
}

#include "Kismet/GameplayStatics.h"
UDreamTweenManager* UDreamTweenManager::GetDreamTweenInstance(UObject* WorldContextObject)
{
	if (auto GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject))
		return GameInstance->GetSubsystem<UDreamTweenManager>();
	else
		return nullptr;
}

void UDreamTweenManager::OnTick(EDreamTweenTickType TickType, float DeltaTime, float UnscaledDeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_Update);
	
	// A tween's own callbacks run inside ToNext, and they are free to start a tween, kill every tween, or
	// remove this one -- so the list can be grown, emptied or reordered underneath the walk. Walking it by
	// index meant the removal below used an index that no longer named the tween that had just finished:
	// it deleted whatever had shifted into that slot, or ran off the end of a list a callback had emptied.
	// So the walk is over a snapshot, and the finished ones come out afterwards by identity.
	TArray<TObjectPtr<UDreamTweener>> tweenersToTick = tweenerList;
	TArray<TObjectPtr<UDreamTweener>> tweenersToRemove;
	TArray<UDreamTweener*> tweenersToDestroy;
	for (auto& tweener : tweenersToTick)
	{
		if (!IsValid(tweener))
		{
			tweenersToRemove.Add(tweener);
		}
		else
		{
			if (tweener->GetTickType() != TickType)continue;
			if (tweener->ToNext(DeltaTime, UnscaledDeltaTime) == false)
			{
				tweenersToRemove.Add(tweener);
				tweenersToDestroy.Add(tweener);
			}
		}
	}
	for (auto& tweener : tweenersToRemove)
	{
		tweenerList.RemoveSingle(tweener);
	}
	for (auto tweener : tweenersToDestroy)
	{
		if (IsValid(tweener))
		{
			tweener->ConditionalBeginDestroy();
		}
	}
	if (TickType == EDreamTweenTickType::DuringPhysics)
	{
		if (updateEvent.IsBound())
			updateEvent.Broadcast(DeltaTime);
	}
}

void UDreamTweenManager::DisableTick()
{
	bTickPaused = true;
}
void UDreamTweenManager::EnableTick()
{
	bTickPaused = false;
}
void UDreamTweenManager::ManualTick(float DeltaTime)
{
	Tick(EDreamTweenTickType::Manual, DeltaTime);
}
void UDreamTweenManager::KillAllTweens(bool callComplete)
{
	// Kill runs the tween's OnComplete, and a completion handler is free to start a new tween -- which
	// reallocates the array this used to walk by reference, and which the trailing Reset() then threw
	// away along with the dead ones. Take the list first, then walk the copy: tweens created from a
	// completion handler land in the now-empty member and survive, as they would from anywhere else.
	TArray<TObjectPtr<UDreamTweener>> tweenersToKill = MoveTemp(tweenerList);
	tweenerList.Reset();
	for (auto item : tweenersToKill)
	{
		if (IsValid(item))
		{
			item->Kill(callComplete);
		}
	}
}

void UDreamTweenManager::KillAllTweensOnTarget(UObject* WorldContextObject, UObject* TargetObject, bool callComplete)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return;
	// Kill runs the tween's OnComplete, which may start new tweens and reallocate the list mid-walk.
	TArray<TObjectPtr<UDreamTweener>> tweenersToKill = Instance->tweenerList;
	for (auto item : tweenersToKill)
	{
		if (IsValid(item))
		{
			if (item->IsInOuter(TargetObject))
			{
				item->Kill(callComplete);
			}
		}
	}
}

bool UDreamTweenManager::IsTweening(UObject* WorldContextObject, UDreamTweener* item)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return false;
	return Instance->IsTweening(item);
}

bool UDreamTweenManager::IsTweening(UDreamTweener* item)
{
	if (!IsValid(item))return false;
	return tweenerList.Contains(item);
}

void UDreamTweenManager::KillIfIsTweening(UObject* WorldContextObject, UDreamTweener* item, bool callComplete)
{
	if (IsTweening(WorldContextObject, item))
	{
		item->Kill(callComplete);
	}
}

void UDreamTweenManager::KillIfIsTweening(UDreamTweener* item, bool callComplete)
{
	if (IsTweening(item))
	{
		item->Kill(callComplete);
	}
}

void UDreamTweenManager::RemoveTweener(UObject* WorldContextObject, UDreamTweener* item)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return;
	Instance->RemoveTweener(item);
}

void UDreamTweenManager::RemoveTweener(UDreamTweener* item)
{
	if (!IsValid(item))return;
	tweenerList.Remove(item);
}

//float
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenFloatGetterFunction& getter, const FDreamTweenFloatSetterFunction& setter, float endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerFloat>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//float
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenDoubleGetterFunction& getter, const FDreamTweenDoubleSetterFunction& setter, double endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerDouble>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//interger
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenIntGetterFunction& getter, const FDreamTweenIntSetterFunction& setter, int endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerInteger>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//position
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenPositionGetterFunction& getter, const FDreamTweenPositionSetterFunction& setter, const FVector& endValue, float duration, bool sweep, FHitResult* sweepHitResult, ETeleportType teleportType)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerPosition>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration, sweep, sweepHitResult, teleportType);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//vector
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenVectorGetterFunction& getter, const FDreamTweenVectorSetterFunction& setter, const FVector& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerVector>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//color
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenColorGetterFunction& getter, const FDreamTweenColorSetterFunction& setter, const FColor& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerColor>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//linearcolor
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenLinearColorGetterFunction& getter, const FDreamTweenLinearColorSetterFunction& setter, const FLinearColor& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerLinearColor>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//vector2d
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenVector2DGetterFunction& getter, const FDreamTweenVector2DSetterFunction& setter, const FVector2D& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerVector2D>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//vector4
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenVector4GetterFunction& getter, const FDreamTweenVector4SetterFunction& setter, const FVector4& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerVector4>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//quaternion
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenQuaternionGetterFunction& getter, const FDreamTweenQuaternionSetterFunction& setter, const FQuat& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerQuaternion>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//rotator
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenRotatorGetterFunction& getter, const FDreamTweenRotatorSetterFunction& setter, const FRotator& endValue, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerRotator>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//rotation euler
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenRotationQuatGetterFunction& getter, const FDreamTweenRotationQuatSetterFunction& setter, const FVector& eulerAngle, float duration, bool sweep, FHitResult* sweepHitResult, ETeleportType teleportType)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerRotationEuler>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, eulerAngle, duration, sweep, sweepHitResult, teleportType);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//rotation quat
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenRotationQuatGetterFunction& getter, const FDreamTweenRotationQuatSetterFunction& setter, const FQuat& endValue, float duration, bool sweep, FHitResult* sweepHitResult, ETeleportType teleportType)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerRotationQuat>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration, sweep, sweepHitResult, teleportType);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//material scalar
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenMaterialScalarGetterFunction& getter, const FDreamTweenMaterialScalarSetterFunction& setter, float endValue, float duration, int32 parameterIndex)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerMaterialScalar>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration, parameterIndex);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
//material vector
UDreamTweener* UDreamTweenManager::To(UObject* WorldContextObject, const FDreamTweenMaterialVectorGetterFunction& getter, const FDreamTweenMaterialVectorSetterFunction& setter, const FLinearColor& endValue, float duration, int32 parameterIndex)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerMaterialVector>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, endValue, duration, parameterIndex);
	Instance->tweenerList.Add(tweener);
	return tweener;
}

UDreamTweener* UDreamTweenManager::VirtualTo(UObject* WorldContextObject, float duration)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerVirtual>(WorldContextObject);
	tweener->SetInitialValue(duration);
	Instance->tweenerList.Add(tweener);
	return tweener;
}

UDreamTweener* UDreamTweenManager::DelayFrameCall(UObject* WorldContextObject, int delayFrame)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerFrame>(WorldContextObject);
	tweener->SetInitialValue(delayFrame);
	Instance->tweenerList.Add(tweener);
	return tweener;
}

UDreamTweenerSpring* UDreamTweenManager::SpringTo(UObject* WorldContextObject, const FDreamTweenFloatGetterFunction& getter, const FDreamTweenFloatSetterFunction& setter, float target, const FDreamSpringParams& params)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerSpring>(WorldContextObject);
	tweener->SetInitialValue(getter, setter, target, params);
	Instance->tweenerList.Add(tweener);
	return tweener;
}

UDreamTweener* UDreamTweenManager::UpdateCall(UObject* WorldContextObject)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerUpdate>(WorldContextObject);
	Instance->tweenerList.Add(tweener);
	return tweener;
}

UDreamTweenerSequence* UDreamTweenManager::CreateSequence(UObject* WorldContextObject)
{
	auto Instance = GetDreamTweenInstance(WorldContextObject);
	if (!IsValid(Instance))return nullptr;

	auto tweener = NewObject<UDreamTweenerSequence>(WorldContextObject);
	Instance->tweenerList.Add(tweener);
	return tweener;
}
