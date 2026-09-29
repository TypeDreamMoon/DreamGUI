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
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "RenderingThread.h"

/*
 * A CLASS RECOMPILED WHILE ITS INSTANCES ARE ALIVE: EVERY TREE OF IT GOES, AND COMES BACK FROM THE NEW CLASS.
 *
 * The reinstancer would replace each live instance with a property copy, and a copy of a widget tree is a
 * husk. So each tree goes before the compile, let go by whoever owns it, and comes back a tick after from the
 * class as it is now: a placed panel's host builds its tree again, a screen shows its pages again where they
 * were in its stack, and a tree nobody can build again is destroyed and said so. A widget of the class added
 * at run time inside a hosted tree goes with that tree, as it would in UMG.
 *
 * Twice -- once without the compile's own collection, once with it -- and after each a full collection with
 * the renderer flushed on either side: every host holds exactly one tree, nothing registered is an instance
 * of a class the compile retired, and nothing reached the collector registered.
 */

namespace DreamLifecycleRecompileTestLocal
{
	using namespace DreamTests::Lifecycle;

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

	/** Registered widgets of InWorld whose class a compile has replaced. */
	TArray<FString> FindStaleRegistered(UWorld* InWorld)
	{
		TArray<FString> Stale;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			for (const UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				const UClass* Class = Widget->GetClass();
				if (Class->HasAnyClassFlags(CLASS_NewerVersionExists) || Class->GetName().StartsWith(TEXT("REINST_")))
				{
					Stale.Add(Widget->GetPathName());
				}
			}
		}
		return Stale;
	}

	/** How many registered hierarchy roots of InWorld are held by InHost -- the outer of the tree each roots. */
	int32 CountTreesHeldBy(UWorld* InWorld, const UObject* InHost)
	{
		int32 Count = 0;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			for (const UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				if (Widget->GetParent() != nullptr)
				{
					continue;
				}
				const UObject* Holder = Widget->GetOuter();
				if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Holder); Tree != nullptr && Tree->RootWidget == Widget)
				{
					Holder = Tree->GetOuter();
				}
				Count += Holder == InHost ? 1 : 0;
			}
		}
		return Count;
	}

	bool RunRecompiles(FAutomationTestBase& InTest, bool bInFlush)
	{
		FScopedPanelClass Panel(bInFlush ? TEXT("RecompileLiveRhi") : TEXT("RecompileLive"));
		UClass* PanelClass = Panel.GetClass();
		UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), PanelClass)
			|| !InTest.TestNotNull(TEXT("the editor's DreamGUI subsystem exists"), EditorSubsystem))
		{
			return false;
		}

		// A level with two placed panels, one of which has had an instance of the class added inside its
		// tree at run time, and a tree nobody hosts.
		FScopedWorld Level(EWorldType::Editor);
		ADreamWorldWidgetActor* FirstActor = PlacePanel(Level.World, PanelClass);
		ADreamWorldWidgetActor* SecondActor = PlacePanel(Level.World, PanelClass);
		if (!InTest.TestNotNull(TEXT("the first panel was placed"), FirstActor) || !InTest.TestNotNull(TEXT("and the second"), SecondActor))
		{
			return false;
		}
		UDreamWorldWidgetComponent* FirstHost = FirstActor->GetWidgetComponent();
		UDreamWorldWidgetComponent* SecondHost = SecondActor->GetWidgetComponent();
		const TWeakObjectPtr<UDreamWidget> Nested = CreateDreamWidget(Level.World, PanelClass, FirstHost->GetLoadedWidget());
		const TWeakObjectPtr<UDreamWidget> Free = CreateDreamWidget(Level.World, PanelClass);
		InTest.TestTrue(TEXT("an instance was added inside the first panel's tree"), Nested.IsValid() && Nested->HasRegistered());
		InTest.TestTrue(TEXT("and one made with no host"), Free.IsValid() && Free->HasRegistered());

		// A game with two pages of the class on its screen, one covering the other.
		FScopedWorld Game(EWorldType::Game);
		UDreamScreenUISubsystem* Screen = Game.World->GetSubsystem<UDreamScreenUISubsystem>();
		if (!InTest.TestNotNull(TEXT("the game world has a screen"), Screen))
		{
			return false;
		}
		Screen->PushWidgetOfClass(TEXT("Bottom"), PanelClass, EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/true);
		Screen->PushWidgetOfClass(TEXT("Top"), PanelClass, EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/true);
		const TArray<FName> StackBefore = Screen->GetUIStack();
		if (!InTest.TestEqual(TEXT("both pages are on the stack"), StackBefore.Num(), 2))
		{
			return false;
		}
		Settle(bInFlush);

		FLeakLogWatch Watch;
		for (const bool bCollectInCompile : { false, true })
		{
			const FString Round = bCollectInCompile ? TEXT("with the compile's collection") : TEXT("without a collection");
			const TWeakObjectPtr<UDreamWidget> FirstBefore = FirstHost->GetLoadedWidget();
			const TWeakObjectPtr<UDreamWidget> SecondBefore = SecondHost->GetLoadedWidget();
			const TWeakObjectPtr<UDreamWidget> BottomBefore = Screen->GetUI(TEXT("Bottom"));
			const TWeakObjectPtr<UDreamWidget> TopBefore = Screen->GetUI(TEXT("Top"));

			FKismetEditorUtilities::CompileBlueprint(Panel.Blueprint,
				bCollectInCompile ? EBlueprintCompileOptions::None : EBlueprintCompileOptions::SkipGarbageCollection);

			InTest.TestFalse(FString::Printf(TEXT("%s: the compile is over"), *Round), EditorSubsystem->IsRecompiling());
			InTest.TestTrue(FString::Printf(TEXT("%s: every tree of the class went before the compile"), *Round),
				!(FirstBefore.IsValid() && FirstBefore->HasRegistered()) && !(SecondBefore.IsValid() && SecondBefore->HasRegistered())
				&& !(BottomBefore.IsValid() && BottomBefore->HasRegistered()) && !(TopBefore.IsValid() && TopBefore->HasRegistered()));
			InTest.TestTrue(FString::Printf(TEXT("%s: the hosts hold nothing while it compiles"), *Round),
				FirstHost->GetLoadedWidget() == nullptr && SecondHost->GetLoadedWidget() == nullptr);
			InTest.TestFalse(FString::Printf(TEXT("%s: the instance added inside a tree went with the tree"), *Round), Nested.IsValid() && Nested->HasRegistered());
			InTest.TestFalse(FString::Printf(TEXT("%s: the tree nobody hosts was destroyed"), *Round), Free.IsValid() && Free->HasRegistered());
			InTest.TestTrue(FString::Printf(TEXT("%s: and the rest wait to be built again"), *Round), EditorSubsystem->HasPendingRebuild());

			// What the tick after the compile does.
			EditorSubsystem->RebuildReleasedTrees();
			InTest.TestFalse(FString::Printf(TEXT("%s: nothing is left waiting"), *Round), EditorSubsystem->HasPendingRebuild());
			for (UDreamWorldWidgetComponent* Host : { FirstHost, SecondHost })
			{
				UDreamWidget* Rebuilt = Host->GetLoadedWidget();
				InTest.TestTrue(FString::Printf(TEXT("%s: %s holds a tree of the new class again"), *Round, *Host->GetOwner()->GetName()),
					Rebuilt != nullptr && Rebuilt->GetClass() == PanelClass && Rebuilt->HasRegistered()
					&& Rebuilt != FirstBefore.Get() && Rebuilt != SecondBefore.Get());
				InTest.TestEqual(FString::Printf(TEXT("%s: %s holds exactly one tree"), *Round, *Host->GetOwner()->GetName()),
					CountTreesHeldBy(Level.World, Host), 1);
			}
			UDreamWidget* Bottom = Screen->GetUI(TEXT("Bottom"));
			UDreamWidget* Top = Screen->GetUI(TEXT("Top"));
			InTest.TestTrue(FString::Printf(TEXT("%s: both pages are back, built from the new class"), *Round),
				Bottom != nullptr && Top != nullptr && Bottom->GetClass() == PanelClass && Top->GetClass() == PanelClass
				&& Bottom != BottomBefore.Get() && Top != TopBefore.Get());
			InTest.TestTrue(FString::Printf(TEXT("%s: in the stack's order"), *Round), Screen->GetUIStack() == StackBefore);
			InTest.TestTrue(FString::Printf(TEXT("%s: the top one showing, the covered one not"), *Round),
				Screen->IsUIShowing(TEXT("Top")) && !Screen->IsUIShowing(TEXT("Bottom")));
			InTest.TestEqual(FString::Printf(TEXT("%s: nothing registered in the level is of a retired class"), *Round),
				JoinLines(FindStaleRegistered(Level.World)), FString(TEXT("none")));
			InTest.TestEqual(FString::Printf(TEXT("%s: nor in the game"), *Round),
				JoinLines(FindStaleRegistered(Game.World)), FString(TEXT("none")));

			DrawFrames(Level.World, 2);
			DrawFrames(Game.World, 2);
			Settle(bInFlush);
			InTest.TestTrue(FString::Printf(TEXT("%s: a collection leaves the rebuilt trees with their hosts"), *Round),
				IsValid(FirstHost->GetLoadedWidget()) && IsValid(SecondHost->GetLoadedWidget()));
		}
		InTest.TestEqual(TEXT("Nothing reached the collector still registered"), Watch.Describe(), FString(TEXT("none")));
		Screen->RemoveAllUI();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRecompileLiveTest,
	"DreamGUI.Lifecycle.RecompilingAClassWhileItsTreesAreAliveBuildsEachOfThemAgainFromTheNewClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleRecompileLiveTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleRecompileTestLocal::RunRecompiles(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRecompileLiveRhiTest,
	"DreamGUI.Lifecycle.RHI.RecompilingAClassWhileItsTreesAreDrawnBuildsEachOfThemAgainFromTheNewClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleRecompileLiveRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleRecompileTestLocal::RunRecompiles(*this, /*bInFlush*/ true);
}

#endif
