// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamText.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamTextBoxTestLocal
{
	/** Where the layout reads a box's centre from: it is handed a size and a pivot, never a rect. */
	FVector2f CentreOf(const FVector2f& InSize, const FVector2f& InPivot)
	{
		return FVector2f(InSize.X * (0.5f - InPivot.X), InSize.Y * (0.5f - InPivot.Y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMarginInsetsEachEdgeTest,
	"DreamGUI.Text.Margin.EachEdgeIsInsetByItsOwnValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMarginInsetsEachEdgeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextBoxTestLocal;

	const FVector2f Size(200.0f, 100.0f);
	const FVector2f Pivot(0.5f, 0.5f);

	// Left only. The box loses 40 of width, and its centre moves right by half of what it lost --
	// shrinking around the same pivot instead would have taken 20 off each side.
	FVector2f OutSize, OutPivot;
	UDreamText::GetContentBox(Size, Pivot, FMargin(40.0f, 0.0f, 0.0f, 0.0f), OutSize, OutPivot);
	TestEqual(TEXT("a left margin narrows the box by its own width"), OutSize.X, 160.0f, 0.001f);
	TestEqual(TEXT("and moves the box right by half of it"), CentreOf(OutSize, OutPivot).X, 20.0f, 0.001f);
	TestEqual(TEXT("leaving the other axis alone"), OutSize.Y, 100.0f, 0.001f);

	// Top only. Y counts upward, so a top margin has to push the box DOWN.
	UDreamText::GetContentBox(Size, Pivot, FMargin(0.0f, 30.0f, 0.0f, 0.0f), OutSize, OutPivot);
	TestEqual(TEXT("a top margin shortens the box"), OutSize.Y, 70.0f, 0.001f);
	TestEqual(TEXT("and pushes it down, because Y is up"), CentreOf(OutSize, OutPivot).Y, -15.0f, 0.001f);

	// Symmetric margins take the size but must not move the box at all.
	UDreamText::GetContentBox(Size, Pivot, FMargin(10.0f, 10.0f, 10.0f, 10.0f), OutSize, OutPivot);
	TestEqual(TEXT("even margins narrow both axes"), OutSize.X, 180.0f, 0.001f);
	TestEqual(TEXT("even margins shorten both axes"), OutSize.Y, 80.0f, 0.001f);
	TestTrue(TEXT("and leave the box exactly where it was"),
		CentreOf(OutSize, OutPivot).Equals(FVector2f::ZeroVector, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMarginHonoursPivotTest,
	"DreamGUI.Text.Margin.TheInsetIsMeasuredFromTheWidgetNotThePivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMarginHonoursPivotTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextBoxTestLocal;

	// A corner pivot: the widget's rect runs from the pivot rather than around it, so a margin that
	// ignored the pivot would inset the wrong edges.
	const FVector2f Size(200.0f, 100.0f);
	const FVector2f Pivot(0.0f, 0.0f);
	const FVector2f BareCentre = CentreOf(Size, Pivot);

	FVector2f OutSize, OutPivot;
	UDreamText::GetContentBox(Size, Pivot, FMargin(40.0f, 0.0f, 0.0f, 0.0f), OutSize, OutPivot);
	TestEqual(TEXT("the box still loses exactly the left margin"), OutSize.X, 160.0f, 0.001f);
	TestEqual(TEXT("and still moves right by half of it, wherever the pivot is"),
		CentreOf(OutSize, OutPivot).X - BareCentre.X, 20.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMarginDegenerateTest,
	"DreamGUI.Text.Margin.AMarginWiderThanTheWidgetLeavesNoRoomRatherThanNegativeRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMarginDegenerateTest::RunTest(const FString& Parameters)
{
	// An inside-out box would wrap at a negative width, which the layout has no sane reading of.
	FVector2f OutSize, OutPivot;
	UDreamText::GetContentBox(FVector2f(100.0f, 50.0f), FVector2f(0.5f, 0.5f),
		FMargin(80.0f, 0.0f, 80.0f, 0.0f), OutSize, OutPivot);
	TestEqual(TEXT("width bottoms out at zero"), OutSize.X, 0.0f, 0.001f);
	TestTrue(TEXT("and the pivot stays finite so nothing downstream sees a NaN"),
		FMath::IsFinite(OutPivot.X) && FMath::IsFinite(OutPivot.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBestFitPicksTheLargestThatFitsTest,
	"DreamGUI.Text.BestFit.PicksTheLargestSizeThatStillFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBestFitPicksTheLargestThatFitsTest::RunTest(const FString& Parameters)
{
	// A stand-in for the real layout: text that grows in proportion to the font size. The search
	// only requires that fitting is monotonic in size, which this has and real text also has.
	int32 Measurements = 0;
	auto Measure = [&Measurements](float InSize)
	{
		++Measurements;
		return FVector2f(InSize * 4.0f, InSize * 2.0f);
	};

	// Height is the binding constraint: 2 per unit into 101 leaves 50.
	const float Chosen = UDreamText::FindBestFitFontSize(FVector2f(1000.0f, 101.0f), 8.0f, 200.0f, Measure);
	TestEqual(TEXT("the largest whole size that fits"), Chosen, 50.0f, 0.001f);
	TestTrue(TEXT("and it bisected rather than trying every size"), Measurements < 12);

	// Width binding instead.
	TestEqual(TEXT("whichever axis runs out first is the one that decides"),
		UDreamText::FindBestFitFontSize(FVector2f(100.0f, 10000.0f), 8.0f, 200.0f, Measure), 25.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBestFitBoundsTest,
	"DreamGUI.Text.BestFit.StaysWithinItsBoundsAtBothEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBestFitBoundsTest::RunTest(const FString& Parameters)
{
	auto Measure = [](float InSize) { return FVector2f(InSize, InSize); };

	// Already fits: Best Fit must not grow the text past the size the author asked for.
	TestEqual(TEXT("a box with room to spare still draws at the authored size"),
		UDreamText::FindBestFitFontSize(FVector2f(9999.0f, 9999.0f), 8.0f, 32.0f, Measure), 32.0f, 0.001f);

	// Fits at no size: overflowing at the floor beats vanishing.
	TestEqual(TEXT("a box too small for even the minimum stops at the minimum"),
		UDreamText::FindBestFitFontSize(FVector2f(1.0f, 1.0f), 8.0f, 32.0f, Measure), 8.0f, 0.001f);

	// A degenerate range must not bisect into nonsense.
	TestEqual(TEXT("a minimum above the maximum collapses to the minimum"),
		UDreamText::FindBestFitFontSize(FVector2f(9999.0f, 9999.0f), 40.0f, 32.0f, Measure), 40.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBestFitCheapWhenItFitsTest,
	"DreamGUI.Text.BestFit.CostsOneMeasurementWhenTheTextAlreadyFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextBestFitCheapWhenItFitsTest::RunTest(const FString& Parameters)
{
	// Every measurement is a full text layout, and the overwhelmingly common case is that nothing
	// needs shrinking. That case must not pay for a bisection.
	int32 Measurements = 0;
	auto Measure = [&Measurements](float InSize) { ++Measurements; return FVector2f(InSize, InSize); };
	UDreamText::FindBestFitFontSize(FVector2f(9999.0f, 9999.0f), 8.0f, 200.0f, Measure);
	TestEqual(TEXT("one layout, not a search"), Measurements, 1);
	return true;
}

#endif
