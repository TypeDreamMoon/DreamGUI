// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

/*
 * Directional navigation has to be able to reach a row that is scrolled off the end of a list.
 * FindSelectable used to drop every candidate whose centre was clipped away, so a gamepad could only
 * ever move between the rows that happened to be on screen -- and the clip test cannot tell "behind a
 * mask, gone for good" from "one scroll below the fold". These tests pin the distinction and the
 * scrolling that follows from it.
 */

namespace DreamNavigationScrollTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Viewport 120 tall over three 100-tall rows: rows 1 and 2 start below the fold. */
	UDreamLayoutContainerScrollBox* MakeListOfThree(UWorld* World, UDreamWidget*& OutScrollWidget, TArray<UDreamWidget*>& OutRows)
	{
		OutScrollWidget = MakeWidget(World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
		UDreamLayoutContainerScrollBox* ScrollBox = OutScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
		for (int32 i = 0; i < 3; i++)
		{
			OutRows.Add(MakeWidget(World, OutScrollWidget, *FString::Printf(TEXT("Row%d"), i), 180.0f, 100.0f));
		}
		OutScrollWidget->OnRegister();
		UDreamWidget::MarkLayoutForRebuild(OutScrollWidget);
		UDreamWidget::RebuildLayoutImmediately(OutScrollWidget);
		return ScrollBox;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScrollReachabilityTest,
	"DreamGUI.Navigation.Scroll.ReachabilityDistinguishesOffscreenFromHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScrollReachabilityTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScrollTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* ScrollWidget = nullptr;
	TArray<UDreamWidget*> Rows;
	UDreamLayoutContainerScrollBox* ScrollBox = MakeListOfThree(TestWorld.World, ScrollWidget, Rows);
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);

	// Row 0 occupies 0..100 of a 0..120 viewport: nothing to scroll to, so it is not "reachable by
	// scrolling" -- it is simply already there, and the clip test never rejects it in the first place.
	TestFalse(TEXT("A row already in view needs no scroll"), ScrollBox->CanScrollWidgetIntoView(Rows[0]));
	// Row 2 occupies 200..300 and the box can travel 180, so it is one scroll away.
	TestTrue(TEXT("A row below the fold can be scrolled to"), ScrollBox->CanScrollWidgetIntoView(Rows[2]));
	TestTrue(TEXT("...and navigation therefore treats it as reachable"), FDreamUINavigationScroll::IsReachableByScrolling(Rows[2]));

	// Nothing scrollable overhead: a widget hidden for any other reason must stay out of reach, or
	// navigation would happily land focus on something the player cannot see.
	UDreamWidget* Loose = MakeWidget(TestWorld.World, nullptr, TEXT("Loose"), 50.0f, 50.0f);
	TestFalse(TEXT("A widget with no scrolling ancestor is unreachable"), FDreamUINavigationScroll::IsReachableByScrolling(Loose));

	Loose->DestroyWidget();
	ScrollWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScrollRevealTest,
	"DreamGUI.Navigation.Scroll.RevealMovesTheLeastDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScrollRevealTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScrollTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* ScrollWidget = nullptr;
	TArray<UDreamWidget*> Rows;
	UDreamLayoutContainerScrollBox* ScrollBox = MakeListOfThree(TestWorld.World, ScrollWidget, Rows);
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	TestEqual(TEXT("Starts at the top"), ScrollBox->GetScrollOffset(), 0.0f);

	// Row 1 spans 100..200. The least move that shows all of it brings its trailing edge to the
	// bottom of the 120-tall viewport: offset 80, not the 150 that centring it would ask for.
	TestTrue(TEXT("Revealing row 1 scrolls"), FDreamUINavigationScroll::RevealWidget(Rows[1], false));
	TestEqual(TEXT("Row 1 sits against the bottom edge"), ScrollBox->GetScrollOffset(), 80.0f);

	// Already framed after that move, so a second request must be a no-op rather than a nudge.
	TestFalse(TEXT("Revealing it again does nothing"), FDreamUINavigationScroll::RevealWidget(Rows[1], false));
	TestEqual(TEXT("Offset unchanged"), ScrollBox->GetScrollOffset(), 80.0f);

	// Going back up aligns the leading edge instead, which is the mirror of the rule above.
	TestTrue(TEXT("Revealing row 0 scrolls back"), FDreamUINavigationScroll::RevealWidget(Rows[0], false));
	TestEqual(TEXT("Row 0 sits against the top edge"), ScrollBox->GetScrollOffset(), 0.0f);

	ScrollWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScrollPagingTest,
	"DreamGUI.Navigation.Scroll.PagingMovesAScreenfulAndTheEndKeysGoAllTheWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScrollPagingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScrollTestLocal;
	FScopedGameWorld TestWorld;

	/*
	 * A list longer than a screen was reachable one row at a time and no faster: navigation revealed
	 * the next row, and nothing else moved a scrolling container at all without a mouse wheel. A
	 * screenful is the container's own visible extent rather than a row count, which is the only
	 * definition that stays right when the rows are not all the same height.
	 */
	UDreamWidget* ScrollWidget = nullptr;
	TArray<UDreamWidget*> Rows;
	UDreamLayoutContainerScrollBox* ScrollBox = MakeListOfThree(TestWorld.World, ScrollWidget, Rows);
	if (!TestNotNull(TEXT("a list to page through"), ScrollBox))
	{
		return false;
	}
	// Viewport 120 over 300 of content: 180 of travel, so one page does not reach the end and two do.
	TestEqual(TEXT("the list has somewhere to go"), ScrollBox->GetMaxScrollOffset(), 180.0f);
	TestTrue(TEXT("a row inside it reports a scrollable ancestor"),
		FDreamUINavigationScroll::HasScrollableAncestor(Rows[0]));
	TestFalse(TEXT("...and a widget outside any list does not"),
		FDreamUINavigationScroll::HasScrollableAncestor(ScrollWidget));

	TestTrue(TEXT("a page down moves"), FDreamUINavigationScroll::ScrollByPages(Rows[0], 1.0f, false));
	TestEqual(TEXT("...by exactly one viewport"), ScrollBox->GetScrollOffset(), 120.0f);

	// The second page runs into the end and is clamped there rather than overshooting.
	TestTrue(TEXT("a second page down moves what is left"), FDreamUINavigationScroll::ScrollByPages(Rows[0], 1.0f, false));
	TestEqual(TEXT("...and stops at the end"), ScrollBox->GetScrollOffset(), 180.0f);
	TestFalse(TEXT("a page down at the end does nothing"), FDreamUINavigationScroll::ScrollByPages(Rows[0], 1.0f, false));

	TestTrue(TEXT("a page up moves back"), FDreamUINavigationScroll::ScrollByPages(Rows[0], -1.0f, false));
	TestEqual(TEXT("...by one viewport"), ScrollBox->GetScrollOffset(), 60.0f);

	// Home and End, which are the whole point of having keys for this at all.
	TestTrue(TEXT("End jumps to the bottom"), FDreamUINavigationScroll::ScrollToExtent(Rows[0], false));
	TestEqual(TEXT("...all the way"), ScrollBox->GetScrollOffset(), 180.0f);
	TestTrue(TEXT("Home jumps back to the top"), FDreamUINavigationScroll::ScrollToExtent(Rows[0], true));
	TestEqual(TEXT("...all the way"), ScrollBox->GetScrollOffset(), 0.0f);
	TestFalse(TEXT("Home again does nothing"), FDreamUINavigationScroll::ScrollToExtent(Rows[0], true));

	// The stick path: a raw delta, no animation, because a stick is already a per-frame value and an
	// interpolation restarted every frame would never arrive.
	TestTrue(TEXT("a stick delta scrolls"), FDreamUINavigationScroll::ScrollByDelta(Rows[0], FVector2D(0.0f, 45.0f)));
	TestEqual(TEXT("...by exactly what it was given"), ScrollBox->GetScrollOffset(), 45.0f);
	TestFalse(TEXT("a resting stick does nothing"),
		FDreamUINavigationScroll::ScrollByDelta(Rows[0], FVector2D::ZeroVector));
	TestEqual(TEXT("...and leaves the offset where it was"), ScrollBox->GetScrollOffset(), 45.0f);

	// A widget in no list at all: the page keys are pressed over plenty of those, and doing nothing
	// is the answer rather than an error.
	UDreamWidget* Loose = MakeWidget(TestWorld.World, nullptr, TEXT("Loose"), 50.0f, 50.0f);
	TestFalse(TEXT("paging a widget with no list does nothing"),
		FDreamUINavigationScroll::ScrollByPages(Loose, 1.0f, false));
	TestFalse(TEXT("...and neither does an end key"), FDreamUINavigationScroll::ScrollToExtent(Loose, false));

	Loose->DestroyWidget();
	ScrollWidget->DestroyWidget();
	return true;
}

#endif
