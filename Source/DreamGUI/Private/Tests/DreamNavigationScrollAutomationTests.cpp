// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Engine/World.h"

/*
 * Directional navigation has to be able to reach a row that is scrolled off the end of a list.
 * FindSelectable used to drop every candidate whose centre was clipped away, so a gamepad could only
 * ever move between the rows that happened to be on screen -- and the clip test cannot tell "behind a
 * mask, gone for good" from "one scroll below the fold". These tests pin the distinction and the
 * scrolling that follows from it.
 */

namespace DreamNavigationScrollTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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

#endif
