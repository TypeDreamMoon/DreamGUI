// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexLayoutContainerGrid.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"

/*
 * ULexLayoutContainerGrid::GetLayoutPreferredSize returned zero unconditionally.
 *
 * For an axis of Ratio tracks that is the honest answer - a ratio is a share of something the parent has
 * not decided yet - and callers read zero as "I contribute no desired size". For an axis of Fixed tracks
 * it is not: that axis is entirely known up front. Reporting zero sent FLexLayoutSize::Calculate's Auto
 * branch, which only accepts a positive value, through to its 100 unit Fixed default, so a grid that
 * should shrink-wrap became a 100x100 box.
 *
 * Mixed axes keep reporting zero deliberately: a Ratio track has no contribution until the outer size
 * exists, so there is no total to give, and reporting the fixed part alone would just be a smaller wrong
 * number.
 */

namespace LexGridPreferredSizeTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static ULexLayoutContainerGrid* MakeGrid(UWorld* World, ULexWidget*& OutWidget,
		const TArray<FLexLayoutGridSize>& Columns, const TArray<FLexLayoutGridSize>& Rows,
		FVector2D Spacing = FVector2D::ZeroVector, FMargin Padding = FMargin())
	{
		OutWidget = NewObject<ULexWidget>(World);
		OutWidget->SetWidth(400.0f);
		OutWidget->SetHeight(300.0f);
		ULexLayoutContainerGrid* Grid = OutWidget->CreateNewLayoutContainer<ULexLayoutContainerGrid>();
		if (!Grid)
		{
			return nullptr;
		}
		Grid->SetColumns(Columns);
		Grid->SetRows(Rows);
		Grid->SetSpacing(Spacing);
		Grid->SetPadding(Padding);
		OutWidget->OnRegister();
		return Grid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexGridFixedTracksReportTheirTotalTest,
	"LGUI.Layout.GridPreferredSize.FixedTracksReportTheirTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexGridFixedTracksReportTheirTotalTest::RunTest(const FString& Parameters)
{
	using namespace LexGridPreferredSizeTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Widget = nullptr;
	// 3 columns of 40 with 10 spacing and 5+7 padding => 120 + 20 + 12 = 152.
	// 2 rows of 30 with 4 spacing and 3+1 padding => 60 + 4 + 4 = 68.
	ULexLayoutContainerGrid* Grid = MakeGrid(TestWorld.World, Widget,
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 40.0f),
		  FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 40.0f),
		  FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 40.0f) },
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 30.0f),
		  FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 30.0f) },
		FVector2D(10.0, 4.0), FMargin(5.0f, 3.0f, 7.0f, 1.0f));
	if (!TestNotNull(TEXT("Grid created"), Grid))
	{
		return false;
	}

	const FVector2f Preferred = Grid->GetLayoutPreferredSize();
	TestTrue(TEXT("Fixed columns report tracks plus spacing plus padding"),
		FMath::IsNearlyEqual(Preferred.X, 152.0f, 0.01f));
	TestTrue(TEXT("Fixed rows report tracks plus spacing plus padding"),
		FMath::IsNearlyEqual(Preferred.Y, 68.0f, 0.01f));

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexGridRatioAndMixedTracksStayZeroTest,
	"LGUI.Layout.GridPreferredSize.RatioAndMixedTracksStayZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexGridRatioAndMixedTracksStayZeroTest::RunTest(const FString& Parameters)
{
	using namespace LexGridPreferredSizeTestLocal;
	FScopedGameWorld TestWorld;

	// All-Ratio: no intrinsic size, and zero is what callers must keep seeing.
	ULexWidget* RatioWidget = nullptr;
	ULexLayoutContainerGrid* RatioGrid = MakeGrid(TestWorld.World, RatioWidget,
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 1.0f),
		  FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 2.0f) },
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 1.0f) });
	if (!TestNotNull(TEXT("Ratio grid created"), RatioGrid))
	{
		return false;
	}
	const FVector2f RatioPreferred = RatioGrid->GetLayoutPreferredSize();
	TestEqual(TEXT("All-ratio columns contribute no desired width"), RatioPreferred.X, 0.0f);
	TestEqual(TEXT("All-ratio rows contribute no desired height"), RatioPreferred.Y, 0.0f);

	// Mixed axis: one Ratio track is enough to make the total undefined.
	ULexWidget* MixedWidget = nullptr;
	ULexLayoutContainerGrid* MixedGrid = MakeGrid(TestWorld.World, MixedWidget,
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 40.0f),
		  FLexLayoutGridSize(ELexLayoutGridSizeType::Ratio, 1.0f) },
		{ FLexLayoutGridSize(ELexLayoutGridSizeType::Fixed, 30.0f) });
	if (!TestNotNull(TEXT("Mixed grid created"), MixedGrid))
	{
		return false;
	}
	const FVector2f MixedPreferred = MixedGrid->GetLayoutPreferredSize();
	TestEqual(TEXT("A mixed axis contributes no desired size"), MixedPreferred.X, 0.0f);
	TestTrue(TEXT("The all-fixed axis of the same grid still reports"),
		FMath::IsNearlyEqual(MixedPreferred.Y, 30.0f, 0.01f));

	RatioWidget->DestroyWidget();
	MixedWidget->DestroyWidget();
	return true;
}

#endif
