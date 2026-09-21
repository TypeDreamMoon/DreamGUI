// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "GameFramework/Actor.h"

/*
 * The pointer's InputType is a sticky mode bit, and ProcessInput's per-frame branch reads it to
 * decide whether to line-trace at all. Navigation input flips it; only a press ever flipped it
 * back. So one arrow key killed hover for the rest of the session while clicks kept working --
 * the mouse position went on updating and nothing re-traced with it.
 *
 * Moving the pointer is the strongest possible statement that this is pointer input, so the move
 * entry points now say so. That is a pure state assertion: no viewport, no raycast, no RHI.
 */

namespace DreamPointerInputTypeTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerMoveClaimsPointerInputTypeTest,
	"DreamGUI.Input.PointerType.MovingThePointerClaimsPointerInputAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerMoveClaimsPointerInputTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPointerInputTypeTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the rig"), Scope.World != nullptr))
	{
		return false;
	}
	FScopedInputRig Rig(Scope.World);
	if (!TestTrue(TEXT("The module registered to the event system"),
		Rig.EventSystem != nullptr && Rig.EventSystem->GetCurrentInputModule() == Rig.Module))
	{
		return false;
	}

	// A direction press is what a player does to move a gamepad focus; it flips pointer 0's mode.
	Rig.Module->InputNavigation(EDreamUINavigationDirection::Down, true, 0);
	UDreamPointerEventData* EventData = Rig.EventSystem->GetPointerEventData(0, true);
	if (!TestTrue(TEXT("The pointer event data exists"), EventData != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("Navigation input owns the pointer after a direction press"),
		EventData->InputType, EDreamUIPointerInputType::Navigation);

	// Moving the mouse takes it back. Before this existed, the mode stayed on Navigation until the
	// next click and the per-frame line trace never ran -- hover was dead the whole time.
	Rig.Module->InputMouseMove(FVector(120.0, 240.0, 0.0));
	TestEqual(TEXT("Moving the mouse claims pointer input"),
		EventData->InputType, EDreamUIPointerInputType::Pointer);
	TestEqual(TEXT("...and carries the position with it"),
		FVector2D(EventData->PointerPosition.X, EventData->PointerPosition.Y), FVector2D(120.0, 240.0));

	// The touch path had the same hole.
	Rig.Module->InputNavigation(EDreamUINavigationDirection::Up, true, 3);
	UDreamPointerEventData* TouchData = Rig.EventSystem->GetPointerEventData(3, true);
	TestEqual(TEXT("The touch pointer is in navigation mode"),
		TouchData->InputType, EDreamUIPointerInputType::Navigation);
	Rig.Module->InputTouchMoved(3, FVector(10.0, 20.0, 0.0));
	TestEqual(TEXT("A moving touch claims pointer input too"),
		TouchData->InputType, EDreamUIPointerInputType::Pointer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerMoveWithoutEventSystemTest,
	"DreamGUI.Input.PointerType.AMoveWithNoEventSystemIsIgnoredRatherThanFatal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPointerMoveWithoutEventSystemTest::RunTest(const FString& Parameters)
{
	using namespace DreamPointerInputTypeTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the module"), Scope.World != nullptr))
	{
		return false;
	}
	AActor* Host = Scope.World->SpawnActor<AActor>();
	UDreamStandaloneInputModule* Module = NewObject<UDreamStandaloneInputModule>(Host);
	Module->RegisterComponent();

	// Never registered to an event system: the bound axis delegate keeps firing while a world tears
	// down, and this was the one entry point here without the guard its five siblings have.
	Module->InputMouseMove(FVector(5.0, 5.0, 0.0));
	Module->InputTouchMoved(0, FVector(5.0, 5.0, 0.0));
	TestTrue(TEXT("Reached here without dereferencing a null event system"), true);
	return true;
}

#endif
