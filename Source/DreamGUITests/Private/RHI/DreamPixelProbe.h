// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"

class FAutomationTestBase;
class UTextureRenderTarget2D;

/**
 * The CPU end of a render-target test: fetch the pixels a real RHI produced, and say something
 * about them that a human can act on when it turns out to be false.
 *
 * Why pixels at all. Everything else this module asserts stops one step short of the picture: a
 * draw call is built, a vertex lands at a coordinate, a material is chosen -- and all of it can be
 * right while nothing appears, because the last step is the GPU's and nobody was watching it. A
 * render target is the one surface a headless process can hold up and read back, so it is the only
 * place where "it renders" can be a test rather than a hope.
 *
 * The golden sample is deliberately a handful of pixel values and not an image on disk: an image
 * has to be tolerant of the machine that drew it -- driver, DPI, window size, filtering -- and the
 * tolerance is what eventually makes it pass whatever happens. A named pixel with a named colour
 * fails for exactly one reason and says which pixel it was.
 *
 * Nothing here draws or waits. A caller is expected to have already made the frames happen; these
 * functions only read what is there, so a wrong answer is the renderer's and not the probe's.
 */
class FDreamPixelProbe
{
public:
	/**
	 * The target's pixels, top row first, and the size they came back at.
	 *
	 * The flush is not defensive. The draw that filled this target was RECORDED on the render thread
	 * -- a scene view extension's pass inside somebody else's graph -- and the game thread runs ahead
	 * of that by as much as a frame, so reading without waiting reads whatever the previous frame
	 * left. Flushing is what makes "the frames have happened" true of the GPU as well as of the game
	 * thread, and it is why no test here needs to wait an extra frame for the read itself.
	 *
	 * OutSize comes from the RESOURCE rather than from the asset's SizeX/SizeY, because the resource
	 * is what the pixels are of; the two differ for exactly as long as it takes a resize to reach the
	 * render thread, and indexing one by the other silently shears the image.
	 */
	static bool ReadBack(UTextureRenderTarget2D* InTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize);

	/**
	 * Assert one pixel, within a per-channel tolerance, and report the actual colour when it is not.
	 *
	 * The tolerance is per channel rather than a distance, because the failures worth catching here
	 * are "this pixel is a different colour", not "this pixel is slightly off": a channel that drifts
	 * by more than a few units has been through a conversion nobody asked for.
	 *
	 * InWhat is what the pixel MEANS -- "the centre of the red block" -- not where it is; the
	 * coordinates are added from InPixel, so a failure reads as a sentence about the picture.
	 */
	static bool ExpectColorAt(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize,
		FIntPoint InPixel, FColor InExpected, uint8 InTolerance, const TCHAR* InWhat);

	/**
	 * How many pixels of InRegion are within tolerance of InExpected.
	 *
	 * An area is the assertion to make about a block whose edges are antialiased or half covered:
	 * the exact boundary pixels are the renderer's business, the count is the author's. The region
	 * is clipped to the image, so a caller may hand in the whole target without checking its size.
	 *
	 * Returns a count rather than a pass/fail on purpose -- the caller decides what an acceptable
	 * area is, and a count that is merely close is worth printing.
	 */
	static int32 CountColor(const TArray<FColor>& InPixels, FIntPoint InSize, const FIntRect& InRegion,
		FColor InExpected, uint8 InTolerance);

	/** Every channel, including alpha, within InTolerance. */
	static bool IsNear(const FColor& InLeft, const FColor& InRight, uint8 InTolerance);

	/** "(R=255,G=0,B=0,A=255)", for a failure message that has to name a colour. */
	static FString Describe(const FColor& InColor);

	/*
	 * Whole pictures. A named pixel says whether one thing is where it should be; a picture says whether everything
	 * else still is -- the rounding of a corner, the weight of a glyph, the falloff of a shadow -- which is what a
	 * change to the renderer can break without moving any block. Each picture a test takes is written out, so a person
	 * can look at what the renderer drew; the golden image beside the tests is that picture, looked at and kept.
	 */

	/** <Project>/Saved/DreamGUITests/Captures: every picture a test held up, as it saw it. */
	static FString GetCaptureDirectory();
	/** Source/DreamGUITests/Resources/Golden in the plugin: the pictures the tests are held to, kept with the code. */
	static FString GetGoldenDirectory();
	/**
	 * Why the golden image InName is pending, from Golden/pending.json ("pending": { "<name>": "<why>" }), or empty when
	 * it is not: a golden due to be written for the first time or rewritten -- a new scene, a change to the renderer made
	 * on purpose -- and not looked at yet. Read from the file each time, so taking an entry out needs no rebuild.
	 */
	static FString GetPendingGoldenReason(const FString& InName);

	/** InPixels as <capture directory>/<InName>.png. The file written, or empty when it could not be. */
	static FString SaveCapture(const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InName);

	/** A PNG file's pixels, top row first. */
	static bool LoadPng(const FString& InFilePath, TArray<FColor>& OutPixels, FIntPoint& OutSize);

	/** How two pictures differ. */
	struct FDifference
	{
		bool bSizesDiffer = false;
		/** Pixels further apart than the tolerance on at least one channel. */
		int32 DifferingPixels = 0;
		/** The largest distance on any channel of any pixel. */
		int32 LargestChannelDelta = 0;
		/** The first pixel past the tolerance, reading rows from the top, or (-1,-1). */
		FIntPoint FirstDifferingPixel = FIntPoint(-1, -1);
	};
	static FDifference Compare(const TArray<FColor>& InActual, FIntPoint InActualSize,
		const TArray<FColor>& InExpected, FIntPoint InExpectedSize, uint8 InTolerance);

	/**
	 * Hold a picture to its golden image, Golden/<InName>.png: no more than InAllowedFraction of its pixels may be
	 * further than InTolerance from the golden's on any channel. The picture is written to the capture directory
	 * whatever happens, and where the two differ, <InName>.diff.png beside it marks the pixels that did.
	 *
	 * A picture with no golden yet passes with a warning naming the file to look at: a golden is a picture someone
	 * has judged right, and only a person can do that. -DreamGUIWriteGoldens on the command line writes every
	 * picture over its golden instead, for when the renderer changed on purpose. A pending golden
	 * (GetPendingGoldenReason) is compared all the same, but a difference is a warning that says why it is pending.
	 */
	static bool ExpectMatchesGolden(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize,
		const FString& InName, uint8 InTolerance = 8, double InAllowedFraction = 0.002);

	/** Two pictures of the same thing, drawn two ways, held to each other the way ExpectMatchesGolden holds one to its golden. */
	static bool ExpectPicturesMatch(FAutomationTestBase& InTest, const TArray<FColor>& InLeft, FIntPoint InLeftSize,
		const TArray<FColor>& InRight, FIntPoint InRightSize, const FString& InName, uint8 InTolerance, double InAllowedFraction);

private:
	/** InActual with every pixel past the tolerance in magenta over a dimmed copy of the rest, as <InName>.diff.png. */
	static FString SaveDifferenceImage(const TArray<FColor>& InActual, FIntPoint InSize, const TArray<FColor>& InExpected, uint8 InTolerance, const FString& InName);
	/** The difference in a sentence: how many pixels, the worst channel, and the first pixel as each picture has it. */
	static FString DescribeDifference(const FDifference& InDifference, const TArray<FColor>& InActual, FIntPoint InActualSize,
		const TArray<FColor>& InExpected, FIntPoint InExpectedSize, uint8 InTolerance, int32 InAllowed);
};
