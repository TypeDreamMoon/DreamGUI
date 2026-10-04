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
#include "UObject/GarbageCollection.h"

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
		// Once per tick group per frame, however many worlds' helpers drive this manager: see TickFromWorld.
		Target->TickFromWorld((EDreamTweenTickType)((uint8)PrimaryComponentTick.TickGroup), DeltaTime);
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
		Target->TickFromWorld(EDreamTweenTickType::DuringPhysics, DeltaSeconds);
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
	// The event is static, so EVERY helper actor in every PIE instance hears EVERY manager being
	// created. Taking the one that was broadcast on trust would let a second PIE client's manager
	// drive this world's actor -- every tween in one instance stepped by the other's clock. Ask for
	// the manager of this actor's own game instance instead; if it is not up yet, this was somebody
	// else's news and the subscription stays open for our own.
	UDreamTweenManager* MyManager = UDreamTweenManager::GetDreamTweenInstance(this);
	if (MyManager == nullptr)
	{
		return;
	}
	SetupTick(MyManager);
	if (OnDreamTweenManagerCreatedDelegateHandle.IsValid())
	{
		UDreamTweenManager::OnDreamTweenManagerCreated.Remove(OnDreamTweenManagerCreatedDelegateHandle);
		OnDreamTweenManagerCreatedDelegateHandle.Reset();
	}
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
	// The one place this event can mean anything. ADreamTweenTickHelperActor::BeginPlay subscribes to
	// it as its only fallback for "the subsystem is not up yet", and nothing in the plugin ever fired
	// it: on the day that fallback was actually needed, SetupTick would never run and every tween in
	// that world would stop being ticked, without a line of log to say why. The subsystem is already
	// in the collection's map by the time Initialize runs, so a listener can resolve us from here.
	OnDreamTweenManagerCreated.Broadcast(this);
}

void UDreamTweenManager::Deinitialize()
{
	Super::Deinitialize();
	// Killed, not merely forgotten. Dropping the array left every tween believing it was still
	// running: callers holding a tweener got "not tweening" from the manager while the tween itself
	// reported otherwise, and nothing that was waiting on a completion ever heard one. callComplete
	// stays false on purpose -- the game instance is going away, and running author completion
	// handlers into a world being torn down is how a "finish" handler reaches a half-destroyed widget.
	KillAllTweens(false);
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

void UDreamTweenManager::TickFromWorld(EDreamTweenTickType TickType, float DeltaTime)
{
	// Keyed on the engine frame, not on the world: each world's helper reads the same game-instance world's delta
	// (see Tick), so whichever helper comes first in a frame steps the group and the rest find it already stepped.
	const int32 TickIndex = static_cast<int32>(TickType);
	if (TickIndex >= 0 && TickIndex < UE_ARRAY_COUNT(LastWorldTickFrames))
	{
		const uint64 FrameMark = GFrameCounter + 1;
		if (LastWorldTickFrames[TickIndex] == FrameMark)
		{
			return;
		}
		LastWorldTickFrames[TickIndex] = FrameMark;
	}
	Tick(TickType, DeltaTime);
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

	// No time is the only safe reading of a step that is negative or not a number. ManualTick hands over
	// whatever its caller worked out, and a negative step ran every clock backwards: a tween parked in its
	// delay for good, or easing a time below zero, where OutCirc answers NaN.
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		DeltaTime = 0.0f;
	}
	if (!FMath::IsFinite(UnscaledDeltaTime) || UnscaledDeltaTime < 0.0f)
	{
		UnscaledDeltaTime = 0.0f;
	}

	// A tween's own callbacks run inside ToNext, and they are free to start a tween, kill every tween, or
	// remove this one -- so the list can be grown, emptied or reordered underneath the walk. Walking it by
	// index meant the removal below used an index that no longer named the tween that had just finished:
	// it deleted whatever had shifted into that slot, or ran off the end of a list a callback had emptied.
	// So the walk is over a snapshot, and the finished ones come out afterwards, in one pass over the list
	// (a search per finished tween made a mass completion quadratic). The snapshot keeps its allocation from
	// one tick to the next; a callback that ticks the manager from inside a tick walks one of its own.
	TArray<UDreamTweener*> NestedSnapshot;
	TArray<UDreamTweener*>& Snapshot = TickDepth == 0 ? TickSnapshot : NestedSnapshot;
	TGuardValue<int32> TickDepthGuard(TickDepth, TickDepth + 1);
	Snapshot.Reset(tweenerList.Num());
	for (const TObjectPtr<UDreamTweener>& Item : tweenerList)
	{
		Snapshot.Add(Item.Get());
	}

	// What finished, with its clock generation at the moment it did: a callback later in this tick may still
	// restart it, seek it or hand it to a sequence, and one that did has taken it back (see clockGeneration).
	struct FFinishedTweener
	{
		UDreamTweener* Tweener = nullptr;
		int32 Generation = 0;
	};
	TArray<FFinishedTweener, TInlineAllocator<16>> Finished;
	bool bListHasDeadEntries = false;
	for (UDreamTweener* Tweener : Snapshot)
	{
		if (!IsValid(Tweener) || Tweener->bRetired)
		{
			// Gone -- swept away with the widget it was made on -- or retired since the snapshot was taken.
			bListHasDeadEntries = true;
		}
		else if (Tweener->IsMarkedToKill() || Tweener->IsOwnerGone())
		{
			// Ahead of the tick-type filter, deliberately. Kill only raises a flag; the list entry goes
			// away when a tick sees it. A tween set to Manual tick is only ever seen by ManualTick, so
			// a killed one that nobody ticks again stayed in this list forever -- holding its outer,
			// which is usually the very widget it was animating, alive for the rest of the run.
			// A tween whose outer is gone is over the same way, and without a callback: what it animated
			// no longer exists, and an endless loop or a held tween made on an actor that was destroyed
			// used to go on stepping -- and calling its callbacks on the remains -- until the map changed.
			Finished.Add({ Tweener, Tweener->clockGeneration });
		}
		else
		{
			if (Tweener->GetTickType() != TickType)continue;
			if (Tweener->ToNext(DeltaTime, UnscaledDeltaTime) == false)
			{
				Finished.Add({ Tweener, Tweener->clockGeneration });
			}
		}
	}

	TArray<UDreamTweener*, TInlineAllocator<16>> RetiredNow;
	for (const FFinishedTweener& Candidate : Finished)
	{
		UDreamTweener* Tweener = Candidate.Tweener;
		if (!IsValid(Tweener) || Tweener->bRetired)
		{
			continue;
		}
		// Taken over since it finished, by a callback later in this tick, and not to end: it runs on.
		if (Tweener->clockGeneration != Candidate.Generation && !Tweener->IsMarkedToKill() && !Tweener->IsOwnerGone())
		{
			continue;
		}
		Tweener->bRetired = true;
		RetiredNow.Add(Tweener);
	}
	if (RetiredNow.Num() > 0 || bListHasDeadEntries)
	{
		tweenerList.RemoveAll([](const TObjectPtr<UDreamTweener>& Item)
		{
			return !IsValid(Item) || Item->bRetired;
		});
	}
	for (UDreamTweener* Tweener : RetiredNow)
	{
		RetireTweener(Tweener);
	}
	Snapshot.Reset();
}

void UDreamTweenManager::RetireTweener(UDreamTweener* Tweener)
{
	Tweener->bRetired = true;
	// Marked garbage rather than ConditionalBeginDestroy'd. Destroying it while something still held it --
	// a Blueprint variable, a game-instance object, a raw pointer -- left a half-destroyed tween that still
	// answered IsValid, completed a second time on Kill(true) or ForceComplete, and, if the holder outlived
	// the map, pinned the old world through the tween's outer until the stale-world check stopped the game.
	// Garbage, every handle reads it as gone, and the collector takes it along with that hold.
	if (!Tweener->IsRooted() && !IsGarbageCollecting())
	{
		Tweener->MarkAsGarbage();
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
	for (UDreamTweener* item : tweenersToKill)
	{
		if (!IsValid(item))
		{
			continue;
		}
		item->Kill(callComplete);
		if (item->IsMarkedToKill())
		{
			// Out of the list already, so no tick will see it again to retire it: retired here.
			RetireTweener(item);
		}
		else if (!tweenerList.Contains(item))
		{
			// Its own completion handler restarted it. It runs on, back in the list it was taken out of --
			// a tween restarted from a handler keeps running wherever the kill came from.
			tweenerList.Add(item);
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
