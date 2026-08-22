// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamGlyphSdf.h"

#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

// The engine ships msdfgen as one source file meant to be included into a single translation unit,
// wrapped in a parent namespace so two modules can each carry a copy (SlateCore has its own).
THIRD_PARTY_INCLUDES_START
#define MSDFGEN_PARENT_NAMESPACE DreamMsdfgen
#include "ThirdParty/msdfgen/msdfgen.cpp"
THIRD_PARTY_INCLUDES_END

namespace DreamGlyphSdfLocal
{
	// Corners sharper than ~171 degrees are treated as corners when colouring edges.
	constexpr double CornerAngleThreshold = 3.0;
	constexpr int32 MaxSide = 4096;
}
#endif

bool FDreamGlyphSdf::GenerateMTSDF(FT_FaceRec_* Face, uint32 GlyphIndex, float PixelsPerEm, float SpreadPixels, float BoldPixels, FDreamGlyphSdfResult& Out)
{
#if !WITH_FREETYPE
	return false;
#else
	using namespace DreamMsdfgen;
	using namespace DreamGlyphSdfLocal;
	Out = FDreamGlyphSdfResult();
	if (Face == nullptr || PixelsPerEm <= 0.0f || Face->units_per_EM == 0)
	{
		return false;
	}

	// Unscaled, unhinted: the outline in font units, exactly as designed.
	FT_Error Error = FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP);
	if (Error != 0 || Face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
	{
		return false;
	}
	const double Scale = (double)PixelsPerEm / (double)Face->units_per_EM;//pixels per font unit
	Out.Advance = (float)(Face->glyph->metrics.horiAdvance * Scale) + (BoldPixels > 0.0f ? BoldPixels : 0.0f);
	if (Face->glyph->outline.n_points <= 0)
	{
		// Space-like glyph: an advance and nothing to draw.
		return true;
	}
	if (BoldPixels > 0.0f)
	{
		// The outline is unscaled, so its coordinates are plain font units and the strength is too:
		// the 26.6 convention only applies to scaled outlines.
		const double BoldUnits = BoldPixels / Scale;
		FT_Outline_Embolden(&Face->glyph->outline, (FT_Pos)FMath::RoundToInt(BoldUnits));
	}

	msdfgen::Shape Shape;
	if (msdfgen::readFreetypeOutline(Shape, &Face->glyph->outline, 1.0) != 0 || Shape.contours.empty())
	{
		return false;
	}
	Shape.normalize();
	// The atlas row 0 is the top; msdfgen's bitmaps are bottom-up unless told otherwise.
	Shape.inverseYAxis = true;

	const msdfgen::Shape::Bounds Bounds = Shape.getBounds();
	if (!(Bounds.r > Bounds.l && Bounds.t > Bounds.b))
	{
		return true;//degenerate outline: treat as empty
	}
	// An inverted outline (holes wound as fills) would produce an inside-out field; fix it.
	{
		const msdfgen::Point2 OuterPoint(Bounds.l - (Bounds.r - Bounds.l) - 1, Bounds.b - (Bounds.t - Bounds.b) - 1);
		if (msdfgen::SimpleTrueShapeDistanceFinder::oneShotDistance(Shape, OuterPoint) > 0)
		{
			for (msdfgen::Contour& Contour : Shape.contours)
			{
				Contour.reverse();
			}
		}
	}

	const double SpreadUnits = SpreadPixels / Scale;
	const double WidthPx = (Bounds.r - Bounds.l) * Scale + 2.0 * SpreadPixels;
	const double HeightPx = (Bounds.t - Bounds.b) * Scale + 2.0 * SpreadPixels;
	Out.Width = FMath::CeilToInt(WidthPx) + 1;
	Out.Height = FMath::CeilToInt(HeightPx) + 1;
	if (Out.Width <= 0 || Out.Height <= 0 || Out.Width > MaxSide || Out.Height > MaxSide)
	{
		Out = FDreamGlyphSdfResult();
		return false;
	}
	// Bitmap pixel (0,0) is the top-left; the shape is moved so its bounds plus spread fill the
	// bitmap. Half a pixel of slack from the ceil above keeps the outer spread inside.
	const double PadX = (Out.Width - WidthPx) * 0.5 / Scale;
	const double PadY = (Out.Height - HeightPx) * 0.5 / Scale;
	const msdfgen::Vector2 Translate(-Bounds.l + SpreadUnits + PadX, -Bounds.b + SpreadUnits + PadY);
	Out.Left = (float)((Bounds.l - SpreadUnits - PadX) * Scale);
	Out.Top = (float)((Bounds.t + SpreadUnits + PadY) * Scale);

	msdfgen::edgeColoringInkTrap(Shape, CornerAngleThreshold);

	TArray<float> FloatPixels;
	FloatPixels.SetNumUninitialized(Out.Width * Out.Height * 4);
	TArray<uint8> ErrorCorrectionBuffer;
	ErrorCorrectionBuffer.SetNumZeroed(Out.Width * Out.Height);
	msdfgen::BitmapRef<float, 4> FloatBitmap(FloatPixels.GetData(), Out.Width, Out.Height);
	msdfgen::generateMTSDF(
		FloatBitmap,
		Shape,
		msdfgen::SDFTransformation(msdfgen::Projection(msdfgen::Vector2(Scale), Translate), msdfgen::Range(-SpreadUnits, SpreadUnits)),
		msdfgen::MSDFGeneratorConfig(true, msdfgen::ErrorCorrectionConfig(
			msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY,
			msdfgen::ErrorCorrectionConfig::CHECK_DISTANCE_AT_EDGE,
			msdfgen::ErrorCorrectionConfig::defaultMinDeviationRatio,
			msdfgen::ErrorCorrectionConfig::defaultMinImproveRatio,
			ErrorCorrectionBuffer.GetData())));

	// BGRA in memory (FColor order): the three fields in R, G, B and the true distance in A.
	Out.Pixels.SetNumUninitialized(Out.Width * Out.Height * 4);
	for (int32 i = 0; i < Out.Width * Out.Height; i++)
	{
		const float* Src = FloatPixels.GetData() + i * 4;
		uint8* Dst = Out.Pixels.GetData() + i * 4;
		Dst[0] = msdfgen::pixelFloatToByte(Src[2]);//B
		Dst[1] = msdfgen::pixelFloatToByte(Src[1]);//G
		Dst[2] = msdfgen::pixelFloatToByte(Src[0]);//R
		Dst[3] = msdfgen::pixelFloatToByte(Src[3]);//A
	}
	return true;
#endif
}
