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

/*
 * EVERY TREE HAS EXACTLY ONE HOST, AND THE HOST LETS IT GO.
 *
 * A placed panel's tree is outered to the component that hosts it and held by it alone: nothing the
 * level keeps -- its save, a play session's duplicate, Copy -- can reach the tree through the level, and
 * the manager's registry, being weak, keeps nothing alive either. A tree nobody hosts sits in the
 * manager's pool until it is attached somewhere or destroyed. And undo, which brings hosts back and takes
 * them away again, never leaves a host without its tree, a tree without its host, or two trees for one.
 */

namespace DreamLifecycleHostOwnershipTestLocal
{
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

	void Collect()
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
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
	using namespace DreamTests::Lifecycle;
	using namespace DreamLifecycleHostOwnershipTestLocal;

	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	// A test world has no world context of the engine's, so deleting an actor from it says so.
	AddExpectedMessagePlain(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	FScopedPanelClass Panel(TEXT("HostUndo"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWorldWidgetComponent* Host = Actor->GetWidgetComponent();
	UDreamWidget* FirstRoot = Host->GetLoadedWidget();
	if (!TestNotNull(TEXT("with its tree"), FirstRoot))return false;

	// Deleted in a transaction, the way the level editor deletes: the host goes, and its tree with it.
	GEditor->BeginTransaction(FText::FromString(TEXT("Delete the panel")));
	Level.World->EditorDestroyActor(Actor, true);
	GEditor->EndTransaction();
	Collect();
	TestFalse(TEXT("Deleting the host destroyed its tree"), IsValid(FirstRoot));
	TestEqual(TEXT("...and left no tree in the world"), RegisteredRoots(Level.World).Num(), 0);

	// Undone: the host comes back, and builds its tree again -- one tree, its own.
	GEditor->UndoTransaction();
	DrawFrames(Level.World, 2);
	if (!TestTrue(TEXT("Undoing the deletion brings the host back"), IsValid(Actor) && IsValid(Host)))return false;
	const TArray<UDreamWidget*> AfterUndo = RegisteredRoots(Level.World);
	TestEqual(TEXT("...with exactly one tree in the world"), AfterUndo.Num(), 1);
	TestTrue(TEXT("...which is the host's, outered to it"),
		AfterUndo.Num() == 1 && Host->GetLoadedWidget() == AfterUndo[0] && HolderOf(AfterUndo[0]) == Host);
	TestFalse(TEXT("...and is not the tree the deletion destroyed"), AfterUndo.Contains(FirstRoot));

	// Redone: the host is gone again, and its tree with it -- nothing registered, nothing hostless.
	GEditor->RedoTransaction();
	Collect();
	TestEqual(TEXT("Redoing the deletion leaves no tree in the world"), RegisteredRoots(Level.World).Num(), 0);
	return true;
}

#endif
