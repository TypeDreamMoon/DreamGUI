// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * How much of the viewport's edges the rulers take.
 *
 * Shared because everything else drawn at those edges has to keep out of them -- the cursor readout
 * in the bottom-left corner and the animation-mode chip along the top both sat exactly where the
 * rulers now are. Nudging each by eye is how they drift apart.
 */
constexpr float DreamUIRulerHorizontalThickness = 16.0f;
/** Wider than the horizontal one is tall, because the numbers on it are drawn upright. */
constexpr float DreamUIRulerVerticalThickness = 34.0f;

/**
 * Where a designer ruler's ticks go — the arithmetic on its own, so it can be tested without a
 * viewport, a camera or a canvas.
 *
 * UMG's SRuler is UMGEditor-private and draws into a Slate widget beside the design surface; this
 * designer's surface is a 3D viewport, so the ruler is drawn into the same FCanvas overlay as the
 * rest of the chrome. What is worth sharing between the two is not the drawing but this: choosing a
 * step the labels fit at, which is the only part that can be wrong in a way nobody notices.
 */
struct FDreamUIRulerTick
{
	/** Where it sits along the ruler, in the same pixels the caller measured the scale in. */
	float Pixel = 0.0f;
	/** What it names, in design units. */
	float Unit = 0.0f;
	/** Labelled and full height, as opposed to a subdivision. */
	bool bMajor = false;
};

/**
 * The 1 / 2 / 5 x 10^n step whose on-screen spacing is at least InMinPixelsPerStep.
 *
 * Rounding UP is the whole point: a step chosen by rounding to nearest can land BELOW the minimum
 * and put the labels on top of each other, which is exactly the case that looks fine at the zoom it
 * was written at. Returns 0 when there is no sensible answer, which the caller draws as no ruler
 * rather than as a division by zero.
 */
inline float ChooseDreamUIRulerStep(float InPixelsPerUnit, float InMinPixelsPerStep)
{
	if (!(InPixelsPerUnit > UE_SMALL_NUMBER) || !(InMinPixelsPerStep > UE_SMALL_NUMBER))
	{
		return 0.0f;
	}
	const float MinStepInUnits = InMinPixelsPerStep / InPixelsPerUnit;
	const float Decade = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, MinStepInUnits)));
	// Guard the mantissa against the floor above landing a hair low: 1.0000001 must stay 1, not
	// become 2, or every ruler at a power-of-ten zoom doubles its step for no reason.
	const float Mantissa = MinStepInUnits / Decade;
	const float Rounded = Mantissa <= 1.0f + UE_KINDA_SMALL_NUMBER ? 1.0f
		: (Mantissa <= 2.0f + UE_KINDA_SMALL_NUMBER ? 2.0f
		: (Mantissa <= 5.0f + UE_KINDA_SMALL_NUMBER ? 5.0f : 10.0f));
	return Rounded * Decade;
}

/**
 * Ticks covering [InMinUnit, InMaxUnit], majors every InStep and minors every InStep/InMinorPerMajor.
 *
 * InUnitToPixel maps a unit value to the ruler's pixel axis; it is signed, because the vertical
 * ruler's units go up while its pixels go down.
 *
 * The count is capped. A degenerate camera can make the visible range enormous relative to the step,
 * and a ruler is not worth an allocation storm — better a ruler that stops than an editor that does.
 */
inline void BuildDreamUIRulerTicks(float InMinUnit, float InMaxUnit, float InStep, int32 InMinorPerMajor,
	TFunctionRef<float(float)> InUnitToPixel, TArray<FDreamUIRulerTick>& OutTicks, int32 InMaxTicks = 512)
{
	OutTicks.Reset();
	if (!(InStep > UE_SMALL_NUMBER) || InMaxUnit <= InMinUnit || InMinorPerMajor < 1)
	{
		return;
	}
	const float MinorStep = InStep / (float)InMinorPerMajor;
	const double FirstIndex = FMath::CeilToDouble((double)InMinUnit / (double)MinorStep);
	const double LastIndex = FMath::FloorToDouble((double)InMaxUnit / (double)MinorStep);
	if (LastIndex < FirstIndex || (LastIndex - FirstIndex) + 1.0 > (double)InMaxTicks)
	{
		return;
	}
	for (double Index = FirstIndex; Index <= LastIndex; Index += 1.0)
	{
		FDreamUIRulerTick& Tick = OutTicks.AddDefaulted_GetRef();
		Tick.Unit = (float)(Index * (double)MinorStep);
		Tick.Pixel = InUnitToPixel(Tick.Unit);
		// Rounded because Index is counted in minors: at MinorPerMajor 5, every fifth one is a major,
		// and asking "is Unit a multiple of Step" in floats instead answers no on most of them.
		Tick.bMajor = FMath::Abs(FMath::Fmod(Index, (double)InMinorPerMajor)) < 0.5;
	}
}
