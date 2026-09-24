// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Core/DreamUISettings.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Event/DreamEnhancedInputEventSystemActor.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"

/*
 * THE STANDARD PRESET THE PLUGIN SHIPS, AS SHIPPED.
 *
 * /DreamGUI/Blueprints/DreamEventSystemActor is the legacy-key event system preset. It used to be an
 * event graph of its own on the bare ADreamEventSystemActor: every key wired by hand and consuming
 * its key, every navigation direction left at None, no D-pad, no space bar, and every binding
 * answering while the game was paused whatever the settings said -- so nothing done to the C++
 * preset reached it. It is now a data-only subclass of ADreamStandaloneInputEventSystemActor, and
 * these tests hold the asset to that: the class it derives from and nothing of its own, and -- spawned
 * for a player the way a game spawns it -- the C++ class's behaviour arriving intact: keys watched
 * rather than eaten, a click through the controller, the D-pad in the D-pad's direction, the space bar
 * as confirm, and a paused game's click following UDreamUISettings::bScreenSpaceUIAffectByGamePause.
 *
 * As for the Enhanced preset (DreamEnhancedInputPresetAssetAutomationTests.cpp), the driver's rigs
 * cannot host a Blueprint whose module is its own, so player 0 is built here the way
 * DreamDriverGameHost::Build builds one, and what is read is what the preset's handlers leave on the
 * event system's pointer.
 */
namespace DreamStandalonePresetAssetTestLocal
{
	const TCHAR* const StandalonePresetClassPath = TEXT("/DreamGUI/Blueprints/DreamEventSystemActor.DreamEventSystemActor_C");

	UClass* LoadStandalonePresetClass()
	{
		return LoadClass<AActor>(nullptr, StandalonePresetClassPath);
	}

	/** An object property by name: InputModule is protected, and which object it holds is the point. */
	UObject* ReadStandaloneObjectProperty(const UObject* InObject, const TCHAR* InProperty)
	{
		if (InObject == nullptr)
		{
			return nullptr;
		}
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InObject->GetClass(), InProperty);
		return Property != nullptr ? Property->GetObjectPropertyValue_InContainer(InObject) : nullptr;
	}

	/**
	 * APlayerController::TickPlayerInput, the controller's input frame alone. Protected, so reached the
	 * way DreamDriverGameHost reaches it: a pointer to a protected member may be formed in the scope of
	 * a class derived from its owner. Never constructed.
	 */
	struct FDreamStandalonePresetInputFrame : public APlayerController
	{
		FDreamStandalonePresetInputFrame() = delete;

		static void Run(APlayerController& InController, float InDeltaSeconds, bool bInGamePaused)
		{
			auto TickFunction = &FDreamStandalonePresetInputFrame::TickPlayerInput;
			(InController.*TickFunction)(InDeltaSeconds, bInGamePaused);
		}
	};

	/**
	 * Player 0 in a game-instance world and the shipped preset spawned for it, initialized, begun and
	 * bound (DreamDriverGameHost::Build's road). Torn down in reverse: the preset, then the local
	 * player, which takes its controller with it, then the world.
	 */
	struct FStandalonePresetHost
	{
		DreamTests::FScopedGameInstanceWorld Scope;
		ULocalPlayer* LocalPlayer = nullptr;
		APlayerController* Controller = nullptr;
		ADreamStandaloneInputEventSystemActor* Preset = nullptr;
		/** Empty once the host is up; otherwise which link failed. */
		FString WhyNot;

		explicit FStandalonePresetHost(UClass* InPresetClass)
		{
			UWorld* World = Scope.World;
			UGameInstance* GameInstance = Scope.GameInstance;
			if (World == nullptr || GameInstance == nullptr || GEngine == nullptr)
			{
				WhyNot = TEXT("there is no game-instance world to spawn the preset in");
				return;
			}
			if (InPresetClass == nullptr || !InPresetClass->IsChildOf(ADreamStandaloneInputEventSystemActor::StaticClass()))
			{
				WhyNot = FString::Printf(TEXT("%s did not load as a standalone input event system actor"), StandalonePresetClassPath);
				return;
			}
			if (GameInstance->GetNumLocalPlayers() > 0)
			{
				WhyNot = TEXT("the game instance already has a local player");
				return;
			}

			UClass* LocalPlayerClass = GEngine->LocalPlayerClass != nullptr ? GEngine->LocalPlayerClass.Get() : ULocalPlayer::StaticClass();
			LocalPlayer = NewObject<ULocalPlayer>(GEngine, LocalPlayerClass);
			if (LocalPlayer == nullptr
				|| GameInstance->AddLocalPlayer(LocalPlayer, IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser()) == INDEX_NONE)
			{
				WhyNot = TEXT("the game instance would not take a local player");
				return;
			}

			FActorSpawnParameters ControllerSpawn;
			ControllerSpawn.ObjectFlags |= RF_Transient;
			ControllerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Controller = World->SpawnActor<APlayerController>(ControllerSpawn);
			if (Controller == nullptr)
			{
				WhyNot = TEXT("the world would not spawn a player controller");
				return;
			}
			World->AddController(Controller);
			Controller->SetPlayer(LocalPlayer);
			if (Controller->PlayerInput == nullptr)
			{
				WhyNot = TEXT("the player controller has no player input");
				return;
			}

			FActorSpawnParameters PresetSpawn;
			PresetSpawn.ObjectFlags |= RF_Transient;
			PresetSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Preset = World->SpawnActor<ADreamStandaloneInputEventSystemActor>(InPresetClass, FTransform::Identity, PresetSpawn);
			if (Preset == nullptr)
			{
				WhyNot = TEXT("the world would not spawn the preset");
				return;
			}
			// What AActor::PostActorConstruction does in a world whose actors are initialized:
			// PreInitializeComponents is where AutoReceiveInput claims player 0, DispatchBeginPlay is
			// where the preset registers its module and binds its keys.
			if (!World->AreActorsInitialized() && !Preset->IsActorInitialized())
			{
				Preset->PreInitializeComponents();
				Preset->InitializeComponents();
				Preset->PostInitializeComponents();
			}
			if (!Preset->HasActorBegunPlay())
			{
				Preset->DispatchBeginPlay();
			}
			if (!Preset->HasActorBegunPlay())
			{
				WhyNot = TEXT("the preset did not begin play");
				return;
			}
			if (!IsValid(Preset->InputComponent) || !Controller->IsInputComponentInStack(Preset->InputComponent))
			{
				WhyNot = TEXT("the preset's input component is not on player 0's input stack");
			}
		}

		~FStandalonePresetHost()
		{
			if (IsValid(Preset))
			{
				Preset->Destroy();
			}
			UGameInstance* GameInstance = Scope.GameInstance;
			if (IsValid(LocalPlayer) && IsValid(GameInstance) && GameInstance->GetLocalPlayers().Contains(LocalPlayer))
			{
				GameInstance->RemoveLocalPlayer(LocalPlayer);
			}
			if (IsValid(Controller) && !Controller->IsActorBeingDestroyed())
			{
				Controller->Destroy();
			}
		}

		FStandalonePresetHost(const FStandalonePresetHost&) = delete;
		FStandalonePresetHost& operator=(const FStandalonePresetHost&) = delete;

		bool IsUp() const { return WhyNot.IsEmpty(); }

		/** A key event into the controller, where the game viewport client delivers a device's key. */
		void SendKey(const FKey& InKey, EInputEvent InEvent) const
		{
			const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, InEvent, InEvent == IE_Released ? 0.0f : 1.0f,
				/*InNumSamplesOverride*/ -1, Device));
		}

		/**
		 * One frame of input: the world's clocks moved as a frame moves them, the controller's input
		 * frame -- paused when the world is -- and the event system's tick, where the module turns a
		 * queued button into the pointer's state.
		 */
		void RunFrame() const
		{
			UWorld* World = Scope.World;
			constexpr float DeltaSeconds = 1.0f / 60.0f;
			const bool bPaused = World->IsPaused();
			World->RealTimeSeconds += DeltaSeconds;
			World->DeltaRealTimeSeconds = DeltaSeconds;
			World->DeltaTimeSeconds = DeltaSeconds;
			if (!bPaused)
			{
				World->TimeSeconds += DeltaSeconds;
			}
			FDreamStandalonePresetInputFrame::Run(*Controller, DeltaSeconds, bPaused);
			if (UActorComponent* EventSystem = Preset->GetEventSystem())
			{
				EventSystem->TickComponent(DeltaSeconds, bPaused ? LEVELTICK_PauseTick : LEVELTICK_All, nullptr);
			}
		}

		/** Player 0's pointer, which the preset's navigation and mouse both feed. */
		UDreamPointerEventData* Pointer() const
		{
			UDreamEventSystem* EventSystem = Preset != nullptr ? Preset->GetEventSystem() : nullptr;
			return EventSystem != nullptr ? EventSystem->GetPointerEventData(0, false) : nullptr;
		}
	};

	FString StandaloneHostCameUp(const FStandalonePresetHost& InHost)
	{
		return InHost.IsUp()
			? FString(TEXT("The shipped standard preset was spawned for player 0 and began play"))
			: FString::Printf(TEXT("The shipped standard preset was spawned for player 0 and began play -- it did not: %s"), *InHost.WhyNot);
	}

	/** The first binding on InInput that consumes its input or goes quiet while paused, or empty when none does. */
	FString FindBindingThatEatsOrPauses(const UInputComponent& InInput)
	{
		for (const FInputKeyBinding& Binding : InInput.KeyBindings)
		{
			if (Binding.bConsumeInput || !Binding.bExecuteWhenPaused)
			{
				return FString::Printf(TEXT("key binding %s"), *Binding.Chord.Key.ToString());
			}
		}
		for (const FInputAxisKeyBinding& Binding : InInput.AxisKeyBindings)
		{
			if (Binding.bConsumeInput || !Binding.bExecuteWhenPaused)
			{
				return FString::Printf(TEXT("axis key binding %s"), *Binding.AxisKey.ToString());
			}
		}
		for (const FInputVectorAxisBinding& Binding : InInput.VectorAxisBindings)
		{
			if (Binding.bConsumeInput || !Binding.bExecuteWhenPaused)
			{
				return FString::Printf(TEXT("vector axis binding %s"), *Binding.AxisKey.ToString());
			}
		}
		for (const FInputTouchBinding& Binding : InInput.TouchBindings)
		{
			if (Binding.bConsumeInput || !Binding.bExecuteWhenPaused)
			{
				return TEXT("a touch binding");
			}
		}
		return FString();
	}

	bool HasKeyBinding(const UInputComponent& InInput, const FKey& InKey)
	{
		for (const FInputKeyBinding& Binding : InInput.KeyBindings)
		{
			if (Binding.Chord.Key == InKey)
			{
				return true;
			}
		}
		return false;
	}
}

/**
 * The asset itself: a direct subclass of the C++ standalone preset -- not the Enhanced one -- that
 * keeps the class's defaults (player 0, keys watched rather than eaten) and brings nothing of its own:
 * no component (its own module and root would take the native ones' names and have them destroyed at
 * construction), no event graph and no input binding (which would answer every key a second time).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStandalonePresetAssetIsTheNativeActorTest,
	"DreamGUI.Input.StandalonePreset.TheShippedBlueprintIsTheNativeStandaloneActorWithNothingOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStandalonePresetAssetIsTheNativeActorTest::RunTest(const FString& Parameters)
{
	using namespace DreamStandalonePresetAssetTestLocal;

	UClass* PresetClass = LoadStandalonePresetClass();
	if (!TestNotNull(TEXT("The shipped standard preset loads"), PresetClass))
	{
		return false;
	}
	TestTrue(TEXT("It is the C++ standalone input event system actor"), PresetClass->IsChildOf(ADreamStandaloneInputEventSystemActor::StaticClass()));
	TestTrue(TEXT("...directly, with nothing in between"), PresetClass->GetSuperClass() == ADreamStandaloneInputEventSystemActor::StaticClass());
	TestFalse(TEXT("...and not the Enhanced Input one: this is the preset that works unconfigured"),
		PresetClass->IsChildOf(ADreamEnhancedInputEventSystemActor::StaticClass()));

	const ADreamStandaloneInputEventSystemActor* Defaults = Cast<ADreamStandaloneInputEventSystemActor>(PresetClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Its class default object is a standalone preset"), Defaults))
	{
		return false;
	}
	TestEqual(TEXT("It listens as player 0"), static_cast<int32>(Defaults->AutoReceiveInput.GetValue()), static_cast<int32>(EAutoReceiveInput::Player0));
	TestFalse(TEXT("It watches the keys it binds rather than eating them"), Defaults->bConsumeBoundInput);

	const UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(PresetClass);
	if (TestNotNull(TEXT("It is a Blueprint class"), Generated))
	{
		TestEqual(TEXT("It adds no component of its own"),
			Generated->SimpleConstructionScript != nullptr ? Generated->SimpleConstructionScript->GetAllNodes().Num() : 0, 0);
		TestEqual(TEXT("It binds no input of its own"), Generated->DynamicBindingObjects.Num(), 0);
		TestNull(TEXT("It has no event graph"), Generated->UberGraphFunction.Get());
	}

	// Spawned, it is exactly the native class's actor.
	DreamTests::FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world to spawn it in"), Scope.World))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	ADreamStandaloneInputEventSystemActor* Actor = Scope.World->SpawnActor<ADreamStandaloneInputEventSystemActor>(PresetClass, FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("It spawns"), Actor))
	{
		return false;
	}
	TInlineComponentArray<UDreamStandaloneInputModule*> Modules(Actor);
	TInlineComponentArray<UDreamEventSystem*> EventSystems(Actor);
	TestEqual(TEXT("It has one input module"), Modules.Num(), 1);
	TestEqual(TEXT("and one event system"), EventSystems.Num(), 1);
	if (Modules.Num() == 1)
	{
		TestSamePtr(TEXT("The module is the one the class's InputModule names"),
			ReadStandaloneObjectProperty(Actor, TEXT("InputModule")), static_cast<UObject*>(Modules[0]));
		TestEqual(TEXT("under the name the class gives it"), Modules[0]->GetName(), FString(TEXT("DreamStandaloneInputModule")));
	}
	TestSamePtr(TEXT("Its event system is the class's"), static_cast<UObject*>(Actor->GetEventSystem()),
		EventSystems.Num() == 1 ? static_cast<UObject*>(EventSystems[0]) : nullptr);
	const USceneComponent* Root = Actor->GetRootComponent();
	TestTrue(TEXT("Its root is the class's DefaultSceneRoot"), Root != nullptr && Root->GetName() == TEXT("DefaultSceneRoot"));
	// Left to the world's tear-down: destroying it by hand in a world with no world context logs a
	// warning (UWorld::DestroyActor) that says nothing about the preset.
	return true;
}

/**
 * Spawned for a player, the preset binds what the C++ class binds, the way it binds it: every binding
 * leaves its key for the pawn and the controller and answers while paused (the setting decides, per
 * key, whether a paused UI listens) -- the old graph's key events consumed theirs -- and a left click
 * through the controller comes out of its handler as the pointer's left button.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStandalonePresetAssetBindingsTest,
	"DreamGUI.Input.StandalonePreset.ForAPlayerItWatchesItsKeysWithoutEatingThemAndALeftClickReachesItsPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStandalonePresetAssetBindingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamStandalonePresetAssetTestLocal;

	FStandalonePresetHost Host(LoadStandalonePresetClass());
	if (!TestTrue(StandaloneHostCameUp(Host), Host.IsUp()))
	{
		return false;
	}
	const UInputComponent& Input = *Host.Preset->InputComponent;
	TestTrue(TEXT("It bound its keys"), Input.KeyBindings.Num() > 0);
	TestTrue(TEXT("...the left mouse button among them"), HasKeyBinding(Input, EKeys::LeftMouseButton));
	TestEqual(TEXT("No binding eats its key or goes quiet while paused"), FindBindingThatEatsOrPauses(Input), FString());

	Host.SendKey(EKeys::LeftMouseButton, IE_Pressed);
	Host.RunFrame();
	UDreamPointerEventData* Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("The left button made player 0's pointer"), Pointer))
	{
		return false;
	}
	TestTrue(TEXT("A left click through the controller presses the pointer"), Pointer->bNowIsTriggerPressed);
	TestEqual(TEXT("...with the left button"), static_cast<int32>(Pointer->MouseButtonType), static_cast<int32>(EDreamUIMouseButtonType::Left));
	Host.SendKey(EKeys::LeftMouseButton, IE_Released);
	Host.RunFrame();
	TestFalse(TEXT("and its release lets it go"), Pointer->bNowIsTriggerPressed);
	return true;
}

/**
 * Two of the fixes made to the C++ preset that the old graph never had: the D-pad is a direction key
 * (Slate's KeyEventRules pair it with the arrows), arriving as the D-pad's own direction -- the graph
 * bound neither the D-pad nor any direction but None -- and the space bar is confirm (Slate's
 * Accept), which the graph never bound either.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStandalonePresetAssetNavigationTest,
	"DreamGUI.Input.StandalonePreset.TheDPadNavigatesInItsOwnDirectionAndTheSpaceBarConfirms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStandalonePresetAssetNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamStandalonePresetAssetTestLocal;

	FStandalonePresetHost Host(LoadStandalonePresetClass());
	if (!TestTrue(StandaloneHostCameUp(Host), Host.IsUp()))
	{
		return false;
	}

	const TPair<FKey, EDreamUINavigationDirection> DPad[] = {
		{ EKeys::Gamepad_DPad_Left, EDreamUINavigationDirection::Left },
		{ EKeys::Gamepad_DPad_Down, EDreamUINavigationDirection::Down },
	};
	for (const TPair<FKey, EDreamUINavigationDirection>& Press : DPad)
	{
		Host.SendKey(Press.Key, IE_Pressed);
		Host.RunFrame();
		UDreamPointerEventData* Pointer = Host.Pointer();
		if (!TestNotNull(*FString::Printf(TEXT("%s through the controller reached the preset's navigation"), *Press.Key.ToString()), Pointer))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s navigates in its own direction"), *Press.Key.ToString()),
			static_cast<int32>(Pointer->NavigateDirection), static_cast<int32>(Press.Value));
		Host.SendKey(Press.Key, IE_Released);
		Host.RunFrame();
		TestEqual(*FString::Printf(TEXT("and releasing %s stops navigating"), *Press.Key.ToString()),
			static_cast<int32>(Pointer->NavigateDirection), static_cast<int32>(EDreamUINavigationDirection::None));
	}

	Host.SendKey(EKeys::SpaceBar, IE_Pressed);
	Host.RunFrame();
	UDreamPointerEventData* Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("The space bar reached the preset's navigation"), Pointer))
	{
		return false;
	}
	TestTrue(TEXT("The space bar is confirm: it presses navigation's trigger"), Pointer->bNowIsTriggerPressed);
	TestEqual(TEXT("...as navigation, not as a pointer"), static_cast<int32>(Pointer->InputType), static_cast<int32>(EDreamUIPointerInputType::Navigation));
	Host.SendKey(EKeys::SpaceBar, IE_Released);
	Host.RunFrame();
	TestFalse(TEXT("and its release lets the trigger go"), Pointer->bNowIsTriggerPressed);
	return true;
}

/**
 * The pause fix: every binding answers while paused and each key asks the setting at the moment it
 * arrives (ADreamStandaloneInputEventSystemActor::IsInputSuspendedByGamePause). So in a paused game a
 * click lands while bScreenSpaceUIAffectByGamePause says the UI goes on, and is dropped once it says
 * the UI pauses with the game -- the old graph's key events answered while paused whatever it said.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStandalonePresetAssetPausedClickTest,
	"DreamGUI.Input.StandalonePreset.WhileTheGameIsPausedAClickFollowsTheScreenSpacePauseSetting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStandalonePresetAssetPausedClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamStandalonePresetAssetTestLocal;

	UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
	const bool bAffectedBefore = Settings->bScreenSpaceUIAffectByGamePause;
	Settings->bScreenSpaceUIAffectByGamePause = false;
	ON_SCOPE_EXIT { Settings->bScreenSpaceUIAffectByGamePause = bAffectedBefore; };

	FStandalonePresetHost Host(LoadStandalonePresetClass());
	if (!TestTrue(StandaloneHostCameUp(Host), Host.IsUp()))
	{
		return false;
	}
	UWorld* World = Host.Scope.World;
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	APlayerState* Pauser = World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("The world has settings to pause"), WorldSettings) || !TestNotNull(TEXT("and a player state to pause it"), Pauser))
	{
		return false;
	}
	WorldSettings->SetPauserPlayerState(Pauser);
	ON_SCOPE_EXIT { WorldSettings->SetPauserPlayerState(nullptr); };
	if (!TestTrue(TEXT("The game is paused"), World->IsPaused()))
	{
		return false;
	}

	// The UI goes on while the game is paused: the click lands.
	Host.SendKey(EKeys::LeftMouseButton, IE_Pressed);
	Host.RunFrame();
	UDreamPointerEventData* Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("A paused left click reached the preset's pointer"), Pointer))
	{
		return false;
	}
	TestTrue(TEXT("With screen-space UI going on while paused, a click presses the pointer"), Pointer->bNowIsTriggerPressed);
	Host.SendKey(EKeys::LeftMouseButton, IE_Released);
	Host.RunFrame();
	TestFalse(TEXT("and its release lets it go"), Pointer->bNowIsTriggerPressed);

	// The UI pauses with the game: the same click, read against the setting as it now is, is dropped.
	Settings->bScreenSpaceUIAffectByGamePause = true;
	Host.SendKey(EKeys::LeftMouseButton, IE_Pressed);
	Host.RunFrame();
	TestFalse(TEXT("With screen-space UI pausing with the game, the same click does not press the pointer"), Pointer->bNowIsTriggerPressed);
	Host.SendKey(EKeys::LeftMouseButton, IE_Released);
	Host.RunFrame();
	TestTrue(TEXT("The game stayed paused throughout"), World->IsPaused());
	return true;
}

#endif
