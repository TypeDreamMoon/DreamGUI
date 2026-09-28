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
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputUser.h"
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

void FDreamUIInputTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (UDreamUIInputSubsystem* InputSubsystem = Subsystem.Get())
	{
		InputSubsystem->ProcessFrame(DeltaTime);
	}
}

FString FDreamUIInputTickFunction::DiagnosticMessage()
{
	return TEXT("FDreamUIInputTickFunction");
}

FName FDreamUIInputTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("DreamUIInput"));
}

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

void UDreamUIInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
}

void UDreamUIInputSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (bTornDownForWorld || !InWorld.IsGameWorld())
	{
		return;
	}
	// The world's input frame. Only a game world plays, so only a game world has one; a rig that drives a world by
	// hand calls ProcessFrame in its place.
	if (!TickFunction.IsTickFunctionRegistered() && InWorld.PersistentLevel != nullptr)
	{
		TickFunction.Subsystem = this;
		TickFunction.TickGroup = TG_PostPhysics;
		TickFunction.bCanEverTick = true;
		TickFunction.bStartWithTickEnabled = true;
		TickFunction.bTickEvenWhenPaused = true;
		TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
		TickFunction.SetTickFunctionEnable(true);
	}
	// A player for every local player there is, and for every one who joins later -- who used to get no event
	// system and no raycaster, and whose Escape cancelled player 0's drag.
	if (UGameInstance* GameInstance = InWorld.GetGameInstance())
	{
		const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
		for (int32 Index = 0; Index < LocalPlayers.Num(); ++Index)
		{
			if (LocalPlayers[Index] != nullptr)
			{
				GetOrCreateUser(Index);
			}
		}
		if (!LocalPlayerAddedHandle.IsValid())
		{
			LocalPlayerAddedHandle = GameInstance->OnLocalPlayerAddedEvent.AddUObject(this, &UDreamUIInputSubsystem::HandleLocalPlayerAdded);
			LocalPlayerRemovedHandle = GameInstance->OnLocalPlayerRemovedEvent.AddUObject(this, &UDreamUIInputSubsystem::HandleLocalPlayerRemoved);
		}
	}
}

void UDreamUIInputSubsystem::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the world had
	// no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
#if WITH_EDITOR
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->OnDrawHelperGizmo.RemoveAll(this);
	}
#endif
	Super::Deinitialize();
}

void UDreamUIInputSubsystem::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	// Every player lets go of everything while the world is still whole: every hovered widget gets its Exit, every
	// pressed one its Up, every drag its end or cancel. Dispatching any of it from the collector instead would reach
	// into a world that is already gone.
	TArray<UDreamUIInputUser*> AllUsers;
	GetUsers(AllUsers);
	for (UDreamUIInputUser* User : AllUsers)
	{
		OnUserRemoved.Broadcast(User);
		User->Shutdown();
	}
	bTornDownForWorld = true;
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	if (UGameInstance* GameInstance = InWorld.GetGameInstance())
	{
		GameInstance->OnLocalPlayerAddedEvent.Remove(LocalPlayerAddedHandle);
		GameInstance->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
	}
	LocalPlayerAddedHandle.Reset();
	LocalPlayerRemovedHandle.Reset();
	// The interaction objects this subsystem spawned are its to take away again. They are transient, so a level
	// change would not carry them anyway; destroying them here is what keeps a PIE session that starts and stops
	// repeatedly from leaving a host actor behind on every run.
	TArray<int32> Indices;
	InteractionHosts.GetKeys(Indices);
	for (const int32 Index : Indices)
	{
		DestroyCreatedInteraction(Index);
	}
	CreatedEventSystemActors.GetKeys(Indices);
	for (const int32 Index : Indices)
	{
		DestroyCreatedInteraction(Index);
	}
	InteractionHosts.Reset();
	CreatedEventSystemActors.Reset();
}

#pragma region Users
UDreamUIInputUser* UDreamUIInputSubsystem::GetUser(int32 InUserIndex) const
{
	const TObjectPtr<UDreamUIInputUser>* Found = Users.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

int32 UDreamUIInputSubsystem::GetScreenIndexForUser(int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr || !User->IsScriptUser())
	{
		return InUserIndex;
	}
	// The screen the screen UI gives the first player, by the same reckoning.
	const UWorld* World = GetWorld();
	return World != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(World->GetFirstPlayerController()) : 0;
}

UDreamUIInputUser* UDreamUIInputSubsystem::GetOrCreateUser(int32 InUserIndex)
{
	if (UDreamUIInputUser* Existing = GetUser(InUserIndex))
	{
		return Existing;
	}
	if (bTornDownForWorld || InUserIndex < 0)
	{
		return nullptr;
	}
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	const bool bHasLocalPlayer = GameInstance != nullptr && GameInstance->GetLocalPlayerByIndex(InUserIndex) != nullptr;
	UDreamUIInputUser* User = NewObject<UDreamUIInputUser>(this, NAME_None, RF_Transient);
	User->InitializeUser(InUserIndex, !bHasLocalPlayer);
	Users.Add(InUserIndex, User);
	OnUserAdded.Broadcast(User);
	return User;
}

void UDreamUIInputSubsystem::GetUsers(TArray<UDreamUIInputUser*>& OutUsers) const
{
	OutUsers.Reset();
	TArray<int32> Indices;
	Users.GetKeys(Indices);
	Indices.Sort();
	for (const int32 Index : Indices)
	{
		if (UDreamUIInputUser* User = GetUser(Index))
		{
			OutUsers.Add(User);
		}
	}
}

void UDreamUIInputSubsystem::RemoveUser(int32 InUserIndex)
{
	UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr)
	{
		return;
	}
	OnUserRemoved.Broadcast(User);
	User->Shutdown();
	Users.Remove(InUserIndex);
	ImplicitEventSystems.Remove(InUserIndex);
}

void UDreamUIInputSubsystem::ProcessFrame(float InDeltaSeconds)
{
	if (bTornDownForWorld)
	{
		return;
	}
	if (!ensureMsgf(!bInFrame, TEXT("%s: an input frame was started from inside one. Refused."), *GetName()))
	{
		return;
	}
	TGuardValue<bool> InFrame(bInFrame, true);
	// In player order, and over a copy: a handler may add or remove a player.
	TArray<UDreamUIInputUser*> AllUsers;
	GetUsers(AllUsers);
	for (UDreamUIInputUser* User : AllUsers)
	{
		if (IsValid(User))
		{
			User->ProcessFrame(InDeltaSeconds);
		}
	}
}

void UDreamUIInputSubsystem::HandleLocalPlayerAdded(ULocalPlayer* InLocalPlayer)
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (GameInstance == nullptr || InLocalPlayer == nullptr)
	{
		return;
	}
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(InLocalPlayer);
	if (Index != INDEX_NONE)
	{
		GetOrCreateUser(Index);
	}
}

void UDreamUIInputSubsystem::HandleLocalPlayerRemoved(ULocalPlayer* InLocalPlayer)
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (GameInstance == nullptr || InLocalPlayer == nullptr)
	{
		return;
	}
	// Called before the local player leaves the array, so its index is still the one its player was made under.
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(InLocalPlayer);
	if (Index == INDEX_NONE)
	{
		return;
	}
	RemoveUser(Index);
	DestroyCreatedInteraction(Index);
}

void UDreamUIInputSubsystem::DestroyCreatedInteraction(int32 InUserIndex)
{
	if (TObjectPtr<AActor>* Host = InteractionHosts.Find(InUserIndex))
	{
		if (IsValid(*Host))
		{
			(*Host)->Destroy();
		}
		InteractionHosts.Remove(InUserIndex);
	}
	if (TObjectPtr<AActor>* Created = CreatedEventSystemActors.Find(InUserIndex))
	{
		if (IsValid(*Created))
		{
			(*Created)->Destroy();
		}
		CreatedEventSystemActors.Remove(InUserIndex);
	}
}
#pragma endregion

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

	// The entry is a weak pointer, so "a key exists" and "an event system is registered" are different questions.
	// A level reload destroys the old component and leaves its stale entry behind: reporting it as a duplicate
	// meant the new level's UI was never registered and stopped responding entirely.
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
		return;
	}
	MapUserIndexToEventSystem.Add(InEventSystem->GetUserIndex(), InEventSystem);
	// The player it was placed for takes its settings, and its Blueprint events relay the player's.
	if (UDreamUIInputUser* User = GetOrCreateUser(InEventSystem->GetUserIndex()))
	{
		User->SetEventSystem(InEventSystem);
		InEventSystem->BindToUser(User);
		InEventSystem->WriteSettingsToUser(User);
	}
}

void UDreamUIInputSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;

	const int32 UserIndex = InEventSystem->GetUserIndex();
	const TWeakObjectPtr<UDreamEventSystem>* InstancePtr = MapUserIndexToEventSystem.Find(UserIndex);
	if (InstancePtr == nullptr)return;
	// Removed by identity, not by user index: an unregister arriving late -- the previous level's event system
	// destroyed after the new one has claimed the same index -- used to evict the live registration.
	UDreamEventSystem* Instance = InstancePtr->Get();
	if (Instance == InEventSystem || Instance == nullptr)
	{
		MapUserIndexToEventSystem.Remove(UserIndex);
	}
	if (Instance != InEventSystem)
	{
		return;
	}
	// The input source for this player is gone. Whatever its pointers hovered and pressed is owed its Exit and Up
	// now, not at some later frame nobody feeds -- and never from the collector. The pointers go too: nothing feeds
	// them any more, and one left behind would be traced from where it last was, re-entering what it just exited.
	if (UDreamUIInputUser* User = GetUser(UserIndex))
	{
		User->RetireAllPointers();
		if (User->GetEventSystem() == InEventSystem)
		{
			User->SetEventSystem(nullptr);
		}
	}
	InEventSystem->UnbindFromUser();
}

void UDreamUIInputSubsystem::ForgetEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;
	const int32 UserIndex = InEventSystem->GetUserIndex();
	if (const TWeakObjectPtr<UDreamEventSystem>* InstancePtr = MapUserIndexToEventSystem.Find(UserIndex))
	{
		UDreamEventSystem* Instance = InstancePtr->Get();
		if (Instance == InEventSystem || Instance == nullptr)
		{
			MapUserIndexToEventSystem.Remove(UserIndex);
		}
	}
}

UDreamEventSystem* UDreamUIInputSubsystem::GetOrCreateImplicitEventSystem(int32 InUserIndex)
{
	if (TObjectPtr<UDreamEventSystem>* Found = ImplicitEventSystems.Find(InUserIndex); Found != nullptr && IsValid(*Found))
	{
		return Found->Get();
	}
	UDreamUIInputUser* User = GetOrCreateUser(InUserIndex);
	if (User == nullptr)
	{
		return nullptr;
	}
	// Never registered as a component: a component no actor carries needs no world to live in, which is what lets a
	// Blueprint ask for a player's event system without the level having one.
	UDreamEventSystem* Implicit = NewObject<UDreamEventSystem>(this, NAME_None, RF_Transient);
	Implicit->InitializeImplicit(this, User);
	ImplicitEventSystems.Add(InUserIndex, Implicit);
	return Implicit;
}
#pragma endregion

#pragma region FocusHoverCapture
bool UDreamUIInputSubsystem::SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr || User->IsShutDown())
	{
		return false;
	}
	UDreamPointerEventData* EventData = User->GetPointerEventData(InPointerId, true);
	User->SetSelectWidget(InWidget, EventData);
	// The navigation cursor has to move with focus, or the next directional press starts from wherever focus USED
	// to be and appears to teleport.
	if (EventData != nullptr)
	{
		EventData->SetHighlightedWidgetForNavigation(InWidget);
	}
	return true;
}

bool UDreamUIInputSubsystem::HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	const UDreamPointerEventData* EventData = User != nullptr ? User->FindPointerEventData(InPointerId) : nullptr;
	return EventData != nullptr && EventData->SelectedComponent == InWidget;
}

void UDreamUIInputSubsystem::ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	if (UDreamUIInputUser* User = GetUser(InUserIndex))
	{
		UDreamPointerEventData* EventData = User->FindPointerEventData(InPointerId);
		if (EventData != nullptr && EventData->SelectedComponent == InWidget)
		{
			User->SetSelectWidget(nullptr, EventData);
		}
	}
}

bool UDreamUIInputSubsystem::HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr)
	{
		return false;
	}
	for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Entry : User->GetPointerEventDataMap())
	{
		const UDreamWidget* Focused = IsValid(Entry.Value) ? Entry.Value->SelectedComponent.Get() : nullptr;
		// Descendants, not "this or its descendants" -- UMG draws the same line, and a widget asking whether
		// something INSIDE it has focus already knows whether it has focus itself.
		if (IsValid(Focused) && Focused != InWidget && Focused->IsChildOf(InWidget))
		{
			return true;
		}
	}
	return false;
}

bool UDreamUIInputSubsystem::IsHovered(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr)
	{
		return false;
	}
	for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Entry : User->GetPointerEventDataMap())
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
		// The enter STACK as well, so a button still reads as hovered while the pointer is over its own label.
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
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr)
	{
		return false;
	}
	for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Entry : User->GetPointerEventDataMap())
	{
		if (InPointerIndex >= 0 && Entry.Key != InPointerIndex)
		{
			continue;
		}
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		// Held down AND pressed on this widget: that pointer's drag and its release go here whatever it travels
		// over in between, which is the whole of what capture buys a caller.
		if (IsValid(PointerEvent) && PointerEvent->bNowIsTriggerPressed && PointerEvent->PressWidget.Get() == InWidget)
		{
			return true;
		}
	}
	return false;
}

UDreamPointerEventData* UDreamUIInputSubsystem::FindPointer(int32 InUserIndex, int32 InPointerId) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	return User != nullptr ? User->FindPointerEventData(InPointerId) : nullptr;
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
	// Scoped to the screen the owner is inside, so the binding is live only while that screen is in front. A widget
	// with no scope above it binds globally, which is the honest reading of "there is no screen this belongs to".
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
	 * The index of the first local player -- the one a null owning player resolves to everywhere else in the
	 * plugin. Usually 0, but it is read rather than assumed.
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
	if (World == nullptr || bTornDownForWorld)return;

	// The event system for THIS player, not "the one at index 0". The registry is only half the answer: a placed
	// event system enrols itself when it begins play, so during level startup the component can exist while the
	// map does not know about it yet.
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
	TObjectPtr<AActor>* CreatedSlot = CreatedEventSystemActors.Find(InUserIndex);
	if (!bHasEventSystem && (CreatedSlot == nullptr || !IsValid(*CreatedSlot)))
	{
		const int32 FirstIndex = DreamUIInputSubsystemLocal::FirstLocalPlayerIndex(World);
		const UGameInstance* GameInstance = World->GetGameInstance();
		const bool bIsLocalPlayer = GameInstance != nullptr && GameInstance->GetLocalPlayerByIndex(InUserIndex) != nullptr;
		// Every local player gets one -- listening to that player's own controller, which is what keeps two players
		// from reading each other's input. Beyond the eight players the engine's automatic input covers, a project
		// has to place its own.
		const bool bCanListen = InUserIndex == FirstIndex
			|| (bIsLocalPlayer && InUserIndex >= 0 && InUserIndex < (int32)EAutoReceiveInput::Player7);
		if (!bCanListen)
		{
			UE_LOG(DreamGUI, Warning,
				TEXT("Player %d has DreamUI to point at but no event system with that UserIndex, so it takes no input. ")
				TEXT("Place a DreamEventSystem with UserIndex %d for that player."), InUserIndex, InUserIndex);
		}
		else if (UClass* EventSystemClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->EventSystemActorClass, TEXT("EventSystemActorClass")))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(World, EventSystemClass, *FString::Printf(TEXT("DreamEventSystem_P%d"), InUserIndex));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.bDeferConstruction = true;
			const FTransform SpawnTransform = FTransform::Identity;
			AActor* Created = World->SpawnActor<AActor>(EventSystemClass, SpawnTransform, SpawnParameters);
			if (IsValid(Created))
			{
				// A later player's controller, set before BeginPlay: AutoReceiveInput is what the preset listens with, and
				// what its event system's UserIndex is made to agree with. The first player's is the class's own.
				if (InUserIndex != FirstIndex)
				{
					Created->AutoReceiveInput = (EAutoReceiveInput::Type)(InUserIndex + 1);
				}
				if (UDreamEventSystem* CreatedEventSystem = Created->FindComponentByClass<UDreamEventSystem>())
				{
					CreatedEventSystem->SetUserIndex(InUserIndex);
				}
				Created->FinishSpawning(SpawnTransform);
				CreatedEventSystemActors.Add(InUserIndex, Created);
			}
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("Cannot create DreamUI input: Project Settings > Plugins > Dream GUI > ")
				TEXT("EventSystemActorClass is not set or failed to load."));
		}
	}

	// An authored raycaster wins. Somebody who placed a world-space raycaster on their pawn, or a screen raycaster
	// with a hand-tuned drag threshold, said what they wanted.
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
	// Asked again on the host itself, because a raycaster only enrols in the manager's raycaster list when it
	// activates, and a world that has not begun play never activates one.
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

AActor* UDreamUIInputSubsystem::GetCreatedEventSystemActor(int32 InUserIndex) const
{
	const TObjectPtr<AActor>* Found = CreatedEventSystemActors.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

void UDreamUIInputSubsystem::PrepareScreenInteraction(UDreamCanvas* InRootCanvas, int32 InUserIndex)
{
	if (!IsValid(InRootCanvas))
	{
		return;
	}
	// A screen page needs the same event system and raycaster a world-space host does. What is particular to a
	// screen is telling this player's screen raycaster which canvas it projects through.
	EnsureInteractionForPlayer(InUserIndex, EDreamInteractionKind::Screen);

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
		{
			UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Raycaster.Get());
			// Only a raycaster that speaks for THIS player; retargeting every screen raycaster at whichever root was
			// built last is what made split screen impossible.
			if (ScreenRaycaster != nullptr && ScreenRaycaster->GetUserIndex() == InUserIndex)
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
			}
		}
	}
	// The one just created is on its host actor and has not necessarily enrolled -- enrolment happens on activation,
	// which a world that has not begun play never performs.
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
