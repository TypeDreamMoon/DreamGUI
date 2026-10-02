// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamGlyphColor.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"

#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
THIRD_PARTY_INCLUDES_END
// FreeType's colour API grew over several releases: colour strikes in 2.5, COLR layers in 2.10, the COLRv1 paint graph in
// 2.13. The engine builds every desktop and mobile platform against 2.14; a platform still on an older copy draws no
// colour glyphs at all rather than some of them.
#define DREAM_GLYPH_COLOR_WITH_FREETYPE (FREETYPE_MAJOR > 2 || FREETYPE_MINOR >= 13)
#else
#define DREAM_GLYPH_COLOR_WITH_FREETYPE 0
#endif

#if DREAM_GLYPH_COLOR_WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include FT_COLOR_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
THIRD_PARTY_INCLUDES_END

namespace DreamGlyphColorLocal
{
	/** A stored bitmap bigger than this on either side is refused rather than allocated. */
	constexpr int32 MaxSide = 4096;

	/** The length of one of a face's sfnt tables; 0 when it has none. */
	FT_ULong GetTableLength(FT_Face Face, FT_ULong Tag)
	{
		FT_ULong Length = 0;
		return FT_Load_Sfnt_Table(Face, Tag, 0, nullptr, &Length) == 0 ? Length : 0;
	}

	uint32 ReadBigEndian16(const uint8* Bytes)
	{
		return ((uint32)Bytes[0] << 8) | (uint32)Bytes[1];
	}

	uint32 ReadBigEndian32(const uint8* Bytes)
	{
		return ((uint32)Bytes[0] << 24) | ((uint32)Bytes[1] << 16) | ((uint32)Bytes[2] << 8) | (uint32)Bytes[3];
	}

	/** Strikes of colour bitmaps, CBDT/CBLC or sbix, as opposed to the monochrome EBDT strikes some CJK fonts carry. */
	bool HasColorStrikes(FT_Face Face)
	{
		return FT_HAS_FIXED_SIZES(Face) && Face->num_fixed_sizes > 0 && Face->available_sizes != nullptr
			&& (GetTableLength(Face, TTAG_CBLC) > 0 || GetTableLength(Face, TTAG_sbix) > 0);
	}

	int32 GetLargestStrike(FT_Face Face)
	{
		int32 Largest = INDEX_NONE;
		for (int32 Strike = 0; Strike < Face->num_fixed_sizes; Strike++)
		{
			const FT_Pos Ppem = Face->available_sizes[Strike].y_ppem;
			if (Ppem > 0 && (Largest == INDEX_NONE || Ppem > Face->available_sizes[Largest].y_ppem))
			{
				Largest = Strike;
			}
		}
		return Largest;
	}

	/**
	 * Select a strike and see whether it has the glyph. Metrics only: the strike's index says where the bitmap is and how big,
	 * and nothing is decoded. A bitmap-only face hands back an empty glyph for one its strike lacks (a space, as a rule),
	 * which does not count. Leaves the strike selected and the glyph's strike metrics in the slot.
	 */
	bool StrikeHasGlyph(FT_Face Face, int32 Strike, uint32 GlyphIndex)
	{
		if (FT_Select_Size(Face, Strike) != 0
			|| FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_COLOR | FT_LOAD_SBITS_ONLY | FT_LOAD_BITMAP_METRICS_ONLY) != 0)
		{
			return false;
		}
		return Face->glyph->format == FT_GLYPH_FORMAT_BITMAP && Face->glyph->metrics.width > 0 && Face->glyph->metrics.height > 0;
	}

	/**
	 * The strike a glyph is drawn from: the preferred one if it has the glyph, else, a strike being free to leave glyphs
	 * out, the largest of the others that has it. INDEX_NONE when none does. Leaves that strike selected.
	 */
	int32 SelectStrikeWithGlyph(FT_Face Face, uint32 GlyphIndex, int32 PreferredStrike)
	{
		if (PreferredStrike == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		if (StrikeHasGlyph(Face, PreferredStrike, GlyphIndex))
		{
			return PreferredStrike;
		}
		TArray<int32, TInlineAllocator<8>> Others;
		for (int32 Strike = 0; Strike < Face->num_fixed_sizes; Strike++)
		{
			if (Strike != PreferredStrike && Face->available_sizes[Strike].y_ppem > 0)
			{
				Others.Add(Strike);
			}
		}
		Others.Sort([Face](int32 A, int32 B) { return Face->available_sizes[A].y_ppem > Face->available_sizes[B].y_ppem; });
		for (const int32 Strike : Others)
		{
			if (StrikeHasGlyph(Face, Strike, GlyphIndex))
			{
				return Strike;
			}
		}
		return INDEX_NONE;
	}

	/** COLRv0 layers for the glyph: what FreeType composites when a glyph loaded with FT_LOAD_COLOR is rendered. */
	bool HasColorLayers(FT_Face Face, uint32 GlyphIndex)
	{
		FT_UInt LayerGlyph = 0;
		FT_UInt LayerColor = 0;
		FT_LayerIterator Iterator;
		Iterator.num_layers = 0;
		Iterator.layer = 0;
		Iterator.p = nullptr;
		return FT_Get_Color_Glyph_Layer(Face, GlyphIndex, &LayerGlyph, &LayerColor, &Iterator) != 0;
	}

	/** A COLRv1 paint graph for the glyph, which FreeType hands out but does not draw. */
	bool HasColorPaint(FT_Face Face, uint32 GlyphIndex)
	{
		FT_OpaquePaint Paint;
		Paint.p = nullptr;
		Paint.insert_root_transform = 0;
		return FT_Get_Color_Glyph_Paint(Face, GlyphIndex, FT_COLOR_NO_ROOT_TRANSFORM, &Paint) != 0;
	}

	/**
	 * Whether the face's 'SVG ' table has a document for the glyph, read from the table's own index -- a count, then records
	 * of {startGlyphID, endGlyphID, svgDocOffset, svgDocLength}, 12 bytes each, sorted -- without loading a document. The
	 * same answer whether or not FreeType was built with its SVG hooks.
	 */
	bool HasSvgDocument(FT_Face Face, uint32 GlyphIndex)
	{
		const uint64 TableLength = GetTableLength(Face, TTAG_SVG);
		uint8 Header[10];
		FT_ULong Length = (FT_ULong)UE_ARRAY_COUNT(Header);
		if (TableLength < UE_ARRAY_COUNT(Header) || FT_Load_Sfnt_Table(Face, TTAG_SVG, 0, Header, &Length) != 0)
		{
			return false;
		}
		// version, then the offset of the document list from the start of the table.
		const uint64 ListOffset = ReadBigEndian32(Header + 2);
		uint8 CountBytes[2];
		Length = (FT_ULong)UE_ARRAY_COUNT(CountBytes);
		if (ListOffset + 2 > TableLength || FT_Load_Sfnt_Table(Face, TTAG_SVG, (FT_Long)ListOffset, CountBytes, &Length) != 0)
		{
			return false;
		}
		const int32 Count = (int32)ReadBigEndian16(CountBytes);
		if (ListOffset + 2 + (uint64)Count * 12 > TableLength)
		{
			return false;
		}
		int32 Low = 0;
		int32 High = Count - 1;
		while (Low <= High)
		{
			const int32 Middle = (Low + High) / 2;
			uint8 Record[4];
			Length = (FT_ULong)UE_ARRAY_COUNT(Record);
			if (FT_Load_Sfnt_Table(Face, TTAG_SVG, (FT_Long)(ListOffset + 2 + (uint64)Middle * 12), Record, &Length) != 0)
			{
				return false;
			}
			if (GlyphIndex < ReadBigEndian16(Record))
			{
				High = Middle - 1;
			}
			else if (GlyphIndex > ReadBigEndian16(Record + 2))
			{
				Low = Middle + 1;
			}
			else
			{
				return true;
			}
		}
		return false;
	}

	/** A premultiplied BGRA image, rows top first and tightly packed, and where its top-left corner is. */
	struct FColorImage
	{
		TArray<uint8> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		/** Its left edge right of the glyph origin and its top edge above the baseline, in its own pixels. */
		int32 Left = 0;
		int32 Top = 0;
	};

	/** The glyph slot's bitmap as a colour image. False when it is too big to keep; an empty bitmap is an empty image. */
	bool ReadSlotImage(const FT_GlyphSlotRec_& Slot, FColorImage& OutImage)
	{
		const FT_Bitmap& Bitmap = Slot.bitmap;
		OutImage = FColorImage();
		OutImage.Left = Slot.bitmap_left;
		OutImage.Top = Slot.bitmap_top;
		if (Bitmap.width == 0 || Bitmap.rows == 0 || Bitmap.buffer == nullptr)
		{
			return true;
		}
		if (Bitmap.width > (unsigned int)MaxSide || Bitmap.rows > (unsigned int)MaxSide)
		{
			return false;
		}
		OutImage.Width = (int32)Bitmap.width;
		OutImage.Height = (int32)Bitmap.rows;
		OutImage.Pixels.SetNumUninitialized(OutImage.Width * OutImage.Height * 4);
		if (Bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
		{
			// Rows are pitch bytes apart, and a negative pitch runs them bottom-up, as ReadGlyphRow reads them.
			for (int32 Row = 0; Row < OutImage.Height; Row++)
			{
				const uint8* Source = Bitmap.pitch >= 0
					? Bitmap.buffer + (int64)Row * Bitmap.pitch
					: Bitmap.buffer + (int64)(OutImage.Height - 1 - Row) * (-Bitmap.pitch);
				FMemory::Memcpy(OutImage.Pixels.GetData() + (int64)Row * OutImage.Width * 4, Source, OutImage.Width * 4);
			}
			return true;
		}
		// A strike of grey or 1-bit bitmaps in a colour font: ink with no colour of its own, drawn black, the colour FreeType
		// gives a COLR layer in the text's colour when it has no other.
		TArray<uint8> Coverage;
		Coverage.SetNumUninitialized(OutImage.Width);
		for (int32 Row = 0; Row < OutImage.Height; Row++)
		{
			UDreamUIFontData_FreeTypeRender::ReadGlyphRow(Bitmap, Row, Coverage.GetData(), OutImage.Width);
			uint8* Texel = OutImage.Pixels.GetData() + (int64)Row * OutImage.Width * 4;
			for (int32 x = 0; x < OutImage.Width; x++, Texel += 4)
			{
				Texel[0] = 0;
				Texel[1] = 0;
				Texel[2] = 0;
				Texel[3] = Coverage[x];
			}
		}
		return true;
	}

	/**
	 * Shrink a premultiplied image by Scale (below 1) with an exact area filter: every texel of the result is the average of
	 * the image over the texel's footprint, each source pixel weighed by how much of it the footprint covers, so the total of
	 * every channel scales by exactly Scale squared before rounding. The two corners at the top left coincide. The filter is
	 * separable -- a pass along the rows into floats, then one down the columns -- and its weights are non-negative, so a
	 * colour channel never ends up above alpha. Writes DstWidth x DstHeight texels at Dst, DstStride bytes apart per row.
	 */
	void ShrinkByArea(const FColorImage& Image, double Scale, uint8* Dst, int32 DstWidth, int32 DstHeight, int32 DstStride)
	{
		// Where source pixel i's footprint [i, i + 1) lands among the result's texels, and how much of it each one gets.
		auto ForEachOverlap = [Scale](int32 SourceIndex, int32 DstCount, auto&& Visit)
		{
			const double From = SourceIndex * Scale;
			const double To = (SourceIndex + 1) * Scale;
			const int32 Last = FMath::Min(FMath::CeilToInt32(To), DstCount) - 1;
			for (int32 Index = FMath::Max(FMath::FloorToInt32(From), 0); Index <= Last; Index++)
			{
				const double Weight = FMath::Min(To, (double)(Index + 1)) - FMath::Max(From, (double)Index);
				if (Weight > 0.0)
				{
					Visit(Index, (float)Weight);
				}
			}
		};

		TArray<float> Rows;
		Rows.SetNumZeroed(Image.Height * DstWidth * 4);
		for (int32 y = 0; y < Image.Height; y++)
		{
			const uint8* Source = Image.Pixels.GetData() + (int64)y * Image.Width * 4;
			float* Row = Rows.GetData() + (int64)y * DstWidth * 4;
			for (int32 x = 0; x < Image.Width; x++)
			{
				const uint8* Pixel = Source + x * 4;
				ForEachOverlap(x, DstWidth, [Row, Pixel](int32 u, float Weight)
				{
					for (int32 Channel = 0; Channel < 4; Channel++)
					{
						Row[u * 4 + Channel] += Weight * Pixel[Channel];
					}
				});
			}
		}

		TArray<float> Columns;
		Columns.SetNumZeroed(DstHeight * DstWidth * 4);
		const int32 RowFloats = DstWidth * 4;
		for (int32 y = 0; y < Image.Height; y++)
		{
			const float* Row = Rows.GetData() + (int64)y * RowFloats;
			ForEachOverlap(y, DstHeight, [&Columns, Row, RowFloats](int32 v, float Weight)
			{
				float* Target = Columns.GetData() + (int64)v * RowFloats;
				for (int32 Index = 0; Index < RowFloats; Index++)
				{
					Target[Index] += Weight * Row[Index];
				}
			});
		}

		for (int32 v = 0; v < DstHeight; v++)
		{
			const float* Source = Columns.GetData() + (int64)v * RowFloats;
			uint8* Target = Dst + (int64)v * DstStride;
			for (int32 Index = 0; Index < RowFloats; Index++)
			{
				Target[Index] = (uint8)FMath::Clamp(FMath::RoundToInt32(Source[Index]), 0, 255);
			}
		}
	}

	/**
	 * Out's bitmap from an image: shrunk by Scale when that is below 1, as it is otherwise, then padded. The padding is the
	 * reach in texels of the stored bitmap, rounded up, and one texel more, all (0,0,0,0): bilinear sampling at the cell's
	 * edge then never reads a neighbour, and an underlay sampled at an offset stays inside the cell.
	 */
	bool StoreImage(const FColorImage& Image, double Scale, float TexelsPerEm, float ReachEm, FDreamGlyphColorResult& Out)
	{
		const bool bShrink = Scale < 1.0;
		const int32 Padding = 1 + FMath::CeilToInt32(FMath::Min(ReachEm * TexelsPerEm, (float)MaxSide));
		// A hair less than the scaled size, so float error never adds a column of nothing.
		const int32 InnerWidth = Image.Width == 0 ? 0 : bShrink ? FMath::Max(1, FMath::CeilToInt32(Image.Width * Scale - 1e-6)) : Image.Width;
		const int32 InnerHeight = Image.Height == 0 ? 0 : bShrink ? FMath::Max(1, FMath::CeilToInt32(Image.Height * Scale - 1e-6)) : Image.Height;
		const int32 Width = InnerWidth + 2 * Padding;
		const int32 Height = InnerHeight + 2 * Padding;
		if (Padding < 1 || Width > MaxSide || Height > MaxSide)
		{
			return false;
		}
		Out.Width = Width;
		Out.Height = Height;
		Out.Left = (float)(Image.Left * Scale) - Padding;
		Out.Top = (float)(Image.Top * Scale) + Padding;
		Out.TexelsPerEm = TexelsPerEm;
		Out.Pixels.SetNumZeroed(Width * Height * 4);
		uint8* Inner = Out.Pixels.GetData() + ((int64)Padding * Width + Padding) * 4;
		if (InnerWidth == 0 || InnerHeight == 0)
		{
			return true;
		}
		if (bShrink)
		{
			ShrinkByArea(Image, Scale, Inner, InnerWidth, InnerHeight, Width * 4);
		}
		else
		{
			for (int32 Row = 0; Row < InnerHeight; Row++)
			{
				FMemory::Memcpy(Inner + (int64)Row * Width * 4, Image.Pixels.GetData() + (int64)Row * InnerWidth * 4, InnerWidth * 4);
			}
		}
		return true;
	}

	/** A glyph of a strike already selected (SelectStrikeWithGlyph). */
	bool RasterizeStrikeGlyph(FT_Face Face, uint32 GlyphIndex, int32 Strike, int32 TargetPixelSize, float ReachEm, FDreamGlyphColorResult& Out)
	{
		FT_Int32 LoadFlags = FT_LOAD_COLOR | FT_LOAD_SBITS_ONLY;
#ifdef FT_LOAD_NO_SVG
		LoadFlags |= FT_LOAD_NO_SVG;
#endif
		if (FT_Load_Glyph(Face, GlyphIndex, LoadFlags) != 0 || Face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
		{
			return false;
		}
		const double StrikePixelsPerEm = Face->available_sizes[Strike].y_ppem / 64.0;
		FColorImage Image;
		if (StrikePixelsPerEm <= 0.0 || !ReadSlotImage(*Face->glyph, Image))
		{
			return false;
		}
		// A larger strike is shrunk to the size asked for; a smaller one is kept as it is, for the GPU to enlarge.
		const double Scale = TargetPixelSize < StrikePixelsPerEm ? TargetPixelSize / StrikePixelsPerEm : 1.0;
		const float TexelsPerEm = Scale < 1.0 ? (float)TargetPixelSize : (float)StrikePixelsPerEm;
		if (!StoreImage(Image, Scale, TexelsPerEm, ReachEm, Out))
		{
			Out = FDreamGlyphColorResult();
			return false;
		}
		Out.Kind = EDreamGlyphColorKind::Bitmap;
		// The strike's own advance, in its pixels and then texels: what the shaping font answers for such a face too.
		Out.Advance = (float)(Face->glyph->metrics.horiAdvance / 64.0 * Scale);
		return true;
	}

	/** A COLRv0 glyph at TargetPixelSize, its layers composited by FreeType. */
	bool RasterizeLayers(FT_Face Face, uint32 GlyphIndex, int32 TargetPixelSize, float ReachEm, FDreamGlyphColorResult& Out)
	{
		if (!FT_IS_SCALABLE(Face) || FT_Set_Char_Size(Face, 0, (FT_F26Dot6)TargetPixelSize * 64, 72, 72) != 0)
		{
			return false;
		}
		// Unhinted: every layer is a glyph of its own, and hinted one by one the edges they share would part.
		FT_Int32 LoadFlags = FT_LOAD_COLOR | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM;
#ifdef FT_LOAD_NO_SVG
		LoadFlags |= FT_LOAD_NO_SVG;
#endif
		if (FT_Load_Glyph(Face, GlyphIndex, LoadFlags) != 0)
		{
			return false;
		}
		FT_GlyphSlot Slot = Face->glyph;
		// hmtx, unhinted and unrounded, as the shaper has it (16.16 pixels).
		const double Advance = Slot->linearHoriAdvance / 65536.0;
		// Rendering a glyph loaded with FT_LOAD_COLOR blends its layers, in palette 0, into one premultiplied BGRA bitmap.
		// When that fails FreeType draws the plain outline instead, which is no colour glyph.
		if (Slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(Slot, FT_RENDER_MODE_NORMAL) != 0)
		{
			return false;
		}
		if (Slot->format != FT_GLYPH_FORMAT_BITMAP || (Slot->bitmap.width > 0 && Slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA))
		{
			return false;
		}
		FColorImage Image;
		if (!ReadSlotImage(*Slot, Image) || !StoreImage(Image, 1.0, (float)TargetPixelSize, ReachEm, Out))
		{
			Out = FDreamGlyphColorResult();
			return false;
		}
		Out.Kind = EDreamGlyphColorKind::Layers;
		Out.Advance = (float)Advance;
		return true;
	}
}
#endif

EDreamGlyphColorKind FDreamGlyphColor::GetColorKind(FT_FaceRec_* Face, uint32 GlyphIndex)
{
#if DREAM_GLYPH_COLOR_WITH_FREETYPE
	using namespace DreamGlyphColorLocal;
	if (Face == nullptr || !FT_HAS_COLOR(Face))
	{
		return EDreamGlyphColorKind::None;
	}
	if (HasColorStrikes(Face) && SelectStrikeWithGlyph(Face, GlyphIndex, GetLargestStrike(Face)) != INDEX_NONE)
	{
		return EDreamGlyphColorKind::Bitmap;
	}
	// v0 layers win over a v1 paint graph: a font that has both (Segoe UI Emoji) is drawn from the layers.
	if (HasColorLayers(Face, GlyphIndex))
	{
		return EDreamGlyphColorKind::Layers;
	}
	if (HasColorPaint(Face, GlyphIndex) || HasSvgDocument(Face, GlyphIndex))
	{
		return EDreamGlyphColorKind::Unsupported;
	}
#endif
	return EDreamGlyphColorKind::None;
}

bool FDreamGlyphColor::Rasterize(FT_FaceRec_* Face, uint32 GlyphIndex, const FDreamGlyphColorParams& Params, FDreamGlyphColorResult& Out)
{
	Out = FDreamGlyphColorResult();
#if DREAM_GLYPH_COLOR_WITH_FREETYPE
	using namespace DreamGlyphColorLocal;
	if (Face == nullptr || Params.TargetPixelSize <= 0 || !FT_HAS_COLOR(Face))
	{
		return false;
	}
	// A negative reach, or none, pads by the one texel.
	const float ReachEm = Params.ReachEm > 0.0f ? Params.ReachEm : 0.0f;
	if (HasColorStrikes(Face))
	{
		const int32 Strike = SelectStrikeWithGlyph(Face, GlyphIndex, ChooseStrike(Face, (float)Params.TargetPixelSize));
		if (Strike != INDEX_NONE)
		{
			return RasterizeStrikeGlyph(Face, GlyphIndex, Strike, Params.TargetPixelSize, ReachEm, Out);
		}
	}
	if (HasColorLayers(Face, GlyphIndex))
	{
		return RasterizeLayers(Face, GlyphIndex, Params.TargetPixelSize, ReachEm, Out);
	}
#endif
	return false;
}

int32 FDreamGlyphColor::GetSizeBucket(float DevicePixelSize)
{
	// A size within 1/64 px above a step -- 12 px at a canvas scale of 1.3334 -- counts as on it rather than a step up.
	const float Size = DevicePixelSize - 1.0f / 64.0f;
	if (!(Size > 1.0f))
	{
		return 1;
	}
	// Rounded up, so the GPU only ever shrinks a bucket's bitmap, by at most one step: 1/16 of the size or less.
	if (Size <= 32.0f)
	{
		return FMath::CeilToInt32(Size);
	}
	if (Size <= 64.0f)
	{
		return 32 + 2 * FMath::CeilToInt32((Size - 32.0f) / 2.0f);
	}
	if (Size <= 128.0f)
	{
		return 64 + 4 * FMath::CeilToInt32((Size - 64.0f) / 4.0f);
	}
	return FMath::Min(256, 128 + 8 * FMath::CeilToInt32((FMath::Min(Size, 256.0f) - 128.0f) / 8.0f));
}

int32 FDreamGlyphColor::ChooseStrike(FT_FaceRec_* Face, float PixelSize)
{
#if DREAM_GLYPH_COLOR_WITH_FREETYPE
	if (Face == nullptr || Face->num_fixed_sizes <= 0 || Face->available_sizes == nullptr)
	{
		return INDEX_NONE;
	}
	// In 26.6, as FreeType keeps the strikes' sizes, so an exact match is exact.
	const FT_Pos Wanted = (FT_Pos)FMath::RoundToInt32(FMath::Clamp(PixelSize, 0.0f, 65536.0f) * 64.0f);
	int32 SmallestAbove = INDEX_NONE;
	int32 Largest = INDEX_NONE;
	for (int32 Strike = 0; Strike < Face->num_fixed_sizes; Strike++)
	{
		const FT_Pos Ppem = Face->available_sizes[Strike].y_ppem;
		if (Ppem <= 0)
		{
			// A strike FreeType disabled for its impossible dimensions.
			continue;
		}
		if (Ppem == Wanted)
		{
			return Strike;
		}
		if (Ppem > Wanted && (SmallestAbove == INDEX_NONE || Ppem < Face->available_sizes[SmallestAbove].y_ppem))
		{
			SmallestAbove = Strike;
		}
		if (Largest == INDEX_NONE || Ppem > Face->available_sizes[Largest].y_ppem)
		{
			Largest = Strike;
		}
	}
	return SmallestAbove != INDEX_NONE ? SmallestAbove : Largest;
#else
	return INDEX_NONE;
#endif
}

bool FDreamGlyphColor::GetStrikeAdvance(FT_FaceRec_* Face, uint32 GlyphIndex, float PixelSize, float& OutAdvancePixels)
{
	OutAdvancePixels = 0.0f;
#if DREAM_GLYPH_COLOR_WITH_FREETYPE
	using namespace DreamGlyphColorLocal;
	if (Face == nullptr || !(PixelSize > 0.0f) || !HasColorStrikes(Face))
	{
		return false;
	}
	const int32 Strike = SelectStrikeWithGlyph(Face, GlyphIndex, ChooseStrike(Face, PixelSize));
	if (Strike == INDEX_NONE)
	{
		return false;
	}
	// SelectStrikeWithGlyph left the glyph's metrics in that strike in the slot.
	const double StrikePixelsPerEm = Face->available_sizes[Strike].y_ppem / 64.0;
	if (StrikePixelsPerEm <= 0.0)
	{
		return false;
	}
	OutAdvancePixels = (float)(Face->glyph->metrics.horiAdvance / 64.0 * PixelSize / StrikePixelsPerEm);
	return true;
#else
	return false;
#endif
}

#undef DREAM_GLYPH_COLOR_WITH_FREETYPE
