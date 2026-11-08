// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamLayerStyleTypes.generated.h"

class UTexture;

/*
 * A layer style: the effects an image editor's layer style dialog puts round and over a layer -- drop shadows, an outer
 * glow, inner shadows, an inner glow, a bevel and emboss, a satin, colour, gradient and pattern overlays, strokes -- each
 * with its own blend mode and opacity. The values are the dialog's, so that a layer style read from an image editor's file
 * maps onto these one for one: angles in degrees as the dialog has them (0 a light from the right, counter-clockwise, so 120
 * is from the upper left), sizes and distances in units of the canvas (a pixel of the file is a unit), percentages as
 * fractions (75% is 0.75).
 *
 * How each is drawn is DreamUILayerStyle.ush's, and FDreamUILayerStyleMath's on the CPU.
 */

/** A contour: how an effect's falloff, 0 at its far end to 1 at the shape, is reshaped before it becomes an alpha. */
UENUM(BlueprintType)
enum class EDreamStyleContourShape : uint8
{
	/** The falloff as it is. */
	Linear,
	/** Up to the middle and down again: a ring that peaks halfway. */
	Cone,
	/** Down to the middle and up again. */
	ConeInverted,
	/** Low for long, rising late and steeply. */
	CoveDeep,
	/** Low at first, rising late. */
	CoveShallow,
	/** An S: slow at both ends. */
	Gaussian,
	/** A quarter circle: rising early, flat at the top. */
	HalfRound,
	/** A rounded bump. */
	Ring,
	/** Two rounded bumps. */
	RingDouble,
	/** A ramp with a ripple on it. */
	RollingSlope,
	/** Four steps with rounded risers. */
	RoundedSteps,
	/** Three teeth. */
	Sawtooth,
	/** The points of Custom. */
	Custom,
};

/** A point of a custom contour: where the falloff Input maps to Output, both 0..1. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleContourPoint
{
	GENERATED_BODY()

	FDreamStyleContourPoint() = default;
	FDreamStyleContourPoint(float InInput, float InOutput, bool bInCorner = false) : Input(InInput), Output(InOutput), bCorner(bInCorner) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour", meta = (ClampMin = "0", ClampMax = "1"))
	float Input = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour", meta = (ClampMin = "0", ClampMax = "1"))
	float Output = 0.0f;
	/** A corner: the curve bends here instead of passing through smoothly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour")
	bool bCorner = false;

	bool operator==(const FDreamStyleContourPoint& Other) const { return Input == Other.Input && Output == Other.Output && bCorner == Other.bCorner; }
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleContour
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour")
	EDreamStyleContourShape Shape = EDreamStyleContourShape::Linear;
	/** Custom: the curve's points, by Input; a curve of fewer than two is Linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour", meta = (EditCondition = "Shape == EDreamStyleContourShape::Custom", EditConditionHides))
	TArray<FDreamStyleContourPoint> Points;

	bool IsLinear() const;
	/** The curve at X (0..1), exactly: what the shader's table of samples is made from. */
	float Evaluate(float X) const;

	bool operator==(const FDreamStyleContour& Other) const { return Shape == Other.Shape && Points == Other.Points; }
};

/** A colour stop of a style's gradient. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleGradientColorStop
{
	GENERATED_BODY()

	FDreamStyleGradientColorStop() = default;
	FDreamStyleGradientColorStop(float InLocation, const FColor& InColor, float InMidpoint = 0.5f) : Location(InLocation), Midpoint(InMidpoint), Color(InColor) {}

	/** Along the gradient, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0", ClampMax = "1"))
	float Location = 0.0f;
	/** Where between this stop and the next their colours are half and half, as a fraction of the way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float Midpoint = 0.5f;
	/** sRGB-encoded; its alpha is not read (opacity has stops of its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FColor Color = FColor::White;

	bool operator==(const FDreamStyleGradientColorStop& Other) const { return Location == Other.Location && Midpoint == Other.Midpoint && Color == Other.Color; }
};

/** An opacity stop of a style's gradient. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleGradientOpacityStop
{
	GENERATED_BODY()

	FDreamStyleGradientOpacityStop() = default;
	FDreamStyleGradientOpacityStop(float InLocation, float InOpacity, float InMidpoint = 0.5f) : Location(InLocation), Midpoint(InMidpoint), Opacity(InOpacity) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0", ClampMax = "1"))
	float Location = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float Midpoint = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;

	bool operator==(const FDreamStyleGradientOpacityStop& Other) const { return Location == Other.Location && Midpoint == Other.Midpoint && Opacity == Other.Opacity; }
};

/**
 * A style's gradient, as an image editor keeps one: colour stops and opacity stops apart, each pair of neighbours with a
 * midpoint. Interpolated in sRGB-encoded values, as the editor does.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleGradient
{
	GENERATED_BODY()

	FDreamStyleGradient();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	TArray<FDreamStyleGradientColorStop> ColorStops;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	TArray<FDreamStyleGradientOpacityStop> OpacityStops;
	/**
	 * The editor's smoothness, kept so that a gradient read from a file can be written back as it was. Not drawn: every
	 * gradient is interpolated as at 0%, linearly between stops (after the midpoint).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0", ClampMax = "1"))
	float Smoothness = 1.0f;

	/** Two colours, opaque, from A at 0 to B at 1. */
	static FDreamStyleGradient FromColors(const FColor& InA, const FColor& InB);
	/** At T (0..1): the colour, sRGB-encoded, straight, in xyz, and the opacity in w. */
	FVector4f Evaluate(float T) const;

	bool operator==(const FDreamStyleGradient& Other) const { return ColorStops == Other.ColorStops && OpacityStops == Other.OpacityStops && Smoothness == Other.Smoothness; }
};

/** How a gradient fill is laid across the layer. */
UENUM(BlueprintType)
enum class EDreamStyleGradientType : uint8
{
	/** Along a line at the angle. */
	Linear,
	/** Out from the centre. */
	Radial,
	/** Round the centre, from the angle. */
	Angle,
	/** Out both ways from a line through the centre. */
	Reflected,
	/** Out from the centre in a diamond. */
	Diamond,
};

/** A gradient laid across the layer: a gradient overlay's, a stroke's, a glow's gradient is only the gradient. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleGradientFill
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FDreamStyleGradient Gradient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamStyleGradientType Type = EDreamStyleGradientType::Linear;
	/** Degrees: 0 left to right, 90 bottom to top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (UIMin = "-180", UIMax = "180"))
	float Angle = 90.0f;
	/** How long the gradient is as a fraction of the layer's extent along it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float Scale = 1.0f;
	/** Where its centre is moved to, as fractions of the layer's width and height, +Y down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FVector2f Offset = FVector2f::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	bool bReverse = false;
	/**
	 * Laid across the layer's rect (on), or across what the layer draws as its effects see it (off): a group's picture, a text's
	 * glyphs. The same for a rect block, whose shape is its rect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	bool bAlignWithLayer = true;
	/** A little noise on the gradient, against banding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	bool bDither = false;

	bool operator==(const FDreamStyleGradientFill& Other) const;
};

/** A pattern laid across the layer, tiled. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStylePatternFill
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	TObjectPtr<UTexture> Pattern = nullptr;
	/** A texel of the pattern is this many units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (ClampMin = "0.01", UIMax = "10"))
	float Scale = 1.0f;
	/** Where the pattern's first texel is, in units from the layer's top left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	FVector2f Offset = FVector2f::ZeroVector;
	/** Moves with the layer (on), or stays where it is on the canvas (off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	bool bLinkWithLayer = true;

	bool operator==(const FDreamStylePatternFill& Other) const { return Pattern == Other.Pattern && Scale == Other.Scale && Offset == Other.Offset && bLinkWithLayer == Other.bLinkWithLayer; }
};

/** Softer: a blur's falloff. Precise: the distance from the shape, as it is. */
UENUM(BlueprintType)
enum class EDreamStyleTechnique : uint8
{
	Softer,
	Precise,
};

/** Where an inner glow comes from. */
UENUM(BlueprintType)
enum class EDreamStyleGlowSource : uint8
{
	/** In from the edge. */
	Edge,
	/** Out from the middle. */
	Center,
};

/** What a glow is filled with. */
UENUM(BlueprintType)
enum class EDreamStyleGlowFill : uint8
{
	Color,
	/** Its gradient, from the shape out (an outer glow) or in (an inner glow). */
	Gradient,
};

UENUM(BlueprintType)
enum class EDreamStyleStrokePosition : uint8
{
	Outside,
	Inside,
	Center,
};

UENUM(BlueprintType)
enum class EDreamStyleStrokeFill : uint8
{
	Color,
	Gradient,
	Pattern,
};

UENUM(BlueprintType)
enum class EDreamStyleBevelStyle : uint8
{
	/** Raised inside the edge. */
	InnerBevel,
	/** Raised outside the edge, over what is behind. */
	OuterBevel,
	/** Raised across the edge, half in and half out. */
	Emboss,
	/** Pressed into what is behind along the edge. */
	PillowEmboss,
	/** On the layer's stroke (its first enabled one); without a stroke, nothing. */
	StrokeEmboss,
};

UENUM(BlueprintType)
enum class EDreamStyleBevelTechnique : uint8
{
	/** Rounded. */
	Smooth,
	/** A hard, straight slope. */
	ChiselHard,
	/** A straight slope, softened. */
	ChiselSoft,
};

UENUM(BlueprintType)
enum class EDreamStyleBevelDirection : uint8
{
	Up,
	Down,
};

/** A shadow cast behind the layer, away from the light. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleDropShadow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Multiply;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	FColor Color = FColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.75f;
	/** Take the angle from the style's global light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	bool bUseGlobalLight = true;
	/** Where the light comes from, degrees: the shadow falls the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (UIMin = "-180", UIMax = "180", EditCondition = "!bUseGlobalLight"))
	float Angle = 120.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (ClampMin = "0", UIMax = "100"))
	float Distance = 5.0f;
	/** How much of Size is solid before it fades: 0 all of it fades, 1 a hard edge Size out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Spread = 0.0f;
	/** How far it reaches out past the shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	FDreamStyleContour Contour;
	/** The contour's table read between its samples (on), or at the nearest (off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	bool bAntiAliased = false;
	/** Grain in the shadow's alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Noise = 0.0f;
	/** The layer hides its own shadow where it covers it, even where the layer is see-through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	bool bLayerKnocksOut = true;

	bool operator==(const FDreamStyleDropShadow& Other) const;
};

/** A shadow inside the layer's edge, as if the layer were a hole. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleInnerShadow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Multiply;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	FColor Color = FColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	bool bUseGlobalLight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (UIMin = "-180", UIMax = "180", EditCondition = "!bUseGlobalLight"))
	float Angle = 120.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (ClampMin = "0", UIMax = "100"))
	float Distance = 5.0f;
	/** How much of Size is solid before it fades, in from the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Choke = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	FDreamStyleContour Contour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow")
	bool bAntiAliased = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Noise = 0.0f;

	bool operator==(const FDreamStyleInnerShadow& Other) const;
};

/** A glow round the outside of the layer's edge. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleOuterGlow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Screen;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Noise = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	EDreamStyleGlowFill Fill = EDreamStyleGlowFill::Color;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	FColor Color = FColor(255, 255, 190);
	/** Fill Gradient: from the shape (0) out to the glow's end (1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	FDreamStyleGradient Gradient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	EDreamStyleTechnique Technique = EDreamStyleTechnique::Softer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Spread = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	FDreamStyleContour Contour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow")
	bool bAntiAliased = false;
	/** How much of the falloff the contour plays over: at 1 all of it; lower, the glow stays solid further out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0.01", ClampMax = "1"))
	float Range = 0.5f;
	/** Fill Gradient: how much the gradient is shaken along itself, pixel by pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outer Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Jitter = 0.0f;

	bool operator==(const FDreamStyleOuterGlow& Other) const;
};

/** A glow inside the layer's edge, or out from its middle. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleInnerGlow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Screen;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Noise = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	EDreamStyleGlowFill Fill = EDreamStyleGlowFill::Color;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	FColor Color = FColor(255, 255, 190);
	/** Fill Gradient: from where the glow comes from (0) to its end (1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	FDreamStyleGradient Gradient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	EDreamStyleTechnique Technique = EDreamStyleTechnique::Softer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	EDreamStyleGlowSource Source = EDreamStyleGlowSource::Edge;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Choke = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	FDreamStyleContour Contour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow")
	bool bAntiAliased = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0.01", ClampMax = "1"))
	float Range = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Jitter = 0.0f;

	bool operator==(const FDreamStyleInnerGlow& Other) const;
};

/** The layer's edge raised or pressed in, lit from an angle and a height: a highlight on one side, a shadow on the other. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleBevelEmboss
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	EDreamStyleBevelStyle Style = EDreamStyleBevelStyle::InnerBevel;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	EDreamStyleBevelTechnique Technique = EDreamStyleBevelTechnique::Smooth;
	/** How steep the slope is: 1 is 100%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0.01", ClampMax = "10"))
	float Depth = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	EDreamStyleBevelDirection Direction = EDreamStyleBevelDirection::Up;
	/** How wide the slope is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 5.0f;
	/** How much the slope is blurred. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0", UIMax = "16"))
	float Soften = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	bool bUseGlobalLight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (UIMin = "-180", UIMax = "180", EditCondition = "!bUseGlobalLight"))
	float Angle = 120.0f;
	/** How high the light is, degrees: 90 straight above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0", ClampMax = "90", EditCondition = "!bUseGlobalLight"))
	float Altitude = 30.0f;
	/** How the lit slope's brightness is reshaped: a ring makes it look metallic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	FDreamStyleContour GlossContour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	bool bGlossAntiAliased = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	EDreamUIBlendMode HighlightMode = EDreamUIBlendMode::Screen;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	FColor HighlightColor = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0", ClampMax = "1"))
	float HighlightOpacity = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	EDreamUIBlendMode ShadowMode = EDreamUIBlendMode::Multiply;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	FColor ShadowColor = FColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0", ClampMax = "1"))
	float ShadowOpacity = 0.75f;
	/** Reshape the slope itself by Contour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	bool bUseContour = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (EditCondition = "bUseContour"))
	FDreamStyleContour Contour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (EditCondition = "bUseContour"))
	bool bContourAntiAliased = false;
	/** How much of the slope Contour plays over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "0.01", ClampMax = "1", EditCondition = "bUseContour"))
	float ContourRange = 0.5f;
	/** Bump the surface with a pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss")
	bool bUseTexture = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (EditCondition = "bUseTexture"))
	FDreamStylePatternFill Texture;
	/** How deep the pattern's bumps are: 1 is 100%, negative presses them in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (ClampMin = "-10", ClampMax = "10", EditCondition = "bUseTexture"))
	float TextureDepth = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bevel and Emboss", meta = (EditCondition = "bUseTexture"))
	bool bTextureInvert = false;

	bool operator==(const FDreamStyleBevelEmboss& Other) const;
};

/** Silky inner shading: the layer's shape, shifted both ways along an angle and blurred, where the two differ. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleSatin
{
	GENERATED_BODY()

	FDreamStyleSatin();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Multiply;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	FColor Color = FColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin", meta = (UIMin = "-180", UIMax = "180"))
	float Angle = 19.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin", meta = (ClampMin = "0", UIMax = "100"))
	float Distance = 11.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin", meta = (ClampMin = "0", UIMax = "100"))
	float Size = 14.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	FDreamStyleContour Contour;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	bool bAntiAliased = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satin")
	bool bInvert = true;

	bool operator==(const FDreamStyleSatin& Other) const;
};

/** The layer filled with one colour. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleColorOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Overlay")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Overlay")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Overlay")
	FColor Color = FColor::Red;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Overlay", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;

	bool operator==(const FDreamStyleColorOverlay& Other) const { return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Color == Other.Color && Opacity == Other.Opacity; }
};

/** The layer filled with a gradient. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleGradientOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient Overlay")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient Overlay")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient Overlay", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient Overlay")
	FDreamStyleGradientFill Fill;

	bool operator==(const FDreamStyleGradientOverlay& Other) const { return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Opacity == Other.Opacity && Fill == Other.Fill; }
};

/** The layer filled with a tiled pattern. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStylePatternOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Overlay")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Overlay")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Overlay", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Overlay")
	FDreamStylePatternFill Pattern;

	bool operator==(const FDreamStylePatternOverlay& Other) const { return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Opacity == Other.Opacity && Pattern == Other.Pattern; }
};

/** A line along the layer's edge: outside it, inside it, or centred on it. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStyleStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", UIMax = "64"))
	float Size = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	EDreamStyleStrokePosition Position = EDreamStyleStrokePosition::Outside;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	EDreamStyleStrokeFill Fill = EDreamStyleStrokeFill::Color;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	FColor Color = FColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	FDreamStyleGradientFill Gradient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	FDreamStylePatternFill Pattern;
	/** Kept so a style read from a file can be written back as it was; drawn as off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	bool bOverprint = false;

	bool operator==(const FDreamStyleStroke& Other) const;
};

/**
 * A whole layer style. The effects that can be there more than once are arrays, drawn in order: a later one over an earlier
 * one. From the bottom up a styled layer is drawn: drop shadows, the outer glow, the layer itself (at FillOpacity), the
 * pattern overlay, gradient overlays, colour overlays, the satin, the inner glow, inner shadows, strokes, the bevel and
 * emboss. The drop shadows and the outer glow lie behind the layer; everything from the overlays up lies on it, where it
 * has alpha, and blends with it by its own mode.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLayerEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	bool bEnabled = true;
	/** Every size and distance of every effect, times this: an editor's "scale effects". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style", meta = (ClampMin = "0.01", UIMax = "10"))
	float Scale = 1.0f;
	/** The layer's own pixels' opacity; its effects keep theirs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style", meta = (ClampMin = "0", ClampMax = "1"))
	float FillOpacity = 1.0f;
	/** The light every effect that uses the global light takes, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style", meta = (UIMin = "-180", UIMax = "180"))
	float GlobalLightAngle = 120.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style", meta = (ClampMin = "0", ClampMax = "90"))
	float GlobalLightAltitude = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	TArray<FDreamStyleDropShadow> DropShadows;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	FDreamStyleOuterGlow OuterGlow;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	FDreamStylePatternOverlay PatternOverlay;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	TArray<FDreamStyleGradientOverlay> GradientOverlays;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	TArray<FDreamStyleColorOverlay> ColorOverlays;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	FDreamStyleSatin Satin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	FDreamStyleInnerGlow InnerGlow;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	TArray<FDreamStyleInnerShadow> InnerShadows;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	TArray<FDreamStyleStroke> Strokes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Style")
	FDreamStyleBevelEmboss BevelEmboss;

	/** Enabled, with at least one enabled effect, or a fill opacity below 1. */
	bool IsActive() const;
	/** How far past the layer's edge its effects reach, in units (Scale applied): what its drawing has to be grown by. */
	float GetOuterReach() const;
	/** Whether an enabled effect reads a pattern's texture. */
	bool UsesPatterns() const;
	/** Whether an enabled effect that lies behind the layer blends with what is behind by a mode other than Normal. */
	bool HasOuterEffectsReadingBackdrop() const;

	bool operator==(const FDreamLayerEffects& Other) const;
	bool operator!=(const FDreamLayerEffects& Other) const { return !(*this == Other); }
};
