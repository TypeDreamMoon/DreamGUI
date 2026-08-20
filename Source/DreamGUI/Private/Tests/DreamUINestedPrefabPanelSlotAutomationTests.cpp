// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUINestedPrefabPanelSlotPersistenceTest,
	"DreamGUI.Prefab.NestedRootPanelSlotPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUINestedPrefabPanelSlotPersistenceTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabSystem;

	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Test world created"), World);
	if (!World)
	{
		return false;
	}

	UDreamUIPrefab* ChildPrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* ChildSourceRoot = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	TMap<UObject*, FGuid> ChildObjectToGuid;
	ChildObjectToGuid.Add(ChildSourceRoot, FGuid::NewGuid());
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	const bool bSavedChild = WidgetSerializer::SavePrefab(
		ChildSourceRoot, ChildPrefab, ChildObjectToGuid, EmptySubPrefabs, true);
	TestTrue(TEXT("Child prefab serialized"), bSavedChild);

	UDreamUIPrefab* ParentPrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* ParentRoot = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	ParentRoot->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>();

	TMap<FGuid, TObjectPtr<UObject>> ChildGuidToObject;
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> LoadedChildSubPrefabs;
	UDreamWidget* NestedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, ChildPrefab, ParentRoot, ChildGuidToObject, LoadedChildSubPrefabs);
	TestNotNull(TEXT("Nested prefab instantiated"), NestedRoot);
	UDreamPanelSlot* AuthoredSlot = NestedRoot ? NestedRoot->GetPanelSlot() : nullptr;
	TestNotNull(TEXT("Parent panel created a slot for the nested root"), AuthoredSlot);
	if (!bSavedChild || !NestedRoot || !AuthoredSlot)
	{
		World->DestroyWorld(false);
		return false;
	}

	AuthoredSlot->SetSizeRule(EDreamPanelSizeRule::Fill);
	AuthoredSlot->SetFillWeight(2.5f);
	AuthoredSlot->SetPadding(FMargin(3.0f, 4.0f, 5.0f, 6.0f));

	TMap<UObject*, FGuid> ParentObjectToGuid;
	ParentObjectToGuid.Add(ParentRoot, FGuid::NewGuid());
	FDreamUISubPrefabData ChildInstanceData;
	ChildInstanceData.PrefabAsset = ChildPrefab;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : ChildGuidToObject)
	{
		const FGuid ParentGuid = FGuid::NewGuid();
		ParentObjectToGuid.Add(Pair.Value, ParentGuid);
		ChildInstanceData.MapGuidToObject.Add(Pair.Key, Pair.Value);
		ChildInstanceData.MapObjectGuidFromParentPrefabToSubPrefab.Add(ParentGuid, Pair.Key);
	}
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> ParentSubPrefabs;
	ParentSubPrefabs.Add(NestedRoot, MoveTemp(ChildInstanceData));
	const bool bSavedParent = WidgetSerializer::SavePrefab(
		ParentRoot, ParentPrefab, ParentObjectToGuid, ParentSubPrefabs, true);
	TestTrue(TEXT("Parent prefab serialized"), bSavedParent);

	TMap<FGuid, TObjectPtr<UObject>> ReloadedGuidToObject;
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> ReloadedSubPrefabs;
	UDreamWidget* ReloadedParent = WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, ParentPrefab, nullptr, ReloadedGuidToObject, ReloadedSubPrefabs);
	UDreamWidget* ReloadedNestedRoot = ReloadedParent && ReloadedParent->GetChildrenCount() == 1
		? ReloadedParent->GetChildByIndex(0)
		: nullptr;
	UDreamPanelSlot* ReloadedSlot = ReloadedNestedRoot ? ReloadedNestedRoot->GetPanelSlot() : nullptr;
	TestNotNull(TEXT("Nested root slot recreated after round trip"), ReloadedSlot);
	if (ReloadedSlot)
	{
		TestEqual(TEXT("Fill rule survives round trip"), ReloadedSlot->SizeRule, EDreamPanelSizeRule::Fill);
		TestEqual(TEXT("Fill weight survives round trip"), ReloadedSlot->FillWeight, 2.5f);
		TestEqual(TEXT("Padding survives round trip"), ReloadedSlot->Padding, FMargin(3.0f, 4.0f, 5.0f, 6.0f));
	}

	const FDreamUISubPrefabData* ReloadedInstanceData = ReloadedSubPrefabs.Find(ReloadedNestedRoot);
	const TObjectPtr<UObject>* TrackedSlot = ReloadedInstanceData
		? ReloadedInstanceData->MapGuidToObject.Find(GetSubPrefabRootPanelSlotOriginGuid())
		: nullptr;
	TestTrue(TEXT("Reloaded helper tracks the parent-owned slot"),
		TrackedSlot && TrackedSlot->Get() == ReloadedSlot);
	TestFalse(TEXT("Parent-owned slot is not exposed as a child-prefab override"),
		ReloadedInstanceData && ReloadedInstanceData->ObjectOverrideParameterArray.ContainsByPredicate(
			[ReloadedSlot](const FDreamUIPrefabOverrideParameterData& Override)
			{
				return Override.Object.Get() == ReloadedSlot
					&& Override.MemberPropertyNames.Contains(GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, SizeRule));
			}));

	// Refreshing an already-loaded parent passes its parent-owned slot back in
	// the existing-object map. The child serializer must ignore that extra object.
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> RefreshedSubPrefabs;
	UDreamWidget* RefreshedParent = WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, ParentPrefab, nullptr, ReloadedGuidToObject, RefreshedSubPrefabs);
	UDreamWidget* RefreshedNestedRoot = RefreshedParent && RefreshedParent->GetChildrenCount() == 1
		? RefreshedParent->GetChildByIndex(0)
		: nullptr;
	UDreamPanelSlot* RefreshedSlot = RefreshedNestedRoot ? RefreshedNestedRoot->GetPanelSlot() : nullptr;
	TestNotNull(TEXT("Existing-object refresh preserves the nested root slot"), RefreshedSlot);
	if (RefreshedSlot)
	{
		TestEqual(TEXT("Refresh retains fill rule"), RefreshedSlot->SizeRule, EDreamPanelSizeRule::Fill);
		TestEqual(TEXT("Refresh retains fill weight"), RefreshedSlot->FillWeight, 2.5f);
		TestEqual(TEXT("Refresh retains padding"), RefreshedSlot->Padding, FMargin(3.0f, 4.0f, 5.0f, 6.0f));
	}

	// Deserialization may normalize transient new-object mappings. Loading the
	// same asset again must retain the reserved parent-slot mapping and values.
	UWorld* SecondWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Second test world created"), SecondWorld);
	if (SecondWorld)
	{
		TMap<FGuid, TObjectPtr<UObject>> SecondGuidToObject;
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SecondSubPrefabs;
		UDreamWidget* SecondParent = WidgetSerializer::LoadPrefabWithExistingObjects(
			SecondWorld, SecondWorld, ParentPrefab, nullptr, SecondGuidToObject, SecondSubPrefabs);
		UDreamWidget* SecondNestedRoot = SecondParent && SecondParent->GetChildrenCount() == 1
			? SecondParent->GetChildByIndex(0)
			: nullptr;
		UDreamPanelSlot* SecondSlot = SecondNestedRoot ? SecondNestedRoot->GetPanelSlot() : nullptr;
		TestNotNull(TEXT("Nested root slot survives a second asset load"), SecondSlot);
		if (SecondSlot)
		{
			TestEqual(TEXT("Second load retains fill rule"), SecondSlot->SizeRule, EDreamPanelSizeRule::Fill);
			TestEqual(TEXT("Second load retains fill weight"), SecondSlot->FillWeight, 2.5f);
			TestEqual(TEXT("Second load retains padding"), SecondSlot->Padding, FMargin(3.0f, 4.0f, 5.0f, 6.0f));
		}
		SecondWorld->DestroyWorld(false);
	}

	World->DestroyWorld(false);
	return true;
}

#endif
