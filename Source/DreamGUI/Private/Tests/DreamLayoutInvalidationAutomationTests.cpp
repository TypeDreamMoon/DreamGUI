// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Tests/DreamLayoutInvalidationTestTypes.h"

/*
 * Regression coverage for the three acknowledged invalidation holes in the cached layout tree:
 *
 *   (a) a collapsed subtree that becomes visible from inside a tick-driven layout pass hits the
 *       bIsExecutingLayout guard in MarkRebuildAllLayoutTree, so the invalidation is swallowed;
 *   (b) OnRegister never invalidates the cache, so a widget registered after the cache was built
 *       used to stay baked out of it permanently;
 *   (c) UpdateLayout can re-enter CalculateLayoutTree through RebuildLayoutImmediately, and the
 *       FindOrAdd there can rehash the map (or a visibility flip can Empty it) out from under the
 *       outer iteration.
 *
 * The fix collects the full subtree, filters per-widget at update time, and iterates a copy. Each
 * test below fails (or crashes) against the pruned-tree/by-reference implementation.
 */

namespace DreamLayoutInvalidationTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};

	/** Root -> NestedPanel(Collapsed) -> NestedChild, every widget registered, overlays everywhere. */
	struct FCollapsedNestedFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* NestedPanel = nullptr;
		UDreamWidget* NestedChild = nullptr;
		UDreamLayoutVisibilityFlipOverlay* FlipOverlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			NestedPanel = NewObject<UDreamWidget>(Root);
			NestedChild = NewObject<UDreamWidget>(NestedPanel);
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			NestedPanel->SetWidth(80.0f);
			NestedPanel->SetHeight(60.0f);
			NestedChild->SetWidth(16.0f);
			NestedChild->SetHeight(12.0f);
			if (!NestedPanel->TrySetParent(Root, false) || !NestedChild->TrySetParent(NestedPanel, false))
			{
				return false;
			}
			FlipOverlay = Cast<UDreamLayoutVisibilityFlipOverlay>(
				Root->CreateNewLayoutContainer(UDreamLayoutVisibilityFlipOverlay::StaticClass()));
			if (!FlipOverlay || !NestedPanel->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
			{
				return false;
			}
			Root->OnRegister();
			NestedPanel->OnRegister();
			NestedChild->OnRegister();
			NestedPanel->SetVisibility(EDreamWidgetVisibility::Collapsed);
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutRevealDuringTickLayoutPassTest,
	"DreamGUI.Layout.Invalidation.RevealDuringTickLayoutPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutRevealDuringTickLayoutPassTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	TestNotNull(TEXT("DreamUI manager subsystem exists in the test world"), Manager);
	FCollapsedNestedFixture Fixture;
	if (!Manager || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	// Build the cached tree while the nested panel is collapsed, driven through the real tick path.
	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickDreamUI(0.016f);

	// Reveal the panel from inside the next tick-driven pass: bIsExecutingLayout is set, so the
	// MarkRebuildAllLayoutTree issued by the visibility change is swallowed by the guard. The revealed
	// subtree must still be laid out by the end of this same TickDreamUI call.
	Fixture.FlipOverlay->WidgetToReveal = Fixture.NestedPanel;
	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("Reveal fired exactly once, from inside the layout pass"), Fixture.FlipOverlay->FlipCount, 1);
	TestEqual(TEXT("Panel revealed mid-pass fills the root before the tick ends"),
		Fixture.NestedPanel->GetSize(), Fixture.Root->GetSize());
	TestEqual(TEXT("Nested child revealed mid-pass is laid out before the tick ends"),
		Fixture.NestedChild->GetSize(), Fixture.NestedPanel->GetSize());

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutRevealDuringImmediateRebuildTest,
	"DreamGUI.Layout.Invalidation.RevealDuringImmediateRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutRevealDuringImmediateRebuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	FCollapsedNestedFixture Fixture;
	if (!UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	UDreamWidget::RebuildLayoutImmediately(Fixture.Root);

	// Reveal from inside an immediate rebuild: bIsExecutingLayout is NOT set here, so the visibility
	// change empties the layout tree map while CalculateLayoutTree is iterating it. Iterating the map
	// entry by reference dies right here; the copy must survive the wipe.
	Fixture.FlipOverlay->WidgetToReveal = Fixture.NestedPanel;
	// The previous rebuild consumed the dirty flag, and an arrange pass only runs when there is one - so
	// re-dirty first, or the overlay is asked to reveal from inside a pass that never happens.
	UDreamWidget::MarkLayoutForRebuild(Fixture.Root);
	UDreamWidget::RebuildLayoutImmediately(Fixture.Root);
	TestEqual(TEXT("Reveal fired exactly once, from inside the immediate rebuild"), Fixture.FlipOverlay->FlipCount, 1);
	TestTrue(TEXT("Revealed panel is layout-visible after the rebuild that revealed it"),
		Fixture.NestedPanel->GetLayoutVisibleInHierarchy());

	// The wipe invalidated the cache legitimately; the next rebuild lays the revealed subtree out.
	UDreamWidget::RebuildLayoutImmediately(Fixture.Root);
	TestEqual(TEXT("Revealed panel fills the root"), Fixture.NestedPanel->GetSize(), Fixture.Root->GetSize());
	TestEqual(TEXT("Revealed nested child is laid out"),
		Fixture.NestedChild->GetSize(), Fixture.NestedPanel->GetSize());

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutLateRegistrationJoinsCachedTreeTest,
	"DreamGUI.Layout.Invalidation.LateRegistrationJoinsCachedTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutLateRegistrationJoinsCachedTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	UDreamWidget* LatePanel = NewObject<UDreamWidget>(Root);
	UDreamWidget* LateGrandchild = NewObject<UDreamWidget>(LatePanel);
	if (!UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World))
	{
		return false;
	}
	Root->SetWidth(320.0f);
	Root->SetHeight(180.0f);
	LatePanel->SetWidth(80.0f);
	LatePanel->SetHeight(60.0f);
	LateGrandchild->SetWidth(16.0f);
	LateGrandchild->SetHeight(12.0f);
	TestTrue(TEXT("Late panel joins the root"), LatePanel->TrySetParent(Root, false));
	TestTrue(TEXT("Late grandchild joins its panel"), LateGrandchild->TrySetParent(LatePanel, false));
	TestNotNull(TEXT("Root overlay is created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());
	TestNotNull(TEXT("Late panel overlay is created"), LatePanel->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());

	// Only the root is registered when the cached tree is first built.
	Root->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);
	TestEqual(TEXT("Unregistered panel's own layout does not run yet"),
		LateGrandchild->GetSize(), FVector2D(16.0, 12.0));

	// OnRegister issues no cache invalidation of its own — the cached tree from the build above is
	// reused as-is. The late-registered panel must still get its layout pass.
	LatePanel->OnRegister();
	LateGrandchild->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);
	TestEqual(TEXT("Late-registered panel fills the root"), LatePanel->GetSize(), Root->GetSize());
	TestEqual(TEXT("Late-registered panel lays out its child"), LateGrandchild->GetSize(), LatePanel->GetSize());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutReentrantRebuildSurvivesRehashTest,
	"DreamGUI.Layout.Invalidation.ReentrantRebuildSurvivesRehash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutReentrantRebuildSurvivesRehashTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	if (!UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World))
	{
		return false;
	}

	UDreamWidget* OuterRoot = NewObject<UDreamWidget>(TestWorld.World);
	UDreamWidget* OuterChild = NewObject<UDreamWidget>(OuterRoot);
	OuterRoot->SetWidth(320.0f);
	OuterRoot->SetHeight(180.0f);
	OuterChild->SetWidth(16.0f);
	OuterChild->SetHeight(12.0f);
	TestTrue(TEXT("Outer child joins the outer root"), OuterChild->TrySetParent(OuterRoot, false));
	UDreamLayoutReentrantRebuildOverlay* ReentrantOverlay = Cast<UDreamLayoutReentrantRebuildOverlay>(
		OuterRoot->CreateNewLayoutContainer(UDreamLayoutReentrantRebuildOverlay::StaticClass()));
	TestNotNull(TEXT("Re-entrant overlay is created"), ReentrantOverlay);
	if (!ReentrantOverlay)
	{
		return false;
	}
	OuterRoot->OnRegister();
	OuterChild->OnRegister();

	// Independent layout roots that are NOT in the layout tree map yet: every nested rebuild inserts
	// one, so the map rehashes several times underneath the outer iteration.
	constexpr int32 SideRootCount = 48;
	TArray<UDreamWidget*> SideRoots;
	TArray<UDreamWidget*> SideChildren;
	for (int32 i = 0; i < SideRootCount; i++)
	{
		UDreamWidget* SideRoot = NewObject<UDreamWidget>(TestWorld.World);
		UDreamWidget* SideChild = NewObject<UDreamWidget>(SideRoot);
		SideRoot->SetWidth(100.0f + i);
		SideRoot->SetHeight(50.0f + i);
		SideChild->SetWidth(1.0f);
		SideChild->SetHeight(1.0f);
		if (!SideChild->TrySetParent(SideRoot, false)
			|| !SideRoot->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
		{
			return false;
		}
		SideRoot->OnRegister();
		SideChild->OnRegister();
		SideRoots.Add(SideRoot);
		SideChildren.Add(SideChild);
	}

	// Prime the outer root's cache entry, then trigger the re-entrant storm from inside its next pass.
	UDreamWidget::MarkLayoutForRebuild(OuterRoot);
	UDreamWidget::RebuildLayoutImmediately(OuterRoot);
	ReentrantOverlay->RootsToRebuild.Append(SideRoots);
	// Same reason as above: the priming rebuild consumed the dirty flag, so the storm needs a real pass
	// to be launched from.
	UDreamWidget::MarkLayoutForRebuild(OuterRoot);
	UDreamWidget::RebuildLayoutImmediately(OuterRoot);

	TestEqual(TEXT("Re-entrant rebuild fired exactly once"), ReentrantOverlay->ReentryCount, 1);
	TestEqual(TEXT("Outer child stays laid out across the re-entrant storm"),
		OuterChild->GetSize(), OuterRoot->GetSize());
	for (int32 i = 0; i < SideRootCount; i++)
	{
		if (SideChildren[i]->GetSize() != SideRoots[i]->GetSize())
		{
			AddError(FString::Printf(TEXT("Side root %d child was not laid out by the nested rebuild"), i));
		}
	}

	OuterRoot->DestroyWidget();
	for (UDreamWidget* SideRoot : SideRoots)
	{
		SideRoot->DestroyWidget();
	}
	return true;
}

#endif
