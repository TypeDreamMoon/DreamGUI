// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Colour conversions into encoded sRGB (0..1 per channel), for documents and descriptors that are not RGB. They are
 * the textbook formulas -- no colour profile is read -- so a CMYK or Lab document comes out close to, not exactly as,
 * the editor that wrote it shows it. The importer says so when it converts one.
 */
namespace DreamPsdColor
{
	/** Hue in degrees, saturation and brightness 0..1. */
	DREAMGUIPSD_API FLinearColor HsbToRgb(float InHue, float InSaturation, float InBrightness);

	/** Ink amounts 0..1 (0 is no ink). */
	DREAMGUIPSD_API FLinearColor CmykToRgb(float InC, float InM, float InY, float InK);

	/** L 0..100, a and b about -128..127, D50 white, to sRGB (D65, Bradford). */
	DREAMGUIPSD_API FLinearColor LabToRgb(float InL, float InA, float InB);

	/** The sRGB transfer function, both ways, per channel. */
	DREAMGUIPSD_API float EncodedToLinear(float InValue);
	DREAMGUIPSD_API float LinearToEncoded(float InValue);

	/** An encoded colour (0..1, alpha 0..1) to 8 bits, rounded. */
	DREAMGUIPSD_API FColor ToColor(const FLinearColor& InEncoded);
}
