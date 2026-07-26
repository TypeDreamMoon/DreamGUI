#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"

// A widget that a user attaches under a nested sub-prefab instance
// (ELexUIPrefabVersion::ActorAttachToSubPrefab) must materialize on every
// load path: the runtime serializer, the editor existing-objects serializer,
// an in-place existing-objects refresh, and the full prefab helper flow that
// the prefab editor, resave commands and cook all build on. The helper flow
// silently dropping such widgets is a data-loss bug: the next editor save
// persists the loss into the asset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUISubPrefabAttachedWidgetBothLoadPathsTest,
	"LGUI.Prefab.SubPrefabAttachedWidgetBothLoadPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LexUISubPrefabAttachedWidgetTestLocal
{
	constexpr const TCHAR* AttachTargetName = TEXT("AttachTarget");
	constexpr const TCHAR* AttachedWidgetName = TEXT("AttachedUnderSubPrefab");
	constexpr const TCHAR* AttachedGrandchildName = TEXT("AttachedGrandchild");

	ULexWidget* FindByDisplayName(ULexWidget* Root, const TCHAR* DisplayName)
	{
		if (!IsValid(Root))
		{
			return nullptr;
		}
		return Root->GetDisplayName().Equals(DisplayName)
			? Root
			: Root->FindChildByDisplayName(DisplayName, true);
	}
}

bool FLexUISubPrefabAttachedWidgetBothLoadPathsTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSystem;
	using namespace LexUISubPrefabAttachedWidgetTestLocal;

	// --- Author the child prefab: root panel with one attachable child. ---
	UWorld* AuthorWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Author world created"), AuthorWorld);
	if (!AuthorWorld)
	{
		return false;
	}

	ULexUIPrefab* ChildPrefab = NewObject<ULexUIPrefab>();
	ULexWidget* ChildSourceRoot = NewObject<ULexWidget>(AuthorWorld, NAME_None, RF_Public | RF_Transactional);
	ChildSourceRoot->SetDisplayName(TEXT("ChildRoot"));
	ChildSourceRoot->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget* ChildAttachTarget = NewObject<ULexWidget>(AuthorWorld, NAME_None, RF_Public | RF_Transactional);
	ChildAttachTarget->SetDisplayName(AttachTargetName);
	ChildAttachTarget->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	TestTrue(TEXT("Attach target joins the child prefab root"), ChildAttachTarget->TrySetParent(ChildSourceRoot, false));

	TMap<UObject*, FGuid> ChildObjectToGuid;
	ChildObjectToGuid.Add(ChildSourceRoot, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Child prefab serialized"),
		WidgetSerializer::SavePrefab(ChildSourceRoot, ChildPrefab, ChildObjectToGuid, EmptySubPrefabs, true));

	// --- Author the parent prefab with the child nested inside it. ---
	ULexUIPrefab* ParentPrefab = NewObject<ULexUIPrefab>();
	ULexWidget* ParentRoot = NewObject<ULexWidget>(AuthorWorld, NAME_None, RF_Public | RF_Transactional);
	ParentRoot->SetDisplayName(TEXT("ParentRoot"));
	ParentRoot->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();

	TMap<FGuid, TObjectPtr<UObject>> ChildGuidToObject;
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> LoadedChildSubPrefabs;
	ULexWidget* NestedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
		AuthorWorld, AuthorWorld, ChildPrefab, ParentRoot, ChildGuidToObject, LoadedChildSubPrefabs);
	TestNotNull(TEXT("Nested child instantiated in the parent"), NestedRoot);
	ULexWidget* NestedAttachTarget = FindByDisplayName(NestedRoot, AttachTargetName);
	TestNotNull(TEXT("Nested attach target found"), NestedAttachTarget);
	if (!NestedRoot || !NestedAttachTarget)
	{
		AuthorWorld->DestroyWorld(false);
		return false;
	}

	// Register the sub-prefab instance the same way MakePrefabAsSubPrefab does.
	TMap<UObject*, FGuid> ParentObjectToGuid;
	ParentObjectToGuid.Add(ParentRoot, FGuid::NewGuid());
	FLexUISubPrefabData ChildInstanceData;
	ChildInstanceData.PrefabAsset = ChildPrefab;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : ChildGuidToObject)
	{
		const FGuid ParentGuid = FGuid::NewGuid();
		ParentObjectToGuid.Add(Pair.Value, ParentGuid);
		ChildInstanceData.MapGuidToObject.Add(Pair.Key, Pair.Value);
		ChildInstanceData.MapObjectGuidFromParentPrefabToSubPrefab.Add(ParentGuid, Pair.Key);
	}
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> ParentSubPrefabs;
	ParentSubPrefabs.Add(NestedRoot, MoveTemp(ChildInstanceData));

	// The user-authored addition: a widget chain attached under the nested
	// sub-prefab's non-root widget. The attached widget is a ScrollBox with its
	// content child, mirroring the historical SidebarBand/SidebarScrollContent
	// pair that a 2026-07-26 editor save dropped from P_TZM_MatchPage.
	ULexWidget* AttachedWidget = NewObject<ULexWidget>(AuthorWorld, NAME_None, RF_Public | RF_Transactional);
	AttachedWidget->SetDisplayName(AttachedWidgetName);
	AttachedWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
	TestTrue(TEXT("New widget attaches under the sub-prefab widget"),
		AttachedWidget->TrySetParent(NestedAttachTarget, false));
	ULexWidget* AttachedGrandchild = NewObject<ULexWidget>(AuthorWorld, NAME_None, RF_Public | RF_Transactional);
	AttachedGrandchild->SetDisplayName(AttachedGrandchildName);
	TestTrue(TEXT("Grandchild attaches under the new widget"),
		AttachedGrandchild->TrySetParent(AttachedWidget, false));

	TestTrue(TEXT("Parent prefab serialized"),
		WidgetSerializer::SavePrefab(ParentRoot, ParentPrefab, ParentObjectToGuid, ParentSubPrefabs, true));

	auto VerifyHierarchy = [&](ULexWidget* Root, const TCHAR* PathTag)
	{
		ULexWidget* Target = FindByDisplayName(Root, AttachTargetName);
		ULexWidget* Attached = FindByDisplayName(Root, AttachedWidgetName);
		ULexWidget* Grandchild = FindByDisplayName(Root, AttachedGrandchildName);
		TestNotNull(FString::Printf(TEXT("%s: attach target materialized"), PathTag), Target);
		TestNotNull(FString::Printf(TEXT("%s: attached widget materialized"), PathTag), Attached);
		TestNotNull(FString::Printf(TEXT("%s: attached grandchild materialized"), PathTag), Grandchild);
		TestTrue(FString::Printf(TEXT("%s: attached widget sits under the sub-prefab widget"), PathTag),
			Attached && Target && Attached->GetParent() == Target);
		TestTrue(FString::Printf(TEXT("%s: grandchild sits under the attached widget"), PathTag),
			Grandchild && Attached && Grandchild->GetParent() == Attached);
	};

	// --- Load path 1: runtime serializer. ---
	UWorld* RuntimeWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Runtime world created"), RuntimeWorld);
	if (RuntimeWorld)
	{
		ULexWidget* RuntimeRoot = WidgetSerializer::LoadPrefab(
			RuntimeWorld, RuntimeWorld, ParentPrefab, nullptr, false);
		TestNotNull(TEXT("Runtime load produced a root"), RuntimeRoot);
		VerifyHierarchy(RuntimeRoot, TEXT("Runtime load"));
	}

	// --- Load path 2: editor existing-objects serializer, fresh maps. ---
	UWorld* EditorWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Editor world created"), EditorWorld);
	TMap<FGuid, TObjectPtr<UObject>> EditorGuidToObject;
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EditorSubPrefabs;
	if (EditorWorld)
	{
		ULexWidget* EditorRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
			EditorWorld, EditorWorld, ParentPrefab, nullptr, EditorGuidToObject, EditorSubPrefabs);
		TestNotNull(TEXT("Editor load produced a root"), EditorRoot);
		VerifyHierarchy(EditorRoot, TEXT("Editor load"));

		// --- Load path 3: in-place refresh with the populated maps. ---
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> RefreshedSubPrefabs;
		ULexWidget* RefreshedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
			EditorWorld, EditorWorld, ParentPrefab, nullptr, EditorGuidToObject, RefreshedSubPrefabs);
		TestNotNull(TEXT("Existing-object refresh produced a root"), RefreshedRoot);
		VerifyHierarchy(RefreshedRoot, TEXT("Existing-object refresh"));
	}

	// --- Load path 4: the full prefab helper flow (prefab editor / resave / cook). ---
	ULexUIPrefabHelperObject* Helper = ParentPrefab->GetPrefabHelperObject();
	TestNotNull(TEXT("Parent prefab helper created"), Helper);
	if (Helper)
	{
		VerifyHierarchy(Helper->LoadedRootWidget, TEXT("Helper load"));

		// The version check runs when prefab assets change while loaded; it must
		// not throw away parent-authored widgets either.
		Helper->CheckPrefabVersion();
		VerifyHierarchy(Helper->LoadedRootWidget, TEXT("Helper after CheckPrefabVersion"));

		// An editor save of the helper hierarchy must keep the widgets in the
		// asset: this is the open-then-save cycle that previously persisted the loss.
		TestTrue(TEXT("Helper saves the prefab"), Helper->SavePrefab());
		UWorld* VerifyWorld = UWorld::CreateWorld(EWorldType::None, false);
		TestNotNull(TEXT("Verify world created"), VerifyWorld);
		if (VerifyWorld)
		{
			ULexWidget* ResavedRoot = WidgetSerializer::LoadPrefab(
				VerifyWorld, VerifyWorld, ParentPrefab, nullptr, false);
			TestNotNull(TEXT("Post-save runtime load produced a root"), ResavedRoot);
			VerifyHierarchy(ResavedRoot, TEXT("Runtime load after helper save"));
			VerifyWorld->DestroyWorld(false);
		}

		Helper->ClearLoadedPrefab();
	}
	ParentPrefab->ClearPrefabInstanceScene();
	if (ULexUIPrefabHelperObject* ChildHelper = ChildPrefab->GetPrefabHelperObject())
	{
		ChildHelper->ClearLoadedPrefab();
	}
	ChildPrefab->ClearPrefabInstanceScene();

	if (RuntimeWorld)
	{
		RuntimeWorld->DestroyWorld(false);
	}
	if (EditorWorld)
	{
		EditorWorld->DestroyWorld(false);
	}
	AuthorWorld->DestroyWorld(false);
	return true;
}

#endif
