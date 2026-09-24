// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputModeLibrary.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "DreamPointerEventTestTypes.h"
#include "DreamScopedWorld.h"

/*
 * Who is registered, which pointers exist, and when a pointer stops existing.
 *
 * Three things that were invisible to the suite because every other input test starts from a pointer
 * that already exists and an event system that is never taken away again:
 *
 *  - Asking about a pointer used to create it. bCreateIfNotExist was accepted and ignored, so the six
 *    call sites that pass false and check for null were checking something that could not happen --
 *    and a query for an id nobody had ever pressed bought a permanent entry in the map, which the
 *    input module line-traces once a frame forever after.
 *  - The event system registered in BeginPlay and unregistered in BeginDestroy, by which time
 *    GetWorld() is routinely null, so it usually did not unregister at all. Reload a level and the
 *    manager still held the dead one; the new one was refused as a duplicate and that player's UI
 *    never responded. The removal was also by user index rather than by identity, so a late
 *    unregister evicted whoever had claimed the index since.
 *  - A lifted finger was never retired. Nothing called RemovePointerEventData anywhere in the plugin.
 *
 * All of it is state, reachable without a viewport, a raycast or an RHI.
 */

namespace DreamEventSystemLifecycleTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** An event system with a standalone module registered to it, the shape the preset actor builds. */
	struct FScopedInputRig
	{
		AActor* Host = nullptr;
		UDreamEventSystem* EventSystem = nullptr;
		UDreamStandaloneInputModule* Module = nullptr;

		explicit FScopedInputRig(UWorld* InWorld)
		{
			Host = InWorld->SpawnActor<AActor>();
			EventSystem = NewObject<UDreamEventSystem>(Host);
			EventSystem->RegisterComponent();
			Module = NewObject<UDreamStandaloneInputModule>(Host);
			Module->RegisterComponent();
			Module->RegisterInputModuleToEventSystem(EventSystem);
		}

		bool IsUsable()const{ return Host != nullptr && EventSystem != nullptr && Module != nullptr; }
	};

	/** An event system on its own actor, not registered with the manager -- that is what the test does. */
	UDreamEventSystem* MakeEventSystem(UWorld* World)
	{
		AActor* Host = World->SpawnActor<AActor>();
		UDreamEventSystem* EventSystem = NewObject<UDreamEventSystem>(Host);
		EventSystem->RegisterComponent();
		return EventSystem;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemPointerQueryTest,
	"DreamGUI.Input.EventSystem.AskingAboutAnUnseenPointerDoesNotMintOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemPointerQueryTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	TestEqual(TEXT("A fresh event system knows no pointers"), Rig.EventSystem->GetPointerEventDataMap().Num(), 0);

	// The contract the six null checks in this class were written against.
	UDreamPointerEventData* Queried = Rig.EventSystem->GetPointerEventData(7, false);
	TestNull(TEXT("Querying an id that was never pressed answers nothing"), Queried);
	TestEqual(TEXT("...and leaves no pointer behind to be raycast every frame"),
		Rig.EventSystem->GetPointerEventDataMap().Num(), 0);

	// The same question asked the other way still creates, because that is what the flag is for.
	UDreamPointerEventData* Created = Rig.EventSystem->GetPointerEventData(7, true);
	if (!TestNotNull(TEXT("Asking for one to be created creates it"), Created))
	{
		return false;
	}
	TestEqual(TEXT("...exactly one"), Rig.EventSystem->GetPointerEventDataMap().Num(), 1);
	TestEqual(TEXT("...carrying the id it was asked for"), Created->PointerID, 7);
	// The field was declared, read by handlers looking for their own event system, and written by
	// nothing -- so every one of them resolved to player 0 regardless of whose pointer it was.
	TestEqual(TEXT("...and the user index of the event system that owns it"),
		Created->UserIndex, Rig.EventSystem->GetUserIndex());

	TestEqual(TEXT("And a second query finds the same one rather than another"),
		Rig.EventSystem->GetPointerEventData(7, false), Created);

	// Two public setters pass true deliberately: a caller saying what a pointer IS is entitled to bring
	// it into being, which is the behaviour those two had before the flag was honoured.
	Rig.EventSystem->ActivateNavigationInput(4, nullptr);
	UDreamPointerEventData* Navigating = Rig.EventSystem->GetPointerEventData(4, false);
	if (TestNotNull(TEXT("Activating navigation on an unseen pointer creates it"), Navigating))
	{
		TestEqual(TEXT("...in navigation mode"), Navigating->InputType, EDreamUIPointerInputType::Navigation);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemStaleRegistrationTest,
	"DreamGUI.Input.EventSystem.ADeadRegistrationDoesNotRefuseTheNextEventSystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemStaleRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the event systems"), Scope.World != nullptr))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("The UI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamEventSystem* First = MakeEventSystem(Scope.World);
	UDreamEventSystem* Second = MakeEventSystem(Scope.World);
	if (!TestTrue(TEXT("Two event systems for the same player"), First != nullptr && Second != nullptr))
	{
		return false;
	}

	Manager->AddEventSystem(First);
	TestEqual(TEXT("The first one is the one the player gets"), Manager->GetEventSystemByUserIndex(0), First);

	// What a level reload looks like from the manager's side: the component is gone but the map still
	// holds its weak pointer. Reading an owner off that entry to name it in a duplicate-registration
	// error was a null dereference, and reporting the duplicate at all meant the new level's event
	// system was never registered -- "reload the level and the UI stops responding".
	First->DestroyComponent();
	Manager->AddEventSystem(Second);
	TestEqual(TEXT("A dead registration is replaced rather than defended"),
		Manager->GetEventSystemByUserIndex(0), Second);

	// And the unregister that arrives afterwards belongs to the dead one, not to the index.
	Manager->RemoveEventSystem(First);
	TestEqual(TEXT("A late unregister from the old one does not evict the live one"),
		Manager->GetEventSystemByUserIndex(0), Second);

	Manager->RemoveEventSystem(Second);
	TestNull(TEXT("The live one unregisters itself normally"), Manager->GetEventSystemByUserIndex(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemNavigationIdleTest,
	"DreamGUI.Input.Navigation.AnIdleNavigationPointerTakesNoStepsAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemNavigationIdleTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Every navigation step ends by raising the hit event, whether or not it found anything, so counting
	// those counts the steps -- without needing a selectable, a canvas or a raycast.
	int32 NavigationSteps = 0;
	Rig.EventSystem->GetRaycastHitEvent().AddLambda(
		[&NavigationSteps](bool, const FDreamUIHitResult&, UDreamWidget*) { ++NavigationSteps; });

	Rig.EventSystem->ActivateNavigationInput(0, nullptr);
	UDreamPointerEventData* EventData = Rig.EventSystem->GetPointerEventData(0, false);
	if (!TestNotNull(TEXT("The navigating pointer exists"), EventData))
	{
		return false;
	}
	TestEqual(TEXT("No direction is being held"), EventData->NavigateDirection, EDreamUINavigationDirection::None);

	// Five interval-sized frames with nothing held. The gate used to be the repeat timer alone, which
	// cannot tell "no key is down" from "the key has been down long enough to repeat": this ran a whole
	// navigation step every NavigateInputInterval forever, reveal-scrolling the highlighted widget back
	// into view and re-dispatching hit and select. That is the list that snaps back while you scroll it.
	//
	// Time is passed by hand, since this world is never ticked, and on both clocks: navigation repeat is
	// timed on the pointer clock, which is the world's REAL time (UDreamEventSystem::
	// GetPointerClockSeconds) -- holding a direction in a paused game's menu has to keep stepping.
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		Scope.World->TimeSeconds += 1.0;
		Scope.World->RealTimeSeconds += 1.0;
		Rig.Module->ProcessInput();
	}
	TestEqual(TEXT("An idle navigation pointer takes no steps"), NavigationSteps, 0);

	// The confirm button still has to arrive on the frame it is pressed, direction or no direction --
	// otherwise navigation-mode clicks would have been the price of the fix.
	Rig.Module->InputTriggerForNavigation(true, 0);
	Scope.World->TimeSeconds += 1.0;
	Scope.World->RealTimeSeconds += 1.0;
	Rig.Module->ProcessInput();
	TestEqual(TEXT("A trigger press in navigation mode is dispatched"), NavigationSteps, 1);

	// ...and holding it is not pressing it again.
	Scope.World->TimeSeconds += 1.0;
	Scope.World->RealTimeSeconds += 1.0;
	Rig.Module->ProcessInput();
	TestEqual(TEXT("...once, not once per frame while it is held"), NavigationSteps, 1);

	// A direction actually held is what the repeat timer is for, and it still repeats.
	Rig.Module->InputNavigation(EDreamUINavigationDirection::Down, true, 0);
	Scope.World->TimeSeconds += 1.0;
	Scope.World->RealTimeSeconds += 1.0;
	Rig.Module->ProcessInput();
	TestEqual(TEXT("A held direction does take a step"), NavigationSteps, 2);
	Scope.World->TimeSeconds += 1.0;
	Scope.World->RealTimeSeconds += 1.0;
	Rig.Module->ProcessInput();
	TestEqual(TEXT("...and another once the interval is up"), NavigationSteps, 3);

	// Letting go stops it again rather than leaving it free-running.
	Rig.Module->InputNavigation(EDreamUINavigationDirection::Down, false, 0);
	Scope.World->TimeSeconds += 1.0;
	Scope.World->RealTimeSeconds += 1.0;
	Rig.Module->ProcessInput();
	TestEqual(TEXT("Releasing the direction stops the repeat"), NavigationSteps, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemTouchRetirementTest,
	"DreamGUI.Input.Touch.ALiftedFingerStopsBeingAPointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemTouchRetirementTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	const FVector TouchPoint(120.0, 240.0, 0.0);
	Rig.Module->InputTouchTrigger(true, 3, TouchPoint);
	Rig.Module->ProcessInput();
	TestTrue(TEXT("A finger on the glass is a pointer"),
		Rig.EventSystem->GetPointerEventDataMap().Contains(3));

	Rig.Module->InputTouchTrigger(false, 3, TouchPoint);
	Rig.Module->ProcessInput();
	// Nothing retired it before: it stayed in the map holding whatever it last touched in hover, and the
	// per-frame branch went on line-tracing from where the finger left the glass. Ten fingers used once
	// each is ten full raycasts a frame for the rest of the session.
	TestFalse(TEXT("A lifted finger is retired once its release has been dispatched"),
		Rig.EventSystem->GetPointerEventDataMap().Contains(3));

	// The mouse is not a finger. Its button coming up leaves the mouse exactly where it is, and its
	// pointer has to survive to keep hovering.
	Rig.Module->InputTrigger(FVector(10.0, 10.0, 0.0), true);
	Rig.Module->ProcessInput();
	Rig.Module->InputTrigger(FVector(10.0, 10.0, 0.0), false);
	Rig.Module->ProcessInput();
	TestTrue(TEXT("Releasing a mouse button does not retire the mouse"),
		Rig.EventSystem->GetPointerEventDataMap().Contains(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemUserIndexPipelineTest,
	"DreamGUI.Input.EventSystem.PointingAnEventSystemAtAnotherPlayerMovesEverythingWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemUserIndexPipelineTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Manager != nullptr))
	{
		return false;
	}
	Manager->AddEventSystem(Rig.EventSystem);

	TestEqual(TEXT("An event system starts out speaking for player 0"), Rig.EventSystem->GetUserIndex(), 0);
	UDreamPointerEventData* PlayerZeroPointer = Rig.EventSystem->GetPointerEventData(0, true);
	if (!TestNotNull(TEXT("A pointer for player 0"), PlayerZeroPointer))
	{
		return false;
	}
	TestEqual(TEXT("...stamped with player 0"), PlayerZeroPointer->UserIndex, 0);

	// The field used to be settable only by an author in the Details panel, while the manager's map was
	// keyed by it -- so moving an event system to another player meant it was registered under the
	// player it used to serve and findable under neither.
	Rig.EventSystem->SetUserIndex(1);
	TestEqual(TEXT("It now speaks for player 1"), Rig.EventSystem->GetUserIndex(), 1);
	TestNull(TEXT("...and is no longer what player 0 finds"), Manager->GetEventSystemByUserIndex(0));
	TestEqual(TEXT("...and is what player 1 finds"), Manager->GetEventSystemByUserIndex(1), Rig.EventSystem);
	TestEqual(TEXT("...which is also what the world-context lookup answers"),
		UDreamEventSystem::GetDreamEventSystemInstance(Scope.World, 1), Rig.EventSystem);

	// Pointers belong to the player who was using them. Keeping them would hand player 1 a pointer that
	// is still hovering whatever player 0 left it on.
	TestEqual(TEXT("The old player's pointers do not come along"),
		Rig.EventSystem->GetPointerEventDataMap().Num(), 0);

	// And the stamp follows, which is what every consumer downstream reads to find its own player back.
	UDreamPointerEventData* PlayerOnePointer = Rig.EventSystem->GetPointerEventData(0, true);
	if (TestNotNull(TEXT("A new pointer under the new index"), PlayerOnePointer))
	{
		TestEqual(TEXT("...is stamped with player 1"), PlayerOnePointer->UserIndex, 1);
	}

	// There is no local player 1 in a bare test world, and saying "nobody" is the honest answer for a
	// player that does not exist -- the fallback to the first controller is only ever for index 0.
	TestNull(TEXT("No controller is invented for a player that does not exist"),
		Rig.EventSystem->GetPlayerController());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemGamepadModelTest,
	"DreamGUI.Input.Device.APadIsIdentifiedFromWhatThePlatformCallsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemGamepadModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	// The classifier is a pure function of the two names the platform reports, which is what makes it
	// answerable without a pad plugged in. The spellings come from the platform SDKs and differ between
	// them: the interface name carries the brand on Windows, the hardware identifier does elsewhere.
	TestEqual(TEXT("XInput is an Xbox pad"),
		UDreamEventSystem::GetGamepadModelForDeviceName(TEXT("XInputInterface"), NAME_None), EDreamUIGamepadModel::Xbox);
	TestEqual(TEXT("...however it is spelled"),
		UDreamEventSystem::GetGamepadModelForDeviceName(NAME_None, TEXT("Xbox_Controller")), EDreamUIGamepadModel::Xbox);
	TestEqual(TEXT("A Sony interface is a PlayStation pad"),
		UDreamEventSystem::GetGamepadModelForDeviceName(TEXT("SonyController"), NAME_None), EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("...and so is a DualSense"),
		UDreamEventSystem::GetGamepadModelForDeviceName(NAME_None, TEXT("DualSense")), EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("A Switch pad is a Switch pad"),
		UDreamEventSystem::GetGamepadModelForDeviceName(TEXT("NintendoSwitchInterface"), NAME_None), EDreamUIGamepadModel::Switch);
	// The important one: an unknown pad is Generic rather than a guess, because a guess means the wrong
	// brand's glyph on screen and Generic only means the plain one.
	TestEqual(TEXT("An unnamed pad is generic"),
		UDreamEventSystem::GetGamepadModelForDeviceName(NAME_None, NAME_None), EDreamUIGamepadModel::Generic);
	TestEqual(TEXT("...and so is one nobody recognizes"),
		UDreamEventSystem::GetGamepadModelForDeviceName(TEXT("SomeVendorHID"), TEXT("Model9000")), EDreamUIGamepadModel::Generic);

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	int32 ModelChanges = 0;
	EDreamUIGamepadModel LastModel = EDreamUIGamepadModel::Generic;
	Rig.EventSystem->GetGamepadModelChangedEvent().AddLambda(
		[&ModelChanges, &LastModel](EDreamUIGamepadModel InModel) { ++ModelChanges; LastModel = InModel; });

	TestEqual(TEXT("With no pad used yet, the model is generic"),
		Rig.EventSystem->GetCurrentGamepadModel(), EDreamUIGamepadModel::Generic);

	// A console build, or a game with a "controller type" setting, knows better than the platform. That
	// is the escape hatch, and it has to actually silence detection rather than race it.
	Rig.EventSystem->SetGamepadModelOverride(true, EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("An override is taken"), Rig.EventSystem->GetCurrentGamepadModel(), EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("...and announced once"), ModelChanges, 1);
	TestEqual(TEXT("...to whoever draws the glyphs"), LastModel, EDreamUIGamepadModel::PlayStation);

	Rig.EventSystem->RefreshGamepadModel();
	TestEqual(TEXT("Detection does not overrule an override"),
		Rig.EventSystem->GetCurrentGamepadModel(), EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("...and says nothing"), ModelChanges, 1);

	Rig.EventSystem->SetGamepadModelOverride(false);
	TestEqual(TEXT("Dropping the override goes back to what the platform says"),
		Rig.EventSystem->GetCurrentGamepadModel(), EDreamUIGamepadModel::Generic);
	TestEqual(TEXT("...and says so"), ModelChanges, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemHoverCursorTest,
	"DreamGUI.Input.Cursor.TheUIGivesTheCursorBackInsteadOfForcingTheArrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemHoverCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	APlayerController* PlayerController = Scope.World->SpawnActor<APlayerController>();
	if (PlayerController == nullptr)
	{
		AddInfo(TEXT("No player controller could be spawned in this world; the cursor assertions were skipped."));
		return true;
	}
	// Enrolled by hand, because a world from UWorld::CreateWorld has never had InitializeActorsForPlay
	// called: AActor::PostActorConstruction skips PostInitializeComponents entirely while
	// AreActorsInitialized() is false, and that is where a controller normally puts itself on the
	// world's controller list. Without this the controller exists as an actor and
	// UWorld::GetFirstPlayerController -- which is how every "whose player is this" lookup in the
	// plugin ends up resolving for user 0 -- answers nothing, so nothing under test would run at all.
	Scope.World->AddController(PlayerController);
	if (!TestEqual(TEXT("The world can find the controller we spawned"),
		Scope.World->GetFirstPlayerController(), PlayerController))
	{
		return false;
	}

	// The project's own cursor, set outside the UI -- an RTS build cursor, an aiming reticle.
	PlayerController->CurrentMouseCursor = EMouseCursor::Crosshairs;

	Rig.EventSystem->ApplyHoverCursorToPlayer(true, EMouseCursor::Hand);
	TestEqual(TEXT("A widget claiming a cursor gets it"), (int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::Hand);
	Rig.EventSystem->ApplyHoverCursorToPlayer(true, EMouseCursor::Hand);
	TestEqual(TEXT("...and holding it changes nothing"), (int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::Hand);

	// The assertion this whole seam exists for. Writing EMouseCursor::Default here -- which is what it
	// used to do -- means the plugin quietly owns the cursor for the rest of the session, and the
	// project's cursor is erased on the first frame the pointer is over nothing.
	Rig.EventSystem->ApplyHoverCursorToPlayer(false, EMouseCursor::Default);
	TestEqual(TEXT("Leaving the widget gives the project's cursor back"),
		(int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::Crosshairs);

	// Turning the feature off mid-claim also gives it back, rather than freezing the last hover.
	Rig.EventSystem->ApplyHoverCursorToPlayer(true, EMouseCursor::ResizeLeftRight);
	TestEqual(TEXT("Claimed again"), (int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::ResizeLeftRight);
	Rig.EventSystem->SetApplyHoverCursor(false);
	TestEqual(TEXT("Switching hover cursors off returns it"),
		(int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::Crosshairs);
	Rig.EventSystem->ApplyHoverCursorToPlayer(true, EMouseCursor::Hand);
	TestEqual(TEXT("...and the UI stops writing it at all"),
		(int32)PlayerController->CurrentMouseCursor.GetValue(), (int32)EMouseCursor::Crosshairs);

	// The input-mode wrapper addresses the same player by the same index. Only the cursor flag is
	// observable without a viewport client -- SetInputMode itself needs a local player, and there is
	// none in a headless world -- but that is the half a project reads back.
	UDreamUIInputModeLibrary::SetShowMouseCursor(Scope.World, true, 0);
	TestTrue(TEXT("The input mode library shows the cursor for user 0"),
		UDreamUIInputModeLibrary::GetShowMouseCursor(Scope.World, 0));
	UDreamUIInputModeLibrary::SetInputModeGameOnly(Scope.World, 0);
	TestFalse(TEXT("Game-only input hides it"), UDreamUIInputModeLibrary::GetShowMouseCursor(Scope.World, 0));
	UDreamUIInputModeLibrary::SetInputModeUIOnly(Scope.World, nullptr, 0);
	TestTrue(TEXT("UI-only input shows it again"), UDreamUIInputModeLibrary::GetShowMouseCursor(Scope.World, 0));
	TestEqual(TEXT("...and the library resolves the same controller the event system does"),
		UDreamUIInputModeLibrary::GetPlayerControllerForUser(Scope.World, 0), PlayerController);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemPinchGestureTest,
	"DreamGUI.Input.Gesture.TwoFingersMovingApartAreAPinch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemPinchGestureTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemLifecycleTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Map = NewObject<UDreamWidget>(Scope.World, NAME_None, RF_Public | RF_Transactional);
	Map->SetDisplayName(TEXT("Map"));
	Map->SetWidth(400.0f);
	Map->SetHeight(400.0f);
	Map->OnRegister();
	UDreamGestureCounter* Counter = Map->AddComponent<UDreamGestureCounter>();
	if (!TestNotNull(TEXT("The map is listening for gestures"), Counter))
	{
		return false;
	}

	// Two fingers down. The queue is drained by ProcessInput, which is also what marks them pressed --
	// and there is no raycaster in this world, so the widget each one landed on is stated rather than
	// traced, exactly as the drag-threshold tests hand in their hit results.
	Rig.Module->InputTouchTrigger(true, 0, FVector(300.0, 300.0, 0.0));
	Rig.Module->InputTouchTrigger(true, 1, FVector(400.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	UDreamPointerEventData* FingerA = Rig.EventSystem->GetPointerEventData(0, false);
	UDreamPointerEventData* FingerB = Rig.EventSystem->GetPointerEventData(1, false);
	if (!TestTrue(TEXT("Both fingers are pointers"), FingerA != nullptr && FingerB != nullptr))
	{
		return false;
	}
	FingerA->PressWidget = Map;
	FingerB->PressWidget = Map;
	TestEqual(TEXT("The frame the second finger lands only measures, it does not report"), Counter->PinchCount, 0);

	// Apart by 100 px, well past the minimum change.
	Rig.Module->InputTouchMoved(1, FVector(500.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	TestEqual(TEXT("Moving apart is a pinch"), Counter->PinchCount, 1);
	TestTrue(TEXT("...reported as growing"), Counter->LastPinchDistanceDelta > 0.0f);
	TestTrue(TEXT("...with a scale over one"), Counter->LastPinchScale > 1.0f);

	// A twitch below the threshold says nothing: two fingers resting on glass are not pinching.
	Rig.Module->InputTouchMoved(1, FVector(501.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	TestEqual(TEXT("A twitch under the threshold is not reported"), Counter->PinchCount, 1);

	// Back together, which has to read as shrinking rather than as movement in the abstract.
	Rig.Module->InputTouchMoved(1, FVector(350.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	TestEqual(TEXT("Moving together is a pinch too"), Counter->PinchCount, 2);
	TestTrue(TEXT("...reported as shrinking"), Counter->LastPinchDistanceDelta < 0.0f);

	// One finger up ends the gesture. The next two-finger touch starts a new baseline rather than
	// continuing this one -- otherwise lifting and replacing a finger would report a huge jump.
	Rig.Module->InputTouchTrigger(false, 1, FVector(350.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	const int32 PinchesBefore = Counter->PinchCount;
	Rig.Module->InputTouchTrigger(true, 1, FVector(900.0, 300.0, 0.0));
	Rig.Module->ProcessInput();
	TestEqual(TEXT("A finger returning far away does not report the jump"), Counter->PinchCount, PinchesBefore);
	return true;
}

#endif
