// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"

/*
 * A layout container writes its results onto its children with the ordinary setters, and those call
 * MarkLayoutForRebuild, which walks the entire ancestor chain. The first ancestor it reaches is the
 * container that just consumed its own dirty flag - so every geometry change used to cost two full
 * CalculateLayoutTree passes, and the fork's own IncreateLayoutCalculationCounter warned about "calculated
 * layout 2 times in a frame" as if that were an anomaly rather than the steady state.
 *
 * ULexWidget::FLayoutWriteScope stops the walk at the writer. The tests below pin both halves of that:
 * the writer is no longer re-dirtied by its own output, and everything *below* the writer still is,
 * because a nested container genuinely has to react to the size it was just handed.
 */

namespace LexLayoutWriteBackTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Makes a widget of the given size parented to InParent (or a root when null). Not registered yet. */
	static ULexWidget* MakeWidget(UObject* Outer, ULexWidget* InParent, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer);
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
	static void SettleAndResetCounters(ULexUIManagerWorldSubsystem* Manager, ULexWidget* Root,
		std::initializer_list<ULexLayoutPassCountingOverlay*> Overlays)
	{
		ULexWidget::MarkLayoutForRebuild(Root);
		Manager->TickLexUI(0.016f);
		Manager->TickLexUI(0.016f);
		for (ULexLayoutPassCountingOverlay* Overlay : Overlays)
		{
			Overlay->PassCount = 0;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutWriteBackDoesNotRedirtyWriterTest,
	"LGUI.Layout.WriteBack.WriteBackDoesNotRedirtyWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutWriteBackDoesNotRedirtyWriterTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	// Deliberately the wrong size, so the first pass is a real geometry change and not a no-op.
	ULexWidget* Child = MakeWidget(Root, Root, 16.0f, 12.0f);
	if (!TestNotNull(TEXT("Child parented"), Child))
	{
		return false;
	}
	ULexLayoutPassCountingOverlay* Overlay = Cast<ULexLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
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
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Child followed the resize"), Child->GetSize(), Root->GetSize());
	TestEqual(TEXT("One geometry change costs exactly one layout pass"), Overlay->PassCount, 1);

	// And it stays settled afterwards.
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("No trailing pass on the following tick"), Overlay->PassCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutWriteBackStillDirtiesDescendantsTest,
	"LGUI.Layout.WriteBack.WriteBackStillDirtiesDescendants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutWriteBackStillDirtiesDescendantsTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	// Root(overlay) -> Mid(overlay) -> Leaf. Mid is sized by Root's pass, and Leaf must pick that up in
	// the SAME tick: suppressing the writer must not suppress the containers underneath it.
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	ULexWidget* Mid = MakeWidget(Root, Root, 20.0f, 10.0f);
	ULexWidget* Leaf = MakeWidget(Mid, Mid, 4.0f, 6.0f);
	if (!TestNotNull(TEXT("Mid parented"), Mid) || !TestNotNull(TEXT("Leaf parented"), Leaf))
	{
		return false;
	}
	ULexLayoutPassCountingOverlay* RootOverlay = Cast<ULexLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
	ULexLayoutPassCountingOverlay* MidOverlay = Cast<ULexLayoutPassCountingOverlay>(
		Mid->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
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
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Mid followed the root resize"), Mid->GetSize(), Root->GetSize());
	TestEqual(TEXT("Leaf followed the root resize in the same tick"), Leaf->GetSize(), Mid->GetSize());
	TestEqual(TEXT("Root resize costs Root exactly one pass"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("Root resize costs Mid exactly one pass"), MidOverlay->PassCount, 1);

	// Settled again.
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("No trailing pass on Root"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("No trailing pass on Mid"), MidOverlay->PassCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutWriteBackLeavesUnrelatedWidgetsAloneTest,
	"LGUI.Layout.WriteBack.WriteBackLeavesUnrelatedWidgetsAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutWriteBackLeavesUnrelatedWidgetsAloneTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutWriteBackTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists in the test world"), Manager))
	{
		return false;
	}

	// Two independent trees. Dirtying the second one from outside must still work normally while the
	// first is settled - the suppression is keyed on the writer's own subtree, not on "a layout is running".
	ULexWidget* RootA = MakeWidget(TestWorld.World, nullptr, 320.0f, 180.0f);
	ULexWidget* ChildA = MakeWidget(RootA, RootA, 16.0f, 12.0f);
	ULexLayoutPassCountingOverlay* OverlayA = Cast<ULexLayoutPassCountingOverlay>(
		RootA->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));

	ULexWidget* RootB = MakeWidget(TestWorld.World, nullptr, 100.0f, 50.0f);
	ULexWidget* ChildB = MakeWidget(RootB, RootB, 7.0f, 9.0f);
	ULexLayoutPassCountingOverlay* OverlayB = Cast<ULexLayoutPassCountingOverlay>(
		RootB->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Overlay A"), OverlayA) || !TestNotNull(TEXT("Overlay B"), OverlayB)
		|| !TestNotNull(TEXT("Child A"), ChildA) || !TestNotNull(TEXT("Child B"), ChildB))
	{
		return false;
	}
	RootA->OnRegister(); ChildA->OnRegister();
	RootB->OnRegister(); ChildB->OnRegister();

	ULexWidget::MarkLayoutForRebuild(RootA);
	ULexWidget::MarkLayoutForRebuild(RootB);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	OverlayA->PassCount = 0;
	OverlayB->PassCount = 0;

	TestEqual(TEXT("Child A filled root A"), ChildA->GetSize(), RootA->GetSize());
	TestEqual(TEXT("Child B filled root B"), ChildB->GetSize(), RootB->GetSize());

	// Touch only B; A must stay settled. The suppression is keyed on the writer's own subtree, so an
	// unrelated tree neither gains nor loses passes because another tree is laying out.
	RootB->SetHeight(64.0f);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Tree B recomputed exactly once"), OverlayB->PassCount, 1);
	TestEqual(TEXT("Tree A was not dragged along"), OverlayA->PassCount, 0);
	TestEqual(TEXT("Child B followed its root"), ChildB->GetSize(), RootB->GetSize());

	RootA->DestroyWidget();
	RootB->DestroyWidget();
	return true;
}

#endif
