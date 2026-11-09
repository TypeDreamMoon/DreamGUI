// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIAdjustment.generated.h"

class UCurveFloat;
class UCurveLinearColor;
class UTexture2D;

/** What an adjustment does to the colours it is given. The numbers are DreamUIAdjust.ush's DREAMUI_ADJUST_*; only appended to. */
UENUM(BlueprintType)
enum class EDreamAdjustmentType : uint8
{
	None = 0 UMETA(Hidden),
	/** Brightness shifts every channel; contrast turns the line about middle grey. */
	BrightnessContrast = 1,
	/** The input range stretched to the output range, with a gamma between. */
	Levels = 2,
	/** A curve for each channel (a colour curve asset's R, G and B), after a master curve. */
	Curves = 3,
	/** Stops of light, an offset and a gamma, in linear light. */
	Exposure = 4,
	/** The hue turned, the saturation scaled, the lightness raised or lowered; or every colour given one hue (Colorize). */
	HueSaturation = 5,
	/** Saturation raised most where there is least of it, then raised alike. */
	Vibrance = 6,
	/** Shifts towards red, green or blue in the shadows, the midtones and the highlights. */
	ColorBalance = 7,
	/** Grey, each primary and each pair of primaries weighted, optionally tinted. */
	BlackAndWhite = 8,
	/** A colour filter over the lens: multiplied in at a density. */
	PhotoFilter = 9,
	/** Each output channel a mix of the three inputs and a constant. */
	ChannelMixer = 10,
	Invert = 11,
	/** Each channel cut to a number of levels. */
	Posterize = 12,
	/** White where the luminosity reaches the level, black below. */
	Threshold = 13,
	/** The luminosity looked up along a gradient. */
	GradientMap = 14,
	/** A lookup table texture in the engine's colour grading layout (256 by 16): the catch-all. */
	LookupTable = 15,
};

/**
 * One operation of an adjustment layer (UDreamAdjustmentLayer): its type, and the parameters that type reads. Colours and
 * values are encoded (sRGB), as the blend modes are: the operations work on what the screen shows.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamAdjustment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment")
	EDreamAdjustmentType Type = EDreamAdjustmentType::BrightnessContrast;
	/** Off, it is left out, as a hidden adjustment is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BrightnessContrast", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float Brightness = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BrightnessContrast", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float Contrast = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Levels", EditConditionHides, UIMin = "0", UIMax = "1"))
	float InputBlack = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Levels", EditConditionHides, UIMin = "0", UIMax = "1"))
	float InputWhite = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Levels", EditConditionHides, ClampMin = "0.01", UIMax = "4"))
	float Gamma = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Levels", EditConditionHides, UIMin = "0", UIMax = "1"))
	float OutputBlack = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Levels", EditConditionHides, UIMin = "0", UIMax = "1"))
	float OutputWhite = 1.0f;

	/**
	 * Each channel's curve: R, G and B of a colour curve asset (its A is not read -- a new colour curve's alpha is a constant
	 * 1). A channel with no keys, or no asset, is left as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Curves", EditConditionHides))
	TObjectPtr<UCurveLinearColor> Curve = nullptr;
	/** The master curve, applied to every channel before its own; none leaves them as they are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Curves", EditConditionHides))
	TObjectPtr<UCurveFloat> MasterCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Exposure", EditConditionHides, UIMin = "-5", UIMax = "5"))
	float Exposure = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Exposure", EditConditionHides, UIMin = "-0.5", UIMax = "0.5"))
	float ExposureOffset = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Exposure", EditConditionHides, ClampMin = "0.01", UIMax = "4"))
	float ExposureGamma = 1.0f;

	/** Degrees the hue is turned; with Colorize, the hue every colour is given (0 to 360). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::HueSaturation", EditConditionHides, UIMin = "-180", UIMax = "360"))
	float Hue = 0.0f;
	/** -1 grey, 0 as it is, 1 twice as saturated; with Colorize, the saturation every colour is given (0 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::HueSaturation", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float Saturation = 0.0f;
	/** -1 black, 0 as it is, 1 white. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::HueSaturation", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float Lightness = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::HueSaturation", EditConditionHides))
	bool bColorize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Vibrance", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float Vibrance = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Vibrance", EditConditionHides, UIMin = "-1", UIMax = "1"))
	float VibranceSaturation = 0.0f;

	/** Each -1 to 1: towards cyan or red, magenta or green, yellow or blue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ColorBalance", EditConditionHides))
	FVector3f Shadows = FVector3f::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ColorBalance", EditConditionHides))
	FVector3f Midtones = FVector3f::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ColorBalance", EditConditionHides))
	FVector3f Highlights = FVector3f::ZeroVector;
	/** The colour balance and the photo filter keep each colour's luminosity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ColorBalance || Type == EDreamAdjustmentType::PhotoFilter", EditConditionHides))
	bool bPreserveLuminosity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Reds = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Yellows = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Greens = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Cyans = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Blues = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, UIMin = "-2", UIMax = "3"))
	float Magentas = 0.8f;
	/** The tint's colour, given the grey's luminosity, and how much of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides))
	FColor TintColor = FColor(225, 211, 179, 255);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::BlackAndWhite", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float TintStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::PhotoFilter", EditConditionHides))
	FColor FilterColor = FColor(236, 138, 0, 255);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::PhotoFilter", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float Density = 0.25f;

	/** Each output channel's mix of the red, green and blue inputs, plus its constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ChannelMixer", EditConditionHides))
	FVector3f RedMix = FVector3f(1.0f, 0.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ChannelMixer", EditConditionHides))
	FVector3f GreenMix = FVector3f(0.0f, 1.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ChannelMixer", EditConditionHides))
	FVector3f BlueMix = FVector3f(0.0f, 0.0f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ChannelMixer", EditConditionHides))
	FVector3f MixConstants = FVector3f::ZeroVector;
	/** Every channel the red output's mix: grey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::ChannelMixer", EditConditionHides))
	bool bMonochrome = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Posterize", EditConditionHides, ClampMin = "2", UIMax = "16"))
	int32 PosterizeLevels = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::Threshold", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float ThresholdLevel = 0.5f;

	/** Looked up along the gradient's stops from its first (black) to its last (white); its type and angle are not used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::GradientMap", EditConditionHides))
	FDreamGradient Gradient;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::GradientMap", EditConditionHides))
	bool bReverse = false;

	/** 16 slices of 16 by 16 side by side (256 by 16), blue choosing the slice; read as it is, so leave sRGB off for an encoded table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::LookupTable", EditConditionHides))
	TObjectPtr<UTexture2D> LookupTable = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustment", meta = (EditCondition = "Type == EDreamAdjustmentType::LookupTable", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.0f;
};

/**
 * DreamUIAdjust.ush on the CPU, operation for operation, and the packing the shader reads: a test holds each operation to
 * the other on a chart of colours. Colours are straight and encoded unless a name says otherwise.
 */
struct DREAMGUI_API FDreamUIAdjustMath
{
	/**
	 * InAdjustments as the shader takes them: the enabled ones, the first DreamUIAdjustLayout::MaxOps, each operation's rows,
	 * the curves and gradients baked into the table's rows, the first lookup table's texture.
	 */
	static void Pack(const TArray<FDreamAdjustment>& InAdjustments, float InOpacity, EDreamUIBlendMode InMode, FDreamUIAdjustPacked& OutPacked);

	/** One packed operation on C: DreamUIAdjust_Apply. InLookupTable is the lookup table's texels (256 by 16, as they are read), or null. */
	static FVector3f ApplyOp(const FDreamUIAdjustPacked& InPacked, int32 InOp, const FVector3f& C, const TArray<FColor>* InLookupTable = nullptr);
	/**
	 * A pixel through the adjust pass: InPremultiplied as the picture holds it, every operation, then mixed with what it was
	 * by the adjustment's mode and opacity, premultiplied again. InTargetPixel is where it is on the target, for Dissolve.
	 */
	static FVector4f ApplyToPixel(const FDreamUIAdjustPacked& InPacked, const FVector4f& InPremultiplied, bool bInLinear, bool bInOpaque, FIntPoint InTargetPixel,
		const TArray<FColor>* InLookupTable = nullptr);

	static float Luma(const FVector3f& C);
	static FVector3f RgbToHsl(const FVector3f& C);
	static FVector3f HslToRgb(const FVector3f& Hsl);
	/** A row of the table at X, as the shader's linear filter reads it. */
	static FVector4f Table(const FDreamUIAdjustPacked& InPacked, int32 InRow, float X);
	/** The engine's lookup table layout, read as the shader reads it, with InTexels the table's (256 by 16). */
	static FVector3f LookUp(const TArray<FColor>& InTexels, const FVector3f& C);
};
