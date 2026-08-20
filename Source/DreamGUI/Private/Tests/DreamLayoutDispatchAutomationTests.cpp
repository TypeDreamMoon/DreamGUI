// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Tests/DreamLayoutInvalidationTestTypes.h"

/*
 * The manager's dirty-widget batch used to be iterated back-to-front, with no check for one entry sitting
 * underneath another.
 *
 * Both can easily be enqueued at once: UDreamWidget::MarkLayoutForRebuild walks up looking for the topmost
 * layout on the ancestor chain, but falls back to the widget itself when it finds none - so sizing a
 * widget before it is parented, or before its container exists, enqueues that widget as its own rebuild
 * root, and it stays queued. Add an ancestor root later and the batch holds both.
 *
 * CalculateLayoutTree walks a whole subtree, so the descendant entry is redundant - and because of the
 * back-to-front iteration it usually ran FIRST, laying its subtree out against the ancestor's stale size,
 * then being laid out a second time when the ancestor's walk reached it. Observed directly: a nested
 * overlay ran once at its authored 20x10 and again at the 320x180 its parent then gave it.
 */

namespace DreamLayoutDispatchTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutDescendantRootIsCoveredByAncestorTest,
	"DreamGUI.Layout.Dispatch.DescendantRootIsCoveredByAncestor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutDescendantRootIsCoveredByAncestorTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutDispatchTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	UDreamWidget* Mid = NewObject<UDreamWidget>(Root);
	UDreamWidget* Leaf = NewObject<UDreamWidget>(Mid);

	// Size before parenting, deliberately: with no layout anywhere on the ancestor chain yet,
	// MarkLayoutForRebuild falls back to the widget itself, so each of these enqueues itself as its own
	// rebuild root and the batch ends up holding all three.
	//
	// Size the DESCENDANTS first, so they sit ahead of their ancestor in the batch. That is the case the
	// pruning has to handle: fixing only the iteration direction would still lay Mid out at its stale
	// 20x10 here, because enqueue order is arbitrary and does not follow the hierarchy.
	Mid->SetWidth(20.0f);
	Mid->SetHeight(10.0f);
	Leaf->SetWidth(4.0f);
	Leaf->SetHeight(6.0f);
	Root->SetWidth(320.0f);
	Root->SetHeight(180.0f);

	if (!TestTrue(TEXT("Mid parented"), Mid->TrySetParent(Root, false))
		|| !TestTrue(TEXT("Leaf parented"), Leaf->TrySetParent(Mid, false)))
	{
		return false;
	}

	UDreamLayoutPassCountingOverlay* RootOverlay = Cast<UDreamLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	UDreamLayoutPassCountingOverlay* MidOverlay = Cast<UDreamLayoutPassCountingOverlay>(
		Mid->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Root overlay"), RootOverlay) || !TestNotNull(TEXT("Mid overlay"), MidOverlay))
	{
		return false;
	}
	Root->OnRegister();
	Mid->OnRegister();
	Leaf->OnRegister();

	RootOverlay->PassCount = 0;
	MidOverlay->PassCount = 0;
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	// The ancestor's walk covers the whole subtree, so the descendant entry must not buy a second pass.
	TestEqual(TEXT("Root laid out once"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("Mid laid out once, not once stale and once correct"), MidOverlay->PassCount, 1);
	TestEqual(TEXT("Mid ended up filling Root"), Mid->GetSize(), Root->GetSize());
	TestEqual(TEXT("Leaf ended up filling Mid"), Leaf->GetSize(), Mid->GetSize());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutIndependentRootsAllRunTest,
	"DreamGUI.Layout.Dispatch.IndependentRootsAllRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutIndependentRootsAllRunTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutDispatchTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// The half that must not regress: pruning covers descendants only. Unrelated roots in one batch must
	// all still be laid out.
	TArray<UDreamWidget*> Roots;
	TArray<UDreamWidget*> Children;
	TArray<UDreamLayoutPassCountingOverlay*> Overlays;
	for (int32 I = 0; I < 3; ++I)
	{
		UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
		Root->SetWidth(100.0f + I * 40.0f);
		Root->SetHeight(60.0f + I * 20.0f);
		UDreamWidget* Child = NewObject<UDreamWidget>(Root);
		Child->SetWidth(5.0f);
		Child->SetHeight(7.0f);
		if (!TestTrue(TEXT("Child parented"), Child->TrySetParent(Root, false)))
		{
			return false;
		}
		UDreamLayoutPassCountingOverlay* Overlay = Cast<UDreamLayoutPassCountingOverlay>(
			Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
		if (!TestNotNull(TEXT("Overlay"), Overlay))
		{
			return false;
		}
		Root->OnRegister();
		Child->OnRegister();
		Roots.Add(Root);
		Children.Add(Child);
		Overlays.Add(Overlay);
	}

	for (int32 I = 0; I < Roots.Num(); ++I)
	{
		Overlays[I]->PassCount = 0;
		UDreamWidget::MarkLayoutForRebuild(Roots[I]);
	}
	Manager->TickDreamUI(0.016f);

	for (int32 I = 0; I < Roots.Num(); ++I)
	{
		TestEqual(*FString::Printf(TEXT("Independent root %d laid out once"), I), Overlays[I]->PassCount, 1);
		TestEqual(*FString::Printf(TEXT("Independent root %d arranged its child"), I),
			Children[I]->GetSize(), Roots[I]->GetSize());
	}

	for (UDreamWidget* Root : Roots)
	{
		Root->DestroyWidget();
	}
	return true;
}

#endif
