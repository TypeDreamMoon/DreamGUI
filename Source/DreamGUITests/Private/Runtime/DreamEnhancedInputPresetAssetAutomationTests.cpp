// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Event/DreamEnhancedInputEventSystemActor.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"

/*
 * THE ENHANCED INPUT PRESET THE PLUGIN SHIPS, AS SHIPPED.
 *
 * /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput is what a project on Enhanced Input points
 * Project Settings > Dream GUI > EventSystemActorClass at. It used to be an event graph of its own on
 * top of the bare ADreamEventSystemActor -- every key wired by hand, directions all left at None, no
 * D-pad, no space bar, and the shipped actions' pause gate -- so nothing done to the C++ presets ever
 * reached it. It is now a data-only subclass of ADreamEnhancedInputEventSystemActor that fills in the
 * context and the four actions, and these tests hold the asset to that: the class it derives from, the
 * five assets on its defaults and nothing else of its own, and -- spawned for a player the way a game
 * spawns it -- the C++ class's behaviour arriving intact: the shipped context and actions on the
 * player, a click through the controller, the D-pad in the D-pad's direction, the space bar as
 * confirm, and a click that still lands while the game is paused.
 *
 * The driver's rigs cannot host this class: they swap their own input module into the C++ presets
 * (DreamDriverInputActors.h), and a Blueprint's native module cannot be swapped. So the player is
 * built here the way DreamDriverGameHost::Build builds one, and what is read is what the preset's
 * handlers leave on the event system's pointer -- no widgets are needed to see that a key arrived.
 */
namespace DreamEnhancedPresetAssetTestLocal
{
	const TCHAR* const PresetClassPath = TEXT("/DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput.DreamEventSystemActor_EnhancedInput_C");

	/** The five properties the preset fills, and the asset each must hold. */
	struct FShippedAsset
	{
		const TCHAR* Property;
		const TCHAR* Path;
	};
	const FShippedAsset ShippedAssets[] = {
		{ TEXT("MappingContext"), TEXT("/DreamGUI/EnhancedInput/IMC_DreamUIInputContext.IMC_DreamUIInputContext") },
		{ TEXT("TriggerLeftAction"), TEXT("/DreamGUI/EnhancedInput/IA_Trigger.IA_Trigger") },
		{ TEXT("TriggerRightAction"), TEXT("/DreamGUI/EnhancedInput/IA_TriggerRight.IA_TriggerRight") },
		{ TEXT("TriggerMiddleAction"), TEXT("/DreamGUI/EnhancedInput/IA_TriggerMiddle.IA_TriggerMiddle") },
		{ TEXT("MouseWheelAction"), TEXT("/DreamGUI/EnhancedInput/IA_MouseWheel.IA_MouseWheel") },
	};

	UClass* LoadPresetClass()
	{
		return LoadClass<AActor>(nullptr, PresetClassPath);
	}

	/** An object property of the preset by name: the five are protected, and what they hold is the point. */
	UObject* ReadObjectProperty(const UObject* InObject, const TCHAR* InProperty)
	{
		if (InObject == nullptr)
		{
			return nullptr;
		}
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InObject->GetClass(), InProperty);
		return Property != nullptr ? Property->GetObjectPropertyValue_InContainer(InObject) : nullptr;
	}

	/**
	 * APlayerController::TickPlayerInput, the controller's input frame and nothing else of its frame.
	 * Protected, so reached the way DreamDriverGameHost reaches it: a pointer to a protected member may
	 * be formed in the scope of a class derived from its owner. Never constructed.
	 */
	struct FDreamEnhancedPresetInputFrame : public APlayerController
	{
		FDreamEnhancedPresetInputFrame() = delete;

		static void Run(APlayerController& InController, float InDeltaSeconds, bool bInGamePaused)
		{
			auto TickFunction = &FDreamEnhancedPresetInputFrame::TickPlayerInput;
			(InController.*TickFunction)(InDeltaSeconds, bInGamePaused);
		}
	};

	/**
	 * Player 0 in a game-instance world, and the shipped preset spawned for it, initialized, begun and
	 * bound -- DreamDriverGameHost::Build's road (see there for why each step is the one it is), for a
	 * class whose module is its own. Torn down in reverse: the preset first, because its EndPlay takes
	 * its context off the local player; then the local player, which takes its controller with it; the
	 * world last.
	 */
	struct FPresetHost
	{
		DreamTests::FScopedGameInstanceWorld Scope;
		ULocalPlayer* LocalPlayer = nullptr;
		APlayerController* Controller = nullptr;
		ADreamEnhancedInputEventSystemActor* Preset = nullptr;
		UEnhancedInputLocalPlayerSubsystem* EnhancedInput = nullptr;
		/** Empty once the host is up; otherwise which link failed. */
		FString WhyNot;

		explicit FPresetHost(UClass* InPresetClass)
		{
			UWorld* World = Scope.World;
			UGameInstance* GameInstance = Scope.GameInstance;
			if (World == nullptr || GameInstance == nullptr || GEngine == nullptr)
			{
				WhyNot = TEXT("there is no game-instance world to spawn the preset in");
				return;
			}
			if (InPresetClass == nullptr || !InPresetClass->IsChildOf(ADreamEnhancedInputEventSystemActor::StaticClass()))
			{
				WhyNot = FString::Printf(TEXT("%s did not load as an Enhanced Input event system actor"), PresetClassPath);
				return;
			}
			const UClass* PlayerInputClass = UInputSettings::GetDefaultPlayerInputClass();
			const UClass* InputComponentClass = UInputSettings::GetDefaultInputComponentClass();
			if (PlayerInputClass == nullptr || !PlayerInputClass->IsChildOf(UEnhancedPlayerInput::StaticClass())
				|| InputComponentClass == nullptr || !InputComponentClass->IsChildOf(UEnhancedInputComponent::StaticClass()))
			{
				WhyNot = TEXT("the project is not on Enhanced Input (DefaultPlayerInputClass and DefaultInputComponentClass in DefaultInput.ini), which is what this preset is for");
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
			if (Cast<UEnhancedPlayerInput>(Controller->PlayerInput) == nullptr)
			{
				WhyNot = TEXT("the player controller has no Enhanced player input");
				return;
			}
			EnhancedInput = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			if (EnhancedInput == nullptr)
			{
				WhyNot = TEXT("the local player has no Enhanced Input subsystem");
				return;
			}

			FActorSpawnParameters PresetSpawn;
			PresetSpawn.ObjectFlags |= RF_Transient;
			PresetSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Preset = World->SpawnActor<ADreamEnhancedInputEventSystemActor>(InPresetClass, FTransform::Identity, PresetSpawn);
			if (Preset == nullptr)
			{
				WhyNot = TEXT("the world would not spawn the preset");
				return;
			}
			// What AActor::PostActorConstruction does in a world whose actors are initialized:
			// PreInitializeComponents is where AutoReceiveInput claims player 0, DispatchBeginPlay is
			// where the preset binds its actions and pushes its context.
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
				return;
			}
			// AddMappingContext only requests the rebuild that FEnhancedInputModule's tick would make at
			// the end of the frame; a test has no engine frames, so the rebuild is asked for now.
			FModifyContextOptions ApplyNow;
			ApplyNow.bForceImmediately = true;
			EnhancedInput->RequestRebuildControlMappings(ApplyNow);
		}

		~FPresetHost()
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

		FPresetHost(const FPresetHost&) = delete;
		FPresetHost& operator=(const FPresetHost&) = delete;

		bool IsUp() const { return WhyNot.IsEmpty(); }

		/** A key event into the controller, where the game viewport client delivers a device's key. */
		void SendKey(const FKey& InKey, EInputEvent InEvent) const
		{
			const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, InEvent, InEvent == IE_Released ? 0.0f : 1.0f,
				/*InNumSamplesOverride*/ -1, Device));
		}

		/**
		 * One frame of input: the world's clocks moved as a frame moves them (the preset tells one
		 * frame from the next by the world's real time), the controller's input frame -- paused when the
		 * world is, as UWorld::Tick hands it over -- and the event system's tick, which is where the
		 * module turns a queued button into the pointer's state.
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
			FDreamEnhancedPresetInputFrame::Run(*Controller, DeltaSeconds, bPaused);
			// The world's input frame, which ticks while paused as the engine's does.
			if (UDreamUIInputSubsystem* InputSubsystem = UDreamUIInputSubsystem::Get(World))
			{
				InputSubsystem->ProcessFrame(DeltaSeconds);
			}
		}

		/** Player 0's pointer, which the preset's navigation and mouse both feed. */
		UDreamPointerEventData* Pointer() const
		{
			UDreamEventSystem* EventSystem = Preset != nullptr ? Preset->GetEventSystem() : nullptr;
			return EventSystem != nullptr ? EventSystem->GetPointerEventData(0, false) : nullptr;
		}
	};

	FString HostCameUp(const FPresetHost& InHost)
	{
		return InHost.IsUp()
			? FString(TEXT("The shipped preset was spawned for player 0 and began play"))
			: FString::Printf(TEXT("The shipped preset was spawned for player 0 and began play -- it did not: %s"), *InHost.WhyNot);
	}

	bool PlayerMaps(const UEnhancedInputLocalPlayerSubsystem& InEnhancedInput, const FKey& InKey, const UInputAction* InAction)
	{
		const UEnhancedPlayerInput* PlayerInput = InEnhancedInput.GetPlayerInput();
		if (PlayerInput == nullptr)
		{
			return false;
		}
		for (const FEnhancedActionKeyMapping& Mapping : PlayerInput->GetEnhancedActionMappingsView())
		{
			if (Mapping.Key == InKey && Mapping.Action == InAction)
			{
				return true;
			}
		}
		return false;
	}
}

/**
 * The asset itself: a direct subclass of the C++ Enhanced Input preset whose defaults hold the shipped
 * context and actions, and which brings nothing of its own -- no component (its own module and root
 * would take the native ones' names and have them destroyed at construction), no event graph and no
 * input binding (which would answer every key a second time beside the class's).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEnhancedPresetAssetIsTheNativeActorTest,
	"DreamGUI.Input.EnhancedPreset.TheShippedBlueprintIsTheNativeActorWithTheShippedContextAndActionsAndNothingOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEnhancedPresetAssetIsTheNativeActorTest::RunTest(const FString& Parameters)
{
	using namespace DreamEnhancedPresetAssetTestLocal;

	UClass* PresetClass = LoadPresetClass();
	if (!TestNotNull(TEXT("The shipped Enhanced Input preset loads"), PresetClass))
	{
		return false;
	}
	TestTrue(TEXT("It is the C++ Enhanced Input event system actor"), PresetClass->IsChildOf(ADreamEnhancedInputEventSystemActor::StaticClass()));
	TestTrue(TEXT("...directly, with nothing in between"), PresetClass->GetSuperClass() == ADreamEnhancedInputEventSystemActor::StaticClass());

	const UObject* Defaults = PresetClass->GetDefaultObject();
	for (const FShippedAsset& Shipped : ShippedAssets)
	{
		const UObject* Held = ReadObjectProperty(Defaults, Shipped.Property);
		TestEqual(*FString::Printf(TEXT("Its %s is the shipped asset"), Shipped.Property),
			Held != nullptr ? Held->GetPathName() : FString(TEXT("(none)")), FString(Shipped.Path));
	}
	const FIntProperty* Priority = FindFProperty<FIntProperty>(PresetClass, TEXT("MappingContextPriority"));
	if (TestNotNull(TEXT("The context's priority is a property of the class"), Priority))
	{
		TestEqual(TEXT("and the preset pushes its context at Enhanced Input's default priority"), Priority->GetPropertyValue_InContainer(Defaults), 0);
	}

	// The shipped context maps the three buttons and the wheel to exactly the actions on the defaults,
	// or the preset would bind actions no key reaches.
	const UInputMappingContext* Context = Cast<UInputMappingContext>(ReadObjectProperty(Defaults, TEXT("MappingContext")));
	if (TestNotNull(TEXT("The context loads"), Context))
	{
		const TPair<FKey, const TCHAR*> ExpectedMappings[] = {
			{ EKeys::LeftMouseButton, TEXT("TriggerLeftAction") },
			{ EKeys::RightMouseButton, TEXT("TriggerRightAction") },
			{ EKeys::MiddleMouseButton, TEXT("TriggerMiddleAction") },
			{ EKeys::MouseWheelAxis, TEXT("MouseWheelAction") },
		};
		for (const TPair<FKey, const TCHAR*>& Expected : ExpectedMappings)
		{
			const UObject* Action = ReadObjectProperty(Defaults, Expected.Value);
			bool bMapped = false;
			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				bMapped |= Mapping.Key == Expected.Key && Mapping.Action == Action;
			}
			TestTrue(*FString::Printf(TEXT("The context maps %s to the preset's %s"), *Expected.Key.ToString(), Expected.Value), bMapped);
		}
	}

	const UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(PresetClass);
	if (TestNotNull(TEXT("It is a Blueprint class"), Generated))
	{
		TestEqual(TEXT("It adds no component of its own"),
			Generated->SimpleConstructionScript != nullptr ? Generated->SimpleConstructionScript->GetAllNodes().Num() : 0, 0);
		TestEqual(TEXT("It binds no input of its own"), Generated->DynamicBindingObjects.Num(), 0);
		TestNull(TEXT("It has no event graph"), Generated->UberGraphFunction.Get());
	}

	// Spawned, it is exactly the native class's actor: one module, the one the class's InputModule
	// names and by the name the class gives it, one event system, and the class's root.
	DreamTests::FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("A world to spawn it in"), Scope.World))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	ADreamEnhancedInputEventSystemActor* Actor = Scope.World->SpawnActor<ADreamEnhancedInputEventSystemActor>(PresetClass, FTransform::Identity, Spawn);
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
			ReadObjectProperty(Actor, TEXT("InputModule")), static_cast<UObject*>(Modules[0]));
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
 * Spawned for a player, the preset does what the C++ class does with the shipped assets: pushes the shipped context
 * itself, whose left button is mapped to the shipped IA_Trigger, and a left click through the controller comes out
 * of its handler as the pointer's left button, pressed and then released.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEnhancedPresetAssetContextTest,
	"DreamGUI.Input.EnhancedPreset.ForAPlayerItPushesTheShippedContextAndALeftClickReachesItsPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEnhancedPresetAssetContextTest::RunTest(const FString& Parameters)
{
	using namespace DreamEnhancedPresetAssetTestLocal;

	FPresetHost Host(LoadPresetClass());
	if (!TestTrue(HostCameUp(Host), Host.IsUp()))
	{
		return false;
	}

	const UObject* ShippedContext = LoadObject<UObject>(nullptr, ShippedAssets[0].Path);
	const UObject* ShippedTrigger = LoadObject<UObject>(nullptr, ShippedAssets[1].Path);
	const UInputMappingContext* PushedContext = Cast<UInputMappingContext>(ReadObjectProperty(Host.Preset, TEXT("MappingContext")));
	const UInputAction* LeftAction = Cast<UInputAction>(ReadObjectProperty(Host.Preset, TEXT("TriggerLeftAction")));
	if (!TestNotNull(TEXT("In play the preset holds a context"), PushedContext) || !TestNotNull(TEXT("and a left-button action"), LeftAction))
	{
		return false;
	}
	TestTrue(TEXT("The context it pushed is on the player"), Host.EnhancedInput->HasMappingContext(PushedContext));
	TestSamePtr(TEXT("...and it is the shipped context itself"), static_cast<const UObject*>(PushedContext), ShippedContext);
	TestSamePtr(TEXT("Its left-button action is the shipped IA_Trigger"), static_cast<const UObject*>(LeftAction), ShippedTrigger);
	TestTrue(TEXT("The player's mappings take the left button to it"), PlayerMaps(*Host.EnhancedInput, EKeys::LeftMouseButton, LeftAction));

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
 * Two of the fixes made to the C++ presets that the preset's old event graph never had: the D-pad is
 * a direction key (Slate's KeyEventRules pair it with the arrows), arriving as the D-pad's own
 * direction -- the graph bound neither the D-pad nor any direction but None -- and the space bar is
 * confirm (Slate's Accept), which the graph never bound either.
 *
 * The space bar presses what the player has focused, as Enter and the pad's accept button do; with nothing focused it
 * is the game's key, so the press is checked both ways.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEnhancedPresetAssetNavigationTest,
	"DreamGUI.Input.EnhancedPreset.TheDPadNavigatesInItsOwnDirectionAndTheSpaceBarConfirms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEnhancedPresetAssetNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamEnhancedPresetAssetTestLocal;

	FPresetHost Host(LoadPresetClass());
	if (!TestTrue(HostCameUp(Host), Host.IsUp()))
	{
		return false;
	}

	const TPair<FKey, EDreamUINavigationDirection> DPad[] = {
		{ EKeys::Gamepad_DPad_Right, EDreamUINavigationDirection::Right },
		{ EKeys::Gamepad_DPad_Up, EDreamUINavigationDirection::Up },
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

	// A confirm presses what the player has focused (DreamUIKeyRouting::RouteConfirmKey). With nothing focused -- the
	// D-pad found nothing to land on here -- the space bar is the game's: a jump in a level with no menu open.
	Host.SendKey(EKeys::SpaceBar, IE_Pressed);
	Host.RunFrame();
	UDreamPointerEventData* Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("Player 0's pointer is still there"), Pointer))
	{
		return false;
	}
	TestFalse(TEXT("With nothing focused the space bar presses nothing"), Pointer->bNowIsTriggerPressed);
	Host.SendKey(EKeys::SpaceBar, IE_Released);
	Host.RunFrame();

	// Something focused: a widget with nothing on it to press, so the press stays navigation's trigger.
	UWorld* World = Host.Scope.World;
	UDreamWidget* Focused = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
	Focused->OnRegister();
	ON_SCOPE_EXIT
	{
		if (IsValid(Focused))
		{
			Focused->DestroyWidget();
		}
	};
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(World);
	if (!TestTrue(TEXT("Player 0 has something focused"), Input != nullptr && Input->FocusForNavigation(Focused, 0)))
	{
		return false;
	}
	Host.SendKey(EKeys::SpaceBar, IE_Pressed);
	Host.RunFrame();
	Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("The space bar reached the preset's navigation"), Pointer))
	{
		return false;
	}
	TestTrue(TEXT("The space bar is confirm: it presses navigation's trigger on the focus"), Pointer->bNowIsTriggerPressed);
	TestEqual(TEXT("...as navigation, not as a pointer"), static_cast<int32>(Pointer->InputType), static_cast<int32>(EDreamUIPointerInputType::Navigation));
	Host.SendKey(EKeys::SpaceBar, IE_Released);
	Host.RunFrame();
	TestFalse(TEXT("and its release lets the trigger go"), Pointer->bNowIsTriggerPressed);
	return true;
}

/**
 * The pause fix: Enhanced Input drops a paused frame's triggers for an action whose bTriggerWhenPaused is false, which
 * the shipped actions were, so the old graph went deaf to the mouse in a paused game's menu. The shipped actions
 * trigger while paused now, and the preset decides a paused game per event from UDreamUISettings, as the legacy preset
 * does: with the UI going on while the game is paused, a paused click reaches the pointer; with the UI paused along
 * with the game, it reaches nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEnhancedPresetAssetPausedClickTest,
	"DreamGUI.Input.EnhancedPreset.WhileTheGameIsPausedALeftClickStillReachesItsPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEnhancedPresetAssetPausedClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamEnhancedPresetAssetTestLocal;

	// The setting the preset asks as each input arrives, pinned to its default for the test: UI that goes on
	// answering while the game is paused.
	UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
	const bool bAffectedBefore = Settings->bScreenSpaceUIAffectByGamePause;
	Settings->bScreenSpaceUIAffectByGamePause = false;
	ON_SCOPE_EXIT { Settings->bScreenSpaceUIAffectByGamePause = bAffectedBefore; };

	FPresetHost Host(LoadPresetClass());
	if (!TestTrue(HostCameUp(Host), Host.IsUp()))
	{
		return false;
	}
	const UInputAction* ShippedTrigger = LoadObject<UInputAction>(nullptr, ShippedAssets[1].Path);
	const UInputAction* LeftAction = Cast<UInputAction>(ReadObjectProperty(Host.Preset, TEXT("TriggerLeftAction")));
	if (!TestNotNull(TEXT("The shipped IA_Trigger loads"), ShippedTrigger) || !TestNotNull(TEXT("and the preset holds it"), LeftAction))
	{
		return false;
	}
	TestTrue(TEXT("The shipped action triggers while paused"), ShippedTrigger->bTriggerWhenPaused);
	TestTrue(TEXT("...and it is the action the preset holds"), LeftAction == ShippedTrigger);

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

	Host.SendKey(EKeys::LeftMouseButton, IE_Pressed);
	Host.RunFrame();
	UDreamPointerEventData* Pointer = Host.Pointer();
	if (!TestNotNull(TEXT("A paused left click reached the preset's pointer"), Pointer))
	{
		return false;
	}
	TestTrue(TEXT("A left click through the controller presses the pointer while the game is paused"), Pointer->bNowIsTriggerPressed);
	Host.SendKey(EKeys::LeftMouseButton, IE_Released);
	Host.RunFrame();
	TestFalse(TEXT("and its release lets it go"), Pointer->bNowIsTriggerPressed);
	TestTrue(TEXT("The game stayed paused throughout"), World->IsPaused());

	// The UI paused along with the game: the same click reaches nothing.
	Settings->bScreenSpaceUIAffectByGamePause = true;
	Host.SendKey(EKeys::LeftMouseButton, IE_Pressed);
	Host.RunFrame();
	TestFalse(TEXT("With the UI paused along with the game, a paused left click does not press the pointer"), Pointer->bNowIsTriggerPressed);
	Host.SendKey(EKeys::LeftMouseButton, IE_Released);
	Host.RunFrame();
	return true;
}

#endif
