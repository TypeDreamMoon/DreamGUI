// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIBlendMode.generated.h"

/**
 * How an element's colour is combined with what is already in the render target.
 *
 * Three ways of doing it, picked by the mode and by what draws the element, never by the user:
 *
 * - The blend hardware alone, for Alpha, Additive, Multiply, Screen, Exclusion and Dissolve: these batch like any element
 *   and are exact for a partly transparent source. (Subtract and linear burn are not: the hardware clamps a partly
 *   transparent element's result where the formula does not, so they read what is behind like the rest.)
 * - Reading what is behind first, for every other mode: the region a draw covers is copied, and the shader blends the
 *   element over the copy by the W3C compositing formula, Cs' = (1 - Da) * Cs + Da * B(Cb, Cs). Elements of one such mode
 *   that do not overlap still share a draw and a copy; elements that overlap are drawn apart.
 * - A group of its own, for an element drawn through a material with any of the modes added after Multiply: the element
 *   is drawn into a texture and the texture blended over what is behind (UDreamLayerGroup does the same for a subtree).
 *   An element with a material and Alpha, Additive or Multiply composites the way its material says, as it always did.
 *
 * The modes blend encoded (sRGB) values, the way image editors do: a viewport target holds them already, and on a linear
 * render target both colours are encoded for the blend and the result decoded again. The hardware modes blend whatever
 * the target holds, so on a linear target they differ a little from an image editor's.
 *
 * Only appended to: a value once saved keeps its meaning.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIBlendMode : uint8
{
	/** Premultiplied alpha composite. The default, and what every element did before this existed. */
	Alpha		UMETA(DisplayName = "Alpha"),
	/** Added to the target, so the element can only brighten it: glows, sparks, highlights. Also called linear dodge. */
	Additive	UMETA(DisplayName = "Additive"),
	/** Multiplies the target, so the element can only darken it: shadows, tints, grading overlays. */
	Multiply	UMETA(DisplayName = "Multiply"),
	/** Each pixel either fully drawn or not drawn, at random, as often as its alpha says. */
	Dissolve	UMETA(DisplayName = "Dissolve"),
	/** The darker of the two, channel by channel. */
	Darken		UMETA(DisplayName = "Darken"),
	/** Darkens what is behind to reflect the element, raising its contrast. */
	ColorBurn	UMETA(DisplayName = "Color Burn"),
	/** Darkens what is behind by the element's darkness: the sum of the two, less one. */
	LinearBurn	UMETA(DisplayName = "Linear Burn"),
	/** Whichever of the two colours is darker, as a whole colour. */
	DarkerColor	UMETA(DisplayName = "Darker Color"),
	/** The lighter of the two, channel by channel. */
	Lighten		UMETA(DisplayName = "Lighten"),
	/** The inverse of multiplying the inverses: always lighter, never past white. */
	Screen		UMETA(DisplayName = "Screen"),
	/** Brightens what is behind to reflect the element, lowering its contrast. */
	ColorDodge	UMETA(DisplayName = "Color Dodge"),
	/** Whichever of the two colours is lighter, as a whole colour. */
	LighterColor	UMETA(DisplayName = "Lighter Color"),
	/** Multiply or screen, by what is behind: its highlights and shadows kept. */
	Overlay		UMETA(DisplayName = "Overlay"),
	/** A gentle darken or lighten by the element, like a diffuse light on what is behind. */
	SoftLight	UMETA(DisplayName = "Soft Light"),
	/** Multiply or screen, by the element: like a harsh light on what is behind. */
	HardLight	UMETA(DisplayName = "Hard Light"),
	/** Color burn or color dodge, by the element. */
	VividLight	UMETA(DisplayName = "Vivid Light"),
	/** Linear burn or linear dodge, by the element. */
	LinearLight	UMETA(DisplayName = "Linear Light"),
	/** Darken or lighten, by the element. */
	PinLight	UMETA(DisplayName = "Pin Light"),
	/** Every channel pushed to black or white: white where the two add up to one or more. */
	HardMix		UMETA(DisplayName = "Hard Mix"),
	/** The difference of the two, channel by channel. */
	Difference	UMETA(DisplayName = "Difference"),
	/** Difference with less contrast: white over a colour inverts it, black leaves it. */
	Exclusion	UMETA(DisplayName = "Exclusion"),
	/** The element taken away from what is behind. */
	Subtract	UMETA(DisplayName = "Subtract"),
	/** What is behind divided by the element. */
	Divide		UMETA(DisplayName = "Divide"),
	/** The element's hue, with the saturation and luminosity of what is behind. */
	Hue			UMETA(DisplayName = "Hue"),
	/** The element's saturation, with the hue and luminosity of what is behind. */
	Saturation	UMETA(DisplayName = "Saturation"),
	/** The element's hue and saturation, with the luminosity of what is behind. */
	Color		UMETA(DisplayName = "Color"),
	/** The element's luminosity, with the hue and saturation of what is behind. */
	Luminosity	UMETA(DisplayName = "Luminosity"),
};

/** What a blend mode needs to be drawn: see EDreamUIBlendMode. */
namespace DreamUIBlend
{
	/** How many modes there are. */
	inline constexpr int32 NumModes = static_cast<int32>(EDreamUIBlendMode::Luminosity) + 1;

	/** Whether the mode reads what is behind the element, rather than leaving it to the blend hardware. */
	inline bool ReadsBackdrop(EDreamUIBlendMode InMode)
	{
		switch (InMode)
		{
		case EDreamUIBlendMode::Alpha:
		case EDreamUIBlendMode::Additive:
		case EDreamUIBlendMode::Multiply:
		case EDreamUIBlendMode::Dissolve:
		case EDreamUIBlendMode::Screen:
		case EDreamUIBlendMode::Exclusion:
			return false;
		default:
			return true;
		}
	}

	/** Alpha, Additive and Multiply: the modes there were before the rest, which a material decides for itself. */
	inline bool IsMaterialDecided(EDreamUIBlendMode InMode)
	{
		return InMode == EDreamUIBlendMode::Alpha || InMode == EDreamUIBlendMode::Additive || InMode == EDreamUIBlendMode::Multiply;
	}
}
