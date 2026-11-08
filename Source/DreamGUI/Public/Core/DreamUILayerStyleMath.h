// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamLayerStyleTypes.h"
#include "DreamUIRender/DreamUILayerStyleRecord.h"

/**
 * A layer style packed for the shaders, and DreamUILayerStyle.ush on the CPU: every function there has its twin here, step for
 * step, and a test holds the two to each other on the GPU's own pixels.
 *
 * The shading is in sRGB-encoded values, as an image editor blends; colours in are straight, colours out premultiplied.
 */
/** A point light over a styled layer (UDreamPointerLight): what its bevel is lit by in place of the style's light. */
struct FDreamUILayerStyleLight
{
	/** Where it is, in units from the layer's centre, +Y down, and how high above the layer. */
	FVector2f Position = FVector2f::ZeroVector;
	float Height = 60.0f;
	/** How far it reaches: nothing is lit past it. */
	float Radius = 200.0f;
	/** Its colour, sRGB-encoded, and its strength: the bevel's highlight is tinted and scaled by them. */
	FColor Color = FColor::White;
	float Intensity = 1.0f;

	bool operator==(const FDreamUILayerStyleLight& Other) const { return Position == Other.Position && Height == Other.Height && Radius == Other.Radius && Color == Other.Color && Intensity == Other.Intensity; }
};

struct DREAMGUI_API FDreamUILayerStyleMath
{
	/** The noise's hash seed, DREAMUI_STYLE_NOISE_SEED. */
	static constexpr uint32 NoiseSeed = 0x6C8E9CF5u;

	/**
	 * InStyle as the shaders read it, its tables made and its outer effects given their slots. False, and OutRecord empty,
	 * when the style draws nothing (FDreamLayerEffects::IsActive). Effects past the record's room (DreamUILayerStyleLayout's
	 * MaxEffects, MaxContours, MaxGradients) are left out, the later ones first.
	 */
	static bool Pack(const FDreamLayerEffects& InStyle, FDreamUILayerStyleRecord& OutRecord, const FDreamUILayerStyleLight* InLight = nullptr);

	/** A named contour at X (0..1): EDreamStyleContourShape's curves. */
	static float ContourShape(EDreamStyleContourShape InShape, float X);

	// ---- DreamUILayerStyle.ush, function for function

	/** DreamUIStyle_Soft: coverage of X < 0, a ramp W either side of 0. */
	static float Soft(float X, float W);
	/** DreamUIStyle_Hard: coverage of X < 0, antialiased over AA. */
	static float Hard(float X, float AA);
	/** DreamUIStyle_Noise: a value 0..1 for a cell. */
	static float Noise(int32 CellX, int32 CellY);
	/** DreamUIStyle_Contour: the record's contour Index (-1 linear) at V. */
	static float Contour(const FVector4f* InRecord, float Index, float V, bool bAntiAliased);
	/** DreamUIStyle_Gradient: the record's gradient Index at T: colour in xyz (straight, encoded), opacity in w. */
	static FVector4f Gradient(const FVector4f* InRecord, float Index, float T);
	/** DreamUIStyle_GradientPosition: where along a gradient laid over the layer P is. */
	static float GradientPosition(int32 Type, float Angle, float Scale, FVector2f Offset, FVector2f P, FVector2f HalfSize, bool bReverse);
	/** DreamUIStyle_Over: Cs at alpha Sa by Mode over Backdrop (premultiplied), all encoded: premultiplied out. */
	static FVector4f Over(int32 Mode, const FVector4f& Backdrop, const FVector3f& Cs, float Sa);
	/** DreamUIStyle_BevelHeight. */
	static float BevelHeight(const FVector4f* InRecord, float X, float Size, int32 Style, int32 Technique, float ContourIndex, float ContourRange, bool bContourAntiAliased, bool bDown);

	/** What DreamUIStyle_Shade is given about a pixel. */
	struct FPixel
	{
		/** Where it is, in units from the layer's centre, +Y down. */
		FVector2f P = FVector2f::ZeroVector;
		/** The layer's half size, in units: what a gradient overlay is laid across. */
		FVector2f HalfSize = FVector2f(1.0f, 1.0f);
		/** Units per pixel: what the hard edges are antialiased over. */
		float AA = 1.0f;
		/** The layer's colour here (straight, encoded) and its opacity before its edge's coverage. */
		FVector3f Color = FVector3f::ZeroVector;
		float Opacity = 0.0f;
		/** How much of the pixel the layer's shape covers: what the effects on the layer are clipped by. */
		float Coverage = 0.0f;
		/** The cell the noise is read at. */
		FIntPoint Cell = FIntPoint::ZeroValue;
	};
	/** What DreamUIStyle_Shade gives: all premultiplied, encoded. */
	struct FShaded
	{
		/** A group's outer effects, slot by slot, each to be put down by its own mode. */
		FVector4f Outer[DreamUILayerStyleLayout::MaxOuterSlots];
		/** The layer with every effect on it, its strokes and its bevel outside its edge under it. */
		FVector4f Layer = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		/** Everything, the outer effects blended with each other by their modes: an element's. */
		FVector4f Flat = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

		FShaded() { for (FVector4f& O : Outer) { O = FVector4f(0.0f, 0.0f, 0.0f, 0.0f); } }
	};
	/**
	 * DreamUIStyle_Shade: a pixel of a styled layer. InDistance is the layer's signed distance (units, negative inside) at a
	 * point; InPattern a pattern's texel (straight, encoded) at a slot and a UV, for a style that has patterns.
	 */
	static FShaded Shade(const TArray<FVector4f>& InRecord, const FPixel& InPixel, TFunctionRef<float(FVector2f)> InDistance,
		TFunctionRef<FVector4f(int32, FVector2f)> InPattern);
	static FShaded Shade(const TArray<FVector4f>& InRecord, const FPixel& InPixel, TFunctionRef<float(FVector2f)> InDistance);

	/**
	 * The exact Euclidean distance transform of a grid (Felzenszwalb and Huttenlocher's, row by row and then column by column):
	 * for every cell, the distance to the nearest cell centre on the other side of the edge than it is -- InInside says which side
	 * each is on -- less half a cell, negative inside. What a group's jump flood approximates (DreamUIJumpFlood.usf), and what
	 * a field measured off an image ahead of time is. A grid with nothing on one side gives the other side InFar.
	 */
	static void ExactDistance(TConstArrayView<bool> InInside, int32 InWidth, int32 InHeight, TArray<float>& OutSigned, float InFar = 1e6f);
	/** The squared distance from each cell to the nearest of InSeeds, along one line of InCount: the transform's one dimension. */
	static void DistanceSquared1D(const float* InCost, int32 InCount, float* OutDistance);
};
