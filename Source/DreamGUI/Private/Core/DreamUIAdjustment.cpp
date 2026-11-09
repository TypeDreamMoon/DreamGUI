// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIAdjustment.h"

#include "Core/DreamUIBlendMath.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/Texture2D.h"

namespace DreamUIAdjustLocal
{
	FVector3f Saturate(const FVector3f& C)
	{
		return FVector3f(FMath::Clamp(C.X, 0.0f, 1.0f), FMath::Clamp(C.Y, 0.0f, 1.0f), FMath::Clamp(C.Z, 0.0f, 1.0f));
	}

	float Saturate(float X)
	{
		return FMath::Clamp(X, 0.0f, 1.0f);
	}

	FVector3f Lerp(const FVector3f& A, const FVector3f& B, float T)
	{
		return A + (B - A) * T;
	}

	float Smoothstep(float A, float B, float X)
	{
		const float T = Saturate((X - A) / (B - A));
		return T * T * (3.0f - 2.0f * T);
	}

	float Frac(float X)
	{
		return X - FMath::FloorToFloat(X);
	}

	float HueToChannel(float P, float Q, float T)
	{
		T = Frac(T);
		if (T < 1.0f / 6.0f)
		{
			return P + (Q - P) * 6.0f * T;
		}
		if (T < 0.5f)
		{
			return Q;
		}
		if (T < 2.0f / 3.0f)
		{
			return P + (Q - P) * (2.0f / 3.0f - T) * 6.0f;
		}
		return P;
	}

	FVector3f Encoded(const FColor& InColor)
	{
		return FVector3f(InColor.R / 255.0f, InColor.G / 255.0f, InColor.B / 255.0f);
	}

	/** The table's row for a value at each of its samples: 0, 1/255, ..., 1. */
	void BakeRow(TArray<FFloat16Color>& InOutTable, int32 InRow, TFunctionRef<FLinearColor(float)> InAt)
	{
		for (int32 Index = 0; Index < DreamUIAdjustLayout::TableWidth; ++Index)
		{
			const float X = static_cast<float>(Index) / (DreamUIAdjustLayout::TableWidth - 1);
			InOutTable[InRow * DreamUIAdjustLayout::TableWidth + Index] = FFloat16Color(InAt(X));
		}
	}

	float Channel(const FVector3f& C, int32 Index)
	{
		return Index == 0 ? C.X : (Index == 1 ? C.Y : C.Z);
	}
}

void FDreamUIAdjustMath::Pack(const TArray<FDreamAdjustment>& InAdjustments, float InOpacity, EDreamUIBlendMode InMode, FDreamUIAdjustPacked& OutPacked)
{
	using namespace DreamUIAdjustLocal;
	OutPacked = FDreamUIAdjustPacked();
	OutPacked.Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	OutPacked.BlendMode = InMode;
	OutPacked.Table.SetNumZeroed(DreamUIAdjustLayout::TableWidth * DreamUIAdjustLayout::MaxOps);
	for (const FDreamAdjustment& Adjustment : InAdjustments)
	{
		if (!Adjustment.bEnabled || Adjustment.Type == EDreamAdjustmentType::None || OutPacked.NumOps >= DreamUIAdjustLayout::MaxOps)
		{
			continue;
		}
		const int32 Op = OutPacked.NumOps++;
		FVector4f* Row = &OutPacked.Rows[Op * DreamUIAdjustLayout::RowsPerOp];
		const float Type = static_cast<float>(Adjustment.Type);
		switch (Adjustment.Type)
		{
		case EDreamAdjustmentType::BrightnessContrast:
			Row[0] = FVector4f(Type, Adjustment.Brightness, Adjustment.Contrast, 0.0f);
			break;
		case EDreamAdjustmentType::Levels:
			Row[0] = FVector4f(Type, Adjustment.InputBlack, Adjustment.InputWhite, Adjustment.Gamma);
			Row[1] = FVector4f(Adjustment.OutputBlack, Adjustment.OutputWhite, 0.0f, 0.0f);
			break;
		case EDreamAdjustmentType::Curves:
		{
			Row[0] = FVector4f(Type, static_cast<float>(Op), 0.0f, 0.0f);
			const UCurveLinearColor* Curve = Adjustment.Curve;
			const UCurveFloat* MasterCurve = Adjustment.MasterCurve;
			auto Eval = [Curve](int32 InCurve, float X)
			{
				if (Curve == nullptr || Curve->FloatCurves[InCurve].GetNumKeys() == 0)
				{
					return X;
				}
				return Curve->FloatCurves[InCurve].Eval(X, X);
			};
			BakeRow(OutPacked.Table, Op, [&Eval, MasterCurve](float X)
			{
				const float Master = MasterCurve != nullptr && MasterCurve->FloatCurve.GetNumKeys() > 0 ? Saturate(MasterCurve->FloatCurve.Eval(X, X)) : X;
				return FLinearColor(Saturate(Eval(0, Master)), Saturate(Eval(1, Master)), Saturate(Eval(2, Master)), 1.0f);
			});
			break;
		}
		case EDreamAdjustmentType::Exposure:
			Row[0] = FVector4f(Type, Adjustment.Exposure, Adjustment.ExposureOffset, Adjustment.ExposureGamma);
			break;
		case EDreamAdjustmentType::HueSaturation:
			Row[0] = FVector4f(Type, Adjustment.Hue, Adjustment.Saturation, Adjustment.Lightness);
			Row[1] = FVector4f(Adjustment.bColorize ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
			break;
		case EDreamAdjustmentType::Vibrance:
			Row[0] = FVector4f(Type, Adjustment.Vibrance, Adjustment.VibranceSaturation, 0.0f);
			break;
		case EDreamAdjustmentType::ColorBalance:
			Row[0] = FVector4f(Type, Adjustment.Shadows.X, Adjustment.Shadows.Y, Adjustment.Shadows.Z);
			Row[1] = FVector4f(Adjustment.Midtones.X, Adjustment.Midtones.Y, Adjustment.Midtones.Z, Adjustment.bPreserveLuminosity ? 1.0f : 0.0f);
			Row[2] = FVector4f(Adjustment.Highlights.X, Adjustment.Highlights.Y, Adjustment.Highlights.Z, 0.0f);
			break;
		case EDreamAdjustmentType::BlackAndWhite:
		{
			const FVector3f Tint = Encoded(Adjustment.TintColor);
			Row[0] = FVector4f(Type, Adjustment.Reds, Adjustment.Yellows, Adjustment.Greens);
			Row[1] = FVector4f(Adjustment.Cyans, Adjustment.Blues, Adjustment.Magentas, 0.0f);
			Row[2] = FVector4f(Tint.X, Tint.Y, Tint.Z, FMath::Clamp(Adjustment.TintStrength, 0.0f, 1.0f));
			break;
		}
		case EDreamAdjustmentType::PhotoFilter:
		{
			const FVector3f Filter = Encoded(Adjustment.FilterColor);
			Row[0] = FVector4f(Type, Filter.X, Filter.Y, Filter.Z);
			Row[1] = FVector4f(FMath::Clamp(Adjustment.Density, 0.0f, 1.0f), Adjustment.bPreserveLuminosity ? 1.0f : 0.0f, 0.0f, 0.0f);
			break;
		}
		case EDreamAdjustmentType::ChannelMixer:
			Row[0] = FVector4f(Type, Adjustment.RedMix.X, Adjustment.RedMix.Y, Adjustment.RedMix.Z);
			Row[1] = FVector4f(Adjustment.GreenMix.X, Adjustment.GreenMix.Y, Adjustment.GreenMix.Z, 0.0f);
			Row[2] = FVector4f(Adjustment.BlueMix.X, Adjustment.BlueMix.Y, Adjustment.BlueMix.Z, 0.0f);
			Row[3] = FVector4f(Adjustment.MixConstants.X, Adjustment.MixConstants.Y, Adjustment.MixConstants.Z, Adjustment.bMonochrome ? 1.0f : 0.0f);
			break;
		case EDreamAdjustmentType::Invert:
			Row[0] = FVector4f(Type, 0.0f, 0.0f, 0.0f);
			break;
		case EDreamAdjustmentType::Posterize:
			Row[0] = FVector4f(Type, static_cast<float>(FMath::Max(Adjustment.PosterizeLevels, 2)), 0.0f, 0.0f);
			break;
		case EDreamAdjustmentType::Threshold:
			Row[0] = FVector4f(Type, Adjustment.ThresholdLevel, 0.0f, 0.0f);
			break;
		case EDreamAdjustmentType::GradientMap:
		{
			Row[0] = FVector4f(Type, static_cast<float>(Op), Adjustment.bReverse ? 1.0f : 0.0f, 0.0f);
			// Along the stops alone: a left-to-right linear gradient of the same stops, read across its middle.
			FDreamGradient Along = Adjustment.Gradient;
			Along.Type = EDreamPaintType::Linear;
			Along.Angle = 90.0f;
			Along.Scale = 1.0f;
			Along.Offset = 0.0f;
			Along.Spread = EDreamPaintSpread::Pad;
			const bool bPainting = Along.IsPainting();
			BakeRow(OutPacked.Table, Op, [&Along, bPainting](float X)
			{
				if (!bPainting)
				{
					return FLinearColor(X, X, X, 1.0f);
				}
				const FLinearColor Linear = Along.Evaluate(FVector2f(X, 0.5f), 1.0f);
				const FVector3f C = FDreamUIBlendMath::ToSrgb(FVector3f(Linear.R, Linear.G, Linear.B));
				return FLinearColor(C.X, C.Y, C.Z, 1.0f);
			});
			break;
		}
		case EDreamAdjustmentType::LookupTable:
			Row[0] = FVector4f(Type, FMath::Clamp(Adjustment.Intensity, 0.0f, 1.0f), 0.0f, 0.0f);
			if (OutPacked.LookupTable == nullptr)
			{
				OutPacked.LookupTable = Adjustment.LookupTable;
			}
			break;
		default:
			--OutPacked.NumOps;
			break;
		}
	}
}

float FDreamUIAdjustMath::Luma(const FVector3f& C)
{
	return C.X * 0.299f + C.Y * 0.587f + C.Z * 0.114f;
}

FVector3f FDreamUIAdjustMath::RgbToHsl(const FVector3f& C)
{
	const float Max = FMath::Max3(C.X, C.Y, C.Z);
	const float Min = FMath::Min3(C.X, C.Y, C.Z);
	const float L = (Max + Min) * 0.5f;
	const float D = Max - Min;
	if (D <= 1e-6f)
	{
		return FVector3f(0.0f, 0.0f, L);
	}
	const float S = L > 0.5f ? D / (2.0f - Max - Min) : D / (Max + Min);
	float H;
	if (Max == C.X)
	{
		H = (C.Y - C.Z) / D + (C.Y < C.Z ? 6.0f : 0.0f);
	}
	else if (Max == C.Y)
	{
		H = (C.Z - C.X) / D + 2.0f;
	}
	else
	{
		H = (C.X - C.Y) / D + 4.0f;
	}
	return FVector3f(H / 6.0f, S, L);
}

FVector3f FDreamUIAdjustMath::HslToRgb(const FVector3f& Hsl)
{
	using namespace DreamUIAdjustLocal;
	if (Hsl.Y <= 1e-6f)
	{
		return FVector3f(Hsl.Z);
	}
	const float Q = Hsl.Z < 0.5f ? Hsl.Z * (1.0f + Hsl.Y) : Hsl.Z + Hsl.Y - Hsl.Z * Hsl.Y;
	const float P = 2.0f * Hsl.Z - Q;
	return FVector3f(HueToChannel(P, Q, Hsl.X + 1.0f / 3.0f), HueToChannel(P, Q, Hsl.X), HueToChannel(P, Q, Hsl.X - 1.0f / 3.0f));
}

FVector4f FDreamUIAdjustMath::Table(const FDreamUIAdjustPacked& InPacked, int32 InRow, float X)
{
	const float Texel = FMath::Clamp(X, 0.0f, 1.0f) * (DreamUIAdjustLayout::TableWidth - 1);
	const int32 I0 = FMath::Clamp(FMath::FloorToInt32(Texel), 0, DreamUIAdjustLayout::TableWidth - 1);
	const int32 I1 = FMath::Min(I0 + 1, DreamUIAdjustLayout::TableWidth - 1);
	const float F = Texel - I0;
	auto At = [&InPacked, InRow](int32 InIndex)
	{
		const int32 Index = InRow * DreamUIAdjustLayout::TableWidth + InIndex;
		const FLinearColor C = InPacked.Table.IsValidIndex(Index) ? InPacked.Table[Index].GetFloats() : FLinearColor::Transparent;
		return FVector4f(C.R, C.G, C.B, C.A);
	};
	return At(I0) + (At(I1) - At(I0)) * F;
}

FVector3f FDreamUIAdjustMath::LookUp(const TArray<FColor>& InTexels, const FVector3f& InC)
{
	using namespace DreamUIAdjustLocal;
	const FVector3f C = Saturate(InC);
	auto Texel = [&InTexels](int32 X, int32 Y)
	{
		const int32 Index = FMath::Clamp(Y, 0, 15) * 256 + FMath::Clamp(X, 0, 255);
		return InTexels.IsValidIndex(Index) ? Encoded(InTexels[Index]) : FVector3f::ZeroVector;
	};
	// Bilinear within a slice, as the sampler reads it at the texel the shader asks for.
	auto Slice = [&Texel, &C](float InSlice)
	{
		const float U = C.X * 15.0f + InSlice * 16.0f;
		const float V = C.Y * 15.0f;
		const int32 X0 = FMath::FloorToInt32(U);
		const int32 Y0 = FMath::FloorToInt32(V);
		const float Fx = U - X0;
		const float Fy = V - Y0;
		const FVector3f Top = Lerp(Texel(X0, Y0), Texel(X0 + 1, Y0), Fx);
		const FVector3f Bottom = Lerp(Texel(X0, Y0 + 1), Texel(X0 + 1, Y0 + 1), Fx);
		return Lerp(Top, Bottom, Fy);
	};
	const float SliceIndex = C.Z * 15.0f;
	const float S0 = FMath::FloorToFloat(SliceIndex);
	const float S1 = FMath::Min(S0 + 1.0f, 15.0f);
	return Lerp(Slice(S0), Slice(S1), SliceIndex - S0);
}

FVector3f FDreamUIAdjustMath::ApplyOp(const FDreamUIAdjustPacked& InPacked, int32 InOp, const FVector3f& C, const TArray<FColor>* InLookupTable)
{
	using namespace DreamUIAdjustLocal;
	if (InOp < 0 || InOp >= DreamUIAdjustLayout::MaxOps)
	{
		return C;
	}
	const FVector4f& P0 = InPacked.Rows[InOp * DreamUIAdjustLayout::RowsPerOp + 0];
	const FVector4f& P1 = InPacked.Rows[InOp * DreamUIAdjustLayout::RowsPerOp + 1];
	const FVector4f& P2 = InPacked.Rows[InOp * DreamUIAdjustLayout::RowsPerOp + 2];
	const FVector4f& P3 = InPacked.Rows[InOp * DreamUIAdjustLayout::RowsPerOp + 3];
	switch (static_cast<EDreamAdjustmentType>(FMath::RoundToInt32(P0.X)))
	{
	case EDreamAdjustmentType::BrightnessContrast:
	{
		const float K = FMath::Tan((FMath::Clamp(P0.Z, -1.0f, 0.99f) + 1.0f) * 0.78539816f);
		return Saturate((C + FVector3f(P0.Y) - FVector3f(0.5f)) * K + FVector3f(0.5f));
	}
	case EDreamAdjustmentType::Levels:
	{
		const float Range = FMath::Max(P0.Z - P0.Y, 1e-4f);
		const float Exponent = 1.0f / FMath::Max(P0.W, 1e-4f);
		auto One = [&](float X) { return FMath::Lerp(P1.X, P1.Y, FMath::Pow(Saturate((X - P0.Y) / Range), Exponent)); };
		return FVector3f(One(C.X), One(C.Y), One(C.Z));
	}
	case EDreamAdjustmentType::Curves:
	{
		const int32 Row = FMath::RoundToInt32(P0.Y);
		return FVector3f(Table(InPacked, Row, C.X).X, Table(InPacked, Row, C.Y).Y, Table(InPacked, Row, C.Z).Z);
	}
	case EDreamAdjustmentType::Exposure:
	{
		const FVector3f Linear = FDreamUIBlendMath::ToLinear(C);
		const float Scale = FMath::Exp2(P0.Y);
		const float Exponent = 1.0f / FMath::Max(P0.W, 1e-4f);
		auto One = [&](float X) { return FMath::Pow(FMath::Max(X * Scale + P0.Z, 0.0f), Exponent); };
		return FDreamUIBlendMath::ToSrgb(FVector3f(One(Linear.X), One(Linear.Y), One(Linear.Z)));
	}
	case EDreamAdjustmentType::HueSaturation:
	{
		FVector3f Hsl = RgbToHsl(C);
		if (P1.X > 0.5f)
		{
			Hsl.X = Frac(P0.Y / 360.0f);
			Hsl.Y = Saturate(P0.Z);
		}
		else
		{
			Hsl.X = Frac(Hsl.X + P0.Y / 360.0f);
			Hsl.Y = Saturate(Hsl.Y * (1.0f + P0.Z));
		}
		FVector3f Out = HslToRgb(Hsl);
		Out = P0.W >= 0.0f ? Out + (FVector3f(1.0f) - Out) * P0.W : Out * (1.0f + P0.W);
		return Saturate(Out);
	}
	case EDreamAdjustmentType::Vibrance:
	{
		const float Saturation = FMath::Max3(C.X, C.Y, C.Z) - FMath::Min3(C.X, C.Y, C.Z);
		const float L0 = Luma(C);
		FVector3f Out = FVector3f(L0) + (C - FVector3f(L0)) * (1.0f + P0.Y * (1.0f - Saturation));
		const float L1 = Luma(Out);
		Out = FVector3f(L1) + (Out - FVector3f(L1)) * (1.0f + P0.Z);
		return Saturate(Out);
	}
	case EDreamAdjustmentType::ColorBalance:
	{
		const float L = FDreamUIBlendMath::Lum(C);
		const float Shadows = 1.0f - Smoothstep(0.0f, 0.5f, L);
		const float Highlights = Smoothstep(0.5f, 1.0f, L);
		const float Midtones = 1.0f - Shadows - Highlights;
		FVector3f Out = C + (FVector3f(P0.Y, P0.Z, P0.W) * Shadows + FVector3f(P1.X, P1.Y, P1.Z) * Midtones + FVector3f(P2.X, P2.Y, P2.Z) * Highlights) * 0.5f;
		if (P1.W > 0.5f)
		{
			Out = FDreamUIBlendMath::SetLum(Out, L);
		}
		return Saturate(Out);
	}
	case EDreamAdjustmentType::BlackAndWhite:
	{
		const int32 Max = C.X >= C.Y && C.X >= C.Z ? 0 : (C.Y >= C.Z ? 1 : 2);
		const int32 Other0 = Max == 0 ? 1 : 0;
		const int32 Other1 = Max == 2 ? 1 : 2;
		const int32 Mid = Channel(C, Other0) > Channel(C, Other1) ? Other0 : Other1;
		const int32 Min = Mid == Other0 ? Other1 : Other0;
		const float Primary = Max == 0 ? P0.Y : (Max == 1 ? P0.W : P1.Y);
		const int32 Pair = Max + Mid;
		const float PairWeight = Pair == 1 ? P0.Z : (Pair == 3 ? P1.X : P1.Z);
		const float Gray = Channel(C, Min) + (Channel(C, Mid) - Channel(C, Min)) * PairWeight + (Channel(C, Max) - Channel(C, Mid)) * Primary;
		const FVector3f Out(Saturate(Gray));
		return Saturate(Lerp(Out, FDreamUIBlendMath::SetLum(FVector3f(P2.X, P2.Y, P2.Z), Saturate(Gray)), P2.W));
	}
	case EDreamAdjustmentType::PhotoFilter:
	{
		const float L = FDreamUIBlendMath::Lum(C);
		FVector3f Out = Lerp(C, C * FVector3f(P0.Y, P0.Z, P0.W), P1.X);
		if (P1.Y > 0.5f)
		{
			Out = FDreamUIBlendMath::SetLum(Out, L);
		}
		return Saturate(Out);
	}
	case EDreamAdjustmentType::ChannelMixer:
	{
		const float Red = FVector3f::DotProduct(FVector3f(P0.Y, P0.Z, P0.W), C) + P3.X;
		if (P3.W > 0.5f)
		{
			return FVector3f(Saturate(Red));
		}
		return Saturate(FVector3f(Red, FVector3f::DotProduct(FVector3f(P1.X, P1.Y, P1.Z), C) + P3.Y, FVector3f::DotProduct(FVector3f(P2.X, P2.Y, P2.Z), C) + P3.Z));
	}
	case EDreamAdjustmentType::Invert:
		return FVector3f(1.0f) - C;
	case EDreamAdjustmentType::Posterize:
	{
		const float Levels = FMath::Max(2.0f, FMath::RoundToFloat(P0.Y));
		auto One = [Levels](float X) { return FMath::Min(FMath::FloorToFloat(X * Levels), Levels - 1.0f) / (Levels - 1.0f); };
		return FVector3f(One(C.X), One(C.Y), One(C.Z));
	}
	case EDreamAdjustmentType::Threshold:
		return FVector3f(FDreamUIBlendMath::Lum(C) >= P0.Y ? 1.0f : 0.0f);
	case EDreamAdjustmentType::GradientMap:
	{
		const float T = FDreamUIBlendMath::Lum(C);
		const FVector4f Out = Table(InPacked, FMath::RoundToInt32(P0.Y), P0.Z > 0.5f ? 1.0f - T : T);
		return FVector3f(Out.X, Out.Y, Out.Z);
	}
	case EDreamAdjustmentType::LookupTable:
		return InLookupTable != nullptr ? Lerp(C, LookUp(*InLookupTable, C), Saturate(P0.Y)) : C;
	default:
		return C;
	}
}

FVector4f FDreamUIAdjustMath::ApplyToPixel(const FDreamUIAdjustPacked& InPacked, const FVector4f& InPremultiplied, bool bInLinear, bool bInOpaque, FIntPoint InTargetPixel,
	const TArray<FColor>* InLookupTable)
{
	using namespace DreamUIAdjustLocal;
	const float Alpha = bInOpaque ? 1.0f : Saturate(InPremultiplied.W);
	FVector3f C = bInOpaque ? FVector3f(InPremultiplied.X, InPremultiplied.Y, InPremultiplied.Z)
		: (InPremultiplied.W > 1e-5f ? FVector3f(InPremultiplied.X, InPremultiplied.Y, InPremultiplied.Z) / InPremultiplied.W : FVector3f::ZeroVector);
	C = Saturate(C);
	if (bInLinear)
	{
		C = FDreamUIBlendMath::ToSrgb(C);
	}
	const FVector3f In = C;
	for (int32 Op = 0; Op < InPacked.NumOps; ++Op)
	{
		C = ApplyOp(InPacked, Op, C, InLookupTable);
	}
	float Opacity = Saturate(InPacked.Opacity);
	if (InPacked.BlendMode == EDreamUIBlendMode::Dissolve)
	{
		Opacity = FDreamUIBlendMath::DissolveAlpha(Opacity, InTargetPixel.X, InTargetPixel.Y);
	}
	const bool bNormal = InPacked.BlendMode == EDreamUIBlendMode::Alpha || InPacked.BlendMode == EDreamUIBlendMode::Dissolve;
	const FVector3f Blended = bNormal ? C : FDreamUIBlendMath::Function(InPacked.BlendMode, In, C);
	FVector3f Out = Lerp(In, Blended, Opacity);
	Out = bInLinear ? FDreamUIBlendMath::ToLinear(Out) : Saturate(Out);
	return bInOpaque ? FVector4f(Out.X, Out.Y, Out.Z, InPremultiplied.W) : FVector4f(Out.X * Alpha, Out.Y * Alpha, Out.Z * Alpha, Alpha);
}
