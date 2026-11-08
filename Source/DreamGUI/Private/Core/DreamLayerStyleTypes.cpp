// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamUILayerStyleMath.h"

bool FDreamStyleContour::IsLinear() const
{
	return Shape == EDreamStyleContourShape::Linear || (Shape == EDreamStyleContourShape::Custom && Points.Num() < 2);
}

float FDreamStyleContour::Evaluate(float X) const
{
	X = FMath::Clamp(X, 0.0f, 1.0f);
	if (Shape != EDreamStyleContourShape::Custom)
	{
		return FDreamUILayerStyleMath::ContourShape(Shape, X);
	}
	if (Points.Num() < 2)
	{
		return X;
	}
	TArray<FDreamStyleContourPoint, TInlineAllocator<16>> Sorted(Points);
	Sorted.StableSort([](const FDreamStyleContourPoint& A, const FDreamStyleContourPoint& B) { return A.Input < B.Input; });
	if (X <= Sorted[0].Input)
	{
		return FMath::Clamp(Sorted[0].Output, 0.0f, 1.0f);
	}
	if (X >= Sorted.Last().Input)
	{
		return FMath::Clamp(Sorted.Last().Output, 0.0f, 1.0f);
	}
	int32 Segment = 0;
	while (Segment + 2 < Sorted.Num() && X > Sorted[Segment + 1].Input)
	{
		++Segment;
	}
	const FDreamStyleContourPoint& A = Sorted[Segment];
	const FDreamStyleContourPoint& B = Sorted[Segment + 1];
	const float Span = FMath::Max(B.Input - A.Input, 1e-6f);
	const float T = (X - A.Input) / Span;
	if (A.bCorner && B.bCorner)
	{
		return FMath::Clamp(FMath::Lerp(A.Output, B.Output, T), 0.0f, 1.0f);
	}
	// A cubic through the two points whose slopes are the chords' about each (a corner's slope on its side is the chord's
	// own): smooth through every point that is not a corner.
	auto Slope = [&Sorted](int32 Index, bool bCorner, int32 Toward) -> float
	{
		const int32 Prev = FMath::Max(Index - 1, 0);
		const int32 Next = FMath::Min(Index + 1, Sorted.Num() - 1);
		if (bCorner)
		{
			const int32 Other = Toward > 0 ? Next : Prev;
			const float Run = Sorted[Other].Input - Sorted[Index].Input;
			return FMath::Abs(Run) > 1e-6f ? (Sorted[Other].Output - Sorted[Index].Output) / Run : 0.0f;
		}
		const float Run = Sorted[Next].Input - Sorted[Prev].Input;
		return Run > 1e-6f ? (Sorted[Next].Output - Sorted[Prev].Output) / Run : 0.0f;
	};
	const float M0 = Slope(Segment, A.bCorner, 1) * Span;
	const float M1 = Slope(Segment + 1, B.bCorner, -1) * Span;
	const float T2 = T * T;
	const float T3 = T2 * T;
	const float Y = (2.0f * T3 - 3.0f * T2 + 1.0f) * A.Output + (T3 - 2.0f * T2 + T) * M0 + (-2.0f * T3 + 3.0f * T2) * B.Output + (T3 - T2) * M1;
	return FMath::Clamp(Y, 0.0f, 1.0f);
}

FDreamStyleGradient::FDreamStyleGradient()
{
	ColorStops.Add(FDreamStyleGradientColorStop(0.0f, FColor::Black));
	ColorStops.Add(FDreamStyleGradientColorStop(1.0f, FColor::White));
	OpacityStops.Add(FDreamStyleGradientOpacityStop(0.0f, 1.0f));
	OpacityStops.Add(FDreamStyleGradientOpacityStop(1.0f, 1.0f));
}

FDreamStyleGradient FDreamStyleGradient::FromColors(const FColor& InA, const FColor& InB)
{
	FDreamStyleGradient Gradient;
	Gradient.ColorStops[0].Color = InA;
	Gradient.ColorStops[1].Color = InB;
	return Gradient;
}

namespace DreamLayerStyleTypesLocal
{
	/** Where a pair of stops is at T, 0..1 between them, its midpoint M the place where they are half and half. */
	float Toward(float T, float M)
	{
		M = FMath::Clamp(M, 0.05f, 0.95f);
		if (FMath::IsNearlyEqual(M, 0.5f))
		{
			return T;
		}
		return FMath::Pow(FMath::Clamp(T, 0.0f, 1.0f), FMath::Loge(0.5f) / FMath::Loge(M));
	}

	template <typename StopType, typename ValueType, typename GetValue>
	ValueType Interpolate(const TArray<StopType>& InStops, float T, GetValue InGet, const ValueType& InDefault)
	{
		if (InStops.Num() == 0)
		{
			return InDefault;
		}
		TArray<const StopType*, TInlineAllocator<16>> Sorted;
		for (const StopType& Stop : InStops)
		{
			Sorted.Add(&Stop);
		}
		Sorted.StableSort([](const StopType& A, const StopType& B) { return A.Location < B.Location; });
		if (T <= Sorted[0]->Location)
		{
			return InGet(*Sorted[0]);
		}
		for (int32 Index = 0; Index + 1 < Sorted.Num(); ++Index)
		{
			const StopType& A = *Sorted[Index];
			const StopType& B = *Sorted[Index + 1];
			if (T <= B.Location)
			{
				const float Span = B.Location - A.Location;
				const float Local = Span > 1e-6f ? (T - A.Location) / Span : 1.0f;
				return InGet(A) + (InGet(B) - InGet(A)) * Toward(Local, A.Midpoint);
			}
		}
		return InGet(*Sorted.Last());
	}
}

FVector4f FDreamStyleGradient::Evaluate(float T) const
{
	using namespace DreamLayerStyleTypesLocal;
	T = FMath::Clamp(T, 0.0f, 1.0f);
	const FVector3f Color = Interpolate(ColorStops, T,
		[](const FDreamStyleGradientColorStop& S) { return FVector3f(S.Color.R, S.Color.G, S.Color.B) / 255.0f; }, FVector3f(1.0f, 1.0f, 1.0f));
	const float Opacity = Interpolate(OpacityStops, T, [](const FDreamStyleGradientOpacityStop& S) { return S.Opacity; }, 1.0f);
	return FVector4f(Color.X, Color.Y, Color.Z, FMath::Clamp(Opacity, 0.0f, 1.0f));
}

bool FDreamStyleGradientFill::operator==(const FDreamStyleGradientFill& Other) const
{
	return Gradient == Other.Gradient && Type == Other.Type && Angle == Other.Angle && Scale == Other.Scale && Offset == Other.Offset
		&& bReverse == Other.bReverse && bAlignWithLayer == Other.bAlignWithLayer && bDither == Other.bDither;
}

bool FDreamStyleDropShadow::operator==(const FDreamStyleDropShadow& Other) const
{
	return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Color == Other.Color && Opacity == Other.Opacity
		&& bUseGlobalLight == Other.bUseGlobalLight && Angle == Other.Angle && Distance == Other.Distance && Spread == Other.Spread
		&& Size == Other.Size && Contour == Other.Contour && bAntiAliased == Other.bAntiAliased && Noise == Other.Noise
		&& bLayerKnocksOut == Other.bLayerKnocksOut;
}

bool FDreamStyleInnerShadow::operator==(const FDreamStyleInnerShadow& Other) const
{
	return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Color == Other.Color && Opacity == Other.Opacity
		&& bUseGlobalLight == Other.bUseGlobalLight && Angle == Other.Angle && Distance == Other.Distance && Choke == Other.Choke
		&& Size == Other.Size && Contour == Other.Contour && bAntiAliased == Other.bAntiAliased && Noise == Other.Noise;
}

bool FDreamStyleOuterGlow::operator==(const FDreamStyleOuterGlow& Other) const
{
	return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Opacity == Other.Opacity && Noise == Other.Noise
		&& Fill == Other.Fill && Color == Other.Color && Gradient == Other.Gradient && Technique == Other.Technique
		&& Spread == Other.Spread && Size == Other.Size && Contour == Other.Contour && bAntiAliased == Other.bAntiAliased
		&& Range == Other.Range && Jitter == Other.Jitter;
}

bool FDreamStyleInnerGlow::operator==(const FDreamStyleInnerGlow& Other) const
{
	return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Opacity == Other.Opacity && Noise == Other.Noise
		&& Fill == Other.Fill && Color == Other.Color && Gradient == Other.Gradient && Technique == Other.Technique
		&& Source == Other.Source && Choke == Other.Choke && Size == Other.Size && Contour == Other.Contour
		&& bAntiAliased == Other.bAntiAliased && Range == Other.Range && Jitter == Other.Jitter;
}

bool FDreamStyleBevelEmboss::operator==(const FDreamStyleBevelEmboss& Other) const
{
	return bEnabled == Other.bEnabled && Style == Other.Style && Technique == Other.Technique && Depth == Other.Depth
		&& Direction == Other.Direction && Size == Other.Size && Soften == Other.Soften && bUseGlobalLight == Other.bUseGlobalLight
		&& Angle == Other.Angle && Altitude == Other.Altitude && GlossContour == Other.GlossContour
		&& bGlossAntiAliased == Other.bGlossAntiAliased && HighlightMode == Other.HighlightMode && HighlightColor == Other.HighlightColor
		&& HighlightOpacity == Other.HighlightOpacity && ShadowMode == Other.ShadowMode && ShadowColor == Other.ShadowColor
		&& ShadowOpacity == Other.ShadowOpacity && bUseContour == Other.bUseContour && Contour == Other.Contour
		&& bContourAntiAliased == Other.bContourAntiAliased && ContourRange == Other.ContourRange && bUseTexture == Other.bUseTexture
		&& Texture == Other.Texture && TextureDepth == Other.TextureDepth && bTextureInvert == Other.bTextureInvert;
}

FDreamStyleSatin::FDreamStyleSatin()
{
	// An editor's satin starts with a soft S: the default its dialog opens with.
	Contour.Shape = EDreamStyleContourShape::Gaussian;
}

bool FDreamStyleSatin::operator==(const FDreamStyleSatin& Other) const
{
	return bEnabled == Other.bEnabled && BlendMode == Other.BlendMode && Color == Other.Color && Opacity == Other.Opacity
		&& Angle == Other.Angle && Distance == Other.Distance && Size == Other.Size && Contour == Other.Contour
		&& bAntiAliased == Other.bAntiAliased && bInvert == Other.bInvert;
}

bool FDreamStyleStroke::operator==(const FDreamStyleStroke& Other) const
{
	return bEnabled == Other.bEnabled && Size == Other.Size && Position == Other.Position && BlendMode == Other.BlendMode
		&& Opacity == Other.Opacity && Fill == Other.Fill && Color == Other.Color && Gradient == Other.Gradient
		&& Pattern == Other.Pattern && bOverprint == Other.bOverprint;
}

bool FDreamLayerEffects::IsActive() const
{
	if (!bEnabled)
	{
		return false;
	}
	if (FillOpacity < 1.0f)
	{
		return true;
	}
	auto AnyEnabled = [](const auto& InArray)
	{
		return InArray.ContainsByPredicate([](const auto& InEffect) { return InEffect.bEnabled && InEffect.Opacity > 0.0f; });
	};
	return AnyEnabled(DropShadows) || AnyEnabled(InnerShadows) || AnyEnabled(ColorOverlays) || AnyEnabled(GradientOverlays)
		|| Strokes.ContainsByPredicate([](const FDreamStyleStroke& S) { return S.bEnabled && S.Opacity > 0.0f && S.Size > 0.0f; })
		|| (OuterGlow.bEnabled && OuterGlow.Opacity > 0.0f) || (InnerGlow.bEnabled && InnerGlow.Opacity > 0.0f)
		|| (Satin.bEnabled && Satin.Opacity > 0.0f) || (PatternOverlay.bEnabled && PatternOverlay.Opacity > 0.0f && PatternOverlay.Pattern.Pattern != nullptr)
		|| (BevelEmboss.bEnabled && (BevelEmboss.HighlightOpacity > 0.0f || BevelEmboss.ShadowOpacity > 0.0f));
}

float FDreamLayerEffects::GetOuterReach() const
{
	FDreamUILayerStyleRecord Record;
	return FDreamUILayerStyleMath::Pack(*this, Record) ? Record.OuterReach : 0.0f;
}

bool FDreamLayerEffects::UsesPatterns() const
{
	if (!bEnabled)
	{
		return false;
	}
	return (PatternOverlay.bEnabled && PatternOverlay.Pattern.Pattern != nullptr)
		|| Strokes.ContainsByPredicate([](const FDreamStyleStroke& S) { return S.bEnabled && S.Fill == EDreamStyleStrokeFill::Pattern && S.Pattern.Pattern != nullptr; });
}

bool FDreamLayerEffects::HasOuterEffectsReadingBackdrop() const
{
	if (!bEnabled)
	{
		return false;
	}
	return DropShadows.ContainsByPredicate([](const FDreamStyleDropShadow& S) { return S.bEnabled && DreamUIBlend::ReadsBackdrop(S.BlendMode); })
		|| (OuterGlow.bEnabled && DreamUIBlend::ReadsBackdrop(OuterGlow.BlendMode));
}

bool FDreamLayerEffects::operator==(const FDreamLayerEffects& Other) const
{
	return bEnabled == Other.bEnabled && Scale == Other.Scale && FillOpacity == Other.FillOpacity
		&& GlobalLightAngle == Other.GlobalLightAngle && GlobalLightAltitude == Other.GlobalLightAltitude
		&& DropShadows == Other.DropShadows && OuterGlow == Other.OuterGlow && PatternOverlay == Other.PatternOverlay
		&& GradientOverlays == Other.GradientOverlays && ColorOverlays == Other.ColorOverlays && Satin == Other.Satin
		&& InnerGlow == Other.InnerGlow && InnerShadows == Other.InnerShadows && Strokes == Other.Strokes
		&& BevelEmboss == Other.BevelEmboss;
}
