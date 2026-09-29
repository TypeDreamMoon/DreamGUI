// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * PASTED, SAVED, PLAYED.
 *
 * The road the crash that started this work took, walked in the level editor's own world and a real play session.
 * A panel drawn by DreamUI's renderer, with a background blur in its tree, and a panel drawn by the engine's are placed
 * and drawn until their meshes have their materials; each is copied and pasted seven times, and the level is saved.
 * Then play. The session duplicates the level, and the moment it exists the render thread runs everything the
 * duplication queued -- a texture cloned without its size asserts right there -- and nothing was counted into the
 * session's copy, no dynamic texture is without a size, no canvas mesh would be kept by the play level, and nothing
 * the play level keeps refers into another panel's tree. In play every panel holds one tree of its own; after the
 * session nothing reached the collector still registered.
 *
 * A render-target surface is not among them: what a surface shows is handed to it at run time and does not travel
 * with a copy, so in play it would show nothing. What its copies make of it is asked by the level-copy test and the
 * surface's own.
 */
namespace DreamLifecyclePastePlayTestLocal
{
	using namespace DreamTests::Lifecycle;

	constexpr int32 Pastes = 7;

	/** What the steps keep between them. */
	struct FState
	{
		int32 CopiesBefore = 0;
		int32 PanelsInTheLevel = 0;
	};

	/** The panels of every level of InWorld. */
	TArray<UDreamWorldWidgetComponent*> FindPanels(const UWorld& InWorld)
	{
		TArray<UDreamWorldWidgetComponent*> Panels;
		for (const ULevel* Level : InWorld.GetLevels())
		{
			if (Level == nullptr)
			{
				continue;
			}
			for (const AActor* Actor : Level->Actors)
			{
				if (const ADreamWorldWidgetActor* Panel = Cast<ADreamWorldWidgetActor>(Actor); IsValid(Panel) && Panel->GetWidgetComponent() != nullptr)
				{
					Panels.Add(Panel->GetWidgetComponent());
				}
			}
		}
		return Panels;
	}

	/** References the levels of InWorld keep into a tree that is not their own actor's: what a paste used to leave. */
	TArray<FString> FindBridgesIntoOtherTrees(const UWorld& InWorld)
	{
		TArray<FString> Found;
		for (const DreamUI::FTreeBridge& Bridge : DreamUI::FindTreeBridges(InWorld))
		{
			if (!Bridge.bIntoOwnHost)
			{
				Found.Add(FString::Printf(TEXT("%s -> %s"), *Bridge.From, *Bridge.To));
			}
		}
		return Found;
	}

	/** The level saved as a person saves before playing: its package written out -- to a file this test then removes. */
	bool SaveTheLevel(UWorld& InWorld, FString& OutError)
	{
		UPackage* Package = InWorld.GetOutermost();
		const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"),
			FPackageName::GetShortName(Package) + FPackageName::GetMapPackageExtension()));
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Standalone;
		Args.SaveFlags = SAVE_None;
		// Not through GError, which in an unattended editor turns SavePackage's explanation into a crash.
		Args.Error = GWarn;
		Args.bSlowTask = false;
		const FSavePackageResultStruct Result = UPackage::Save(Package, &InWorld, *Filename, Args);
		IFileManager::Get().Delete(*Filename, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
		if (!Result.IsSuccessful())
		{
			OutError = FString::Printf(TEXT("saving %s to %s failed with ESavePackageResult %d"), *Package->GetName(), *Filename, (int32)Result.Result);
			return false;
		}
		return true;
	}

	void EnqueueStep(TFunction<bool()> InStep)
	{
		// Through a named local: a lambda's capture list carries commas, and the macro would cut its argument there.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	bool RunPastePlay(FAutomationTestBase& InTest, bool bInFlush)
	{
		const TSharedRef<FScopedPanelClass> Panel = MakeShared<FScopedPanelClass>(bInFlush ? TEXT("PastePlayRhi") : TEXT("PastePlay"));
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel->GetClass()))
		{
			return false;
		}
		const TSharedRef<FState> State = MakeShared<FState>();
		const TSharedRef<FLeakLogWatch> Watch = MakeShared<FLeakLogWatch>();
		FAutomationTestBase* Test = &InTest;

		FDreamPieRigOptions Options;
		// A panel that begins play asks for an event system for each player, as a level with world-space UI
		// and none of its own gets one; so the level brings the session its own. The session is only looked at.
		Options.bLevelBringsItsOwnEventSystem = true;
		Options.PopulateEditorWorld = [Test, State, Panel](UWorld& InWorld)
		{
			const FWorldSpaceKinds Kinds = PlaceEveryWorldSpaceKind(&InWorld, Panel->GetClass(), /*bInWithSurface*/ false);
			const TArray<AActor*> Placed = Kinds.Actors();
			Test->TestEqual(TEXT("In the level editor: both panels were placed"), Placed.Num(), 2);
			for (AActor* Actor : Placed)
			{
				Test->TestEqual(FString::Printf(TEXT("...%s was pasted"), *GetNameSafe(Actor)), CopyPasteActor(&InWorld, Actor, Pastes).Num(), Pastes);
			}
			DrawFrames(&InWorld, 2);
			State->PanelsInTheLevel = FindPanels(InWorld).Num();
			Test->TestEqual(TEXT("...so the level holds every panel and its pastes"), State->PanelsInTheLevel, Placed.Num() * (Pastes + 1));
			Test->TestEqual(TEXT("...no canvas mesh the level would save"), JoinLines(FindPersistentCanvasMeshes(&InWorld)), FString(TEXT("none")));
			Test->TestEqual(TEXT("...nothing the level keeps refers into another panel's tree"), JoinLines(FindBridgesIntoOtherTrees(InWorld)), FString(TEXT("none")));
			FString Error;
			const bool bSaved = SaveTheLevel(InWorld, Error);
			Test->TestTrue(FString::Printf(TEXT("...and the level was saved (%s)"), *Error), bSaved);
			State->CopiesBefore = DreamUI::GetCopiedIntoPlaySessionCount();
		};
		Options.EditorSettleFrames = 2;
		// Right after the session duplicated the level and the render thread ran what the duplication queued.
		Options.OnPlayWorldCreated = [Test, State](UWorld& InPlayWorld)
		{
			Test->TestEqual(TEXT("The play session's copy of the level took nothing of a tree"), DreamUI::GetCopiedIntoPlaySessionCount() - State->CopiesBefore, 0);
			Test->TestEqual(TEXT("...no dynamic texture is without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
			Test->TestEqual(TEXT("...no canvas mesh the play level would keep"), JoinLines(FindPersistentCanvasMeshes(&InPlayWorld)), FString(TEXT("none")));
			Test->TestEqual(TEXT("...and nothing the play level keeps refers into another panel's tree"), JoinLines(FindBridgesIntoOtherTrees(InPlayWorld)), FString(TEXT("none")));
		};

		TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(InTest, Options);
		Rig->Start();
		Rig->Sequence()
			.Then([Test, State, bInFlush](FDreamDriverContext& InContext)
			{
				if (bInFlush)
				{
					FlushRenderingCommands();
				}
				const TArray<UDreamWorldWidgetComponent*> Panels = FindPanels(*InContext.World);
				Test->TestEqual(TEXT("In play: every panel of the level is there"), Panels.Num(), State->PanelsInTheLevel);
				const TArray<UDreamWidget*> Roots = RegisteredRoots(InContext.World);
				TArray<FString> NotHoldingOne;
				for (const UDreamWorldWidgetComponent* Host : Panels)
				{
					const UDreamWidget* Tree = Host->GetLoadedWidget();
					const int32 Held = Roots.FilterByPredicate([Host](const UDreamWidget* Root) { return HolderOf(Root) == Host; }).Num();
					if (Tree == nullptr || !Tree->HasRegistered() || HolderOf(Tree) != Host || Held != 1)
					{
						NotHoldingOne.Add(FString::Printf(TEXT("%s (%d)"), *GetNameSafe(Host->GetOwner()), Held));
					}
				}
				Test->TestEqual(TEXT("...each holding exactly one tree, its own"), JoinLines(NotHoldingOne), FString(TEXT("none")));
				Test->TestEqual(TEXT("...no dynamic texture is without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
				Test->TestEqual(TEXT("...no canvas mesh the play level would keep"), JoinLines(FindPersistentCanvasMeshes(InContext.World)), FString(TEXT("none")));
			})
			.PerformLatent();
		Rig->Finish();
		// After the session: a collection, and nothing reached the collector still registered. The level editor's
		// world keeps its panels -- the next map takes them down with it.
		EnqueueStep([Test, Watch, Panel, bInFlush]()
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
			Test->TestEqual(TEXT("After the session, nothing reached the collector still registered"), Watch->Describe(), FString(TEXT("none")));
			return true;
		});
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePastePlayTest,
	"DreamGUI.Pie.PanelsPastedAndSavedThenPlayedCloneNothingIntoThePlaySession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecyclePastePlayTest::RunTest(const FString& Parameters)
{
	return DreamLifecyclePastePlayTestLocal::RunPastePlay(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePastePlayRhiTest,
	"DreamGUI.Pie.RHI.PanelsPastedAndSavedThenPlayedCloneNothingIntoThePlaySessionOnTheRenderThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecyclePastePlayRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecyclePastePlayTestLocal::RunPastePlay(*this, /*bInFlush*/ true);
}

#endif
