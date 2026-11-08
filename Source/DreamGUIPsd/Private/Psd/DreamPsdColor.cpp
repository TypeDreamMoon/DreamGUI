// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdColor.h"

FLinearColor DreamPsdColor::HsbToRgb(float InHue, float InSaturation, float InBrightness)
{
	const float H = FMath::Fmod(FMath::Fmod(InHue, 360.0f) + 360.0f, 360.0f) / 60.0f;
	const float S = FMath::Clamp(InSaturation, 0.0f, 1.0f);
	const float V = FMath::Clamp(InBrightness, 0.0f, 1.0f);
	const int32 Sector = FMath::Clamp((int32)FMath::FloorToFloat(H), 0, 5);
	const float F = H - Sector;
	const float P = V * (1.0f - S);
	const float Q = V * (1.0f - S * F);
	const float T = V * (1.0f - S * (1.0f - F));
	switch (Sector)
	{
	case 0: return FLinearColor(V, T, P, 1.0f);
	case 1: return FLinearColor(Q, V, P, 1.0f);
	case 2: return FLinearColor(P, V, T, 1.0f);
	case 3: return FLinearColor(P, Q, V, 1.0f);
	case 4: return FLinearColor(T, P, V, 1.0f);
	default: return FLinearColor(V, P, Q, 1.0f);
	}
}

FLinearColor DreamPsdColor::CmykToRgb(float InC, float InM, float InY, float InK)
{
	const float K = 1.0f - FMath::Clamp(InK, 0.0f, 1.0f);
	return FLinearColor((1.0f - FMath::Clamp(InC, 0.0f, 1.0f)) * K, (1.0f - FMath::Clamp(InM, 0.0f, 1.0f)) * K,
		(1.0f - FMath::Clamp(InY, 0.0f, 1.0f)) * K, 1.0f);
}

float DreamPsdColor::EncodedToLinear(float InValue)
{
	const float V = FMath::Clamp(InValue, 0.0f, 1.0f);
	return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f);
}

float DreamPsdColor::LinearToEncoded(float InValue)
{
	const float V = FMath::Clamp(InValue, 0.0f, 1.0f);
	return V <= 0.0031308f ? V * 12.92f : 1.055f * FMath::Pow(V, 1.0f / 2.4f) - 0.055f;
}

FLinearColor DreamPsdColor::LabToRgb(float InL, float InA, float InB)
{
	// Lab (D50) to XYZ.
	const float Fy = (InL + 16.0f) / 116.0f;
	const float Fx = Fy + InA / 500.0f;
	const float Fz = Fy - InB / 200.0f;
	auto Finv = [](float T) { const float T3 = T * T * T; return T3 > 0.008856f ? T3 : (T - 16.0f / 116.0f) / 7.787f; };
	const float X50 = 0.96422f * Finv(Fx);
	const float Y50 = 1.00000f * Finv(Fy);
	const float Z50 = 0.82521f * Finv(Fz);
	// Bradford D50 -> D65.
	const float X = 0.9555766f * X50 - 0.0230393f * Y50 + 0.0631636f * Z50;
	const float Y = -0.0282895f * X50 + 1.0099416f * Y50 + 0.0210077f * Z50;
	const float Z = 0.0122982f * X50 - 0.0204830f * Y50 + 1.3299098f * Z50;
	// XYZ (D65) to linear sRGB.
	const float R = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
	const float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
	const float B = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
	return FLinearColor(LinearToEncoded(R), LinearToEncoded(G), LinearToEncoded(B), 1.0f);
}

FColor DreamPsdColor::ToColor(const FLinearColor& InEncoded)
{
	auto Q = [](float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255); };
	return FColor(Q(InEncoded.R), Q(InEncoded.G), Q(InEncoded.B), Q(InEncoded.A));
}
