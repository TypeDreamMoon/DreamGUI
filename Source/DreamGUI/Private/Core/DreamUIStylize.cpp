// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIStylize.h"

void FDreamUIStylizeMath::Pack(const TArray<FDreamStylizeLayer>& InLayers, float InOpacity, EDreamUIBlendMode InMode, float InBurstStart, float InBurstDuration
	, float InBurstIntensity, FDreamUIStylizePacked& OutPacked)
{
	OutPacked = FDreamUIStylizePacked();
	OutPacked.Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	OutPacked.BlendMode = InMode;
	for (const FDreamStylizeLayer& Layer : InLayers)
	{
		if (!Layer.bEnabled || Layer.Type == EDreamStylizeType::None || OutPacked.NumLayers >= DreamUIStylizeLayout::MaxLayers)
		{
			continue;
		}
		FVector4f* Row = &OutPacked.Rows[OutPacked.NumLayers * DreamUIStylizeLayout::RowsPerLayer];
		const float Type = static_cast<float>(Layer.Type);
		float Reach = 0.0f;
		switch (Layer.Type)
		{
		case EDreamStylizeType::CRT:
			Row[0] = FVector4f(Type, Layer.ScanlineStrength, Layer.ScanlineSpacing, Layer.Curvature);
			Row[1] = FVector4f(Layer.PhosphorStrength, Layer.PhosphorSize, Layer.FlickerStrength, Layer.FlickerFrequency);
			break;
		case EDreamStylizeType::Glitch:
			Row[0] = FVector4f(Type, Layer.BlockSize, Layer.BlockShift, Layer.Rate);
			Row[1] = FVector4f(Layer.ColorSplit, Layer.LineJitter, Layer.LineDensity, static_cast<float>(Layer.Seed));
			Row[2] = FVector4f(InBurstStart, InBurstDuration, InBurstIntensity, Layer.GlitchIntensity);
			// At its strongest, a burst's start: every shift and split together.
			Reach = (FMath::Max(Layer.GlitchIntensity, 0.0f) + FMath::Max(InBurstIntensity, 0.0f)) * (Layer.BlockShift + Layer.LineJitter + Layer.ColorSplit);
			break;
		case EDreamStylizeType::ChromaticAberration:
			Row[0] = FVector4f(Type, Layer.AberrationAmount, Layer.AberrationMode == EDreamChromaticAberrationMode::Directional ? 1.0f : 0.0f, Layer.AberrationAngle);
			Reach = Layer.AberrationAmount;
			break;
		case EDreamStylizeType::Halftone:
			Row[0] = FVector4f(Type, Layer.CellSize, Layer.ScreenAngle, Layer.bCMYK ? 1.0f : 0.0f);
			Row[1] = FVector4f(Layer.HalftoneShape == EDreamHalftoneShape::Lines ? 1.0f : 0.0f, Layer.PaperColor.R / 255.0f, Layer.PaperColor.G / 255.0f, Layer.PaperColor.B / 255.0f);
			Reach = Layer.CellSize;
			break;
		case EDreamStylizeType::Dither:
			Row[0] = FVector4f(Type, Layer.MatrixSize >= 8 ? 8.0f : 4.0f, static_cast<float>(FMath::Max(Layer.Levels, 2)), Layer.PixelSize);
			break;
		case EDreamStylizeType::FilmGrain:
			Row[0] = FVector4f(Type, Layer.GrainIntensity, Layer.GrainSize, Layer.bAnimateGrain ? 1.0f : 0.0f);
			break;
		case EDreamStylizeType::Vignette:
			Row[0] = FVector4f(Type, Layer.VignetteIntensity, Layer.VignetteRadius, Layer.VignetteSoftness);
			Row[1] = FVector4f(Layer.VignetteRoundness, Layer.VignetteColor.R / 255.0f, Layer.VignetteColor.G / 255.0f, Layer.VignetteColor.B / 255.0f);
			break;
		case EDreamStylizeType::Posterize:
			Row[0] = FVector4f(Type, static_cast<float>(FMath::Max(Layer.Levels, 2)), 0.0f, 0.0f);
			break;
		default:
			continue;
		}
		++OutPacked.NumLayers;
		OutPacked.Reach += FMath::Max(Reach, 0.0f);
	}
}

float FDreamUIStylizeMath::Bayer(int32 InX, int32 InY, int32 InSize)
{
	const int32 Size = InSize >= 8 ? 8 : 4;
	const int32 Bits = Size >= 8 ? 3 : 2;
	const int32 X = InX & (Size - 1);
	const int32 Y = InY & (Size - 1);
	int32 Value = 0;
	for (int32 Bit = 0; Bit < Bits; ++Bit)
	{
		const int32 Xb = (X >> Bit) & 1;
		const int32 Yb = (Y >> Bit) & 1;
		Value |= (((Xb ^ Yb) << 1) | Yb) << (2 * (Bits - 1 - Bit));
	}
	return (Value + 0.5f) / static_cast<float>(Size * Size);
}

float FDreamUIStylizeMath::Dither(float C, float Levels, float T)
{
	const float Steps = FMath::Max(Levels - 1.0f, 1.0f);
	return FMath::Clamp(FMath::FloorToFloat(FMath::Clamp(C, 0.0f, 1.0f) * Steps + T) / Steps, 0.0f, 1.0f);
}

float FDreamUIStylizeMath::Scanline(float TargetY, float PixelScale, float Spacing, float Strength)
{
	const float Line = TargetY / FMath::Max(PixelScale * Spacing, 1e-3f);
	return 1.0f - Strength * (0.5f - 0.5f * FMath::Cos(6.2831853f * Line));
}

float FDreamUIStylizeMath::Vignette(const FVector2f& Q, float Aspect, float Roundness, float Radius, float Softness, float Intensity)
{
	const FVector2f R(Q.X * FMath::Lerp(1.0f, Aspect, Roundness), Q.Y);
	const float D = R.Size() / FMath::Sqrt(2.0f);
	const float T = FMath::Clamp((D - Radius) / FMath::Max(Softness, 1e-3f), 0.0f, 1.0f);
	return Intensity * T * T * (3.0f - 2.0f * T);
}

float FDreamUIStylizeMath::HalftoneInk(const FVector2f& P, float Coverage, bool bLines, float Aa)
{
	const FVector2f F(P.X - FMath::FloorToFloat(P.X) - 0.5f, P.Y - FMath::FloorToFloat(P.Y) - 0.5f);
	const float Width = FMath::Max(Aa, 1e-4f);
	if (bLines)
	{
		const float Half = FMath::Clamp(Coverage, 0.0f, 1.0f) * 0.5f;
		return FMath::Clamp((Half - FMath::Abs(F.Y)) / Width + 0.5f, 0.0f, 1.0f) * FMath::Clamp(Half * 2.0f / Width, 0.0f, 1.0f);
	}
	const float Radius = FMath::Sqrt(FMath::Clamp(Coverage, 0.0f, 1.0f) / 3.14159265f);
	return FMath::Clamp((Radius - F.Size()) / Width + 0.5f, 0.0f, 1.0f) * FMath::Clamp(Radius * 2.0f / Width, 0.0f, 1.0f);
}
