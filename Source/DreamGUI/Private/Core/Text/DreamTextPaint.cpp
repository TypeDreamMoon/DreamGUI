// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPaint.h"

#include "Core/DreamGradientAsset.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Hash/CityHash.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CString.h"
#include "Text/DreamUIValueFormat.h"

/*
 * The gradient as drawn: the reference evaluation on FDreamGradient's comment, in the order DreamUIPaint.ush takes it,
 * from the very values the gradient row holds. Evaluate and PackRow both go through PrepareGradient and PrepareStopColor,
 * so what the CPU computes and what the shader reads out of the row can only differ in the shader's own float arithmetic.
 */
namespace DreamTextPaintLocal
{
	/** A gradient as its row holds it: enums in range, every number finite, the stops' positions fixed up as CSS does. */
	struct FPreparedGradient
	{
		EDreamPaintType Type = EDreamPaintType::None;
		EDreamPaintSpread Spread = EDreamPaintSpread::Pad;
		EDreamPaintInterpolation Interpolation = EDreamPaintInterpolation::SRGB;
		EDreamPaintRadialShape Shape = EDreamPaintRadialShape::Ellipse;
		EDreamPaintRadialSize Size = EDreamPaintRadialSize::FarthestCorner;
		int32 StopCount = 0;
		float Angle = 180.0f;
		float Scale = 1.0f;
		float Offset = 0.0f;
		FVector2f Center = FVector2f(0.5f, 0.5f);
		FVector2f Radius = FVector2f(0.5f, 0.5f);
		float Positions[FDreamGradient::MaxStops] = {};
	};

	constexpr float PaintSqrt2 = 1.41421356f;

	/** An enum's value, or its first one when a corrupt byte put it past InLast. */
	template <typename EnumType>
	EnumType PaintEnumInRange(EnumType InValue, EnumType InLast)
	{
		return static_cast<uint8>(InValue) <= static_cast<uint8>(InLast) ? InValue : static_cast<EnumType>(0);
	}

	float PaintFiniteOr(float InValue, float InDefault)
	{
		return FMath::IsFinite(InValue) ? InValue : InDefault;
	}

	void PrepareGradient(const FDreamGradient& InGradient, FPreparedGradient& OutPrepared)
	{
		OutPrepared.Type = PaintEnumInRange(InGradient.Type, EDreamPaintType::Corners);
		OutPrepared.Spread = PaintEnumInRange(InGradient.Spread, EDreamPaintSpread::Reflect);
		OutPrepared.Interpolation = PaintEnumInRange(InGradient.Interpolation, EDreamPaintInterpolation::Oklab);
		OutPrepared.Shape = PaintEnumInRange(InGradient.Shape, EDreamPaintRadialShape::Circle);
		OutPrepared.Size = PaintEnumInRange(InGradient.Size, EDreamPaintRadialSize::Explicit);
		OutPrepared.Angle = PaintFiniteOr(InGradient.Angle, 180.0f);
		OutPrepared.Scale = PaintFiniteOr(InGradient.Scale, 1.0f);
		OutPrepared.Offset = PaintFiniteOr(InGradient.Offset, 0.0f);
		OutPrepared.Center = FVector2f(PaintFiniteOr(InGradient.Center.X, 0.5f), PaintFiniteOr(InGradient.Center.Y, 0.5f));
		OutPrepared.Radius = FVector2f(PaintFiniteOr(InGradient.Radius.X, 0.5f), PaintFiniteOr(InGradient.Radius.Y, 0.5f));
		OutPrepared.StopCount = FMath::Min(InGradient.Stops.Num(), FDreamGradient::MaxStops);
		// CSS's fix-up: a position before the one ahead of it is taken as that one, which makes a hard edge.
		float Previous = 0.0f;
		for (int32 StopIndex = 0; StopIndex < OutPrepared.StopCount; ++StopIndex)
		{
			float Position = PaintFiniteOr(InGradient.Stops[StopIndex].Position, Previous);
			if (StopIndex > 0)
			{
				Position = FMath::Max(Position, Previous);
			}
			OutPrepared.Positions[StopIndex] = Position;
			Previous = Position;
		}
	}

	/** The sRGB transfer from encoded to linear, exactly as DreamUIText_DecodeSrgb. */
	float PaintDecodeSrgb(float InEncoded)
	{
		const float Encoded = FMath::Clamp(InEncoded, 0.0f, 1.0f);
		return Encoded <= 0.04045f ? Encoded / 12.92f : FMath::Pow((Encoded + 0.055f) / 1.055f, 2.4f);
	}

	float PaintCubeRoot(float InValue)
	{
		return InValue < 0.0f ? -FMath::Pow(-InValue, 1.0f / 3.0f) : FMath::Pow(InValue, 1.0f / 3.0f);
	}

	/** Linear sRGB to Oklab, Ottosson's matrices. */
	FVector3f PaintLinearToOklab(const FVector3f& InLinear)
	{
		const float LongCone = PaintCubeRoot(0.4122214708f * InLinear.X + 0.5363325363f * InLinear.Y + 0.0514459929f * InLinear.Z);
		const float MediumCone = PaintCubeRoot(0.2119034982f * InLinear.X + 0.6806995451f * InLinear.Y + 0.1073969566f * InLinear.Z);
		const float ShortCone = PaintCubeRoot(0.0883024619f * InLinear.X + 0.2817188376f * InLinear.Y + 0.6299787005f * InLinear.Z);
		return FVector3f(
			0.2104542553f * LongCone + 0.7936177850f * MediumCone - 0.0040720468f * ShortCone,
			1.9779984951f * LongCone - 2.4285922050f * MediumCone + 0.4505937099f * ShortCone,
			0.0259040371f * LongCone + 0.7827717662f * MediumCone - 0.8086757660f * ShortCone);
	}

	/** Oklab back to linear sRGB. */
	FVector3f PaintOklabToLinear(const FVector3f& InLab)
	{
		const float LongCone = InLab.X + 0.3963377774f * InLab.Y + 0.2158037573f * InLab.Z;
		const float MediumCone = InLab.X - 0.1055613458f * InLab.Y - 0.0638541728f * InLab.Z;
		const float ShortCone = InLab.X - 0.0894841775f * InLab.Y - 1.2914855480f * InLab.Z;
		const float Long3 = LongCone * LongCone * LongCone;
		const float Medium3 = MediumCone * MediumCone * MediumCone;
		const float Short3 = ShortCone * ShortCone * ShortCone;
		return FVector3f(
			4.0767416621f * Long3 - 3.3077115913f * Medium3 + 0.2309699292f * Short3,
			-1.2684380046f * Long3 + 2.6097574011f * Medium3 - 0.3413193965f * Short3,
			-0.0041960863f * Long3 - 0.7034186147f * Medium3 + 1.7076147010f * Short3);
	}

	/** A stop's colour as its row holds it: premultiplied, and already in the interpolation space. */
	FVector4f PrepareStopColor(const FColor& InColor, EDreamPaintInterpolation InSpace)
	{
		const float Alpha = (float)InColor.A / 255.0f;
		FVector3f Rgb((float)InColor.R / 255.0f, (float)InColor.G / 255.0f, (float)InColor.B / 255.0f);
		if (InSpace != EDreamPaintInterpolation::SRGB)
		{
			Rgb = FVector3f(PaintDecodeSrgb(Rgb.X), PaintDecodeSrgb(Rgb.Y), PaintDecodeSrgb(Rgb.Z));
			if (InSpace == EDreamPaintInterpolation::Oklab)
			{
				Rgb = PaintLinearToOklab(Rgb);
			}
		}
		return FVector4f(Rgb.X * Alpha, Rgb.Y * Alpha, Rgb.Z * Alpha, Alpha);
	}

	/** A mixed colour out of its space: un-premultiplied (0 where alpha is 0), linear, clamped at 0. */
	FLinearColor FinishPaintColor(const FVector4f& InMixed, EDreamPaintInterpolation InSpace)
	{
		const float Alpha = InMixed.W;
		FVector3f Rgb = Alpha > 0.0f ? FVector3f(InMixed.X / Alpha, InMixed.Y / Alpha, InMixed.Z / Alpha) : FVector3f::ZeroVector;
		if (InSpace == EDreamPaintInterpolation::SRGB)
		{
			Rgb = FVector3f(PaintDecodeSrgb(Rgb.X), PaintDecodeSrgb(Rgb.Y), PaintDecodeSrgb(Rgb.Z));
		}
		else if (InSpace == EDreamPaintInterpolation::Oklab)
		{
			Rgb = PaintOklabToLinear(Rgb);
		}
		return FLinearColor(FMath::Max(Rgb.X, 0.0f), FMath::Max(Rgb.Y, 0.0f), FMath::Max(Rgb.Z, 0.0f), Alpha);
	}

	/** HLSL's frac: x - floor(x), so a negative x answers in [0, 1) too. */
	float PaintFrac(float InValue)
	{
		return InValue - FMath::FloorToFloat(InValue);
	}

	/** HLSL's lerp, a + s * (b - a). */
	FVector4f PaintLerp(const FVector4f& InFrom, const FVector4f& InTo, float InAlpha)
	{
		return InFrom + (InTo - InFrom) * InAlpha;
	}

	/** A Radial's radii (an ellipse's for a Diamond), in box heights, each at least 1e-6. */
	FVector2f GetPaintRadii(const FPreparedGradient& InPrepared, const FVector2f& InCentre, float InAspect, bool bInEllipse)
	{
		FVector2f Radii(0.0f, 0.0f);
		if (InPrepared.Size == EDreamPaintRadialSize::Explicit)
		{
			Radii = bInEllipse
				? FVector2f(InPrepared.Radius.X * InAspect, InPrepared.Radius.Y)
				: FVector2f(InPrepared.Radius.X * InAspect, InPrepared.Radius.X * InAspect);
		}
		else
		{
			const bool bClosest = InPrepared.Size == EDreamPaintRadialSize::ClosestSide || InPrepared.Size == EDreamPaintRadialSize::ClosestCorner;
			const bool bCorner = InPrepared.Size == EDreamPaintRadialSize::ClosestCorner || InPrepared.Size == EDreamPaintRadialSize::FarthestCorner;
			const float ToLeft = FMath::Abs(InCentre.X) * InAspect;
			const float ToRight = FMath::Abs(1.0f - InCentre.X) * InAspect;
			const float ToTop = FMath::Abs(InCentre.Y);
			const float ToBottom = FMath::Abs(1.0f - InCentre.Y);
			const float SideX = bClosest ? FMath::Min(ToLeft, ToRight) : FMath::Max(ToLeft, ToRight);
			const float SideY = bClosest ? FMath::Min(ToTop, ToBottom) : FMath::Max(ToTop, ToBottom);
			if (bInEllipse)
			{
				Radii = bCorner ? FVector2f(SideX * PaintSqrt2, SideY * PaintSqrt2) : FVector2f(SideX, SideY);
			}
			else
			{
				const float CircleRadius = bCorner ? FMath::Sqrt(SideX * SideX + SideY * SideY)
					: (bClosest ? FMath::Min(SideX, SideY) : FMath::Max(SideX, SideY));
				Radii = FVector2f(CircleRadius, CircleRadius);
			}
		}
		return FVector2f(FMath::Max(Radii.X, 1e-6f), FMath::Max(Radii.Y, 1e-6f));
	}

	/** The stops' colour at InT, mixed premultiplied in the interpolation space; a hard edge belongs to the later segment. */
	FVector4f MixPaintStops(const FDreamGradient& InGradient, const FPreparedGradient& InPrepared, float InT)
	{
		const int32 Last = InPrepared.StopCount - 1;
		const EDreamPaintInterpolation Space = InPrepared.Interpolation;
		if (Last == 0 || !(InT > InPrepared.Positions[0]))
		{
			return PrepareStopColor(InGradient.Stops[0].Color, Space);
		}
		if (InT >= InPrepared.Positions[Last])
		{
			return PrepareStopColor(InGradient.Stops[Last].Color, Space);
		}
		// Every segment before the one holding t ends at or before t, so the first that ends after it holds it -- and an
		// empty one (a hard edge) never does.
		for (int32 StopIndex = 0; StopIndex < Last; ++StopIndex)
		{
			const float SegmentEnd = InPrepared.Positions[StopIndex + 1];
			if (InT < SegmentEnd)
			{
				const float SegmentStart = InPrepared.Positions[StopIndex];
				// The segment is never empty here; its length is still taken as at least 1e-9, as the shader takes it.
				const float Fraction = (InT - SegmentStart) / FMath::Max(SegmentEnd - SegmentStart, 1e-9f);
				return PaintLerp(PrepareStopColor(InGradient.Stops[StopIndex].Color, Space),
					PrepareStopColor(InGradient.Stops[StopIndex + 1].Color, Space), Fraction);
			}
		}
		return PrepareStopColor(InGradient.Stops[Last].Color, Space);
	}

	/** -0 written as 0, so two gradients that draw the same pack the same bytes and share their row. */
	void PaintPositiveZeros(FVector4f& InOutPixel)
	{
		for (int32 Component = 0; Component < 4; ++Component)
		{
			if (InOutPixel[Component] == 0.0f)
			{
				InOutPixel[Component] = 0.0f;
			}
		}
	}
}

bool FDreamGradient::IsPainting() const
{
	const uint8 TypeValue = static_cast<uint8>(Type);
	return TypeValue != static_cast<uint8>(EDreamPaintType::None) && TypeValue <= static_cast<uint8>(EDreamPaintType::Corners)
		&& Stops.Num() > 0;
}

FLinearColor FDreamGradient::Evaluate(const FVector2f& InBoxUV, float InAspect, const FDreamPaintAnimation& InAnimation) const
{
	using namespace DreamTextPaintLocal;
	FPreparedGradient Prepared;
	PrepareGradient(*this, Prepared);
	if (Prepared.Type == EDreamPaintType::None || Prepared.StopCount == 0)
	{
		return FLinearColor::White;
	}
	const float Aspect = FMath::IsFinite(InAspect) ? InAspect : 1.0f;
	const float U = InBoxUV.X;
	const float V = InBoxUV.Y;

	if (Prepared.Type == EDreamPaintType::Corners)
	{
		// Top left, top right, bottom left, bottom right; the last stop stands in for a corner the stops do not reach.
		const int32 Last = Prepared.StopCount - 1;
		auto CornerColor = [this, &Prepared, Last](int32 InCorner)
		{
			return PrepareStopColor(Stops[FMath::Min(InCorner, Last)].Color, Prepared.Interpolation);
		};
		const float Across = FMath::Clamp(U, 0.0f, 1.0f);
		const float Down = FMath::Clamp(V, 0.0f, 1.0f);
		const FVector4f TopEdge = PaintLerp(CornerColor(0), CornerColor(1), Across);
		const FVector4f BottomEdge = PaintLerp(CornerColor(2), CornerColor(3), Across);
		return FinishPaintColor(PaintLerp(TopEdge, BottomEdge, Down), Prepared.Interpolation);
	}

	// 1. The gradient parameter.
	const float TurnRadians = FMath::DegreesToRadians(Prepared.Angle + InAnimation.AngleOffset);
	float T = 0.0f;
	switch (Prepared.Type)
	{
	case EDreamPaintType::Linear:
	{
		const FVector2f Middle = FVector2f(0.5f, 0.5f) + InAnimation.CenterOffset;
		const FVector2f Point((U - Middle.X) * Aspect, V - Middle.Y);
		const float Sine = FMath::Sin(TurnRadians);
		const float Cosine = FMath::Cos(TurnRadians);
		// CSS's gradient line: through the middle at the angle, as long as the box's corners need -- the "magic corners".
		// At least 1e-6 long, as the shader's, for a box with no width.
		T = (Point.X * Sine - Point.Y * Cosine) / FMath::Max(FMath::Abs(Aspect * Sine) + FMath::Abs(Cosine), 1e-6f) + 0.5f;
		break;
	}
	case EDreamPaintType::Radial:
	{
		const FVector2f Centre = Prepared.Center + InAnimation.CenterOffset;
		const FVector2f Point((U - Centre.X) * Aspect, V - Centre.Y);
		const FVector2f Radii = GetPaintRadii(Prepared, Centre, Aspect, Prepared.Shape == EDreamPaintRadialShape::Ellipse);
		T = FVector2f(Point.X / Radii.X, Point.Y / Radii.Y).Size();
		break;
	}
	case EDreamPaintType::Conic:
	{
		const FVector2f Centre = Prepared.Center + InAnimation.CenterOffset;
		const FVector2f Point((U - Centre.X) * Aspect, V - Centre.Y);
		const float FromUp = FMath::RadiansToDegrees(FMath::Atan2(Point.X, -Point.Y));
		T = PaintFrac((FromUp - (Prepared.Angle + InAnimation.AngleOffset)) / 360.0f);
		break;
	}
	case EDreamPaintType::Diamond:
	{
		const FVector2f Centre = Prepared.Center + InAnimation.CenterOffset;
		const FVector2f Point((U - Centre.X) * Aspect, V - Centre.Y);
		const FVector2f Radii = GetPaintRadii(Prepared, Centre, Aspect, /*bInEllipse*/ true);
		const float Sine = FMath::Sin(TurnRadians);
		const float Cosine = FMath::Cos(TurnRadians);
		const FVector2f Turned(Point.X * Cosine + Point.Y * Sine, -Point.X * Sine + Point.Y * Cosine);
		T = FMath::Abs(Turned.X) / Radii.X + FMath::Abs(Turned.Y) / Radii.Y;
		break;
	}
	default:
		break;
	}

	// 2. Scale and move.
	const float ScaleFactor = FMath::Max(Prepared.Scale * InAnimation.ScaleMultiplier, 1e-4f);
	const float Shift = Prepared.Offset + InAnimation.Phase;
	T = Prepared.Type == EDreamPaintType::Linear ? (T - 0.5f) / ScaleFactor + 0.5f - Shift : T / ScaleFactor - Shift;

	// 3. Spread, over the span from the first stop to the last.
	const float SpanStart = Prepared.Positions[0];
	const float Span = Prepared.Positions[Prepared.StopCount - 1] - SpanStart;
	if (Span > 0.0f)
	{
		if (Prepared.Spread == EDreamPaintSpread::Repeat)
		{
			T = SpanStart + PaintFrac((T - SpanStart) / Span) * Span;
		}
		else if (Prepared.Spread == EDreamPaintSpread::Reflect)
		{
			float Cycle = (T - SpanStart) / Span;
			Cycle = 1.0f - FMath::Abs(PaintFrac(Cycle * 0.5f) * 2.0f - 1.0f);
			T = SpanStart + Cycle * Span;
		}
	}

	// 4. and 5. The stops, and out of the interpolation space.
	return FinishPaintColor(MixPaintStops(*this, Prepared, T), Prepared.Interpolation);
}

void FDreamGradient::PackRow(TArray<FVector4f>& OutPixels) const
{
	using namespace DreamTextPaintLocal;
	FPreparedGradient Prepared;
	PrepareGradient(*this, Prepared);
	OutPixels.Reset(DreamPaintRows::RowWidth);
	OutPixels.SetNumZeroed(DreamPaintRows::RowWidth);
	OutPixels[DreamPaintRows::GradientHeaderPixel] = FVector4f((float)static_cast<uint8>(Prepared.Type), (float)static_cast<uint8>(Prepared.Spread),
		(float)static_cast<uint8>(Prepared.Interpolation), (float)Prepared.StopCount);
	OutPixels[DreamPaintRows::GradientShapePixel] = FVector4f(Prepared.Angle, Prepared.Scale, Prepared.Offset, (float)static_cast<uint8>(Prepared.Shape));
	OutPixels[DreamPaintRows::GradientCenterPixel] = FVector4f(Prepared.Center.X, Prepared.Center.Y, Prepared.Radius.X, Prepared.Radius.Y);
	OutPixels[DreamPaintRows::GradientSizePixel] = FVector4f((float)static_cast<uint8>(Prepared.Size), 0.0f, 0.0f, 0.0f);
	for (int32 StopIndex = 0; StopIndex < Prepared.StopCount; ++StopIndex)
	{
		OutPixels[DreamPaintRows::GradientPositionsPixel + StopIndex / 4][StopIndex % 4] = Prepared.Positions[StopIndex];
		OutPixels[DreamPaintRows::GradientColorsPixel + StopIndex] = PrepareStopColor(Stops[StopIndex].Color, Prepared.Interpolation);
	}
	for (FVector4f& Pixel : OutPixels)
	{
		PaintPositiveZeros(Pixel);
	}
}

uint64 FDreamGradient::GetRowHash() const
{
	TArray<FVector4f> Pixels;
	PackRow(Pixels);
	return CityHash64(reinterpret_cast<const char*>(Pixels.GetData()), static_cast<uint32>(Pixels.Num() * sizeof(FVector4f)));
}

bool FDreamGradient::operator==(const FDreamGradient& Other) const
{
	return Type == Other.Type
		&& Stops == Other.Stops
		&& Angle == Other.Angle
		&& Center == Other.Center
		&& Shape == Other.Shape
		&& Size == Other.Size
		&& Radius == Other.Radius
		&& Spread == Other.Spread
		&& Scale == Other.Scale
		&& Offset == Other.Offset
		&& Interpolation == Other.Interpolation;
}

/*
 * CSS. The grammar is on FDreamGradient::ParseCss. Reading is a function name, its arguments split at the commas outside
 * brackets, the first of them taken as the prelude unless it starts with a colour, and the rest as stops; each argument is
 * split at its spaces, a bracket kept whole (rgb(0 0 0 / 50%)). Numbers are checked for their shape here and converted
 * with FCString::Atod, which is what DreamUIValueFormat::PrintScalar checks its own spellings against: ToCss prints with
 * it, so every number it writes reads back as exactly the float it was.
 */
namespace DreamTextPaintCssLocal
{
	/** A piece of the text, with the character it starts at in the whole of it: what an error points at. */
	struct FCssPiece
	{
		FString Text;
		int32 Offset = 0;
	};

	/** A stop as read: CSS leaves a position out to have it placed. */
	struct FCssStop
	{
		FColor Color = FColor::Black;
		bool bHasPosition = false;
		float Position = 0.0f;
	};

	struct FCssNamedColor
	{
		const TCHAR* Name;
		uint8 R;
		uint8 G;
		uint8 B;
	};

	/** CSS's basic colours, and the names a rich text's <color> also knows, with CSS's values (green is #008000 here). */
	const FCssNamedColor CssNamedColors[] =
	{
		{ TEXT("black"), 0, 0, 0 },
		{ TEXT("silver"), 192, 192, 192 },
		{ TEXT("gray"), 128, 128, 128 },
		{ TEXT("grey"), 128, 128, 128 },
		{ TEXT("white"), 255, 255, 255 },
		{ TEXT("maroon"), 128, 0, 0 },
		{ TEXT("red"), 255, 0, 0 },
		{ TEXT("purple"), 128, 0, 128 },
		{ TEXT("fuchsia"), 255, 0, 255 },
		{ TEXT("magenta"), 255, 0, 255 },
		{ TEXT("green"), 0, 128, 0 },
		{ TEXT("lime"), 0, 255, 0 },
		{ TEXT("olive"), 128, 128, 0 },
		{ TEXT("yellow"), 255, 255, 0 },
		{ TEXT("navy"), 0, 0, 128 },
		{ TEXT("blue"), 0, 0, 255 },
		{ TEXT("teal"), 0, 128, 128 },
		{ TEXT("aqua"), 0, 255, 255 },
		{ TEXT("cyan"), 0, 255, 255 },
		{ TEXT("orange"), 255, 165, 0 },
		{ TEXT("pink"), 255, 192, 203 },
		{ TEXT("brown"), 165, 42, 42 },
		{ TEXT("gold"), 255, 215, 0 },
	};

	struct FCssGradientKind
	{
		const TCHAR* Name;
		EDreamPaintType Type;
	};

	const FCssGradientKind CssGradientKinds[] =
	{
		{ TEXT("linear-gradient"), EDreamPaintType::Linear },
		{ TEXT("radial-gradient"), EDreamPaintType::Radial },
		{ TEXT("conic-gradient"), EDreamPaintType::Conic },
		{ TEXT("diamond-gradient"), EDreamPaintType::Diamond },
		{ TEXT("corners-gradient"), EDreamPaintType::Corners },
		{ TEXT("none-gradient"), EDreamPaintType::None },
	};

	const TCHAR* const CssRepeatingPrefix = TEXT("repeating-");
	const TCHAR* const CssReflectingPrefix = TEXT("reflecting-");

	bool IsCssSpace(TCHAR InChar)
	{
		return InChar == TEXT(' ') || InChar == TEXT('\t') || InChar == TEXT('\n') || InChar == TEXT('\r') || InChar == TEXT('\f');
	}

	bool IsCssDigit(TCHAR InChar)
	{
		return InChar >= TEXT('0') && InChar <= TEXT('9');
	}

	bool IsCssLetter(TCHAR InChar)
	{
		return (InChar >= TEXT('a') && InChar <= TEXT('z')) || (InChar >= TEXT('A') && InChar <= TEXT('Z'));
	}

	FString MakeCssError(int32 InOffset, const FString& InWhat)
	{
		return FString::Printf(TEXT("at character %d: %s"), InOffset, *InWhat);
	}

	/** InText less its spaces at both ends, its offset moved past the leading ones. */
	FCssPiece TrimCssPiece(const FString& InText, int32 InOffset)
	{
		int32 Start = 0;
		int32 End = InText.Len();
		while (Start < End && IsCssSpace(InText[Start]))
		{
			++Start;
		}
		while (End > Start && IsCssSpace(InText[End - 1]))
		{
			--End;
		}
		FCssPiece Piece;
		Piece.Text = InText.Mid(Start, End - Start);
		Piece.Offset = InOffset + Start;
		return Piece;
	}

	/** InInner's arguments: split at the commas outside brackets, each trimmed. An empty one is an error. */
	bool SplitCssArguments(const FCssPiece& InInner, TArray<FCssPiece>& OutArguments, FString& OutError)
	{
		if (TrimCssPiece(InInner.Text, InInner.Offset).Text.IsEmpty())
		{
			return true;
		}
		const int32 Length = InInner.Text.Len();
		int32 Depth = 0;
		int32 Start = 0;
		for (int32 CharIndex = 0; CharIndex <= Length; ++CharIndex)
		{
			const bool bEnd = CharIndex == Length;
			const TCHAR Current = bEnd ? TEXT(',') : InInner.Text[CharIndex];
			if (Current == TEXT('('))
			{
				++Depth;
			}
			else if (Current == TEXT(')'))
			{
				--Depth;
			}
			else if (Current == TEXT(',') && (Depth == 0 || bEnd))
			{
				FCssPiece Argument = TrimCssPiece(InInner.Text.Mid(Start, CharIndex - Start), InInner.Offset + Start);
				if (Argument.Text.IsEmpty())
				{
					OutError = MakeCssError(Argument.Offset, TEXT("an empty argument -- two commas in a row, or one at an end"));
					return false;
				}
				OutArguments.Add(MoveTemp(Argument));
				Start = CharIndex + 1;
			}
		}
		return true;
	}

	/** InArgument split at its spaces, what is inside a bracket kept whole. */
	void SplitCssTokens(const FCssPiece& InArgument, TArray<FCssPiece>& OutTokens)
	{
		const FString& Text = InArgument.Text;
		const int32 Length = Text.Len();
		int32 Cursor = 0;
		while (Cursor < Length)
		{
			while (Cursor < Length && IsCssSpace(Text[Cursor]))
			{
				++Cursor;
			}
			if (Cursor >= Length)
			{
				break;
			}
			const int32 Start = Cursor;
			int32 Depth = 0;
			while (Cursor < Length && (Depth > 0 || !IsCssSpace(Text[Cursor])))
			{
				if (Text[Cursor] == TEXT('('))
				{
					++Depth;
				}
				else if (Text[Cursor] == TEXT(')'))
				{
					Depth = FMath::Max(Depth - 1, 0);
				}
				++Cursor;
			}
			FCssPiece Token;
			Token.Text = Text.Mid(Start, Cursor - Start);
			Token.Offset = InArgument.Offset + Start;
			OutTokens.Add(MoveTemp(Token));
		}
	}

	/**
	 * A CSS number at the start of InText -- a sign, digits with at most one point, an exponent -- and the rest as its
	 * unit, which is letters or '%' alone. False when there is no number, the unit is anything else, or the value is not
	 * finite.
	 */
	bool ScanCssNumber(const FString& InText, double& OutValue, FString& OutUnit)
	{
		const int32 Length = InText.Len();
		int32 Cursor = 0;
		if (Cursor < Length && (InText[Cursor] == TEXT('+') || InText[Cursor] == TEXT('-')))
		{
			++Cursor;
		}
		int32 Digits = 0;
		while (Cursor < Length && IsCssDigit(InText[Cursor]))
		{
			++Cursor;
			++Digits;
		}
		if (Cursor < Length && InText[Cursor] == TEXT('.'))
		{
			++Cursor;
			while (Cursor < Length && IsCssDigit(InText[Cursor]))
			{
				++Cursor;
				++Digits;
			}
		}
		if (Digits == 0)
		{
			return false;
		}
		// An 'e' with digits after it is an exponent -- what PrintScalar writes for the very large and the very small --
		// and any other 'e' starts the unit.
		if (Cursor < Length && (InText[Cursor] == TEXT('e') || InText[Cursor] == TEXT('E')))
		{
			int32 Exponent = Cursor + 1;
			if (Exponent < Length && (InText[Exponent] == TEXT('+') || InText[Exponent] == TEXT('-')))
			{
				++Exponent;
			}
			if (Exponent < Length && IsCssDigit(InText[Exponent]))
			{
				while (Exponent < Length && IsCssDigit(InText[Exponent]))
				{
					++Exponent;
				}
				Cursor = Exponent;
			}
		}
		OutUnit = InText.Mid(Cursor);
		if (OutUnit != TEXT("%"))
		{
			for (const TCHAR UnitChar : OutUnit)
			{
				if (!IsCssLetter(UnitChar))
				{
					return false;
				}
			}
		}
		OutValue = FCString::Atod(*InText.Left(Cursor));
		return FMath::IsFinite(OutValue);
	}

	/** `25%` as 0.25: what a stop's position, a centre, a radius and an offset are written as. */
	bool ParseCssPercent(const FString& InToken, float& OutFraction)
	{
		double Value = 0.0;
		FString Unit;
		if (!ScanCssNumber(InToken, Value, Unit) || Unit != TEXT("%"))
		{
			return false;
		}
		OutFraction = static_cast<float>(Value / 100.0);
		return FMath::IsFinite(OutFraction);
	}

	/** A percentage, or a bare 0 (a length of nothing): a stop's position, or one of a <position>'s values. */
	bool ParseCssPlace(const FString& InToken, float& OutFraction)
	{
		double Value = 0.0;
		FString Unit;
		if (ScanCssNumber(InToken, Value, Unit) && Unit.IsEmpty() && Value == 0.0)
		{
			OutFraction = 0.0f;
			return true;
		}
		return ParseCssPercent(InToken, OutFraction);
	}

	/** An angle in deg, grad, rad or turn, or a bare 0, as degrees. */
	bool ParseCssAngle(const FString& InToken, float& OutDegrees)
	{
		double Value = 0.0;
		FString Unit;
		if (!ScanCssNumber(InToken, Value, Unit))
		{
			return false;
		}
		double Degrees = 0.0;
		if (Unit.Equals(TEXT("deg"), ESearchCase::IgnoreCase))
		{
			Degrees = Value;
		}
		else if (Unit.Equals(TEXT("grad"), ESearchCase::IgnoreCase))
		{
			Degrees = Value * 0.9;
		}
		else if (Unit.Equals(TEXT("rad"), ESearchCase::IgnoreCase))
		{
			Degrees = Value * (180.0 / UE_DOUBLE_PI);
		}
		else if (Unit.Equals(TEXT("turn"), ESearchCase::IgnoreCase))
		{
			Degrees = Value * 360.0;
		}
		else if (!(Unit.IsEmpty() && Value == 0.0))
		{
			return false;
		}
		OutDegrees = static_cast<float>(Degrees);
		return FMath::IsFinite(OutDegrees);
	}

	int32 CssHexValue(TCHAR InChar)
	{
		if (InChar >= TEXT('0') && InChar <= TEXT('9'))
		{
			return InChar - TEXT('0');
		}
		if (InChar >= TEXT('a') && InChar <= TEXT('f'))
		{
			return InChar - TEXT('a') + 10;
		}
		if (InChar >= TEXT('A') && InChar <= TEXT('F'))
		{
			return InChar - TEXT('A') + 10;
		}
		return INDEX_NONE;
	}

	/** #rgb, #rgba, #rrggbb or #rrggbbaa: the short forms double each digit, as CSS does. */
	bool ParseCssHexColor(const FString& InToken, FColor& OutColor)
	{
		const int32 DigitCount = InToken.Len() - 1;
		if (!InToken.StartsWith(TEXT("#")) || (DigitCount != 3 && DigitCount != 4 && DigitCount != 6 && DigitCount != 8))
		{
			return false;
		}
		const bool bShort = DigitCount <= 4;
		const int32 PerChannel = bShort ? 1 : 2;
		uint8 Channels[4] = { 0, 0, 0, 255 };
		for (int32 Channel = 0; Channel * PerChannel < DigitCount; ++Channel)
		{
			const int32 High = CssHexValue(InToken[1 + Channel * PerChannel]);
			const int32 Low = bShort ? High : CssHexValue(InToken[2 + Channel * PerChannel]);
			if (High == INDEX_NONE || Low == INDEX_NONE)
			{
				return false;
			}
			Channels[Channel] = static_cast<uint8>(High * 16 + Low);
		}
		OutColor = FColor(Channels[0], Channels[1], Channels[2], Channels[3]);
		return true;
	}

	/** One of rgb()'s colour channels: a number 0..255, or a percentage of 255; `none` is 0. */
	bool ParseCssRgbChannel(const FString& InText, uint8& OutByte)
	{
		if (InText.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutByte = 0;
			return true;
		}
		double Value = 0.0;
		FString Unit;
		if (!ScanCssNumber(InText, Value, Unit))
		{
			return false;
		}
		if (Unit == TEXT("%"))
		{
			Value *= 2.55;
		}
		else if (!Unit.IsEmpty())
		{
			return false;
		}
		OutByte = static_cast<uint8>(FMath::FloorToInt32(FMath::Clamp(Value, 0.0, 255.0) + 0.5));
		return true;
	}

	/** rgb()'s alpha: a number 0..1, or a percentage; `none` is 0. */
	bool ParseCssAlpha(const FString& InText, uint8& OutByte)
	{
		if (InText.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutByte = 0;
			return true;
		}
		double Value = 0.0;
		FString Unit;
		if (!ScanCssNumber(InText, Value, Unit))
		{
			return false;
		}
		if (Unit == TEXT("%"))
		{
			Value /= 100.0;
		}
		else if (!Unit.IsEmpty())
		{
			return false;
		}
		OutByte = static_cast<uint8>(FMath::FloorToInt32(FMath::Clamp(Value, 0.0, 1.0) * 255.0 + 0.5));
		return true;
	}

	/** rgb() or rgba(): three channels and an alpha, with commas (CSS's legacy form) or with spaces and '/'. */
	bool ParseCssRgbFunction(const FString& InToken, FColor& OutColor)
	{
		const int32 Open = InToken.Find(TEXT("("));
		if (Open == INDEX_NONE || !InToken.EndsWith(TEXT(")")))
		{
			return false;
		}
		const FString Name = InToken.Left(Open);
		if (!Name.Equals(TEXT("rgb"), ESearchCase::IgnoreCase) && !Name.Equals(TEXT("rgba"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		const FString Inner = InToken.Mid(Open + 1, InToken.Len() - Open - 2);
		TArray<FString> Channels;
		FString AlphaText;
		bool bHasAlpha = false;
		if (Inner.Contains(TEXT(",")))
		{
			TArray<FString> Parts;
			Inner.ParseIntoArray(Parts, TEXT(","), /*bInCullEmpty*/ false);
			if (Parts.Num() != 3 && Parts.Num() != 4)
			{
				return false;
			}
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
			}
			Channels = { Parts[0], Parts[1], Parts[2] };
			if (Parts.Num() == 4)
			{
				AlphaText = Parts[3];
				bHasAlpha = true;
			}
		}
		else
		{
			FString ChannelText = Inner;
			FString AfterSlash;
			if (Inner.Split(TEXT("/"), &ChannelText, &AfterSlash))
			{
				AlphaText = AfterSlash.TrimStartAndEnd();
				bHasAlpha = true;
				if (AlphaText.IsEmpty() || AlphaText.Contains(TEXT("/")) || AlphaText.Contains(TEXT(" ")) || AlphaText.Contains(TEXT("\t")))
				{
					return false;
				}
			}
			ChannelText.ParseIntoArrayWS(Channels);
			if (Channels.Num() != 3)
			{
				return false;
			}
		}
		uint8 Bytes[4] = { 0, 0, 0, 255 };
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			if (!ParseCssRgbChannel(Channels[Channel], Bytes[Channel]))
			{
				return false;
			}
		}
		if (bHasAlpha && !ParseCssAlpha(AlphaText, Bytes[3]))
		{
			return false;
		}
		OutColor = FColor(Bytes[0], Bytes[1], Bytes[2], Bytes[3]);
		return true;
	}

	bool ParseCssColor(const FString& InToken, FColor& OutColor)
	{
		if (InToken.StartsWith(TEXT("#")))
		{
			return ParseCssHexColor(InToken, OutColor);
		}
		if (InToken.Contains(TEXT("(")))
		{
			return ParseCssRgbFunction(InToken, OutColor);
		}
		if (InToken.Equals(TEXT("transparent"), ESearchCase::IgnoreCase))
		{
			OutColor = FColor(0, 0, 0, 0);
			return true;
		}
		for (const FCssNamedColor& Named : CssNamedColors)
		{
			if (InToken.Equals(Named.Name, ESearchCase::IgnoreCase))
			{
				OutColor = FColor(Named.R, Named.G, Named.B, 255);
				return true;
			}
		}
		return false;
	}

	/** Whether InToken starts a stop rather than the prelude: a colour, or something spelled as one (a broken #hex). */
	bool LooksLikeCssColor(const FString& InToken)
	{
		FColor Unused;
		return InToken.StartsWith(TEXT("#")) || InToken.StartsWith(TEXT("rgb"), ESearchCase::IgnoreCase) || ParseCssColor(InToken, Unused);
	}

	/** Which axis a <position> keyword can stand for. */
	enum class ECssPositionAxis : uint8
	{
		Either,
		Horizontal,
		Vertical,
	};

	bool ParseCssPositionKeyword(const FString& InToken, float& OutFraction, ECssPositionAxis& OutAxis)
	{
		struct FKeyword
		{
			const TCHAR* Name;
			float Fraction;
			ECssPositionAxis Axis;
		};
		static const FKeyword Keywords[] =
		{
			{ TEXT("left"), 0.0f, ECssPositionAxis::Horizontal },
			{ TEXT("right"), 1.0f, ECssPositionAxis::Horizontal },
			{ TEXT("top"), 0.0f, ECssPositionAxis::Vertical },
			{ TEXT("bottom"), 1.0f, ECssPositionAxis::Vertical },
			{ TEXT("center"), 0.5f, ECssPositionAxis::Either },
		};
		for (const FKeyword& Keyword : Keywords)
		{
			if (InToken.Equals(Keyword.Name, ESearchCase::IgnoreCase))
			{
				OutFraction = Keyword.Fraction;
				OutAxis = Keyword.Axis;
				return true;
			}
		}
		return false;
	}

	bool IsCssPositionToken(const FString& InToken)
	{
		float Unused = 0.0f;
		ECssPositionAxis UnusedAxis = ECssPositionAxis::Either;
		return ParseCssPositionKeyword(InToken, Unused, UnusedAxis) || ParseCssPlace(InToken, Unused);
	}

	/** Two values that each stand for an axis, in either order (`top left`): which is x, which is y. */
	bool PlaceCssPositionPair(float InFirst, ECssPositionAxis InFirstAxis, float InSecond, ECssPositionAxis InSecondAxis, FVector2f& OutCenter)
	{
		if (InFirstAxis == ECssPositionAxis::Vertical || InSecondAxis == ECssPositionAxis::Horizontal)
		{
			if (InFirstAxis == ECssPositionAxis::Horizontal || InSecondAxis == ECssPositionAxis::Vertical)
			{
				return false;
			}
			OutCenter = FVector2f(InSecond, InFirst);
			return true;
		}
		OutCenter = FVector2f(InFirst, InSecond);
		return true;
	}

	/** CSS's <position>: one value, two, or an edge keyword with its offset for each axis. */
	bool ParseCssPosition(const TArray<FCssPiece>& InTokens, FVector2f& OutCenter)
	{
		const int32 Count = InTokens.Num();
		if (Count == 1)
		{
			float Fraction = 0.0f;
			ECssPositionAxis Axis = ECssPositionAxis::Either;
			if (ParseCssPositionKeyword(InTokens[0].Text, Fraction, Axis))
			{
				OutCenter = Axis == ECssPositionAxis::Vertical ? FVector2f(0.5f, Fraction) : FVector2f(Fraction, 0.5f);
				return true;
			}
			if (ParseCssPlace(InTokens[0].Text, Fraction))
			{
				OutCenter = FVector2f(Fraction, 0.5f);
				return true;
			}
			return false;
		}
		if (Count == 2)
		{
			float First = 0.0f;
			float Second = 0.0f;
			ECssPositionAxis FirstAxis = ECssPositionAxis::Either;
			ECssPositionAxis SecondAxis = ECssPositionAxis::Either;
			const bool bFirstKeyword = ParseCssPositionKeyword(InTokens[0].Text, First, FirstAxis);
			const bool bSecondKeyword = ParseCssPositionKeyword(InTokens[1].Text, Second, SecondAxis);
			if (bFirstKeyword && bSecondKeyword)
			{
				return PlaceCssPositionPair(First, FirstAxis, Second, SecondAxis, OutCenter);
			}
			// A percentage fixes the order: the first value is x, the second y.
			const bool bFirstFits = bFirstKeyword ? FirstAxis != ECssPositionAxis::Vertical : ParseCssPlace(InTokens[0].Text, First);
			const bool bSecondFits = bSecondKeyword ? SecondAxis != ECssPositionAxis::Horizontal : ParseCssPlace(InTokens[1].Text, Second);
			if (!bFirstFits || !bSecondFits)
			{
				return false;
			}
			OutCenter = FVector2f(First, Second);
			return true;
		}
		if (Count == 3 || Count == 4)
		{
			// `right 10% bottom 20%`: an edge keyword and its offset from that edge for each axis; `center` has none.
			float Values[2] = { 0.5f, 0.5f };
			ECssPositionAxis Axes[2] = { ECssPositionAxis::Either, ECssPositionAxis::Either };
			int32 Groups = 0;
			int32 TokenIndex = 0;
			while (TokenIndex < Count)
			{
				if (Groups == 2)
				{
					return false;
				}
				float Edge = 0.0f;
				ECssPositionAxis Axis = ECssPositionAxis::Either;
				if (!ParseCssPositionKeyword(InTokens[TokenIndex].Text, Edge, Axis))
				{
					return false;
				}
				++TokenIndex;
				float FromEdge = 0.0f;
				if (Axis != ECssPositionAxis::Either && TokenIndex < Count && ParseCssPlace(InTokens[TokenIndex].Text, FromEdge))
				{
					++TokenIndex;
					Edge = Edge == 0.0f ? FromEdge : 1.0f - FromEdge;
				}
				Values[Groups] = Edge;
				Axes[Groups] = Axis;
				++Groups;
			}
			return Groups == 2 && PlaceCssPositionPair(Values[0], Axes[0], Values[1], Axes[1], OutCenter);
		}
		return false;
	}

	/** The CSS angle `to` a side or a corner points at; a corner is the square's (see ParseCss). */
	float GetCssSideAngle(int32 InAcross, int32 InDown)
	{
		if (InAcross == 0)
		{
			return InDown < 0 ? 0.0f : 180.0f;
		}
		if (InDown == 0)
		{
			return InAcross > 0 ? 90.0f : 270.0f;
		}
		if (InAcross > 0)
		{
			return InDown < 0 ? 45.0f : 135.0f;
		}
		return InDown > 0 ? 225.0f : 315.0f;
	}

	/** The prelude's items into InOutGradient, each at most once. */
	bool ParseCssPrelude(const TArray<FCssPiece>& InTokens, FDreamGradient& InOutGradient, FString& OutError)
	{
		bool bAngleSeen = false;
		bool bCenterSeen = false;
		bool bShapeSeen = false;
		bool bSizeSeen = false;
		bool bRadiusSeen = false;
		bool bSpaceSeen = false;
		bool bScaleSeen = false;
		bool bOffsetSeen = false;
		auto Once = [&OutError](bool& bInOutSeen, const FCssPiece& InAt, const TCHAR* InWhat) -> bool
		{
			if (bInOutSeen)
			{
				OutError = MakeCssError(InAt.Offset, FString::Printf(TEXT("%s is given twice"), InWhat));
				return false;
			}
			bInOutSeen = true;
			return true;
		};
		auto Fail = [&OutError](const FCssPiece& InAt, const FString& InWhat) -> bool
		{
			OutError = MakeCssError(InAt.Offset, InWhat);
			return false;
		};
		// One or two percentages from InOutIndex on: a radius, of the width and then of the height.
		auto ReadRadius = [&InTokens](int32& InOutIndex, FVector2f& InOutRadius) -> bool
		{
			float Across = 0.0f;
			if (InOutIndex >= InTokens.Num() || !ParseCssPercent(InTokens[InOutIndex].Text, Across))
			{
				return false;
			}
			InOutRadius.X = Across;
			++InOutIndex;
			float Down = 0.0f;
			if (InOutIndex < InTokens.Num() && ParseCssPercent(InTokens[InOutIndex].Text, Down))
			{
				InOutRadius.Y = Down;
				++InOutIndex;
			}
			return true;
		};

		int32 TokenIndex = 0;
		while (TokenIndex < InTokens.Num())
		{
			const FCssPiece& Token = InTokens[TokenIndex];
			const FString& Word = Token.Text;
			float Number = 0.0f;
			if (Word.Equals(TEXT("to"), ESearchCase::IgnoreCase))
			{
				if (!Once(bAngleSeen, Token, TEXT("the angle")))
				{
					return false;
				}
				++TokenIndex;
				int32 Across = 0;
				int32 Down = 0;
				int32 Sides = 0;
				while (TokenIndex < InTokens.Num() && Sides < 2)
				{
					const FString& Side = InTokens[TokenIndex].Text;
					if (Down == 0 && Side.Equals(TEXT("top"), ESearchCase::IgnoreCase))
					{
						Down = -1;
					}
					else if (Down == 0 && Side.Equals(TEXT("bottom"), ESearchCase::IgnoreCase))
					{
						Down = 1;
					}
					else if (Across == 0 && Side.Equals(TEXT("left"), ESearchCase::IgnoreCase))
					{
						Across = -1;
					}
					else if (Across == 0 && Side.Equals(TEXT("right"), ESearchCase::IgnoreCase))
					{
						Across = 1;
					}
					else
					{
						break;
					}
					++Sides;
					++TokenIndex;
				}
				if (Sides == 0)
				{
					return Fail(Token, TEXT("'to' needs a side: top, right, bottom or left, or two of them for a corner"));
				}
				InOutGradient.Angle = GetCssSideAngle(Across, Down);
				continue;
			}
			if (Word.Equals(TEXT("from"), ESearchCase::IgnoreCase))
			{
				if (!Once(bAngleSeen, Token, TEXT("the angle")))
				{
					return false;
				}
				if (TokenIndex + 1 >= InTokens.Num() || !ParseCssAngle(InTokens[TokenIndex + 1].Text, Number))
				{
					return Fail(Token, TEXT("'from' needs an angle: deg, grad, rad or turn"));
				}
				InOutGradient.Angle = Number;
				TokenIndex += 2;
				continue;
			}
			if (Word.Equals(TEXT("at"), ESearchCase::IgnoreCase))
			{
				if (!Once(bCenterSeen, Token, TEXT("the position")))
				{
					return false;
				}
				++TokenIndex;
				TArray<FCssPiece> Position;
				while (TokenIndex < InTokens.Num() && Position.Num() < 4 && IsCssPositionToken(InTokens[TokenIndex].Text))
				{
					Position.Add(InTokens[TokenIndex]);
					++TokenIndex;
				}
				FVector2f Center = FVector2f(0.5f, 0.5f);
				if (!ParseCssPosition(Position, Center))
				{
					return Fail(Token, TEXT("'at' needs a position: left, center, right, top, bottom and percentages, one, two or four of them"));
				}
				InOutGradient.Center = Center;
				continue;
			}
			if (Word.Equals(TEXT("in"), ESearchCase::IgnoreCase))
			{
				if (!Once(bSpaceSeen, Token, TEXT("the colour space")))
				{
					return false;
				}
				const FString Space = TokenIndex + 1 < InTokens.Num() ? InTokens[TokenIndex + 1].Text : FString();
				if (Space.Equals(TEXT("srgb"), ESearchCase::IgnoreCase))
				{
					InOutGradient.Interpolation = EDreamPaintInterpolation::SRGB;
				}
				else if (Space.Equals(TEXT("srgb-linear"), ESearchCase::IgnoreCase))
				{
					InOutGradient.Interpolation = EDreamPaintInterpolation::Linear;
				}
				else if (Space.Equals(TEXT("oklab"), ESearchCase::IgnoreCase))
				{
					InOutGradient.Interpolation = EDreamPaintInterpolation::Oklab;
				}
				else
				{
					return Fail(Token, FString::Printf(TEXT("'in %s': the colour spaces are srgb, srgb-linear and oklab"), *Space));
				}
				TokenIndex += 2;
				continue;
			}
			if (Word.Equals(TEXT("circle"), ESearchCase::IgnoreCase) || Word.Equals(TEXT("ellipse"), ESearchCase::IgnoreCase))
			{
				if (!Once(bShapeSeen, Token, TEXT("the shape")))
				{
					return false;
				}
				InOutGradient.Shape = Word.Equals(TEXT("circle"), ESearchCase::IgnoreCase) ? EDreamPaintRadialShape::Circle : EDreamPaintRadialShape::Ellipse;
				++TokenIndex;
				continue;
			}
			{
				struct FSizeKeyword
				{
					const TCHAR* Name;
					EDreamPaintRadialSize Size;
				};
				static const FSizeKeyword SizeKeywords[] =
				{
					{ TEXT("closest-side"), EDreamPaintRadialSize::ClosestSide },
					{ TEXT("closest-corner"), EDreamPaintRadialSize::ClosestCorner },
					{ TEXT("farthest-side"), EDreamPaintRadialSize::FarthestSide },
					{ TEXT("farthest-corner"), EDreamPaintRadialSize::FarthestCorner },
				};
				const FSizeKeyword* Found = nullptr;
				for (const FSizeKeyword& Keyword : SizeKeywords)
				{
					if (Word.Equals(Keyword.Name, ESearchCase::IgnoreCase))
					{
						Found = &Keyword;
						break;
					}
				}
				if (Found != nullptr)
				{
					if (!Once(bSizeSeen, Token, TEXT("the size")))
					{
						return false;
					}
					InOutGradient.Size = Found->Size;
					++TokenIndex;
					continue;
				}
			}
			if (Word.Equals(TEXT("radius"), ESearchCase::IgnoreCase))
			{
				if (!Once(bRadiusSeen, Token, TEXT("the radius")))
				{
					return false;
				}
				++TokenIndex;
				if (!ReadRadius(TokenIndex, InOutGradient.Radius))
				{
					return Fail(Token, TEXT("'radius' needs one or two percentages"));
				}
				continue;
			}
			if (Word.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
			{
				if (!Once(bScaleSeen, Token, TEXT("the scale")))
				{
					return false;
				}
				double Value = 0.0;
				FString Unit;
				if (TokenIndex + 1 >= InTokens.Num() || !ScanCssNumber(InTokens[TokenIndex + 1].Text, Value, Unit) || !Unit.IsEmpty()
					|| !FMath::IsFinite(static_cast<float>(Value)))
				{
					return Fail(Token, TEXT("'scale' needs a number"));
				}
				InOutGradient.Scale = static_cast<float>(Value);
				TokenIndex += 2;
				continue;
			}
			if (Word.Equals(TEXT("offset"), ESearchCase::IgnoreCase))
			{
				if (!Once(bOffsetSeen, Token, TEXT("the offset")))
				{
					return false;
				}
				double Value = 0.0;
				FString Unit;
				const FString Next = TokenIndex + 1 < InTokens.Num() ? InTokens[TokenIndex + 1].Text : FString();
				if (ParseCssPercent(Next, Number))
				{
					InOutGradient.Offset = Number;
				}
				else if (ScanCssNumber(Next, Value, Unit) && Unit.IsEmpty() && FMath::IsFinite(static_cast<float>(Value)))
				{
					InOutGradient.Offset = static_cast<float>(Value);
				}
				else
				{
					return Fail(Token, TEXT("'offset' needs a percentage of the gradient's length, or a fraction of it"));
				}
				TokenIndex += 2;
				continue;
			}
			if (ParseCssAngle(Word, Number))
			{
				if (!Once(bAngleSeen, Token, TEXT("the angle")))
				{
					return false;
				}
				InOutGradient.Angle = Number;
				++TokenIndex;
				continue;
			}
			if (ParseCssPercent(Word, Number))
			{
				// An explicit size: the radii themselves.
				if (!Once(bSizeSeen, Token, TEXT("the size")) || !Once(bRadiusSeen, Token, TEXT("the radius")))
				{
					return false;
				}
				InOutGradient.Size = EDreamPaintRadialSize::Explicit;
				ReadRadius(TokenIndex, InOutGradient.Radius);
				continue;
			}
			return Fail(Token, FString::Printf(TEXT("'%s' is not something a gradient's first argument holds (an angle, to, from, at, in, ")
				TEXT("a shape or size, a radius, scale, offset); if it is the first colour, a comma is missing before it"), *Word));
		}
		return true;
	}

	/** One stop argument: a colour and up to two positions, two being a band of that colour. */
	bool ParseCssStop(const FCssPiece& InArgument, TArray<FCssStop>& OutStops, FString& OutError)
	{
		TArray<FCssPiece> Tokens;
		SplitCssTokens(InArgument, Tokens);
		if (Tokens.Num() == 0)
		{
			OutError = MakeCssError(InArgument.Offset, TEXT("an empty stop"));
			return false;
		}
		FCssStop Stop;
		if (!ParseCssColor(Tokens[0].Text, Stop.Color))
		{
			float Unused = 0.0f;
			OutError = MakeCssError(Tokens[0].Offset, Tokens.Num() == 1 && ParseCssPlace(Tokens[0].Text, Unused)
				? FString(TEXT("a position with no colour: midpoint hints are not read"))
				: FString::Printf(TEXT("'%s' is not a colour (#hex, rgb(), rgba(), a CSS name or transparent)"), *Tokens[0].Text));
			return false;
		}
		if (Tokens.Num() > 3)
		{
			OutError = MakeCssError(Tokens[3].Offset, TEXT("a stop is a colour and at most two positions"));
			return false;
		}
		if (Tokens.Num() == 1)
		{
			OutStops.Add(Stop);
			return true;
		}
		for (int32 TokenIndex = 1; TokenIndex < Tokens.Num(); ++TokenIndex)
		{
			if (!ParseCssPlace(Tokens[TokenIndex].Text, Stop.Position))
			{
				OutError = MakeCssError(Tokens[TokenIndex].Offset,
					FString::Printf(TEXT("'%s' is not a position: stops are placed in percentages"), *Tokens[TokenIndex].Text));
				return false;
			}
			Stop.bHasPosition = true;
			OutStops.Add(Stop);
		}
		return true;
	}

	/**
	 * Stops with no position placed as CSS places them: the first at 0, the last at 1, each run between two placed stops
	 * spread evenly between them -- each placed stop counting as at least the largest position before it, CSS's fix-up
	 * coming first. Written positions are left as written (the fix-up is the drawing's, PrepareGradient).
	 */
	void PlaceCssStops(TArray<FCssStop>& InOutStops)
	{
		const int32 Count = InOutStops.Num();
		if (Count == 0)
		{
			return;
		}
		if (!InOutStops[0].bHasPosition)
		{
			InOutStops[0].bHasPosition = true;
			InOutStops[0].Position = 0.0f;
		}
		if (!InOutStops.Last().bHasPosition)
		{
			InOutStops.Last().bHasPosition = true;
			InOutStops.Last().Position = 1.0f;
		}
		float Largest = InOutStops[0].Position;
		int32 StopIndex = 1;
		while (StopIndex < Count)
		{
			if (InOutStops[StopIndex].bHasPosition)
			{
				Largest = FMath::Max(Largest, InOutStops[StopIndex].Position);
				++StopIndex;
				continue;
			}
			int32 RunEnd = StopIndex;
			while (!InOutStops[RunEnd].bHasPosition)
			{
				++RunEnd;
			}
			const float From = Largest;
			const float To = FMath::Max(Largest, InOutStops[RunEnd].Position);
			const int32 Steps = RunEnd - StopIndex + 1;
			for (int32 Step = 1; Step < Steps; ++Step)
			{
				FCssStop& Placed = InOutStops[StopIndex + Step - 1];
				Placed.Position = From + (To - From) * (static_cast<float>(Step) / static_cast<float>(Steps));
				Placed.bHasPosition = true;
			}
			StopIndex = RunEnd;
		}
	}

	bool ParseCssGradient(const FString& InCss, FDreamGradient& OutGradient, FString& OutError)
	{
		const FCssPiece Whole = TrimCssPiece(InCss, 0);
		if (Whole.Text.IsEmpty())
		{
			OutError = MakeCssError(0, TEXT("there is no gradient: the text is empty"));
			return false;
		}
		if (Whole.Text.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			FDreamGradient Nothing;
			Nothing.Type = EDreamPaintType::None;
			OutGradient = Nothing;
			return true;
		}
		const int32 Open = Whole.Text.Find(TEXT("("));
		if (Open == INDEX_NONE)
		{
			OutError = MakeCssError(Whole.Offset, FString::Printf(TEXT("'%s' is not a gradient: one is written as a function, linear-gradient(...)"), *Whole.Text));
			return false;
		}
		int32 Depth = 0;
		int32 Close = INDEX_NONE;
		for (int32 CharIndex = Open; CharIndex < Whole.Text.Len(); ++CharIndex)
		{
			if (Whole.Text[CharIndex] == TEXT('('))
			{
				++Depth;
			}
			else if (Whole.Text[CharIndex] == TEXT(')') && --Depth == 0)
			{
				Close = CharIndex;
				break;
			}
		}
		if (Close == INDEX_NONE)
		{
			OutError = MakeCssError(Whole.Offset + Open, TEXT("this bracket is never closed"));
			return false;
		}
		if (Close != Whole.Text.Len() - 1)
		{
			OutError = MakeCssError(Whole.Offset + Close + 1, TEXT("there is more after the gradient's closing bracket"));
			return false;
		}

		FString Name = Whole.Text.Left(Open);
		EDreamPaintSpread Spread = EDreamPaintSpread::Pad;
		if (Name.StartsWith(CssRepeatingPrefix, ESearchCase::IgnoreCase))
		{
			Spread = EDreamPaintSpread::Repeat;
			Name.RightChopInline(FCString::Strlen(CssRepeatingPrefix));
		}
		else if (Name.StartsWith(CssReflectingPrefix, ESearchCase::IgnoreCase))
		{
			Spread = EDreamPaintSpread::Reflect;
			Name.RightChopInline(FCString::Strlen(CssReflectingPrefix));
		}
		const FCssGradientKind* Kind = nullptr;
		for (const FCssGradientKind& Candidate : CssGradientKinds)
		{
			if (Name.Equals(Candidate.Name, ESearchCase::IgnoreCase))
			{
				Kind = &Candidate;
				break;
			}
		}
		if (Kind == nullptr)
		{
			OutError = MakeCssError(Whole.Offset, FString::Printf(TEXT("'%s' is not a gradient this reads: linear-, radial-, conic-, diamond- or ")
				TEXT("corners-gradient, each with repeating- or reflecting- before it if wanted"), *Whole.Text.Left(Open)));
			return false;
		}

		FDreamGradient Result;
		Result.Type = Kind->Type;
		Result.Spread = Spread;
		if (Kind->Type == EDreamPaintType::Conic)
		{
			Result.Angle = 0.0f;
		}
		FCssPiece Inner;
		Inner.Text = Whole.Text.Mid(Open + 1, Close - Open - 1);
		Inner.Offset = Whole.Offset + Open + 1;
		TArray<FCssPiece> Arguments;
		if (!SplitCssArguments(Inner, Arguments, OutError))
		{
			return false;
		}
		int32 FirstStop = 0;
		if (Arguments.Num() > 0)
		{
			TArray<FCssPiece> Tokens;
			SplitCssTokens(Arguments[0], Tokens);
			if (Tokens.Num() > 0 && !LooksLikeCssColor(Tokens[0].Text))
			{
				if (!ParseCssPrelude(Tokens, Result, OutError))
				{
					return false;
				}
				FirstStop = 1;
			}
		}
		TArray<FCssStop> CssStops;
		for (int32 ArgumentIndex = FirstStop; ArgumentIndex < Arguments.Num(); ++ArgumentIndex)
		{
			if (!ParseCssStop(Arguments[ArgumentIndex], CssStops, OutError))
			{
				return false;
			}
		}
		PlaceCssStops(CssStops);
		Result.Stops.Reserve(CssStops.Num());
		for (const FCssStop& Stop : CssStops)
		{
			Result.Stops.Add(FDreamGradientStop(Stop.Position, Stop.Color));
		}
		OutGradient = MoveTemp(Result);
		return true;
	}

	// ---------------------------------------------------------------- writing

	FString PrintCssNumber(float InValue)
	{
		return DreamUIValueFormat::PrintScalar(InValue, /*bSinglePrecision*/ true);
	}

	/**
	 * A fraction as the percentage that reads back as exactly that fraction, ParseCssPercent's (float)(Atod / 100): the
	 * shortest fixed spelling that does, then the shortest exponent one -- the search PrintScalar makes, checked against
	 * the reading this file does.
	 */
	FString PrintCssPercent(float InFraction)
	{
		if (InFraction == 0.0f)
		{
			return TEXT("0%");
		}
		const double Percent = static_cast<double>(InFraction) * 100.0;
		auto ReadsBack = [InFraction](const FString& InCandidate)
		{
			return static_cast<float>(FCString::Atod(*InCandidate) / 100.0) == InFraction;
		};
		if (FMath::Abs(Percent) < 1.0e17)
		{
			for (int32 FractionalDigits = 0; FractionalDigits <= 24; ++FractionalDigits)
			{
				const FString Candidate = FString::Printf(TEXT("%.*f"), FractionalDigits, Percent);
				if (ReadsBack(Candidate))
				{
					return Candidate + TEXT("%");
				}
			}
		}
		for (int32 SignificantDigits = 1; SignificantDigits <= 17; ++SignificantDigits)
		{
			const FString Candidate = FString::Printf(TEXT("%.*g"), SignificantDigits, Percent);
			if (ReadsBack(Candidate))
			{
				return Candidate + TEXT("%");
			}
		}
		return FString::Printf(TEXT("%.17g%%"), Percent);
	}

	FString PrintCssColor(const FColor& InColor)
	{
		return InColor.A == 255
			? FString::Printf(TEXT("#%02X%02X%02X"), InColor.R, InColor.G, InColor.B)
			: FString::Printf(TEXT("#%02X%02X%02X%02X"), InColor.R, InColor.G, InColor.B, InColor.A);
	}

	const TCHAR* GetCssSizeKeyword(EDreamPaintRadialSize InSize)
	{
		switch (InSize)
		{
		case EDreamPaintRadialSize::ClosestSide: return TEXT("closest-side");
		case EDreamPaintRadialSize::ClosestCorner: return TEXT("closest-corner");
		case EDreamPaintRadialSize::FarthestSide: return TEXT("farthest-side");
		case EDreamPaintRadialSize::FarthestCorner: return TEXT("farthest-corner");
		default: return nullptr;
		}
	}

	const TCHAR* GetCssKindName(EDreamPaintType InType)
	{
		for (const FCssGradientKind& Kind : CssGradientKinds)
		{
			if (Kind.Type == InType)
			{
				return Kind.Name;
			}
		}
		return TEXT("none-gradient");
	}

	/** Which stop positions a spelling leaves for CSS to place. */
	enum class ECssPrintedPositions : uint8
	{
		/** None: every stop where CSS would place it. */
		Nothing,
		/** All but a first at 0% and a last at 100%. */
		Inner,
		All,
	};

	FString PrintCssGradient(const FDreamGradient& InGradient, ECssPrintedPositions InPositions)
	{
		FString Name;
		if (InGradient.Spread == EDreamPaintSpread::Repeat)
		{
			Name = CssRepeatingPrefix;
		}
		else if (InGradient.Spread == EDreamPaintSpread::Reflect)
		{
			Name = CssReflectingPrefix;
		}
		Name += GetCssKindName(InGradient.Type);

		// The prelude in CSS's order -- the angle, the shape and size, the position, the colour space last -- with what
		// CSS has no word for between: written only where it differs from what reading would start from.
		TArray<FString> Prelude;
		const bool bConic = InGradient.Type == EDreamPaintType::Conic;
		if (InGradient.Angle != (bConic ? 0.0f : 180.0f))
		{
			Prelude.Add(FString::Printf(TEXT("%s%sdeg"), bConic ? TEXT("from ") : TEXT(""), *PrintCssNumber(InGradient.Angle)));
		}
		if (InGradient.Shape == EDreamPaintRadialShape::Circle)
		{
			Prelude.Add(TEXT("circle"));
		}
		if (InGradient.Size == EDreamPaintRadialSize::Explicit)
		{
			// A circle's radius is one value, of the width; a second is written only when it carries something.
			const bool bOneValue = InGradient.Shape == EDreamPaintRadialShape::Circle && InGradient.Radius.Y == 0.5f;
			Prelude.Add(bOneValue ? PrintCssPercent(InGradient.Radius.X)
				: PrintCssPercent(InGradient.Radius.X) + TEXT(" ") + PrintCssPercent(InGradient.Radius.Y));
		}
		else if (InGradient.Size != EDreamPaintRadialSize::FarthestCorner)
		{
			if (const TCHAR* Keyword = GetCssSizeKeyword(InGradient.Size))
			{
				Prelude.Add(Keyword);
			}
		}
		if (InGradient.Center != FVector2f(0.5f, 0.5f))
		{
			Prelude.Add(FString::Printf(TEXT("at %s %s"), *PrintCssPercent(InGradient.Center.X), *PrintCssPercent(InGradient.Center.Y)));
		}
		if (InGradient.Size != EDreamPaintRadialSize::Explicit && InGradient.Radius != FVector2f(0.5f, 0.5f))
		{
			Prelude.Add(FString::Printf(TEXT("radius %s %s"), *PrintCssPercent(InGradient.Radius.X), *PrintCssPercent(InGradient.Radius.Y)));
		}
		if (InGradient.Scale != 1.0f)
		{
			Prelude.Add(FString::Printf(TEXT("scale %s"), *PrintCssNumber(InGradient.Scale)));
		}
		if (InGradient.Offset != 0.0f)
		{
			Prelude.Add(FString::Printf(TEXT("offset %s"), *PrintCssPercent(InGradient.Offset)));
		}
		if (InGradient.Interpolation == EDreamPaintInterpolation::Linear)
		{
			Prelude.Add(TEXT("in srgb-linear"));
		}
		else if (InGradient.Interpolation == EDreamPaintInterpolation::Oklab)
		{
			Prelude.Add(TEXT("in oklab"));
		}

		TArray<FString> Arguments;
		if (Prelude.Num() > 0)
		{
			Arguments.Add(FString::Join(Prelude, TEXT(" ")));
		}
		const int32 Last = InGradient.Stops.Num() - 1;
		for (int32 StopIndex = 0; StopIndex <= Last; ++StopIndex)
		{
			const FDreamGradientStop& Stop = InGradient.Stops[StopIndex];
			bool bWritePosition = InPositions == ECssPrintedPositions::All;
			if (InPositions == ECssPrintedPositions::Inner)
			{
				// What reading places a stop with no position at: the first at 0 (a lone stop too), the last at 1.
				const float Placed = StopIndex == 0 ? 0.0f : (StopIndex == Last ? 1.0f : -1.0f);
				bWritePosition = (StopIndex != 0 && StopIndex != Last) || Stop.Position != Placed;
			}
			Arguments.Add(bWritePosition ? PrintCssColor(Stop.Color) + TEXT(" ") + PrintCssPercent(Stop.Position) : PrintCssColor(Stop.Color));
		}
		return FString::Printf(TEXT("%s(%s)"), *Name, *FString::Join(Arguments, TEXT(", ")));
	}
}

bool FDreamGradient::ParseCss(const FString& InCss, FDreamGradient& OutGradient, FString* OutError)
{
	FString Error;
	FDreamGradient Parsed;
	if (!DreamTextPaintCssLocal::ParseCssGradient(InCss, Parsed, Error))
	{
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return false;
	}
	OutGradient = MoveTemp(Parsed);
	if (OutError != nullptr)
	{
		OutError->Reset();
	}
	return true;
}

FString FDreamGradient::ToCss() const
{
	using namespace DreamTextPaintCssLocal;
	FDreamGradient Nothing;
	Nothing.Type = EDreamPaintType::None;
	if (*this == Nothing)
	{
		return TEXT("none");
	}
	// The plainest spelling that reads back as this gradient, field for field: the stops' positions left for CSS to place
	// where it would place them the same, then the ends left out, then every position written.
	const ECssPrintedPositions Attempts[] = { ECssPrintedPositions::Nothing, ECssPrintedPositions::Inner, ECssPrintedPositions::All };
	for (const ECssPrintedPositions Attempt : Attempts)
	{
		FString Spelling = PrintCssGradient(*this, Attempt);
		FDreamGradient ReadBack;
		if (Attempt == ECssPrintedPositions::All || (ParseCss(Spelling, ReadBack) && ReadBack == *this))
		{
			return Spelling;
		}
	}
	return PrintCssGradient(*this, ECssPrintedPositions::All);
}

const FDreamGradient& FDreamTextPaint::GetEffectiveGradient() const
{
	const UDreamGradientAsset* Asset = Preset.Get();
	return IsValid(Asset) ? Asset->GetGradient() : Gradient;
}

bool FDreamTextPaint::IsPainting() const
{
	return bEnabled && GetEffectiveGradient().IsPainting();
}

bool FDreamTextPaint::ResolveTagPaint(FName InName, const UDreamUIRichTextCustomStyleData* InStyles, FDreamGradient& OutGradient)
{
	if (InName.IsNone())
	{
		return false;
	}
	// The custom style entry of that name first, its paint whether or not it says it is on: the entry's paintType is what
	// turns it on.
	if (IsValid(InStyles))
	{
		if (const FDreamUIRichTextCustomStyleItemData* Entry = InStyles->GetDataMap().Find(InName))
		{
			if (Entry->paintType == EDreamUIRichTextCustomStyleData_PaintType::Set)
			{
				const FDreamGradient& Styled = Entry->paint.GetEffectiveGradient();
				if (Styled.IsPainting())
				{
					OutGradient = Styled;
					return true;
				}
			}
		}
	}
	// Then the project's preset of that name, then the name itself as CSS.
	FDreamGradient Found;
	if (DreamGradientPresets::FindPreset(InName, Found) && Found.IsPainting())
	{
		OutGradient = MoveTemp(Found);
		return true;
	}
	if (DreamGradientPresets::ParseCssCached(InName.ToString(), Found) && Found.IsPainting())
	{
		OutGradient = MoveTemp(Found);
		return true;
	}
	return false;
}

bool FDreamTextPaint::operator==(const FDreamTextPaint& Other) const
{
	return bEnabled == Other.bEnabled && Preset == Other.Preset && Gradient == Other.Gradient;
}
