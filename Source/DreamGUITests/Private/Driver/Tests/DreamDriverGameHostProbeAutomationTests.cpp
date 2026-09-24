// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "Core/DreamUIManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"

#include "Driver/DreamDriverInputActors.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * PROBES OF THE HEADLESS GAME HOST.
 *
 * The input-actor rigs rest on a handful of engine facts that nobody had tried headlessly: that a
 * local player made without a viewport gets its Enhanced Input subsystem, that a controller given it
 * by SetPlayer has a PlayerInput and an input stack, that an input actor begun by hand in a world
 * that never began play binds its keys on that stack, that a mapping context reaches the player's
 * mappings, and that one key sent to the controller comes out of its input frame as the action or
 * the binding it should. Each probe checks one link at a time, in order, and says in its message
 * which link broke -- so a red run answers "does this road work at all" before any control test
 * built on it is read.
 *
 * The keys here are sent to APlayerController::InputKey directly, not through the game host's
 * dispatch, so what is probed is the engine road itself.
 */
namespace DreamGameHostProbeLocal
{
	const FIntPoint ViewportSize(1280, 720);

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	FString RigCameUp(const TCHAR* InHostName, const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? FString::Printf(TEXT("[%s] The rig came up"), InHostName)
			: FString::Printf(TEXT("[%s] The rig came up -- it did not: %s"), InHostName, *WhyNot);
	}

	/** A key event the way a device's arrives at the controller, stamped with the player's own device. */
	void SendKey(APlayerController& InController, const FKey& InKey, EInputEvent InEvent)
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(InController.GetPlatformUserId());
		InController.InputKey(FInputKeyEventArgs::CreateSimulated(InKey, InEvent, InEvent == IE_Released ? 0.0f : 1.0f, -1, Device));
	}

	bool PointerIsPressed(const FDreamDriverRig& InRig)
	{
		const UDreamPointerEventData* Pointer = InRig.Context().GetPointerEventData(0);
		return Pointer != nullptr && Pointer->bNowIsTriggerPressed;
	}

	bool PlayerInputMaps(const UEnhancedPlayerInput& InPlayerInput, const FKey& InKey, const UInputAction* InAction)
	{
		// The public read-only view; the array behind it is protected.
		for (const FEnhancedActionKeyMapping& Mapping : InPlayerInput.GetEnhancedActionMappingsView())
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
 * The structure every actor-host test stands on, link by link: a local player on the game instance,
 * its controller on the world's list with a PlayerInput, the input actor initialized and begun with
 * its input component on that controller's stack, its module the driver's (and the production one
 * never made beside it), its event system the rig's, enrolled with the UI manager and finding the
 * same controller through its own lookup -- the lookup the presets use for shift and for the
 * Enhanced Input subsystem.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostProbeStructureTest,
	"DreamGUI.Driver.GameHost.Probe.EachActorHostIsALocalPlayerAControllerAndABegunInputActorOnItsStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostProbeStructureTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostProbeLocal;
	struct FProbeCase { EDreamRigInputHost Host; const TCHAR* Name; };
	const FProbeCase Cases[] = {
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};
	for (const FProbeCase& Case : Cases)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case.Name, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const FDreamDriverContext& Context = Rig.Context();
		UGameInstance* GameInstance = Rig.GetGameInstance();
		APlayerController* Controller = Rig.GetPlayerController();
		ULocalPlayer* LocalPlayer = Context.LocalPlayer;
		AActor* InputActor = Context.InputActor;

		// The player.
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 1. The rig has a game instance"), Case.Name), GameInstance)
			|| !TestEqual(FString::Printf(TEXT("[%s] 2. The game instance has exactly one local player"), Case.Name), GameInstance->GetNumLocalPlayers(), 1)
			|| !TestTrue(FString::Printf(TEXT("[%s] 3. It is the context's local player"), Case.Name), GameInstance->GetLocalPlayers()[0] == LocalPlayer))
		{
			continue;
		}
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 4. There is a player controller"), Case.Name), Controller)
			|| !TestTrue(FString::Printf(TEXT("[%s] 5. The controller's local player is that one"), Case.Name), Controller->GetLocalPlayer() == LocalPlayer)
			|| !TestTrue(FString::Printf(TEXT("[%s] 6. The world's first player controller is that one"), Case.Name), Rig.GetWorld()->GetFirstPlayerController() == Controller)
			|| !TestNotNull(*FString::Printf(TEXT("[%s] 7. SetPlayer gave the controller a PlayerInput"), Case.Name), Controller->PlayerInput.Get()))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("[%s] PlayerInput class: %s"), Case.Name, *Controller->PlayerInput->GetClass()->GetName()));

		// The actor.
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 8. There is an input actor"), Case.Name), InputActor)
			|| !TestTrue(FString::Printf(TEXT("[%s] 9. It was initialized (so its EndPlay will run)"), Case.Name), InputActor->IsActorInitialized())
			|| !TestTrue(FString::Printf(TEXT("[%s] 10. It has begun play"), Case.Name), InputActor->HasActorBegunPlay())
			|| !TestNotNull(*FString::Printf(TEXT("[%s] 11. AutoReceiveInput gave it an input component"), Case.Name), InputActor->InputComponent.Get())
			|| !TestTrue(FString::Printf(TEXT("[%s] 12. The input component is on player 0's input stack"), Case.Name),
				Controller->IsInputComponentInStack(InputActor->InputComponent)))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("[%s] input component class: %s, %d key bindings"), Case.Name,
			*InputActor->InputComponent->GetClass()->GetName(), InputActor->InputComponent->KeyBindings.Num()));
		TestTrue(FString::Printf(TEXT("[%s] 13. BeginPlay bound keys on it"), Case.Name), InputActor->InputComponent->KeyBindings.Num() > 0);

		// The module and the event system.
		UDreamDriverInputModule* Module = Rig.InputModule();
		int32 StandaloneModuleCount = 0;
		for (UActorComponent* Component : InputActor->GetComponents())
		{
			if (Cast<UDreamStandaloneInputModule>(Component) != nullptr)
			{
				++StandaloneModuleCount;
			}
		}
		TestTrue(FString::Printf(TEXT("[%s] 14. The actor's module is the driver's"), Case.Name), Module != nullptr && Module->GetOwner() == InputActor);
		TestEqual(FString::Printf(TEXT("[%s] 15. And it is the actor's only module: the production one was never made"), Case.Name), StandaloneModuleCount, 1);

		const ADreamEventSystemActor* EventSystemActor = Cast<ADreamEventSystemActor>(InputActor);
		UDreamEventSystem* EventSystem = Rig.EventSystem();
		TestTrue(FString::Printf(TEXT("[%s] 16. The rig's event system is the actor's own"), Case.Name),
			EventSystemActor != nullptr && EventSystem != nullptr && EventSystemActor->GetEventSystem() == EventSystem);
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
		TestTrue(FString::Printf(TEXT("[%s] 17. It is enrolled with the UI manager as player 0's"), Case.Name),
			Manager != nullptr && EventSystem != nullptr && Manager->GetEventSystemByUserIndex(0) == EventSystem);
		TestTrue(FString::Printf(TEXT("[%s] 18. The driver module is the one registered with it"), Case.Name),
			EventSystem != nullptr && EventSystem->GetCurrentInputModule() == Module);
		TestTrue(FString::Printf(TEXT("[%s] 19. Its own lookup (game instance, local player 0, the world's controller) finds the same controller"), Case.Name),
			EventSystem != nullptr && EventSystem->GetPlayerController() == Controller);
	}
	return true;
}

/**
 * The Enhanced Input road, link by link: the player's PlayerInput is an EnhancedPlayerInput, the
 * local player has the subsystem, the preset's context is on it, the player's mappings map the left
 * button to the preset's action, and one LeftMouseButton press sent to the controller comes out of
 * the next input frame as that action Started -- then the release as Completed -- and as a press of
 * the rig's pointer (the preset's handler calls the module).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostProbeEnhancedActionTest,
	"DreamGUI.Driver.GameHost.Probe.AMouseButtonKeyThroughTheControllerFiresTheEnhancedInputActionTheActorBinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostProbeEnhancedActionTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostProbeLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(EDreamRigInputHost::EnhancedActor));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(TEXT("Enhanced Input actor"), Rig), Rig.IsUsable()))
	{
		return false;
	}
	APlayerController* Controller = Rig.GetPlayerController();
	ULocalPlayer* LocalPlayer = Rig.Context().LocalPlayer;
	ADreamDriverEnhancedInputActor* InputActor = Cast<ADreamDriverEnhancedInputActor>(Rig.Context().InputActor);
	if (!TestNotNull(TEXT("1. There is a player controller"), Controller)
		|| !TestNotNull(TEXT("2. There is a local player"), LocalPlayer)
		|| !TestNotNull(TEXT("3. The input actor is the Enhanced Input one"), InputActor))
	{
		return false;
	}

	UEnhancedPlayerInput* PlayerInput = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
	if (!TestNotNull(TEXT("4. The player's PlayerInput is an EnhancedPlayerInput (DefaultPlayerInputClass)"), PlayerInput))
	{
		return false;
	}
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!TestNotNull(TEXT("5. The local player has the Enhanced Input subsystem (made in ULocalPlayer::PlayerAdded, no viewport needed)"), Subsystem))
	{
		return false;
	}
	const UInputMappingContext* Context = InputActor->GetDriverMappingContext();
	const UInputAction* LeftAction = InputActor->GetDriverTriggerAction(EDreamUIMouseButtonType::Left);
	if (!TestNotNull(TEXT("6. The actor has its mapping context"), Context)
		|| !TestNotNull(TEXT("7. and its left-button action"), LeftAction)
		|| !TestTrue(TEXT("8. The preset's BeginPlay added the context to the local player"), Subsystem->HasMappingContext(Context))
		|| !TestTrue(TEXT("9. The player's mappings map LeftMouseButton to the action (the rebuild the game host forces has happened)"),
			PlayerInputMaps(*PlayerInput, EKeys::LeftMouseButton, LeftAction)))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("The player's Enhanced Input mappings: %d"), PlayerInput->GetEnhancedActionMappingsView().Num()));

	// Watchers of our own on the actor's component, beside the preset's bindings, counting what fires.
	UEnhancedInputComponent* InputComponent = Cast<UEnhancedInputComponent>(InputActor->InputComponent);
	if (!TestNotNull(TEXT("10. The actor's input component is an EnhancedInputComponent (DefaultInputComponentClass)"), InputComponent))
	{
		return false;
	}
	const TSharedRef<int32> StartedCount = MakeShared<int32>(0);
	const TSharedRef<int32> CompletedCount = MakeShared<int32>(0);
	InputComponent->BindActionInstanceLambda(LeftAction, ETriggerEvent::Started,
		[StartedCount](const FInputActionInstance&) { ++(*StartedCount); });
	InputComponent->BindActionInstanceLambda(LeftAction, ETriggerEvent::Completed,
		[CompletedCount](const FInputActionInstance&) { ++(*CompletedCount); });

	SendKey(*Controller, EKeys::LeftMouseButton, IE_Pressed);
	Rig.PumpFrames(1);
	TestEqual(TEXT("11. One press through the controller's input frame Started the action once"), *StartedCount, 1);
	TestTrue(TEXT("12. The action reads as held"), PlayerInput->GetActionValue(LeftAction).Get<bool>());
	TestTrue(TEXT("13. The preset handed the press to the module, and the rig's pointer is pressed"), PointerIsPressed(Rig));

	SendKey(*Controller, EKeys::LeftMouseButton, IE_Released);
	Rig.PumpFrames(1);
	TestEqual(TEXT("14. The release Completed the action once"), *CompletedCount, 1);
	TestFalse(TEXT("15. and the rig's pointer is released"), PointerIsPressed(Rig));
	return true;
}

/**
 * The legacy road the standalone preset (and the navigation, touch and Back halves of the Enhanced
 * one) is on: one LeftMouseButton press sent to the controller comes out of its next input frame
 * through the preset's key binding as a press of the rig's pointer, and the release as a release.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostProbeLegacyBindingTest,
	"DreamGUI.Driver.GameHost.Probe.AMouseButtonKeyThroughTheControllerReachesTheStandaloneActorsBindingAndThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostProbeLegacyBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostProbeLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(EDreamRigInputHost::StandaloneActor));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(TEXT("standalone input actor"), Rig), Rig.IsUsable()))
	{
		return false;
	}
	APlayerController* Controller = Rig.GetPlayerController();
	AActor* InputActor = Rig.Context().InputActor;
	if (!TestNotNull(TEXT("1. There is a player controller"), Controller)
		|| !TestNotNull(TEXT("2. There is an input actor"), InputActor)
		|| !TestNotNull(TEXT("3. with an input component"), InputActor->InputComponent.Get()))
	{
		return false;
	}
	bool bBindsLeftButton = false;
	for (const FInputKeyBinding& Binding : InputActor->InputComponent->KeyBindings)
	{
		bBindsLeftButton |= Binding.Chord.Key == EKeys::LeftMouseButton;
	}
	if (!TestTrue(TEXT("4. The preset bound LeftMouseButton on it"), bBindsLeftButton))
	{
		return false;
	}

	TestFalse(TEXT("5. Before anything, the rig's pointer is not pressed"), PointerIsPressed(Rig));
	SendKey(*Controller, EKeys::LeftMouseButton, IE_Pressed);
	Rig.PumpFrames(1);
	TestTrue(TEXT("6. One press through the controller's input frame pressed the rig's pointer"), PointerIsPressed(Rig));
	SendKey(*Controller, EKeys::LeftMouseButton, IE_Released);
	Rig.PumpFrames(1);
	TestFalse(TEXT("7. and the release released it"), PointerIsPressed(Rig));
	return true;
}

/**
 * The engine fact the game host's forced mapping rebuild rests on: a context added with the default
 * options is ON the local player at once but not in the player's mappings, because the rebuild is left
 * to FEnhancedInputModule::Tick, once per engine frame -- and a synchronous test has none. If this
 * goes red with step 3 passing, the engine now applies contexts at once and the forced rebuild in
 * DreamDriverGameHost::Build is merely redundant.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGameHostProbeDeferredRebuildTest,
	"DreamGUI.Driver.GameHost.Probe.AMappingContextAddedWithDefaultOptionsWaitsForTheEnhancedInputModulesTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGameHostProbeDeferredRebuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamGameHostProbeLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(EDreamRigInputHost::EnhancedActor));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(TEXT("Enhanced Input actor"), Rig), Rig.IsUsable()))
	{
		return false;
	}
	APlayerController* Controller = Rig.GetPlayerController();
	ULocalPlayer* LocalPlayer = Rig.Context().LocalPlayer;
	UEnhancedPlayerInput* PlayerInput = Controller != nullptr ? Cast<UEnhancedPlayerInput>(Controller->PlayerInput) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer != nullptr ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("1. The player's PlayerInput is an EnhancedPlayerInput"), PlayerInput)
		|| !TestNotNull(TEXT("2. The local player has the Enhanced Input subsystem"), Subsystem))
	{
		return false;
	}

	// A context of the test's own, on a key nothing else maps.
	UInputAction* Action = NewObject<UInputAction>(GetTransientPackage(), NAME_None, RF_Transient);
	Action->ValueType = EInputActionValueType::Boolean;
	UInputMappingContext* Context = NewObject<UInputMappingContext>(GetTransientPackage(), NAME_None, RF_Transient);
	Context->MapKey(Action, EKeys::K);

	Subsystem->AddMappingContext(Context, 1);
	TestTrue(TEXT("3. A context added with the default options is on the local player at once"), Subsystem->HasMappingContext(Context));
	TestFalse(TEXT("4. but not yet in the player's mappings: the rebuild waits for the Enhanced Input module's tick"),
		PlayerInputMaps(*PlayerInput, EKeys::K, Action));

	FModifyContextOptions ApplyNow;
	ApplyNow.bForceImmediately = true;
	Subsystem->RequestRebuildControlMappings(ApplyNow);
	TestTrue(TEXT("5. Forcing the rebuild puts it there"), PlayerInputMaps(*PlayerInput, EKeys::K, Action));

	// Leave the player as the rig made it.
	Subsystem->RemoveMappingContext(Context, ApplyNow);
	TestFalse(TEXT("6. and removing it, forced, takes it out again"), PlayerInputMaps(*PlayerInput, EKeys::K, Action));
	return true;
}

#endif
