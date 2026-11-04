// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"

float FDreamUIBlendMath::ToSrgbChannel(float X)
{
	X = FMath::Clamp(X, 0.0f, 1.0f);
	return X <= 0.0031308f ? 12.92f * X : 1.055f * FMath::Pow(X, 1.0f / 2.4f) - 0.055f;
}

float FDreamUIBlendMath::ToLinearChannel(float X)
{
	X = FMath::Clamp(X, 0.0f, 1.0f);
	return X <= 0.04045f ? X / 12.92f : FMath::Pow((X + 0.055f) / 1.055f, 2.4f);
}

FVector3f FDreamUIBlendMath::ToSrgb(const FVector3f& C)
{
	return FVector3f(ToSrgbChannel(C.X), ToSrgbChannel(C.Y), ToSrgbChannel(C.Z));
}

FVector3f FDreamUIBlendMath::ToLinear(const FVector3f& C)
{
	return FVector3f(ToLinearChannel(C.X), ToLinearChannel(C.Y), ToLinearChannel(C.Z));
}

float FDreamUIBlendMath::Lum(const FVector3f& C)
{
	return C.X * 0.3f + C.Y * 0.59f + C.Z * 0.11f;
}

FVector3f FDreamUIBlendMath::ClipColor(FVector3f C)
{
	const float L = Lum(C);
	const float N = FMath::Min3(C.X, C.Y, C.Z);
	const float X = FMath::Max3(C.X, C.Y, C.Z);
	const FVector3f LV(L, L, L);
	if (N < 0.0f)
	{
		C = LV + (C - LV) * L / FMath::Max(L - N, 1e-6f);
	}
	if (X > 1.0f)
	{
		C = LV + (C - LV) * (1.0f - L) / FMath::Max(X - L, 1e-6f);
	}
	return C;
}

FVector3f FDreamUIBlendMath::SetLum(const FVector3f& C, float L)
{
	const float D = L - Lum(C);
	return ClipColor(C + FVector3f(D, D, D));
}

float FDreamUIBlendMath::Sat(const FVector3f& C)
{
	return FMath::Max3(C.X, C.Y, C.Z) - FMath::Min3(C.X, C.Y, C.Z);
}

FVector3f FDreamUIBlendMath::SetSat(const FVector3f& C, float S)
{
	const float N = FMath::Min3(C.X, C.Y, C.Z);
	const float X = FMath::Max3(C.X, C.Y, C.Z);
	return X > N ? (C - FVector3f(N, N, N)) * S / (X - N) : FVector3f::ZeroVector;
}

namespace DreamUIBlendMathLocal
{
	float Screen(float B, float S) { return B + S - B * S; }
	float ColorDodge(float B, float S)
	{
		if (B <= 0.0f)
		{
			return 0.0f;
		}
		if (S >= 1.0f)
		{
			return 1.0f;
		}
		return FMath::Min(1.0f, B / (1.0f - S));
	}
	float ColorBurn(float B, float S)
	{
		if (B >= 1.0f)
		{
			return 1.0f;
		}
		if (S <= 0.0f)
		{
			return 0.0f;
		}
		return 1.0f - FMath::Min(1.0f, (1.0f - B) / S);
	}
	float HardLight(float B, float S) { return S <= 0.5f ? B * 2.0f * S : Screen(B, 2.0f * S - 1.0f); }
	float SoftLight(float B, float S)
	{
		if (S <= 0.5f)
		{
			return B - (1.0f - 2.0f * S) * B * (1.0f - B);
		}
		const float D = B <= 0.25f ? ((16.0f * B - 12.0f) * B + 4.0f) * B : FMath::Sqrt(B);
		return B + (2.0f * S - 1.0f) * (D - B);
	}
	float VividLight(float B, float S) { return S <= 0.5f ? ColorBurn(B, 2.0f * S) : ColorDodge(B, 2.0f * (S - 0.5f)); }
	float PinLight(float B, float S) { return S <= 0.5f ? FMath::Min(B, 2.0f * S) : FMath::Max(B, 2.0f * S - 1.0f); }
	float Divide(float B, float S)
	{
		if (S <= 0.0f)
		{
			return B > 0.0f ? 1.0f : 0.0f;
		}
		return FMath::Min(1.0f, B / S);
	}
	float Saturate(float X) { return FMath::Clamp(X, 0.0f, 1.0f); }
	FVector3f Saturate(const FVector3f& C) { return FVector3f(Saturate(C.X), Saturate(C.Y), Saturate(C.Z)); }
}

float FDreamUIBlendMath::Separable(EDreamUIBlendMode Mode, float B, float S)
{
	using namespace DreamUIBlendMathLocal;
	switch (Mode)
	{
	case EDreamUIBlendMode::Additive: return FMath::Min(1.0f, B + S);
	case EDreamUIBlendMode::Multiply: return B * S;
	case EDreamUIBlendMode::Darken: return FMath::Min(B, S);
	case EDreamUIBlendMode::ColorBurn: return ColorBurn(B, S);
	case EDreamUIBlendMode::LinearBurn: return FMath::Max(0.0f, B + S - 1.0f);
	case EDreamUIBlendMode::Lighten: return FMath::Max(B, S);
	case EDreamUIBlendMode::Screen: return Screen(B, S);
	case EDreamUIBlendMode::ColorDodge: return ColorDodge(B, S);
	case EDreamUIBlendMode::Overlay: return HardLight(S, B);
	case EDreamUIBlendMode::SoftLight: return SoftLight(B, S);
	case EDreamUIBlendMode::HardLight: return HardLight(B, S);
	case EDreamUIBlendMode::VividLight: return VividLight(B, S);
	case EDreamUIBlendMode::LinearLight: return Saturate(B + 2.0f * S - 1.0f);
	case EDreamUIBlendMode::PinLight: return PinLight(B, S);
	case EDreamUIBlendMode::HardMix: return B + S >= 1.0f ? 1.0f : 0.0f;
	case EDreamUIBlendMode::Difference: return FMath::Abs(B - S);
	case EDreamUIBlendMode::Exclusion: return B + S - 2.0f * B * S;
	case EDreamUIBlendMode::Subtract: return FMath::Max(0.0f, B - S);
	case EDreamUIBlendMode::Divide: return Divide(B, S);
	default: return S;
	}
}

FVector3f FDreamUIBlendMath::Function(EDreamUIBlendMode Mode, const FVector3f& Cb, const FVector3f& Cs)
{
	switch (Mode)
	{
	case EDreamUIBlendMode::DarkerColor:
		return Cs.X + Cs.Y + Cs.Z < Cb.X + Cb.Y + Cb.Z ? Cs : Cb;
	case EDreamUIBlendMode::LighterColor:
		return Cs.X + Cs.Y + Cs.Z > Cb.X + Cb.Y + Cb.Z ? Cs : Cb;
	case EDreamUIBlendMode::Hue:
		return SetLum(SetSat(Cs, Sat(Cb)), Lum(Cb));
	case EDreamUIBlendMode::Saturation:
		return SetLum(SetSat(Cb, Sat(Cs)), Lum(Cb));
	case EDreamUIBlendMode::Color:
		return SetLum(Cs, Lum(Cb));
	case EDreamUIBlendMode::Luminosity:
		return SetLum(Cb, Lum(Cs));
	default:
		return FVector3f(Separable(Mode, Cb.X, Cs.X), Separable(Mode, Cb.Y, Cs.Y), Separable(Mode, Cb.Z, Cs.Z));
	}
}

FVector4f FDreamUIBlendMath::OverBackdrop(EDreamUIBlendMode Mode, const FVector3f& Cs, float Sa, const FVector4f& BackdropPremultiplied, bool bLinearTarget, bool bOpaqueBackdrop)
{
	using namespace DreamUIBlendMathLocal;
	const float Da = bOpaqueBackdrop ? 1.0f : Saturate(BackdropPremultiplied.W);
	const FVector3f BackdropRGB(BackdropPremultiplied.X, BackdropPremultiplied.Y, BackdropPremultiplied.Z);
	const FVector3f Cb = bOpaqueBackdrop ? BackdropRGB : (BackdropPremultiplied.W > 1e-5f ? BackdropRGB / BackdropPremultiplied.W : FVector3f::ZeroVector);
	const FVector3f Blended = bLinearTarget
		? ToLinear(Function(Mode, ToSrgb(Cb), ToSrgb(Cs)))
		: Function(Mode, Saturate(Cb), Saturate(Cs));
	const FVector3f Mixed = Cs * (1.0f - Da) + Blended * Da;
	return FVector4f(Mixed.X * Sa, Mixed.Y * Sa, Mixed.Z * Sa, Sa);
}

FVector4f FDreamUIBlendMath::FixedOutput(EDreamUIBlendMode Mode, const FVector3f& Premultiplied, float Sa)
{
	if (Mode == EDreamUIBlendMode::Multiply)
	{
		return FVector4f(1.0f - Sa + Premultiplied.X, 1.0f - Sa + Premultiplied.Y, 1.0f - Sa + Premultiplied.Z, Sa);
	}
	return FVector4f(Premultiplied.X, Premultiplied.Y, Premultiplied.Z, Sa);
}

FVector4f FDreamUIBlendMath::ApplyFixedBlendState(EDreamUIBlendMode Mode, const FVector4f& Source, const FVector4f& Destination)
{
	using namespace DreamUIBlendMathLocal;
	auto Each = [](const FVector4f& V, TFunctionRef<float(float, float)> InOp, const FVector4f& D)
	{
		return FVector3f(InOp(V.X, D.X), InOp(V.Y, D.Y), InOp(V.Z, D.Z));
	};
	FVector3f RGB;
	float A = Source.W + Destination.W * (1.0f - Source.W);
	switch (Mode)
	{
	case EDreamUIBlendMode::Additive:
		// One, One; the alpha left as it was.
		RGB = Each(Source, [](float S, float D) { return S + D; }, Destination);
		A = Destination.W;
		break;
	case EDreamUIBlendMode::Multiply:
		// DestColor, Zero; the alpha left as it was.
		RGB = Each(Source, [](float S, float D) { return S * D; }, Destination);
		A = Destination.W;
		break;
	case EDreamUIBlendMode::Screen:
		// One, InverseSourceColor.
		RGB = Each(Source, [](float S, float D) { return S + D * (1.0f - S); }, Destination);
		break;
	case EDreamUIBlendMode::Exclusion:
		// InverseDestColor, InverseSourceColor.
		RGB = Each(Source, [](float S, float D) { return S * (1.0f - D) + D * (1.0f - S); }, Destination);
		break;
	default:
		// One, InverseSourceAlpha: Alpha and Dissolve.
		RGB = Each(Source, [&Source](float S, float D) { return S + D * (1.0f - Source.W); }, Destination);
		break;
	}
	return FVector4f(Saturate(RGB.X), Saturate(RGB.Y), Saturate(RGB.Z), Saturate(A));
}

float FDreamUIBlendMath::DissolveAlpha(float Alpha, int32 CellX, int32 CellY)
{
	const float Value = static_cast<float>(FDreamUIClipMaskMath::Hash(static_cast<uint32>(CellX), static_cast<uint32>(CellY), DissolveSeed) & 0xFFFFFFu) / 16777216.0f;
	return Value < Alpha ? 1.0f : 0.0f;
}

FVector4f FDreamUIBlendMath::ReferenceComposite(EDreamUIBlendMode Mode, const FVector3f& Cb, float Ab, const FVector3f& Cs, float Sa)
{
	// W3C Compositing and Blending Level 1, section 5.8 and 9.1.4, with source-over:
	//   Cs' = (1 - ab) * Cs + ab * B(Cb, Cs)
	//   co  = as * Cs' + ab * Cb * (1 - as)
	//   ao  = as + ab * (1 - as)
	const FVector3f B = Function(Mode, Cb, Cs);
	const FVector3f CsBlended = Cs * (1.0f - Ab) + B * Ab;
	const FVector3f Co = CsBlended * Sa + Cb * Ab * (1.0f - Sa);
	return FVector4f(Co.X, Co.Y, Co.Z, Sa + Ab * (1.0f - Sa));
}
