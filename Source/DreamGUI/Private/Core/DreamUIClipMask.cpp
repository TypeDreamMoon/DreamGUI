// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIClipMask.h"

#include "Engine/Texture.h"

namespace DreamUIClipMaskLocal
{
	FORCEINLINE float Saturate(float V) { return FMath::Clamp(V, 0.0f, 1.0f); }
	FORCEINLINE float Frac(float V) { return V - FMath::FloorToFloat(V); }
	FORCEINLINE float Length(FVector2f V) { return FMath::Sqrt(V.X * V.X + V.Y * V.Y); }
	FORCEINLINE float Dot(FVector2f A, FVector2f B) { return A.X * B.X + A.Y * B.Y; }
	FORCEINLINE float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	/** HLSL's round: halves away from zero. */
	FORCEINLINE float Round(float V) { return V >= 0.0f ? FMath::FloorToFloat(V + 0.5f) : -FMath::FloorToFloat(-V + 0.5f); }
	FORCEINLINE uint32 Bits(uint32 Flags, int32 Shift, uint32 Mask) { return (Flags >> Shift) & Mask; }

	float CellValue(int32 X, int32 Y, uint32 Seed)
	{
		return static_cast<float>(FDreamUIClipMaskMath::Hash(static_cast<uint32>(X), static_cast<uint32>(Y), Seed) & 0xFFFFFFu) / 16777215.0f;
	}

	/** Width of the extent of a HalfSize box along the direction Angle (radians) makes with +x. */
	float ExtentAlong(FVector2f HalfSize, float Angle)
	{
		return 2.0f * (HalfSize.X * FMath::Abs(FMath::Cos(Angle)) + HalfSize.Y * FMath::Abs(FMath::Sin(Angle)));
	}

	float SafeInverse(double V)
	{
		const double Magnitude = FMath::Max(FMath::Abs(V), 0.0001);
		return static_cast<float>((V < 0.0 ? -1.0 : 1.0) / Magnitude);
	}

	float TextureValue(uint32 Flags, FVector2f UV, const FLinearColor& Texel)
	{
		float Value = Texel.A;
		switch (Bits(Flags, DreamUIClipLayout::ChannelShift, 7u))
		{
		case 1: Value = Texel.R; break;
		case 2: Value = Texel.G; break;
		case 3: Value = Texel.B; break;
		case 4: Value = Texel.R * 0.2126f + Texel.G * 0.7152f + Texel.B * 0.0722f; break;
		default: break;
		}
		if (Bits(Flags, DreamUIClipLayout::AddressShift, 3u) == DreamUIClipLayout::AddressBounded
			&& (UV.X < 0.0f || UV.Y < 0.0f || UV.X > 1.0f || UV.Y > 1.0f))
		{
			Value = 0.0f;
		}
		return Value;
	}
}

bool FDreamClipMask::IsActive() const
{
	return Shape != EDreamClipMaskShape::Rect
		|| Feather.Left > 0.0f || Feather.Right > 0.0f || Feather.Top > 0.0f || Feather.Bottom > 0.0f
		|| Source == EDreamClipMaskSource::Procedural
		|| (Source == EDreamClipMaskSource::Texture && Texture != nullptr)
		|| bInvert;
}

UTexture* FDreamClipMask::GetActiveTexture() const
{
	return Source == EDreamClipMaskSource::Texture ? Texture.Get() : nullptr;
}

bool FDreamClipMask::operator==(const FDreamClipMask& Other) const
{
	return Shape == Other.Shape && Sides == Other.Sides && Roundness == Other.Roundness && InnerRatio == Other.InnerRatio
		&& Feather == Other.Feather && Source == Other.Source && Texture == Other.Texture && Channel == Other.Channel
		&& Fill == Other.Fill && Pattern == Other.Pattern && Frequency == Other.Frequency && Seed == Other.Seed
		&& Rotation == Other.Rotation && Scale == Other.Scale && Offset == Other.Offset && Threshold == Other.Threshold
		&& Softness == Other.Softness && bInvert == Other.bInvert && HitTest == Other.HitTest;
}

float FDreamUIClipMaskMath::SdRoundRect(FVector2f P, FVector2f B, FVector4f R)
{
	const float MinB = FMath::Min(B.X, B.Y);
	R = FVector4f(FMath::Min(R.X, MinB), FMath::Min(R.Y, MinB), FMath::Min(R.Z, MinB), FMath::Min(R.W, MinB));
	if (!(P.X > 0.0f))
	{
		R.X = R.W;
		R.Y = R.Z;
	}
	const float Radius = P.Y > 0.0f ? R.X : R.Y;
	const FVector2f Q(FMath::Abs(P.X) - B.X + Radius, FMath::Abs(P.Y) - B.Y + Radius);
	return FMath::Min(FMath::Max(Q.X, Q.Y), 0.0f) + DreamUIClipMaskLocal::Length(FVector2f(FMath::Max(Q.X, 0.0f), FMath::Max(Q.Y, 0.0f))) - Radius;
}

float FDreamUIClipMaskMath::SdEllipse(FVector2f P, FVector2f B)
{
	B = FVector2f(FMath::Max(B.X, 0.0001f), FMath::Max(B.Y, 0.0001f));
	const float K0 = DreamUIClipMaskLocal::Length(FVector2f(P.X / B.X, P.Y / B.Y));
	const float K1 = DreamUIClipMaskLocal::Length(FVector2f(P.X / (B.X * B.X), P.Y / (B.Y * B.Y)));
	return K1 > 0.0f ? K0 * (K0 - 1.0f) / K1 : -FMath::Min(B.X, B.Y);
}

float FDreamUIClipMaskMath::SdStar(FVector2f Q, float Points, float R, float Inner)
{
	using namespace DreamUIClipMaskLocal;
	const float TwoPi = 6.28318530718f;
	const float Sector = TwoPi / FMath::Max(Points, 2.0f);
	const float Theta = FMath::Atan2(Q.X, Q.Y);
	const float Phi = FMath::Abs(Theta - Sector * Round(Theta / Sector));
	const float Rho = Length(Q);
	const FVector2f P(Rho * FMath::Sin(Phi), Rho * FMath::Cos(Phi));
	const FVector2f A(0.0f, R);
	const FVector2f B = FVector2f(FMath::Sin(0.5f * Sector), FMath::Cos(0.5f * Sector)) * (Inner * R);
	const FVector2f AB = B - A;
	const FVector2f AP = P - A;
	const float T = Saturate(Dot(AP, AB) / FMath::Max(Dot(AB, AB), 1e-8f));
	const float Dist = Length(AP - AB * T);
	const float Side = AB.X * AP.Y - AB.Y * AP.X;
	const float OriginSide = AB.X * (-A.Y) - AB.Y * (-A.X);
	return Side * OriginSide >= 0.0f ? -Dist : Dist;
}

float FDreamUIClipMaskMath::ShapeDistance(int32 Shape, FVector2f P, FVector2f B, float ParamA, float ParamB)
{
	using namespace DreamUIClipMaskLocal;
	const float MinHalf = FMath::Max(FMath::Min(B.X, B.Y), 0.0001f);
	if (Shape == static_cast<int32>(EDreamClipMaskShape::Ellipse))
	{
		return SdEllipse(P, B);
	}
	if (Shape == static_cast<int32>(EDreamClipMaskShape::Ring))
	{
		return FMath::Max(SdEllipse(P, B), -SdEllipse(P, B * Saturate(ParamA)));
	}
	const FVector2f Q(P.X / FMath::Max(B.X, 0.0001f), P.Y / FMath::Max(B.Y, 0.0001f));
	if (Shape == static_cast<int32>(EDreamClipMaskShape::Polygon))
	{
		const float Sides = FMath::Max(ParamA, 3.0f);
		const float HalfSector = 3.14159265359f / Sides;
		const float Rounding = FMath::Clamp(ParamB / MinHalf, 0.0f, 1.0f);
		const float R = FMath::Max(1.0f - Rounding / FMath::Cos(HalfSector), 0.0f);
		return (SdStar(Q, Sides, R, FMath::Cos(HalfSector)) - Rounding) * MinHalf;
	}
	return SdStar(Q, FMath::Max(ParamA, 2.0f), 1.0f, Saturate(ParamB)) * MinHalf;
}

uint32 FDreamUIClipMaskMath::Hash(uint32 X, uint32 Y, uint32 Seed)
{
	uint32 H = (X * 0x8DA6B343u) ^ (Y * 0xD8163841u) ^ (Seed * 0xCB1AB31Fu);
	H ^= H >> 16;
	H *= 0x7FEB352Du;
	H ^= H >> 15;
	H *= 0x846CA68Bu;
	H ^= H >> 16;
	return H;
}

float FDreamUIClipMaskMath::Pattern(int32 InPattern, FVector2f UV, float Frequency, uint32 Seed)
{
	using namespace DreamUIClipMaskLocal;
	const FVector2f Q = UV - FVector2f(0.5f, 0.5f);
	switch (static_cast<EDreamClipMaskPattern>(InPattern))
	{
	case EDreamClipMaskPattern::Linear:
		return UV.X;
	case EDreamClipMaskPattern::Radial:
		return Length(Q) * 2.0f;
	case EDreamClipMaskPattern::Angular:
		return Frac(FMath::Atan2(Q.X, -Q.Y) / 6.28318530718f + 1.0f);
	case EDreamClipMaskPattern::Diamond:
		return (FMath::Abs(Q.X) + FMath::Abs(Q.Y)) * 2.0f;
	case EDreamClipMaskPattern::Blinds:
		return Frac(UV.X * Frequency);
	case EDreamClipMaskPattern::Checker:
		return Frac((FMath::FloorToFloat(UV.X * Frequency) + FMath::FloorToFloat(UV.Y * Frequency)) * 0.5f) * 2.0f;
	default:
		break;
	}
	const FVector2f Grid = UV * Frequency;
	const FVector2f Base(FMath::FloorToFloat(Grid.X), FMath::FloorToFloat(Grid.Y));
	FVector2f F = Grid - Base;
	F = FVector2f(F.X * F.X * (3.0f - 2.0f * F.X), F.Y * F.Y * (3.0f - 2.0f * F.Y));
	const int32 CX = static_cast<int32>(Base.X);
	const int32 CY = static_cast<int32>(Base.Y);
	const float V00 = CellValue(CX, CY, Seed);
	const float V10 = CellValue(CX + 1, CY, Seed);
	const float V01 = CellValue(CX, CY + 1, Seed);
	const float V11 = CellValue(CX + 1, CY + 1, Seed);
	return Lerp(Lerp(V00, V10, F.X), Lerp(V01, V11, F.X), F.Y);
}

float FDreamUIClipMaskMath::Remap(float Value, float Threshold, float Softness)
{
	return DreamUIClipMaskLocal::Saturate((Value - Threshold + Softness) / FMath::Max(2.0f * Softness, 0.00001f));
}

float FDreamUIClipMaskMath::SliceAxis(float T, float Size, float Low, float High, const FVector4f& U)
{
	using namespace DreamUIClipMaskLocal;
	if (T < Low)
	{
		return Lerp(U.X, U.Y, T / FMath::Max(Low, 0.00001f));
	}
	if (T > Size - High)
	{
		return Lerp(U.Z, U.W, (T - (Size - High)) / FMath::Max(High, 0.00001f));
	}
	return Lerp(U.Y, U.Z, (T - Low) / FMath::Max(Size - Low - High, 0.00001f));
}

float FDreamUIClipMaskMath::Edge(float Distance, float Width)
{
	return DreamUIClipMaskLocal::Saturate(0.5f - Distance / FMath::Max(Width, 0.000001f));
}

float FDreamUIClipMaskMath::EvaluateMaskedLevel(const FDreamUIClipLevel& Level, FVector2f P, float AAWidth, TFunctionRef<bool(FVector2f UV, FLinearColor& OutTexel)> InSample)
{
	using namespace DreamUIClipMaskLocal;
	const FVector4f& M0 = Level.Mask[0];
	const FVector4f& M1 = Level.Mask[1];
	const FVector4f& M2 = Level.Mask[2];
	const FVector4f& M3 = Level.Mask[3];
	const FVector2f B = Level.HalfSize;
	const uint32 Flags = static_cast<uint32>(M0.W + 0.5f);
	const uint32 Kind = Bits(Flags, DreamUIClipLayout::KindShift, 3u);
	const uint32 Source = Bits(Flags, DreamUIClipLayout::SourceShift, 3u);
	const float RectAlpha = Edge(SdRoundRect(P, B, Level.CornerRadius), AAWidth);
	if (Kind == DreamUIClipLayout::KindUnderlayShape)
	{
		return RectAlpha * M0.X;
	}
	float Alpha = 1.0f;
	FVector2f UV = FVector2f::ZeroVector;
	float Threshold = 0.5f;
	float Softness = 0.5f;
	if (Kind == DreamUIClipLayout::KindUnderlayImage)
	{
		const FVector2f Size = B * 2.0f;
		const FVector2f T = P + B;
		UV = FVector2f(SliceAxis(T.X, Size.X, M1.X, M1.Y, M2), SliceAxis(T.Y, Size.Y, M1.Z, M1.W, M3));
		Alpha = RectAlpha * M0.X;
		if (Bits(Flags, DreamUIClipLayout::NoCentreShift, 1u) != 0u
			&& T.X > M1.X && T.X < Size.X - M1.Y && T.Y > M1.Z && T.Y < Size.Y - M1.W)
		{
			Alpha = 0.0f;
		}
	}
	else
	{
		const int32 Shape = static_cast<int32>(Bits(Flags, DreamUIClipLayout::ShapeShift, 7u));
		if (Shape == static_cast<int32>(EDreamClipMaskShape::Rect))
		{
			Alpha = RectAlpha;
			const FVector4f Inside(P.X + B.X, B.X - P.X, P.Y + B.Y, B.Y - P.Y);
			if (M1.X > 0.0f) Alpha *= Saturate(Inside.X / M1.X);
			if (M1.Y > 0.0f) Alpha *= Saturate(Inside.Y / M1.Y);
			if (M1.Z > 0.0f) Alpha *= Saturate(Inside.Z / M1.Z);
			if (M1.W > 0.0f) Alpha *= Saturate(Inside.W / M1.W);
		}
		else
		{
			const float Distance = ShapeDistance(Shape, P, B, M0.Y, M0.Z);
			Alpha = Edge(Distance, FMath::Max(AAWidth, M1.X));
		}
		const FVector2f Pixel(P.X, -P.Y);
		const float S = FMath::Sin(M2.W);
		const float C = FMath::Cos(M2.W);
		const FVector2f Turned(C * Pixel.X + S * Pixel.Y, -S * Pixel.X + C * Pixel.Y);
		UV = FVector2f(Turned.X * M3.X, Turned.Y * M3.Y) + FVector2f(0.5f, 0.5f) + FVector2f(M3.Z, M3.W);
		if (Source == static_cast<uint32>(EDreamClipMaskSource::Procedural))
		{
			const int32 PatternKind = static_cast<int32>(Bits(Flags, DreamUIClipLayout::PatternShift, 7u));
			Alpha *= Remap(Pattern(PatternKind, UV, M2.Z, static_cast<uint32>(M0.X + 0.5f)), M2.X, M2.Y);
		}
		Threshold = M2.X;
		Softness = M2.Y;
	}
	const bool bInvert = Bits(Flags, DreamUIClipLayout::InvertShift, 1u) != 0u;
	if (Source == static_cast<uint32>(EDreamClipMaskSource::Texture))
	{
		const bool bRepeat = Bits(Flags, DreamUIClipLayout::AddressShift, 3u) == DreamUIClipLayout::AddressRepeat;
		const FVector2f ReadAt = bRepeat ? FVector2f(Frac(UV.X), Frac(UV.Y)) : UV;
		FLinearColor Texel = FLinearColor::White;
		if (!InSample(ReadAt, Texel))
		{
			Texel = FLinearColor::White;
		}
		Alpha *= Remap(TextureValue(Flags, UV, Texel), Threshold, Softness);
	}
	return bInvert ? 1.0f - Alpha : Alpha;
}

float FDreamUIClipMaskMath::EvaluateLevel(const FDreamUIClipLevel& Level, FVector2f P, float AAWidth, TFunctionRef<bool(FVector2f UV, FLinearColor& OutTexel)> InSample)
{
	if (Level.bMasked)
	{
		return EvaluateMaskedLevel(Level, P, AAWidth, InSample);
	}
	return Edge(SdRoundRect(P, Level.HalfSize, Level.CornerRadius), AAWidth);
}

FVector2f FDreamUIClipMaskMath::TextureSize(const UTexture* InTexture)
{
	if (InTexture == nullptr)
	{
		return FVector2f(1.0f, 1.0f);
	}
	return FVector2f(FMath::Max(InTexture->GetSurfaceWidth(), 1.0f), FMath::Max(InTexture->GetSurfaceHeight(), 1.0f));
}

bool FDreamUIClipMaskMath::PackMask(const FDreamClipMask& Mask, FVector2f HalfSize, int32 TextureSlot, FVector4f OutMask[DreamUIClipLayout::MaskTexels])
{
	using namespace DreamUIClipMaskLocal;
	if (!Mask.IsActive())
	{
		return false;
	}
	UTexture* MaskTexture = Mask.GetActiveTexture();
	EDreamClipMaskSource Source = Mask.Source;
	if (Source == EDreamClipMaskSource::Texture && (MaskTexture == nullptr || TextureSlot == INDEX_NONE))
	{
		// No texture to read, or no slot to read it from: the shape stays, the texture is left out.
		Source = EDreamClipMaskSource::None;
	}
	const int32 Address = (Source == EDreamClipMaskSource::Texture && Mask.Fill == EDreamClipMaskFill::Tile)
		? DreamUIClipLayout::AddressRepeat : DreamUIClipLayout::AddressBounded;
	const uint32 Flags = (static_cast<uint32>(DreamUIClipLayout::KindMask) << DreamUIClipLayout::KindShift)
		| (static_cast<uint32>(Mask.Shape) << DreamUIClipLayout::ShapeShift)
		| (static_cast<uint32>(Source) << DreamUIClipLayout::SourceShift)
		| (static_cast<uint32>(Mask.Channel) << DreamUIClipLayout::ChannelShift)
		| (static_cast<uint32>(TextureSlot > 0 ? 1 : 0) << DreamUIClipLayout::SlotShift)
		| (static_cast<uint32>(Mask.Pattern) << DreamUIClipLayout::PatternShift)
		| (static_cast<uint32>(Mask.bInvert ? 1 : 0) << DreamUIClipLayout::InvertShift)
		| (static_cast<uint32>(Address) << DreamUIClipLayout::AddressShift);

	float ParamA = 0.0f;
	float ParamB = 0.0f;
	switch (Mask.Shape)
	{
	case EDreamClipMaskShape::Polygon:
		ParamA = static_cast<float>(FMath::Max(Mask.Sides, 3));
		ParamB = FMath::Max(Mask.Roundness, 0.0f);
		break;
	case EDreamClipMaskShape::Star:
		ParamA = static_cast<float>(FMath::Max(Mask.Sides, 2));
		ParamB = FMath::Clamp(Mask.InnerRatio, 0.0f, 1.0f);
		break;
	case EDreamClipMaskShape::Ring:
		ParamA = FMath::Clamp(Mask.InnerRatio, 0.0f, 1.0f);
		break;
	default:
		break;
	}
	OutMask[0] = FVector4f(static_cast<float>(FMath::Clamp(Mask.Seed, 0, 0xFFFF)), ParamA, ParamB, static_cast<float>(Flags));
	OutMask[1] = Mask.Shape == EDreamClipMaskShape::Rect
		? FVector4f(FMath::Max(Mask.Feather.Left, 0.0f), FMath::Max(Mask.Feather.Right, 0.0f), FMath::Max(Mask.Feather.Bottom, 0.0f), FMath::Max(Mask.Feather.Top, 0.0f))
		: FVector4f(FMath::Max(Mask.Feather.Left, 0.0f), 0.0f, 0.0f, 0.0f);

	const float Radians = FMath::DegreesToRadians(Mask.Rotation);
	OutMask[2] = FVector4f(Mask.Threshold, FMath::Max(Mask.Softness, 0.0f), FMath::Max(Mask.Frequency, 0.0f), Radians);

	// The size one whole of the texture or pattern covers on the clip, in pixels.
	FVector2D Display;
	if (Source == EDreamClipMaskSource::Texture)
	{
		const FVector2f TexSize = TextureSize(MaskTexture);
		const double Width = 2.0 * HalfSize.X;
		const double Height = 2.0 * HalfSize.Y;
		const double TextureAspect = TexSize.X / TexSize.Y;
		const bool bWider = Height > 0.0 && Width / Height > TextureAspect;
		switch (Mask.Fill)
		{
		case EDreamClipMaskFill::Fit:
			Display = bWider ? FVector2D(Height * TextureAspect, Height) : FVector2D(Width, Width / TextureAspect);
			break;
		case EDreamClipMaskFill::Fill:
			Display = bWider ? FVector2D(Width, Width / TextureAspect) : FVector2D(Height * TextureAspect, Height);
			break;
		case EDreamClipMaskFill::Tile:
			Display = FVector2D(TexSize.X, TexSize.Y);
			break;
		default:
			Display = FVector2D(Width, Height);
			break;
		}
	}
	else
	{
		// A pattern spans the clip along its own turned axes, so that a gradient at any angle runs 0 to 1 corner to corner.
		Display = FVector2D(ExtentAlong(HalfSize, Radians), ExtentAlong(HalfSize, Radians + UE_HALF_PI));
	}
	OutMask[3] = FVector4f(SafeInverse(Display.X * Mask.Scale.X), SafeInverse(Display.Y * Mask.Scale.Y), static_cast<float>(Mask.Offset.X), static_cast<float>(Mask.Offset.Y));
	return true;
}

void FDreamUIClipMaskMath::PackUnderlay(const FDreamClipUnderlay& Underlay, int32 TextureSlot, FVector4f OutMask[DreamUIClipLayout::MaskTexels])
{
	const float Opacity = FMath::Clamp(Underlay.Opacity, 0.0f, 1.0f);
	if (Underlay.Kind != FDreamClipUnderlay::EKind::Image)
	{
		OutMask[0] = FVector4f(Opacity, 0.0f, 0.0f, static_cast<float>(DreamUIClipLayout::KindUnderlayShape << DreamUIClipLayout::KindShift));
		OutMask[1] = FVector4f::Zero();
		OutMask[2] = FVector4f::Zero();
		OutMask[3] = FVector4f::Zero();
		return;
	}
	const bool bTexture = TextureSlot != INDEX_NONE && Underlay.Texture.IsValid();
	const uint32 Flags = (static_cast<uint32>(DreamUIClipLayout::KindUnderlayImage) << DreamUIClipLayout::KindShift)
		| (static_cast<uint32>(bTexture ? EDreamClipMaskSource::Texture : EDreamClipMaskSource::None) << DreamUIClipLayout::SourceShift)
		| (static_cast<uint32>(EDreamClipMaskChannel::Alpha) << DreamUIClipLayout::ChannelShift)
		| (static_cast<uint32>(TextureSlot > 0 ? 1 : 0) << DreamUIClipLayout::SlotShift)
		| (static_cast<uint32>(DreamUIClipLayout::AddressRaw) << DreamUIClipLayout::AddressShift)
		| (static_cast<uint32>(Underlay.bFillCentre ? 0 : 1) << DreamUIClipLayout::NoCentreShift);
	OutMask[0] = FVector4f(Opacity, 0.0f, 0.0f, static_cast<float>(Flags));
	OutMask[1] = Underlay.Border;
	OutMask[2] = Underlay.U;
	OutMask[3] = Underlay.V;
}
