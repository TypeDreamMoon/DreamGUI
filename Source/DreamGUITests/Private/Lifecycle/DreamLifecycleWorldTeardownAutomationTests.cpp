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
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Misc/OutputDevice.h"
#include "RenderingThread.h"
#include "UObject/UObjectIterator.h"

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

	/** Every log line naming a tree the collector reached before its owner took it down. */
	struct FLeakLogWatch : public FOutputDevice
	{
		FCriticalSection Lock;
		TArray<FString> Hits;

		FLeakLogWatch() { GLog->AddOutputDevice(this); }
		virtual ~FLeakLogWatch() override { GLog->RemoveOutputDevice(this); }

		virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type InVerbosity, const FName& InCategory) override
		{
			static const TCHAR* const Patterns[] = {
				TEXT("was not destroyed by its owner"),
				TEXT("reached the collector still registered"),
				TEXT("outlived its host"),
				TEXT("collected with its widget tree still loaded"),
			};
			for (const TCHAR* Pattern : Patterns)
			{
				if (FCString::Stristr(InText, Pattern) != nullptr)
				{
					FScopeLock Guard(&Lock);
					Hits.Add(InText);
					return;
				}
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
	};

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
		GLog->Flush();
		FScopeLock Guard(&Watch.Lock);
		InTest.TestEqual(TEXT("and none of it reached the collector before the world's teardown took it down"), JoinLines(Watch.Hits), FString(TEXT("none")));
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

#endif
