#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIPrefabGuidMapCleanupTest,
	"LGUI.Prefab.GuidMapCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIPrefabUnsupportedCookVersionTest,
	"LGUI.Prefab.UnsupportedCookVersionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIPrefabExistingObjectValidationTest,
	"LGUI.Prefab.ExistingObjectMappingValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIPrefabUnsupportedCookVersionTest::RunTest(const FString& Parameters)
{
	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	TestNotNull(TEXT("Prefab created"), Prefab);
	if (!Prefab)
	{
		return false;
	}

	Prefab->PrefabVersion = static_cast<uint16>(ELexUIPrefabVersion::BuiltinFArchive) - 1;
	AddExpectedError(TEXT("Cannot cook prefab"), EAutomationExpectedErrorFlags::Contains, 1);
	Prefab->BeginCacheForCookedPlatformData(nullptr);
	TestTrue(TEXT("Unsupported prefab produces no cooked payload"), Prefab->BinaryDataForBuild.IsEmpty());
	return true;
}

bool FLexUIPrefabExistingObjectValidationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	ULexWidget* SourceRoot = World ? NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional) : nullptr;
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
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Source prefab serialized"),
		LexUIPrefabSystem::WidgetSerializer::SavePrefab(SourceRoot, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	UObject* WrongObject = NewObject<UObject>(World);
	TMap<FGuid, TObjectPtr<UObject>> ExistingObjects;
	ExistingObjects.Add(RootGuid, WrongObject);
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> LoadedSubPrefabs;
	AddExpectedError(TEXT("Discarding incompatible existing widget mapping"), EAutomationExpectedErrorFlags::Contains, 1);
	ULexWidget* LoadedRoot = LexUIPrefabSystem::WidgetSerializer::LoadPrefabWithExistingObjects(
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

bool FLexUIPrefabGuidMapCleanupTest::RunTest(const FString& Parameters)
{
	ULexWidget* Root = NewObject<ULexWidget>();
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	ULexWidget* OrphanWidget = NewObject<ULexWidget>();
	ULexLayoutContainerCanvasPanel* ValidLayout = NewObject<ULexLayoutContainerCanvasPanel>(Child);
	ULexLayoutContainerCanvasPanel* OrphanLayout = NewObject<ULexLayoutContainerCanvasPanel>(OrphanWidget);
	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	ULexUIPrefabHelperObject* Helper = NewObject<ULexUIPrefabHelperObject>();
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

	FLexUISubPrefabData SubPrefabData;
	SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(OrphanLayoutGuid, SubPrefabGuid);
	SubPrefabData.MapGuidToObject.Add(SubPrefabGuid, OrphanLayout);
	FLexUISubPrefabObjectUniqueId NewObjectId;
	NewObjectId.RootWidgetGuidInParentPrefab = OrphanLayoutGuid;
	NewObjectId.ObjectGuidInOriginPrefab = FGuid::NewGuid();
	SubPrefabData.MapObjectIdToNewlyCreatedId.Add(NewObjectId, OrphanLayoutGuid);
	FLexUIPrefabOverrideParameterData Override;
	Override.Object = OrphanLayout;
	Override.MemberPropertyNames.Add(TEXT("Padding"));
	SubPrefabData.ObjectOverrideParameterArray.Add(Override);
	Helper->SubPrefabMap.Add(Child, MoveTemp(SubPrefabData));

	TestEqual(TEXT("One outside object is removed"), Helper->CleanupObjectsOutsideRootHierarchy(), 1);
	TestTrue(TEXT("Root GUID is retained"), Helper->MapGuidToObject.Contains(RootGuid));
	TestTrue(TEXT("Child GUID is retained"), Helper->MapGuidToObject.Contains(ChildGuid));
	TestTrue(TEXT("Owned layout GUID is retained"), Helper->MapGuidToObject.Contains(ValidLayoutGuid));
	TestFalse(TEXT("Orphan layout GUID is removed"), Helper->MapGuidToObject.Contains(OrphanLayoutGuid));
	const FLexUISubPrefabData* CleanSubPrefabData = Helper->SubPrefabMap.Find(Child);
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
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Prefab serializer accepts the reachable hierarchy"),
		LexUIPrefabSystem::WidgetSerializer::SavePrefab(
			Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));
	TestFalse(TEXT("Serializer filters the orphan mapping"), ObjectToGuid.Contains(OrphanLayout));
	TestTrue(TEXT("Serializer retains the owned layout mapping"), ObjectToGuid.Contains(ValidLayout));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPanelSlotUserAnchorEditTest,
	"LGUI.Layout.Canvas.UserAnchorEditPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPanelSlotUserAnchorEditTest::RunTest(const FString& Parameters)
{
	ULexWidget* Parent = NewObject<ULexWidget>();
	ULexWidget* Child = NewObject<ULexWidget>(Parent);
	ULexLayoutContainerCanvasPanel* Canvas = Parent->CreateNewLayoutContainer<ULexLayoutContainerCanvasPanel>();
	TestNotNull(TEXT("Canvas layout created"), Canvas);
	TestTrue(TEXT("Child joins the canvas"), Child->TrySetParent(Parent, false));
	ULexPanelSlot* Slot = Child->CreateNewPanelSlot<ULexPanelSlot>();
	TestNotNull(TEXT("Canvas slot created"), Slot);
	if (!Parent || !Child || !Canvas || !Slot)
	{
		return false;
	}

	FLexUIAnchorData Centered;
	Centered.AnchorMin = FVector2D(0.5, 0.5);
	Centered.AnchorMax = FVector2D(0.5, 0.5);
	Centered.SizeDelta = FVector2D(1920.0, 1080.0);
	Child->SetAnchorData(Centered);
	Slot->CaptureAuthoredGeometry(true);
	Slot->MarkLayoutGeometryApplied();

	FLexUIAnchorData Stretched = Centered;
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
	FLexUIPrefabSchemaMigrationTest,
	"LGUI.Prefab.SchemaMigrationPreviewAndUpgrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIPrefabSchemaMigrationTest::RunTest(const FString& Parameters)
{
	UWorld* SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Seed world created"), SeedWorld);
	if (!SeedWorld)
	{
		return false;
	}

	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	ULexWidget* SeedRoot = NewObject<ULexWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional);
	ULexWidget* SeedMissingSlotChild = NewObject<ULexWidget>(SeedRoot, NAME_None, RF_Public | RF_Transactional);
	ULexWidget* SeedPlainParent = NewObject<ULexWidget>(SeedRoot, NAME_None, RF_Public | RF_Transactional);
	ULexWidget* SeedStaleSlotChild = NewObject<ULexWidget>(SeedPlainParent, NAME_None, RF_Public | RF_Transactional);
	ULexLayoutContainerHorizontalBox* Panel = SeedRoot->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
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
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Seed prefab serialized"), LexUIPrefabSystem::WidgetSerializer::SavePrefab(
		SeedRoot, Prefab, SeedObjectToGuid, EmptySubPrefabs, true));
	SeedWorld->DestroyWorld(false);

	Prefab->PrefabSchemaVersion = static_cast<uint16>(ELexUIPrefabSchemaVersion::Unversioned);
	ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	ULexWidget* Root = IsValid(Helper) ? Helper->LoadedRootWidget : nullptr;
	ULexWidget* MissingSlotChild = IsValid(Root) && Root->GetChildrenCount() > 0 ? Root->GetChildByIndex(0) : nullptr;
	ULexWidget* PlainParent = IsValid(Root) && Root->GetChildrenCount() > 1 ? Root->GetChildByIndex(1) : nullptr;
	ULexWidget* StaleSlotChild = IsValid(PlainParent) && PlainParent->GetChildrenCount() > 0
		? PlainParent->GetChildByIndex(0) : nullptr;
	if (!Helper || !Root || !MissingSlotChild || !PlainParent || !StaleSlotChild)
	{
		return false;
	}
	MissingSlotChild->RemovePanelSlot();
	ULexPanelSlot* StaleSlot = StaleSlotChild->CreateNewPanelSlot<ULexPanelSlot>();
	TestNotNull(TEXT("Legacy hierarchy contains a stale slot"), StaleSlot);

	const FLexUIPrefabSchemaMigrationReport Preview = Prefab->PreviewSchemaUpgrade();
	TestFalse(TEXT("Preview has no errors"), Preview.HasErrors());
	TestTrue(TEXT("Preview finds hierarchy repairs"), Preview.HasChanges());
	TestNull(TEXT("Preview does not add the missing slot to the source"), MissingSlotChild->GetPanelSlot());
	TestNotNull(TEXT("Preview does not remove the stale source slot"), StaleSlotChild->GetPanelSlot());
	TestEqual(TEXT("Preview does not update the source schema"), Prefab->PrefabSchemaVersion,
		static_cast<uint16>(ELexUIPrefabSchemaVersion::Unversioned));

	const FLexUIPrefabSchemaMigrationReport Upgrade = Prefab->UpgradeSchema();
	TestFalse(TEXT("Upgrade has no errors"), Upgrade.HasErrors());
	TestEqual(TEXT("Upgrade matches the preview change count"), Upgrade.ChangedObjectCount, Preview.ChangedObjectCount);
	TestTrue(TEXT("Upgrade applies both ownership repairs"), Upgrade.ChangedObjectCount >= 2);
	TestNotNull(TEXT("Upgrade creates the panel-owned slot"), MissingSlotChild->GetPanelSlot());
	TestNull(TEXT("Upgrade removes the stale slot"), StaleSlotChild->GetPanelSlot());
	TestEqual(TEXT("Upgrade records the current schema"), Prefab->PrefabSchemaVersion,
		LEXUI_CURRENT_PREFAB_SCHEMA_VERSION);

	const FLexUIPrefabSchemaMigrationReport SecondUpgrade = Prefab->UpgradeSchema();
	TestFalse(TEXT("Second upgrade has no errors"), SecondUpgrade.HasErrors());
	TestFalse(TEXT("Second upgrade is idempotent"), SecondUpgrade.HasChanges());
	TestEqual(TEXT("Second upgrade changes no objects"), SecondUpgrade.ChangedObjectCount, 0);

	Helper->ClearLoadedPrefab();
	Prefab->ClearPrefabInstanceScene();
	return true;
}

#endif
