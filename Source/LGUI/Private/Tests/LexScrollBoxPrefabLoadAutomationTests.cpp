// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexScrollBoxInputHandler.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/LexUIPrefabInstanceScene.h"
#include "PrefabSystem/WidgetSerializer.h"

namespace LexScrollBoxPrefabLoadTestLocal
{
	// A ScrollBox-layout widget with children, followed by a sibling subtree the loader still has to
	// walk after the ScrollBox registers — the shape most exposed to mid-registration mutation.
	struct FScrollBoxPrefabFixture
	{
		UWorld* SeedWorld = nullptr;
		ULexUIPrefab* Prefab = nullptr;

		bool BuildAndSave()
		{
			SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
			if (!SeedWorld)
			{
				return false;
			}
			Prefab = NewObject<ULexUIPrefab>();
			ULexWidget* Root = NewObject<ULexWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional);
			ULexWidget* ScrollPanel = NewObject<ULexWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			ULexWidget* ItemA = NewObject<ULexWidget>(ScrollPanel, NAME_None, RF_Public | RF_Transactional);
			ULexWidget* ItemB = NewObject<ULexWidget>(ScrollPanel, NAME_None, RF_Public | RF_Transactional);
			ULexWidget* TrailingPanel = NewObject<ULexWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			ULexWidget* TrailingChild = NewObject<ULexWidget>(TrailingPanel, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("ScrollRoot"));
			ScrollPanel->SetDisplayName(TEXT("SidebarBand"));
			ItemA->SetDisplayName(TEXT("ScrollItemA"));
			ItemB->SetDisplayName(TEXT("ScrollItemB"));
			TrailingPanel->SetDisplayName(TEXT("TrailingPanel"));
			TrailingChild->SetDisplayName(TEXT("TrailingChild"));
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			ScrollPanel->SetWidth(120.0f);
			ScrollPanel->SetHeight(300.0f);
			ItemA->SetWidth(120.0f);
			ItemA->SetHeight(80.0f);
			ItemB->SetWidth(120.0f);
			ItemB->SetHeight(80.0f);
			TrailingPanel->SetWidth(280.0f);
			TrailingPanel->SetHeight(300.0f);
			TrailingChild->SetWidth(100.0f);
			TrailingChild->SetHeight(100.0f);
			if (!ScrollPanel->TrySetParent(Root, false)
				|| !ItemA->TrySetParent(ScrollPanel, false)
				|| !ItemB->TrySetParent(ScrollPanel, false)
				|| !TrailingPanel->TrySetParent(Root, false)
				|| !TrailingChild->TrySetParent(TrailingPanel, false))
			{
				return false;
			}
			if (!ScrollPanel->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>())
			{
				return false;
			}

			TMap<UObject*, FGuid> ObjectToGuid;
			ObjectToGuid.Add(Root, FGuid::NewGuid());
			TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
			return LexUIPrefabSystem::WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true);
		}

		~FScrollBoxPrefabFixture()
		{
			if (SeedWorld)
			{
				SeedWorld->DestroyWorld(false);
			}
		}
	};

	ULexWidget* FindByDisplayName(ULexWidget* Widget, const FString& DisplayName)
	{
		if (!IsValid(Widget))
		{
			return nullptr;
		}
		if (Widget->GetDisplayName() == DisplayName)
		{
			return Widget;
		}
		for (ULexWidget* Child : Widget->GetChildren())
		{
			if (ULexWidget* Found = FindByDisplayName(Child, DisplayName))
			{
				return Found;
			}
		}
		return nullptr;
	}

	void CollectSubtree(ULexWidget* Widget, TArray<ULexWidget*>& OutWidgets)
	{
		if (!IsValid(Widget))
		{
			return;
		}
		OutWidgets.Add(Widget);
		for (ULexWidget* Child : Widget->GetChildren())
		{
			CollectSubtree(Child, OutWidgets);
		}
	}

	bool VerifySubtreeSurvives(FAutomationTestBase& Test, ULexWidget* LoadedRoot, const FString& PathContext)
	{
		Test.TestNotNull(*FString::Printf(TEXT("%s: root widget loads"), *PathContext), LoadedRoot);
		if (!IsValid(LoadedRoot))
		{
			return false;
		}
		bool bAllPresent = true;
		for (const TCHAR* Name : { TEXT("SidebarBand"), TEXT("ScrollItemA"), TEXT("ScrollItemB"),
			TEXT("TrailingPanel"), TEXT("TrailingChild") })
		{
			ULexWidget* Found = FindByDisplayName(LoadedRoot, Name);
			Test.TestNotNull(*FString::Printf(TEXT("%s: '%s' survives the load"), *PathContext, Name), Found);
			bAllPresent &= Found != nullptr;
		}
		TArray<ULexWidget*> AllWidgets;
		CollectSubtree(LoadedRoot, AllWidgets);
		Test.TestEqual(*FString::Printf(TEXT("%s: subtree keeps every widget"), *PathContext), AllWidgets.Num(), 6);

		ULexWidget* ScrollPanel = FindByDisplayName(LoadedRoot, TEXT("SidebarBand"));
		ULexLayoutContainerScrollBox* ScrollBox = IsValid(ScrollPanel)
			? Cast<ULexLayoutContainerScrollBox>(ScrollPanel->GetLayoutContainer()) : nullptr;
		Test.TestNotNull(*FString::Printf(TEXT("%s: ScrollBox layout survives the load"), *PathContext), ScrollBox);
		if (IsValid(ScrollPanel))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: ScrollBox keeps both children"), *PathContext),
				ScrollPanel->GetChildrenCount(), 2);
			// Registration must never grow the widget's Components array: the loader may still be walking
			// it. The input companion only appears once BeginPlay runs.
			Test.TestNull(*FString::Printf(TEXT("%s: registration creates no input handler"), *PathContext),
				ScrollPanel->GetComponent<ULexScrollBoxInputHandler>());
		}
		return bAllPresent && IsValid(ScrollPanel);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxRuntimeLoadKeepsSubtreeTest,
	"LGUI.Prefab.ScrollBoxLoad.RuntimeLoadInGameWorldKeepsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxRuntimeLoadKeepsSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxPrefabLoadTestLocal;
	FScrollBoxPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Game world created"), GameWorld);
	if (!GameWorld)
	{
		return false;
	}
	TestTrue(TEXT("Runtime load world is a game world"), GameWorld->IsGameWorld());

	ULexWidget* LoadedRoot = Fixture.Prefab->LoadPrefab(GameWorld, nullptr);
	const bool bSubtreeIntact = VerifySubtreeSurvives(*this, LoadedRoot, TEXT("Runtime LoadPrefab"));

	if (bSubtreeIntact)
	{
		// The world has not begun play, so LoadPrefab registered the widgets without BeginPlay. Begin play
		// the loaded tree the same way the deserializer and the manager do — per widget, parent first.
		TArray<ULexWidget*> AllWidgets;
		CollectSubtree(LoadedRoot, AllWidgets);
		for (ULexWidget* Widget : AllWidgets)
		{
			Widget->BeginPlay();
		}

		ULexWidget* ScrollPanel = FindByDisplayName(LoadedRoot, TEXT("SidebarBand"));
		ULexScrollBoxInputHandler* Handler = ScrollPanel->GetComponent<ULexScrollBoxInputHandler>();
		TestNotNull(TEXT("BeginPlay creates the input handler"), Handler);
		if (Handler)
		{
			TestTrue(TEXT("The handler drives the loaded ScrollBox layout"),
				Handler->TargetLayout.Get() == ScrollPanel->GetLayoutContainer());
		}
		TestNotNull(TEXT("BeginPlay gives the hit-test visual"), ScrollPanel->GetVisual());

		TArray<ULexWidget*> WidgetsAfterBeginPlay;
		CollectSubtree(LoadedRoot, WidgetsAfterBeginPlay);
		TestEqual(TEXT("BeginPlay leaves the subtree intact"), WidgetsAfterBeginPlay.Num(), 6);

		LoadedRoot->DestroyWidget();
	}
	GameWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxHelperLoadKeepsSubtreeTest,
	"LGUI.Prefab.ScrollBoxLoad.EditorHelperLoadInGameWorldKeepsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxHelperLoadKeepsSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxPrefabLoadTestLocal;
	FScrollBoxPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	// The helper load registers widgets while it is still walking the hierarchy, so it is the path most
	// exposed to mid-registration mutation. A non-editor prefab scene gives a GamePreview world, which
	// IsGameWorld() reports true for — the world class that arms companion creation.
	FLexUIPrefabScene::ConstructionValues CVS;
	CVS.SetEditor(false);
	CVS.SetTransactional(false);
	FLexUIPrefabInstanceScene Scene(CVS);
	TestNotNull(TEXT("Prefab scene world created"), Scene.GetWorld());
	if (!Scene.GetWorld())
	{
		return false;
	}
	TestTrue(TEXT("Helper load world is a game world"), Scene.GetWorld()->IsGameWorld());

	ULexUIPrefabHelperObject* Helper = NewObject<ULexUIPrefabHelperObject>();
	Helper->Init(Fixture.Prefab, &Scene);
	VerifySubtreeSurvives(*this, Helper->LoadedRootWidget, TEXT("Helper Init"));

	Helper->ClearLoadedPrefab();
	return true;
}

#endif
