// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIInputSubsystem.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWorldContext.h"
#include "DreamGUI.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/UISelectable.h"
#include "Utils/DreamUIUtils.h"
#if WITH_EDITOR
#include "EditorViewportClient.h"
#endif

UDreamUIInputSubsystem* UDreamUIInputSubsystem::Get(const UObject* InWorldContext)
{
	const UWorld* World = GEngine != nullptr && InWorldContext != nullptr
		? GEngine->GetWorldFromContextObject(InWorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<UDreamUIInputSubsystem>() : nullptr;
}

bool UDreamUIInputSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// The same gate as UDreamUIManagerWorldSubsystem's: every question asked here is about a widget that
	// manager registered, and a world with one and not the other would answer half of them.
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUIInputSubsystem::PostInitialize()
{
	Super::PostInitialize();
#if WITH_EDITOR
	// PostInitialize rather than Initialize: every subsystem of the world exists by now, the manager included.
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->OnDrawHelperGizmo.AddUObject(this, &UDreamUIInputSubsystem::DrawNavigationVisualizers);
	}
#endif
}

void UDreamUIInputSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->OnDrawHelperGizmo.RemoveAll(this);
	}
#endif
	// The interaction objects this subsystem spawned are its to take away again. They are transient, so a
	// level change would not carry them anyway; destroying them here is what keeps a PIE session that
	// starts and stops repeatedly from leaving a host actor behind on every run.
	for (TPair<int32, TObjectPtr<AActor>>& HostPair : InteractionHosts)
	{
		if (IsValid(HostPair.Value))
		{
			HostPair.Value->Destroy();
		}
	}
	InteractionHosts.Reset();
	if (IsValid(CreatedEventSystemActor))
	{
		CreatedEventSystemActor->Destroy();
		CreatedEventSystemActor = nullptr;
	}
	Super::Deinitialize();
}

#pragma region EventSystemRegistry
UDreamEventSystem* UDreamUIInputSubsystem::GetEventSystemByUserIndex(int32 InUserIndex) const
{
	if (const TWeakObjectPtr<UDreamEventSystem>* ResultPtr = MapUserIndexToEventSystem.Find(InUserIndex))
	{
		return ResultPtr->Get();
	}
	return nullptr;
}

void UDreamUIInputSubsystem::AddEventSystem(UDreamEventSystem* InEventSystem)
{
	if (!IsValid(InEventSystem))return;

	// The entry is a weak pointer, so "a key exists" and "an event system is registered" are different
	// questions. A level reload destroys the old component and leaves its stale entry behind: asking
	// that entry for an owner to name was a null dereference, and reporting it was a duplicate error
	// about a component that no longer exists -- after which the new level's UI was never registered
	// and stopped responding entirely.
	const TWeakObjectPtr<UDreamEventSystem>* InstancePtr = MapUserIndexToEventSystem.Find(InEventSystem->GetUserIndex());
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

void UDreamUIInputSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;

	const int32 UserIndex = InEventSystem->GetUserIndex();
	const TWeakObjectPtr<UDreamEventSystem>* InstancePtr = MapUserIndexToEventSystem.Find(UserIndex);
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
#pragma endregion

#pragma region FocusHoverCapture
bool UDreamUIInputSubsystem::SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(InPointerId, true);
	EventSystem->SetSelectWidget(InWidget, EventData);
	// The navigation cursor has to move with focus, or the next directional press starts from wherever
	// focus USED to be and appears to teleport. UDreamUINavigationStack::FocusSelectable already does
	// both halves for the same reason.
	EventSystem->SetHighlightedComponentForNavigation(InWidget, InPointerId);
	return true;
}

bool UDreamUIInputSubsystem::HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	return EventSystem != nullptr && EventSystem->GetCurrentSelectedComponent(InPointerId) == InWidget;
}

void UDreamUIInputSubsystem::ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	if (UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(InPointerId, false);
		if (EventData && EventData->SelectedComponent == InWidget)
		{
			EventSystem->SetSelectWidget(nullptr, EventData);
		}
	}
}

bool UDreamUIInputSubsystem::HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		UDreamWidget* Focused = EventSystem->GetCurrentSelectedComponent(Entry.Key);
		// Descendants, not "this or its descendants" -- UMG draws the same line, and a widget asking
		// whether something INSIDE it has focus already knows whether it has focus itself.
		if (IsValid(Focused) && Focused != InWidget && Focused->IsChildOf(InWidget))
		{
			return true;
		}
	}
	return false;
}

bool UDreamUIInputSubsystem::IsHovered(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		if (PointerEvent->EnterWidget.Get() == InWidget)
		{
			return true;
		}
		// The enter STACK as well, so a button still reads as hovered while the pointer is over its own
		// label. Slate gets that for free because hover propagates to parents; here the stack is where
		// that fact lives.
		for (const TObjectPtr<UDreamWidget>& Entered : PointerEvent->EnterWidgetStack)
		{
			if (Entered.Get() == InWidget)
			{
				return true;
			}
		}
	}
	return false;
}

bool UDreamUIInputSubsystem::HasMouseCapture(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerIndex) const
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		if (InPointerIndex >= 0 && Entry.Key != InPointerIndex)
		{
			continue;
		}
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		// Held down AND pressed on this widget: that pointer's drag and its release go here whatever it
		// travels over in between, which is the whole of what capture buys a caller.
		if (PointerEvent->bNowIsTriggerPressed && PointerEvent->PressWidget.Get() == InWidget)
		{
			return true;
		}
	}
	return false;
}

UDreamPointerEventData* UDreamUIInputSubsystem::FindPointer(int32 InUserIndex, int32 InPointerId) const
{
	UDreamEventSystem* EventSystem = GetEventSystemByUserIndex(InUserIndex);
	return IsValid(EventSystem) ? EventSystem->GetPointerEventData(InPointerId, false) : nullptr;
}
#pragma endregion

#pragma region Actions
bool UDreamUIInputSubsystem::CanListenForActions() const
{
	return UDreamUIActionRouter::Get(this) != nullptr;
}

FDreamUIActionHandle UDreamUIInputSubsystem::RegisterWidgetAction(UDreamWidget* InOwner, const FDataTableRowHandle& InAction,
	FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex, bool bInDisplayInActionBar)
{
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	if (Router == nullptr)
	{
		return FDreamUIActionHandle();
	}
	// Scoped to the screen the owner is inside, so the binding is live only while that screen is in front.
	// A widget with no scope above it binds globally, which is the honest reading of "there is no screen
	// this belongs to".
	UDreamUINavigationScope* Scope = nullptr;
	for (UDreamWidget* Walker = InOwner; IsValid(Walker) && Scope == nullptr; Walker = Walker->GetParent())
	{
		Scope = Walker->GetComponent<UDreamUINavigationScope>();
	}
	return Router->RegisterAction(Scope, InAction, InCallback, InUserIndex, bInDisplayInActionBar);
}

void UDreamUIInputSubsystem::UnregisterAction(const FDreamUIActionHandle& InHandle)
{
	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		Router->UnregisterAction(InHandle);
	}
}
#pragma endregion

#pragma region DragDrop
bool UDreamUIInputSubsystem::IsDragDropping() const
{
	const UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(this);
	return DragDrop != nullptr && DragDrop->IsDragInProgress();
}

UDreamDragDropOperation* UDreamUIInputSubsystem::GetDragOperationForPointer(int32 InPointerId) const
{
	const UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(this);
	return DragDrop != nullptr ? DragDrop->GetDragOperationForPointer(InPointerId) : nullptr;
}

bool UDreamUIInputSubsystem::CancelActiveDrag()
{
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(this);
	return DragDrop != nullptr && DragDrop->CancelActiveDrag();
}
#pragma endregion

#pragma region Interaction
namespace DreamUIInputSubsystemLocal
{
	/**
	 * The index of the first local player -- the one a null owning player resolves to everywhere else in
	 * the plugin. Usually 0, but it is read rather than assumed so that "the first player" keeps meaning
	 * the same thing here as it does to a widget asking who owns it.
	 */
	int32 FirstLocalPlayerIndex(const UWorld* InWorld)
	{
		return InWorld != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(InWorld->GetFirstPlayerController()) : 0;
	}

#if WITH_EDITOR
	/** Arrows from InSelectable to wherever navigation would move from it, drawn with the manager's helpers. */
	void DrawNavigationVisualizer(UDreamUIManagerWorldSubsystem* Manager, UWorld* InWorld, UUISelectable* InSelectable, bool IsScreenSpace)
	{
		auto SourceWidget = InSelectable->GetWidget();
		if (!IsValid(SourceWidget))return;
		const FColor Color = Manager->GetSelection()->IsSelected(SourceWidget) ? FColor(255, 255, 0, 255) : FColor(140, 140, 0, 255);
		constexpr float Offset = 2;
		constexpr float ArrowSize = 5;
	
		auto GetArrowSizeScaledByDistanceToCamera = [=](FVector WorldPoint)
		{
			if (Manager->GetWorld()->IsGameWorld())
			{
				if (auto PC = Manager->GetWorld()->GetFirstPlayerController())
				{
					if (auto CameraManager = PC->PlayerCameraManager)
					{
						auto ViewLocation = CameraManager->GetCameraLocation();
						float Distance = FVector::Distance(WorldPoint, ViewLocation);
						return Distance * 0.01f;
					}
				}
			}
			else
			{
				if (auto ViewportClient = Manager->GetEditorViewportClient())
				{
					if (ViewportClient->IsOrtho())
					{
						return ViewportClient->GetOrthoZoom() * 0.001f; 
					}
					else
					{
						auto ViewLocation = ViewportClient->GetViewLocation();
						float Distance = FVector::Distance(WorldPoint, ViewLocation);
						return Distance * 0.01f;
					}
				}
			}
			return ArrowSize;
		};

		if (auto ToLeftComp = InSelectable->FindSelectableOnLeft())
		{
			if (ToLeftComp != InSelectable)
			{
				auto SourceLeftPoint = FVector(0, SourceWidget->GetLocalSpaceLeft(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) + Offset);
				SourceLeftPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceLeftPoint);
				auto DestWidget = ToLeftComp->GetWidget();
				auto LocalDestRightPoint = FVector(0, DestWidget->GetLocalSpaceRight(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) + Offset);
				auto DestRightPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint);
				float Distance = FVector::Distance(SourceLeftPoint, DestRightPoint);
				Distance *= 0.2f;
				auto ScaledArrowSize = ArrowSize;
				if (!IsScreenSpace)
				{
					ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestRightPoint);
				}
				auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
				auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
				Manager->DrawNavigationArrow(InWorld
					, {
						SourceLeftPoint,
						SourceLeftPoint - SourceWidget->GetRightVector() * Distance,
						DestRightPoint + DestWidget->GetRightVector() * Distance,
						DestRightPoint,
					}
					, ArrowPointA, ArrowPointB
					, Color, InSelectable, FString::Printf(TEXT("%s.NavigationLeft"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
			}
		}
		if (auto ToRightComp = InSelectable->FindSelectableOnRight())
		{
			if (ToRightComp != InSelectable)
			{
				auto SourceRightPoint = FVector(0, SourceWidget->GetLocalSpaceRight(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) - Offset);
				SourceRightPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceRightPoint);
				auto DestWidget = ToRightComp->GetWidget();
				auto LocalDestLeftPoint = FVector(0, DestWidget->GetLocalSpaceLeft(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) - Offset);
				auto DestLeftPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint);
				float Distance = FVector::Distance(SourceRightPoint, DestLeftPoint);
				Distance *= 0.2f;
				auto ScaledArrowSize = ArrowSize;
				if (!IsScreenSpace)
				{
					ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestLeftPoint);
				}
				auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
				auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
				Manager->DrawNavigationArrow(InWorld
					, {
						SourceRightPoint,
						SourceRightPoint + SourceWidget->GetRightVector() * Distance,
						DestLeftPoint - DestWidget->GetRightVector() * Distance,
						DestLeftPoint,
					}
					, ArrowPointA, ArrowPointB
					, Color, InSelectable, FString::Printf(TEXT("%s.NavigationRight"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
			}
		}
		if (auto ToDownComp = InSelectable->FindSelectableOnDown())
		{
			if (ToDownComp != InSelectable)
			{
				auto SourceDownPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) - Offset, SourceWidget->GetLocalSpaceBottom());
				SourceDownPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceDownPoint);
				auto DestWidget = ToDownComp->GetWidget();
				auto LocalDestUpPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) - Offset, DestWidget->GetLocalSpaceTop());
				auto DestUpPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint);
				float Distance = FVector::Distance(SourceDownPoint, DestUpPoint);
				Distance *= 0.2f;
				auto ScaledArrowSize = ArrowSize;
				if (!IsScreenSpace)
				{
					ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestUpPoint);
				}
				auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
				auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
				Manager->DrawNavigationArrow(InWorld
					, {
						SourceDownPoint,
						SourceDownPoint - SourceWidget->GetUpVector() * Distance,
						DestUpPoint + DestWidget->GetUpVector() * Distance,
						DestUpPoint,
					}
					, ArrowPointA, ArrowPointB
					, Color, InSelectable, FString::Printf(TEXT("%s.NavigationDown"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
			}
		}
		if (auto ToUpComp = InSelectable->FindSelectableOnUp())
		{
			if (ToUpComp != InSelectable)
			{
				auto SourceUpPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) + Offset, SourceWidget->GetLocalSpaceTop());
				SourceUpPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceUpPoint);
				auto DestWidget = ToUpComp->GetWidget();
				auto LocalDestDownPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) + Offset, DestWidget->GetLocalSpaceBottom());
				auto DestDownPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint);
				float Distance = FVector::Distance(SourceUpPoint, DestDownPoint);
				Distance *= 0.2f;
				auto ScaledArrowSize = ArrowSize;
				if (!IsScreenSpace)
				{
					ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestDownPoint);
				}
				auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
				auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
				Manager->DrawNavigationArrow(InWorld
					, {
						SourceUpPoint,
						SourceUpPoint + SourceWidget->GetUpVector() * Distance,
						DestDownPoint - DestWidget->GetUpVector() * Distance,
						DestDownPoint,
					}
					, ArrowPointA, ArrowPointB
					, Color, InSelectable, FString::Printf(TEXT("%s.NavigationUp"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
			}
		}
	}
#endif

	/** Does this player already have a raycaster of this kind, wherever it was placed? */
	bool HasRaycasterOfKind(const UDreamBaseRaycaster* InRaycaster, EDreamInteractionKind InKind)
	{
		return InKind == EDreamInteractionKind::Screen
			? InRaycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass())
			: InRaycaster->IsA(UDreamWorldSpaceRaycaster::StaticClass());
	}
}

void UDreamUIInputSubsystem::EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind)
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
	if (!bHasEventSystem && InUserIndex != DreamUIInputSubsystemLocal::FirstLocalPlayerIndex(World))
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

	// An authored raycaster wins. Somebody who placed a world-space raycaster on their pawn, or a screen
	// raycaster with a hand-tuned drag threshold, said what they wanted; adding a default one beside it
	// would give that player two rays into the same UI.
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& RaycasterPtr : Manager->GetAllRaycasterArray())
		{
			const UDreamBaseRaycaster* Raycaster = RaycasterPtr.Get();
			if (IsValid(Raycaster) && Raycaster->GetUserIndex() == InUserIndex
				&& DreamUIInputSubsystemLocal::HasRaycasterOfKind(Raycaster, InKind))
			{
				return;
			}
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
	// Asked again on the host itself, because a raycaster only enrols in the manager's raycaster list when
	// it activates, and a world that has not begun play never activates one. Without this the second call
	// would add a second raycaster to the same host and the function would not be idempotent in exactly
	// the case -- an inactive or headless world -- where nothing else would notice.
	for (UActorComponent* Component : Host->GetComponents())
	{
		const UDreamBaseRaycaster* Existing = Cast<UDreamBaseRaycaster>(Component);
		if (Existing != nullptr && DreamUIInputSubsystemLocal::HasRaycasterOfKind(Existing, InKind))
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

AActor* UDreamUIInputSubsystem::GetInteractionHost(int32 InUserIndex) const
{
	const TObjectPtr<AActor>* Found = InteractionHosts.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

void UDreamUIInputSubsystem::PrepareScreenInteraction(UDreamCanvas* InRootCanvas, int32 InUserIndex)
{
	if (!IsValid(InRootCanvas))
	{
		return;
	}
	// A screen page needs the same event system and raycaster a world-space host does. What is particular
	// to a screen is telling this player's screen raycaster which canvas it projects through.
	EnsureInteractionForPlayer(InUserIndex, EDreamInteractionKind::Screen);

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
		{
			UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Raycaster.Get());
			// Only a raycaster that speaks for THIS player. A second player's raycaster carries its own
			// UserIndex and must keep pointing at its own canvas; retargeting every screen raycaster at
			// whichever root was built last is what made split screen impossible.
			if (ScreenRaycaster != nullptr && ScreenRaycaster->GetUserIndex() == InUserIndex)
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
			}
		}
	}
	// The one just created is on its host actor and has not necessarily enrolled -- enrolment happens on
	// activation, which a world that has not begun play never performs -- so it would otherwise be left
	// without a canvas until the first frame of play.
	if (const AActor* Host = GetInteractionHost(InUserIndex))
	{
		for (UActorComponent* Component : Host->GetComponents())
		{
			if (UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Component);
				ScreenRaycaster != nullptr && ScreenRaycaster->GetUserIndex() == InUserIndex)
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
			}
		}
	}
}
#pragma endregion

#if WITH_EDITOR
void UDreamUIInputSubsystem::DrawNavigationVisualizers(UDreamUIManagerWorldSubsystem* InManager)
{
	if (!GetDefault<UDreamUIEditorSettings>()->bDrawSelectableNavigationVisualizer)
	{
		return;
	}
	for (const TWeakObjectPtr<UDreamUIBehaviour>& Entry : InManager->GetAllSelectableArray())
	{
		UUISelectable* Selectable = Cast<UUISelectable>(Entry.Get());
		if (Selectable == nullptr)continue;
		if (!IsValid(Selectable->GetWorld()))continue;
		if (!IsValid(Selectable->GetWidget()))continue;
		if (!IsValid(Selectable->GetWidget()->GetRenderCanvas()))continue;
		if (!Selectable->GetWidget()->GetInteractableInHierarchy())continue;

		bool bIsScreenSpace = false;
		if (DreamUI::IsGameWorld(Selectable))
		{
			UDreamCanvas* RenderCanvas = Selectable->GetWidget()->GetRenderCanvas();
			bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
		}
		DreamUIInputSubsystemLocal::DrawNavigationVisualizer(InManager, Selectable->GetWorld(), Selectable, bIsScreenSpace);
	}
}
#endif
