// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Engine/World.h"

/*
 * RegisterDreamWidgetHierarchy over a tree it does not entirely own.
 *
 * The function makes two passes over the same collected subtree, and until recently the two disagreed
 * about what a second visit means. UDreamWidget::OnRegister is deliberately idempotent -- an explicit
 * `if (bIsRegistered) return;` with a comment saying that registering a widget you did not create is
 * safe to ask for. UDreamWidget::BeginPlay opens with `check(!bHasBegunPlay)`. So the same walk
 * tolerated a re-visit in its first loop and killed the process in its second.
 *
 * That is not hypothetical, and it is not reachable by building a tree the ordinary way. An `each`
 * list makes its cells the moment it is handed a data source: UDreamUserWidget::Initialize ->
 * ResolveEachBindings -> UUIRecyclableScrollView::SetDataSource -> CreateDreamWidget, which registers
 * AND begins the cell subtree -- all of it while the user widget CONTAINING that list is still
 * initializing and has not been registered at all. The outer CreateDreamWidget then walks the finished
 * tree and reaches those cells a second time. Real stack: AddWidgetOfClassToViewport ->
 * CreateDreamWidget -> RegisterDreamWidgetHierarchy -> UDreamWidget::BeginPlay -> assertion failed.
 *
 * The claim being pinned is therefore behavioural, not cosmetic: registering a tree that contains an
 * already-registered, already-begun subtree completes, and does not begin that subtree a second time.
 * Three things make that testable at all, and each is a reason one of the tests below exists:
 *
 *   - The BeginPlay half of the function is gated on the MANAGER having begun play, not the world (see
 *     the note at the top of DreamUserWidget.cpp). UWorldSubsystem::OnWorldBeginPlay is public and its
 *     only job in the base is to set that flag, so the gate can be opened by hand -- BEFORE any widget
 *     exists, because UDreamUIManagerWorldSubsystem::OnWorldBeginPlay also sweeps every widget already
 *     registered with it and begins the ones that have not. Opening it later would begin the fixtures
 *     from the side and prove nothing.
 *   - With the gate shut, the whole second loop is skipped and every assertion about BeginPlay is
 *     vacuously satisfiable. The last test here asserts the shut case explicitly, which is what makes
 *     the other three mean something: if the rig ever stops opening the gate, that test goes red
 *     rather than the suite going quietly green.
 *   - A fix that simply stopped calling BeginPlay would satisfy the first test on its own, so the
 *     second test pins the opposite direction: a tree where nothing has begun play must come out with
 *     everything begun.
 *
 * There is no soft failure mode for the defect itself. `check` is fatal, so a regression takes the
 * test process with it rather than reporting a red test -- surviving to the assertions after the call
 * is half of what is being pinned, and is why those assertions are written to be reached rather than
 * skipped by an early return.
 */

namespace DreamRegisterHierarchyTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/**
	 * Open the gate RegisterDreamWidgetHierarchy reads, and return the manager so the caller can assert
	 * it is actually open.
	 *
	 * Call this before any widget exists. UDreamUIManagerWorldSubsystem::OnWorldBeginPlay walks its
	 * AllWidgetArray and begins everything in it that has not begun; on an empty world that is a flag
	 * flip and nothing else, which is exactly what these tests want it to be. The base's ensure fires
	 * on a SECOND call, hence the guard -- a world created here never has its own BeginPlay run, so one
	 * call is all that ever happens.
	 */
	UDreamUIManagerWorldSubsystem* OpenBeginPlayGate(UWorld* InWorld)
	{
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld);
		if (Manager != nullptr && !Manager->HasBegunPlay())
		{
			Manager->OnWorldBeginPlay(*InWorld);
		}
		return Manager;
	}

	/**
	 * A widget attached the way a hierarchy under assembly is attached: SetParentBeforeRegister, which
	 * raises no attach event and asserts if the widget is already registered. Every production caller
	 * of RegisterDreamWidgetHierarchy hands it a tree built this way.
	 */
	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transactional);
		Widget->SetDisplayName(InName);
		if (InParent != nullptr)
		{
			Widget->SetParentBeforeRegister(InParent);
		}
		return Widget;
	}

	/**
	 * The shape of the crash, minus the list machinery:
	 *
	 *   Host
	 *    +- ListHolder
	 *    |    +- Cell
	 *    |         +- CellLabel
	 *    +- Header
	 *
	 * Cell/CellLabel stand in for what an `each` block brings to life early. Header is attached AFTER
	 * the holder so it lands later in the walk than the cell does -- it is the widget whose state
	 * distinguishes "skipped the cell and carried on" from "stopped at the cell".
	 */
	struct FHostTree
	{
		UDreamWidget* Host = nullptr;
		UDreamWidget* ListHolder = nullptr;
		UDreamWidget* Cell = nullptr;
		UDreamWidget* CellLabel = nullptr;
		UDreamWidget* Header = nullptr;

		bool IsUsable() const
		{
			return Host != nullptr && ListHolder != nullptr && Cell != nullptr
				&& CellLabel != nullptr && Header != nullptr;
		}
	};

	FHostTree BuildHostTree(UWorld* InWorld)
	{
		FHostTree Tree;
		Tree.Host = MakeWidget(InWorld, nullptr, TEXT("Host"));
		Tree.ListHolder = MakeWidget(InWorld, Tree.Host, TEXT("ListHolder"));
		Tree.Cell = MakeWidget(InWorld, Tree.ListHolder, TEXT("Cell"));
		Tree.CellLabel = MakeWidget(InWorld, Tree.Cell, TEXT("CellLabel"));
		Tree.Header = MakeWidget(InWorld, Tree.Host, TEXT("Header"));
		return Tree;
	}

	/** Exactly what RegisterDreamWidgetHierarchy hands its two loops, in the order it hands it. */
	TArray<UDreamWidget*> WalkOrder(UDreamWidget* InRoot)
	{
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(InRoot, Widgets, /*IncludeTarget*/true);
		return Widgets;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRegisterHierarchySkipsAlreadyBegunSubtreeTest,
	"DreamGUI.UserWidget.RegisterHierarchy.ASubtreeThatAlreadyBegunPlayIsNotBegunASecondTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRegisterHierarchySkipsAlreadyBegunSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamRegisterHierarchyTestLocal;

	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("a world to register into"), TestWorld.World))
	{
		return false;
	}

	// First, before a single widget exists. See OpenBeginPlayGate.
	UDreamUIManagerWorldSubsystem* Manager = OpenBeginPlayGate(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	if (!TestTrue(TEXT("and reports begun play, so the BeginPlay branch is the one under test"),
		Manager->HasBegunPlay()))
	{
		return false;
	}

	FHostTree Tree = BuildHostTree(TestWorld.World);
	if (!TestTrue(TEXT("the host tree was built"), Tree.IsUsable()))
	{
		return false;
	}

	// The `each` list's move, one level down and through the very same function: the cell subtree is
	// registered and brought to life while the tree containing it is still being assembled.
	RegisterDreamWidgetHierarchy(Tree.Cell);

	if (!TestTrue(TEXT("the cell registered"), Tree.Cell->HasRegistered()))
	{
		return false;
	}
	if (!TestTrue(TEXT("...and begun play, which is the precondition this whole test rests on"),
		Tree.Cell->HasBegunPlay()))
	{
		return false;
	}
	TestTrue(TEXT("...along with everything under it"), Tree.CellLabel->HasBegunPlay());
	TestFalse(TEXT("while the host above it has not registered"), Tree.Host->HasRegistered());
	TestFalse(TEXT("...nor begun play"), Tree.Host->HasBegunPlay());
	TestFalse(TEXT("...and neither has the widget attached after the cell"), Tree.Header->HasBegunPlay());

	// Read the walk order rather than assume it, so the header assertion further down is a statement
	// about "carried on past the cell" and not about whichever order the children happened to sort in.
	const TArray<UDreamWidget*> Walk = WalkOrder(Tree.Host);
	TestEqual(TEXT("the walk covers the whole tree"), Walk.Num(), 5);
	TestTrue(TEXT("and the header sits after the already-begun subtree in it"),
		Walk.IndexOfByKey(Tree.Header) > Walk.IndexOfByKey(Tree.CellLabel));

	// THE CLAIM. Before the HasBegunPlay() guard this asserted inside UDreamWidget::BeginPlay and took
	// the editor with it, so reaching the next line at all is half of what is pinned here.
	RegisterDreamWidgetHierarchy(Tree.Host);

	TestTrue(TEXT("the host registered"), Tree.Host->HasRegistered());
	TestTrue(TEXT("...and begun play"), Tree.Host->HasBegunPlay());
	TestTrue(TEXT("the holder between host and cell registered"), Tree.ListHolder->HasRegistered());
	TestTrue(TEXT("...and begun play"), Tree.ListHolder->HasBegunPlay());

	// The half that separates "skipped the cell and carried on" from "stopped at the cell".
	TestTrue(TEXT("the widget walked after the cell registered"), Tree.Header->HasRegistered());
	TestTrue(TEXT("...and begun play"), Tree.Header->HasBegunPlay());

	// And the subtree that was already alive comes out untouched rather than torn down or re-run.
	TestTrue(TEXT("the cell is still registered"), Tree.Cell->HasRegistered());
	TestTrue(TEXT("...and still begun"), Tree.Cell->HasBegunPlay());
	TestTrue(TEXT("its child is still registered"), Tree.CellLabel->HasRegistered());
	TestTrue(TEXT("...and still begun"), Tree.CellLabel->HasBegunPlay());

	// The tree is one hierarchy, not two: a second pass that re-attached anything would show up here.
	TestEqual(TEXT("the host still has exactly its two children"), Tree.Host->GetChildrenCount(), 2);
	TestEqual(TEXT("and the cell is still the holder's only child"), Tree.ListHolder->GetChildrenCount(), 1);

	Tree.Host->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRegisterHierarchyBeginsAWholeFreshTreeTest,
	"DreamGUI.UserWidget.RegisterHierarchy.ATreeWhereNothingHasBegunPlayGetsEveryWidgetBegun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRegisterHierarchyBeginsAWholeFreshTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamRegisterHierarchyTestLocal;

	// The negative control for the test above. Skipping an already-begun widget and never calling
	// BeginPlay at all are indistinguishable from that test alone, and the second of those is a fix
	// somebody could plausibly reach for -- it would turn every screen inert and pass the suite.
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("a world to register into"), TestWorld.World))
	{
		return false;
	}

	UDreamUIManagerWorldSubsystem* Manager = OpenBeginPlayGate(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	if (!TestTrue(TEXT("and reports begun play"), Manager->HasBegunPlay()))
	{
		return false;
	}

	FHostTree Tree = BuildHostTree(TestWorld.World);
	if (!TestTrue(TEXT("the host tree was built"), Tree.IsUsable()))
	{
		return false;
	}

	const TArray<UDreamWidget*> Walk = WalkOrder(Tree.Host);
	if (!TestEqual(TEXT("the walk covers the whole tree"), Walk.Num(), 5))
	{
		return false;
	}
	for (UDreamWidget* Widget : Walk)
	{
		TestFalse(*FString::Printf(TEXT("'%s' starts unregistered"), *Widget->GetDisplayName()),
			Widget->HasRegistered());
		TestFalse(*FString::Printf(TEXT("'%s' starts without having begun play"), *Widget->GetDisplayName()),
			Widget->HasBegunPlay());
	}

	RegisterDreamWidgetHierarchy(Tree.Host);

	for (UDreamWidget* Widget : Walk)
	{
		TestTrue(*FString::Printf(TEXT("'%s' registered"), *Widget->GetDisplayName()),
			Widget->HasRegistered());
		TestTrue(*FString::Printf(TEXT("'%s' begun play"), *Widget->GetDisplayName()),
			Widget->HasBegunPlay());
	}

	Tree.Host->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRegisterHierarchySecondCallIsANoOpTest,
	"DreamGUI.UserWidget.RegisterHierarchy.RegisteringTheSameRootTwiceChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRegisterHierarchySecondCallIsANoOpTest::RunTest(const FString& Parameters)
{
	using namespace DreamRegisterHierarchyTestLocal;

	// The same claim from the caller's side. A caller that cannot tell whether a hierarchy was already
	// brought to life -- which is precisely the position CreateDreamWidget is in with respect to an
	// `each` list's cells -- has to be able to ask for it again and get a no-op.
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("a world to register into"), TestWorld.World))
	{
		return false;
	}

	UDreamUIManagerWorldSubsystem* Manager = OpenBeginPlayGate(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	if (!TestTrue(TEXT("and reports begun play"), Manager->HasBegunPlay()))
	{
		return false;
	}

	FHostTree Tree = BuildHostTree(TestWorld.World);
	if (!TestTrue(TEXT("the host tree was built"), Tree.IsUsable()))
	{
		return false;
	}

	RegisterDreamWidgetHierarchy(Tree.Host);
	const int32 RegisteredWidgetsAfterFirstCall = Manager->GetAllWidgetArray().Num();
	const int32 HostChildrenAfterFirstCall = Tree.Host->GetChildrenCount();
	// Reported rather than bailed on: the claim below is a DELTA, and it stays meaningful even if this
	// world ever comes to hold a widget these tests did not make.
	TestEqual(TEXT("the first call enrolled the whole tree with the manager"),
		RegisteredWidgetsAfterFirstCall, 5);

	// Fatal if the guard is gone, since every widget in the tree has begun play by now.
	RegisterDreamWidgetHierarchy(Tree.Host);

	TestEqual(TEXT("the second call enrols nothing further"),
		Manager->GetAllWidgetArray().Num(), RegisteredWidgetsAfterFirstCall);
	TestEqual(TEXT("and re-attaches nothing"), Tree.Host->GetChildrenCount(), HostChildrenAfterFirstCall);

	const TArray<UDreamWidget*> Walk = WalkOrder(Tree.Host);
	TestEqual(TEXT("the tree is still one hierarchy of five"), Walk.Num(), 5);
	for (UDreamWidget* Widget : Walk)
	{
		TestTrue(*FString::Printf(TEXT("'%s' is still registered"), *Widget->GetDisplayName()),
			Widget->HasRegistered());
		TestTrue(*FString::Printf(TEXT("'%s' is still begun"), *Widget->GetDisplayName()),
			Widget->HasBegunPlay());
	}

	Tree.Host->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRegisterHierarchyGatesBeginPlayOnTheManagerTest,
	"DreamGUI.UserWidget.RegisterHierarchy.WithoutTheManagersBeginPlayRegistrationDoesNotBeginPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRegisterHierarchyGatesBeginPlayOnTheManagerTest::RunTest(const FString& Parameters)
{
	using namespace DreamRegisterHierarchyTestLocal;

	// The control for the RIG rather than for the fix. Everything above is only meaningful if the
	// manager's flag really is what selects the BeginPlay branch and the other tests really did open
	// it; if opening it ever stops working, this test is the one that stays green while they go red,
	// and the pair says which of the two happened.
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("a world to register into"), TestWorld.World))
	{
		return false;
	}

	// Deliberately NOT opened.
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	if (!TestFalse(TEXT("a world that was never started has a manager that has not begun play"),
		Manager->HasBegunPlay()))
	{
		return false;
	}

	FHostTree Tree = BuildHostTree(TestWorld.World);
	if (!TestTrue(TEXT("the host tree was built"), Tree.IsUsable()))
	{
		return false;
	}

	RegisterDreamWidgetHierarchy(Tree.Host);

	// Registration still happens: an unregistered widget is inert, and the manager's own
	// OnWorldBeginPlay is what picks these up later -- it can only do that for widgets it knows about.
	const TArray<UDreamWidget*> Walk = WalkOrder(Tree.Host);
	TestEqual(TEXT("the walk covers the whole tree"), Walk.Num(), 5);
	for (UDreamWidget* Widget : Walk)
	{
		TestTrue(*FString::Printf(TEXT("'%s' registered"), *Widget->GetDisplayName()),
			Widget->HasRegistered());
		TestFalse(*FString::Printf(TEXT("'%s' did NOT begin play"), *Widget->GetDisplayName()),
			Widget->HasBegunPlay());
	}

	Tree.Host->DestroyWidget();
	return true;
}

#endif
