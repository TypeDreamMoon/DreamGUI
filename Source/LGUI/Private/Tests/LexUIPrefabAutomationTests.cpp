#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIPrefabGuidMapCleanupTest,
	"LGUI.Prefab.GuidMapCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

#endif
