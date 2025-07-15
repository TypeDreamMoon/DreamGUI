// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LGUILifeCycleBehaviour.h"
#include "LGUI.h"
#include "Core/LGUIManager.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "Components/SceneComponent.h"

ULGUILifeCycleBehaviour::ULGUILifeCycleBehaviour()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
}

void ULGUILifeCycleBehaviour::BeginPlay()
{
	Super::BeginPlay();
	ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehaviourForLifecycleEvent(this);
}
void ULGUILifeCycleBehaviour::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
void ULGUILifeCycleBehaviour::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
void ULGUILifeCycleBehaviour::OnRegister()
{
	Super::OnRegister();
}
void ULGUILifeCycleBehaviour::OnUnregister()
{
	Super::OnUnregister();
}

#if WITH_EDITOR
void ULGUILifeCycleBehaviour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

void ULGUILifeCycleBehaviour::SetCanExecuteUpdate(bool value)
{
	if (bCanExecuteUpdate != value)
	{
		bCanExecuteUpdate = value;
		if (bIsStartCalled)
		{
			if (bCanExecuteUpdate)
			{
				ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehavioursForUpdate(this);
			}
			else
			{
				ULGUIManagerWorldSubsystem::RemoveLGUILifeCycleBehavioursFromUpdate(this);
			}
		}
	}
}

void ULGUILifeCycleBehaviour::Awake()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveAwake();
	}
}
void ULGUILifeCycleBehaviour::Start()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveStart();
	}
}
void ULGUILifeCycleBehaviour::Update(float DeltaTime)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveUpdate(DeltaTime);
	}
}

void ULGUILifeCycleBehaviour::Call_Awake()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsAwakeCalled)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Awake already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsAwakeCalled = true;
	Awake();
}

void ULGUILifeCycleBehaviour::Call_Start()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsStartCalled)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Start already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsStartCalled = true;
	Start();
}

USceneComponent* ULGUILifeCycleBehaviour::GetRootSceneComponent()const
{
	if (!RootComp.IsValid())
	{
		if (auto Owner = GetOwner())
		{
			if (auto TempRootComp = Owner->GetRootComponent())
			{
				RootComp = TWeakObjectPtr<USceneComponent>(TempRootComp);
			}
			else
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d RootComponent not exist in owner actor!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
		}
		else
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Owner is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	return RootComp.Get();
}
