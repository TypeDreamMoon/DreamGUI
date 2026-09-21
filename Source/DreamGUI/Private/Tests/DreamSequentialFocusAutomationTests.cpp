// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Tests/DreamInputEventSystemTestTypes.h"

/*
 * Tab and Shift+Tab, which the framework could express and no key produced.
 *
 * EDreamUINavigationDirection has carried Next and Prev from the start and UUISelectable implements
 * both -- Next tries right, then down; Prev tries left, then up -- so sequential focus was fully
 * built. What was missing was the one line that connects it to a keyboard: the preset actor's
 * direction table listed the four arrows and the four stick directions and nothing that meant Next
 * or Prev, so the feature was unreachable from the drop-in event system every project starts with.
 *
 * Shift is the awkward half. A legacy binding fires for Tab whether or not shift is held and FKey
 * carries no modifier state, so the distinction has to be read off the live player input at the
 * moment the key arrives -- which is why the resolver is an instance method and not part of the
 * table. The shift-held branch needs a real UPlayerInput and is therefore not asserted here; what is
 * asserted is that Tab maps to Next, that the resolver agrees with the table when nothing is held,
 * and that the key is actually bound.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequentialFocusKeyMappingTest,
	"DreamGUI.Navigation.SequentialFocus.TabIsMappedToTheNextDirectionThePresetNeverProduced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequentialFocusKeyMappingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Tab means Next"),
		ADreamNavigationKeyProbeActor::TableDirectionFor(EKeys::Tab), EDreamUINavigationDirection::Next);

	// The arrows are asserted alongside it so a table edit that reached Tab by breaking them fails
	// here rather than in whatever screen notices first.
	TestEqual(TEXT("the arrows still mean what they meant"),
		ADreamNavigationKeyProbeActor::TableDirectionFor(EKeys::Left), EDreamUINavigationDirection::Left);
	TestEqual(TEXT("...all four of them"),
		ADreamNavigationKeyProbeActor::TableDirectionFor(EKeys::Down), EDreamUINavigationDirection::Down);
	TestEqual(TEXT("and a key that is not a direction means nothing"),
		ADreamNavigationKeyProbeActor::TableDirectionFor(EKeys::SpaceBar), EDreamUINavigationDirection::None);

	// The CDO has no world and therefore no player input, which is exactly the "nothing is held"
	// case: the resolver must fall through to the table rather than guess.
	const ADreamNavigationKeyProbeActor* Probe = GetDefault<ADreamNavigationKeyProbeActor>();
	TestEqual(TEXT("with no modifier state to read, Tab resolves to the table's answer"),
		Probe->DirectionFor(EKeys::Tab), EDreamUINavigationDirection::Next);
	TestEqual(TEXT("...and a direction key is unaffected by the shift rule"),
		Probe->DirectionFor(EKeys::Right), EDreamUINavigationDirection::Right);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequentialFocusKeyIsBoundTest,
	"DreamGUI.Navigation.SequentialFocus.ThePresetActorActuallyBindsTheTabKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequentialFocusKeyIsBoundTest::RunTest(const FString& Parameters)
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	} TestWorld;

	ADreamNavigationKeyProbeActor* Actor = TestWorld.World->SpawnActor<ADreamNavigationKeyProbeActor>();
	if (!TestNotNull(TEXT("the preset actor spawns"), Actor))
	{
		return false;
	}
	// AutoReceiveInput only builds an InputComponent when there is a PlayerController to enable input
	// from, and a bare test world has none; hanging one on by hand leaves the actor in the state
	// EnableInput would have left it in. (Same reason the binding-consumption suite does it.)
	Actor->InputComponent = NewObject<UInputComponent>(Actor);
	Actor->BindDreamInput();

	int32 PressedBindings = 0;
	int32 ReleasedBindings = 0;
	for (const FInputKeyBinding& Binding : Actor->InputComponent->KeyBindings)
	{
		if (Binding.Chord.Key != EKeys::Tab)
		{
			continue;
		}
		PressedBindings += Binding.KeyEvent == IE_Pressed ? 1 : 0;
		ReleasedBindings += Binding.KeyEvent == IE_Released ? 1 : 0;
	}

	// The bindings are generated from the direction table, so an entry that is in the table is
	// necessarily bound -- but that implication is exactly what a future refactor can break, and a
	// direction nothing binds is the shape this whole defect had.
	TestEqual(TEXT("Tab is bound on press"), PressedBindings, 1);
	TestEqual(TEXT("...and on release, so the two stay symmetric"), ReleasedBindings, 1);
	return true;
}

#endif
