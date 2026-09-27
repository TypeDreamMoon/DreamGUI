// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0

void UDreamUIManagerWorldSubsystem::AddRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		auto& AllRaycasterArray = Instance->AllRaycasterArray;
		if (AllRaycasterArray.Contains(InRaycaster))return;
		AllRaycasterArray.Add(InRaycaster);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		int32 index;
		if (Instance->AllRaycasterArray.Find(InRaycaster, index))
		{
			Instance->AllRaycasterArray.RemoveAt(index);
		}
	}
}

void UDreamUIManagerWorldSubsystem::AddSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		auto& AllSelectableArray = Instance->AllSelectableArray;
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
		if (AllSelectableArray.Contains(InSelectable))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		AllSelectableArray.AddUnique(InSelectable);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		int32 index;
		if (Instance->AllSelectableArray.Find(InSelectable, index))
		{
			Instance->AllSelectableArray.RemoveAt(index);
		}
	}
}

UDreamEventSystem* UDreamUIManagerWorldSubsystem::GetEventSystemByUserIndex(int UserIndex)
{
	if (auto ResultPtr = MapUserIndexToEventSystem.Find(UserIndex))
	{
		return ResultPtr->Get();
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddEventSystem(UDreamEventSystem* InEventSystem)
{
	if (!IsValid(InEventSystem))return;

	// The entry is a weak pointer, so "a key exists" and "an event system is registered" are different
	// questions. A level reload destroys the old component and leaves its stale entry behind: asking
	// that entry for an owner to name was a null dereference, and reporting it was a duplicate error
	// about a component that no longer exists -- after which the new level's UI was never registered
	// and stopped responding entirely.
	auto InstancePtr = MapUserIndexToEventSystem.Find(InEventSystem->GetUserIndex());
	UDreamEventSystem* Instance = InstancePtr != nullptr ? InstancePtr->Get() : nullptr;
	if (IsValid(Instance) && Instance != InEventSystem)
	{
		const AActor* InstanceOwner = Instance->GetOwner();
		FString ActorName = InstanceOwner == nullptr ? TEXT("(no owner)") :
#if WITH_EDITOR
			InstanceOwner->GetActorLabel();
#else
			InstanceOwner->GetName();
#endif
		FString ErrorMsg = FString::Printf(TEXT("[%s].%d DreamEventSystem component is already exist in actor:%s, pathName:%s, world:%s, multiple DreamEventSystem with same UserIndex in same world is not allowed!")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ActorName, *Instance->GetPathName(), *GetWorld()->GetPathName());
		UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrorMsg);
		GEngine->AddOnScreenDebugMessage(-1, -1, FColor::Red, ErrorMsg);
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(FText::FromString(ErrorMsg), false, 10);
#endif
	}
	else
	{
		MapUserIndexToEventSystem.Add(InEventSystem->GetUserIndex(), InEventSystem);
	}
}

void UDreamUIManagerWorldSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;

	const int UserIndex = InEventSystem->GetUserIndex();
	auto InstancePtr = MapUserIndexToEventSystem.Find(UserIndex);
	if (InstancePtr == nullptr)return;
	// Removed by identity, not by user index. An unregister arriving late -- the previous level's event
	// system being destroyed after the new one has already claimed the same index -- used to evict the
	// live registration and leave that player's UI deaf with nothing in the log.
	UDreamEventSystem* Instance = InstancePtr->Get();
	if (Instance == InEventSystem || Instance == nullptr)
	{
		MapUserIndexToEventSystem.Remove(UserIndex);
	}
}

namespace DreamInteractionLocal
{
	/**
	 * The index of the first local player -- the one a null owning player resolves to everywhere else
	 * in the plugin. Usually 0, but it is read rather than assumed so that "the first player" keeps
	 * meaning the same thing here as it does to a widget asking who owns it.
	 */
	int32 FirstLocalPlayerIndex(const UWorld* InWorld)
	{
		return InWorld != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(InWorld->GetFirstPlayerController()) : 0;
	}

	/** Does this player already have a raycaster of this kind, wherever it was placed? */
	bool HasRaycasterOfKind(const UDreamBaseRaycaster* InRaycaster, EDreamInteractionKind InKind)
	{
		return InKind == EDreamInteractionKind::Screen
			? InRaycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass())
			: InRaycaster->IsA(UDreamWorldSpaceRaycaster::StaticClass());
	}
}

void UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind)
{
	UWorld* World = GetWorld();
	if (World == nullptr)return;

	// The event system for THIS player, not "the one at index 0". A second local player with no event
	// system of their own gets nothing rather than borrowing the first player's cursor.
	//
	// The registry is only half the answer: a placed event system enrols itself when it begins play,
	// so during level startup the component can exist while the map does not know about it yet.
	bool bHasEventSystem = GetEventSystemByUserIndex(InUserIndex) != nullptr;
	if (!bHasEventSystem)
	{
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if (const UDreamEventSystem* PlacedEventSystem = ActorIt->FindComponentByClass<UDreamEventSystem>();
				PlacedEventSystem != nullptr && PlacedEventSystem->GetUserIndex() == InUserIndex)
			{
				bHasEventSystem = true;
				break;
			}
		}
	}
	// Only the first player gets one spawned for them. A second local player's event system has to be
	// placed deliberately -- with its UserIndex set -- because spawning a copy of the default actor
	// would give both players the same index and make each read the other's input.
	if (!bHasEventSystem && InUserIndex != DreamInteractionLocal::FirstLocalPlayerIndex(World))
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("Local player %d has DreamUI to point at but no event system with that UserIndex, so it takes no input. ")
			TEXT("Place a DreamEventSystem with UserIndex %d for that player."), InUserIndex, InUserIndex);
	}
	else if (!bHasEventSystem && !IsValid(CreatedEventSystemActor))
	{
		if (UClass* EventSystemClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->EventSystemActorClass, TEXT("EventSystemActorClass")))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(World, EventSystemClass, TEXT("DreamEventSystem"));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreatedEventSystemActor = World->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, SpawnParameters);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("Cannot create DreamUI input: Project Settings > Plugins > Dream GUI > ")
				TEXT("EventSystemActorClass is not set or failed to load."));
		}
	}

	// An authored raycaster wins. Somebody who placed a world-space raycaster on their pawn, or a
	// screen raycaster with a hand-tuned drag threshold, said what they wanted; adding a default one
	// beside it would give that player two rays into the same UI.
	for (const TWeakObjectPtr<UDreamBaseRaycaster>& RaycasterPtr : AllRaycasterArray)
	{
		const UDreamBaseRaycaster* Raycaster = RaycasterPtr.Get();
		if (IsValid(Raycaster) && Raycaster->GetUserIndex() == InUserIndex
			&& DreamInteractionLocal::HasRaycasterOfKind(Raycaster, InKind))
		{
			return;
		}
	}

	TObjectPtr<AActor>& HostSlot = InteractionHosts.FindOrAdd(InUserIndex);
	if (!IsValid(HostSlot))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(),
			*FString::Printf(TEXT("DreamInteractionHost_P%d"), InUserIndex));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HostSlot = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (IsValid(HostSlot))
		{
			HostSlot->SetActorEnableCollision(false);
		}
	}
	AActor* Host = HostSlot.Get();
	if (!IsValid(Host))return;
	// Asked again on the host itself, because a raycaster only enrols in AllRaycasterArray when it
	// activates, and a world that has not begun play never activates one. Without this the second
	// call would add a second raycaster to the same host and the function would not be idempotent
	// in exactly the case -- an inactive or headless world -- where nothing else would notice.
	for (UActorComponent* Component : Host->GetComponents())
	{
		const UDreamBaseRaycaster* Existing = Cast<UDreamBaseRaycaster>(Component);
		if (Existing != nullptr && DreamInteractionLocal::HasRaycasterOfKind(Existing, InKind))
		{
			return;
		}
	}

	UDreamBaseRaycaster* NewRaycaster = InKind == EDreamInteractionKind::Screen
		? static_cast<UDreamBaseRaycaster*>(NewObject<UDreamScreenSpaceRaycaster>(Host, NAME_None, RF_Transient))
		: static_cast<UDreamBaseRaycaster*>(NewObject<UDreamWorldSpaceRaycaster>(Host, NAME_None, RF_Transient));
	NewRaycaster->SetUserIndex(InUserIndex);
	Host->AddInstanceComponent(NewRaycaster);
	NewRaycaster->RegisterComponent();
}

AActor* UDreamUIManagerWorldSubsystem::GetInteractionHost(int32 InUserIndex)const
{
	const TObjectPtr<AActor>* Found = InteractionHosts.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

#undef LOCTEXT_NAMESPACE
