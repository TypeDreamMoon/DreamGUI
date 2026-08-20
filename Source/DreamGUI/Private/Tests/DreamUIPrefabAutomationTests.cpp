// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"
#include "Runtime/Launch/Resources/Version.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabGuidMapCleanupTest,
	"DreamGUI.Prefab.GuidMapCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabUnsupportedCookVersionTest,
	"DreamGUI.Prefab.UnsupportedCookVersionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabExistingObjectValidationTest,
	"DreamGUI.Prefab.ExistingObjectMappingValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabInvalidOverrideMappingTest,
	"DreamGUI.Prefab.InvalidOverrideMappingRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabCycleTraversalTest,
	"DreamGUI.Prefab.CycleTraversalIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabRelatedAnchorPropertyTest,
	"DreamGUI.Prefab.RelativeLocationIncludesAnchorData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabRefreshSaveFailureTest,
	"DreamGUI.Prefab.RefreshSaveFailureKeepsVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabLevelHelperDirtyStateTest,
	"DreamGUI.Prefab.LevelHelperDirtyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabUnknownVersionTargetTest,
	"DreamGUI.Prefab.UnknownVersionTargetRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabNestedObjectMappingTest,
	"DreamGUI.Prefab.NestedObjectMappingValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabLevelVersionCleanupTest,
	"DreamGUI.Prefab.LevelVersionCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabInvalidRootCleanupTest,
	"DreamGUI.Prefab.InvalidRootCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabRelatedAnchorPropertyTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamWidget* Widget = NewObject<UDreamWidget>();
	UObject* PlainObject = NewObject<UDreamUIPrefab>();
	if (!Helper || !Widget || !PlainObject)
	{
		return false;
	}

	TestEqual(TEXT("Relative location includes anchor data"),
		Helper->GetExtraRelatedPropertyForApplyOrRevert(
			Widget, UDreamWidget::GetPropertyName_RelativeLocation()),
		UDreamWidget::GetPropertyName_AnchorData());
	TestEqual(TEXT("Unrelated widget properties have no companion"),
		Helper->GetExtraRelatedPropertyForApplyOrRevert(
			Widget, UDreamWidget::GetPropertyName_RelativeRotation()),
		NAME_None);
	TestEqual(TEXT("Non-widget objects have no anchor companion"),
		Helper->GetExtraRelatedPropertyForApplyOrRevert(
			PlainObject, UDreamWidget::GetPropertyName_RelativeLocation()),
		NAME_None);
	return true;
}

bool FDreamUIPrefabRefreshSaveFailureTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	UDreamUIPrefab* SubPrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* SourceRoot = World
		? NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional)
		: nullptr;
	if (!World || !SubPrefab || !SourceRoot)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	const FGuid SourceRootGuid = FGuid::NewGuid();
	TMap<UObject*, FGuid> SourceObjectToGuid;
	SourceObjectToGuid.Add(SourceRoot, SourceRootGuid);
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Sub-prefab source is serialized"),
		DreamUIPrefabSystem::WidgetSerializer::SavePrefab(
			SourceRoot, SubPrefab, SourceObjectToGuid, EmptySubPrefabs, true));

	UDreamUIPrefabHelperObject* SubPrefabHelper = SubPrefab->GetPrefabHelperObject();
	UDreamUIPrefabHelperObject* ParentHelper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamWidget* ParentRoot = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* InstanceRoot = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* ExtraWidget = NewObject<UDreamWidget>(InstanceRoot, NAME_None, RF_Public | RF_Transactional);
	if (!SubPrefabHelper || !ParentHelper || !ParentRoot || !InstanceRoot || !ExtraWidget)
	{
		SubPrefab->ClearPrefabInstanceScene();
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Sub-prefab instance joins the parent hierarchy"), InstanceRoot->TrySetParent(ParentRoot, false));
	TestTrue(TEXT("Extra widget starts inside the sub-prefab instance"), ExtraWidget->TrySetParent(InstanceRoot, false));

	const FGuid ParentRootGuid = FGuid::NewGuid();
	const FGuid InstanceRootGuid = FGuid::NewGuid();
	const FGuid ExtraWidgetGuid = FGuid::NewGuid();
	const FGuid MissingSourceGuid = FGuid::NewGuid();
	ParentHelper->LoadedRootWidget = ParentRoot;
	ParentHelper->PrefabInstanceWorld = World;
	ParentHelper->MapGuidToObject.Add(ParentRootGuid, ParentRoot);
	ParentHelper->MapGuidToObject.Add(InstanceRootGuid, InstanceRoot);
	ParentHelper->MapGuidToObject.Add(ExtraWidgetGuid, ExtraWidget);

	FDreamUISubPrefabData InstanceData;
	InstanceData.PrefabAsset = SubPrefab;
	InstanceData.OverallVersionMD5 = TEXT("unaccepted-version");
	InstanceData.MapGuidToObject.Add(SourceRootGuid, InstanceRoot);
	InstanceData.MapGuidToObject.Add(MissingSourceGuid, ExtraWidget);
	InstanceData.MapObjectGuidFromParentPrefabToSubPrefab.Add(InstanceRootGuid, SourceRootGuid);
	InstanceData.MapObjectGuidFromParentPrefabToSubPrefab.Add(ExtraWidgetGuid, MissingSourceGuid);
	ParentHelper->SubPrefabMap.Add(InstanceRoot, MoveTemp(InstanceData));

	AddExpectedError(TEXT("PrefabAsset is null"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("the refresh remains dirty for retry"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Refresh reports the parent save failure"),
		ParentHelper->RefreshOnSubPrefabDirty(SubPrefab, InstanceRoot));
	TestTrue(TEXT("Failed refresh remains dirty"), ParentHelper->GetAnythingDirty());
	const FDreamUISubPrefabData* RefreshedData = ParentHelper->SubPrefabMap.Find(InstanceRoot);
	TestTrue(TEXT("Failed refresh keeps the previous accepted version"),
		RefreshedData && RefreshedData->OverallVersionMD5 == TEXT("unaccepted-version"));

	ParentHelper->ClearLoadedPrefab();
	SubPrefabHelper->ClearLoadedPrefab();
	SubPrefab->ClearPrefabInstanceScene();
	World->DestroyWorld(false);
	return true;
}

bool FDreamUIPrefabLevelHelperDirtyStateTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	if (!Helper)
	{
		return false;
	}

	TestNull(TEXT("Level-prefab helper has no asset"), Helper->PrefabAsset.Get());
	Helper->SetAnythingDirty();
	TestTrue(TEXT("Level-prefab helper still records dirty state"), Helper->GetAnythingDirty());
	return true;
}

bool FDreamUIPrefabUnknownVersionTargetTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamWidget* UnknownRoot = NewObject<UDreamWidget>();
	if (!Helper || !UnknownRoot)
	{
		return false;
	}

	Helper->RefreshSubPrefabVersion(UnknownRoot);
	TestTrue(TEXT("Unknown version target does not create sub-prefab data"), Helper->SubPrefabMap.IsEmpty());

	FDreamUISubPrefabData InvalidData;
	InvalidData.OverallVersionMD5 = TEXT("unchanged-version");
	Helper->SubPrefabMap.Add(UnknownRoot, MoveTemp(InvalidData));
	Helper->RefreshSubPrefabVersion(UnknownRoot);
	const FDreamUISubPrefabData* StoredData = Helper->SubPrefabMap.Find(UnknownRoot);
	TestTrue(TEXT("Invalid prefab asset leaves the accepted version unchanged"),
		StoredData && StoredData->OverallVersionMD5 == TEXT("unchanged-version"));
	return true;
}

bool FDreamUIPrefabNestedObjectMappingTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefab* SourcePrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* SeedSourceRoot = NewObject<UDreamWidget>();
	UDreamWidget* SeedSourceWidget = NewObject<UDreamWidget>(SeedSourceRoot);
	UDreamLayoutContainerHorizontalBox* SeedPanel = SeedSourceRoot
		? SeedSourceRoot->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>()
		: nullptr;
	if (!SourcePrefab || !SeedSourceRoot || !SeedSourceWidget || !SeedPanel)
	{
		return false;
	}
	TestTrue(TEXT("Nested-reference source joins its panel"), SeedSourceWidget->TrySetParent(SeedSourceRoot, false));
	UDreamPanelSlot* SeedSourceSlot = SeedSourceWidget->GetPanelSlot();
	TestNotNull(TEXT("Nested-reference source receives a panel slot"), SeedSourceSlot);
	if (!SeedSourceSlot)
	{
		return false;
	}

	const FGuid SourceRootGuid = FGuid::NewGuid();
	const FGuid SourceWidgetGuid = FGuid::NewGuid();
	const FGuid SourceSlotGuid = FGuid::NewGuid();
	TMap<UObject*, FGuid> SourceObjectToGuid;
	SourceObjectToGuid.Add(SeedSourceRoot, SourceRootGuid);
	SourceObjectToGuid.Add(SeedSourceWidget, SourceWidgetGuid);
	SourceObjectToGuid.Add(SeedSourceSlot, SourceSlotGuid);
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Nested-reference source prefab is serialized"),
		DreamUIPrefabSystem::WidgetSerializer::SavePrefab(
			SeedSourceRoot, SourcePrefab, SourceObjectToGuid, EmptySubPrefabs, true));

	UDreamUIPrefabHelperObject* SourceHelper = SourcePrefab->GetPrefabHelperObject();
	UDreamWidget* SourceRoot = SourceHelper ? SourceHelper->LoadedRootWidget.Get() : nullptr;
	UDreamWidget* SourceWidget = SourceRoot && SourceRoot->GetChildrenCount() > 0
		? SourceRoot->GetChildByIndex(0)
		: nullptr;
	UDreamPanelSlot* SourceSlot = SourceWidget ? SourceWidget->GetPanelSlot() : nullptr;
	UDreamUIPrefabHelperObject* ParentHelper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamWidget* ParentWidget = NewObject<UDreamWidget>();
	UDreamPanelSlot* ParentSlot = ParentWidget
		? ParentWidget->CreateNewPanelSlot<UDreamPanelSlot>()
		: nullptr;
	FProperty* PanelSlotProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), TEXT("PanelSlot"));
	if (!SourceHelper || !SourceWidget || !SourceSlot || !ParentHelper || !ParentWidget || !ParentSlot || !PanelSlotProperty)
	{
		SourcePrefab->ClearPrefabInstanceScene();
		return false;
	}

	const FGuid ParentSlotGuid = FGuid::NewGuid();
	FDreamUISubPrefabData SubPrefabData;
	SubPrefabData.PrefabAsset = SourcePrefab;
	SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(ParentSlotGuid, SourceSlotGuid);

	AddExpectedError(TEXT("parent object mapping is missing"), EAutomationExpectedErrorFlags::Contains, 1);
	ParentHelper->RevertPrefabPropertyValue(
		ParentWidget, PanelSlotProperty, ParentWidget, SourceWidget, SubPrefabData);
	TestEqual(TEXT("Revert keeps the existing slot when the parent mapping is stale"),
		ParentWidget->GetPanelSlot(), ParentSlot);
	TestFalse(TEXT("Revert does not insert an empty parent mapping"),
		ParentHelper->MapGuidToObject.Contains(ParentSlotGuid));

	ParentHelper->MapGuidToObject.Add(ParentSlotGuid, ParentSlot);
	SourceHelper->MapGuidToObject.Remove(SourceSlotGuid);
	AddExpectedError(TEXT("source object mapping is missing"), EAutomationExpectedErrorFlags::Contains, 1);
	ParentHelper->ApplyPrefabPropertyValue(
		SourceWidget, PanelSlotProperty, ParentWidget, SourceWidget, SubPrefabData);
	TestEqual(TEXT("Apply keeps the existing slot when the source mapping is stale"),
		SourceWidget->GetPanelSlot(), SourceSlot);
	TestFalse(TEXT("Apply does not insert an empty source mapping"),
		SourceHelper->MapGuidToObject.Contains(SourceSlotGuid));

	SourceHelper->ClearLoadedPrefab();
	SourcePrefab->ClearPrefabInstanceScene();
	return true;
}

bool FDreamUIPrefabLevelVersionCleanupTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamWidget* MissingPrefabRoot = NewObject<UDreamWidget>();
	if (!Helper || !MissingPrefabRoot)
	{
		return false;
	}

	Helper->SubPrefabMap.Add(MissingPrefabRoot, FDreamUISubPrefabData());
	Helper->CheckPrefabVersion();
	TestTrue(TEXT("Level helper removes invalid sub-prefab data"), Helper->SubPrefabMap.IsEmpty());
	TestTrue(TEXT("Level helper records version cleanup as dirty"), Helper->GetAnythingDirty());
	return true;
}

bool FDreamUIPrefabInvalidRootCleanupTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	UDreamUIPrefab* ValidPrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* InvalidRoot = NewObject<UDreamWidget>();
	UObject* MappedObject = NewObject<UDreamUIPrefab>();
	if (!Helper || !ValidPrefab || !InvalidRoot || !MappedObject)
	{
		return false;
	}

	const FGuid ParentObjectGuid = FGuid::NewGuid();
	FDreamUISubPrefabData SubPrefabData;
	SubPrefabData.PrefabAsset = ValidPrefab;
	SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(ParentObjectGuid, FGuid::NewGuid());
	Helper->SubPrefabMap.Add(InvalidRoot, MoveTemp(SubPrefabData));
	Helper->MapGuidToObject.Add(ParentObjectGuid, MappedObject);
	InvalidRoot->MarkAsGarbage();
	TestFalse(TEXT("Test root is invalid before cleanup"), IsValid(InvalidRoot));

	TestTrue(TEXT("Invalid sub-prefab root is reported as cleanup"), Helper->CleanupInvalidSubPrefab());
	TestTrue(TEXT("Invalid sub-prefab root is removed"), Helper->SubPrefabMap.IsEmpty());
	TestFalse(TEXT("Invalid root parent mapping is removed"), Helper->MapGuidToObject.Contains(ParentObjectGuid));
	TestTrue(TEXT("Invalid root cleanup marks the helper dirty"), Helper->GetAnythingDirty());
	return true;
}

bool FDreamUIPrefabUnsupportedCookVersionTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
	TestNotNull(TEXT("Prefab created"), Prefab);
	if (!Prefab)
	{
		return false;
	}

	Prefab->PrefabVersion = static_cast<uint16>(EDreamUIPrefabVersion::BuiltinFArchive) - 1;
	AddExpectedError(TEXT("Cannot cook prefab"), EAutomationExpectedErrorFlags::Contains, 1);
	Prefab->BeginCacheForCookedPlatformData(nullptr);
	TestTrue(TEXT("Unsupported prefab produces no cooked payload"), Prefab->BinaryDataForBuild.IsEmpty());
	return true;
}

bool FDreamUIPrefabExistingObjectValidationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* SourceRoot = World ? NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional) : nullptr;
	if (!World || !Prefab || !SourceRoot)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	const FGuid RootGuid = FGuid::NewGuid();
	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(SourceRoot, RootGuid);
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Source prefab serialized"),
		DreamUIPrefabSystem::WidgetSerializer::SavePrefab(SourceRoot, Prefab, ObjectToGuid, EmptySubPrefabs, true));
	TestEqual(TEXT("Prefab records the engine patch version"), Prefab->EnginePatchVersion, static_cast<uint16>(ENGINE_PATCH_VERSION));

	UObject* WrongObject = NewObject<UDreamUIPrefab>(World);
	TMap<FGuid, TObjectPtr<UObject>> ExistingObjects;
	ExistingObjects.Add(RootGuid, WrongObject);
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> LoadedSubPrefabs;
	AddExpectedError(TEXT("Discarding incompatible existing widget mapping"), EAutomationExpectedErrorFlags::Contains, 1);
	UDreamWidget* LoadedRoot = DreamUIPrefabSystem::WidgetSerializer::LoadPrefabWithExistingObjects(
		World, World, Prefab, nullptr, ExistingObjects, LoadedSubPrefabs);
	TestNotNull(TEXT("A valid widget replaces the incompatible mapping"), LoadedRoot);
	TestNotEqual(TEXT("The incompatible UObject is not reused"), static_cast<UObject*>(LoadedRoot), WrongObject);
	TestEqual(TEXT("The root guid points at the replacement widget"), ExistingObjects.FindRef(RootGuid).Get(), static_cast<UObject*>(LoadedRoot));

	if (LoadedRoot)
	{
		LoadedRoot->DestroyWidget();
	}
	World->DestroyWorld(false);
	return true;
}

bool FDreamUIPrefabInvalidOverrideMappingTest::RunTest(const FString& Parameters)
{
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	UObject* UnownedObject = NewObject<UDreamUIPrefab>();
	if (!Helper || !UnownedObject)
	{
		return false;
	}

	AddExpectedError(TEXT("without an owning widget"), EAutomationExpectedErrorFlags::Contains, 2);
	Helper->ApplyPrefabOverride(UnownedObject, {TEXT("MissingProperty")});
	Helper->RevertPrefabOverride(UnownedObject, {TEXT("MissingProperty")});
	return true;
}

bool FDreamUIPrefabCycleTraversalTest::RunTest(const FString& Parameters)
{
	UWorld* SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
	UDreamUIPrefab* PrefabA = NewObject<UDreamUIPrefab>();
	UDreamUIPrefab* PrefabB = NewObject<UDreamUIPrefab>();
	UDreamUIPrefab* UnrelatedPrefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* RootA = SeedWorld ? NewObject<UDreamWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional) : nullptr;
	UDreamWidget* RootB = SeedWorld ? NewObject<UDreamWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional) : nullptr;
	if (!SeedWorld || !PrefabA || !PrefabB || !UnrelatedPrefab || !RootA || !RootB)
	{
		if (SeedWorld)
		{
			SeedWorld->DestroyWorld(false);
		}
		return false;
	}

	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TMap<UObject*, FGuid> ObjectToGuidA;
	ObjectToGuidA.Add(RootA, FGuid::NewGuid());
	TMap<UObject*, FGuid> ObjectToGuidB;
	ObjectToGuidB.Add(RootB, FGuid::NewGuid());
	if (!DreamUIPrefabSystem::WidgetSerializer::SavePrefab(RootA, PrefabA, ObjectToGuidA, EmptySubPrefabs, true)
		|| !DreamUIPrefabSystem::WidgetSerializer::SavePrefab(RootB, PrefabB, ObjectToGuidB, EmptySubPrefabs, true))
	{
		SeedWorld->DestroyWorld(false);
		return false;
	}

	UDreamUIPrefabHelperObject* HelperA = PrefabA ? PrefabA->GetPrefabHelperObject() : nullptr;
	UDreamUIPrefabHelperObject* HelperB = PrefabB ? PrefabB->GetPrefabHelperObject() : nullptr;
	if (!HelperA || !HelperB)
	{
		SeedWorld->DestroyWorld(false);
		return false;
	}

	FDreamUISubPrefabData DataForB;
	DataForB.PrefabAsset = PrefabB;
	FDreamUISubPrefabData DataForA;
	DataForA.PrefabAsset = PrefabA;
	HelperA->SubPrefabMap.Add(NewObject<UDreamWidget>(), MoveTemp(DataForB));
	HelperB->SubPrefabMap.Add(NewObject<UDreamWidget>(), MoveTemp(DataForA));
	TestTrue(TEXT("Direct nested prefab is still detected"), PrefabA->IsPrefabBelongsToThisSubPrefab(PrefabB, true));
	TestFalse(TEXT("Cycle traversal terminates for an unrelated prefab"), PrefabA->IsPrefabBelongsToThisSubPrefab(UnrelatedPrefab, true));

	PrefabA->ReferenceAssetList.Add(PrefabB);
	PrefabB->ReferenceAssetList.Add(PrefabA);
	TestFalse(TEXT("Overall version generation terminates on a cycle"), PrefabA->GenerateOverallVersionMD5().IsEmpty());

	HelperA->ClearLoadedPrefab();
	HelperB->ClearLoadedPrefab();
	PrefabA->ClearPrefabInstanceScene();
	PrefabB->ClearPrefabInstanceScene();
	SeedWorld->DestroyWorld(false);
	return true;
}

bool FDreamUIPrefabGuidMapCleanupTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Root = NewObject<UDreamWidget>();
	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	UDreamWidget* OrphanWidget = NewObject<UDreamWidget>();
	UDreamLayoutContainerCanvasPanel* ValidLayout = NewObject<UDreamLayoutContainerCanvasPanel>(Child);
	UDreamLayoutContainerCanvasPanel* OrphanLayout = NewObject<UDreamLayoutContainerCanvasPanel>(OrphanWidget);
	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	TestTrue(TEXT("Child joins the prefab hierarchy"), Child->TrySetParent(Root, false));
	TestNotNull(TEXT("Valid layout created"), ValidLayout);
	TestNotNull(TEXT("Orphan layout created"), OrphanLayout);
	if (!Root || !Child || !OrphanWidget || !ValidLayout || !OrphanLayout || !Prefab || !Helper)
	{
		return false;
	}

	const FGuid RootGuid = FGuid::NewGuid();
	const FGuid ChildGuid = FGuid::NewGuid();
	const FGuid ValidLayoutGuid = FGuid::NewGuid();
	const FGuid OrphanLayoutGuid = FGuid::NewGuid();
	const FGuid SubPrefabGuid = FGuid::NewGuid();
	Helper->PrefabAsset = Prefab;
	Helper->LoadedRootWidget = Root;
	Helper->MapGuidToObject.Add(RootGuid, Root);
	Helper->MapGuidToObject.Add(ChildGuid, Child);
	Helper->MapGuidToObject.Add(ValidLayoutGuid, ValidLayout);
	Helper->MapGuidToObject.Add(OrphanLayoutGuid, OrphanLayout);

	FDreamUISubPrefabData SubPrefabData;
	SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(OrphanLayoutGuid, SubPrefabGuid);
	SubPrefabData.MapGuidToObject.Add(SubPrefabGuid, OrphanLayout);
	FDreamUISubPrefabObjectUniqueId NewObjectId;
	NewObjectId.RootWidgetGuidInParentPrefab = OrphanLayoutGuid;
	NewObjectId.ObjectGuidInOriginPrefab = FGuid::NewGuid();
	SubPrefabData.MapObjectIdToNewlyCreatedId.Add(NewObjectId, OrphanLayoutGuid);
	FDreamUIPrefabOverrideParameterData Override;
	Override.Object = OrphanLayout;
	Override.MemberPropertyNames.Add(TEXT("Padding"));
	SubPrefabData.ObjectOverrideParameterArray.Add(Override);
	Helper->SubPrefabMap.Add(Child, MoveTemp(SubPrefabData));

	TestEqual(TEXT("One outside object is removed"), Helper->CleanupObjectsOutsideRootHierarchy(), 1);
	TestTrue(TEXT("Root GUID is retained"), Helper->MapGuidToObject.Contains(RootGuid));
	TestTrue(TEXT("Child GUID is retained"), Helper->MapGuidToObject.Contains(ChildGuid));
	TestTrue(TEXT("Owned layout GUID is retained"), Helper->MapGuidToObject.Contains(ValidLayoutGuid));
	TestFalse(TEXT("Orphan layout GUID is removed"), Helper->MapGuidToObject.Contains(OrphanLayoutGuid));
	const FDreamUISubPrefabData* CleanSubPrefabData = Helper->SubPrefabMap.Find(Child);
	TestTrue(TEXT("Sub-prefab parent mapping is removed"),
		CleanSubPrefabData && !CleanSubPrefabData->MapObjectGuidFromParentPrefabToSubPrefab.Contains(OrphanLayoutGuid));
	TestTrue(TEXT("Sub-prefab object mapping is removed"),
		CleanSubPrefabData && !CleanSubPrefabData->MapGuidToObject.Contains(SubPrefabGuid));
	TestTrue(TEXT("New-object mapping is removed"),
		CleanSubPrefabData && CleanSubPrefabData->MapObjectIdToNewlyCreatedId.IsEmpty());
	TestTrue(TEXT("Override mapping is removed"),
		CleanSubPrefabData && CleanSubPrefabData->ObjectOverrideParameterArray.IsEmpty());
	TestTrue(TEXT("Cleanup marks the helper dirty"), Helper->GetAnythingDirty());

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Root, RootGuid);
	ObjectToGuid.Add(Child, ChildGuid);
	ObjectToGuid.Add(ValidLayout, ValidLayoutGuid);
	ObjectToGuid.Add(OrphanLayout, OrphanLayoutGuid);
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Prefab serializer accepts the reachable hierarchy"),
		DreamUIPrefabSystem::WidgetSerializer::SavePrefab(
			Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));
	TestFalse(TEXT("Serializer filters the orphan mapping"), ObjectToGuid.Contains(OrphanLayout));
	TestTrue(TEXT("Serializer retains the owned layout mapping"), ObjectToGuid.Contains(ValidLayout));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelSlotUserAnchorEditTest,
	"DreamGUI.Layout.Canvas.UserAnchorEditPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelSlotUserAnchorEditTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Parent = NewObject<UDreamWidget>();
	UDreamWidget* Child = NewObject<UDreamWidget>(Parent);
	UDreamLayoutContainerCanvasPanel* Canvas = Parent->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>();
	TestNotNull(TEXT("Canvas layout created"), Canvas);
	TestTrue(TEXT("Child joins the canvas"), Child->TrySetParent(Parent, false));
	UDreamPanelSlot* Slot = Child->CreateNewPanelSlot<UDreamPanelSlot>();
	TestNotNull(TEXT("Canvas slot created"), Slot);
	if (!Parent || !Child || !Canvas || !Slot)
	{
		return false;
	}

	FDreamUIAnchorData Centered;
	Centered.AnchorMin = FVector2D(0.5, 0.5);
	Centered.AnchorMax = FVector2D(0.5, 0.5);
	Centered.SizeDelta = FVector2D(1920.0, 1080.0);
	Child->SetAnchorData(Centered);
	Slot->CaptureAuthoredGeometry(true);
	Slot->MarkLayoutGeometryApplied();

	FDreamUIAnchorData Stretched = Centered;
	Stretched.AnchorMin = FVector2D::ZeroVector;
	Stretched.AnchorMax = FVector2D::UnitVector;
	Stretched.SizeDelta = FVector2D::ZeroVector;
	Child->SetAnchorData(Stretched);
	Slot->SyncAuthoredGeometryAfterUserEdit();
	TestFalse(TEXT("Canvas without AutoSize releases stale layout geometry"), Slot->HasLayoutGeometryApplied());
	Canvas->SnapshotLayout();
	Canvas->CalculateLayout();
	if (Slot->HasLayoutGeometryApplied())
	{
		Slot->RestoreAuthoredGeometry();
	}
	else
	{
		Slot->CaptureAuthoredGeometry(true);
	}
	TestEqual(TEXT("Stretch anchor minimum survives the next canvas pass"), Child->GetAnchorMin(), FVector2D::ZeroVector);
	TestEqual(TEXT("Stretch anchor maximum survives the next canvas pass"), Child->GetAnchorMax(), FVector2D::UnitVector);

	Child->SetAnchorData(Centered);
	Slot->CaptureAuthoredGeometry(true);
	Slot->SetAutoSize(true);
	Slot->MarkLayoutGeometryApplied(false, false, true, true);
	Child->SetAnchorData(Stretched);
	Slot->SyncAuthoredGeometryAfterUserEdit();
	TestTrue(TEXT("Canvas AutoSize retains size ownership"), Slot->HasLayoutGeometryApplied());
	Canvas->SnapshotLayout();
	Canvas->CalculateLayout();
	Slot->SetAutoSize(false);
	Slot->RestoreAuthoredGeometry();
	TestEqual(TEXT("AutoSize restore keeps the user-authored anchor minimum"), Child->GetAnchorMin(), FVector2D::ZeroVector);
	TestEqual(TEXT("AutoSize restore keeps the user-authored anchor maximum"), Child->GetAnchorMax(), FVector2D::UnitVector);
	TestEqual(TEXT("AutoSize restore returns the authored width"), Child->GetSizeDelta().X, 1920.0);
	TestEqual(TEXT("AutoSize restore returns the authored height"), Child->GetSizeDelta().Y, 1080.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabSchemaMigrationTest,
	"DreamGUI.Prefab.SchemaMigrationPreviewAndUpgrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabSchemaMigrationTest::RunTest(const FString& Parameters)
{
	UWorld* SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Seed world created"), SeedWorld);
	if (!SeedWorld)
	{
		return false;
	}

	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
	UDreamWidget* SeedRoot = NewObject<UDreamWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* SeedMissingSlotChild = NewObject<UDreamWidget>(SeedRoot, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* SeedPlainParent = NewObject<UDreamWidget>(SeedRoot, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* SeedStaleSlotChild = NewObject<UDreamWidget>(SeedPlainParent, NAME_None, RF_Public | RF_Transactional);
	UDreamLayoutContainerHorizontalBox* Panel = SeedRoot->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>();
	TestNotNull(TEXT("Panel layout created"), Panel);
	TestTrue(TEXT("Seed child joins panel"), SeedMissingSlotChild->TrySetParent(SeedRoot, false));
	TestTrue(TEXT("Seed plain parent joins panel"), SeedPlainParent->TrySetParent(SeedRoot, false));
	TestTrue(TEXT("Seed nested child joins plain parent"), SeedStaleSlotChild->TrySetParent(SeedPlainParent, false));
	if (!Prefab || !SeedRoot || !SeedMissingSlotChild || !SeedPlainParent || !SeedStaleSlotChild || !Panel)
	{
		SeedWorld->DestroyWorld(false);
		return false;
	}

	TMap<UObject*, FGuid> SeedObjectToGuid;
	SeedObjectToGuid.Add(SeedRoot, FGuid::NewGuid());
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Seed prefab serialized"), DreamUIPrefabSystem::WidgetSerializer::SavePrefab(
		SeedRoot, Prefab, SeedObjectToGuid, EmptySubPrefabs, true));
	SeedWorld->DestroyWorld(false);

	Prefab->PrefabSchemaVersion = static_cast<uint16>(EDreamUIPrefabSchemaVersion::Unversioned);
	UDreamUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	UDreamWidget* Root = IsValid(Helper) ? Helper->LoadedRootWidget : nullptr;
	UDreamWidget* MissingSlotChild = IsValid(Root) && Root->GetChildrenCount() > 0 ? Root->GetChildByIndex(0) : nullptr;
	UDreamWidget* PlainParent = IsValid(Root) && Root->GetChildrenCount() > 1 ? Root->GetChildByIndex(1) : nullptr;
	UDreamWidget* StaleSlotChild = IsValid(PlainParent) && PlainParent->GetChildrenCount() > 0
		? PlainParent->GetChildByIndex(0) : nullptr;
	if (!Helper || !Root || !MissingSlotChild || !PlainParent || !StaleSlotChild)
	{
		return false;
	}
	MissingSlotChild->RemovePanelSlot();
	UDreamPanelSlot* StaleSlot = StaleSlotChild->CreateNewPanelSlot<UDreamPanelSlot>();
	TestNotNull(TEXT("Legacy hierarchy contains a stale slot"), StaleSlot);

	const FDreamUIPrefabSchemaMigrationReport Preview = Prefab->PreviewSchemaUpgrade();
	TestFalse(TEXT("Preview has no errors"), Preview.HasErrors());
	TestTrue(TEXT("Preview finds hierarchy repairs"), Preview.HasChanges());
	TestNull(TEXT("Preview does not add the missing slot to the source"), MissingSlotChild->GetPanelSlot());
	TestNotNull(TEXT("Preview does not remove the stale source slot"), StaleSlotChild->GetPanelSlot());
	TestEqual(TEXT("Preview does not update the source schema"), Prefab->PrefabSchemaVersion,
		static_cast<uint16>(EDreamUIPrefabSchemaVersion::Unversioned));

	const FDreamUIPrefabSchemaMigrationReport Upgrade = Prefab->UpgradeSchema();
	TestFalse(TEXT("Upgrade has no errors"), Upgrade.HasErrors());
	TestEqual(TEXT("Upgrade matches the preview change count"), Upgrade.ChangedObjectCount, Preview.ChangedObjectCount);
	TestTrue(TEXT("Upgrade applies both ownership repairs"), Upgrade.ChangedObjectCount >= 2);
	TestNotNull(TEXT("Upgrade creates the panel-owned slot"), MissingSlotChild->GetPanelSlot());
	TestNull(TEXT("Upgrade removes the stale slot"), StaleSlotChild->GetPanelSlot());
	TestEqual(TEXT("Upgrade records the current schema"), Prefab->PrefabSchemaVersion,
		LEXUI_CURRENT_PREFAB_SCHEMA_VERSION);

	const FDreamUIPrefabSchemaMigrationReport SecondUpgrade = Prefab->UpgradeSchema();
	TestFalse(TEXT("Second upgrade has no errors"), SecondUpgrade.HasErrors());
	TestFalse(TEXT("Second upgrade is idempotent"), SecondUpgrade.HasChanges());
	TestEqual(TEXT("Second upgrade changes no objects"), SecondUpgrade.ChangedObjectCount, 0);

	Helper->ClearLoadedPrefab();
	Prefab->ClearPrefabInstanceScene();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabMoveSkipsRootNameSyncTest,
	"DreamGUI.Prefab.MovingBetweenFoldersDoesNotRewriteThePrefab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabMoveSkipsRootNameSyncTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabSystem;

	// PostRename syncs the root widget's display name from the asset name, and that sync is
	// expensive: it builds an editor world, deserializes the prefab, rewrites the asset and tears
	// the world down. A MOVE is a rename into another package under the same name, so there is
	// nothing to sync -- and paying it anyway is what made dragging prefabs between folders look
	// like a hang, while silently rewriting bytes in an asset the user only meant to relocate.
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Author world created"), World);
	if (!World)
	{
		return false;
	}
	UPackage* SourcePackage = CreatePackage(TEXT("/Temp/DreamUIPrefabMoveTest/Source"));
	UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>(SourcePackage, TEXT("MoveProbePrefab"), RF_Public | RF_Standalone);
	UDreamWidget* SourceRoot = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
	SourceRoot->SetDisplayName(TEXT("HandNamedRoot"));
	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(SourceRoot, FGuid::NewGuid());
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Probe prefab is serialized"),
		WidgetSerializer::SavePrefab(SourceRoot, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	// Move: same object name, different package.
	UPackage* DestPackage = CreatePackage(TEXT("/Temp/DreamUIPrefabMoveTest/Destination"));
	const bool bMoved = Prefab->Rename(TEXT("MoveProbePrefab"), DestPackage, REN_DontCreateRedirectors | REN_NonTransactional);
	TestTrue(TEXT("The prefab moves to another package"), bMoved);

	UWorld* VerifyWorld = UWorld::CreateWorld(EWorldType::None, false);
	if (VerifyWorld)
	{
		UDreamWidget* Loaded = WidgetSerializer::LoadPrefab(VerifyWorld, VerifyWorld, Prefab, nullptr, false);
		TestNotNull(TEXT("The moved prefab still loads"), Loaded);
		// The observable consequence of skipping the sync: a hand-given root name survives a move.
		// Before the fix the move rewrote it to the asset name, which is a content change the user
		// never asked for and the reason the asset's bytes were touched at all.
		TestEqual(TEXT("A move leaves the root widget's name alone"),
			Loaded ? Loaded->GetDisplayName() : FString(), FString(TEXT("HandNamedRoot")));
		VerifyWorld->DestroyWorld(false);
	}

	// A real rename still syncs, so the feature itself is intact rather than merely disabled.
	const bool bRenamed = Prefab->Rename(TEXT("RenamedProbePrefab"), DestPackage, REN_DontCreateRedirectors | REN_NonTransactional);
	TestTrue(TEXT("The prefab renames"), bRenamed);
	UWorld* RenameVerifyWorld = UWorld::CreateWorld(EWorldType::None, false);
	if (RenameVerifyWorld)
	{
		UDreamWidget* Loaded = WidgetSerializer::LoadPrefab(RenameVerifyWorld, RenameVerifyWorld, Prefab, nullptr, false);
		TestEqual(TEXT("A rename does sync the root widget's name"),
			Loaded ? Loaded->GetDisplayName() : FString(), FString(TEXT("RenamedProbePrefab")));
		RenameVerifyWorld->DestroyWorld(false);
	}

	if (UDreamUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject())
	{
		Helper->ClearLoadedPrefab();
	}
	Prefab->ClearPrefabInstanceScene();
	World->DestroyWorld(false);
	return true;
}

#endif
