// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"

/*
 * Coverage for the two halves of "arranged geometry must never masquerade as authored data":
 *
 *  - Measurement: GetDesiredSize may only see authored values. Feeding a panel-arranged rect back into
 *    measurement closes the loop where a squeezed widget measures as squeezed forever.
 *  - Persistence: the asset stores authored AnchorData only. Panel arrangement is a runtime result,
 *    re-derived by the first layout pass after load, so re-saves cannot churn arranged values.
 */

namespace LexUIAuthoredGeometryTestLocal
{
	/** Game world with Root(320x180, VerticalBox) -> Child(authored 120x80, slot Fill), laid out once. */
	struct FArrangedFixture
	{
		UWorld* World = nullptr;
		ULexWidget* Root = nullptr;
		ULexWidget* Child = nullptr;
		ULexLayoutContainerVerticalBox* Panel = nullptr;

		bool BuildAndArrange()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return false;
			}
			Root = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Child = NewObject<ULexWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("ArrangedRoot"));
			Child->SetDisplayName(TEXT("ArrangedChild"));
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			Child->SetWidth(120.0f);
			Child->SetHeight(80.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			Panel = Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
			ULexPanelSlot* Slot = Child->GetPanelSlot();
			if (!Panel || !Slot)
			{
				return false;
			}
			Slot->SetSizeRule(ELexPanelSizeRule::Fill);
			Root->OnRegister();
			Child->OnRegister();
			ULexWidget::MarkLayoutForRebuild(Root);
			ULexWidget::RebuildLayoutImmediately(Root);
			return true;
		}

		~FArrangedFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexMeasureIgnoresArrangedValuesTest,
	"LGUI.Layout.Measure.DesiredSizeIgnoresArrangedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexMeasureIgnoresArrangedValuesTest::RunTest(const FString& Parameters)
{
	using namespace LexUIAuthoredGeometryTestLocal;
	FArrangedFixture Fixture;
	if (!Fixture.BuildAndArrange())
	{
		AddError(TEXT("Fixture failed to build"));
		return false;
	}

	// Precondition: the panel really stomped the child (Fill stretches it to the panel rect).
	TestEqual(TEXT("Fill child is arranged to the panel size"), Fixture.Child->GetSize(), Fixture.Root->GetSize());
	TestTrue(TEXT("Slot records that layout wrote the rect"), Fixture.Child->GetPanelSlot()->HasLayoutGeometryApplied());

	// Measurement must report the authored 120x80, never the arranged 320x180.
	TestEqual(TEXT("Desired size is the authored rect, not the arranged rect"),
		Fixture.Panel->GetDesiredSize(Fixture.Child), FVector2D(120.0, 80.0));

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexSaveWritesAuthoredGeometryTest,
	"LGUI.Prefab.AuthoredGeometry.SaveWritesAuthoredNotArranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexSaveWritesAuthoredGeometryTest::RunTest(const FString& Parameters)
{
	using namespace LexUIAuthoredGeometryTestLocal;
	using namespace LexUIPrefabSystem;
	FArrangedFixture Fixture;
	if (!Fixture.BuildAndArrange())
	{
		AddError(TEXT("Fixture failed to build"));
		return false;
	}

	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Fixture.Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Arranged hierarchy saves"),
		WidgetSerializer::SavePrefab(Fixture.Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	// The live hierarchy keeps its arranged state after the save (the scope restored it).
	TestEqual(TEXT("Live child keeps its arranged rect after saving"),
		Fixture.Child->GetSize(), Fixture.Root->GetSize());
	TestTrue(TEXT("Live slot still reads applied after saving"),
		Fixture.Child->GetPanelSlot()->HasLayoutGeometryApplied());

	// The asset holds the authored rect: reload without running layout and read what was persisted.
	UWorld* ReloadWorld = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Reload world created"), ReloadWorld);
	if (ReloadWorld)
	{
		TMap<FGuid, TObjectPtr<UObject>> ReloadedObjects;
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> ReloadedSubPrefabs;
		ULexWidget* ReloadedRoot = WidgetSerializer::LoadPrefabWithExistingObjects(
			ReloadWorld, ReloadWorld, Prefab, nullptr, ReloadedObjects, ReloadedSubPrefabs);
		ULexWidget* ReloadedChild = IsValid(ReloadedRoot) && ReloadedRoot->GetChildrenCount() == 1
			? ReloadedRoot->GetChildByIndex(0) : nullptr;
		TestNotNull(TEXT("Reloaded child exists"), ReloadedChild);
		if (ReloadedChild)
		{
			TestEqual(TEXT("Asset persisted the AUTHORED rect, not the arranged one"),
				ReloadedChild->GetSize(), FVector2D(120.0, 80.0));
			ULexPanelSlot* ReloadedSlot = ReloadedChild->GetPanelSlot();
			TestNotNull(TEXT("Reloaded slot exists"), ReloadedSlot);
			if (ReloadedSlot)
			{
				TestFalse(TEXT("Persisted slot reads 'nothing applied' so the first layout pass re-arranges"),
					ReloadedSlot->HasLayoutGeometryApplied());
			}
		}
		if (IsValid(ReloadedRoot))
		{
			ReloadedRoot->DestroyWidget();
		}
		ReloadWorld->DestroyWorld(false);
	}

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexResaveIsByteStableTest,
	"LGUI.Prefab.AuthoredGeometry.ResaveIsByteStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexResaveIsByteStableTest::RunTest(const FString& Parameters)
{
	using namespace LexUIAuthoredGeometryTestLocal;
	using namespace LexUIPrefabSystem;
	FArrangedFixture Fixture;
	if (!Fixture.BuildAndArrange())
	{
		AddError(TEXT("Fixture failed to build"));
		return false;
	}

	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Fixture.Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("First save succeeds"),
		WidgetSerializer::SavePrefab(Fixture.Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));
	const TArray<uint8> FirstPayload = Prefab->BinaryData;

	// Re-arrange (same inputs, same results) and save again: with arranged geometry excluded from the
	// payload, an untouched hierarchy re-saves to the identical bytes — the "prefab always diffs" churn
	// class is gone.
	ULexWidget::MarkLayoutForRebuild(Fixture.Root);
	ULexWidget::RebuildLayoutImmediately(Fixture.Root);
	TestTrue(TEXT("Second save succeeds"),
		WidgetSerializer::SavePrefab(Fixture.Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));
	TestTrue(TEXT("An untouched hierarchy re-saves byte-identical"), FirstPayload == Prefab->BinaryData);

	Fixture.Root->DestroyWidget();
	return true;
}

#endif
