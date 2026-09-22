// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPixelSort.h"

namespace DreamPixelSortTestLocal
{
	/** Threshold-mode rules, which is what every test below was written against. */
	FDreamPixelSortRunRules MakeRules(float InLow, float InHigh)
	{
		FDreamPixelSortRunRules Rules;
		Rules.Interval = EDreamPixelSortInterval::Threshold;
		Rules.Band = DreamPixelSort::ResolveBand(InLow, InHigh);
		return Rules;
	}
}
using DreamPixelSortTestLocal::MakeRules;

/*
 * The pixel sort's arithmetic.
 *
 * A render effect is mostly untestable here -- no shader harness, no golden images, and an automation
 * test cannot see a pixel. What CAN be pinned is the arithmetic, and that is where the bugs in a sort
 * actually live: a comparator that disagrees with itself, a parity that never alternates, a band that
 * silently selects nothing. Every one of those still LOOKS like a pixel sort.
 *
 * These pin the C++ half. The .usf mirrors it and nothing here can prove the two agree -- that is
 * stated in both files and is the known limitation of the approach.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortDestinationTest,
	"DreamGUI.PixelSort.Rank.EveryTexelGetsItsOwnDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortDestinationTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// The property the whole algorithm rests on: the destinations must be a PERMUTATION. Each texel
	// works out where it goes without seeing what anyone else decided, so if two of them land on the
	// same index, one colour is duplicated and another erased -- and the second pass, which searches
	// for whoever claimed its position, would find the wrong one.
	//
	// It holds because of the asymmetric tie-break, one character different on each side of the
	// scan. That makes ties order by position. The fixture is therefore mostly ties, since that is
	// the only case where it can fail, and flat UI colour is nothing but ties.
	TArray<float> Keys = { 0.4f, 0.4f, 0.4f, 0.9f, 0.4f, 0.1f, 0.1f, 0.7f, 0.4f, 0.55f, 0.55f, 0.2f };
	FDreamPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);

	for (int32 Descending = 0; Descending < 2; ++Descending)
	{
		TArray<int32> Claims;
		Claims.Init(0, Keys.Num());
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			const int32 Destination = ComputeDestination(Keys, Index, Rules, Descending != 0, Keys.Num());
			if (TestTrue(TEXT("A destination lands inside the line"), Destination >= 0 && Destination < Keys.Num()))
			{
				Claims[Destination]++;
			}
		}
		bool bPermutation = true;
		for (int32 Count : Claims)
		{
			bPermutation &= Count == 1;
		}
		TestTrue(*FString::Printf(TEXT("Every index is claimed exactly once, descending=%d"), Descending), bPermutation);
	}

	// And it really sorts: gathering by destination gives an ordered line.
	TArray<float> Sorted;
	Sorted.Init(0.0f, Keys.Num());
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		Sorted[ComputeDestination(Keys, Index, Rules, false, Keys.Num())] = Keys[Index];
	}
	bool bOrdered = true;
	for (int32 Index = 0; Index + 1 < Sorted.Num(); ++Index)
	{
		bOrdered &= Sorted[Index] <= Sorted[Index + 1];
	}
	TestTrue(TEXT("A full-radius scan sorts the run exactly"), bOrdered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortWallsTest,
	"DreamGUI.PixelSort.Rank.WallsStayPutAndBoundTheirRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortWallsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// Out-of-band texels are what makes a run a run. They must not move, and crucially the scan must
	// STOP at them -- a rank counted past a wall would place a texel outside its own run, dragging
	// colour across a boundary the author drew deliberately.
	TArray<float> Keys = { 0.9f, 0.55f, 0.30f, 0.45f, 0.05f, 0.70f, 0.60f, 0.95f };
	const FDreamPixelSortRunRules Rules = MakeRules(0.25f, 0.8f);

	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const int32 Destination = ComputeDestination(Keys, Index, Rules, false, Keys.Num());
		if (!IsSortable(Keys[Index], Index, Rules))
		{
			TestEqual(*FString::Printf(TEXT("Wall at %d stays put"), Index), Destination, Index);
		}
		else
		{
			// 0.9 and 0.05 and 0.95 are walls, so the runs are [1..3] and [5..6].
			const bool bFirstRun = Index >= 1 && Index <= 3;
			const bool bLanded = bFirstRun ? (Destination >= 1 && Destination <= 3) : (Destination >= 5 && Destination <= 6);
			TestTrue(*FString::Printf(TEXT("Texel %d stays inside its own run"), Index), bLanded);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortReachTest,
	"DreamGUI.PixelSort.Rank.APermutationNeedsTheRadiusToCoverTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortReachTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// THIS IS THE SETTING THAT DECIDES WHETHER THE EFFECT LOOKS LIKE STREAKS OR LIKE NOISE, and
	// until now it lived nowhere but in the argument for why the algorithm works.
	//
	// Each texel works out its own destination without seeing what anyone else decided. That only
	// adds up to a permutation when every texel in a run scans the WHOLE run: they must all agree
	// on where the run starts, and a scan cut short by the radius does not reach the start. Two
	// texels with different windows can then compute the same destination -- one colour is written
	// twice and another index is claimed by nobody. The gather leaves unclaimed positions alone, so
	// the result is a mix of sorted and untouched pixels, which on screen reads as speckle rather
	// than as a weaker sort. The knob does not degrade gracefully; it degrades into noise.
	//
	// Hence the property clamps: MaxSortPasses must be able to reach IntervalLength, or the details
	// panel can express a combination that cannot produce a correct image.
	// NOISY keys, not a clean ramp. On a monotonic run the interior survives truncation by
	// coincidence -- every texel sees the same number of smaller values on one side and larger on the
	// other, so it maps back to itself and only the two ends collide. Real image luminance is nothing
	// like that, and the speckle on screen comes from the MIDDLE. A ramp fixture would make this test
	// pass while demonstrating the wrong thing.
	//
	// The generator is deliberately self-contained rather than DreamPixelSort::Hash: a fixture drawn
	// from the code under test cannot witness against it.
	const int32 Length = 512;//long enough that the interior window below is not empty
	TArray<float> Keys;
	uint32 Seed = 0x9e3779b9u;
	for (int32 Index = 0; Index < Length; ++Index)
	{
		Seed = Seed * 1664525u + 1013904223u;
		Keys.Add(((Seed >> 8) & 0xffffu) / 65535.0f);
	}
	const FDreamPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);

	// Counts positions nobody lands on, optionally only within a window well away from both ends.
	auto CountUnclaimed = [&Keys, &Rules, Length](int32 InRadius, int32 InFrom, int32 InTo)
		{
			TArray<int32> Claims;
			Claims.Init(0, Length);
			for (int32 Index = 0; Index < Length; ++Index)
			{
				const int32 Destination = ComputeDestination(Keys, Index, Rules, false, InRadius);
				if (Destination >= 0 && Destination < Length)
				{
					Claims[Destination]++;
				}
			}
			int32 Unclaimed = 0;
			for (int32 Index = InFrom; Index < InTo; ++Index)
			{
				Unclaimed += Claims[Index] == 0 ? 1 : 0;
			}
			return Unclaimed;
		};

	// Radius at or beyond the run length: exact, every position claimed exactly once.
	for (const int32 Radius : { Length, Length * 2 })
	{
		TestEqual(*FString::Printf(TEXT("Radius %d covers the %d-long run, so nothing is left unclaimed"), Radius, Length),
			CountUnclaimed(Radius, 0, Length), 0);
	}
	// Radius short of the run: NOT a permutation, and not merely at the edges. The interior window
	// starts four radii in, so nothing counted here can be blamed on running off the end of the line
	// -- these are texels that disagreed with their neighbours about where their run began. That
	// disagreement is what puts speckle on screen, so it is asserted rather than merely admitted.
	const int32 ShortRadius = 32;
	TestTrue(TEXT("A radius shorter than the run leaves positions unclaimed in the interior, not just at the ends"),
		CountUnclaimed(ShortRadius, ShortRadius * 4, Length - ShortRadius * 4) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortRadiusTest,
	"DreamGUI.PixelSort.Rank.TravelIsBoundedByTheSearchRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortRadiusTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// The radius is both the cost bound and the reach control, and the second pass searches the same
	// distance to find whoever claimed a position -- so a texel travelling further than the radius
	// would simply be lost.
	TArray<float> Keys;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		Keys.Add(1.0f - Index / 64.0f);//exactly reversed: every texel wants to travel as far as it can
	}
	const FDreamPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);
	for (const int32 Radius : { 1, 4, 16 })
	{
		int32 Furthest = 0;
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			Furthest = FMath::Max(Furthest, FMath::Abs(ComputeDestination(Keys, Index, Rules, false, Radius) - Index));
		}
		TestTrue(*FString::Printf(TEXT("At radius %d nothing travels further than %d"), Radius, Radius),
			Furthest <= Radius);
		TestTrue(*FString::Printf(TEXT("At radius %d something actually moves"), Radius), Furthest > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortParamsTest,
	"DreamGUI.PixelSort.Params.BandAndStrengthSurviveHostileInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortParamsTest::RunTest(const FString& Parameters)
{
	// Typed backwards in the panel. Without the swap the band is empty, CanRender returns false, and
	// the widget silently disappears the moment a user drags Min past Max.
	const FVector2f Forward = DreamPixelSort::ResolveBand(0.25f, 0.8f);
	const FVector2f Backward = DreamPixelSort::ResolveBand(0.8f, 0.25f);
	TestEqual(TEXT("A band typed backwards resolves the same"), Backward, Forward);

	const FVector2f Clamped = DreamPixelSort::ResolveBand(-3.0f, 7.0f);
	TestTrue(TEXT("A band from out-of-range input stays inside 0..1"),
		Clamped.X >= 0.0f && Clamped.Y <= 1.0f && Clamped.X <= Clamped.Y);

	// The pass count is the only thing between a Blueprint setting strength to 20 and several hundred
	// render passes in one frame.
	TestEqual(TEXT("Zero strength is zero passes"), DreamPixelSort::ResolvePassCount(0.0f, 32), 0);
	TestEqual(TEXT("Full strength is the maximum"), DreamPixelSort::ResolvePassCount(1.0f, 32), 32);
	TestEqual(TEXT("Strength above one is clamped, not scaled"), DreamPixelSort::ResolvePassCount(20.0f, 32), 32);
	TestEqual(TEXT("Negative strength is zero"), DreamPixelSort::ResolvePassCount(-1.0f, 32), 0);
	int32 Previous = -1;
	bool bMonotone = true;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const int32 Passes = DreamPixelSort::ResolvePassCount(Step / 10.0f, 32);
		bMonotone &= Passes >= Previous;
		Previous = Passes;
	}
	TestTrue(TEXT("Strength maps onto passes monotonically"), bMonotone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortKeyTest,
	"DreamGUI.PixelSort.Key.LuminanceOrdersByBrightness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// A key that returns a constant makes every run a no-op and reads as "the strength does nothing".
	float Previous = -1.0f;
	bool bIncreasing = true;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const float Grey = Step / 10.0f;
		const float Key = ComputeKey(FLinearColor(Grey, Grey, Grey, 1.0f), EDreamPixelSortKey::Luminance);
		bIncreasing &= Key > Previous;
		Previous = Key;
	}
	TestTrue(TEXT("A grey ramp gives a strictly increasing luminance"), bIncreasing);

	// Rec.709 weights, which are what the shader mirrors. Swapping them is invisible on greys, so a
	// grey-ramp test alone would not catch it.
	const float Red = ComputeKey(FLinearColor(1, 0, 0, 1), EDreamPixelSortKey::Luminance);
	const float Green = ComputeKey(FLinearColor(0, 1, 0, 1), EDreamPixelSortKey::Luminance);
	const float Blue = ComputeKey(FLinearColor(0, 0, 1, 1), EDreamPixelSortKey::Luminance);
	TestTrue(TEXT("Green outweighs red, which outweighs blue"), Green > Red && Red > Blue);

	TestEqual(TEXT("Brightness takes the largest channel"),
		ComputeKey(FLinearColor(0.2f, 0.9f, 0.4f, 1), EDreamPixelSortKey::Brightness), 0.9f);
	TestEqual(TEXT("A grey has no saturation"),
		ComputeKey(FLinearColor(0.5f, 0.5f, 0.5f, 1), EDreamPixelSortKey::Saturation), 0.0f);
	TestEqual(TEXT("A pure hue is fully saturated"),
		ComputeKey(FLinearColor(0.8f, 0.0f, 0.0f, 1), EDreamPixelSortKey::Saturation), 1.0f);
	TestEqual(TEXT("Alpha is alpha"),
		ComputeKey(FLinearColor(1, 1, 1, 0.35f), EDreamPixelSortKey::Alpha), 0.35f);

	// The band test saturates so a float target's above-1 highlights are not frozen out of the sort,
	// but the ORDERING still uses the raw key.
	const FVector2f Band = ResolveBand(0.0f, 1.0f);
	TestTrue(TEXT("An over-bright key is still inside a full band"), IsInBand(4.0f, Band));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortIntervalTest,
	"DreamGUI.PixelSort.Interval.SpatialModesIgnoreTheImageAndThresholdDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortIntervalTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// The distinction the interval axis exists for, and the one that is easy to collapse by accident:
	// Threshold follows the picture, so the runs move when the picture changes. Random and Waves are
	// position-only, so the SAME runs are cut out of any image at all -- which is why they read as an
	// imposed pattern rather than as something draped over the content.
	auto CutsFor = [](const FDreamPixelSortRunRules& InRules, const TArray<float>& InKeys)
	{
		TArray<bool> Cuts;
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			Cuts.Add(IsSortable(InKeys[Index], Index, InRules));
		}
		return Cuts;
	};

	TArray<float> ImageA, ImageB;
	FRandomStream Stream(4242);
	for (int32 Index = 0; Index < 128; ++Index)
	{
		ImageA.Add(Stream.FRand());
		ImageB.Add(Stream.FRand());
	}

	for (EDreamPixelSortInterval Mode : { EDreamPixelSortInterval::Waves, EDreamPixelSortInterval::Random, EDreamPixelSortInterval::None })
	{
		FDreamPixelSortRunRules Rules;
		Rules.Interval = Mode;
		Rules.IntervalLength = 8;
		TestEqual(*FString::Printf(TEXT("Mode %d cuts the same runs from any image"), (int32)Mode),
			CutsFor(Rules, ImageA), CutsFor(Rules, ImageB));
	}

	// Threshold must NOT have that property, or the mode has silently collapsed into a spatial one.
	FDreamPixelSortRunRules ThresholdRules = MakeRules(0.25f, 0.8f);
	TestNotEqual(TEXT("Threshold follows the image rather than the position"),
		CutsFor(ThresholdRules, ImageA), CutsFor(ThresholdRules, ImageB));

	// None sorts the whole line end to end -- nothing is a wall.
	FDreamPixelSortRunRules NoneRules;
	NoneRules.Interval = EDreamPixelSortInterval::None;
	bool bAllSortable = true;
	for (int32 Index = 0; Index < ImageA.Num(); ++Index)
	{
		bAllSortable &= IsSortable(ImageA[Index], Index, NoneRules);
	}
	TestTrue(TEXT("None leaves no walls at all"), bAllSortable);

	// Waves puts its walls at a fixed spacing, so the count is predictable; Random averages the same
	// spacing without the regularity. Both must actually cut SOMETHING, or the mode does nothing.
	for (EDreamPixelSortInterval Mode : { EDreamPixelSortInterval::Waves, EDreamPixelSortInterval::Random })
	{
		FDreamPixelSortRunRules Rules;
		Rules.Interval = Mode;
		Rules.IntervalLength = 8;
		int32 Walls = 0;
		for (int32 Index = 0; Index < ImageA.Num(); ++Index)
		{
			Walls += IsSortable(ImageA[Index], Index, Rules) ? 0 : 1;
		}
		TestTrue(*FString::Printf(TEXT("Mode %d actually cuts the line"), (int32)Mode), Walls > 0 && Walls < ImageA.Num());
	}

	// Randomness drops whole runs, not scattered pixels. With a length of 8 and everything dropped,
	// nothing survives; with nothing dropped, the run structure is untouched.
	FDreamPixelSortRunRules AllDropped = MakeRules(0.0f, 1.0f);
	AllDropped.Randomness = 1.0f;
	bool bAnySurvives = false;
	for (int32 Index = 0; Index < ImageA.Num(); ++Index)
	{
		bAnySurvives |= IsSortable(ImageA[Index], Index, AllDropped);
	}
	TestFalse(TEXT("Full randomness leaves nothing sorted"), bAnySurvives);

	// Different lines must differ, or every row cuts identically and the result is a column grid.
	FDreamPixelSortRunRules LineZero;  LineZero.Interval = EDreamPixelSortInterval::Waves;  LineZero.IntervalLength = 8;  LineZero.LineIndex = 0;
	FDreamPixelSortRunRules LineOne = LineZero;  LineOne.LineIndex = 1;
	TestNotEqual(TEXT("Neighbouring lines cut differently"), CutsFor(LineZero, ImageA), CutsFor(LineOne, ImageA));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPixelSortRegionTest,
	"DreamGUI.PixelSort.Region.FullSizeMeansTheScreenAndNotAnAuthoredResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPixelSortRegionTest::RunTest(const FString& Parameters)
{
	using namespace DreamPixelSort;
	// The bug this exists to prevent, which shipped and was caught on screen rather than in review:
	// inferring "the whole screen is the subject" by testing whether the widget rect HAPPENS to equal
	// the screen size, instead of reading the flag that says so.
	//
	// With Use Full Size on, RectSize becomes the root canvas's AUTHORED resolution -- 1920x1080 --
	// which equals the actual screen only by coincidence. Take the widget-rect path in that state and
	// the sort runs in a buffer whose texels are not screen pixels and whose edges sample past what
	// is on screen, so the effect's apparent strength changes with the viewport and flat bands of
	// clamped edge colour get sorted into view.
	const FVector2f AuthoredRect(1920.0f, 1080.0f);
	const FIntPoint OddScreen(1274, 719);

	TestEqual(TEXT("Full size uses the screen even when the authored rect differs"),
		ResolveRegionSize(true, AuthoredRect, OddScreen), OddScreen);
	// The coincidence that made the old test appear to work.
	TestEqual(TEXT("Full size still uses the screen when the two happen to match"),
		ResolveRegionSize(true, AuthoredRect, FIntPoint(1920, 1080)), FIntPoint(1920, 1080));
	// And without the flag, the widget's own rect is the subject regardless of the screen.
	TestEqual(TEXT("Without the flag the widget rect is the subject"),
		ResolveRegionSize(false, FVector2f(400.0f, 260.0f), OddScreen), FIntPoint(400, 260));
	TestEqual(TEXT("A widget rect equal to the screen is still the widget rect"),
		ResolveRegionSize(false, FVector2f(1274.0f, 719.0f), OddScreen), OddScreen);
	// A collapsed widget must not produce a zero-sized buffer.
	TestEqual(TEXT("A degenerate rect still yields at least one texel"),
		ResolveRegionSize(false, FVector2f(0.0f, 0.0f), OddScreen), FIntPoint(1, 1));
	return true;
}

#endif
