// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPixelSort.h"

namespace LexPixelSortTestLocal
{
	/** Threshold-mode rules, which is what every test below was written against. */
	FLexPixelSortRunRules MakeRules(float InLow, float InHigh)
	{
		FLexPixelSortRunRules Rules;
		Rules.Interval = ELexPixelSortInterval::Threshold;
		Rules.Band = LexPixelSort::ResolveBand(InLow, InHigh);
		return Rules;
	}
}
using LexPixelSortTestLocal::MakeRules;

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
	FLexPixelSortConservationTest,
	"LGUI.PixelSort.Phase.NoPixelIsLostOrDuplicated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortConservationTest::RunTest(const FString& Parameters)
{
	// The property that separates a sort from a smear: a sort is a PERMUTATION, so every value that
	// went in comes out exactly once. The classic failure is a comparator that answers differently
	// for the two members of a pair, which duplicates one texel and erases its partner on every tie.
	// Flat UI backgrounds are nothing but ties, so it shows as a spreading stain of one colour.
	TArray<float> Keys;
	FRandomStream Stream(20260728);
	for (int32 Index = 0; Index < 200; ++Index)
	{
		Keys.Add(Stream.FRand());
	}
	TArray<float> Expected = Keys;
	Expected.Sort();

	const FLexPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);
	for (int32 Phase = 0; Phase < 64; ++Phase)
	{
		LexPixelSort::ApplyPhase(Keys, Phase, Rules, false);
	}

	TArray<float> Actual = Keys;
	Actual.Sort();
	if (TestEqual(TEXT("The line is still the same length"), Actual.Num(), Expected.Num()))
	{
		bool bSameMultiset = true;
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			bSameMultiset &= FMath::IsNearlyEqual(Actual[Index], Expected[Index]);
		}
		TestTrue(TEXT("Every value that went in comes out exactly once"), bSameMultiset);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortBandTest,
	"LGUI.PixelSort.Phase.OutOfBandPixelsNeverMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortBandTest::RunTest(const FString& Parameters)
{
	// Out-of-band pixels are the walls that make a run a run. If only the anchor's band is checked --
	// the natural half to write -- the effect still looks like a pixel sort, but runs bleed through
	// their own boundaries and the threshold stops meaning anything.
	TArray<float> Keys = { 0.9f, 0.55f, 0.30f, 0.45f, 0.05f, 0.70f, 0.60f, 0.95f };
	const TArray<float> Original = Keys;
	const FLexPixelSortRunRules Rules = MakeRules(0.25f, 0.8f);

	for (int32 Phase = 0; Phase < 16; ++Phase)
	{
		LexPixelSort::ApplyPhase(Keys, Phase, Rules, false);
	}

	for (int32 Index = 0; Index < Original.Num(); ++Index)
	{
		if (!LexPixelSort::IsSortable(Original[Index], Index, Rules))
		{
			TestEqual(*FString::Printf(TEXT("Out-of-band value at %d stayed put"), Index), Keys[Index], Original[Index]);
		}
	}
	// And the in-band run between the walls really did sort, or the assertions above are vacuous.
	TestTrue(TEXT("The run between the walls is ordered"), Keys[1] <= Keys[2] && Keys[2] <= Keys[3]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortOrderingTest,
	"LGUI.PixelSort.Phase.EnoughPhasesFullySortARun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortOrderingTest::RunTest(const FString& Parameters)
{
	const FLexPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);
	auto RunAndCheck = [this, &Rules](bool bDescending)
	{
		TArray<float> Keys = { 0.7f, 0.1f, 0.9f, 0.35f, 0.5f, 0.05f, 0.8f, 0.2f };
		// N phases is sufficient for an N-element odd-even transposition sort.
		for (int32 Phase = 0; Phase < Keys.Num(); ++Phase)
		{
			LexPixelSort::ApplyPhase(Keys, Phase, Rules, bDescending);
		}
		bool bOrdered = true;
		for (int32 Index = 0; Index + 1 < Keys.Num(); ++Index)
		{
			bOrdered &= bDescending ? (Keys[Index] >= Keys[Index + 1]) : (Keys[Index] <= Keys[Index + 1]);
		}
		TestTrue(*FString::Printf(TEXT("Fully sorted, descending=%d"), bDescending ? 1 : 0), bOrdered);
	};
	RunAndCheck(false);
	RunAndCheck(true);

	// The parity must alternate. A phase index that never changes parity stalls the line half-sorted,
	// which reads to an author as "the strength slider tops out early".
	TArray<float> Stalled = { 0.7f, 0.1f, 0.9f, 0.35f, 0.5f, 0.05f, 0.8f, 0.2f };
	for (int32 Repeat = 0; Repeat < 16; ++Repeat)
	{
		LexPixelSort::ApplyPhase(Stalled, 0, Rules, false);
	}
	bool bFullyOrdered = true;
	for (int32 Index = 0; Index + 1 < Stalled.Num(); ++Index)
	{
		bFullyOrdered &= Stalled[Index] <= Stalled[Index + 1];
	}
	TestFalse(TEXT("A never-alternating parity cannot finish the sort"), bFullyOrdered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortTravelTest,
	"LGUI.PixelSort.Phase.TravelIsBoundedByThePassCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortTravelTest::RunTest(const FString& Parameters)
{
	// This bound is what SortStrength MEANS -- how far a pixel may smear. Any clever optimisation
	// that moves a value more than one place per phase breaks the definition of the control, even if
	// the finished image still looks sorted.
	const int32 Count = 32;
	TArray<float> Keys;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Keys.Add(1.0f - Index / (float)Count);//worst case: exactly reversed
	}
	const FLexPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);
	const int32 Phases = 4;
	const float Lowest = Keys.Last();
	for (int32 Phase = 0; Phase < Phases; ++Phase)
	{
		LexPixelSort::ApplyPhase(Keys, Phase, Rules, false);
	}
	const int32 LandedAt = Keys.IndexOfByPredicate([Lowest](float V) { return FMath::IsNearlyEqual(V, Lowest); });
	TestTrue(TEXT("The value that must travel furthest moved at most one place per phase"),
		LandedAt != INDEX_NONE && (Count - 1 - LandedAt) <= Phases);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortParamsTest,
	"LGUI.PixelSort.Params.BandAndStrengthSurviveHostileInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortParamsTest::RunTest(const FString& Parameters)
{
	// Typed backwards in the panel. Without the swap the band is empty, CanRender returns false, and
	// the widget silently disappears the moment a user drags Min past Max.
	const FVector2f Forward = LexPixelSort::ResolveBand(0.25f, 0.8f);
	const FVector2f Backward = LexPixelSort::ResolveBand(0.8f, 0.25f);
	TestEqual(TEXT("A band typed backwards resolves the same"), Backward, Forward);

	const FVector2f Clamped = LexPixelSort::ResolveBand(-3.0f, 7.0f);
	TestTrue(TEXT("A band from out-of-range input stays inside 0..1"),
		Clamped.X >= 0.0f && Clamped.Y <= 1.0f && Clamped.X <= Clamped.Y);

	// The pass count is the only thing between a Blueprint setting strength to 20 and several hundred
	// render passes in one frame.
	TestEqual(TEXT("Zero strength is zero passes"), LexPixelSort::ResolvePassCount(0.0f, 32), 0);
	TestEqual(TEXT("Full strength is the maximum"), LexPixelSort::ResolvePassCount(1.0f, 32), 32);
	TestEqual(TEXT("Strength above one is clamped, not scaled"), LexPixelSort::ResolvePassCount(20.0f, 32), 32);
	TestEqual(TEXT("Negative strength is zero"), LexPixelSort::ResolvePassCount(-1.0f, 32), 0);
	int32 Previous = -1;
	bool bMonotone = true;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const int32 Passes = LexPixelSort::ResolvePassCount(Step / 10.0f, 32);
		bMonotone &= Passes >= Previous;
		Previous = Passes;
	}
	TestTrue(TEXT("Strength maps onto passes monotonically"), bMonotone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortKeyTest,
	"LGUI.PixelSort.Key.LuminanceOrdersByBrightness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortKeyTest::RunTest(const FString& Parameters)
{
	using namespace LexPixelSort;
	// A key that returns a constant makes every run a no-op and reads as "the strength does nothing".
	float Previous = -1.0f;
	bool bIncreasing = true;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const float Grey = Step / 10.0f;
		const float Key = ComputeKey(FLinearColor(Grey, Grey, Grey, 1.0f), ELexPixelSortKey::Luminance);
		bIncreasing &= Key > Previous;
		Previous = Key;
	}
	TestTrue(TEXT("A grey ramp gives a strictly increasing luminance"), bIncreasing);

	// Rec.709 weights, which are what the shader mirrors. Swapping them is invisible on greys, so a
	// grey-ramp test alone would not catch it.
	const float Red = ComputeKey(FLinearColor(1, 0, 0, 1), ELexPixelSortKey::Luminance);
	const float Green = ComputeKey(FLinearColor(0, 1, 0, 1), ELexPixelSortKey::Luminance);
	const float Blue = ComputeKey(FLinearColor(0, 0, 1, 1), ELexPixelSortKey::Luminance);
	TestTrue(TEXT("Green outweighs red, which outweighs blue"), Green > Red && Red > Blue);

	TestEqual(TEXT("Brightness takes the largest channel"),
		ComputeKey(FLinearColor(0.2f, 0.9f, 0.4f, 1), ELexPixelSortKey::Brightness), 0.9f);
	TestEqual(TEXT("A grey has no saturation"),
		ComputeKey(FLinearColor(0.5f, 0.5f, 0.5f, 1), ELexPixelSortKey::Saturation), 0.0f);
	TestEqual(TEXT("A pure hue is fully saturated"),
		ComputeKey(FLinearColor(0.8f, 0.0f, 0.0f, 1), ELexPixelSortKey::Saturation), 1.0f);
	TestEqual(TEXT("Alpha is alpha"),
		ComputeKey(FLinearColor(1, 1, 1, 0.35f), ELexPixelSortKey::Alpha), 0.35f);

	// The band test saturates so a float target's above-1 highlights are not frozen out of the sort,
	// but the ORDERING still uses the raw key.
	const FVector2f Band = ResolveBand(0.0f, 1.0f);
	TestTrue(TEXT("An over-bright key is still inside a full band"), IsInBand(4.0f, Band));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortGatherAgreementTest,
	"LGUI.PixelSort.Phase.TwoIndependentDecisionsAlwaysAgree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortGatherAgreementTest::RunTest(const FString& Parameters)
{
	// The closest this codebase can get to testing the shader.
	//
	// A pixel shader cannot swap two texels -- each invocation only chooses which source texel to
	// read. So every pair gets TWO independent decisions, and the sort is correct only if they agree.
	// Disagreement duplicates one texel and erases its partner, and on a flat background, where every
	// pair is a tie, that happens to every pair on every pass. Running the gather formulation over a
	// whole line and requiring it to reproduce ApplyPhase exactly is what pins that.
	//
	// Ties are the case that matters, so the fixture is built to be full of them.
	TArray<float> Base = { 0.4f, 0.4f, 0.9f, 0.4f, 0.1f, 0.1f, 0.7f, 0.4f, 0.55f, 0.55f, 0.2f, 0.95f };
	const TArray<FLexPixelSortRunRules> RuleSets = { MakeRules(0.0f, 1.0f), MakeRules(0.25f, 0.8f) };

	for (const FLexPixelSortRunRules& Rules : RuleSets)
	{
		for (int32 Descending = 0; Descending < 2; ++Descending)
		{
			TArray<float> ByApply = Base;
			for (int32 Phase = 0; Phase < 8; ++Phase)
			{
				// One phase, both ways.
				TArray<float> Before = ByApply;
				LexPixelSort::ApplyPhase(ByApply, Phase, Rules, Descending != 0);

				TArray<float> ByGather;
				ByGather.SetNum(Before.Num());
				for (int32 Index = 0; Index < Before.Num(); ++Index)
				{
					ByGather[Index] = Before[LexPixelSort::GatherIndex(Before, Index, Phase, Rules, Descending != 0)];
				}

				bool bAgree = ByGather.Num() == ByApply.Num();
				for (int32 Index = 0; bAgree && Index < ByApply.Num(); ++Index)
				{
					bAgree = FMath::IsNearlyEqual(ByGather[Index], ByApply[Index]);
				}
				TestTrue(*FString::Printf(TEXT("Gather matches swap at phase %d, band %.2f-%.2f, descending=%d"),
					Phase, Rules.Band.X, Rules.Band.Y, Descending), bAgree);
			}
		}
	}

	// And that the gather really is a permutation on its own terms -- a texel is never gathered by
	// two different outputs, which is the shape the duplication bug takes.
	const FLexPixelSortRunRules Rules = MakeRules(0.0f, 1.0f);
	for (int32 Phase = 0; Phase < 2; ++Phase)
	{
		TArray<int32> GatherCount;
		GatherCount.Init(0, Base.Num());
		for (int32 Index = 0; Index < Base.Num(); ++Index)
		{
			GatherCount[LexPixelSort::GatherIndex(Base, Index, Phase, Rules, false)]++;
		}
		bool bPermutation = true;
		for (int32 Count : GatherCount)
		{
			bPermutation &= Count == 1;
		}
		TestTrue(*FString::Printf(TEXT("Every source texel is read exactly once at phase %d"), Phase), bPermutation);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPixelSortIntervalTest,
	"LGUI.PixelSort.Interval.SpatialModesIgnoreTheImageAndThresholdDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPixelSortIntervalTest::RunTest(const FString& Parameters)
{
	using namespace LexPixelSort;
	// The distinction the interval axis exists for, and the one that is easy to collapse by accident:
	// Threshold follows the picture, so the runs move when the picture changes. Random and Waves are
	// position-only, so the SAME runs are cut out of any image at all -- which is why they read as an
	// imposed pattern rather than as something draped over the content.
	auto CutsFor = [](const FLexPixelSortRunRules& InRules, const TArray<float>& InKeys)
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

	for (ELexPixelSortInterval Mode : { ELexPixelSortInterval::Waves, ELexPixelSortInterval::Random, ELexPixelSortInterval::None })
	{
		FLexPixelSortRunRules Rules;
		Rules.Interval = Mode;
		Rules.IntervalLength = 8;
		TestEqual(*FString::Printf(TEXT("Mode %d cuts the same runs from any image"), (int32)Mode),
			CutsFor(Rules, ImageA), CutsFor(Rules, ImageB));
	}

	// Threshold must NOT have that property, or the mode has silently collapsed into a spatial one.
	FLexPixelSortRunRules ThresholdRules = MakeRules(0.25f, 0.8f);
	TestNotEqual(TEXT("Threshold follows the image rather than the position"),
		CutsFor(ThresholdRules, ImageA), CutsFor(ThresholdRules, ImageB));

	// None sorts the whole line end to end -- nothing is a wall.
	FLexPixelSortRunRules NoneRules;
	NoneRules.Interval = ELexPixelSortInterval::None;
	bool bAllSortable = true;
	for (int32 Index = 0; Index < ImageA.Num(); ++Index)
	{
		bAllSortable &= IsSortable(ImageA[Index], Index, NoneRules);
	}
	TestTrue(TEXT("None leaves no walls at all"), bAllSortable);

	// Waves puts its walls at a fixed spacing, so the count is predictable; Random averages the same
	// spacing without the regularity. Both must actually cut SOMETHING, or the mode does nothing.
	for (ELexPixelSortInterval Mode : { ELexPixelSortInterval::Waves, ELexPixelSortInterval::Random })
	{
		FLexPixelSortRunRules Rules;
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
	FLexPixelSortRunRules AllDropped = MakeRules(0.0f, 1.0f);
	AllDropped.Randomness = 1.0f;
	bool bAnySurvives = false;
	for (int32 Index = 0; Index < ImageA.Num(); ++Index)
	{
		bAnySurvives |= IsSortable(ImageA[Index], Index, AllDropped);
	}
	TestFalse(TEXT("Full randomness leaves nothing sorted"), bAnySurvives);

	// Different lines must differ, or every row cuts identically and the result is a column grid.
	FLexPixelSortRunRules LineZero;  LineZero.Interval = ELexPixelSortInterval::Waves;  LineZero.IntervalLength = 8;  LineZero.LineIndex = 0;
	FLexPixelSortRunRules LineOne = LineZero;  LineOne.LineIndex = 1;
	TestNotEqual(TEXT("Neighbouring lines cut differently"), CutsFor(LineZero, ImageA), CutsFor(LineOne, ImageA));
	return true;
}

#endif
