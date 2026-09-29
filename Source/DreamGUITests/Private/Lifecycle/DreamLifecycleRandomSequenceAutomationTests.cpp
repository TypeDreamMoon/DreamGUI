// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Math/RandomStream.h"

/*
 * A THOUSAND RANDOM STEPS, AND EVERY TREE STILL HAS EXACTLY ONE OWNER.
 *
 * Each scenario walks one road; an author walks them in any order -- places a panel, deletes it, undoes,
 * pastes a copy, recompiles the class, opens a page, pops it, builds a tree nobody hosts and hangs it
 * somewhere -- and the failures this work exists for were all at the crossings. A fixed seed draws a
 * thousand steps from all of them, and after every tenth the invariants are asked of both worlds: each
 * host holds exactly one tree, every registered tree is held by something, nothing registered is of a
 * retired class, no dynamic texture is without a size, no canvas mesh would be saved with the level.
 * A failure names the seed and the step, so the walk can be taken again.
 */

namespace DreamLifecycleRandomSequenceTestLocal
{
	using namespace DreamTests::Lifecycle;

	constexpr int32 Seed = 20260929;
	constexpr int32 Steps = 1000;
	constexpr int32 CheckEvery = 10;

	enum class EStep : uint8
	{
		Place, Delete, Undo, Redo, Paste, Compile, PushPage, PopPage, MakeFree, AttachFree, DetachFree, DestroyFree, Draw, Collect,
	};

	/** How often each step is drawn, out of the total. A compile and a collection are the expensive ones. */
	const TPair<EStep, int32> Weights[] = {
		{ EStep::Place, 12 }, { EStep::Delete, 10 }, { EStep::Undo, 10 }, { EStep::Redo, 6 }, { EStep::Paste, 5 },
		{ EStep::Compile, 2 }, { EStep::PushPage, 10 }, { EStep::PopPage, 8 }, { EStep::MakeFree, 8 },
		{ EStep::AttachFree, 6 }, { EStep::DetachFree, 5 }, { EStep::DestroyFree, 6 }, { EStep::Draw, 9 }, { EStep::Collect, 3 },
	};

	const TCHAR* NameOf(EStep InStep)
	{
		switch (InStep)
		{
		case EStep::Place: return TEXT("place a panel");
		case EStep::Delete: return TEXT("delete a panel");
		case EStep::Undo: return TEXT("undo");
		case EStep::Redo: return TEXT("redo");
		case EStep::Paste: return TEXT("copy and paste a panel");
		case EStep::Compile: return TEXT("recompile the class");
		case EStep::PushPage: return TEXT("push a page");
		case EStep::PopPage: return TEXT("pop a page");
		case EStep::MakeFree: return TEXT("make a tree nobody hosts");
		case EStep::AttachFree: return TEXT("hang a free tree under a hosted one");
		case EStep::DetachFree: return TEXT("take a hung tree off again");
		case EStep::DestroyFree: return TEXT("destroy a free tree");
		case EStep::Draw: return TEXT("draw two frames");
		case EStep::Collect: return TEXT("collect garbage");
		}
		return TEXT("?");
	}

	struct FWalk
	{
		UWorld* Level = nullptr;
		UWorld* Game = nullptr;
		UClass* PanelClass = nullptr;
		UBlueprint* Blueprint = nullptr;
		FRandomStream Random{ Seed };
		TArray<TWeakObjectPtr<ADreamWorldWidgetActor>> Placed;
		TArray<TWeakObjectPtr<UDreamWidget>> Free;
		TArray<TWeakObjectPtr<UDreamWidget>> Hung;
		int32 PagesMade = 0;

		EStep Draw()
		{
			int32 Total = 0;
			for (const TPair<EStep, int32>& Weight : Weights)
			{
				Total += Weight.Value;
			}
			int32 Pick = Random.RandRange(0, Total - 1);
			for (const TPair<EStep, int32>& Weight : Weights)
			{
				if (Pick < Weight.Value)
				{
					return Weight.Key;
				}
				Pick -= Weight.Value;
			}
			return EStep::Draw;
		}

		template<typename T>
		T* PickLive(TArray<TWeakObjectPtr<T>>& InFrom)
		{
			InFrom.RemoveAll([](const TWeakObjectPtr<T>& Entry) { return !Entry.IsValid(); });
			return InFrom.Num() > 0 ? InFrom[Random.RandRange(0, InFrom.Num() - 1)].Get() : nullptr;
		}

		void Take(EStep InStep)
		{
			switch (InStep)
			{
			case EStep::Place:
				if (ADreamWorldWidgetActor* Actor = PlacePanel(Level, PanelClass))
				{
					Placed.Add(Actor);
				}
				break;
			case EStep::Delete:
				if (ADreamWorldWidgetActor* Actor = PickLive(Placed))
				{
					GEditor->BeginTransaction(FText::FromString(TEXT("Delete a panel")));
					Level->EditorDestroyActor(Actor, true);
					GEditor->EndTransaction();
				}
				break;
			case EStep::Undo:
				GEditor->UndoTransaction();
				break;
			case EStep::Redo:
				GEditor->RedoTransaction();
				break;
			case EStep::Paste:
				if (ADreamWorldWidgetActor* Actor = PickLive(Placed))
				{
					for (AActor* Pasted : CopyPasteActor(Level, Actor, 1))
					{
						if (ADreamWorldWidgetActor* Panel = Cast<ADreamWorldWidgetActor>(Pasted))
						{
							Placed.Add(Panel);
						}
					}
				}
				break;
			case EStep::Compile:
				FKismetEditorUtilities::CompileBlueprint(Blueprint,
					Random.FRand() < 0.5f ? EBlueprintCompileOptions::SkipGarbageCollection : EBlueprintCompileOptions::None);
				if (UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get())
				{
					EditorSubsystem->RebuildReleasedTrees();
				}
				break;
			case EStep::PushPage:
				if (UDreamScreenUISubsystem* Screen = Game->GetSubsystem<UDreamScreenUISubsystem>())
				{
					Screen->PushWidgetOfClass(FName(*FString::Printf(TEXT("Page%d"), PagesMade++)), PanelClass,
						Random.FRand() < 0.5f ? EDreamUIScreenPageCachePolicy::KeepAlive : EDreamUIScreenPageCachePolicy::DestroyOnPop,
						/*bHidePrevious*/ Random.FRand() < 0.5f);
				}
				break;
			case EStep::PopPage:
				if (UDreamScreenUISubsystem* Screen = Game->GetSubsystem<UDreamScreenUISubsystem>())
				{
					Screen->PopUI();
				}
				break;
			case EStep::MakeFree:
				if (UDreamWidget* Tree = CreateDreamWidget(Random.FRand() < 0.5f ? Level : Game, PanelClass))
				{
					Free.Add(Tree);
				}
				break;
			case EStep::AttachFree:
			{
				UDreamWidget* Tree = PickLive(Free);
				ADreamWorldWidgetActor* Actor = PickLive(Placed);
				UDreamWidget* HostTree = Actor != nullptr ? Actor->GetWidgetComponent()->GetLoadedWidget() : nullptr;
				if (Tree != nullptr && HostTree != nullptr && Tree->GetParent() == nullptr && Tree->GetWorld() == HostTree->GetWorld())
				{
					Tree->TrySetParent(HostTree, false);
					Hung.Add(Tree);
				}
				break;
			}
			case EStep::DetachFree:
				if (UDreamWidget* Tree = PickLive(Hung); Tree != nullptr && Tree->GetParent() != nullptr)
				{
					Tree->RemoveFromParent();
				}
				break;
			case EStep::DestroyFree:
				if (UDreamWidget* Tree = PickLive(Free))
				{
					Tree->DestroyWidget();
				}
				break;
			case EStep::Draw:
				DrawFrames(Level, 2);
				DrawFrames(Game, 2);
				break;
			case EStep::Collect:
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
				break;
			}
		}

		/** What should hold of both worlds between any two steps; each broken rule as a sentence. */
		TArray<FString> Check() const
		{
			TArray<FString> Broken;
			for (UWorld* World : { Level, Game })
			{
				UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
				if (Manager == nullptr)
				{
					continue;
				}
				for (UDreamWidget* Widget : Manager->GetRegisteredWidgets())
				{
					const UClass* Class = Widget->GetClass();
					if (Class->HasAnyClassFlags(CLASS_NewerVersionExists) || Class->GetName().StartsWith(TEXT("REINST_")))
					{
						Broken.Add(FString::Printf(TEXT("%s is registered as an instance of a retired class"), *Widget->GetPathName()));
					}
					if (Widget->GetParent() == nullptr && !Manager->IsHeldByHost(Widget) && !Manager->IsFreeRoot(Widget) && !Manager->IsWidgetParked(Widget))
					{
						Broken.Add(FString::Printf(TEXT("%s is a registered tree nothing holds"), *Widget->GetPathName()));
					}
				}
			}
			for (const TWeakObjectPtr<ADreamWorldWidgetActor>& Actor : Placed)
			{
				if (!Actor.IsValid() || Actor->IsActorBeingDestroyed())
				{
					continue;
				}
				const UDreamWorldWidgetComponent* Host = Actor->GetWidgetComponent();
				if (Host == nullptr || !Host->IsRegistered())
				{
					continue;
				}
				const UDreamWidget* Tree = Host->GetLoadedWidget();
				if (Tree == nullptr || !Tree->HasRegistered())
				{
					Broken.Add(FString::Printf(TEXT("%s is registered and holds no live tree"), *Host->GetPathName()));
				}
			}
			for (const FString& Texture : FindZeroSizeDynamicTextures())
			{
				Broken.Add(FString::Printf(TEXT("%s has no size"), *Texture));
			}
			for (const FString& Mesh : FindPersistentCanvasMeshes(Level))
			{
				Broken.Add(FString::Printf(TEXT("%s would be saved with the level"), *Mesh));
			}
			return Broken;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRandomSequenceTest,
	"DreamGUI.Lifecycle.AThousandRandomStepsLeaveEveryTreeWithExactlyOneOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleRandomSequenceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleRandomSequenceTestLocal;

	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	// A test world has no world context of the engine's, so every deletion says so; that is the test's
	// arrangement, not something the walk did.
	AddExpectedMessagePlain(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	FScopedPanelClass Panel(TEXT("RandomSequence"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))
	{
		return false;
	}
	FScopedWorld Level(EWorldType::Editor);
	FScopedWorld Game(EWorldType::Game);
	FLeakLogWatch Watch;

	// The walk's undo reaches only the walk's own edits: an earlier test's, left in the buffer, name
	// objects of worlds long gone.
	GEditor->ResetTransaction(FText::FromString(TEXT("A random walk begins")));
	FWalk Walk;
	Walk.Level = Level.World;
	Walk.Game = Game.World;
	Walk.PanelClass = Panel.GetClass();
	Walk.Blueprint = Panel.Blueprint;

	TMap<EStep, int32> Taken;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const EStep Next = Walk.Draw();
		Walk.Take(Next);
		Taken.FindOrAdd(Next)++;
		if (Step % CheckEvery == CheckEvery - 1)
		{
			const TArray<FString> Broken = Walk.Check();
			if (Broken.Num() > 0)
			{
				AddError(FString::Printf(TEXT("After step %d (%s) of the walk with seed %d: %s"),
					Step, NameOf(Next), Seed, *JoinLines(Broken)));
				break;
			}
		}
	}
	GEditor->ResetTransaction(FText::FromString(TEXT("The random walk is over")));
	if (UDreamScreenUISubsystem* Screen = Game.World->GetSubsystem<UDreamScreenUISubsystem>())
	{
		Screen->RemoveAllUI();
	}
	TArray<FString> Counts;
	for (const TPair<EStep, int32>& Pair : Taken)
	{
		Counts.Add(FString::Printf(TEXT("%s x%d"), NameOf(Pair.Key), Pair.Value));
	}
	AddInfo(FString::Printf(TEXT("Seed %d: %s"), Seed, *FString::Join(Counts, TEXT(", "))));
	TestEqual(TEXT("Nothing reached the collector still registered"), Watch.Describe(), FString(TEXT("none")));
	return true;
}

#endif
