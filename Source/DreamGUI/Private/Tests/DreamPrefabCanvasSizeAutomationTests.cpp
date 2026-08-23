// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"

// A prefab root is authored to stretch across the design canvas (the factory sets anchors 0..1 and a
// zero SizeDelta, and the prefab editor's root agent supplies the canvas). Loaded with no parent --
// a world-space presenter, LoadPrefab(World, nullptr) -- a stretched axis has nothing to stretch
// across and collapses to zero, and every child panel is then laid out into a 0x0 area. The asset's
// CanvasSize stands in for the missing parent on that path.

namespace DreamPrefabCanvasSizeTestLocal
{
	struct FCanvasPrefabFixture
	{
		UWorld* SeedWorld = nullptr;
		UDreamUIPrefab* Prefab = nullptr;

		/** Root (stretched, Overlay) > Box (SizeBox 1000x500) > Leaf. CanvasSize on the asset is 1000x500. */
		bool BuildAndSave(bool bStretchedRoot)
		{
			SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
			if (!SeedWorld)
			{
				return false;
			}
			Prefab = NewObject<UDreamUIPrefab>();
			Prefab->CanvasSize = FIntPoint(1000, 500);

			UDreamWidget* Root = NewObject<UDreamWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* Box = NewObject<UDreamWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* Leaf = NewObject<UDreamWidget>(Box, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("Root"));
			Box->SetDisplayName(TEXT("Box"));
			Leaf->SetDisplayName(TEXT("Leaf"));

			FDreamUIAnchorData RootAnchor = Root->GetAnchorData();
			if (bStretchedRoot)
			{
				RootAnchor.AnchorMin = FVector2D::ZeroVector;
				RootAnchor.AnchorMax = FVector2D(1.0, 1.0);
				RootAnchor.SizeDelta = FVector2D::ZeroVector;
			}
			else
			{
				RootAnchor.SizeDelta = FVector2D(640, 360);
			}
			RootAnchor.AnchoredPosition = FVector2D::ZeroVector;
			Root->SetAnchorData(RootAnchor);

			if (!Box->TrySetParent(Root, false) || !Leaf->TrySetParent(Box, false))
			{
				return false;
			}
			if (!Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
			{
				return false;
			}
			UDreamLayoutContainerSizeBox* SizeBox = Box->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
			if (!SizeBox)
			{
				return false;
			}
			SizeBox->SetOverrideWidth(true);
			SizeBox->SetWidthOverride(1000.0f);
			SizeBox->SetOverrideHeight(true);
			SizeBox->SetHeightOverride(500.0f);

			TMap<UObject*, FGuid> ObjectToGuid;
			ObjectToGuid.Add(Root, FGuid::NewGuid());
			TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
			return DreamUIPrefabSystem::WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true);
		}

		~FCanvasPrefabFixture()
		{
			if (SeedWorld)
			{
				SeedWorld->DestroyWorld(false);
			}
		}
	};

	UDreamWidget* FindByDisplayName(UDreamWidget* Widget, const FString& DisplayName)
	{
		if (!IsValid(Widget))
		{
			return nullptr;
		}
		if (Widget->GetDisplayName() == DisplayName)
		{
			return Widget;
		}
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (UDreamWidget* Found = FindByDisplayName(Child, DisplayName))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPrefabCanvasSizeParentlessLoadTest,
	"DreamGUI.Prefab.CanvasSize.AParentlessLoadSizesAStretchedRootToTheCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPrefabCanvasSizeParentlessLoadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabCanvasSizeTestLocal;
	FCanvasPrefabFixture Fixture;
	if (!TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave(true)))
	{
		return false;
	}
	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Game world created"), GameWorld))
	{
		return false;
	}

	UDreamWidget* Root = Fixture.Prefab->LoadPrefab(GameWorld, nullptr);
	if (TestNotNull(TEXT("Prefab loads"), Root))
	{
		TestEqual(TEXT("Root width is the canvas width"), Root->GetWidth(), 1000.0f);
		TestEqual(TEXT("Root height is the canvas height"), Root->GetHeight(), 500.0f);
		// The anchors collapse so the size is the root's own: re-parenting it later must not add a
		// parent width on top of the baked one.
		TestFalse(TEXT("The horizontal anchor no longer stretches"), Root->GetAnchorData().IsHorizontalStretched());
		TestFalse(TEXT("The vertical anchor no longer stretches"), Root->GetAnchorData().IsVerticalStretched());

		UDreamWidget::MarkLayoutForRebuild(Root);
		UDreamWidget::RebuildLayoutImmediately(Root);
		UDreamWidget* Box = FindByDisplayName(Root, TEXT("Box"));
		if (TestNotNull(TEXT("Box survives the round trip"), Box))
		{
			// This is the user-visible symptom: the Overlay handed the Size Box a 0x0 area and its
			// 1000x500 override was clamped to nothing, so text inside wrapped one glyph per line.
			TestEqual(TEXT("The Size Box gets its override width from the Overlay"), Box->GetWidth(), 1000.0f);
			TestEqual(TEXT("The Size Box gets its override height from the Overlay"), Box->GetHeight(), 500.0f);
		}
		Root->DestroyWidget();
	}
	GameWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPrefabCanvasSizeFixedRootTest,
	"DreamGUI.Prefab.CanvasSize.AFixedSizeRootKeepsItsAuthoredSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPrefabCanvasSizeFixedRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabCanvasSizeTestLocal;
	FCanvasPrefabFixture Fixture;
	if (!TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave(false)))
	{
		return false;
	}
	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Game world created"), GameWorld))
	{
		return false;
	}
	UDreamWidget* Root = Fixture.Prefab->LoadPrefab(GameWorld, nullptr);
	if (TestNotNull(TEXT("Prefab loads"), Root))
	{
		// The canvas only stands in where a parent would have mattered; an axis the author fixed is
		// the author's number, not the canvas's.
		TestEqual(TEXT("Authored width is kept"), Root->GetWidth(), 640.0f);
		TestEqual(TEXT("Authored height is kept"), Root->GetHeight(), 360.0f);
		Root->DestroyWidget();
	}
	GameWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPrefabCanvasSizeUnderParentTest,
	"DreamGUI.Prefab.CanvasSize.ALoadUnderAParentStillStretchesToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPrefabCanvasSizeUnderParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamPrefabCanvasSizeTestLocal;
	FCanvasPrefabFixture Fixture;
	if (!TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave(true)))
	{
		return false;
	}
	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Game world created"), GameWorld))
	{
		return false;
	}
	UDreamWidget* Parent = NewObject<UDreamWidget>(GameWorld);
	Parent->SetWidth(300.0f);
	Parent->SetHeight(200.0f);
	Parent->OnRegister();

	// A page or a nested prefab has a real parent; the canvas must stay out of that.
	UDreamWidget* Root = Fixture.Prefab->LoadPrefab(GameWorld, Parent);
	if (TestNotNull(TEXT("Prefab loads under the parent"), Root))
	{
		TestTrue(TEXT("The root still stretches"), Root->GetAnchorData().IsHorizontalStretched());
		TestEqual(TEXT("The root fills the parent, not the canvas"), Root->GetWidth(), 300.0f);
		TestEqual(TEXT("The root fills the parent height"), Root->GetHeight(), 200.0f);
	}
	Parent->DestroyWidget();
	GameWorld->DestroyWorld(false);
	return true;
}

#endif
