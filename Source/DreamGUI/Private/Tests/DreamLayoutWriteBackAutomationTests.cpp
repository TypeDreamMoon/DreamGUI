// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Tests/DreamLayoutInvalidationTestTypes.h"

/*
 * A layout container writes its results onto its children with the ordinary setters, and those call
 * MarkLayoutForRebuild, which walks the entire ancestor chain. The first ancestor it reaches is the
 * container that just consumed its own dirty flag - so every geometry change used to cost two full
 * CalculateLayoutTree passes, and the fork's own IncreateLayoutCalculationCounter warned about "calculated
 * layout 2 times in a frame" as if that were an anomaly rather than the steady state.
 *
 * UDreamWidget::FLayoutWriteScope stops the walk at the writer. The tests below pin both halves of that:
 * the writer is no longer re-dirtied by its own output, and everything *below* the writer still is,
 * because a nested container genuinely has to react to the size it was just handed.
 */

namespace DreamLayoutWriteBackTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Makes a widget of the given size parented to InParent (or a root when null). Not registered yet. */
	static UDreamWidget* MakeWidget(UObject* Outer, UDreamWidget* InParent, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (InParent && !Widget->TrySetParent(InParent, false))
		{
			return nullptr;
		}
		return Widget;
	}

	/**
	 * Runs the cold start and drains it.
	 *
	 * Building a fixture enqueues several rebuild roots - sizing a widget before it is parented makes it
	 * its own root, because MarkLayoutForRebuild falls back to InWidget when no layout exists yet - and the
	 * manager iterates that queue in reverse, so a descendant root can be laid out before the ancestor that
	 * sizes it and then again afterwards. That is a real defect, but it is a *dispatch* defect and it is
	 * tracked separately; it is not what these tests are about. Settle first, then count.
	 */
	static void SettleAndResetCounters(UDreamUIManagerWorldSubsystem* Manager, UDreamWidget* Root,
		std::initializer_list<UDreamLayoutPassCountingOverlay*> Overlays)
	{
		UDreamWidget::MarkLayoutForRebuild(Root);
		Manager->TickDreamUI(0.016f);
		Manager->TickDreamUI(0.016f);
		for (UDreamLayoutPassCountingOverlay* Overlay : Overlays)
		{
			Overlay->PassCount = 0;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutWriteBackDoesNotRedirtyWriterTest,
	"DreamGUI.Layout.WriteBack.WriteBackDoesNotRedirtyWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutWriteBackDoesNotRedirtyWriterTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	// Deliberately the wrong size, so the first pass is a real geometry change and not a no-op.
	UDreamWidget* Child = MakeWidget(Root, Root, 16.0f, 12.0f);
	if (!TestNotNull(TEXT("Child parented"), Child))
	{
		return false;
	}
	UDreamLayoutPassCountingOverlay* Overlay = Cast<UDreamLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Counting overlay created"), Overlay))
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();

	SettleAndResetCounters(Manager, Root, { Overlay });
	TestEqual(TEXT("Child was stretched to fill the overlay"), Child->GetSize(), Root->GetSize());
	TestEqual(TEXT("A settled layout does not recompute on an idle tick"), Overlay->PassCount, 0);

	// One real geometry change. The write-back that carries the result out to the child must not re-arm
	// the container that produced it, so this costs exactly one pass and not two.
	Root->SetWidth(400.0f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Child followed the resize"), Child->GetSize(), Root->GetSize());
	TestEqual(TEXT("One geometry change costs exactly one layout pass"), Overlay->PassCount, 1);

	// And it stays settled afterwards.
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("No trailing pass on the following tick"), Overlay->PassCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutWriteBackStillDirtiesDescendantsTest,
	"DreamGUI.Layout.WriteBack.WriteBackStillDirtiesDescendants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutWriteBackStillDirtiesDescendantsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	// Root(overlay) -> Mid(overlay) -> Leaf. Mid is sized by Root's pass, and Leaf must pick that up in
	// the SAME tick: suppressing the writer must not suppress the containers underneath it.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	UDreamWidget* Mid = MakeWidget(Root, Root, 20.0f, 10.0f);
	UDreamWidget* Leaf = MakeWidget(Mid, Mid, 4.0f, 6.0f);
	if (!TestNotNull(TEXT("Mid parented"), Mid) || !TestNotNull(TEXT("Leaf parented"), Leaf))
	{
		return false;
	}
	UDreamLayoutPassCountingOverlay* RootOverlay = Cast<UDreamLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	UDreamLayoutPassCountingOverlay* MidOverlay = Cast<UDreamLayoutPassCountingOverlay>(
		Mid->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Root overlay created"), RootOverlay) || !TestNotNull(TEXT("Mid overlay created"), MidOverlay))
	{
		return false;
	}
	Root->OnRegister();
	Mid->OnRegister();
	Leaf->OnRegister();

	SettleAndResetCounters(Manager, Root, { RootOverlay, MidOverlay });
	TestEqual(TEXT("Mid was stretched to fill Root"), Mid->GetSize(), Root->GetSize());
	TestEqual(TEXT("Leaf was stretched to fill Mid"), Leaf->GetSize(), Mid->GetSize());

	// Resizing the root must flow all the way down in ONE tick and cost each container exactly one pass:
	// suppressing the writer must not suppress the containers underneath it, or Leaf would go stale.
	Root->SetWidth(400.0f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Mid followed the root resize"), Mid->GetSize(), Root->GetSize());
	TestEqual(TEXT("Leaf followed the root resize in the same tick"), Leaf->GetSize(), Mid->GetSize());
	TestEqual(TEXT("Root resize costs Root exactly one pass"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("Root resize costs Mid exactly one pass"), MidOverlay->PassCount, 1);

	// Settled again.
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("No trailing pass on Root"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("No trailing pass on Mid"), MidOverlay->PassCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutWriteBackLeavesUnrelatedWidgetsAloneTest,
	"DreamGUI.Layout.WriteBack.WriteBackLeavesUnrelatedWidgetsAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutWriteBackLeavesUnrelatedWidgetsAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	// Two independent trees. Dirtying the second one from outside must still work normally while the
	// first is settled - the suppression is keyed on the writer's own subtree, not on "a layout is running".
	UDreamWidget* RootA = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	UDreamWidget* ChildA = MakeWidget(RootA, RootA, 16.0f, 12.0f);
	UDreamLayoutPassCountingOverlay* OverlayA = Cast<UDreamLayoutPassCountingOverlay>(
		RootA->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));

	UDreamWidget* RootB = MakeWidget(TestWorld.World, nullptr, 100.0f, 50.0f);
	UDreamWidget* ChildB = MakeWidget(RootB, RootB, 7.0f, 9.0f);
	UDreamLayoutPassCountingOverlay* OverlayB = Cast<UDreamLayoutPassCountingOverlay>(
		RootB->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Overlay A"), OverlayA) || !TestNotNull(TEXT("Overlay B"), OverlayB)
		|| !TestNotNull(TEXT("Child A"), ChildA) || !TestNotNull(TEXT("Child B"), ChildB))
	{
		return false;
	}
	RootA->OnRegister(); ChildA->OnRegister();
	RootB->OnRegister(); ChildB->OnRegister();

	UDreamWidget::MarkLayoutForRebuild(RootA);
	UDreamWidget::MarkLayoutForRebuild(RootB);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	OverlayA->PassCount = 0;
	OverlayB->PassCount = 0;

	TestEqual(TEXT("Child A filled root A"), ChildA->GetSize(), RootA->GetSize());
	TestEqual(TEXT("Child B filled root B"), ChildB->GetSize(), RootB->GetSize());

	// Touch only B; A must stay settled. The suppression is keyed on the writer's own subtree, so an
	// unrelated tree neither gains nor loses passes because another tree is laying out.
	RootB->SetHeight(64.0f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Tree B recomputed exactly once"), OverlayB->PassCount, 1);
	TestEqual(TEXT("Tree A was not dragged along"), OverlayA->PassCount, 0);
	TestEqual(TEXT("Child B followed its root"), ChildB->GetSize(), RootB->GetSize());

	RootA->DestroyWidget();
	RootB->DestroyWidget();
	return true;
}

#endif
