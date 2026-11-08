// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamUIStylize.generated.h"

/** What a stylize layer does. The numbers are DreamUIStylize.ush's DREAMUI_STYLIZE_*; only appended to. */
UENUM(BlueprintType)
enum class EDreamStylizeType : uint8
{
	None = 0 UMETA(Hidden),
	/** A CRT screen: scanlines, the screen's curve, a phosphor mask, flicker. */
	CRT = 1,
	/** Blocks shifted, lines jittered, red and blue split apart; TriggerBurst makes it flare. */
	Glitch = 2,
	/** Red and blue sampled apart: outward from the middle, or along a direction. */
	ChromaticAberration = 3,
	/** A halftone screen: dots or lines, one ink or four (CMYK). */
	Halftone = 4,
	/** Ordered dithering to a few levels, through a 4 by 4 or 8 by 8 Bayer matrix. */
	Dither = 5,
	FilmGrain = 6,
	Vignette = 7,
	Posterize = 8,
};

UENUM(BlueprintType)
enum class EDreamChromaticAberrationMode : uint8
{
	/** Outward from the element's middle, more the further out. */
	Radial,
	/** Along Angle, the same everywhere. */
	Directional,
};

UENUM(BlueprintType)
enum class EDreamHalftoneShape : uint8
{
	Dots,
	Lines,
};

/**
 * One layer of a stylize effect (UDreamStylizeEffect), in the order they are listed: each works on what the ones before it
 * made. Sizes are in units of the canvas, so the look keeps its size with the UI's scale; a pattern laid on the screen
 * (scanlines, a halftone screen, the dither, grain) stays put as the element moves.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamStylizeLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize")
	EDreamStylizeType Type = EDreamStylizeType::CRT;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize")
	bool bEnabled = true;

	/** How dark the gaps between scanlines are, and how far apart the lines are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float ScanlineStrength = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0.5", UIMax = "16"))
	float ScanlineSpacing = 3.0f;
	/** How much the screen bulges: 0 flat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0", UIMax = "1"))
	float Curvature = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float PhosphorStrength = 0.25f;
	/** A column of the phosphor mask, one of red, green and blue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0.5", UIMax = "8"))
	float PhosphorSize = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float FlickerStrength = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::CRT", EditConditionHides, ClampMin = "0", UIMax = "60"))
	float FlickerFrequency = 30.0f;

	/** How much there is of it without a burst: 0 none, 1 plenty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", UIMax = "2"))
	float GlitchIntensity = 0.3f;
	/** The height of a shifted block, and the most it shifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "1", UIMax = "64"))
	float BlockSize = 16.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", UIMax = "64"))
	float BlockShift = 12.0f;
	/** How far red and blue split apart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", UIMax = "16"))
	float ColorSplit = 2.0f;
	/** The most a jittered line moves, and how many lines jitter (0 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", UIMax = "32"))
	float LineJitter = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float LineDensity = 0.1f;
	/** How many times a second the glitch changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides, ClampMin = "0", UIMax = "60"))
	float Rate = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Glitch", EditConditionHides))
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::ChromaticAberration", EditConditionHides))
	EDreamChromaticAberrationMode AberrationMode = EDreamChromaticAberrationMode::Radial;
	/** How far red and blue are sampled apart: at the edge for Radial, everywhere for Directional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::ChromaticAberration", EditConditionHides, ClampMin = "0", UIMax = "16"))
	float AberrationAmount = 3.0f;
	/** Directional: degrees, 0 up, clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::ChromaticAberration", EditConditionHides, UIMin = "0", UIMax = "360"))
	float AberrationAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Halftone", EditConditionHides))
	EDreamHalftoneShape HalftoneShape = EDreamHalftoneShape::Dots;
	/** Four screens, cyan, magenta, yellow and black, rather than one of black ink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Halftone", EditConditionHides))
	bool bCMYK = false;
	/** A cell of the screen, and the screen's angle in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Halftone", EditConditionHides, ClampMin = "2", UIMax = "32"))
	float CellSize = 6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Halftone", EditConditionHides, UIMin = "0", UIMax = "90"))
	float ScreenAngle = 45.0f;
	/** The paper one ink is laid on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Halftone && !bCMYK", EditConditionHides))
	FColor PaperColor = FColor::White;

	/** 4 or 8: the Bayer matrix's size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Dither", EditConditionHides, ClampMin = "4", ClampMax = "8"))
	int32 MatrixSize = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Dither || Type == EDreamStylizeType::Posterize", EditConditionHides, ClampMin = "2", UIMax = "16"))
	int32 Levels = 4;
	/** How big a dither cell is: 1 a unit of the canvas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Dither", EditConditionHides, ClampMin = "0.5", UIMax = "8"))
	float PixelSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::FilmGrain", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float GrainIntensity = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::FilmGrain", EditConditionHides, ClampMin = "0.5", UIMax = "8"))
	float GrainSize = 1.0f;
	/** A new grain 24 times a second, rather than one that holds still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::FilmGrain", EditConditionHides))
	bool bAnimateGrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Vignette", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float VignetteIntensity = 0.5f;
	/** Where the darkening starts, 0 the middle and 1 the corners, and how far it takes to reach full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Vignette", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float VignetteRadius = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Vignette", EditConditionHides, ClampMin = "0.01", ClampMax = "1"))
	float VignetteSoftness = 0.5f;
	/** 1 round in the element's units, 0 following its box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Vignette", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float VignetteRoundness = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stylize", meta = (EditCondition = "Type == EDreamStylizeType::Vignette", EditConditionHides))
	FColor VignetteColor = FColor::Black;
};

/**
 * DreamUIStylize.ush on the CPU for the layers that are a function of a pixel's own colour and place, and the packing the
 * shader reads. Colours straight and encoded.
 */
struct DREAMGUI_API FDreamUIStylizeMath
{
	/** InLayers as the shader takes them: the enabled ones, the first DreamUIStylizeLayout::MaxLayers, and how far they read. */
	static void Pack(const TArray<FDreamStylizeLayer>& InLayers, float InOpacity, EDreamUIBlendMode InMode, float InBurstStart, float InBurstDuration, float InBurstIntensity,
		FDreamUIStylizePacked& OutPacked);

	static constexpr uint32 GlitchSeed = 0x68E31DA4u;
	static constexpr uint32 GrainSeed = 0xB5297A4Du;

	/** DreamUIStylize_Bayer: the threshold of a cell, 0 up to 1. */
	static float Bayer(int32 InX, int32 InY, int32 InSize);
	static float Dither(float C, float Levels, float T);
	static float Scanline(float TargetY, float PixelScale, float Spacing, float Strength);
	static float Vignette(const FVector2f& Q, float Aspect, float Roundness, float Radius, float Softness, float Intensity);
	static float HalftoneInk(const FVector2f& P, float Coverage, bool bLines, float Aa);
};
