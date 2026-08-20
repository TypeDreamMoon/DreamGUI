// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamScrollBoxInputHandler.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabSystem/DreamUIPrefabInstanceScene.h"
#include "PrefabSystem/WidgetSerializer.h"

namespace DreamScrollBoxPrefabLoadTestLocal
{
	// A ScrollBox-layout widget with children, followed by a sibling subtree the loader still has to
	// walk after the ScrollBox registers — the shape most exposed to mid-registration mutation.
	struct FScrollBoxPrefabFixture
	{
		UWorld* SeedWorld = nullptr;
		UDreamUIPrefab* Prefab = nullptr;

		bool BuildAndSave()
		{
			SeedWorld = UWorld::CreateWorld(EWorldType::None, false);
			if (!SeedWorld)
			{
				return false;
			}
			Prefab = NewObject<UDreamUIPrefab>();
			UDreamWidget* Root = NewObject<UDreamWidget>(SeedWorld, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* ScrollPanel = NewObject<UDreamWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* ItemA = NewObject<UDreamWidget>(ScrollPanel, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* ItemB = NewObject<UDreamWidget>(ScrollPanel, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* TrailingPanel = NewObject<UDreamWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			UDreamWidget* TrailingChild = NewObject<UDreamWidget>(TrailingPanel, NAME_None, RF_Public | RF_Transactional);
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
			if (!ScrollPanel->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>())
			{
				return false;
			}

			TMap<UObject*, FGuid> ObjectToGuid;
			ObjectToGuid.Add(Root, FGuid::NewGuid());
			TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> EmptySubPrefabs;
			return DreamUIPrefabSystem::WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true);
		}

		~FScrollBoxPrefabFixture()
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

	void CollectSubtree(UDreamWidget* Widget, TArray<UDreamWidget*>& OutWidgets)
	{
		if (!IsValid(Widget))
		{
			return;
		}
		OutWidgets.Add(Widget);
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			CollectSubtree(Child, OutWidgets);
		}
	}

	bool VerifySubtreeSurvives(FAutomationTestBase& Test, UDreamWidget* LoadedRoot, const FString& PathContext)
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
			UDreamWidget* Found = FindByDisplayName(LoadedRoot, Name);
			Test.TestNotNull(*FString::Printf(TEXT("%s: '%s' survives the load"), *PathContext, Name), Found);
			bAllPresent &= Found != nullptr;
		}
		TArray<UDreamWidget*> AllWidgets;
		CollectSubtree(LoadedRoot, AllWidgets);
		Test.TestEqual(*FString::Printf(TEXT("%s: subtree keeps every widget"), *PathContext), AllWidgets.Num(), 6);

		UDreamWidget* ScrollPanel = FindByDisplayName(LoadedRoot, TEXT("SidebarBand"));
		UDreamLayoutContainerScrollBox* ScrollBox = IsValid(ScrollPanel)
			? Cast<UDreamLayoutContainerScrollBox>(ScrollPanel->GetLayoutContainer()) : nullptr;
		Test.TestNotNull(*FString::Printf(TEXT("%s: ScrollBox layout survives the load"), *PathContext), ScrollBox);
		if (IsValid(ScrollPanel))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: ScrollBox keeps both children"), *PathContext),
				ScrollPanel->GetChildrenCount(), 2);
			// Registration must never grow the widget's Components array: the loader may still be walking
			// it. The input companion only appears once BeginPlay runs.
			Test.TestNull(*FString::Printf(TEXT("%s: registration creates no input handler"), *PathContext),
				ScrollPanel->GetComponent<UDreamScrollBoxInputHandler>());
		}
		return bAllPresent && IsValid(ScrollPanel);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxRuntimeLoadKeepsSubtreeTest,
	"DreamGUI.Prefab.ScrollBoxLoad.RuntimeLoadInGameWorldKeepsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxRuntimeLoadKeepsSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxPrefabLoadTestLocal;
	FScrollBoxPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Game world created"), GameWorld);
	if (!GameWorld)
	{
		return false;
	}
	TestTrue(TEXT("Runtime load world is a game world"), GameWorld->IsGameWorld());

	UDreamWidget* LoadedRoot = Fixture.Prefab->LoadPrefab(GameWorld, nullptr);
	const bool bSubtreeIntact = VerifySubtreeSurvives(*this, LoadedRoot, TEXT("Runtime LoadPrefab"));

	if (bSubtreeIntact)
	{
		// The world has not begun play, so LoadPrefab registered the widgets without BeginPlay. Begin play
		// the loaded tree the same way the deserializer and the manager do — per widget, parent first.
		TArray<UDreamWidget*> AllWidgets;
		CollectSubtree(LoadedRoot, AllWidgets);
		for (UDreamWidget* Widget : AllWidgets)
		{
			Widget->BeginPlay();
		}

		UDreamWidget* ScrollPanel = FindByDisplayName(LoadedRoot, TEXT("SidebarBand"));
		UDreamScrollBoxInputHandler* Handler = ScrollPanel->GetComponent<UDreamScrollBoxInputHandler>();
		TestNotNull(TEXT("BeginPlay creates the input handler"), Handler);
		if (Handler)
		{
			TestTrue(TEXT("The handler drives the loaded ScrollBox layout"),
				Handler->TargetLayout.Get() == ScrollPanel->GetLayoutContainer());
		}
		TestNotNull(TEXT("BeginPlay gives the hit-test visual"), ScrollPanel->GetVisual());

		TArray<UDreamWidget*> WidgetsAfterBeginPlay;
		CollectSubtree(LoadedRoot, WidgetsAfterBeginPlay);
		TestEqual(TEXT("BeginPlay leaves the subtree intact"), WidgetsAfterBeginPlay.Num(), 6);

		LoadedRoot->DestroyWidget();
	}
	GameWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxHelperLoadKeepsSubtreeTest,
	"DreamGUI.Prefab.ScrollBoxLoad.EditorHelperLoadInGameWorldKeepsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxHelperLoadKeepsSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxPrefabLoadTestLocal;
	FScrollBoxPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	// The helper load registers widgets while it is still walking the hierarchy, so it is the path most
	// exposed to mid-registration mutation. A non-editor prefab scene gives a GamePreview world, which
	// IsGameWorld() reports true for — the world class that arms companion creation.
	FDreamUIPrefabScene::ConstructionValues CVS;
	CVS.SetEditor(false);
	CVS.SetTransactional(false);
	FDreamUIPrefabInstanceScene Scene(CVS);
	TestNotNull(TEXT("Prefab scene world created"), Scene.GetWorld());
	if (!Scene.GetWorld())
	{
		return false;
	}
	TestTrue(TEXT("Helper load world is a game world"), Scene.GetWorld()->IsGameWorld());

	UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>();
	Helper->Init(Fixture.Prefab, &Scene);
	VerifySubtreeSurvives(*this, Helper->LoadedRootWidget, TEXT("Helper Init"));

	Helper->ClearLoadedPrefab();
	return true;
}

#endif
