// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamUIBPLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "RenderingThread.h"

/*
 * EVERY TREE HAS EXACTLY ONE HOST, AND THE HOST LETS IT GO.
 *
 * A placed panel's tree is outered to the component that hosts it and held by it alone: nothing the
 * level keeps -- its save, a play session's duplicate, Copy -- can reach the tree through the level, and
 * the manager's registry, being weak, keeps nothing alive either. A tree nobody hosts sits in the
 * manager's pool until it is attached somewhere or destroyed. And undo, which brings hosts back and takes
 * them away again, never leaves a host without its tree, a tree without its host, or two trees for one --
 * headless, and under the renderer with the render thread flushed after every step and around every
 * collection, so whatever a deletion or an undo pulls out from under the renderer fails in that step.
 */

namespace DreamLifecycleHostOwnershipTestLocal
{
	void Collect()
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	void Flush(bool bInFlush)
	{
		if (bInFlush)
		{
			FlushRenderingCommands();
		}
	}

	void Settle(bool bInFlush)
	{
		Flush(bInFlush);
		Collect();
		Flush(bInFlush);
	}

	constexpr int32 UndoRounds = 3;

	/**
	 * A panel with a background blur in its tree -- a canvas of its own under the panel's -- deleted in a
	 * transaction the way the level editor deletes, undone, redone and undone again, UndoRounds times over.
	 */
	bool RunHostUndo(FAutomationTestBase& InTest, bool bInFlush)
	{
		using namespace DreamTests::Lifecycle;

		if (GEditor == nullptr || GEditor->Trans == nullptr)
		{
			InTest.AddError(TEXT("no transaction buffer; this test cannot say anything"));
			return false;
		}
		// A test world has no world context of the engine's, so deleting an actor from it says so.
		InTest.AddExpectedMessagePlain(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		FScopedPanelClass Panel(bInFlush ? TEXT("HostUndoRhi") : TEXT("HostUndo"));
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))
		{
			return false;
		}
		FScopedWorld Level(EWorldType::Editor);
		// Held weakly across the collections below: the tree the deletion takes down is freed by them, and
		// nothing else keeps it -- the component's hold on its tree is not undo's to record.
		const TWeakObjectPtr<ADreamWorldWidgetActor> Actor = PlacePanel(Level.World, Panel.GetClass());
		if (!InTest.TestTrue(TEXT("the panel was placed"), Actor.IsValid()))
		{
			return false;
		}
		const TWeakObjectPtr<UDreamWorldWidgetComponent> Host = Actor->GetWidgetComponent();
		TWeakObjectPtr<UDreamWidget> Tree = Host->GetLoadedWidget();
		if (!InTest.TestTrue(TEXT("with its tree"), Tree.IsValid()))
		{
			return false;
		}
		TWeakObjectPtr<UDreamWidget> Blur = AddBackgroundBlur(Level.World, Tree.Get());
		InTest.TestTrue(TEXT("...and a background blur in it, with a canvas of its own"), Blur.IsValid());
		DrawFrames(Level.World, 2);
		Flush(bInFlush);

		for (int32 Round = 0; Round < UndoRounds; ++Round)
		{
			const FString Deleted = FString::Printf(TEXT("Round %d, deleted"), Round);
			// Deleted in a transaction, the way the level editor deletes: the host goes, and its tree with it.
			GEditor->BeginTransaction(FText::FromString(TEXT("Delete the panel")));
			Level.World->EditorDestroyActor(Actor.Get(), true);
			GEditor->EndTransaction();
			Flush(bInFlush);
			Settle(bInFlush);
			InTest.TestFalse(Deleted + TEXT(": deleting the host destroyed its tree"), Tree.IsValid());
			InTest.TestFalse(Deleted + TEXT(": ...the blur and its canvas with it"), Blur.IsValid());
			InTest.TestEqual(Deleted + TEXT(": ...and left no tree in the world"), RegisteredRoots(Level.World).Num(), 0);

			// Undone: the host comes back, and builds its tree again -- one tree, its own.
			const FString Undone = FString::Printf(TEXT("Round %d, undone"), Round);
			GEditor->UndoTransaction();
			DrawFrames(Level.World, 2);
			Flush(bInFlush);
			if (!InTest.TestTrue(Undone + TEXT(": the host is back"), Actor.IsValid() && Host.IsValid()))
			{
				return false;
			}
			const TArray<UDreamWidget*> AfterUndo = RegisteredRoots(Level.World);
			InTest.TestEqual(Undone + TEXT(": ...with exactly one tree in the world"), AfterUndo.Num(), 1);
			InTest.TestTrue(Undone + TEXT(": ...which is the host's, outered to it"),
				AfterUndo.Num() == 1 && Host->GetLoadedWidget() == AfterUndo[0] && HolderOf(AfterUndo[0]) == Host.Get());
			InTest.TestFalse(Undone + TEXT(": ...and is not the tree the deletion destroyed"), Tree.IsValid() && AfterUndo.Contains(Tree.Get()));

			// Redone: the host is gone again, and its tree with it -- nothing registered, nothing hostless.
			GEditor->RedoTransaction();
			Flush(bInFlush);
			Settle(bInFlush);
			InTest.TestEqual(FString::Printf(TEXT("Round %d, redone: no tree in the world"), Round), RegisteredRoots(Level.World).Num(), 0);

			// Undone again, for the next round: the host back once more, with a blur in its new tree.
			GEditor->UndoTransaction();
			DrawFrames(Level.World, 2);
			Flush(bInFlush);
			if (!InTest.TestTrue(FString::Printf(TEXT("Round %d, undone again: the host is back"), Round), Actor.IsValid() && Host.IsValid() && Host->GetLoadedWidget() != nullptr))
			{
				return false;
			}
			Tree = Host->GetLoadedWidget();
			Blur = AddBackgroundBlur(Level.World, Tree.Get());
			DrawFrames(Level.World, 2);
			Flush(bInFlush);
		}
		InTest.TestEqual(TEXT("No dynamic texture was left without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleHostHoldsItsTreeTest,
	"DreamGUI.Lifecycle.APlacedPanelsTreeIsOuteredToItsHostAndHeldByNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleHostHoldsItsTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;
	using namespace DreamLifecycleHostOwnershipTestLocal;

	FScopedPanelClass Panel(TEXT("HostHoldsTree"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWorldWidgetComponent* Host = Actor->GetWidgetComponent();
	UDreamWidget* Root = Host->GetLoadedWidget();
	if (!TestNotNull(TEXT("with its tree"), Root))return false;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Level.World);
	if (!TestNotNull(TEXT("in a world with a UI manager"), Manager))return false;

	TestTrue(TEXT("The tree is outered to the component that hosts it"), HolderOf(Root) == Host);
	TestTrue(TEXT("...which the manager knows as a host"), Manager->IsTreeHostRegistered(Host));
	TestFalse(TEXT("...and the manager does not also hold it"), Manager->IsFreeRoot(Root));
	TestTrue(TEXT("It is the only tree in the world"), RegisteredRoots(Level.World).Num() == 1 && RegisteredRoots(Level.World)[0] == Root);

	// The registry does not keep a tree alive: a tree its host let go of without destroying it would be
	// collected. Asked the other way round -- a host that still holds its tree keeps it through a collection.
	Collect();
	TestTrue(TEXT("A collection leaves the hosted tree alone"), IsValid(Root) && Host->GetLoadedWidget() == Root);

	// A tree nobody hosts is the manager's, until it is attached somewhere.
	UDreamWidget* Free = CreateDreamWidget(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("a tree made with no host"), Free))return false;
	TestTrue(TEXT("A tree made with no host is held in the manager's pool"), Manager->IsFreeRoot(Free) && HolderOf(Free) == Manager);
	Collect();
	TestTrue(TEXT("...which keeps it through a collection"), IsValid(Free));
	Free->TrySetParent(Root, false);
	TestFalse(TEXT("Attached to a hosted tree, it leaves the pool: its parent holds it now"), Manager->IsFreeRoot(Free));
	Free->RemoveFromParent();
	TestTrue(TEXT("Taken off again, it is held once more -- parked, and pooled"), Manager->IsWidgetParked(Free) && Manager->IsFreeRoot(Free));
	Free->DestroyWidget();
	TestFalse(TEXT("Destroyed, it leaves the pool"), Manager->IsFreeRoot(Free));

	// A Blueprint's free widget is the manager's too.
	UDreamWidget* Constructed = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("Constructed"), nullptr);
	if (TestNotNull(TEXT("a Blueprint's free widget"), Constructed))
	{
		TestTrue(TEXT("A Blueprint's free widget is parked and pooled"), Manager->IsWidgetParked(Constructed) && Manager->IsFreeRoot(Constructed));
		Constructed->DestroyWidget();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleHostUndoTest,
	"DreamGUI.Lifecycle.UndoingAndRedoingAHostsDeletionNeverLeavesAHostWithoutExactlyOneTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleHostUndoTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleHostOwnershipTestLocal::RunHostUndo(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleHostUndoRhiTest,
	"DreamGUI.Lifecycle.RHI.UndoingAndRedoingAHostsDeletionNeverLeavesAHostWithoutExactlyOneTreeOnTheRenderThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleHostUndoRhiTest::RunTest(const FString& Parameters)
{
	return DreamLifecycleHostOwnershipTestLocal::RunHostUndo(*this, /*bInFlush*/ true);
}

#endif
