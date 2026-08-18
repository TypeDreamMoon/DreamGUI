// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"

/*
 * The manager's dirty-widget batch used to be iterated back-to-front, with no check for one entry sitting
 * underneath another.
 *
 * Both can easily be enqueued at once: ULexWidget::MarkLayoutForRebuild walks up looking for the topmost
 * layout on the ancestor chain, but falls back to the widget itself when it finds none - so sizing a
 * widget before it is parented, or before its container exists, enqueues that widget as its own rebuild
 * root, and it stays queued. Add an ancestor root later and the batch holds both.
 *
 * CalculateLayoutTree walks a whole subtree, so the descendant entry is redundant - and because of the
 * back-to-front iteration it usually ran FIRST, laying its subtree out against the ancestor's stale size,
 * then being laid out a second time when the ancestor's walk reached it. Observed directly: a nested
 * overlay ran once at its authored 20x10 and again at the 320x180 its parent then gave it.
 */

namespace LexLayoutDispatchTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutDescendantRootIsCoveredByAncestorTest,
	"LGUI.Layout.Dispatch.DescendantRootIsCoveredByAncestor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutDescendantRootIsCoveredByAncestorTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutDispatchTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	ULexWidget* Mid = NewObject<ULexWidget>(Root);
	ULexWidget* Leaf = NewObject<ULexWidget>(Mid);

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

	ULexLayoutPassCountingOverlay* RootOverlay = Cast<ULexLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
	ULexLayoutPassCountingOverlay* MidOverlay = Cast<ULexLayoutPassCountingOverlay>(
		Mid->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Root overlay"), RootOverlay) || !TestNotNull(TEXT("Mid overlay"), MidOverlay))
	{
		return false;
	}
	Root->OnRegister();
	Mid->OnRegister();
	Leaf->OnRegister();

	RootOverlay->PassCount = 0;
	MidOverlay->PassCount = 0;
	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);

	// The ancestor's walk covers the whole subtree, so the descendant entry must not buy a second pass.
	TestEqual(TEXT("Root laid out once"), RootOverlay->PassCount, 1);
	TestEqual(TEXT("Mid laid out once, not once stale and once correct"), MidOverlay->PassCount, 1);
	TestEqual(TEXT("Mid ended up filling Root"), Mid->GetSize(), Root->GetSize());
	TestEqual(TEXT("Leaf ended up filling Mid"), Leaf->GetSize(), Mid->GetSize());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutIndependentRootsAllRunTest,
	"LGUI.Layout.Dispatch.IndependentRootsAllRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutIndependentRootsAllRunTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutDispatchTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// The half that must not regress: pruning covers descendants only. Unrelated roots in one batch must
	// all still be laid out.
	TArray<ULexWidget*> Roots;
	TArray<ULexWidget*> Children;
	TArray<ULexLayoutPassCountingOverlay*> Overlays;
	for (int32 I = 0; I < 3; ++I)
	{
		ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
		Root->SetWidth(100.0f + I * 40.0f);
		Root->SetHeight(60.0f + I * 20.0f);
		ULexWidget* Child = NewObject<ULexWidget>(Root);
		Child->SetWidth(5.0f);
		Child->SetHeight(7.0f);
		if (!TestTrue(TEXT("Child parented"), Child->TrySetParent(Root, false)))
		{
			return false;
		}
		ULexLayoutPassCountingOverlay* Overlay = Cast<ULexLayoutPassCountingOverlay>(
			Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
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
		ULexWidget::MarkLayoutForRebuild(Roots[I]);
	}
	Manager->TickLexUI(0.016f);

	for (int32 I = 0; I < Roots.Num(); ++I)
	{
		TestEqual(*FString::Printf(TEXT("Independent root %d laid out once"), I), Overlays[I]->PassCount, 1);
		TestEqual(*FString::Printf(TEXT("Independent root %d arranged its child"), I),
			Children[I]->GetSize(), Roots[I]->GetSize());
	}

	for (ULexWidget* Root : Roots)
	{
		Root->DestroyWidget();
	}
	return true;
}

#endif
