// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverGameHost.h"

#include "Components/InputComponent.h"
#include "Core/DreamUIManager.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriverInputActors.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverSequence.h"

namespace DreamDriverGameHostLocal
{
	/**
	 * APlayerController::TickPlayerInput, and nothing else of the controller's frame.
	 *
	 * The engine calls it from the controller's PlayerTick and TickActor, and both do more than input:
	 * PlayerTick sends ServerShortTimeout on its first call -- whose implementation walks the net
	 * driver's object list when the world is paused, which in a world with no net driver is a null
	 * dereference -- and updates rotation from state; TickActor is the whole actor tick. TickPlayerInput
	 * is the input frame alone: UPlayerInput::Tick, the input stack built and processed
	 * (ProcessPlayerInput), and force feedback. The last one sends Slate's input interface this
	 * player's rumble values every frame, which with nothing playing is a zero for controller 0 --
	 * exactly what every game frame sends.
	 *
	 * The function is protected, and a pointer to a protected member may be formed only in the scope
	 * of a class derived from its owner ([class.protected]); the call through the pointer is then an
	 * ordinary call. That is all this type is for: it is never constructed.
	 */
	struct FPlayerInputTickAccess : public APlayerController
	{
		FPlayerInputTickAccess() = delete;

		static void RunInputFrame(APlayerController& InController, float InDeltaSeconds, bool bInGamePaused)
		{
			auto TickFunction = &FPlayerInputTickAccess::TickPlayerInput;
			(InController.*TickFunction)(InDeltaSeconds, bInGamePaused);
		}
	};

	/** What this namespace still owes one player controller between calls. */
	struct FPlayerHostState
	{
		/** Keys TypeKey pressed, released once the controller has processed the press. */
		TArray<FKey> DueReleases;
		/** Fingers on the glass, so the cursor can be parked when the last one lifts. */
		TSet<int32> FingersDown;
		/** Under the engine pump: the one-shot hook that delivers DueReleases after the next world tick. */
		FDelegateHandle EnginePumpReleaseHandle;
	};

	/**
	 * Per controller rather than per context: the releases belong to the keyboard, and the keyboard is
	 * the controller's. Weak keys, so an entry never keeps a controller alive; entries whose controller
	 * has gone are dropped (and their hooks removed) whenever the map is touched.
	 */
	TMap<TWeakObjectPtr<APlayerController>, FPlayerHostState>& HostStates()
	{
		static TMap<TWeakObjectPtr<APlayerController>, FPlayerHostState> States;
		return States;
	}

	/**
	 * The local players and controllers Build made, as opposed to ones AttachInputActor was handed. Only
	 * these are Teardown's to remove: a PIE session's player belongs to the session.
	 */
	TSet<TWeakObjectPtr<ULocalPlayer>>& BuiltLocalPlayers()
	{
		static TSet<TWeakObjectPtr<ULocalPlayer>> LocalPlayers;
		return LocalPlayers;
	}
	TSet<TWeakObjectPtr<APlayerController>>& BuiltControllers()
	{
		static TSet<TWeakObjectPtr<APlayerController>> Controllers;
		return Controllers;
	}

	void UnhookEnginePumpRelease(FPlayerHostState& InState)
	{
		if (InState.EnginePumpReleaseHandle.IsValid())
		{
			FWorldDelegates::OnWorldPostActorTick.Remove(InState.EnginePumpReleaseHandle);
			InState.EnginePumpReleaseHandle.Reset();
		}
	}

	void PruneHostStates()
	{
		for (auto It = HostStates().CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				UnhookEnginePumpRelease(It.Value());
				It.RemoveCurrent();
			}
		}
	}

	FPlayerHostState& HostStateFor(APlayerController& InController)
	{
		PruneHostStates();
		return HostStates().FindOrAdd(TWeakObjectPtr<APlayerController>(&InController));
	}

	void ForgetHostState(APlayerController* InController)
	{
		if (InController != nullptr)
		{
			const TWeakObjectPtr<APlayerController> Key(InController);
			if (FPlayerHostState* State = HostStates().Find(Key))
			{
				UnhookEnginePumpRelease(*State);
				HostStates().Remove(Key);
			}
		}
		PruneHostStates();
	}

	/**
	 * The player's own primary input device -- the one Enhanced Input's own key injection stamps on
	 * what it injects -- so input passes UInputSettings::bFilterInputByPlatformUser in a project that
	 * turns it on, where an event from no device would be dropped as somebody else's.
	 */
	FInputDeviceId DeviceOf(const APlayerController& InController)
	{
		return IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(InController.GetPlatformUserId());
	}

	/** The controller input goes to, or null with OutWhyNot saying which part is missing. */
	APlayerController* ControllerFor(const FDreamDriverContext& InContext, FString& OutWhyNot)
	{
		APlayerController* Controller = InContext.PlayerController;
		if (!IsValid(Controller))
		{
			OutWhyNot = TEXT("the input host has no player controller");
			return nullptr;
		}
		if (Controller->PlayerInput == nullptr)
		{
			OutWhyNot = TEXT("the player controller has no PlayerInput, which SetPlayer creates -- no local player was ever set on it");
			return nullptr;
		}
		return Controller;
	}

	/**
	 * One key event into the controller, where UGameViewportClient::InputKey delivers a device's key.
	 * FInputKeyEventArgs::CreateSimulated is the engine's own constructor for input that comes from code
	 * rather than a device -- Enhanced Input's key injection and UPlayerInput::FlushPressedKeys use it.
	 * Queued in the controller's UPlayerInput; nothing is dispatched until its next input frame.
	 */
	void SendKey(APlayerController& InController, const FKey& InKey, EInputEvent InEvent)
	{
		InController.InputKey(FInputKeyEventArgs::CreateSimulated(InKey, InEvent, InEvent == IE_Released ? 0.0f : 1.0f,
			/*InNumSamplesOverride*/ -1, DeviceOf(InController)));
	}

	/** One sample of an axis, the way UGameViewportClient::InputAxis delivers one: IE_Axis, one sample. */
	void SendAxis(APlayerController& InController, const FKey& InAxisKey, float InValue, float InDeltaSeconds)
	{
		FInputKeyEventArgs AxisEvent = FInputKeyEventArgs::CreateSimulated(InAxisKey, IE_Axis, InValue,
			/*InNumSamplesOverride*/ 1, DeviceOf(InController));
		AxisEvent.DeltaTime = InDeltaSeconds;
		InController.InputKey(AxisEvent);
	}

	void DeliverDueReleases(APlayerController& InController)
	{
		FPlayerHostState* State = HostStates().Find(TWeakObjectPtr<APlayerController>(&InController));
		if (State == nullptr || State->DueReleases.Num() == 0)
		{
			return;
		}
		// Taken out first: sending input runs no game code here, but the list is emptied before
		// anything is sent so a release can never be sent twice.
		TArray<FKey> Releases = MoveTemp(State->DueReleases);
		State->DueReleases.Reset();
		for (const FKey& Key : Releases)
		{
			SendKey(InController, Key, IE_Released);
		}
	}

	/**
	 * Under the engine pump nothing calls TickPlayerInput: the engine ticks the controller in its own
	 * world tick. The releases a keystroke owes are then delivered after the next tick of the
	 * controller's world, by which point the controller's TG_PrePhysics tick has processed the press --
	 * whether the step that pressed ran before this frame's world tick or after the last one.
	 */
	void EnsureEnginePumpRelease(APlayerController& InController)
	{
		FPlayerHostState& State = HostStateFor(InController);
		if (State.EnginePumpReleaseHandle.IsValid())
		{
			return;
		}
		const TWeakObjectPtr<APlayerController> WeakController(&InController);
		const TWeakObjectPtr<UWorld> WeakWorld(InController.GetWorld());
		State.EnginePumpReleaseHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda(
			[WeakController, WeakWorld](UWorld* InTickedWorld, ELevelTick, float)
			{
				FPlayerHostState* TickState = HostStates().Find(WeakController);
				if (TickState == nullptr)
				{
					return;
				}
				APlayerController* Controller = WeakController.Get();
				if (Controller != nullptr && InTickedWorld != WeakWorld.Get())
				{
					return;//another world's tick; the editor world ticks alongside a PIE one
				}
				// One-shot: unhooked before delivering. Removing a binding from inside its own
				// broadcast is something multicast delegates allow.
				UnhookEnginePumpRelease(*TickState);
				if (Controller != nullptr)
				{
					DeliverDueReleases(*Controller);
				}
				else
				{
					HostStates().Remove(WeakController);
				}
			});
	}

	/**
	 * What AActor::PostActorConstruction does for an actor spawned into a world whose actors are
	 * initialized, done by hand for a world whose actors are not -- which is every world made with
	 * UWorld::CreateWorld and never loaded or begun, the headless rig's among them.
	 *
	 * PreInitializeComponents is where AutoReceiveInput claims player 0 (EnableInput builds the input
	 * component and pushes it on the controller's stack); PostInitializeComponents marks the actor
	 * initialized, without which AActor::RouteEndPlay would never route EndPlay on destruction; and
	 * DispatchBeginPlay is BeginPlay exactly as the engine calls it -- the components' BeginPlay first
	 * (the event system enrols with the UI manager), then the actor's (module registered, keys bound).
	 * Each part is skipped when the world already did it, so the same call is right in a PIE world,
	 * where spawning did everything.
	 */
	void BeginPlayAsTheWorldWould(AActor& InActor)
	{
		UWorld* World = InActor.GetWorld();
		if (World == nullptr)
		{
			return;
		}
		if (!World->AreActorsInitialized() && !InActor.IsActorInitialized())
		{
			InActor.PreInitializeComponents();
			InActor.InitializeComponents();
			InActor.PostInitializeComponents();
		}
		if (!InActor.HasActorBegunPlay())
		{
			InActor.DispatchBeginPlay();
		}
	}

	UDreamDriverInputModule* DriverModuleOf(const AActor* InActor)
	{
		if (const ADreamDriverStandaloneInputActor* Standalone = Cast<ADreamDriverStandaloneInputActor>(InActor))
		{
			return Standalone->GetDriverInputModule();
		}
		if (const ADreamDriverEnhancedInputActor* Enhanced = Cast<ADreamDriverEnhancedInputActor>(InActor))
		{
			return Enhanced->GetDriverInputModule();
		}
		return nullptr;
	}

	/**
	 * Whether the project is on Enhanced Input, which is what the Enhanced preset assumes rather than
	 * arranges: every input component AActor::EnableInput builds and every PlayerInput InitInputSystem
	 * builds comes from UInputSettings, and the preset binds nothing on a component that is not a
	 * UEnhancedInputComponent (it says so, at Error). A project on Enhanced Input sets both classes in
	 * DefaultInput.ini; a host that quietly substituted them would be testing a configuration no project
	 * has.
	 */
	bool ProjectIsOnEnhancedInput(FString& OutWhyNot)
	{
		const UClass* PlayerInputClass = UInputSettings::GetDefaultPlayerInputClass();
		const UClass* InputComponentClass = UInputSettings::GetDefaultInputComponentClass();
		const bool bPlayerInputIsEnhanced = PlayerInputClass != nullptr && PlayerInputClass->IsChildOf(UEnhancedPlayerInput::StaticClass());
		const bool bInputComponentIsEnhanced = InputComponentClass != nullptr && InputComponentClass->IsChildOf(UEnhancedInputComponent::StaticClass());
		if (bPlayerInputIsEnhanced && bInputComponentIsEnhanced)
		{
			return true;
		}
		OutWhyNot = FString::Printf(TEXT("the Enhanced Input actor needs a project on Enhanced Input, and this project's input settings make '%s' player inputs and '%s' input components (DefaultPlayerInputClass and DefaultInputComponentClass in DefaultInput.ini must be EnhancedPlayerInput and EnhancedInputComponent)"),
			*GetNameSafe(PlayerInputClass), *GetNameSafe(InputComponentClass));
		return false;
	}

	ETouchType::Type TouchTypeOf(EDreamDriverTouchPhase InPhase)
	{
		switch (InPhase)
		{
		case EDreamDriverTouchPhase::Began:
			return ETouchType::Began;
		case EDreamDriverTouchPhase::Moved:
			return ETouchType::Moved;
		case EDreamDriverTouchPhase::Ended:
		default:
			return ETouchType::Ended;
		}
	}
}

bool DreamDriverGameHost::Build(FDreamDriverContext& InContext, EDreamRigInputHost InHost, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	if (InHost == EDreamRigInputHost::ModuleOnly)
	{
		OutWhyNot = TEXT("ModuleOnly has no input actor to build; the rig builds its own module for it");
		return false;
	}
	UWorld* World = InContext.World;
	if (World == nullptr)
	{
		OutWhyNot = TEXT("there is no world to build the input host in");
		return false;
	}
	UGameInstance* GameInstance = InContext.GameInstance;
	if (GameInstance == nullptr)
	{
		// The whole chain hangs off this: the input actor claims player 0 through AutoReceiveInput, player
		// 0 is a local player's controller, and local players exist only on a game instance.
		OutWhyNot = TEXT("an input actor listens to player 0's controller, a controller for a player needs a local player, and a local player lives on a UGameInstance -- this world has none; build the rig with bWithGameInstance");
		return false;
	}
	if (GEngine == nullptr)
	{
		OutWhyNot = TEXT("there is no engine to make a local player with");
		return false;
	}
	if (InHost == EDreamRigInputHost::EnhancedActor && !ProjectIsOnEnhancedInput(OutWhyNot))
	{
		return false;
	}
	if (GameInstance->GetNumLocalPlayers() > 0)
	{
		OutWhyNot = FString::Printf(TEXT("the game instance already has %d local player(s); the input host builds player 0 itself and will not guess which existing one to drive"),
			GameInstance->GetNumLocalPlayers());
		return false;
	}

	// 1. The local player. Not UGameInstance::CreateLocalPlayer: with no game viewport it takes the
	// dedicated-server branch and ensure(IsDedicatedServerInstance()) fires before anything is made.
	// What it does after that check is these two calls -- a local player of the engine's class,
	// outered to the engine like every local player, then AddLocalPlayer for the primary platform user
	// (the user CreateInitialPlayer picks). AddLocalPlayer runs ULocalPlayer::PlayerAdded, which is
	// where the local player's subsystems, Enhanced Input's among them, are created.
	UClass* LocalPlayerClass = GEngine->LocalPlayerClass != nullptr ? GEngine->LocalPlayerClass.Get() : ULocalPlayer::StaticClass();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine, LocalPlayerClass);
	if (LocalPlayer == nullptr)
	{
		OutWhyNot = TEXT("the engine would not make a local player");
		return false;
	}
	BuiltLocalPlayers().Add(LocalPlayer);
	InContext.LocalPlayer = LocalPlayer;
	if (GameInstance->AddLocalPlayer(LocalPlayer, IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser()) == INDEX_NONE)
	{
		OutWhyNot = TEXT("the game instance would not take the local player");
		return false;
	}

	// 2. Its controller. A stock APlayerController: there is no game mode to name a class, and nothing
	// here depends on one.
	FActorSpawnParameters ControllerSpawn;
	ControllerSpawn.ObjectFlags |= RF_Transient;
	ControllerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerController* Controller = World->SpawnActor<APlayerController>(ControllerSpawn);
	if (Controller == nullptr)
	{
		OutWhyNot = TEXT("the world would not spawn a player controller");
		return false;
	}
	BuiltControllers().Add(Controller);
	InContext.PlayerController = Controller;
	// Spawning does not put the controller on the world's controller list here: AController's
	// PostInitializeComponents does, and a world whose actors were never initialized skips it. Off the
	// list, World->GetFirstPlayerController and UPlayer::GetPlayerController(World) -- which the event
	// system's own GetPlayerController ends in -- cannot find it, and a text field's key road
	// (UUITextInput::CheckPlayerController) finds no player. Idempotent where the world did add it.
	World->AddController(Controller);
	// What UWorld::SpawnPlayActor does once a game mode has made the controller: give it its player.
	// For a local player that is SetAsLocalPlayerController, InitInputSystem -- its PlayerInput from the
	// project's input settings, its own input component, and the AutoReceiveInput actors that were
	// waiting for a player 0 -- and ReceivedPlayer, which tells the local player's subsystems. After
	// AddController, because InitInputSystem's hand-over finds the controller's index on that list.
	Controller->SetPlayer(LocalPlayer);

	if (Controller->PlayerInput == nullptr)
	{
		OutWhyNot = TEXT("SetPlayer did not give the player controller a PlayerInput");
		return false;
	}
	if (Controller->GetLocalPlayer() != LocalPlayer)
	{
		OutWhyNot = TEXT("the player controller does not answer with the local player it was given");
		return false;
	}
	if (World->GetFirstPlayerController() != Controller)
	{
		OutWhyNot = TEXT("the world's first player controller is not the one just made, so anything that asks the world for player 0 would find another");
		return false;
	}
	UEnhancedInputLocalPlayerSubsystem* EnhancedInput = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (InHost == EDreamRigInputHost::EnhancedActor)
	{
		if (Cast<UEnhancedPlayerInput>(Controller->PlayerInput) == nullptr)
		{
			OutWhyNot = FString::Printf(TEXT("the player controller's PlayerInput is a %s, not an EnhancedPlayerInput, so no Input Action can fire"),
				*Controller->PlayerInput->GetClass()->GetName());
			return false;
		}
		if (EnhancedInput == nullptr)
		{
			OutWhyNot = TEXT("the local player has no Enhanced Input subsystem, so the preset has nowhere to add its mapping context");
			return false;
		}
	}

	// 3. The input actor, begun and bound.
	if (!AttachInputActor(InContext, Controller, InHost, OutWhyNot))
	{
		return false;
	}

	// 4. The Enhanced preset's mapping context, applied now. AddMappingContext (from the preset's
	// BeginPlay) only REQUESTS a rebuild of the player's mappings; the rebuild is made by
	// FEnhancedInputModule::Tick, an engine-level tickable that runs once per engine frame -- at the
	// end of the frame the context was added in, so a game's next input frame sees it. A synchronous
	// test has no engine frames at all, so here the rebuild is asked for with bForceImmediately, which
	// is the public way to do now what the module's tick would do before the rig's first frame.
	if (InHost == EDreamRigInputHost::EnhancedActor)
	{
		const ADreamDriverEnhancedInputActor* EnhancedActor = Cast<ADreamDriverEnhancedInputActor>(InContext.InputActor);
		const UInputMappingContext* PresetContext = EnhancedActor != nullptr ? EnhancedActor->GetDriverMappingContext() : nullptr;
		if (PresetContext == nullptr || !EnhancedInput->HasMappingContext(PresetContext))
		{
			OutWhyNot = TEXT("the Enhanced Input preset's BeginPlay did not add its mapping context to the local player (ADreamEnhancedInputEventSystemActor::AddMappingContextToLocalPlayer logs why at Warning)");
			return false;
		}
		FModifyContextOptions ApplyNow;
		ApplyNow.bForceImmediately = true;
		EnhancedInput->RequestRebuildControlMappings(ApplyNow);
	}
	return true;
}

bool DreamDriverGameHost::AttachInputActor(FDreamDriverContext& InContext, APlayerController* InController, EDreamRigInputHost InHost, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	if (InHost == EDreamRigInputHost::ModuleOnly)
	{
		OutWhyNot = TEXT("ModuleOnly has no input actor to attach");
		return false;
	}
	if (!IsValid(InController))
	{
		OutWhyNot = TEXT("there is no player controller to attach the input actor to");
		return false;
	}
	if (InController->GetLocalPlayer() == nullptr)
	{
		OutWhyNot = TEXT("the player controller has no local player, and the input actor claims a local player's input (AutoReceiveInput = Player0)");
		return false;
	}
	UWorld* World = InController->GetWorld();
	if (World == nullptr)
	{
		OutWhyNot = TEXT("the player controller is in no world");
		return false;
	}
	if (InContext.World != nullptr && InContext.World != World)
	{
		OutWhyNot = TEXT("the player controller is in a different world from the context's");
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (Manager == nullptr)
	{
		// UDreamEventSystem::BeginPlay checks for one; better said here than hit there.
		OutWhyNot = TEXT("the world has no DreamUI manager for the input actor's event system to enrol with");
		return false;
	}
	if (UDreamEventSystem* Existing = Manager->GetEventSystemByUserIndex(0))
	{
		OutWhyNot = FString::Printf(TEXT("the world already has an event system for player 0 (%s), and the UI manager refuses a second one for the same player"),
			*Existing->GetPathName());
		return false;
	}

	UClass* ActorClass = InHost == EDreamRigInputHost::EnhancedActor
		? ADreamDriverEnhancedInputActor::StaticClass()
		: ADreamDriverStandaloneInputActor::StaticClass();
	FActorSpawnParameters ActorSpawn;
	ActorSpawn.ObjectFlags |= RF_Transient;
	ActorSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Deferred, so the Enhanced actor's context and actions are in place before anything can begin it:
	// in a world that has begun play, finishing the spawn IS BeginPlay.
	ActorSpawn.bDeferConstruction = true;
	ADreamEventSystemActor* InputActor = World->SpawnActor<ADreamEventSystemActor>(ActorClass, FTransform::Identity, ActorSpawn);
	if (InputActor == nullptr)
	{
		OutWhyNot = FString::Printf(TEXT("the world would not spawn a %s"), *ActorClass->GetName());
		return false;
	}
	InContext.InputActor = InputActor;
	if (ADreamDriverEnhancedInputActor* EnhancedActor = Cast<ADreamDriverEnhancedInputActor>(InputActor))
	{
		EnhancedActor->InstallTransientMappings();
	}
	InputActor->FinishSpawning(FTransform::Identity);
	BeginPlayAsTheWorldWould(*InputActor);

	UDreamDriverInputModule* Module = DriverModuleOf(InputActor);
	if (!IsValid(Module))
	{
		OutWhyNot = TEXT("the input actor's module is not a UDreamDriverInputModule: the subobject override in DreamDriverInputActors.cpp matched nothing -- has ADreamStandaloneInputEventSystemActor renamed its module subobject?");
		return false;
	}
	UDreamEventSystem* EventSystem = InputActor->GetEventSystem();
	if (!IsValid(EventSystem))
	{
		OutWhyNot = TEXT("the input actor has no event system");
		return false;
	}
	if (!InputActor->HasActorBegunPlay())
	{
		OutWhyNot = TEXT("the input actor did not begin play, so it registered no module and bound no key");
		return false;
	}
	if (!IsValid(InputActor->InputComponent) || !InController->IsInputComponentInStack(InputActor->InputComponent))
	{
		OutWhyNot = TEXT("the input actor's input component is not on player 0's input stack: AutoReceiveInput found no player controller for player 0 (UGameplayStatics::GetPlayerController), so nothing it binds would ever fire");
		return false;
	}
	if (EventSystem->GetCurrentInputModule() != Module)
	{
		OutWhyNot = TEXT("the input actor's BeginPlay did not register its module with its event system");
		return false;
	}

	InContext.EventSystem = EventSystem;
	InContext.InputModule = Module;
	InContext.InputHost = InHost;
	return true;
}

void DreamDriverGameHost::TickPlayerInput(FDreamDriverContext& InContext, float InDeltaSeconds)
{
	using namespace DreamDriverGameHostLocal;

	if (InContext.bEnginePumped)
	{
		return;//the engine's own frame ticks the controller; a second input frame here would process everything twice
	}
	APlayerController* Controller = InContext.PlayerController;
	if (!IsValid(Controller) || Controller->PlayerInput == nullptr)
	{
		return;
	}
	UWorld* World = Controller->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// As UWorld::Tick hands it over: a paused world ticks its controllers (they tick even when paused)
	// with LEVELTICK_PauseTick, and APlayerController::TickActor then runs TickPlayerInput with
	// bGamePaused true -- the flag every legacy binding's bExecuteWhenPaused, and every Input Action's
	// bTriggerWhenPaused, is weighed against.
	const bool bGamePaused = World->IsPaused();
	FPlayerInputTickAccess::RunInputFrame(*Controller, InDeltaSeconds, bGamePaused);

	// The input actor's own tick, when it has one: the Enhanced preset polls the pointer position there,
	// because Enhanced Input has no absolute-position axis. In a game it shares TG_PrePhysics with the
	// controller, both ahead of the event system's TG_DuringPhysics tick -- the pump's next step. Held to
	// the engine's gates: a tick function that never ticks, is disabled, or does not tick while paused.
	AActor* InputActor = InContext.InputActor;
	if (IsValid(InputActor) && InputActor->PrimaryActorTick.bCanEverTick && InputActor->IsActorTickEnabled()
		&& (!bGamePaused || InputActor->PrimaryActorTick.bTickEvenWhenPaused))
	{
		InputActor->TickActor(InDeltaSeconds, bGamePaused ? LEVELTICK_PauseTick : LEVELTICK_All, InputActor->PrimaryActorTick);
	}

	// The releases TypeKey owes: the presses were processed just now, so the releases are queued for
	// the next input frame -- never the same one (see the header).
	DeliverDueReleases(*Controller);
}

void DreamDriverGameHost::Teardown(FDreamDriverContext& InContext)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = InContext.PlayerController;
	ULocalPlayer* LocalPlayer = InContext.LocalPlayer;
	// Whatever was still owed to the controller -- a release, a finger it believes is down, a hook on
	// the world tick -- goes with it.
	ForgetHostState(Controller);

	if (AActor* InputActor = InContext.InputActor; IsValid(InputActor))
	{
		// Before the local player: the Enhanced preset's EndPlay takes its mapping context off the local
		// player, found through its event system's controller, and removing the player first would leave
		// the context behind -- the "outlives the level" leak that EndPlay exists to stop. Destroy routes
		// EndPlay because the actor was initialized and begun; the event system's EndPlay takes it out of
		// the UI manager.
		InputActor->Destroy();
	}
	InContext.InputActor = nullptr;
	InContext.EventSystem = nullptr;
	InContext.InputModule = nullptr;

	const bool bLocalPlayerBuiltHere = LocalPlayer != nullptr && BuiltLocalPlayers().Contains(LocalPlayer);
	const bool bControllerBuiltHere = Controller != nullptr && BuiltControllers().Contains(Controller);
	UGameInstance* GameInstance = InContext.GameInstance;
	if (bLocalPlayerBuiltHere && IsValid(LocalPlayer) && IsValid(GameInstance) && GameInstance->GetLocalPlayers().Contains(LocalPlayer))
	{
		// The engine's own removal: the player's controller destroyed (it is the authority here), its
		// subsystems deinitialized (ULocalPlayer::PlayerRemoved), and the player off the game instance --
		// where it would outlive the world, since it belongs to the game instance and is outered to the
		// engine.
		GameInstance->RemoveLocalPlayer(LocalPlayer);
	}
	// A controller the local player never took -- a build that failed between the two -- or one the
	// removal above did not destroy.
	if (bControllerBuiltHere && IsValid(Controller) && !Controller->IsActorBeingDestroyed())
	{
		Controller->Destroy();
	}
	if (LocalPlayer != nullptr)
	{
		BuiltLocalPlayers().Remove(LocalPlayer);
	}
	if (Controller != nullptr)
	{
		BuiltControllers().Remove(Controller);
	}
	// Only what this namespace made is forgotten from the context; a PIE session's player and
	// controller stay the caller's.
	if (bControllerBuiltHere)
	{
		InContext.PlayerController = nullptr;
	}
	if (bLocalPlayerBuiltHere)
	{
		InContext.LocalPlayer = nullptr;
	}
}

bool DreamDriverGameHost::PressMouseButton(FDreamDriverContext& InContext, EDreamUIMouseButtonType InButton, bool bInPressed, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	// The preset's MouseButtons table; the Enhanced preset's context maps the same three keys.
	FKey ButtonKey;
	switch (InButton)
	{
	case EDreamUIMouseButtonType::Left:
		ButtonKey = EKeys::LeftMouseButton;
		break;
	case EDreamUIMouseButtonType::Right:
		ButtonKey = EKeys::RightMouseButton;
		break;
	case EDreamUIMouseButtonType::Middle:
		ButtonKey = EKeys::MiddleMouseButton;
		break;
	default:
		OutWhyNot = FString::Printf(TEXT("button %d is not one a mouse key produces: the input actors bind Left, Right and Middle, and the user-defined buttons are only reachable through the module"),
			static_cast<int32>(InButton));
		return false;
	}
	SendKey(*Controller, ButtonKey, bInPressed ? IE_Pressed : IE_Released);
	return true;
}

bool DreamDriverGameHost::Scroll(FDreamDriverContext& InContext, const FVector2D& InAxisValue, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	// A wheel has one axis, and both presets forward it as (v, v) -- InputScroll's own instruction for
	// a mouse wheel. So the wheel value is Y, or X when only X was given; two different non-zero
	// components are something no wheel produces, and quietly dropping one would scroll a
	// two-axis container in a way the test did not ask for.
	double WheelValue = 0.0;
	if (FMath::IsNearlyZero(InAxisValue.X) || FMath::IsNearlyEqual(InAxisValue.X, InAxisValue.Y))
	{
		WheelValue = InAxisValue.Y;
	}
	else if (FMath::IsNearlyZero(InAxisValue.Y))
	{
		WheelValue = InAxisValue.X;
	}
	else
	{
		OutWhyNot = FString::Printf(TEXT("a mouse wheel has one axis and %s asks for two different values; the input actors deliver a wheel as (v, v)"),
			*InAxisValue.ToString());
		return false;
	}
	if (FMath::IsNearlyZero(WheelValue))
	{
		return true;//a wheel at rest sends nothing
	}
	// What FSceneViewport::OnMouseWheel sends for one wheel event: the notch as a key pressed and
	// released (MouseScrollDown for a negative delta), then the axis. The input actors act on the axis;
	// the key is what the AnyKey router, and anything a project binds to the wheel keys, sees.
	const FKey NotchKey = WheelValue < 0.0 ? EKeys::MouseScrollDown : EKeys::MouseScrollUp;
	SendKey(*Controller, NotchKey, IE_Pressed);
	SendKey(*Controller, NotchKey, IE_Released);
	SendAxis(*Controller, EKeys::MouseWheelAxis, static_cast<float>(WheelValue), InContext.FrameSeconds);
	return true;
}

bool DreamDriverGameHost::Navigate(FDreamDriverContext& InContext, EDreamUINavigationDirection InDirection, bool bInPressed, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	// The preset's NavigationDirectionKeys: the left stick for the four directions (see the header for
	// why not the arrows), Tab for Next, and Tab with shift held for Prev -- which the preset resolves
	// from the live shift state on UPlayerInput when Tab arrives.
	FKey DirectionKey;
	bool bWithShift = false;
	switch (InDirection)
	{
	case EDreamUINavigationDirection::Left:
		DirectionKey = EKeys::Gamepad_LeftStick_Left;
		break;
	case EDreamUINavigationDirection::Right:
		DirectionKey = EKeys::Gamepad_LeftStick_Right;
		break;
	case EDreamUINavigationDirection::Up:
		DirectionKey = EKeys::Gamepad_LeftStick_Up;
		break;
	case EDreamUINavigationDirection::Down:
		DirectionKey = EKeys::Gamepad_LeftStick_Down;
		break;
	case EDreamUINavigationDirection::Next:
		DirectionKey = EKeys::Tab;
		break;
	case EDreamUINavigationDirection::Prev:
		DirectionKey = EKeys::Tab;
		bWithShift = true;
		break;
	default:
		OutWhyNot = TEXT("None is not a direction any key presses");
		return false;
	}
	if (bInPressed)
	{
		// Shift goes down in the same input frame as Tab: a key pressed in a frame counts as down for
		// every binding dispatched in it, so the preset sees shift held when Tab's binding asks.
		if (bWithShift)
		{
			SendKey(*Controller, EKeys::LeftShift, IE_Pressed);
		}
		SendKey(*Controller, DirectionKey, IE_Pressed);
	}
	else
	{
		SendKey(*Controller, DirectionKey, IE_Released);
		if (bWithShift)
		{
			SendKey(*Controller, EKeys::LeftShift, IE_Released);
		}
	}
	return true;
}

bool DreamDriverGameHost::NavigationTrigger(FDreamDriverContext& InContext, bool bInPressed, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	// The pad's accept button from the preset's NavigationTriggerKeys. Enter is the other one, and it is
	// also a text field's submit key; see the header.
	SendKey(*Controller, EKeys::Gamepad_FaceButton_Bottom, bInPressed ? IE_Pressed : IE_Released);
	return true;
}

bool DreamDriverGameHost::TypeCharacter(FDreamDriverContext& InContext, TCHAR InCharacter, FString& OutWhyNot)
{
	UUITextInput* ActiveInput = UUITextInput::GetActiveTextInput();
	if (ActiveInput == nullptr)
	{
		OutWhyNot = TEXT("no text field is being edited, so a character has nowhere to go: the game's road hands it to whichever field owns the keyboard (UUITextInput::RouteCharacterInputToActiveInput) and none does");
		return false;
	}
	if (InContext.World != nullptr && ActiveInput->GetWorld() != InContext.World)
	{
		// The road is process-wide, so a field some other world left mid-edit would take this rig's
		// characters. That is a leak to report, not a keyboard to type into.
		OutWhyNot = FString::Printf(TEXT("the field that owns the keyboard (%s) belongs to another world"), *ActiveInput->GetPathName());
		return false;
	}
	// The answer is dropped, as the module's road drops it: a refused character -- read-only, full, a
	// letter in a number field -- is the field deciding.
	UUITextInput::RouteCharacterInputToActiveInput(InCharacter);
	return true;
}

bool DreamDriverGameHost::TypeKey(FDreamDriverContext& InContext, const FKey& InKey, const FKey& InModifier, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	if (!InKey.IsValid())
	{
		OutWhyNot = TEXT("the key to type is not a valid key");
		return false;
	}
	if (InKey.IsAnalog())
	{
		OutWhyNot = FString::Printf(TEXT("%s is an axis, and an axis has no press to type; ScrollBy turns the wheel"), *InKey.ToString());
		return false;
	}
	const bool bWithModifier = InModifier.IsValid();
	if (bWithModifier && !InModifier.IsModifierKey())
	{
		OutWhyNot = FString::Printf(TEXT("%s is not a modifier key, so it cannot be held with %s"), *InModifier.ToString(), *InKey.ToString());
		return false;
	}

	// Pressed now, both in the same input frame: a key pressed in a frame is down for every binding
	// dispatched in it (UPlayerInput::ProcessNonAxesKeys), so a text field asking PlayerInput whether
	// shift is held while the letter's binding runs is told yes.
	if (bWithModifier)
	{
		SendKey(*Controller, InModifier, IE_Pressed);
	}
	SendKey(*Controller, InKey, IE_Pressed);

	// Released after the frame that processes the presses -- the key first, then the modifier it was
	// held with, which is the order fingers leave the keys in.
	FPlayerHostState& State = HostStateFor(*Controller);
	State.DueReleases.Add(InKey);
	if (bWithModifier)
	{
		State.DueReleases.Add(InModifier);
	}
	if (InContext.bEnginePumped)
	{
		EnsureEnginePumpRelease(*Controller);
	}
	return true;
}

bool DreamDriverGameHost::Touch(FDreamDriverContext& InContext, EDreamDriverTouchPhase InPhase, int32 InFingerId, const FVector2D& InPixel, FString& OutWhyNot)
{
	using namespace DreamDriverGameHostLocal;

	APlayerController* Controller = ControllerFor(InContext, OutWhyNot);
	if (Controller == nullptr)
	{
		return false;
	}
	const int32 TouchKeyCount = EKeys::NUM_TOUCH_KEYS;
	if (InFingerId < 0 || InFingerId >= TouchKeyCount)
	{
		OutWhyNot = FString::Printf(TEXT("finger %d is outside the %d touch keys a player has"), InFingerId, TouchKeyCount);
		return false;
	}

	FPlayerHostState& State = HostStateFor(*Controller);
	UDreamDriverInputModule* Module = InContext.InputModule;
	const ETouchType::Type TouchType = TouchTypeOf(InPhase);

	// The viewport's cursor follows every touch: FSceneViewport::OnTouchStarted, OnTouchMoved and
	// OnTouchEnded each call UpdateCachedCursorPos, and GetMousePos answers with that cache. The preset
	// re-reads the "mouse" every frame (its Mouse2D binding, the Enhanced preset's Tick) and writes it
	// into pointer 0, so on a real touch screen that pointer follows the finger. The driver module's
	// cursor stands in for the viewport's; without moving it the preset would drag pointer 0 back to
	// wherever the last mouse step left it, in the middle of a touch.
	if (IsValid(Module))
	{
		Module->MoveTo(InPixel);
	}
	// As FSceneViewport sends it: the touch's device and finger, the viewport pixel, full force while
	// down and none on the way up.
	Controller->InputTouch(FTouchId(DeviceOf(*Controller), static_cast<ETouchIndex::Type>(InFingerId)),
		TouchType, InPixel, TouchType == ETouchType::Ended ? 0.0f : 1.0f, FPlatformTime::Cycles64());

	if (TouchType == ETouchType::Began)
	{
		State.FingersDown.Add(InFingerId);
	}
	else if (TouchType == ETouchType::Ended)
	{
		State.FingersDown.Remove(InFingerId);
		// FSceneViewport::OnTouchEnded: when the last finger lifts, the cached cursor becomes (-1, -1).
		if (State.FingersDown.Num() == 0 && IsValid(Module))
		{
			Module->MoveTo(FVector2D(-1.0, -1.0));
		}
	}
	return true;
}
