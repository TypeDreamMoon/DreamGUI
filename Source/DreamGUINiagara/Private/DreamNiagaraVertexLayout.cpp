// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraVertexLayout.h"

float DreamNiagaraVertexLayout::Pack(float InScale, float InBlend)
{
	const float Steps = FMath::Clamp(FMath::RoundToFloat(InScale * ScaleSteps), ScaleSteps, MaxScale * ScaleSteps);
	return Steps + FMath::Clamp(InBlend, 0.0f, 1.0f) * BlendRange;
}

void DreamNiagaraVertexLayout::Unpack(float InPacked, float& OutScale, float& OutBlend)
{
	const float Whole = FMath::Floor(InPacked);
	OutScale = FMath::Max(Whole, ScaleSteps) / ScaleSteps;
	OutBlend = FMath::Clamp((InPacked - Whole) / BlendRange, 0.0f, 1.0f);
}

float DreamNiagaraVertexLayout::ScaleFor(const FLinearColor& InLinear)
{
	const float Brightest = FMath::Max3(InLinear.R, InLinear.G, InLinear.B);
	if (Brightest <= 1.0f)
	{
		return 1.0f;
	}
	return FMath::Min(FMath::CeilToFloat(Brightest * ScaleSteps), MaxScale * ScaleSteps) / ScaleSteps;
}

FColor DreamNiagaraVertexLayout::EncodeColor(const FLinearColor& InLinear, float InScale)
{
	const float Divide = InScale > 1.0f ? 1.0f / InScale : 1.0f;
	const FLinearColor Scaled(
		FMath::Clamp(InLinear.R * Divide, 0.0f, 1.0f),
		FMath::Clamp(InLinear.G * Divide, 0.0f, 1.0f),
		FMath::Clamp(InLinear.B * Divide, 0.0f, 1.0f),
		FMath::Clamp(InLinear.A, 0.0f, 1.0f));
	// sRGB, as every DreamGUI vertex colour is authored: the shaders take it back to linear light.
	return Scaled.ToFColor(true);
}
