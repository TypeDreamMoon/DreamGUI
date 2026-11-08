// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUILayerStyleMath.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"
#include "Core/Components/DreamRectBlock.h"
#include "Engine/Texture.h"

using namespace DreamUILayerStyleLayout;

namespace DreamUILayerStyleMathLocal
{
	float Smoothstep(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Saturate(float X)
	{
		return FMath::Clamp(X, 0.0f, 1.0f);
	}

	float Component(const FVector4f& V, int32 Index)
	{
		return Index == 0 ? V.X : (Index == 1 ? V.Y : (Index == 2 ? V.Z : V.W));
	}

	FVector3f Rgb(const FColor& InColor)
	{
		return FVector3f(InColor.R, InColor.G, InColor.B) / 255.0f;
	}

	FVector4f UnpackColor(float InBits)
	{
		const uint32 Bits = *reinterpret_cast<const uint32*>(&InBits);
		return FVector4f(static_cast<float>((Bits >> 16) & 0xff), static_cast<float>((Bits >> 8) & 0xff), static_cast<float>(Bits & 0xff),
			static_cast<float>((Bits >> 24) & 0xff)) / 255.0f;
	}

	float PackColor(const FVector4f& InColor)
	{
		const FColor Bytes(
			static_cast<uint8>(FMath::RoundToInt(Saturate(InColor.X) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(Saturate(InColor.Y) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(Saturate(InColor.Z) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt(Saturate(InColor.W) * 255.0f)));
		const uint32 Bits = UDreamRectBlock::PackColorForDataTexture(Bytes);
		return *reinterpret_cast<const float*>(&Bits);
	}

	/** The light's direction, towards it, +Y down, from an angle and an altitude in degrees. */
	FVector3f Light(float InAngle, float InAltitude)
	{
		const float A = FMath::DegreesToRadians(InAngle);
		const float B = FMath::DegreesToRadians(FMath::Clamp(InAltitude, 0.0f, 90.0f));
		return FVector3f(FMath::Cos(A) * FMath::Cos(B), -FMath::Sin(A) * FMath::Cos(B), FMath::Sin(B));
	}

	/** A shadow's offset: away from the light at InAngle, by InDistance, +Y down. */
	FVector2f AwayFrom(float InAngle, float InDistance)
	{
		const float A = FMath::DegreesToRadians(InAngle);
		return FVector2f(-FMath::Cos(A), FMath::Sin(A)) * InDistance;
	}

	struct FPacker
	{
		TArray<FVector4f> Entries;
		TArray<FDreamStyleContour> Contours;
		TArray<FDreamStyleGradient> Gradients;
		FDreamUILayerStyleRecord& Record;
		int32 NumPatterns = 0;
		bool bPatterns = false;

		explicit FPacker(FDreamUILayerStyleRecord& InRecord) : Record(InRecord) {}

		bool HasRoom() const { return Entries.Num() / EntryTexels < MaxEffects; }

		FVector4f* Add(int32 InKind, EDreamUIBlendMode InMode, float InOpacity, int32 InFlags)
		{
			if (!HasRoom())
			{
				return nullptr;
			}
			const int32 First = Entries.Num();
			Entries.AddZeroed(EntryTexels);
			FVector4f* E = &Entries[First];
			E[0] = FVector4f(static_cast<float>(InKind), static_cast<float>(static_cast<uint8>(InMode)), Saturate(InOpacity), static_cast<float>(InFlags));
			E[3].W = -1.0f;
			E[4].X = -1.0f;
			E[5].Z = -1.0f;
			return E;
		}

		float Contour(const FDreamStyleContour& InContour)
		{
			if (InContour.IsLinear())
			{
				return -1.0f;
			}
			int32 Index = Contours.IndexOfByKey(InContour);
			if (Index == INDEX_NONE)
			{
				if (Contours.Num() >= MaxContours)
				{
					return -1.0f;
				}
				Index = Contours.Add(InContour);
			}
			return static_cast<float>(Index);
		}

		float Gradient(const FDreamStyleGradient& InGradient)
		{
			int32 Index = Gradients.IndexOfByKey(InGradient);
			if (Index == INDEX_NONE)
			{
				if (Gradients.Num() >= MaxGradients)
				{
					return -1.0f;
				}
				Index = Gradients.Add(InGradient);
			}
			return static_cast<float>(Index);
		}

		float Pattern(const UTexture* InTexture)
		{
			if (InTexture == nullptr)
			{
				return -1.0f;
			}
			for (int32 Slot = 0; Slot < NumPatterns; ++Slot)
			{
				if (Record.Patterns[Slot] == InTexture)
				{
					bPatterns = true;
					return static_cast<float>(Slot);
				}
			}
			if (NumPatterns >= MaxPatterns)
			{
				return -1.0f;
			}
			Record.Patterns[NumPatterns] = InTexture;
			bPatterns = true;
			return static_cast<float>(NumPatterns++);
		}

		/** A pattern's tile, in units across x and y, from its texture's size and its scale. */
		static FVector2f Tile(const FDreamStylePatternFill& InFill, float InScale)
		{
			const FVector2f Size = FDreamUIClipMaskMath::TextureSize(InFill.Pattern);
			return FVector2f(FMath::Max(Size.X, 1.0f), FMath::Max(Size.Y, 1.0f)) * FMath::Max(InFill.Scale * InScale, 0.01f);
		}

		void Fill(FVector4f* E, const FDreamStyleGradientFill& InFill)
		{
			E[4] = FVector4f(Gradient(InFill.Gradient), static_cast<float>(static_cast<uint8>(InFill.Type)),
				FMath::DegreesToRadians(InFill.Angle), FMath::Max(InFill.Scale, 0.1f));
			E[5].X = InFill.Offset.X;
			E[5].Y = InFill.Offset.Y;
		}
	};
}

float FDreamUILayerStyleMath::ContourShape(EDreamStyleContourShape InShape, float X)
{
	X = FMath::Clamp(X, 0.0f, 1.0f);
	switch (InShape)
	{
	case EDreamStyleContourShape::Cone: return 1.0f - FMath::Abs(2.0f * X - 1.0f);
	case EDreamStyleContourShape::ConeInverted: return FMath::Abs(2.0f * X - 1.0f);
	case EDreamStyleContourShape::CoveDeep: return X * X * X;
	case EDreamStyleContourShape::CoveShallow: return X * X;
	case EDreamStyleContourShape::Gaussian: return 0.5f - 0.5f * FMath::Cos(UE_PI * X);
	case EDreamStyleContourShape::HalfRound: return FMath::Sqrt(FMath::Max(1.0f - (1.0f - X) * (1.0f - X), 0.0f));
	case EDreamStyleContourShape::Ring: return FMath::Sin(UE_PI * X);
	case EDreamStyleContourShape::RingDouble: return FMath::Abs(FMath::Sin(2.0f * UE_PI * X));
	case EDreamStyleContourShape::RollingSlope: return FMath::Clamp(X + 0.1f * FMath::Sin(4.0f * UE_PI * X), 0.0f, 1.0f);
	case EDreamStyleContourShape::RoundedSteps:
	{
		const float S = X * 4.0f;
		const float Step = FMath::Floor(S);
		return FMath::Clamp((Step + DreamUILayerStyleMathLocal::Smoothstep(0.7f, 1.0f, S - Step)) / 4.0f, 0.0f, 1.0f);
	}
	case EDreamStyleContourShape::Sawtooth: return X >= 1.0f ? 1.0f : FMath::Frac(3.0f * X);
	default: return X;
	}
}

bool FDreamUILayerStyleMath::Pack(const FDreamLayerEffects& InStyle, FDreamUILayerStyleRecord& OutRecord, const FDreamUILayerStyleLight* InLight)
{
	using namespace DreamUILayerStyleMathLocal;
	OutRecord = FDreamUILayerStyleRecord();
	if (!InStyle.IsActive())
	{
		return false;
	}
	const float Scale = FMath::Max(InStyle.Scale, 0.01f);
	auto Angle = [&InStyle](bool bGlobal, float InAngle) { return bGlobal ? InStyle.GlobalLightAngle : InAngle; };
	FPacker Packer(OutRecord);
	float Reach = 0.0f;

	// What lies behind the layer, each given a slot: a run of Normal ones shares one, anything else has its own while there
	// are slots, and the last slot takes what is left.
	EDreamUIBlendMode SlotModes[MaxOuterSlots] = { EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Alpha, EDreamUIBlendMode::Alpha };
	int32 NumSlots = 0;
	auto SlotFor = [&SlotModes, &NumSlots](EDreamUIBlendMode InMode) -> float
	{
		if (NumSlots == 0 || ((InMode != EDreamUIBlendMode::Alpha || SlotModes[NumSlots - 1] != EDreamUIBlendMode::Alpha) && NumSlots < MaxOuterSlots))
		{
			SlotModes[NumSlots] = InMode;
			return static_cast<float>(NumSlots++);
		}
		return static_cast<float>(NumSlots - 1);
	};

	for (const FDreamStyleDropShadow& Shadow : InStyle.DropShadows)
	{
		if (!Shadow.bEnabled || Shadow.Opacity <= 0.0f)
		{
			continue;
		}
		FVector4f* E = Packer.Add(KindDropShadow, Shadow.BlendMode, Shadow.Opacity, Shadow.bLayerKnocksOut ? EntryFlagKnockOut : 0);
		if (E == nullptr)
		{
			break;
		}
		const FVector2f Offset = AwayFrom(Angle(Shadow.bUseGlobalLight, Shadow.Angle), Shadow.Distance * Scale);
		const FVector3f Color = Rgb(Shadow.Color);
		E[1] = FVector4f(Color.X, Color.Y, Color.Z, SlotFor(Shadow.BlendMode));
		E[2] = FVector4f(Shadow.Size * Scale, Saturate(Shadow.Spread), Offset.X, Offset.Y);
		E[3] = FVector4f(Saturate(Shadow.Noise), 1.0f, 0.0f, Packer.Contour(Shadow.Contour));
		Reach = FMath::Max(Reach, Shadow.Distance * Scale + Shadow.Size * Scale);
	}
	const FDreamStyleOuterGlow& OuterGlow = InStyle.OuterGlow;
	if (OuterGlow.bEnabled && OuterGlow.Opacity > 0.0f)
	{
		const bool bGradient = OuterGlow.Fill == EDreamStyleGlowFill::Gradient;
		if (FVector4f* E = Packer.Add(KindOuterGlow, OuterGlow.BlendMode, OuterGlow.Opacity,
			(OuterGlow.Technique == EDreamStyleTechnique::Precise ? EntryFlagPrecise : 0) | (bGradient ? EntryFlagGradientFill : 0)))
		{
			const FVector3f Color = Rgb(OuterGlow.Color);
			E[1] = FVector4f(Color.X, Color.Y, Color.Z, SlotFor(OuterGlow.BlendMode));
			E[2] = FVector4f(OuterGlow.Size * Scale, Saturate(OuterGlow.Spread), 0.0f, 0.0f);
			E[3] = FVector4f(Saturate(OuterGlow.Noise), FMath::Clamp(OuterGlow.Range, 0.01f, 1.0f), Saturate(OuterGlow.Jitter), Packer.Contour(OuterGlow.Contour));
			if (bGradient)
			{
				E[4].X = Packer.Gradient(OuterGlow.Gradient);
			}
			Reach = FMath::Max(Reach, OuterGlow.Size * Scale);
		}
	}
	const FDreamStylePatternOverlay& PatternOverlay = InStyle.PatternOverlay;
	if (PatternOverlay.bEnabled && PatternOverlay.Opacity > 0.0f && PatternOverlay.Pattern.Pattern != nullptr)
	{
		if (FVector4f* E = Packer.Add(KindPatternOverlay, PatternOverlay.BlendMode, PatternOverlay.Opacity, 0))
		{
			const FVector2f Tile = FPacker::Tile(PatternOverlay.Pattern, Scale);
			E[2] = FVector4f(Tile.X, Tile.Y, PatternOverlay.Pattern.Offset.X * Scale, PatternOverlay.Pattern.Offset.Y * Scale);
			E[5].Z = Packer.Pattern(PatternOverlay.Pattern.Pattern);
		}
	}
	for (const FDreamStyleGradientOverlay& Overlay : InStyle.GradientOverlays)
	{
		if (!Overlay.bEnabled || Overlay.Opacity <= 0.0f)
		{
			continue;
		}
		FVector4f* E = Packer.Add(KindGradientOverlay, Overlay.BlendMode, Overlay.Opacity,
			(Overlay.Fill.bReverse ? EntryFlagReverse : 0) | (Overlay.Fill.bDither ? EntryFlagDither : 0));
		if (E == nullptr)
		{
			break;
		}
		Packer.Fill(E, Overlay.Fill);
	}
	for (const FDreamStyleColorOverlay& Overlay : InStyle.ColorOverlays)
	{
		if (!Overlay.bEnabled || Overlay.Opacity <= 0.0f)
		{
			continue;
		}
		FVector4f* E = Packer.Add(KindColorOverlay, Overlay.BlendMode, Overlay.Opacity, 0);
		if (E == nullptr)
		{
			break;
		}
		const FVector3f Color = Rgb(Overlay.Color);
		E[1] = FVector4f(Color.X, Color.Y, Color.Z, 0.0f);
	}
	const FDreamStyleSatin& Satin = InStyle.Satin;
	if (Satin.bEnabled && Satin.Opacity > 0.0f)
	{
		if (FVector4f* E = Packer.Add(KindSatin, Satin.BlendMode, Satin.Opacity, Satin.bInvert ? EntryFlagInvert : 0))
		{
			const float A = FMath::DegreesToRadians(Satin.Angle);
			const FVector2f Shift = FVector2f(FMath::Cos(A), -FMath::Sin(A)) * Satin.Distance * Scale;
			const FVector3f Color = Rgb(Satin.Color);
			E[1] = FVector4f(Color.X, Color.Y, Color.Z, 0.0f);
			E[2] = FVector4f(Satin.Size * Scale, 0.0f, Shift.X, Shift.Y);
			E[3].W = Packer.Contour(Satin.Contour);
		}
	}
	const FDreamStyleInnerGlow& InnerGlow = InStyle.InnerGlow;
	if (InnerGlow.bEnabled && InnerGlow.Opacity > 0.0f)
	{
		const bool bGradient = InnerGlow.Fill == EDreamStyleGlowFill::Gradient;
		if (FVector4f* E = Packer.Add(KindInnerGlow, InnerGlow.BlendMode, InnerGlow.Opacity,
			(InnerGlow.Technique == EDreamStyleTechnique::Precise ? EntryFlagPrecise : 0) | (bGradient ? EntryFlagGradientFill : 0)
			| (InnerGlow.Source == EDreamStyleGlowSource::Center ? EntryFlagFromCenter : 0)))
		{
			const FVector3f Color = Rgb(InnerGlow.Color);
			E[1] = FVector4f(Color.X, Color.Y, Color.Z, 0.0f);
			E[2] = FVector4f(InnerGlow.Size * Scale, Saturate(InnerGlow.Choke), 0.0f, 0.0f);
			E[3] = FVector4f(Saturate(InnerGlow.Noise), FMath::Clamp(InnerGlow.Range, 0.01f, 1.0f), Saturate(InnerGlow.Jitter), Packer.Contour(InnerGlow.Contour));
			if (bGradient)
			{
				E[4].X = Packer.Gradient(InnerGlow.Gradient);
			}
		}
	}
	for (const FDreamStyleInnerShadow& Shadow : InStyle.InnerShadows)
	{
		if (!Shadow.bEnabled || Shadow.Opacity <= 0.0f)
		{
			continue;
		}
		FVector4f* E = Packer.Add(KindInnerShadow, Shadow.BlendMode, Shadow.Opacity, 0);
		if (E == nullptr)
		{
			break;
		}
		const FVector2f Offset = AwayFrom(Angle(Shadow.bUseGlobalLight, Shadow.Angle), Shadow.Distance * Scale);
		const FVector3f Color = Rgb(Shadow.Color);
		E[1] = FVector4f(Color.X, Color.Y, Color.Z, 0.0f);
		E[2] = FVector4f(Shadow.Size * Scale, Saturate(Shadow.Choke), Offset.X, Offset.Y);
		E[3] = FVector4f(Saturate(Shadow.Noise), 1.0f, 0.0f, Packer.Contour(Shadow.Contour));
	}
	const FDreamStyleStroke* FirstStroke = nullptr;
	for (const FDreamStyleStroke& Stroke : InStyle.Strokes)
	{
		if (!Stroke.bEnabled || Stroke.Opacity <= 0.0f || Stroke.Size <= 0.0f)
		{
			continue;
		}
		const bool bGradient = Stroke.Fill == EDreamStyleStrokeFill::Gradient;
		const bool bPattern = Stroke.Fill == EDreamStyleStrokeFill::Pattern && Stroke.Pattern.Pattern != nullptr;
		FVector4f* E = Packer.Add(KindStroke, Stroke.BlendMode, Stroke.Opacity, (bGradient ? EntryFlagGradientFill : 0) | (bPattern ? EntryFlagPatternFill : 0)
			| (bGradient && Stroke.Gradient.bReverse ? EntryFlagReverse : 0) | (bGradient && Stroke.Gradient.bDither ? EntryFlagDither : 0));
		if (E == nullptr)
		{
			break;
		}
		FirstStroke = FirstStroke != nullptr ? FirstStroke : &Stroke;
		const FVector3f Color = Rgb(Stroke.Color);
		E[1] = FVector4f(Color.X, Color.Y, Color.Z, 0.0f);
		E[2] = FVector4f(Stroke.Size * Scale, static_cast<float>(static_cast<uint8>(Stroke.Position)), 0.0f, 0.0f);
		if (bGradient)
		{
			Packer.Fill(E, Stroke.Gradient);
		}
		if (bPattern)
		{
			const FVector2f Tile = FPacker::Tile(Stroke.Pattern, Scale);
			E[3] = FVector4f(Tile.X, Tile.Y, Stroke.Pattern.Offset.X * Scale, Stroke.Pattern.Offset.Y * Scale);
			E[5].Z = Packer.Pattern(Stroke.Pattern.Pattern);
		}
		const float Out = Stroke.Position == EDreamStyleStrokePosition::Outside ? Stroke.Size : (Stroke.Position == EDreamStyleStrokePosition::Center ? Stroke.Size * 0.5f : 0.0f);
		Reach = FMath::Max(Reach, Out * Scale);
	}
	const FDreamStyleBevelEmboss& Bevel = InStyle.BevelEmboss;
	if (Bevel.bEnabled && (Bevel.HighlightOpacity > 0.0f || Bevel.ShadowOpacity > 0.0f)
		&& (Bevel.Style != EDreamStyleBevelStyle::StrokeEmboss || FirstStroke != nullptr))
	{
		int32 Flags = (Bevel.Direction == EDreamStyleBevelDirection::Down ? EntryFlagDown : 0) | (Bevel.bGlossAntiAliased ? EntryFlagGlossAntiAliased : 0)
			| (Bevel.bContourAntiAliased ? EntryFlagAntiAliased : 0) | (InLight != nullptr ? EntryFlagPointLight : 0);
		// A point light tints the highlight by its colour and scales it by its strength.
		const float HighlightOpacity = InLight != nullptr ? Saturate(Bevel.HighlightOpacity * FMath::Max(InLight->Intensity, 0.0f)) : Bevel.HighlightOpacity;
		if (FVector4f* E = Packer.Add(KindBevel, Bevel.HighlightMode, HighlightOpacity, Flags))
		{
			const FVector3f L = Light(Angle(Bevel.bUseGlobalLight, Bevel.Angle), Bevel.bUseGlobalLight ? InStyle.GlobalLightAltitude : Bevel.Altitude);
			EDreamStyleBevelStyle Style = Bevel.Style;
			float Size = Bevel.Size * Scale;
			float Shift = 0.0f;
			if (Style == EDreamStyleBevelStyle::StrokeEmboss)
			{
				// An emboss across the stroke's band: as wide as it, centred on it.
				const float StrokeSize = FirstStroke->Size * Scale;
				const float Out = FirstStroke->Position == EDreamStyleStrokePosition::Outside ? StrokeSize
					: (FirstStroke->Position == EDreamStyleStrokePosition::Center ? StrokeSize * 0.5f : 0.0f);
				Shift = Out - StrokeSize * 0.5f;
				Size = StrokeSize;
				Style = EDreamStyleBevelStyle::Emboss;
			}
			const FVector3f Highlight = InLight != nullptr ? Rgb(Bevel.HighlightColor) * Rgb(InLight->Color) : Rgb(Bevel.HighlightColor);
			const FVector3f Shadow = Rgb(Bevel.ShadowColor);
			E[1] = FVector4f(Highlight.X, Highlight.Y, Highlight.Z, Shift);
			E[2] = FVector4f(Size, Bevel.Soften * Scale, L.X, L.Y);
			E[3] = FVector4f(FMath::Clamp(Bevel.Depth, 0.01f, 10.0f), FMath::Clamp(Bevel.ContourRange, 0.01f, 1.0f), L.Z,
				Bevel.bUseContour ? Packer.Contour(Bevel.Contour) : -1.0f);
			E[4] = FVector4f(Shadow.X, Shadow.Y, Shadow.Z, Saturate(Bevel.ShadowOpacity));
			E[5] = FVector4f(static_cast<float>(static_cast<uint8>(Bevel.ShadowMode)), Packer.Contour(Bevel.GlossContour),
				static_cast<float>(static_cast<uint8>(Style)), static_cast<float>(static_cast<uint8>(Bevel.Technique)));
			if (InLight != nullptr)
			{
				E[6] = FVector4f(InLight->Position.X, InLight->Position.Y, FMath::Max(InLight->Height, 1.0f), FMath::Max(InLight->Radius, 1.0f));
			}
			const float Outside = Style == EDreamStyleBevelStyle::OuterBevel ? Size : (Style == EDreamStyleBevelStyle::InnerBevel ? 0.0f : Size * 0.5f);
			Reach = FMath::Max(Reach, Outside + Shift + Bevel.Soften * Scale);
		}
	}

	const int32 NumEffects = Packer.Entries.Num() / EntryTexels;
	if (NumEffects == 0 && InStyle.FillOpacity >= 1.0f)
	{
		OutRecord = FDreamUILayerStyleRecord();
		return false;
	}
	const int32 ContourStart = HeaderTexels + Packer.Entries.Num();
	const int32 GradientStart = ContourStart + Packer.Contours.Num() * ContourTexels;
	const int32 Total = GradientStart + Packer.Gradients.Num() * GradientTexels;
	TArray<FVector4f>& Texels = OutRecord.Texels;
	Texels.Reset(Total);
	// One unit beyond the furthest effect, for the antialiasing of its edge.
	OutRecord.OuterReach = Reach > 0.0f ? Reach + 1.0f : 0.0f;
	const int32 RecordFlags = Packer.bPatterns ? RecordFlagPatterns : 0;
	Texels.Add(FVector4f(static_cast<float>(NumEffects), static_cast<float>(RecordFlags), Saturate(InStyle.FillOpacity), OutRecord.OuterReach));
	Texels.Add(FVector4f(static_cast<float>(ContourStart), static_cast<float>(GradientStart), static_cast<float>(NumSlots), 0.0f));
	Texels.Append(Packer.Entries);
	for (const FDreamStyleContour& Contour : Packer.Contours)
	{
		for (int32 Texel = 0; Texel < ContourTexels; ++Texel)
		{
			FVector4f Samples;
			for (int32 Component = 0; Component < 4; ++Component)
			{
				const int32 Sample = Texel * 4 + Component;
				Samples[Component] = Contour.Evaluate(static_cast<float>(Sample) / static_cast<float>(ContourSamples - 1));
			}
			Texels.Add(Samples);
		}
	}
	for (const FDreamStyleGradient& Gradient : Packer.Gradients)
	{
		for (int32 Texel = 0; Texel < GradientTexels; ++Texel)
		{
			FVector4f Samples;
			for (int32 Component = 0; Component < 4; ++Component)
			{
				const int32 Sample = Texel * 4 + Component;
				Samples[Component] = PackColor(Gradient.Evaluate(static_cast<float>(Sample) / static_cast<float>(GradientSamples - 1)));
			}
			Texels.Add(Samples);
		}
	}
	OutRecord.NumOuterSlots = NumSlots;
	for (int32 Slot = 0; Slot < MaxOuterSlots; ++Slot)
	{
		OutRecord.OuterSlotModes[Slot] = SlotModes[Slot];
	}
	return true;
}

float FDreamUILayerStyleMath::Soft(float X, float W)
{
	return 1.0f - DreamUILayerStyleMathLocal::Smoothstep(-W, W, X);
}

float FDreamUILayerStyleMath::Hard(float X, float AA)
{
	return FMath::Clamp(0.5f - X / AA, 0.0f, 1.0f);
}

float FDreamUILayerStyleMath::Noise(int32 CellX, int32 CellY)
{
	return static_cast<float>(FDreamUIClipMaskMath::Hash(static_cast<uint32>(CellX), static_cast<uint32>(CellY), NoiseSeed) & 0xFFFFFFu) / 16777215.0f;
}

float FDreamUILayerStyleMath::Contour(const FVector4f* InRecord, float Index, float V, bool bAntiAliased)
{
	(void)bAntiAliased;
	V = FMath::Clamp(V, 0.0f, 1.0f);
	if (Index < -0.5f)
	{
		return V;
	}
	const int32 Base = static_cast<int32>(InRecord[1].X) + FMath::RoundToInt(Index) * ContourTexels;
	const float At = V * static_cast<float>(ContourSamples - 1);
	const int32 I0 = FMath::Min(static_cast<int32>(FMath::Floor(At)), ContourSamples - 1);
	const int32 I1 = FMath::Min(I0 + 1, ContourSamples - 1);
	const float A = DreamUILayerStyleMathLocal::Component(InRecord[Base + I0 / 4], I0 % 4);
	const float B = DreamUILayerStyleMathLocal::Component(InRecord[Base + I1 / 4], I1 % 4);
	return FMath::Lerp(A, B, At - static_cast<float>(I0));
}

FVector4f FDreamUILayerStyleMath::Gradient(const FVector4f* InRecord, float Index, float T)
{
	using namespace DreamUILayerStyleMathLocal;
	if (Index < -0.5f)
	{
		return FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	}
	const int32 Base = static_cast<int32>(InRecord[1].Y) + FMath::RoundToInt(Index) * GradientTexels;
	const float At = Saturate(T) * static_cast<float>(GradientSamples - 1);
	const int32 I0 = FMath::Min(static_cast<int32>(FMath::Floor(At)), GradientSamples - 1);
	const int32 I1 = FMath::Min(I0 + 1, GradientSamples - 1);
	const FVector4f A = UnpackColor(Component(InRecord[Base + I0 / 4], I0 % 4));
	const FVector4f B = UnpackColor(Component(InRecord[Base + I1 / 4], I1 % 4));
	return A + (B - A) * (At - static_cast<float>(I0));
}

float FDreamUILayerStyleMath::GradientPosition(int32 Type, float Angle, float Scale, FVector2f Offset, FVector2f P, FVector2f HalfSize, bool bReverse)
{
	const FVector2f C = P - Offset * HalfSize * 2.0f;
	const float Cos = FMath::Cos(Angle);
	const float Sin = FMath::Sin(Angle);
	const FVector2f Dir(Cos, -Sin);
	const FVector2f Perp(-Sin, -Cos);
	const float Length = FMath::Max(FMath::Abs(HalfSize.X * Cos) + FMath::Abs(HalfSize.Y * Sin), 1e-4f) * Scale;
	const float U = FVector2f::DotProduct(C, Dir);
	const float W = FVector2f::DotProduct(C, Perp);
	float T;
	switch (Type)
	{
	case 1: T = C.Size() / Length; break;
	case 2:
	{
		T = FMath::Atan2(W, U) / (2.0f * UE_PI);
		T = T < 0.0f ? T + 1.0f : T;
		break;
	}
	case 3: T = FMath::Abs(U) / Length; break;
	case 4: T = (FMath::Abs(U) + FMath::Abs(W)) / Length; break;
	default: T = 0.5f + U / (2.0f * Length); break;
	}
	T = bReverse ? 1.0f - T : T;
	return FMath::Clamp(T, 0.0f, 1.0f);
}

FVector4f FDreamUILayerStyleMath::Over(int32 Mode, const FVector4f& Backdrop, const FVector3f& Cs, float Sa)
{
	const FVector4f Source = FDreamUIBlendMath::OverBackdrop(static_cast<EDreamUIBlendMode>(Mode), Cs, Sa, Backdrop, false, false);
	return Source + Backdrop * (1.0f - Sa);
}

float FDreamUILayerStyleMath::BevelHeight(const FVector4f* InRecord, float X, float Size, int32 Style, int32 Technique, float ContourIndex, float ContourRange, bool bContourAntiAliased, bool bDown)
{
	using namespace DreamUILayerStyleMathLocal;
	Size = FMath::Max(Size, 1e-3f);
	float U;
	switch (Style)
	{
	case 1: U = 1.0f - Saturate(X / Size); break;
	case 2: U = Saturate(0.5f - X / Size); break;
	case 3: U = Saturate(FMath::Abs(X) / (0.5f * Size)); break;
	default: U = Saturate(-X / Size); break;
	}
	float H;
	switch (Technique)
	{
	case 1: H = U; break;
	case 2: H = FMath::Lerp(U, Smoothstep(0.0f, 1.0f, U), 0.5f); break;
	default: H = FMath::Sin(U * UE_HALF_PI); break;
	}
	if (ContourIndex > -0.5f)
	{
		H = Contour(InRecord, ContourIndex, Saturate(H / ContourRange), bContourAntiAliased);
	}
	return bDown ? -H : H;
}

FDreamUILayerStyleMath::FShaded FDreamUILayerStyleMath::Shade(const TArray<FVector4f>& InRecord, const FPixel& InPixel, TFunctionRef<float(FVector2f)> InDistance)
{
	return Shade(InRecord, InPixel, InDistance, [](int32, FVector2f) { return FVector4f(1.0f, 1.0f, 1.0f, 1.0f); });
}

FDreamUILayerStyleMath::FShaded FDreamUILayerStyleMath::Shade(const TArray<FVector4f>& InRecord, const FPixel& InPixel, TFunctionRef<float(FVector2f)> InDistance,
	TFunctionRef<FVector4f(int32, FVector2f)> InPattern)
{
	using namespace DreamUILayerStyleMathLocal;
	FShaded Out;
	const float FillOpacity = InRecord.Num() > 0 ? InRecord[0].Z : 1.0f;
	// The layer itself, before its edge's coverage: what the effects on it blend over.
	FVector4f Layer(InPixel.Color.X * InPixel.Opacity * FillOpacity, InPixel.Color.Y * InPixel.Opacity * FillOpacity,
		InPixel.Color.Z * InPixel.Opacity * FillOpacity, InPixel.Opacity * FillOpacity);
	if (InRecord.Num() < HeaderTexels)
	{
		Out.Layer = Layer * InPixel.Coverage;
		Out.Flat = Out.Layer;
		return Out;
	}
	const FVector4f* R = InRecord.GetData();
	const int32 NumEffects = static_cast<int32>(R[0].X);
	const float AA = FMath::Max(InPixel.AA, 1e-4f);
	const FVector2f P = InPixel.P;
	const float X0 = InDistance(P);
	// What lies behind the layer is knocked out where the layer is, inside its edge's antialiasing: no seam along the edge.
	const float Knock = X0 < -0.5f * AA ? 0.0f : 1.0f;
	const float Grain = Noise(InPixel.Cell.X, InPixel.Cell.Y);
	FVector4f Behind(0.0f, 0.0f, 0.0f, 0.0f);
	// A stroke's or a bevel's part outside the edge: under the layer, over what lies behind it.
	FVector4f Under(0.0f, 0.0f, 0.0f, 0.0f);
	for (int32 Effect = 0; Effect < NumEffects; ++Effect)
	{
		const FVector4f* E = R + HeaderTexels + Effect * EntryTexels;
		const int32 Kind = FMath::RoundToInt(E[0].X);
		const int32 Mode = FMath::RoundToInt(E[0].Y);
		const float Opacity = E[0].Z;
		const int32 Flags = FMath::RoundToInt(E[0].W);
		const FVector3f Color(E[1].X, E[1].Y, E[1].Z);
		switch (Kind)
		{
		case KindDropShadow:
		case KindOuterGlow:
		{
			const float Size = E[2].X;
			const float Spread = E[2].Y;
			const float W = FMath::Max((1.0f - Spread) * Size, 0.5f * AA);
			FVector3f Fill = Color;
			float A;
			if (Kind == KindDropShadow)
			{
				const float X = InDistance(P - FVector2f(E[2].Z, E[2].W));
				A = Contour(R, E[3].W, Soft(X - Spread * Size, W), (Flags & EntryFlagAntiAliased) != 0);
				A *= (Flags & EntryFlagKnockOut) != 0 ? Knock : 1.0f;
			}
			else
			{
				float V = (Flags & EntryFlagPrecise) != 0 ? 1.0f - Saturate(FMath::Max(X0 - Spread * Size, 0.0f) / W) : Soft(X0 - Spread * Size, W);
				V = Saturate(V / E[3].Y);
				A = Contour(R, E[3].W, V, (Flags & EntryFlagAntiAliased) != 0) * Knock;
				if ((Flags & EntryFlagGradientFill) != 0)
				{
					const FVector4f G = Gradient(R, E[4].X, Saturate(1.0f - V + E[3].Z * (Grain - 0.5f)));
					Fill = FVector3f(G.X, G.Y, G.Z);
					A *= G.W;
				}
			}
			A *= Opacity * (1.0f - E[3].X * Grain);
			const int32 Slot = FMath::Clamp(FMath::RoundToInt(E[1].W), 0, MaxOuterSlots - 1);
			Out.Outer[Slot] = Over(Mode, Out.Outer[Slot], Fill, A);
			Behind = Over(Mode, Behind, Fill, A);
			break;
		}
		case KindPatternOverlay:
		{
			const FVector2f UV((P.X + InPixel.HalfSize.X - E[2].Z) / E[2].X, (P.Y + InPixel.HalfSize.Y - E[2].W) / E[2].Y);
			const FVector4f Texel = InPattern(FMath::RoundToInt(E[5].Z), UV);
			Layer = Over(Mode, Layer, FVector3f(Texel.X, Texel.Y, Texel.Z), Opacity * Texel.W);
			break;
		}
		case KindGradientOverlay:
		{
			float T = GradientPosition(FMath::RoundToInt(E[4].Y), E[4].Z, E[4].W, FVector2f(E[5].X, E[5].Y), P, InPixel.HalfSize, (Flags & EntryFlagReverse) != 0);
			T = (Flags & EntryFlagDither) != 0 ? Saturate(T + (Grain - 0.5f) / 255.0f) : T;
			const FVector4f G = Gradient(R, E[4].X, T);
			Layer = Over(Mode, Layer, FVector3f(G.X, G.Y, G.Z), Opacity * G.W);
			break;
		}
		case KindColorOverlay:
			Layer = Over(Mode, Layer, Color, Opacity);
			break;
		case KindSatin:
		{
			const FVector2f Shift(E[2].Z, E[2].W);
			const float W = FMath::Max(0.5f * E[2].X, 0.5f * AA);
			const float A1 = Soft(InDistance(P + Shift), W);
			const float A2 = Soft(InDistance(P - Shift), W);
			float V = Contour(R, E[3].W, FMath::Abs(A1 - A2), (Flags & EntryFlagAntiAliased) != 0);
			V = (Flags & EntryFlagInvert) != 0 ? 1.0f - V : V;
			Layer = Over(Mode, Layer, Color, Opacity * V);
			break;
		}
		case KindInnerGlow:
		case KindInnerShadow:
		{
			const float Size = E[2].X;
			const float Choke = E[2].Y;
			const float W = FMath::Max((1.0f - Choke) * Size, 0.5f * AA);
			FVector3f Fill = Color;
			float A;
			if (Kind == KindInnerShadow)
			{
				const float X = InDistance(P - FVector2f(E[2].Z, E[2].W));
				A = Contour(R, E[3].W, Soft(-X - Choke * Size, W), (Flags & EntryFlagAntiAliased) != 0);
			}
			else
			{
				const float Xi = -X0 - Choke * Size;
				float V = (Flags & EntryFlagPrecise) != 0 ? 1.0f - Saturate(FMath::Max(Xi, 0.0f) / W) : Soft(Xi, W);
				V = (Flags & EntryFlagFromCenter) != 0 ? 1.0f - V : V;
				V = Saturate(V / E[3].Y);
				A = Contour(R, E[3].W, V, (Flags & EntryFlagAntiAliased) != 0);
				if ((Flags & EntryFlagGradientFill) != 0)
				{
					const FVector4f G = Gradient(R, E[4].X, Saturate(1.0f - V + E[3].Z * (Grain - 0.5f)));
					Fill = FVector3f(G.X, G.Y, G.Z);
					A *= G.W;
				}
			}
			Layer = Over(Mode, Layer, Fill, A * Opacity * (1.0f - E[3].X * Grain));
			break;
		}
		case KindStroke:
		{
			const float Size = E[2].X;
			const int32 Position = FMath::RoundToInt(E[2].Y);
			const float OutSize = Position == 0 ? Size : (Position == 2 ? 0.5f * Size : 0.0f);
			const float InSize = Size - OutSize;
			FVector3f Fill = Color;
			float FillAlpha = 1.0f;
			if ((Flags & EntryFlagGradientFill) != 0)
			{
				float T = GradientPosition(FMath::RoundToInt(E[4].Y), E[4].Z, E[4].W, FVector2f(E[5].X, E[5].Y), P, InPixel.HalfSize, (Flags & EntryFlagReverse) != 0);
				T = (Flags & EntryFlagDither) != 0 ? Saturate(T + (Grain - 0.5f) / 255.0f) : T;
				const FVector4f G = Gradient(R, E[4].X, T);
				Fill = FVector3f(G.X, G.Y, G.Z);
				FillAlpha = G.W;
			}
			else if ((Flags & EntryFlagPatternFill) != 0)
			{
				const FVector2f UV((P.X + InPixel.HalfSize.X - E[3].Z) / E[3].X, (P.Y + InPixel.HalfSize.Y - E[3].W) / E[3].Y);
				const FVector4f Texel = InPattern(FMath::RoundToInt(E[5].Z), UV);
				Fill = FVector3f(Texel.X, Texel.Y, Texel.Z);
				FillAlpha = Texel.W;
			}
			if (InSize > 0.0f)
			{
				const float Band = Saturate(0.5f + (X0 + InSize) / AA);
				Layer = Over(Mode, Layer, Fill, Opacity * FillAlpha * Band);
			}
			if (OutSize > 0.0f)
			{
				const float Band = Hard(X0 - OutSize, AA) * Knock;
				Under = Over(Mode, Under, Fill, Opacity * FillAlpha * Band);
			}
			break;
		}
		case KindBevel:
		{
			const float Size = E[2].X;
			const float Step = FMath::Max(E[2].Y, AA);
			FVector3f Light(E[2].Z, E[2].W, E[3].Z);
			float Falloff = 1.0f;
			if ((Flags & EntryFlagPointLight) != 0)
			{
				const FVector2f Toward = FVector2f(E[6].X, E[6].Y) - P;
				Light = FVector3f(Toward.X, Toward.Y, E[6].Z);
				Light = Light / Light.Size();
				Falloff = Saturate(1.0f - Toward.Size() / FMath::Max(E[6].W, 1e-3f));
				Falloff *= Falloff;
			}
			const int32 Style = FMath::RoundToInt(E[5].Z);
			const int32 Technique = FMath::RoundToInt(E[5].W);
			const bool bDown = (Flags & EntryFlagDown) != 0;
			const bool bContourAA = (Flags & EntryFlagAntiAliased) != 0;
			const float X = X0 - E[1].W;
			const float Rise = (BevelHeight(R, X + Step, Size, Style, Technique, E[3].W, E[3].Y, bContourAA, bDown)
				- BevelHeight(R, X - Step, Size, Style, Technique, E[3].W, E[3].Y, bContourAA, bDown)) / (2.0f * Step);
			// The shape's outward normal, from its distance either side.
			const float D = FMath::Max(0.5f * AA, 1e-3f);
			FVector2f Normal2(InDistance(P + FVector2f(D, 0.0f)) - InDistance(P - FVector2f(D, 0.0f)), InDistance(P + FVector2f(0.0f, D)) - InDistance(P - FVector2f(0.0f, D)));
			const float Length = Normal2.Size();
			Normal2 = Length > 1e-6f ? Normal2 / Length : FVector2f::ZeroVector;
			const float Slope = E[3].X * Size * Rise;
			FVector3f N(-Slope * Normal2.X, -Slope * Normal2.Y, 1.0f);
			N = N / N.Size();
			const float Shade = FVector3f::DotProduct(N, Light);
			const float Flat = Light.Z;
			const float Lit = Contour(R, E[5].Y, Saturate((Shade - Flat) / FMath::Max(1.0f - Flat, 1e-3f)), (Flags & EntryFlagGlossAntiAliased) != 0) * Falloff;
			const float Dark = Contour(R, E[5].Y, Saturate((Flat - Shade) / FMath::Max(Flat, 1e-3f)), (Flags & EntryFlagGlossAntiAliased) != 0) * Falloff;
			const int32 ShadowMode = FMath::RoundToInt(E[5].X);
			const FVector3f ShadowColor(E[4].X, E[4].Y, E[4].Z);
			// Inside the edge, on the layer; outside it, under the layer.
			if (X0 < 0.5f * AA)
			{
				Layer = Over(Mode, Layer, Color, Opacity * Lit);
				Layer = Over(ShadowMode, Layer, ShadowColor, E[4].W * Dark);
			}
			if (Knock > 0.5f)
			{
				const float Outside = 1.0f - InPixel.Coverage;
				Under = Over(Mode, Under, Color, Opacity * Lit * Outside);
				Under = Over(ShadowMode, Under, ShadowColor, E[4].W * Dark * Outside);
			}
			break;
		}
		default:
			break;
		}
	}
	// The layer and the effects on it, clipped to its edge; what is outside the edge under it.
	const FVector4f Clipped = Layer * InPixel.Coverage;
	Out.Layer = Clipped + Under * (1.0f - Clipped.W);
	Out.Flat = Out.Layer + Behind * (1.0f - Out.Layer.W);
	return Out;
}

void FDreamUILayerStyleMath::DistanceSquared1D(const float* InCost, int32 InCount, float* OutDistance)
{
	// The lower envelope of the parabolas rooted at each cell, (q - p)^2 + cost(p): its vertices in V, the boundaries between
	// neighbouring parabolas in Z.
	TArray<int32, TInlineAllocator<256>> V;
	TArray<float, TInlineAllocator<257>> Z;
	V.SetNumUninitialized(InCount);
	Z.SetNumUninitialized(InCount + 1);
	int32 K = -1;
	for (int32 Q = 0; Q < InCount; ++Q)
	{
		if (InCost[Q] >= 1e19f)
		{
			continue;
		}
		if (K < 0)
		{
			K = 0;
			V[0] = Q;
			Z[0] = -1e30f;
			Z[1] = 1e30f;
			continue;
		}
		float S;
		while (true)
		{
			const int32 P = V[K];
			S = ((InCost[Q] + static_cast<float>(Q) * Q) - (InCost[P] + static_cast<float>(P) * P)) / (2.0f * (Q - P));
			if (S <= Z[K] && K > 0)
			{
				--K;
				continue;
			}
			break;
		}
		if (S <= Z[K])
		{
			// Only when K is 0: the new parabola is lower everywhere.
			V[0] = Q;
			Z[0] = -1e30f;
			Z[1] = 1e30f;
			continue;
		}
		++K;
		V[K] = Q;
		Z[K] = S;
		Z[K + 1] = 1e30f;
	}
	if (K < 0)
	{
		for (int32 Q = 0; Q < InCount; ++Q)
		{
			OutDistance[Q] = 1e20f;
		}
		return;
	}
	int32 J = 0;
	for (int32 Q = 0; Q < InCount; ++Q)
	{
		while (Z[J + 1] < static_cast<float>(Q))
		{
			++J;
		}
		const float D = static_cast<float>(Q - V[J]);
		OutDistance[Q] = D * D + InCost[V[J]];
	}
}

void FDreamUILayerStyleMath::ExactDistance(TConstArrayView<bool> InInside, int32 InWidth, int32 InHeight, TArray<float>& OutSigned, float InFar)
{
	const int32 Count = InWidth * InHeight;
	OutSigned.SetNumZeroed(Count);
	if (InWidth <= 0 || InHeight <= 0 || InInside.Num() < Count)
	{
		return;
	}
	// The squared distance to the nearest cell of one side, for every cell: rows, then columns of the rows' answers.
	auto Transform = [InWidth, InHeight, Count, &InInside](bool bToInside, TArray<float>& OutSquared)
	{
		OutSquared.SetNumUninitialized(Count);
		TArray<float> Line;
		TArray<float> Result;
		Line.SetNumUninitialized(FMath::Max(InWidth, InHeight));
		Result.SetNumUninitialized(FMath::Max(InWidth, InHeight));
		for (int32 Y = 0; Y < InHeight; ++Y)
		{
			for (int32 X = 0; X < InWidth; ++X)
			{
				Line[X] = InInside[Y * InWidth + X] == bToInside ? 0.0f : 1e20f;
			}
			DistanceSquared1D(Line.GetData(), InWidth, Result.GetData());
			for (int32 X = 0; X < InWidth; ++X)
			{
				OutSquared[Y * InWidth + X] = Result[X];
			}
		}
		for (int32 X = 0; X < InWidth; ++X)
		{
			for (int32 Y = 0; Y < InHeight; ++Y)
			{
				Line[Y] = OutSquared[Y * InWidth + X];
			}
			DistanceSquared1D(Line.GetData(), InHeight, Result.GetData());
			for (int32 Y = 0; Y < InHeight; ++Y)
			{
				OutSquared[Y * InWidth + X] = Result[Y];
			}
		}
	};
	TArray<float> ToInside;
	TArray<float> ToOutside;
	Transform(true, ToInside);
	Transform(false, ToOutside);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (InInside[Index])
		{
			OutSigned[Index] = ToOutside[Index] >= 1e19f ? -InFar : -(FMath::Sqrt(ToOutside[Index]) - 0.5f);
		}
		else
		{
			OutSigned[Index] = ToInside[Index] >= 1e19f ? InFar : FMath::Sqrt(ToInside[Index]) - 0.5f;
		}
	}
}