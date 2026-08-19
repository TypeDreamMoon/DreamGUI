// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Editor.h"
#include "Engine/World.h"
#include "PrefabEditor/LexUIPrefabOverridesViewer.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"

// The overrides panel's per-row "Revert all on this object" used to call RevertAllPrefabOverride,
// which is not per-object at all: it walks every pinned object of the instance and finishes with
// RemoveAllMemberPropertyFromSubPrefab(Root, InIncludeRootTransform = true), so it also drops the
// instance root's Relative Location/Rotation/Scale -- where the user dragged the instance. The
// panel then repainted to "(no pinned overrides)", so nothing on screen said anything had been
// lost. The two tests below are a pair: one pins the per-object contract, the other pins the
// instance-wide behaviour it must not be confused with.
namespace LexPrefabOverridesTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/**
	 * One sub-prefab instance with two pinned objects: the instance root, pinned on the transform
	 * that says where it sits, and a child pinned on an ordinary property.
	 *
	 * Nothing here is loaded from a .uasset. The revert machinery only ever reads the guid maps and
	 * SubPrefabMap, so hand-built widgets standing in for both sides of the instance/source pair
	 * exercise the same code the panel does. The one thing that does need to be real is the
	 * sub-prefab asset: ULexUIPrefab hands out a helper object only once its version says its
	 * binary is readable, which is what the serialize below is for.
	 */
	struct FSubPrefabFixture
	{
		FScopedTestWorld TestWorld;
		ULexUIPrefab* SubPrefabAsset = nullptr;
		ULexUIPrefabHelperObject* SubHelper = nullptr;
		ULexUIPrefabHelperObject* Helper = nullptr;
		ULexWidget* InstanceRoot = nullptr;
		ULexWidget* InstanceChild = nullptr;
		ULexWidget* SourceRoot = nullptr;
		ULexWidget* SourceChild = nullptr;
		bool bSerialized = false;

		/** Where the user put this instance. Reverting one child property must not touch it. */
		const FVector PlacedLocation = FVector(120.0f, 340.0f, 560.0f);
		/** Where the sub-prefab asset itself puts its root, so "reverted" is distinguishable from "zero". */
		const FVector SourceLocation = FVector(-11.0f, 22.0f, -33.0f);

		FSubPrefabFixture()
		{
			SubPrefabAsset = NewObject<ULexUIPrefab>();
			ULexWidget* AssetRoot = MakeWidget(TEXT("AssetRoot"));
			AssetRoot->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
			TMap<UObject*, FGuid> ObjectToGuid;
			ObjectToGuid.Add(AssetRoot, FGuid::NewGuid());
			TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> NoSubPrefabs;
			bSerialized = LexUIPrefabSystem::WidgetSerializer::SavePrefab(AssetRoot, SubPrefabAsset, ObjectToGuid, NoSubPrefabs, true);
			SubHelper = SubPrefabAsset->GetPrefabHelperObject();
			if (SubHelper == nullptr)
			{
				return;
			}

			Helper = NewObject<ULexUIPrefabHelperObject>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);

			InstanceRoot = MakeWidget(TEXT("InstanceRoot"));
			InstanceRoot->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
			InstanceRoot->SetRelativeLocation(PlacedLocation);
			InstanceChild = MakeWidget(TEXT("InstanceChild"));
			InstanceChild->TrySetParent(InstanceRoot, false);

			SourceRoot = MakeWidget(TEXT("SourceRoot"));
			SourceRoot->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
			// Distinct from PlacedLocation and from the zero vector: comparing the instance against
			// a source that was never positioned proves nothing, because both sides would read
			// FVector::ZeroVector whether or not anything was copied.
			SourceRoot->SetRelativeLocation(SourceLocation);
			SourceChild = MakeWidget(TEXT("SourceChild"));
			SourceChild->TrySetParent(SourceRoot, false);

			const FGuid RootGuidInParent = FGuid::NewGuid();
			const FGuid ChildGuidInParent = FGuid::NewGuid();
			const FGuid RootGuidInSource = FGuid::NewGuid();
			const FGuid ChildGuidInSource = FGuid::NewGuid();

			Helper->MapGuidToObject.Add(RootGuidInParent, InstanceRoot);
			Helper->MapGuidToObject.Add(ChildGuidInParent, InstanceChild);
			SubHelper->MapGuidToObject.Add(RootGuidInSource, SourceRoot);
			SubHelper->MapGuidToObject.Add(ChildGuidInSource, SourceChild);

			FLexUISubPrefabData Data;
			Data.PrefabAsset = SubPrefabAsset;
			Data.MapGuidToObject.Add(RootGuidInParent, InstanceRoot);
			Data.MapGuidToObject.Add(ChildGuidInParent, InstanceChild);
			Data.MapObjectGuidFromParentPrefabToSubPrefab.Add(RootGuidInParent, RootGuidInSource);
			Data.MapObjectGuidFromParentPrefabToSubPrefab.Add(ChildGuidInParent, ChildGuidInSource);
			Data.AddMemberProperty(InstanceRoot, ULexWidget::GetPropertyName_RelativeLocation());
			Data.AddMemberProperty(InstanceChild, ULexWidget::GetPropertyName_DisplayName());
			Helper->SubPrefabMap.Add(InstanceRoot, MoveTemp(Data));
		}
		~FSubPrefabFixture()
		{
			if (SubHelper != nullptr)
			{
				SubHelper->ClearLoadedPrefab();
			}
			if (SubPrefabAsset != nullptr)
			{
				SubPrefabAsset->ClearPrefabInstanceScene();
			}
		}

		bool IsUsable() const { return bSerialized && SubHelper != nullptr && Helper != nullptr; }

		ULexWidget* MakeWidget(const TCHAR* InDisplayName)
		{
			ULexWidget* Widget = NewObject<ULexWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(InDisplayName);
			Widget->SetWidth(200.0f);
			Widget->SetHeight(100.0f);
			return Widget;
		}

		/** What the panel would still list for one object; empty once that object has nothing pinned. */
		TArray<FName> PinnedNames(UObject* InObject) const
		{
			if (const FLexUISubPrefabData* Data = Helper->SubPrefabMap.Find(InstanceRoot))
			{
				for (const FLexUIPrefabOverrideParameterData& Override : Data->ObjectOverrideParameterArray)
				{
					if (Override.Object.Get() == InObject)
					{
						return Override.MemberPropertyNames;
					}
				}
			}
			return TArray<FName>();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexOverridesPerObjectRevertTest,
	"LGUI.Editor.PrefabOverrides.PerObjectRevertLeavesTheInstanceRootAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexOverridesPerObjectRevertTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabOverridesTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; the revert path opens one and this test cannot say anything"));
		return false;
	}
	FSubPrefabFixture Fixture;
	if (!TestTrue(TEXT("the fixture instance was built"), Fixture.IsUsable()))return false;

	SLexUIPrefabOverridesViewer::RevertOverridesOnObject(
		Fixture.Helper, Fixture.InstanceChild, Fixture.PinnedNames(Fixture.InstanceChild));

	// The row that was clicked did what it says.
	TestEqual(TEXT("the child's override is gone"), Fixture.PinnedNames(Fixture.InstanceChild).Num(), 0);
	TestEqual(TEXT("and its value came back from the sub-prefab asset"),
		Fixture.InstanceChild->GetDisplayName(), Fixture.SourceChild->GetDisplayName());

	// And nothing else did. This is the whole bug: an instance-wide revert here would have taken
	// the root's transform out of the table and moved the instance back to the asset's origin.
	const TArray<FName> RootNames = Fixture.PinnedNames(Fixture.InstanceRoot);
	TestTrue(TEXT("the instance root is still pinned on its location"),
		RootNames.Contains(ULexWidget::GetPropertyName_RelativeLocation()));
	TestEqual(TEXT("and the instance has not moved"),
		Fixture.InstanceRoot->GetRelativeLocation(), Fixture.PlacedLocation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexOverridesWholeInstanceRevertTest,
	"LGUI.Editor.PrefabOverrides.WholeInstanceRevertDropsTheRootTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexOverridesWholeInstanceRevertTest::RunTest(const FString& Parameters)
{
	using namespace LexPrefabOverridesTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; the revert path opens one and this test cannot say anything"));
		return false;
	}
	FSubPrefabFixture Fixture;
	if (!TestTrue(TEXT("the fixture instance was built"), Fixture.IsUsable()))return false;

	// Same fixture, the other API. Without this the per-object test above could pass against a
	// revert that quietly does nothing at all, and there would be nothing to say why the panel's
	// row menu must not reach for this one.
	Fixture.Helper->RevertAllPrefabOverride(Fixture.InstanceChild);

	TestEqual(TEXT("every object of the instance is un-pinned, not just the one asked for"),
		Fixture.PinnedNames(Fixture.InstanceRoot).Num(), 0);
	TestEqual(TEXT("including the child"), Fixture.PinnedNames(Fixture.InstanceChild).Num(), 0);
	TestEqual(TEXT("and the instance snaps back to where the sub-prefab asset puts it"),
		Fixture.InstanceRoot->GetRelativeLocation(), Fixture.SourceRoot->GetRelativeLocation());
	return true;
}

#endif
