// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

/*
 * CalculateLayout clamped ScrollOffset in place against MaxScrollOffset, and MaxScrollOffset is only ever
 * as good as the measurement behind it. Any pass that measured the content too small permanently truncated
 * the user's scroll position, and the corrected, larger range on the very next pass could not hand it back.
 *
 * The measurement really does go wrong transiently. ApplyChildRect evaluates a child's desired size BEFORE
 * it writes the child's new width, so a wrapping UDreamText is measured at its pre-arrangement width -
 * narrowing a vertical scroll box underestimates its content height for one pass. That extra pass is
 * invisible on its own, since the manager converges within the same tick; the damage was that this one
 * clamp was destructive and nothing else was.
 *
 * The offset is now re-derived from the request each pass, so an underestimate moves the view without
 * destroying the position it moved away from. The tests drive the range directly by resizing the viewport,
 * which reproduces the same transient deterministically and without depending on font metrics.
 */

namespace DreamScrollBoxOffsetPreservationTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxOffsetSurvivesTransientRangeCollapseTest,
	"DreamGUI.Layout.ScrollBox.OffsetSurvivesTransientRangeCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxOffsetSurvivesTransientRangeCollapseTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxOffsetPreservationTestLocal;
	FScopedGameWorld TestWorld;
	// Viewport 120 tall, content 3 x 100 => range 180.
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, 200.0f, 120.0f);
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	if (!TestNotNull(TEXT("ScrollBox created"), ScrollBox))
	{
		return false;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		MakeWidget(TestWorld.World, ScrollWidget, 180.0f, 100.0f);
	}
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("Range is content minus viewport"), ScrollBox->GetMaxScrollOffset(), 180.0f);
	ScrollBox->SetScrollOffset(150.0f);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	TestEqual(TEXT("Scrolled to 150"), ScrollBox->GetScrollOffset(), 150.0f);

	// Collapse the range: a viewport taller than the content leaves nothing to scroll. The visible offset
	// has to follow, but the position must not be destroyed.
	ScrollWidget->SetHeight(500.0f);
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	TestEqual(TEXT("Nothing scrolls while everything fits"), ScrollBox->GetMaxScrollOffset(), 0.0f);
	TestEqual(TEXT("The visible offset follows the collapsed range"), ScrollBox->GetScrollOffset(), 0.0f);

	// Restore the range. The position the user had must come back.
	ScrollWidget->SetHeight(120.0f);
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	TestEqual(TEXT("Range is back"), ScrollBox->GetMaxScrollOffset(), 180.0f);
	TestEqual(TEXT("The scroll position is restored, not truncated"), ScrollBox->GetScrollOffset(), 150.0f);

	ScrollWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxExplicitScrollStillWinsTest,
	"DreamGUI.Layout.ScrollBox.ExplicitScrollAfterCollapseWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxExplicitScrollStillWinsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxOffsetPreservationTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, 200.0f, 120.0f);
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	if (!TestNotNull(TEXT("ScrollBox created"), ScrollBox))
	{
		return false;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		MakeWidget(TestWorld.World, ScrollWidget, 180.0f, 100.0f);
	}
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	ScrollBox->SetScrollOffset(150.0f);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	// The remembered request must not outrank a later explicit scroll: moving to the top has to stick,
	// including across a range collapse and restore.
	ScrollBox->SetScrollOffset(0.0f);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	TestEqual(TEXT("Scrolled back to the top"), ScrollBox->GetScrollOffset(), 0.0f);

	ScrollWidget->SetHeight(500.0f);
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	ScrollWidget->SetHeight(120.0f);
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("The old position does not come back after an explicit scroll to the top"),
		ScrollBox->GetScrollOffset(), 0.0f);

	ScrollWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxGenuineShrinkStillClampsTest,
	"DreamGUI.Layout.ScrollBox.GenuineContentShrinkStillClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxGenuineShrinkStillClampsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxOffsetPreservationTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, 200.0f, 120.0f);
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	if (!TestNotNull(TEXT("ScrollBox created"), ScrollBox))
	{
		return false;
	}
	TArray<UDreamWidget*> Blocks;
	for (int32 I = 0; I < 3; ++I)
	{
		Blocks.Add(MakeWidget(TestWorld.World, ScrollWidget, 180.0f, 100.0f));
	}
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	ScrollBox->SetScrollOffset(180.0f);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
	TestEqual(TEXT("Scrolled to the end"), ScrollBox->GetScrollOffset(), 180.0f);

	// The half that must not regress: content that genuinely shrinks and stays shrunk still clamps the
	// visible offset to the smaller range.
	Blocks[2]->DestroyWidget();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("Range shrank with the content"), ScrollBox->GetMaxScrollOffset(), 80.0f);
	TestEqual(TEXT("The visible offset is clamped to the smaller range"),
		ScrollBox->GetScrollOffset(), 80.0f);

	ScrollWidget->DestroyWidget();
	return true;
}

#endif
