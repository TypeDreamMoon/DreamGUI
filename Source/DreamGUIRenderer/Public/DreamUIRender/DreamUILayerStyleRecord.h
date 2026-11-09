// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIBlendMode.h"

class UTexture;

/**
 * A layer style as the shaders read it (DreamUILayerStyle.ush): a run of float4 texels, written by FDreamUILayerStyleMath::Pack
 * from an FDreamLayerEffects, read through a Texture2D's Load -- a rect block's row of rect block data past its own texels, or a
 * texture of its own for a group's.
 *
 *   texel 0         x the number of effects, y the record's flags, z the fill opacity, w how far its effects reach out (units)
 *   texel 1         x where the contours' table starts, y where the gradients' table starts (texels from the record's first),
 *                   z the number of slots its outer effects are drawn into apart (a group's), w unused
 *   texels 2..      the effects, EntryTexels each, from the bottom up as they are drawn
 *   then            the contours, ContourTexels each: ContourSamples values at 0, 1/63, ..., 1
 *   then            the gradients, GradientTexels each: GradientSamples colours at 0, 1/31, ..., 1, each a float's bits holding
 *                   ARGB bytes, alpha on top (UDreamRectBlock::PackColorForDataTexture's order)
 *
 * An effect's texels, by kind (all sizes and offsets in units of the canvas, +Y down, scale applied; colours sRGB-encoded):
 *   E0  x kind, y blend mode (EDreamUIBlendMode), z opacity, w flags (EntryFlag*)
 *   E1  colour, rgb; a: an outer effect's slot (a group's)
 *   E2  drop/inner shadow: size, spread or choke, offset x, offset y
 *       glows: size, spread or choke, 0, 0
 *       satin: size, 0, offset x, offset y (one of the two shifts; the other is minus it)
 *       stroke: size, position (EDreamStyleStrokePosition), 0, 0
 *       pattern overlay, a stroke's pattern: units per texel x, y, offset x, offset y
 *       bevel: size, soften, light x, light y (towards the light, scaled by the cosine of its altitude)
 *   E3  noise, range, jitter, contour (-1 linear); bevel: depth, contour range, light z, contour
 *   E4  gradient (-1 none), type (EDreamStyleGradientType), angle (radians), scale;
 *       bevel: shadow colour rgb, shadow opacity
 *   E5  gradient offset x, y (fractions of the layer's size), pattern slot (-1 none), extra;
 *       bevel: shadow mode, gloss contour, style (EDreamStyleBevelStyle), technique (EDreamStyleBevelTechnique)
 *   E6  bevel lit by a point light (EntryFlagPointLight): the light's position x, y, its height, its reach; else unused
 *
 * Every constant here has its #define in DreamUILayerStyle.ush, and a test holds the two to each other.
 */
namespace DreamUILayerStyleLayout
{
	inline constexpr int32 HeaderTexels = 2;
	inline constexpr int32 EntryTexels = 7;
	inline constexpr int32 MaxEffects = 10;
	inline constexpr int32 ContourSamples = 64;
	inline constexpr int32 ContourTexels = ContourSamples / 4;
	inline constexpr int32 MaxContours = 4;
	inline constexpr int32 GradientSamples = 32;
	inline constexpr int32 GradientTexels = GradientSamples / 4;
	inline constexpr int32 MaxGradients = 4;
	/** The most texels a record takes. */
	inline constexpr int32 MaxTexels = HeaderTexels + MaxEffects * EntryTexels + MaxContours * ContourTexels + MaxGradients * GradientTexels;
	/** The slots a group's outer effects are drawn into apart, each put down by its own mode (FDreamUILayerStyleComposite). */
	inline constexpr int32 MaxOuterSlots = 3;
	/** The pattern textures a group's style reads. */
	inline constexpr int32 MaxPatterns = 2;

	/** The kinds of effect, by the order they are drawn in (E0.x). */
	enum EKind : int32
	{
		KindDropShadow = 1,
		KindOuterGlow = 2,
		KindPatternOverlay = 3,
		KindGradientOverlay = 4,
		KindColorOverlay = 5,
		KindSatin = 6,
		KindInnerGlow = 7,
		KindInnerShadow = 8,
		KindStroke = 9,
		KindBevel = 10,
	};

	/** E0.w. */
	enum EEntryFlag : int32
	{
		/** The contour's table read between its samples. */
		EntryFlagAntiAliased = 1 << 0,
		/** A drop shadow the layer knocks out. */
		EntryFlagKnockOut = 1 << 1,
		/** A satin inverted. */
		EntryFlagInvert = 1 << 2,
		/** A glow's Precise technique. */
		EntryFlagPrecise = 1 << 3,
		/** An inner glow from the centre. */
		EntryFlagFromCenter = 1 << 4,
		/** The gradient read backwards. */
		EntryFlagReverse = 1 << 5,
		/** The gradient dithered. */
		EntryFlagDither = 1 << 6,
		/** A glow, or a stroke, filled with its gradient. */
		EntryFlagGradientFill = 1 << 7,
		/** A stroke filled with its pattern. */
		EntryFlagPatternFill = 1 << 8,
		/** A bevel pressed down. */
		EntryFlagDown = 1 << 9,
		/** A bevel's gloss contour read between its samples. */
		EntryFlagGlossAntiAliased = 1 << 10,
		/** A bevel lit by a point light at E6, not by its angle and altitude. */
		EntryFlagPointLight = 1 << 11,
	};

	/** Texel 0's y. */
	enum ERecordFlag : int32
	{
		/** An effect reads a pattern: a rect block's style is drawn as a group then, where the pattern can be bound. */
		RecordFlagPatterns = 1 << 0,
	};
}

/**
 * A layer style packed for the shaders: its texels, and what the one that draws it needs beside them. Plain data, made on the
 * game thread and copied to the render thread.
 */
struct FDreamUILayerStyleRecord
{
	TArray<FVector4f> Texels;
	/** How far past the layer's edge its effects reach, in units. */
	float OuterReach = 0.0f;
	/** The modes the outer slots are put down by, from the bottom up: a group's (texel 1's z says how many). */
	EDreamUIBlendMode OuterSlotModes[DreamUILayerStyleLayout::MaxOuterSlots] = { EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Alpha };
	int32 NumOuterSlots = 0;
	/** The pattern textures its effects read, by slot (E5.z). Read on the render thread only, through their resources. */
	const UTexture* Patterns[DreamUILayerStyleLayout::MaxPatterns] = { nullptr, nullptr };

	bool IsEmpty() const { return Texels.Num() == 0; }
	bool UsesPatterns() const { return Patterns[0] != nullptr || Patterns[1] != nullptr; }
	bool operator==(const FDreamUILayerStyleRecord& Other) const
	{
		if (Texels.Num() != Other.Texels.Num() || OuterReach != Other.OuterReach || NumOuterSlots != Other.NumOuterSlots)
		{
			return false;
		}
		for (int32 Index = 0; Index < DreamUILayerStyleLayout::MaxOuterSlots; ++Index)
		{
			if (OuterSlotModes[Index] != Other.OuterSlotModes[Index])
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < DreamUILayerStyleLayout::MaxPatterns; ++Index)
		{
			if (Patterns[Index] != Other.Patterns[Index])
			{
				return false;
			}
		}
		return Texels.Num() == 0 || FMemory::Memcmp(Texels.GetData(), Other.Texels.GetData(), Texels.Num() * sizeof(FVector4f)) == 0;
	}
};
