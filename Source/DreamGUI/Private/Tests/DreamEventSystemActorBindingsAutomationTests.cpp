// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "Event/DreamEnhancedInputEventSystemActor.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"

/*
 * Who still gets the key once the event system has seen it.
 *
 * Every binding the preset made kept FInputBinding's default bConsumeInput of true, and
 * AutoReceiveInput puts this actor above the pawn on the input stack. Consuming there says nothing
 * about what happened this frame: UPlayerInput adds a bound key to the consume list every frame
 * whether or not an event arrived for it, AnyKey stands in for every key in the key state map, and
 * UEnhancedPlayerInput skips any action whose trigger key is already consumed. So doing the one thing
 * the README asks -- drop the event system actor into the level -- took the keyboard, the mouse
 * buttons, the wheel, the look axis and touch away from the pawn.
 *
 * The actor observes. Whether the UI swallowed an event is the pointer hit test's answer, given per
 * event; bConsumeBoundInput exists for the project that really does want the UI to hold the lot.
 */

namespace DreamEventSystemBindingsTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/**
	 * The actor with its bindings made, without going through BeginPlay.
	 *
	 * AutoReceiveInput only builds an InputComponent when there is a PlayerController to enable input
	 * from, and a bare test world has none -- BindDreamInput would log its warning and return having
	 * bound nothing, which is a green test over empty arrays. Hanging the component on by hand leaves
	 * the actor in the state EnableInput would have left it in.
	 */
	template<class ActorType, class ComponentType = UInputComponent>
	ActorType* MakeBoundActor(UWorld* World, bool bConsumeBoundInput)
	{
		ActorType* Actor = World->SpawnActor<ActorType>();
		if (Actor == nullptr)
		{
			return nullptr;
		}
		Actor->bConsumeBoundInput = bConsumeBoundInput;
		Actor->InputComponent = NewObject<ComponentType>(Actor);
		Actor->BindDreamInput();
		return Actor;
	}

	/**
	 * Describes the first binding whose bConsumeInput disagrees with bExpected, or an empty string
	 * when every one of them agrees.
	 *
	 * A description rather than a bool because a failure has to name the key that was missed: the
	 * bindings are made in three different functions and there are thirty-odd of them.
	 */
	FString FindMismatchedBinding(const UInputComponent& Input, bool bExpected)
	{
		for (const FInputKeyBinding& Binding : Input.KeyBindings)
		{
			if ((Binding.bConsumeInput != 0) != bExpected)
			{
				return FString::Printf(TEXT("key binding %s"), *Binding.Chord.Key.ToString());
			}
		}
		for (const FInputAxisKeyBinding& Binding : Input.AxisKeyBindings)
		{
			if ((Binding.bConsumeInput != 0) != bExpected)
			{
				return FString::Printf(TEXT("axis key binding %s"), *Binding.AxisKey.ToString());
			}
		}
		for (const FInputVectorAxisBinding& Binding : Input.VectorAxisBindings)
		{
			if ((Binding.bConsumeInput != 0) != bExpected)
			{
				return FString::Printf(TEXT("vector axis binding %s"), *Binding.AxisKey.ToString());
			}
		}
		for (const FInputTouchBinding& Binding : Input.TouchBindings)
		{
			if ((Binding.bConsumeInput != 0) != bExpected)
			{
				return FString::Printf(TEXT("touch binding for input event %d"),
					static_cast<int32>(Binding.KeyEvent.GetValue()));
			}
		}
		return FString();
	}

	/** True when some key binding stands for Key. */
	bool HasBindingForKey(const UInputComponent& Input, const FKey& Key)
	{
		for (const FInputKeyBinding& Binding : Input.KeyBindings)
		{
			if (Binding.Chord.Key == Key)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemBindingsObserveByDefaultTest,
	"DreamGUI.Input.EventSystemBindings.TheDropInEventSystemWatchesInputInsteadOfEatingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemBindingsObserveByDefaultTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemBindingsTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to place the preset in"), Scope.World != nullptr))
	{
		return false;
	}

	ADreamStandaloneInputEventSystemActor* Actor =
		MakeBoundActor<ADreamStandaloneInputEventSystemActor>(Scope.World, false);
	if (!TestTrue(TEXT("The preset actor, with an input component and its bindings made"),
		Actor != nullptr && Actor->InputComponent != nullptr))
	{
		return false;
	}
	const UInputComponent& Input = *Actor->InputComponent;

	// Empty arrays satisfy every "nothing consumes" check below, and an early return out of
	// BindDreamInput is exactly how they would come to be empty -- so count first, judge second.
	TestTrue(TEXT("Keys were bound"), Input.KeyBindings.Num() > 0);
	TestTrue(TEXT("The wheel axis was bound"), Input.AxisKeyBindings.Num() > 0);
	TestTrue(TEXT("Mouse movement was bound"), Input.VectorAxisBindings.Num() > 0);
	TestTrue(TEXT("Touch was bound"), Input.TouchBindings.Num() > 0);
	// Named on its own because it is the binding that turns the bug from wrong into total: consuming
	// AnyKey takes every key the player owns, not just the ones listed in the preset.
	TestTrue(TEXT("AnyKey is among the key bindings"), HasBindingForKey(Input, EKeys::AnyKey));

	TestEqual(TEXT("Nothing the preset binds is taken from the pawn"),
		FindMismatchedBinding(Input, false), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventSystemBindingsConsumeWhenAskedTest,
	"DreamGUI.Input.EventSystemBindings.ConsumeBoundInputHandsTheUIEveryBoundKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventSystemBindingsConsumeWhenAskedTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemBindingsTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to place the preset in"), Scope.World != nullptr))
	{
		return false;
	}

	// The escape hatch for a project whose UI is meant to be the only thing listening -- a menu-only
	// title, say. It has to reach every binding, or it silently half-works.
	ADreamStandaloneInputEventSystemActor* Actor =
		MakeBoundActor<ADreamStandaloneInputEventSystemActor>(Scope.World, true);
	if (!TestTrue(TEXT("The preset actor, with an input component and its bindings made"),
		Actor != nullptr && Actor->InputComponent != nullptr))
	{
		return false;
	}
	const UInputComponent& Input = *Actor->InputComponent;

	TestTrue(TEXT("Keys were bound"), Input.KeyBindings.Num() > 0);
	TestTrue(TEXT("The wheel axis was bound"), Input.AxisKeyBindings.Num() > 0);
	TestTrue(TEXT("Mouse movement was bound"), Input.VectorAxisBindings.Num() > 0);
	TestTrue(TEXT("Touch was bound"), Input.TouchBindings.Num() > 0);

	TestEqual(TEXT("Every binding consumes once the project asks for it"),
		FindMismatchedBinding(Input, true), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEnhancedEventSystemInheritedBindingsTest,
	"DreamGUI.Input.EventSystemBindings.TheEnhancedInputPresetInheritsTheSameRestraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEnhancedEventSystemInheritedBindingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventSystemBindingsTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to place the preset in"), Scope.World != nullptr))
	{
		return false;
	}

	// The Enhanced variant replaces only the mouse half; navigation, touch and AnyKey are the base
	// class's legacy bindings, and those are the ones that were eating the pawn's keyboard.
	ADreamEnhancedInputEventSystemActor* Actor =
		MakeBoundActor<ADreamEnhancedInputEventSystemActor, UEnhancedInputComponent>(Scope.World, false);
	if (!TestTrue(TEXT("The Enhanced Input preset, with an enhanced input component and its bindings made"),
		Actor != nullptr && Actor->InputComponent != nullptr))
	{
		return false;
	}
	const UInputComponent& Input = *Actor->InputComponent;

	TestTrue(TEXT("The inherited navigation and AnyKey bindings are there"), Input.KeyBindings.Num() > 0);
	TestTrue(TEXT("So is the inherited touch binding"), Input.TouchBindings.Num() > 0);
	TestTrue(TEXT("AnyKey is among them"), HasBindingForKey(Input, EKeys::AnyKey));
	// No mouse axis bindings to check: this preset's BindMouseInput deliberately does not call Super,
	// and the Input Actions it binds instead decide consumption in the asset, not here.
	TestEqual(TEXT("No mouse vector axis was bound"), Input.VectorAxisBindings.Num(), 0);

	TestEqual(TEXT("The inherited bindings are left for the pawn too"),
		FindMismatchedBinding(Input, false), FString());
	return true;
}

#endif
