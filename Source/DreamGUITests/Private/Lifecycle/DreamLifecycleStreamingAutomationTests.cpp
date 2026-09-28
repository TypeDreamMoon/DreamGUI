// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamOnDiskFixture.h"
#include "DreamUIBPLibrary.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A LEVEL STREAMED IN AND OUT.
 *
 * A panel placed in a sublevel is registered when its level comes into the world and unregistered when the level
 * leaves, and neither is a destruction. In the level editor, hiding a sublevel keeps its actors -- only their
 * components leave the world -- and showing it again registers them anew; in a playing world a level streamed out
 * ends play for its actors and is then collected with everything in it. Either way the trees the level's hosts built
 * go with the level, and a level that comes back builds them afresh: one tree for each host, never an old one, and
 * nothing left for the collector to find still registered.
 *
 * The level holds what a designer puts in one -- a panel drawn by DreamUI's renderer and one drawn by the engine's,
 * saved to disk and read back through the serializer -- and a render-target surface that a level's script sets up
 * at run time, showing a canvas the world holds: the level's to take down, the canvas not.
 */
namespace DreamLifecycleStreamingTestLocal
{
	using namespace DreamTests::Lifecycle;

	const int32 EditorRounds = 10;
	const int32 PlayRounds = 3;

	void Settle(bool bInFlush)
	{
		if (bInFlush)
		{
			FlushRenderingCommands();
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		if (bInFlush)
		{
			FlushRenderingCommands();
		}
	}

	/** The registered hierarchy roots of InWorld. */
	TArray<UDreamWidget*> RegisteredRoots(UWorld* InWorld)
	{
		TArray<UDreamWidget*> Roots;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			for (UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				if (Widget->GetParent() == nullptr)
				{
					Roots.Add(Widget);
				}
			}
		}
		return Roots;
	}

	/** The object a root is held by: the outer of the tree it roots, or its own outer. */
	const UObject* HolderOf(const UDreamWidget* InRoot)
	{
		const UObject* Owner = InRoot != nullptr ? InRoot->GetOuter() : nullptr;
		if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Owner); Tree != nullptr && Tree->RootWidget == InRoot)
		{
			Owner = Tree->GetOuter();
		}
		return Owner;
	}

	/** The panels placed in InLevel. */
	TArray<UDreamWorldWidgetComponent*> FindHosts(const ULevel* InLevel)
	{
		TArray<UDreamWorldWidgetComponent*> Hosts;
		if (InLevel != nullptr)
		{
			for (const AActor* Actor : InLevel->Actors)
			{
				if (const ADreamWorldWidgetActor* Panel = Cast<ADreamWorldWidgetActor>(Actor); IsValid(Panel) && Panel->GetWidgetComponent() != nullptr)
				{
					Hosts.Add(Panel->GetWidgetComponent());
				}
			}
		}
		return Hosts;
	}

	/**
	 * The level a designer made to stream, saved to disk: a panel drawn by DreamUI's renderer and one drawn by the
	 * engine's. Made as the level editor's New Level makes one -- a world of its own package, inactive
	 * (UWorldFactory) -- so nothing in it builds: a host builds its tree only in a world that plays or is edited.
	 */
	bool SaveStreamedLevel(DreamOnDiskFixture::FScopedOnDiskPackage& InMap, UClass* InPanelClass, FString& OutError)
	{
		UWorld::InitializationValues Values;
		Values.ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateNavigation(false).CreateAISystem(false).AllowAudioPlayback(false);
		UWorld* Level = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld*/ false, FName(*InMap.AssetName), InMap.Package,
			/*bAddToRoot*/ false, ERHIFeatureLevel::Num, &Values);
		if (Level == nullptr)
		{
			OutError = TEXT("no world was made for the level");
			return false;
		}
		const EDreamWorldWidgetBackend Backends[] = { EDreamWorldWidgetBackend::DreamUIRenderer, EDreamWorldWidgetBackend::UERenderer };
		const int32 BackendCount = UE_ARRAY_COUNT(Backends);
		bool bPlaced = true;
		for (int32 Index = 0; Index < BackendCount; ++Index)
		{
			ADreamWorldWidgetActor* Panel = Level->SpawnActor<ADreamWorldWidgetActor>(FVector(0.0, 300.0 * Index, 150.0), FRotator::ZeroRotator);
			if (Panel == nullptr || Panel->GetWidgetComponent() == nullptr)
			{
				bPlaced = false;
				break;
			}
			Panel->GetWidgetComponent()->SetWidgetClass(InPanelClass);
			Panel->GetWidgetComponent()->SetBackend(Backends[Index]);
		}
		const bool bSaved = bPlaced ? InMap.Save(Level, OutError) : false;
		if (!bPlaced)
		{
			OutError = TEXT("a panel was not placed in the level");
		}
		// Let go of as the level editor lets go of a level it has saved and closed.
		Level->DestroyWorld(/*bInformEngineOfWorld*/ false);
		Level->ClearFlags(RF_Standalone);
		return bSaved;
	}

	/** InLevelPackage streamed into InWorld as the level editor's Add Existing Level adds one: always loaded, and shown. */
	ULevelStreaming* AddSublevel(UWorld* InWorld, const FString& InLevelPackage)
	{
		// UEditorLevelUtils::AddLevelToWorld without its dialog and its undo record: the record holds the streaming level,
		// and through it this test's world, for as long as the editor keeps its undo history.
		ULevelStreaming* Streaming = NewObject<ULevelStreamingAlwaysLoaded>(InWorld, NAME_None, RF_Transient);
		Streaming->SetWorldAssetByPackageName(FName(*InLevelPackage));
		Streaming->SetShouldBeVisibleInEditor(true);
		InWorld->AddStreamingLevel(Streaming);
		InWorld->FlushLevelStreaming();
		return Streaming;
	}

	/**
	 * A render-target surface put in InLevel as a level's script puts one there at run time: an actor in the level
	 * whose surface shows a canvas the world holds -- a free widget drawing to a render target, hung on the actor.
	 */
	UDreamUIRenderTargetGeometrySource* PlaceSurface(UWorld* InWorld, ULevel* InLevel, UDreamWidget*& OutRoot)
	{
		OutRoot = nullptr;
		FActorSpawnParameters Params;
		Params.OverrideLevel = InLevel;
		AActor* Actor = InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		if (Actor == nullptr)
		{
			return nullptr;
		}
		USceneComponent* Anchor = NewObject<USceneComponent>(Actor, TEXT("Anchor"), RF_Transactional);
		Actor->SetRootComponent(Anchor);
		Actor->AddInstanceComponent(Anchor);
		Anchor->RegisterComponent();

		UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(InWorld, TEXT("SurfaceCanvas"), nullptr);
		UDreamCanvas* Canvas = Root != nullptr ? Root->AddComponent<UDreamCanvas>() : nullptr;
		if (Canvas == nullptr)
		{
			if (Root != nullptr)
			{
				Root->DestroyWidget();
			}
			return nullptr;
		}
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Anchor);
		if (!InWorld->IsGameWorld())
		{
			// An edited world has nothing to poll the canvas with until it has its target, so it draws first; a playing
			// world's surface polls for the target itself.
			DrawFrames(InWorld, 2);
		}
		UDreamUIRenderTargetGeometrySource* Surface = NewObject<UDreamUIRenderTargetGeometrySource>(Actor, TEXT("Surface"), RF_Transactional);
		Surface->SetCanvas(Canvas);
		Surface->SetupAttachment(Anchor);
		Actor->AddInstanceComponent(Surface);
		Surface->RegisterComponent();
		OutRoot = Root;
		return Surface;
	}

	/** Whether InSurface shows its canvas's render target through a material instance of its own. */
	bool ShowsItsCanvas(const UDreamUIRenderTargetGeometrySource* InSurface)
	{
		const UDreamCanvas* Canvas = InSurface != nullptr ? InSurface->GetCanvas() : nullptr;
		return InSurface->IsRegistered() && InSurface->GetMaterialInstance() != nullptr && Canvas != nullptr && Canvas->GetRenderTarget() != nullptr;
	}

	/** Each of InHosts holds exactly one tree, registered and its own -- and none of InPrevious. OutTrees are those trees. */
	void ExpectEachHostBuilt(FAutomationTestBase& InTest, const FString& InWhen, UWorld* InWorld, const TArray<UDreamWorldWidgetComponent*>& InHosts,
		const TArray<TWeakObjectPtr<UDreamWidget>>& InPrevious, TArray<TWeakObjectPtr<UDreamWidget>>& OutTrees)
	{
		OutTrees.Reset();
		const TArray<UDreamWidget*> Roots = RegisteredRoots(InWorld);
		for (const UDreamWorldWidgetComponent* Host : InHosts)
		{
			UDreamWidget* Tree = Host != nullptr ? Host->GetLoadedWidget() : nullptr;
			const FString Who = FString::Printf(TEXT("%s: the panel %s"), *InWhen, *GetNameSafe(Host != nullptr ? Host->GetOwner() : nullptr));
			InTest.TestTrue(Who + TEXT(" holds its tree, registered"), Tree != nullptr && Tree->HasRegistered() && HolderOf(Tree) == Host);
			InTest.TestEqual(Who + TEXT(" holds exactly one"),
				Roots.FilterByPredicate([Host](const UDreamWidget* Root) { return HolderOf(Root) == Host; }).Num(), 1);
			InTest.TestFalse(Who + TEXT(" built it afresh, not a tree it held before"),
				Tree != nullptr && InPrevious.ContainsByPredicate([Tree](const TWeakObjectPtr<UDreamWidget>& Old) { return Old.Get(/*bEvenIfGarbage*/ true) == Tree; }));
			OutTrees.Add(Tree);
		}
	}

	/** None of InTrees is registered any more, and nothing registered in InWorld is held by one of InHosts. */
	void ExpectNoHostBuilt(FAutomationTestBase& InTest, const FString& InWhen, UWorld* InWorld,
		const TArray<TWeakObjectPtr<UDreamWorldWidgetComponent>>& InHosts, const TArray<TWeakObjectPtr<UDreamWidget>>& InTrees)
	{
		for (const TWeakObjectPtr<UDreamWidget>& Tree : InTrees)
		{
			InTest.TestFalse(InWhen + TEXT(": a tree the level's panels held is down"), Tree.IsValid() && Tree->HasRegistered());
		}
		for (const UDreamWidget* Root : RegisteredRoots(InWorld))
		{
			const UObject* Holder = HolderOf(Root);
			InTest.TestFalse(FString::Printf(TEXT("%s: %s, registered, is held by none of the level's panels"), *InWhen, *GetPathNameSafe(Root)),
				InHosts.ContainsByPredicate([Holder](const TWeakObjectPtr<UDreamWorldWidgetComponent>& Host) { return Host.Get(/*bEvenIfGarbage*/ true) == Holder; }));
		}
	}

	/** Every one of InObjects is gone: collected, not merely marked. */
	template<typename T>
	void ExpectCollected(FAutomationTestBase& InTest, const FString& InWhat, const TArray<TWeakObjectPtr<T>>& InObjects)
	{
		int32 Left = 0;
		for (const TWeakObjectPtr<T>& Object : InObjects)
		{
			Left += Object.IsValid(/*bEvenIfGarbage*/ true) ? 1 : 0;
		}
		InTest.TestEqual(InWhat + TEXT(": collected, as nothing held them"), Left, 0);
	}

	bool RunEditorRounds(FAutomationTestBase& InTest, bool bInFlush)
	{
		FScopedPanelClass Panel(bInFlush ? TEXT("StreamedPanelRhi") : TEXT("StreamedPanel"));
		DreamOnDiskFixture::FScopedOnDiskPackage Map(bInFlush ? TEXT("StreamedPanelsRhi") : TEXT("StreamedPanels"), /*bInIsMap*/ true);
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))
		{
			return false;
		}
		FString Error;
		const bool bSaved = SaveStreamedLevel(Map, Panel.GetClass(), Error);
		if (!InTest.TestTrue(FString::Printf(TEXT("the level was saved to disk (%s)"), *Error), bSaved))
		{
			return false;
		}
		// Read back, as every level the editor opens is: through the serializer.
		const TWeakObjectPtr<UWorld> Loaded = Cast<UWorld>(Map.Reload(Error));
		if (!InTest.TestTrue(FString::Printf(TEXT("and read back (%s)"), *Error), Loaded.IsValid()))
		{
			return false;
		}
		Settle(bInFlush);
		FLeakLogWatch Watch;
		{
			FScopedWorld Persistent(EWorldType::Editor);
			UWorld* World = Persistent.World;
			const ULevelStreaming* Streaming = AddSublevel(World, Map.PackageName);
			ULevel* SubLevel = Streaming != nullptr ? Streaming->GetLoadedLevel() : nullptr;
			if (!InTest.TestTrue(TEXT("The level is the world's sublevel, shown"), SubLevel != nullptr && SubLevel->bIsVisible && SubLevel == Loaded->PersistentLevel))
			{
				return false;
			}
			DrawFrames(World, 2);
			const TArray<UDreamWorldWidgetComponent*> Hosts = FindHosts(SubLevel);
			InTest.TestEqual(TEXT("It holds its two panels"), Hosts.Num(), 2);
			TArray<TWeakObjectPtr<UDreamWorldWidgetComponent>> WeakHosts;
			for (UDreamWorldWidgetComponent* Host : Hosts)
			{
				WeakHosts.Add(Host);
			}
			UDreamWidget* SurfaceRoot = nullptr;
			UDreamUIRenderTargetGeometrySource* Surface = PlaceSurface(World, SubLevel, SurfaceRoot);
			if (!InTest.TestTrue(TEXT("...and a render-target surface, showing a canvas the world holds"), Surface != nullptr && ShowsItsCanvas(Surface)))
			{
				return false;
			}
			TArray<TWeakObjectPtr<UDreamWidget>> Trees;
			ExpectEachHostBuilt(InTest, TEXT("Added"), World, Hosts, {}, Trees);

			for (int32 Round = 0; Round < EditorRounds; ++Round)
			{
				const FString Hidden = FString::Printf(TEXT("Round %d, hidden"), Round);
				UEditorLevelUtils::SetLevelVisibility(SubLevel, false, false, ELevelVisibilityDirtyMode::DontModify);
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
				InTest.TestFalse(Hidden + TEXT(": the sublevel is out of the world"), SubLevel->bIsVisible);
				for (const UDreamWorldWidgetComponent* Host : Hosts)
				{
					InTest.TestTrue(Hidden + TEXT(": its panels are still there, unregistered"), IsValid(Host) && !Host->IsRegistered());
					InTest.TestNull(Hidden + TEXT(": ...holding no tree"), IsValid(Host) ? Host->GetLoadedWidget() : nullptr);
				}
				ExpectNoHostBuilt(InTest, Hidden, World, WeakHosts, Trees);
				InTest.TestFalse(Hidden + TEXT(": the surface left the world with it"), Surface->IsRegistered());
				InTest.TestTrue(Hidden + TEXT(": the canvas it showed, the world's, stays"), IsValid(SurfaceRoot) && SurfaceRoot->HasRegistered());
				Settle(bInFlush);
				ExpectCollected(InTest, Hidden + TEXT(", then collected: the trees the panels let go"), Trees);
				InTest.TestEqual(Hidden + TEXT(": no dynamic texture without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));

				const FString Shown = FString::Printf(TEXT("Round %d, shown again"), Round);
				UEditorLevelUtils::SetLevelVisibility(SubLevel, true, false, ELevelVisibilityDirtyMode::DontModify);
				DrawFrames(World, 2);
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
				InTest.TestTrue(Shown + TEXT(": the sublevel is back in the world"), SubLevel->bIsVisible);
				const TArray<TWeakObjectPtr<UDreamWidget>> Before = Trees;
				ExpectEachHostBuilt(InTest, Shown, World, Hosts, Before, Trees);
				InTest.TestTrue(Shown + TEXT(": the surface is back, showing its canvas"), ShowsItsCanvas(Surface));
				InTest.TestEqual(Shown + TEXT(": no canvas mesh the level would save"), JoinLines(FindPersistentCanvasMeshes(World)), FString(TEXT("none")));
			}
			// The world goes with the sublevel in it, the canvas the world holds too: the world's own teardown.
		}
		Settle(bInFlush);
		InTest.TestEqual(TEXT("Nothing reached the collector still registered, the world's teardown included"), Watch.Describe(), FString(TEXT("none")));
		// Let go of as the level editor lets go of a sublevel it unloads.
		if (UWorld* LoadedWorld = Loaded.Get())
		{
			LoadedWorld->ClearFlags(RF_Standalone);
		}
		return true;
	}

	/** What the play rounds keep between steps: weakly, as the rig requires. */
	struct FPlayRound
	{
		TWeakObjectPtr<ULevelStreamingDynamic> Streaming;
		TWeakObjectPtr<ULevel> Level;
		TArray<TWeakObjectPtr<UDreamWorldWidgetComponent>> Hosts;
		TArray<TWeakObjectPtr<UDreamWidget>> Trees;
		TWeakObjectPtr<UDreamUIRenderTargetGeometrySource> Surface;
		TWeakObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
		TWeakObjectPtr<UDreamWidget> SurfaceRoot;
	};

	bool RunPlayRounds(FAutomationTestBase& InTest, bool bInFlush)
	{
		const TSharedRef<FScopedPanelClass> Panel = MakeShared<FScopedPanelClass>(bInFlush ? TEXT("StreamedPanelPieRhi") : TEXT("StreamedPanelPie"));
		const TSharedRef<DreamOnDiskFixture::FScopedOnDiskPackage> Map = MakeShared<DreamOnDiskFixture::FScopedOnDiskPackage>(
			bInFlush ? TEXT("StreamedPanelsPieRhi") : TEXT("StreamedPanelsPie"), /*bInIsMap*/ true);
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel->GetClass()))
		{
			return false;
		}
		FString Error;
		const bool bSaved = SaveStreamedLevel(*Map, Panel->GetClass(), Error);
		if (!InTest.TestTrue(FString::Printf(TEXT("the level was saved to disk (%s)"), *Error), bSaved))
		{
			return false;
		}
		const TSoftObjectPtr<UWorld> LevelAsset{ FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *Map->PackageName, *Map->AssetName)) };
		const TSharedRef<FLeakLogWatch> Watch = MakeShared<FLeakLogWatch>();
		const TSharedRef<FPlayRound> State = MakeShared<FPlayRound>();
		FAutomationTestBase* Test = &InTest;

		TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(InTest);
		Rig->Start();
		FDreamDriverSequence Steps = Rig->Sequence();
		for (int32 Round = 0; Round < PlayRounds; ++Round)
		{
			// In, as a level's script streams one in: an instance of the saved level, loaded and shown, and the surface
			// the script sets up in it.
			Steps.Then([Test, State, LevelAsset, Round, bInFlush](FDreamDriverContext& InContext)
			{
				*State = FPlayRound();
				bool bAsked = false;
				ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(InContext.World, LevelAsset,
					FTransform(FVector(0.0, 0.0, 500.0 * (Round + 1))), bAsked);
				if (!Test->TestTrue(FString::Printf(TEXT("Round %d: an instance of the level was asked for"), Round), bAsked && Streaming != nullptr))
				{
					return;
				}
				InContext.World->FlushLevelStreaming();
				ULevel* Level = Streaming->GetLoadedLevel();
				if (!Test->TestTrue(FString::Printf(TEXT("Round %d: ...streamed in and shown"), Round), Level != nullptr && Level->bIsVisible))
				{
					return;
				}
				State->Streaming = Streaming;
				State->Level = Level;
				for (UDreamWorldWidgetComponent* Host : FindHosts(Level))
				{
					State->Hosts.Add(Host);
				}
				UDreamWidget* SurfaceRoot = nullptr;
				State->Surface = PlaceSurface(InContext.World, Level, SurfaceRoot);
				State->SurfaceRoot = SurfaceRoot;
				Test->TestTrue(FString::Printf(TEXT("Round %d: ...with its surface set up"), Round), State->Surface.IsValid() && SurfaceRoot != nullptr);
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
			});
			Steps.WaitFrames(3);
			// Played a few frames, then out, as a level's script streams one out.
			Steps.Then([Test, State, Round, bInFlush](FDreamDriverContext& InContext)
			{
				const FString In = FString::Printf(TEXT("Round %d, streamed in"), Round);
				TArray<UDreamWorldWidgetComponent*> Hosts;
				for (const TWeakObjectPtr<UDreamWorldWidgetComponent>& Host : State->Hosts)
				{
					if (Host.IsValid())
					{
						Hosts.Add(Host.Get());
					}
				}
				Test->TestEqual(In + TEXT(": the level brought its two panels"), Hosts.Num(), 2);
				ExpectEachHostBuilt(*Test, In, InContext.World, Hosts, {}, State->Trees);
				UDreamUIRenderTargetGeometrySource* Surface = State->Surface.Get();
				Test->TestTrue(In + TEXT(": its surface shows the world's canvas"), Surface != nullptr && ShowsItsCanvas(Surface));
				State->SurfaceMaterial = Surface != nullptr ? Surface->GetMaterialInstance() : nullptr;

				if (ULevelStreamingDynamic* Streaming = State->Streaming.Get())
				{
					Streaming->SetIsRequestingUnloadAndRemoval(true);
				}
				InContext.World->FlushLevelStreaming();
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
				const FString Out = FString::Printf(TEXT("Round %d, streamed out"), Round);
				Test->TestFalse(Out + TEXT(": the level is out of the world"), State->Level.IsValid() && State->Level->bIsVisible);
				ExpectNoHostBuilt(*Test, Out, InContext.World, State->Hosts, State->Trees);
				Test->TestTrue(Out + TEXT(": the canvas its surface showed is the world's, and stays"), State->SurfaceRoot.IsValid() && State->SurfaceRoot->HasRegistered());
			});
			// A level streamed out is made ready for the collector as a world tick ends.
			Steps.WaitFrames(2);
			Steps.Then([Test, State, Round, bInFlush](FDreamDriverContext& InContext)
			{
				Settle(bInFlush);
				const FString Gone = FString::Printf(TEXT("Round %d, then collected"), Round);
				ExpectCollected(*Test, Gone + TEXT(": the trees the level's panels held"), State->Trees);
				ExpectCollected(*Test, Gone + TEXT(": the level's panels"), State->Hosts);
				Test->TestFalse(Gone + TEXT(": the level itself, collected"), State->Level.IsValid(/*bEvenIfGarbage*/ true));
				Test->TestFalse(Gone + TEXT(": its surface, and the material instance it made"),
					State->Surface.IsValid(/*bEvenIfGarbage*/ true) || State->SurfaceMaterial.IsValid(/*bEvenIfGarbage*/ true));
				Test->TestEqual(Gone + TEXT(": no dynamic texture without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
				UDreamWidget* SurfaceRoot = State->SurfaceRoot.Get();
				if (Test->TestTrue(Gone + TEXT(": the world's canvas outlived the level it hung in"), IsValid(SurfaceRoot) && SurfaceRoot->HasRegistered()))
				{
					SurfaceRoot->DestroyWidget();
				}
			});
		}
		// The class and the map on disk are held to the end, by the step that ends it.
		Steps.Then([Test, Watch, Panel, Map](FDreamDriverContext& InContext)
		{
			Test->TestEqual(TEXT("Nothing reached the collector still registered"), Watch->Describe(), FString(TEXT("none")));
		});
		Steps.PerformLatent();
		Rig->Finish();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStreamingEditorTest,
	"DreamGUI.Lifecycle.HidingAndShowingASublevelTakesItsPanelsTreesDownAndBuildsThemAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleStreamingEditorTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleStreamingTestLocal::RunEditorRounds(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStreamingEditorRhiTest,
	"DreamGUI.Lifecycle.RHI.HidingAndShowingASublevelTakesItsPanelsTreesDownAndBuildsThemAgainOnTheRenderThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleStreamingEditorRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleStreamingTestLocal::RunEditorRounds(*this, /*bInFlush*/ true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStreamingPieTest,
	"DreamGUI.Pie.ALevelStreamedIntoAPlaySessionAndOutAgainTakesItsTreesWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleStreamingPieTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleStreamingTestLocal::RunPlayRounds(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStreamingPieRhiTest,
	"DreamGUI.Pie.RHI.ALevelStreamedIntoAPlaySessionAndOutAgainTakesItsTreesWithItOnTheRenderThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleStreamingPieRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleStreamingTestLocal::RunPlayRounds(*this, /*bInFlush*/ true);
}

#endif
