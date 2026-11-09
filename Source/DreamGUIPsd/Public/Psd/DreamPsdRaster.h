// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"

/** The pixel work the importer does on the CPU: trimming, filling a path, drawing a gradient, blurring and baking masks. */
namespace DreamPsdRaster
{
	/** The smallest rectangle of InImage (its own pixels) holding every pixel whose alpha is above zero; empty when none is. */
	DREAMGUIPSD_API FIntRect FindOpaqueBounds(const FDreamPsdImage& InImage);

	/** InRect of InImage (its own pixels). */
	DREAMGUIPSD_API FDreamPsdImage Crop(const FDreamPsdImage& InImage, const FIntRect& InRect);

	/**
	 * Fill InPath over InRegion (document pixels): the coverage of every pixel of the region, 0..255, anti-aliased by
	 * exact horizontal spans over InSubRows rows per pixel. Each subpath is filled non-zero and combined with the ones
	 * before it by its operation; a path from before the operations existed is filled even-odd.
	 */
	DREAMGUIPSD_API FDreamPsdGray FillPath(const FDreamPsdPath& InPath, const FIntRect& InRegion, int32 InSubRows = 5);

	/** A separable Gaussian blur, in place. InSigma in pixels; nothing below a quarter of a pixel. */
	DREAMGUIPSD_API void Blur(FDreamPsdGray& InOut, float InSigma);

	/**
	 * The pixel mask's value over InRegion (document pixels): its pixels inside its rectangle, its default colour outside,
	 * feathered (InFeatherScale times its feather, as the sigma of a blur) and with its density applied.
	 */
	DREAMGUIPSD_API FDreamPsdGray BakeLayerMask(const FDreamPsdLayerMask& InMask, const FIntRect& InRegion, float InFeatherScale = 1.0f);

	/** A vector mask over InRegion: the path filled, inverted when it says so, feathered, density applied. */
	DREAMGUIPSD_API FDreamPsdGray BakeVectorMask(const FDreamPsdVectorMask& InMask, const FIntRect& InRegion, float InFeatherScale = 1.0f);

	/**
	 * A gradient fill drawn into an image of InSize pixels standing for InBox (document pixels): linear, radial, angle,
	 * reflected and diamond, at the fill's angle, scale and offset, reversed when it says so.
	 */
	DREAMGUIPSD_API FDreamPsdImage DrawGradient(const FDreamPsdGradientFill& InFill, const FBox2D& InBox, FIntPoint InSize);

	/** Multiply InMask (0..255) into InImage's alpha, the two the same size. */
	DREAMGUIPSD_API void MultiplyAlpha(FDreamPsdImage& InOutImage, const FDreamPsdGray& InMask);

	/** Multiply two masks of the same size. */
	DREAMGUIPSD_API void MultiplyMask(FDreamPsdGray& InOutMask, const FDreamPsdGray& InOther);

	/** Whether every pixel of a mask is 255. */
	DREAMGUIPSD_API bool IsAllOpaque(const FDreamPsdGray& InMask);
}
