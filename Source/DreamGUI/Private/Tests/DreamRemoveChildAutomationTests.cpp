// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

/*
 * Removing children, which splits into two verbs here where UMG has one and a half.
 *
 * UMG's RemoveChild detaches and lets GC take the pieces, and its ClearChildren does that in bulk.
 * Neither is safe to copy: a detached UDreamWidget is still registered and still held by the manager,
 * so "detach and forget" produces a live widget nobody owns rather than garbage. So removal is
 * explicit about which one you meant, and the detaching one puts the widget back into the same
 * held, suppressed state a freshly created widget is in.
 */

namespace DreamRemoveChildTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Register then attach, which is the order the creation verbs use. OnRegister does not recurse
	 *  into children, so a helper that only registered the root would leave the rest unregistered
	 *  and every claim about registration below would be vacuous. */
	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRemoveChildSurvivesTest,
	"DreamGUI.Widget.Remove.RemovedChildSurvivesAndCanBeAddedAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRemoveChildSurvivesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRemoveChildTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}

	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"));
	UDreamWidget* Child = MakeWidget(TestWorld.World, Panel, TEXT("Child"));
	UDreamWidget* Grandchild = MakeWidget(TestWorld.World, Child, TEXT("Grandchild"));
	Panel->OnRegister();
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();

	TestTrue(TEXT("Removing a child reports success"), Panel->RemoveChild(Child));

	// The whole point of a separate remove verb: this widget is still usable. Anything less and
	// pooling would have to go through create-and-destroy every cycle.
	TestTrue(TEXT("The removed child is still valid"), IsValid(Child));
	TestTrue(TEXT("It is still registered"), Child->HasRegistered());
	TestNull(TEXT("It has no parent"), Child->GetParent());
	TestTrue(TEXT("Its own subtree came with it"), Child->GetChildren().Contains(Grandchild));
	TestTrue(TEXT("The grandchild is untouched"), IsValid(Grandchild) && Grandchild->GetParent() == Child);
	TestEqual(TEXT("The panel lost it"), Panel->GetChildren().Num(), 0);

	// It is suppressed and held, exactly like a widget that has been created but not added -- so a
	// pooled widget neither draws nor ticks while it waits, and is not relying on the caller alone
	// to keep it alive.
	TestTrue(TEXT("It is parked"), Manager->IsWidgetParked(Child));
	TestFalse(TEXT("So it is not on screen"), Child->GetWidgetActiveInHierarchy());
	TestTrue(TEXT("The manager still anchors it"), Manager->GetAllWidgetArray().Contains(Child));

	// And it goes back in.
	UDreamWidget* OtherPanel = MakeWidget(TestWorld.World, nullptr, TEXT("OtherPanel"));
	OtherPanel->OnRegister();
	OtherPanel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	TestNotNull(TEXT("Re-adding it yields a fresh slot"), OtherPanel->AddChild(Child));
	TestFalse(TEXT("It is no longer parked"), Manager->IsWidgetParked(Child));
	TestTrue(TEXT("It is live again"), Child->GetWidgetActiveInHierarchy());
	TestTrue(TEXT("Its subtree came back with it"), Grandchild->GetParent() == Child);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRemoveChildRefusalTest,
	"DreamGUI.Widget.Remove.OnlyRemovesItsOwnChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRemoveChildRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamRemoveChildTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"));
	UDreamWidget* Mine = MakeWidget(TestWorld.World, Panel, TEXT("Mine"));
	UDreamWidget* Other = MakeWidget(TestWorld.World, nullptr, TEXT("Other"));
	UDreamWidget* Stranger = MakeWidget(TestWorld.World, Other, TEXT("Stranger"));
	Panel->OnRegister();
	Other->OnRegister();

	// Ownership matters: UMG's RemoveChild is a no-op on a widget that is not yours, and detaching
	// someone else's child by mistake would silently take it out of a hierarchy you do not own.
	TestFalse(TEXT("Refuses a widget belonging to someone else"), Panel->RemoveChild(Stranger));
	TestTrue(TEXT("...and leaves it where it was"), Stranger->GetParent() == Other);
	TestFalse(TEXT("Refuses null"), Panel->RemoveChild(nullptr));
	TestFalse(TEXT("Refuses an out-of-range index"), Panel->RemoveChildAt(7));
	TestFalse(TEXT("Refuses a negative index"), Panel->RemoveChildAt(-1));
	TestEqual(TEXT("Nothing was removed"), Panel->GetChildren().Num(), 1);

	TestTrue(TEXT("A valid index does remove"), Panel->RemoveChildAt(0));
	TestNull(TEXT("...the right one"), Mine->GetParent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDestroyChildrenTest,
	"DreamGUI.Widget.Remove.DestroyTakesTheWholeSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDestroyChildrenTest::RunTest(const FString& Parameters)
{
	using namespace DreamRemoveChildTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"));
	UDreamWidget* A = MakeWidget(TestWorld.World, Panel, TEXT("A"));
	UDreamWidget* B = MakeWidget(TestWorld.World, Panel, TEXT("B"));
	UDreamWidget* C = MakeWidget(TestWorld.World, Panel, TEXT("C"));
	UDreamWidget* Deep = MakeWidget(TestWorld.World, B, TEXT("Deep"));
	Panel->OnRegister();
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();

	TestTrue(TEXT("Destroying one child reports success"), Panel->DestroyChild(A));
	TestFalse(TEXT("It is unregistered"), A->HasRegistered());
	TestEqual(TEXT("The panel is down to two"), Panel->GetChildren().Num(), 2);
	TestFalse(TEXT("Destroying it again does nothing"), Panel->DestroyChild(A));

	// Every child is torn down even though each teardown mutates the array being walked -- the
	// implementation iterates a snapshot, and iterating the live array would visit every other one.
	Panel->DestroyAllChildren();
	TestEqual(TEXT("No children are left"), Panel->GetChildren().Num(), 0);
	TestFalse(TEXT("B went"), B->HasRegistered());
	TestFalse(TEXT("C went too, not just every other one"), C->HasRegistered());
	TestFalse(TEXT("And so did what was underneath B"), Deep->HasRegistered());
	TestFalse(TEXT("The panel reports no children"), Panel->HasAnyChildren());

	// The panel itself is fine and still usable.
	TestTrue(TEXT("The panel survives"), IsValid(Panel) && Panel->HasRegistered());
	UDreamWidget* Fresh = MakeWidget(TestWorld.World, nullptr, TEXT("Fresh"));
	Fresh->OnRegister();
	TestNotNull(TEXT("And still takes children"), Panel->AddChild(Fresh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamChildQueriesTest,
	"DreamGUI.Widget.Remove.QueriesAgreeWithTheHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamChildQueriesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRemoveChildTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"));
	UDreamWidget* A = MakeWidget(TestWorld.World, Panel, TEXT("A"));
	UDreamWidget* B = MakeWidget(TestWorld.World, Panel, TEXT("B"));
	UDreamWidget* Grandchild = MakeWidget(TestWorld.World, A, TEXT("Grandchild"));
	UDreamWidget* Outsider = MakeWidget(TestWorld.World, nullptr, TEXT("Outsider"));
	Panel->OnRegister();

	TestEqual(TEXT("A is at 0"), Panel->GetChildIndex(A), 0);
	TestEqual(TEXT("B is at 1"), Panel->GetChildIndex(B), 1);
	// A grandchild is not a child. Reporting otherwise would make GetChildIndex disagree with
	// GetChildren, which is what callers actually index into.
	TestEqual(TEXT("A grandchild is not one of them"), Panel->GetChildIndex(Grandchild), (int32)INDEX_NONE);
	TestEqual(TEXT("Neither is an unrelated widget"), Panel->GetChildIndex(Outsider), (int32)INDEX_NONE);
	TestEqual(TEXT("Nor null"), Panel->GetChildIndex(nullptr), (int32)INDEX_NONE);

	TestTrue(TEXT("HasChild agrees"), Panel->HasChild(A));
	TestFalse(TEXT("...about grandchildren"), Panel->HasChild(Grandchild));
	TestFalse(TEXT("...and about null"), Panel->HasChild(nullptr));
	TestTrue(TEXT("HasAnyChildren is true here"), Panel->HasAnyChildren());
	TestFalse(TEXT("...and false at a leaf"), B->HasAnyChildren());

	// Having children and having slots are different questions, and only the second one explains why
	// AddChild returns null on a widget with no layout container.
	TestFalse(TEXT("A plain widget has no slots"), Panel->HasPanelSlots());
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	TestTrue(TEXT("A panel layout does"), Panel->HasPanelSlots());
	TestNotNull(TEXT("...which is exactly when AddChild returns one"), Panel->AddChild(A, 0));
	return true;
}

#endif
