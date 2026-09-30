// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamScopedWorld.h"
#include "DreamUIBPLibrary.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "UObject/UObjectIterator.h"

#include <atomic>

/*
 * A WORLD DESTROYED WITH ITS TREES STILL IN IT LEAVES NOTHING BEHIND.
 *
 * Nobody destroys a widget tree before its world goes: a level editor closes a map, a play session ends,
 * a preview's owner goes away. Whatever the world held -- a placed panel's tree, a page on its screen, a
 * tree nobody hosts, a Blueprint's free widget -- has to come down on the world's own teardown path, before
 * the world's subsystems deinitialize and long before the collector; the collector reaching a tree still
 * registered is exactly the leak this pins. Twenty rounds in each kind of world, and the live objects count
 * back to where they started.
 */

namespace DreamLifecycleWorldTeardownTestLocal
{
	using namespace DreamTests::Lifecycle;

	struct FCounts
	{
		int32 Widgets = 0;
		int32 Trees = 0;
		int32 Behaviours = 0;
		int32 Meshes = 0;
		int32 DataTextures = 0;

		bool operator==(const FCounts& Other) const
		{
			return Widgets == Other.Widgets && Trees == Other.Trees && Behaviours == Other.Behaviours
				&& Meshes == Other.Meshes && DataTextures == Other.DataTextures;
		}
		FString ToString() const
		{
			return FString::Printf(TEXT("%d widgets, %d trees, %d behaviours, %d meshes, %d data textures"),
				Widgets, Trees, Behaviours, Meshes, DataTextures);
		}
	};

	template<typename T>
	int32 CountLive()
	{
		int32 Count = 0;
		for (TObjectIterator<T> It(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	FCounts CountLiveObjects()
	{
		FCounts Counts;
		Counts.Widgets = CountLive<UDreamWidget>();
		Counts.Trees = CountLive<UDreamWidgetTree>();
		Counts.Behaviours = CountLive<UDreamUIBehaviour>();
		Counts.Meshes = CountLive<UDreamUIMeshComponent>();
		Counts.DataTextures = CountLive<UDreamUIDataAsTexture>();
		return Counts;
	}

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

	/** Every kind of tree a world of its type can hold, none of them destroyed: the world's teardown is what is on trial. */
	void Populate(UWorld* InWorld, UClass* InPanelClass)
	{
		// A panel placed as a level designer places one: its component hosts the tree. A game world loads
		// it at BeginPlay, which a test world never reaches, so it is asked to.
		if (ADreamWorldWidgetActor* Actor = PlacePanel(InWorld, InPanelClass); Actor != nullptr && InWorld->IsGameWorld())
		{
			Actor->GetWidgetComponent()->ReloadWidget();
		}
		// A tree nobody hosts, in the manager's pool.
		CreateDreamWidget(InWorld, InPanelClass);
		// A Blueprint's free widget, parked.
		UDreamUIBPLibrary::ConstructWidget(InWorld, TEXT("Free"), nullptr);
		// A page on the screen, where the world has one.
		if (UDreamScreenUISubsystem* Screen = InWorld->GetSubsystem<UDreamScreenUISubsystem>())
		{
			Screen->CreateWidgetOnScreen(InPanelClass);
		}
	}

	bool RunRounds(FAutomationTestBase& InTest, bool bInFlush)
	{
		FScopedPanelClass Panel(bInFlush ? TEXT("WorldTeardownRhi") : TEXT("WorldTeardown"));
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))
		{
			return false;
		}
		Settle(bInFlush);
		const FCounts Baseline = CountLiveObjects();
		FLeakLogWatch Watch;

		const EWorldType::Type WorldTypes[] = { EWorldType::Game, EWorldType::Editor, EWorldType::EditorPreview };
		for (int32 Round = 0; Round < 20; ++Round)
		{
			for (EWorldType::Type WorldType : WorldTypes)
			{
				UWorld* World = UWorld::CreateWorld(WorldType, false);
				if (!InTest.TestNotNull(TEXT("a world was created"), World))
				{
					return false;
				}
				InTest.TestNotNull(FString::Printf(TEXT("a world of type %d has a UI manager"), (int32)WorldType),
					UDreamUIManagerWorldSubsystem::GetInstance(World));
				Populate(World, Panel.GetClass());
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
				World->DestroyWorld(false);
			}
			if (Round % 5 == 4)
			{
				Settle(bInFlush);
			}
		}
		Settle(bInFlush);

		const FCounts After = CountLiveObjects();
		InTest.TestTrue(FString::Printf(TEXT("Everything the worlds held is gone: before %s, after %s"), *Baseline.ToString(), *After.ToString()),
			After == Baseline);
		InTest.TestEqual(TEXT("and none of it reached the collector before the world's teardown took it down"), Watch.Describe(), FString(TEXT("none")));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleWorldTeardownTest,
	"DreamGUI.Lifecycle.DestroyingAWorldWithItsTreesStillInItLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleWorldTeardownTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleWorldTeardownTestLocal::RunRounds(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleWorldTeardownRhiTest,
	"DreamGUI.Lifecycle.RHI.DestroyingAWorldWithItsTreesStillInItLeavesNothingBehindOnTheRenderThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleWorldTeardownRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleWorldTeardownTestLocal::RunRounds(*this, /*bInFlush*/ true);
}

/*
 * A WORLD'S RENDERER LIVES AS LONG AS WHAT IT HAS SENT THE RENDER THREAD.
 *
 * FSceneViewExtensions::NewExtension makes a renderer with MakeShareable, so whoever lets go of it last deletes it on
 * the spot, and the manager lets go of its world's renderer when the world is torn down -- which a test world is within
 * the frame it was built in. The render commands the renderer had sent by then were still waiting for the render
 * thread, and while they held it by a bare pointer they ran against freed memory, writing into whatever the allocator
 * had put there since. Here the render thread is held while the renderer's owner lets go, so the commands are certain
 * to be waiting when it does.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRendererOutlivesItsCommandsTest,
	"DreamGUI.Lifecycle.ARendererOutlivesItsLastOwnerUntilTheRenderCommandsItSentHaveRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleRendererOutlivesItsCommandsTest::RunTest(const FString& Parameters)
{
	if (!GIsThreadedRendering)
	{
		AddInfo(TEXT("Rendering is not threaded here: a render command runs as it is sent, and none is ever left waiting."));
		return true;
	}
	DreamTests::FScopedGameWorld Scoped;
	if (!TestNotNull(TEXT("a world was created"), Scoped.World))
	{
		return false;
	}
	TSharedPtr<FDreamUIRenderer, ESPMode::ThreadSafe> Owner =
		FSceneViewExtensions::NewExtension<FDreamUIRenderer>(Scoped.World, EDreamUIRendererType::ScreenSpace_and_WorldSpace);
	const TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> Renderer = Owner;

	// The render thread is held on a command of the test's own, so everything sent after it waits. Nothing between here
	// and the release may wait for the render thread.
	FEventRef Release(EEventMode::ManualReset);
	std::atomic<bool> bSentAfterRan = false;
	ENQUEUE_RENDER_COMMAND(DreamTest_HoldTheRenderThread)([&Release](FRHICommandListImmediate&)
	{
		Release->Wait();
	});
	Owner->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
	Owner->SetRenderCanvasDepthParameter(Scoped.World, 0.5f, 1);
	ENQUEUE_RENDER_COMMAND(DreamTest_SentAfterTheRenderersCommands)([&bSentAfterRan](FRHICommandListImmediate&)
	{
		bSentAfterRan = true;
	});

	Owner.Reset();
	const bool bCommandsWereWaiting = !bSentAfterRan;
	const bool bAliveWhileTheyWaited = Renderer.IsValid();

	Release->Trigger();
	FlushRenderingCommands();

	TestTrue(TEXT("The renderer's commands were still waiting for the render thread when its last owner let go"), bCommandsWereWaiting);
	TestTrue(TEXT("The commands the renderer sent keep it alive after its last owner lets go"), bAliveWhileTheyWaited);
	TestTrue(TEXT("The render thread got through them"), bSentAfterRan.load());
	TestFalse(TEXT("The renderer is let go of once they have run"), Renderer.IsValid());
	return true;
}

#endif
