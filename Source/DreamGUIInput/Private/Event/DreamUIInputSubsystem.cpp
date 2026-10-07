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
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "Event/DreamUISlateInputSource.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SViewport.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITextInputTarget.h"
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
	if (GetDefault<UDreamGUISettings>()->bUseSlateInputSource)
	{
		SetSlateInputSourceEnabled(true);
	}
	// Slate's own Tab kept out of UMG while DreamGUI has the keys, for whichever viewport client the project uses.
	BindSlateNavigationGuard(InWorld.GetGameViewport());
}

void UDreamUIInputSubsystem::SetSlateInputSourceEnabled(bool bInEnabled)
{
	if (bInEnabled == SlateInputSource.IsValid() || (bInEnabled && bTornDownForWorld))
	{
		return;
	}
	if (bInEnabled)
	{
		SlateInputSource = MakeShared<FDreamUISlateInputSource>(this);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RegisterInputPreProcessor(SlateInputSource);
		}
		return;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(SlateInputSource);
	}
	SlateInputSource.Reset();
}

void UDreamUIInputSubsystem::BindSlateNavigationGuard(UGameViewportClient* InClient)
{
	if (InClient == nullptr || bTornDownForWorld || SlateGuardClient.Get() == InClient)
	{
		return;
	}
	UnbindSlateNavigationGuard();
	// The delegate holds one binding: whatever a project bound there is kept, and asked whenever the guard lets Slate's
	// navigation through.
	PreviousNavigationOverride = InClient->OnNavigationOverride();
	InClient->OnNavigationOverride().BindUObject(this, &UDreamUIInputSubsystem::HandleSlateNavigation);
	SlateGuardClient = InClient;
}

void UDreamUIInputSubsystem::UnbindSlateNavigationGuard()
{
	// Given back only while the guard is still what is bound there: a binding made since is the project's, and stays.
	if (UGameViewportClient* Client = SlateGuardClient.Get(); Client != nullptr && Client->OnNavigationOverride().IsBoundToObject(this))
	{
		Client->OnNavigationOverride() = PreviousNavigationOverride;
	}
	PreviousNavigationOverride.Unbind();
	SlateGuardClient.Reset();
}

bool UDreamUIInputSubsystem::HandleSlateNavigation(const uint32 InSlateUserIndex, TSharedPtr<SWidget> InDestination)
{
	if (ShouldSwallowSlateNavigation(InSlateUserIndex))
	{
		return true;//handled: Slate leaves its focus where it is
	}
	return PreviousNavigationOverride.IsBound() && PreviousNavigationOverride.Execute(InSlateUserIndex, InDestination);
}

bool UDreamUIInputSubsystem::ShouldSwallowSlateNavigation(uint32 InSlateUserIndex) const
{
	if (bTornDownForWorld || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
	if (!Viewport.IsValid())
	{
		return false;
	}
	// Only from the bare viewport. A UMG widget that has the focus navigates as UMG does, among its own.
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(InSlateUserIndex);
	if (!Focused.IsValid() || Focused.Get() != static_cast<SWidget*>(Viewport.Get()))
	{
		return false;
	}
	// And only while DreamGUI has the keys: its UI up for the player that Slate user is -- something focused, or a Tab stop
	// to land on. A game with no DreamGUI menu open keeps Slate's navigation as the engine has it.
	const int32 UserIndex = FindUserIndexForSlateUser(static_cast<int32>(InSlateUserIndex));
	const UDreamUIInputUser* User = UserIndex != INDEX_NONE ? GetUser(UserIndex) : nullptr;
	if (User == nullptr || User->IsShutDown())
	{
		return false;
	}
	return IsValid(User->GetFocusedWidget()) || DreamUIKeyRouting::HasTabStops(User);
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
	// Slate stops being heard first: nothing it delivers now has a player to go to.
	SetSlateInputSourceEnabled(false);
	// And the viewport client gets back the navigation binding it had: the client outlives this world on a level change.
	UnbindSlateNavigationGuard();
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

bool UDreamUIInputSubsystem::HasPlayerAt(int32 InUserIndex) const
{
	if (InUserIndex < 0 || bTornDownForWorld)
	{
		return false;
	}
	if (GetUser(InUserIndex) != nullptr || GetEventSystemByUserIndex(InUserIndex) != nullptr)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (GameInstance != nullptr && GameInstance->GetLocalPlayerByIndex(InUserIndex) != nullptr)
	{
		return true;
	}
	// The first player, whom every world has: the one the screen UI and a null owning player resolve to, and the one a
	// world with no local players at all -- a test's, a headless rig's -- is driven as.
	return InUserIndex == (World != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(World->GetFirstPlayerController()) : 0);
}

int32 UDreamUIInputSubsystem::FindUserIndexForSlateUser(int32 InSlateUserIndex) const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (GameInstance == nullptr)
	{
		return INDEX_NONE;
	}
	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	for (int32 Index = 0; Index < LocalPlayers.Num(); ++Index)
	{
		const ULocalPlayer* LocalPlayer = LocalPlayers[Index];
		const TSharedPtr<const FSlateUser> SlateUser = LocalPlayer != nullptr ? LocalPlayer->GetSlateUser() : nullptr;
		if (SlateUser.IsValid() && SlateUser->GetUserIndex() == InSlateUserIndex)
		{
			return Index;
		}
	}
	// With one local player every Slate user is theirs: the keyboard and the mouse are Slate user 0, whoever holds the pad.
	return LocalPlayers.Num() == 1 ? 0 : INDEX_NONE;
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
	// The Slate guard, for a world whose game viewport client arrived after its play began.
	if (!SlateGuardClient.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			BindSlateNavigationGuard(World->GetGameViewport());
		}
	}
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
	// Somebody's: a Blueprint asking for player 3's event system in a one-player game is told there is none, rather than
	// handed one that speaks for a player made up on the spot.
	UDreamUIInputUser* User = HasPlayerAt(InUserIndex) ? GetOrCreateUser(InUserIndex) : nullptr;
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
namespace DreamUIInputSubsystemFocusLocal
{
	/** The pointer whose highlight is the navigation cursor: navigation is single-pointer, on the mouse's pointer. */
	constexpr int32 NavigationPointerID = 0;

	/**
	 * Whether focus may be put on InWidget the way a directional move puts it: registered with its world along its whole
	 * parent chain, active, drawn and interactable, and when it carries a selectable, that selectable interactable and
	 * navigable.
	 *
	 * Active and drawn are read from each widget's own switches up the chain, and not only from the hierarchy caches: a
	 * screen's navigation scope focuses its target from inside the walk that wakes the screen (bActivateWhenEnabled, the
	 * screen shown by an ancestor), when the caches of everything below the scope -- and every drawn cache, which a second
	 * walk settles after the first -- still say asleep. The switches are set before either walk starts, so they already
	 * say what the walks settle on, whichever way they go. Where the active cache already says awake -- a settled screen
	 * -- the drawn cache is asked too, since only it knows a page a switcher keeps out of its layout. That also refuses a
	 * widget the waking walk has passed and the second walk has not reached yet; a scope focuses below itself, where the
	 * walk has not been.
	 *
	 * Registered rather than in play, for the same kind of reason: a new screen's scope focuses from its own begin play,
	 * which runs parents first, before the controls under it have begun theirs. A widget being torn down is registered
	 * still while it ends play; the focus-return paths, which may be handed one, ask for play themselves
	 * (FDreamFocusReturn, UDreamUINavigationStack::PopScope).
	 */
	bool IsUsableForNavigationFocus(const UDreamWidget* InWidget)
	{
		if (!IsValid(InWidget))
		{
			return false;
		}
		for (const UDreamWidget* Walker = InWidget; Walker != nullptr; Walker = Walker->GetParent())
		{
			if (!IsValid(Walker) || !Walker->HasRegistered() || !Walker->GetWidgetActive() || Walker->IsParked())
			{
				return false;
			}
			const EDreamWidgetVisibility Visibility = Walker->GetVisibility();
			if (Visibility == EDreamWidgetVisibility::Hidden || Visibility == EDreamWidgetVisibility::Collapsed)
			{
				return false;
			}
		}
		if (InWidget->GetWidgetActiveInHierarchy() && !InWidget->GetRenderVisibleInHierarchy())
		{
			return false;
		}
		// The interactable walk does not follow activity, so its cache is settled whatever is waking.
		if (!InWidget->GetInteractableInHierarchy())
		{
			return false;
		}
		if (const UUISelectable* Selectable = InWidget->GetComponent<UUISelectable>())
		{
			// The selectable's own switch: its IsInteractable reads the drawn cache, which is what this function reads
			// around.
			return Selectable->GetInteractable() && Selectable->GetCanNavigateHere();
		}
		return true;
	}
}

void UDreamUIInputSubsystem::GetUserIndices(TArray<int32>& OutUserIndices) const
{
	OutUserIndices.Reset();
	for (const TPair<int32, TObjectPtr<UDreamUIInputUser>>& Entry : Users)
	{
		const UDreamUIInputUser* User = Entry.Value.Get();
		if (IsValid(User) && !User->IsShutDown())
		{
			OutUserIndices.Add(Entry.Key);
		}
	}
	OutUserIndices.Sort();
}

UDreamWidget* UDreamUIInputSubsystem::GetFocusedWidget(int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	return User != nullptr ? User->GetFocusedWidget() : nullptr;
}

bool UDreamUIInputSubsystem::FocusForNavigation(UDreamWidget* InWidget, int32 InUserIndex)
{
	using namespace DreamUIInputSubsystemFocusLocal;
	if (!IsUsableForNavigationFocus(InWidget))
	{
		return false;
	}
	// The player is made when it has none yet -- a scope pushed for player 1 before anything else asked about player 1
	// still puts their focus somewhere -- but only for an index that is somebody's (HasPlayerAt).
	UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr && HasPlayerAt(InUserIndex))
	{
		User = GetOrCreateUser(InUserIndex);
	}
	if (User == nullptr || User->IsShutDown())
	{
		return false;
	}
	UDreamPointerEventData* Navigation = User->GetPointerEventData(NavigationPointerID, true);
	if (Navigation == nullptr)
	{
		return false;
	}
	// The cursor before the selection: a select handler that moves focus on again moves the cursor with it, and the two
	// agree wherever that ends -- written after, the cursor would be left here with the focus somewhere else. Code moved
	// it, unless a navigation step is landing, which records its own cause.
	Navigation->SetHighlightedWidgetForNavigation(InWidget);
	User->SetSelectWidgetForCause(InWidget, Navigation, EDreamUIFocusCause::Script);
	return User->GetFocusedWidget() == InWidget;
}

EDreamUIFocusCause UDreamUIInputSubsystem::GetFocusCause(int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	return User != nullptr ? User->GetFocusCause() : EDreamUIFocusCause::None;
}

bool UDreamUIInputSubsystem::IsFocusVisible(int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	return User != nullptr ? User->IsFocusVisible() : !UDreamGUISettings::Get()->bFocusVisibleOnlyFromKeys;
}

UDreamWidget* UDreamUIInputSubsystem::ResolveScopeFocusTarget(int32 InUserIndex) const
{
	const UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(this);
	const UDreamUINavigationScope* Scope = Stack != nullptr ? Stack->GetActiveScope(InUserIndex) : nullptr;
	const UUISelectable* Target = Scope != nullptr ? Scope->ResolveFocusTarget() : nullptr;
	return Target != nullptr ? Target->GetWidget() : nullptr;
}

bool UDreamUIInputSubsystem::SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr || User->IsShutDown())
	{
		return false;
	}
	const TWeakObjectPtr<UDreamUIInputUser> WeakUser(User);
	const uint64 ExpectedSerial = User->GetFocusTransitionSerial() + (User->GetFocusedWidget() != InWidget ? 1 : 0);
	UDreamPointerEventData* EventData = User->GetPointerEventData(InPointerId, true);
	// Written before dispatch, as FocusForNavigation does: a nested focus or clear owns the
	// cursor it leaves behind. The callback can also retire and replace this pointer.
	if (EventData != nullptr)
	{
		EventData->SetHighlightedWidgetForNavigation(InWidget);
	}
	User->SetSelectWidgetForCause(InWidget, EventData, EDreamUIFocusCause::Script);
	// A handler can replace the pointer while this transition keeps the focus. Re-query the map
	// and only finish our own transition; a nested B -> C -> B is already somebody else's.
	if (UDreamUIInputUser* Current = GetUser(InUserIndex); Current != nullptr && Current == WeakUser.Get()
		&& !Current->IsShutDown() && Current->GetFocusTransitionSerial() == ExpectedSerial && Current->GetFocusedWidget() == InWidget)
	{
		if (UDreamPointerEventData* CurrentEventData = Current->FindPointerEventData(InPointerId))
		{
			CurrentEventData->SetHighlightedWidgetForNavigation(InWidget);
		}
	}
	return true;
}

bool UDreamUIInputSubsystem::HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const
{
	// The player's focus, whichever of their pointers is asked about: focus is the player's.
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	return User != nullptr && InWidget != nullptr && User->GetFocusedWidget() == InWidget;
}

void UDreamUIInputSubsystem::ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId)
{
	UDreamUIInputUser* User = GetUser(InUserIndex);
	if (User == nullptr || InWidget == nullptr || User->GetFocusedWidget() != InWidget)
	{
		return;
	}
	// The cursor goes with the focus it marked: left behind on a widget that was just hidden, it went on marking it for
	// everything that reads the highlight.
	auto ClearOldHighlight = [User, InWidget](int32 InId)
	{
		if (UDreamPointerEventData* Pointer = User->FindPointerEventData(InId))
		{
			if (Pointer->HighlightWidgetForNavigation.Get() == InWidget)
			{
				Pointer->HighlightWidgetForNavigation = nullptr;
			}
		}
	};
	ClearOldHighlight(DreamUIInputSubsystemFocusLocal::NavigationPointerID);
	if (InPointerId != DreamUIInputSubsystemFocusLocal::NavigationPointerID)ClearOldHighlight(InPointerId);
	User->SetSelectWidget(nullptr, User->GetPointerEventData(InPointerId, true));
}

bool UDreamUIInputSubsystem::HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	const UDreamUIInputUser* User = GetUser(InUserIndex);
	const UDreamWidget* Focused = User != nullptr ? User->GetFocusedWidget() : nullptr;
	// Descendants, not "this or its descendants" -- UMG draws the same line, and a widget asking whether
	// something INSIDE it has focus already knows whether it has focus itself.
	return IsValid(Focused) && InWidget != nullptr && Focused != InWidget && Focused->IsChildOf(InWidget);
}

bool UDreamUIInputSubsystem::HandleViewportCharacter(int32 InUserIndex, TCHAR InCharacter)
{
	if (DreamUITextInputRouter::RouteCharacter(this, InUserIndex, InCharacter))
	{
		return true;
	}
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	return Router != nullptr && Router->HandleCharacter(InUserIndex, InCharacter);
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
	EnsureInteraction(InUserIndex, InKind, nullptr);
}

void UDreamUIInputSubsystem::EnsureInteraction(int32 InUserIndex, EDreamInteractionKind InKind, UDreamCanvas* InScreenRootCanvas)
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
	FindOrMakeHostRaycaster(InUserIndex, InKind, InScreenRootCanvas);
}

UDreamBaseRaycaster* UDreamUIInputSubsystem::FindOrMakeHostRaycaster(int32 InUserIndex, EDreamInteractionKind InKind, UDreamCanvas* InScreenRootCanvas)
{
	UWorld* World = GetWorld();
	if (World == nullptr || bTornDownForWorld)return nullptr;

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
	if (!IsValid(Host))return nullptr;
	// Asked again on the host itself, because a raycaster only enrols in the manager's raycaster list when it
	// activates, and a world that has not begun play never activates one.
	for (UActorComponent* Component : Host->GetComponents())
	{
		UDreamBaseRaycaster* Existing = Cast<UDreamBaseRaycaster>(Component);
		if (Existing != nullptr && DreamUIInputSubsystemLocal::HasRaycasterOfKind(Existing, InKind))
		{
			return Existing;
		}
	}

	UDreamBaseRaycaster* NewRaycaster = nullptr;
	if (InKind == EDreamInteractionKind::Screen)
	{
		UDreamScreenSpaceRaycaster* ScreenRaycaster = NewObject<UDreamScreenSpaceRaycaster>(Host, NAME_None, RF_Transient);
		// Before RegisterComponent, which begins the raycaster in a world that has begun play: one beginning with no root
		// canvas reports that nothing set one.
		ScreenRaycaster->SetRootCanvas(InScreenRootCanvas);
		NewRaycaster = ScreenRaycaster;
	}
	else
	{
		NewRaycaster = NewObject<UDreamWorldSpaceRaycaster>(Host, NAME_None, RF_Transient);
	}
	NewRaycaster->SetUserIndex(InUserIndex);
	Host->AddInstanceComponent(NewRaycaster);
	NewRaycaster->RegisterComponent();
	return NewRaycaster;
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
	EnsureInteraction(InUserIndex, EDreamInteractionKind::Screen, InRootCanvas);

	// Which of the player's screen raycasters project through InRootCanvas: one that already does, the one this
	// subsystem made for the player, and one that projects through no overlay root at all. One projecting through an
	// overlay root of its own was put there for that canvas, and keeps it: every screen raycaster of the player used to
	// be pointed at whichever root was asked for last, so on a screen with two overlay canvases the first tooltip or
	// modal took the second canvas's raycaster away from it, and the second canvas stopped answering.
	const AActor* Host = GetInteractionHost(InUserIndex);
	bool bServed = false;
	const auto Serve = [InRootCanvas, InUserIndex, Host, &bServed](UDreamScreenSpaceRaycaster* InRaycaster)
	{
		// Only a raycaster that speaks for THIS player: retargeting every player's at whichever root was built last is
		// what made split screen impossible.
		if (InRaycaster == nullptr || InRaycaster->GetUserIndex() != InUserIndex)
		{
			return;
		}
		const UDreamCanvas* Current = InRaycaster->GetRootCanvas();
		const bool bProjectsThroughAnotherRoot = IsValid(Current) && Current != InRootCanvas && Current->IsRootCanvas()
			&& Current->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay;
		if (bProjectsThroughAnotherRoot && (Host == nullptr || InRaycaster->GetOwner() != Host))
		{
			return;
		}
		InRaycaster->SetRootCanvas(InRootCanvas);
		bServed = true;
	};
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
		{
			Serve(Cast<UDreamScreenSpaceRaycaster>(Raycaster.Get()));
		}
	}
	// The one this subsystem made is on its host actor and has not necessarily enrolled -- enrolment happens on
	// activation, which a world that has not begun play never performs.
	if (Host != nullptr)
	{
		for (UActorComponent* Component : Host->GetComponents())
		{
			Serve(Cast<UDreamScreenSpaceRaycaster>(Component));
		}
	}
	if (!bServed)
	{
		// Every screen raycaster the player has projects through an overlay canvas of its own: the root is given one of
		// its own too, beside them.
		if (UDreamScreenSpaceRaycaster* Made = Cast<UDreamScreenSpaceRaycaster>(FindOrMakeHostRaycaster(InUserIndex, EDreamInteractionKind::Screen, InRootCanvas)))
		{
			Made->SetRootCanvas(InRootCanvas);
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
