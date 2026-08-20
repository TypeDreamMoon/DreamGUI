// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/WidgetSerializer.h"
#include "UObject/Package.h"

/*
 * Clearing a squatted name during deserialization.
 *
 * When the payload wants to create an object at a name something already occupies -- a constructor
 * built a subobject there, typically -- the loader has to move the occupant aside first. That is a
 * rename, and the order matters: renaming ends in UnhashObject, which is fatal on an object that
 * has already begun destruction and is therefore no longer in the hash. Doing the teardown first
 * and the rename second is the ordering that crashes.
 *
 * These pin what the call must achieve (the name is free) and what it must refuse (touching an
 * object already on its way out). Neither reproduces the crash itself: that needs an object in the
 * exact intermediate hash state, and a test that fataled would take the whole suite with it.
 */

namespace DreamNameReleaseTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamReleaseNameFreesItTest,
	"DreamGUI.Prefab.NameRelease.TheNameIsActuallyFreedAfterwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamReleaseNameFreesItTest::RunTest(const FString& Parameters)
{
	using namespace DreamNameReleaseTestLocal;
	using namespace LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE;
	FScopedGameWorld TestWorld;

	UDreamWidget* Outer = NewObject<UDreamWidget>(TestWorld.World, TEXT("ReleaseOuter"), RF_Public | RF_Transactional);
	const FName ContestedName(TEXT("Contested"));
	UObject* Squatter = NewObject<UDreamPanelSlot>(Outer, ContestedName);
	if (!TestNotNull(TEXT("The squatter exists"), Squatter))return false;
	TestNotNull(TEXT("...and holds the name"), StaticFindObjectFast(nullptr, Outer, ContestedName));

	TestTrue(TEXT("Releasing reports success"), WidgetSerializer::ReleaseNameFromExistingObject(Squatter));

	// The whole purpose of the call. If the name is still taken, the NewObject that follows it in
	// the loader silently gets a numbered variant and the payload deserializes under wrong names.
	TestNull(TEXT("The name is free"), StaticFindObjectFast(nullptr, Outer, ContestedName));
	TestTrue(TEXT("The squatter was moved to the transient package"),
		Squatter->GetOuter() == GetTransientPackage());

	// And the name really is reusable, which is the thing the loader does next.
	UObject* Replacement = NewObject<UDreamPanelSlot>(Outer, ContestedName);
	TestNotNull(TEXT("A new object takes the name"), Replacement);
	TestEqual(TEXT("...under the name asked for, not a numbered variant"), Replacement->GetFName(), ContestedName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamReleaseNameRefusesDyingTest,
	"DreamGUI.Prefab.NameRelease.RefusesAnObjectAlreadyBeingDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamReleaseNameRefusesDyingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNameReleaseTestLocal;
	using namespace LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE;
	FScopedGameWorld TestWorld;

	UDreamWidget* Outer = NewObject<UDreamWidget>(TestWorld.World, TEXT("RefuseOuter"), RF_Public | RF_Transactional);
	UObject* Dying = NewObject<UDreamPanelSlot>(Outer, TEXT("Dying"));
	if (!TestNotNull(TEXT("The object exists"), Dying))return false;
	Dying->ConditionalBeginDestroy();
	TestTrue(TEXT("It has begun destruction"), Dying->HasAnyFlags(RF_BeginDestroyed));

	// Renaming from here is exactly the sequence that fatals with a hash consistency failure, so the
	// call has to decline and say so rather than press on. The caller reports it and moves the new
	// object to a different name, which is a bad prefab, not a dead editor.
	TestFalse(TEXT("Releasing declines"), WidgetSerializer::ReleaseNameFromExistingObject(Dying));

	// Null is the other way the name can already be free.
	TestTrue(TEXT("A null occupant means the name is free"), WidgetSerializer::ReleaseNameFromExistingObject(nullptr));
	return true;
}

#endif
