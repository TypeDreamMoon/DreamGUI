// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Core/Text/DreamGlyphCoverage.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
THIRD_PARTY_INCLUDES_END
#endif

/*
 * The small-text coverage rasterizer on its own, against FreeType and fonts the engine ships: Roboto, which its own
 * bytecode hints, and DroidSansFallback, whose glyphs carry no hints at all. No font asset, atlas or RHI: a face opened on a
 * FreeType library of the test's own, the way the rasterizer's worker opens its faces.
 */
namespace DreamGlyphCoverageTestLocal
{
#if WITH_FREETYPE
	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	/** A font file's face on a FreeType library of its own. FreeType reads the bytes in place, so they live as long as the face. */
	struct FTestFace
	{
		TArray<uint8> Bytes;
		FT_Library Library = nullptr;
		FT_Face Face = nullptr;

		explicit FTestFace(const FString& Path)
		{
			if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || Bytes.Num() == 0 || FT_Init_FreeType(&Library) != 0)
			{
				Library = nullptr;
				return;
			}
			if (FT_New_Memory_Face(Library, Bytes.GetData(), Bytes.Num(), 0, &Face) != 0)
			{
				Face = nullptr;
			}
		}

		~FTestFace()
		{
			if (Face != nullptr)
			{
				FT_Done_Face(Face);
			}
			if (Library != nullptr)
			{
				FT_Done_FreeType(Library);
			}
		}

		FTestFace(const FTestFace&) = delete;
		FTestFace& operator=(const FTestFace&) = delete;
	};

	FDreamGlyphCoverageParams MakeParams(int32 Size26Dot6, EDreamGlyphHinting Hinting = EDreamGlyphHinting::Auto, float BoldPixels = 0.0f, float ItalicSlope = 0.0f)
	{
		FDreamGlyphCoverageParams Params;
		Params.Size26Dot6 = Size26Dot6;
		Params.Hinting = Hinting;
		Params.BoldPixels = BoldPixels;
		Params.ItalicSlope = ItalicSlope;
		return Params;
	}

	/** The glyph of a code point, rasterized, with a box and as many bytes as it says. Reports a failure itself. */
	bool RasterizeCodepoint(FAutomationTestBase& Test, const FString& What, FT_Face Face, uint32 Codepoint, const FDreamGlyphCoverageParams& Params, FDreamGlyphCoverageResult& Out)
	{
		const uint32 GlyphIndex = FT_Get_Char_Index(Face, Codepoint);
		const bool bRasterized = GlyphIndex != 0 && FDreamGlyphCoverage::Rasterize(Face, GlyphIndex, Params, Out)
			&& Out.Width > 0 && Out.Height > 0 && Out.Pixels.Num() == Out.Width * Out.Height * 4;
		return Test.TestTrue(FString::Printf(TEXT("%s rasterizes"), *What), bRasterized);
	}

	/** Phase p's coverage of a texel, 0 to 1: byte p of the texel. */
	float CoverageAt(const FDreamGlyphCoverageResult& Glyph, int32 x, int32 y, int32 Phase)
	{
		return Glyph.Pixels[(y * Glyph.Width + x) * 4 + Phase] / 255.0f;
	}

	/** A row's ink in one phase: the sum of its coverage, in pixels. */
	float RowInk(const FDreamGlyphCoverageResult& Glyph, int32 y, int32 Phase)
	{
		float Ink = 0.0f;
		for (int32 x = 0; x < Glyph.Width; x++)
		{
			Ink += CoverageAt(Glyph, x, y, Phase);
		}
		return Ink;
	}

	/** The row whose bottom edge is this many pixels above the baseline (row 0 is the box's top row). */
	int32 RowAbove(const FDreamGlyphCoverageResult& Glyph, int32 PixelsAboveBaseline)
	{
		return Glyph.Top - 1 - PixelsAboveBaseline;
	}

	/** The first row from the top with ink worth the name; INDEX_NONE for none. A sliver of coverage is not a row of ink. */
	int32 TopInkRow(const FDreamGlyphCoverageResult& Glyph, int32 Phase)
	{
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			if (RowInk(Glyph, y, Phase) >= 0.02f)
			{
				return y;
			}
		}
		return INDEX_NONE;
	}

	/** How dark the top row of ink is next to the row under it: about 1 when the glyph's top sits on a pixel boundary. */
	float TopRowRatio(const FDreamGlyphCoverageResult& Glyph, int32 Phase)
	{
		const int32 Top = TopInkRow(Glyph, Phase);
		if (Top == INDEX_NONE || Top + 1 >= Glyph.Height)
		{
			return 0.0f;
		}
		const float Below = RowInk(Glyph, Top + 1, Phase);
		return Below > 0.0f ? RowInk(Glyph, Top, Phase) / Below : 0.0f;
	}

	/** A phase's ink-weighted horizontal centre, in pixels right of the pen's column. */
	double CentroidX(const FDreamGlyphCoverageResult& Glyph, int32 Phase)
	{
		double Ink = 0.0;
		double Moment = 0.0;
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			for (int32 x = 0; x < Glyph.Width; x++)
			{
				const double Coverage = CoverageAt(Glyph, x, y, Phase);
				Ink += Coverage;
				Moment += (Glyph.Left + x + 0.5) * Coverage;
			}
		}
		return Ink > 0.0 ? Moment / Ink : 0.0;
	}

	/** A phase's ink-weighted height above the baseline. */
	double CentroidY(const FDreamGlyphCoverageResult& Glyph, int32 Phase)
	{
		double Ink = 0.0;
		double Moment = 0.0;
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			const double RowCoverage = RowInk(Glyph, y, Phase);
			Ink += RowCoverage;
			Moment += (Glyph.Top - y - 0.5) * RowCoverage;
		}
		return Ink > 0.0 ? Moment / Ink : 0.0;
	}

	/** The first and last columns with ink in any of the phases of the mask (bit p for phase p). False when there is none. */
	bool InkColumns(const FDreamGlyphCoverageResult& Glyph, uint32 PhaseMask, int32& OutFirst, int32& OutLast)
	{
		OutFirst = MAX_int32;
		OutLast = MIN_int32;
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			for (int32 x = 0; x < Glyph.Width; x++)
			{
				for (int32 Phase = 0; Phase < 4; Phase++)
				{
					if ((PhaseMask & (1u << Phase)) != 0 && Glyph.Pixels[(y * Glyph.Width + x) * 4 + Phase] != 0)
					{
						OutFirst = FMath::Min(OutFirst, x);
						OutLast = FMath::Max(OutLast, x);
					}
				}
			}
		}
		return OutFirst <= OutLast;
	}

	/**
	 * Where a row's ink starts and ends, in pixels right of the pen's column, read from the coverage of its outermost texels:
	 * exact for a row that crosses only vertical edges, as one through the stems of l and H does.
	 */
	bool RowEdges(const FDreamGlyphCoverageResult& Glyph, int32 y, int32 Phase, float& OutLeft, float& OutRight)
	{
		if (y < 0 || y >= Glyph.Height)
		{
			return false;
		}
		int32 First = INDEX_NONE;
		int32 Last = INDEX_NONE;
		for (int32 x = 0; x < Glyph.Width; x++)
		{
			if (CoverageAt(Glyph, x, y, Phase) > 0.0f)
			{
				First = First == INDEX_NONE ? x : First;
				Last = x;
			}
		}
		if (First == INDEX_NONE)
		{
			return false;
		}
		OutLeft = Glyph.Left + First + (1.0f - CoverageAt(Glyph, First, y, Phase));
		OutRight = Glyph.Left + Last + CoverageAt(Glyph, Last, y, Phase);
		return true;
	}

	/** Loaded with no hinting of any kind: the outline exactly at its size. */
	constexpr FT_Int32 UnhintedLoad = (FT_Int32)(FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM);

	/** Loaded with the light target and the hinter the rasterizer said it used. */
	FT_Int32 LightLoad(bool bAutohinted)
	{
		return (FT_Int32)(FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM | (bAutohinted ? FT_LOAD_FORCE_AUTOHINT : FT_LOAD_NO_AUTOHINT));
	}

	/** A glyph's outline's horizontal extent at a size, loaded with the flags given, in pixels. */
	bool OutlineExtent(FT_Face Face, uint32 Codepoint, int32 Size26Dot6, FT_Int32 LoadFlags, float& OutLeft, float& OutRight)
	{
		if (FT_Set_Char_Size(Face, 0, Size26Dot6, 72, 72) != 0 || FT_Load_Glyph(Face, FT_Get_Char_Index(Face, Codepoint), LoadFlags) != 0
			|| Face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		{
			return false;
		}
		FT_BBox Box;
		FT_Outline_Get_CBox(&Face->glyph->outline, &Box);
		OutLeft = Box.xMin / 64.0f;
		OutRight = Box.xMax / 64.0f;
		return true;
	}

	/** The width of FreeType's own bitmap of a glyph hinted as the rasterizer hints it and not moved: the unmoved phase drawn alone. -1 on failure. */
	int32 FreeTypeBitmapWidth(FT_Face Face, uint32 Codepoint, int32 Size26Dot6, bool bAutohinted)
	{
		if (FT_Set_Char_Size(Face, 0, Size26Dot6, 72, 72) != 0 || FT_Load_Glyph(Face, FT_Get_Char_Index(Face, Codepoint), LightLoad(bAutohinted)) != 0
			|| FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL) != 0)
		{
			return -1;
		}
		return (int32)Face->glyph->bitmap.width;
	}

	/** A glyph as FreeType draws it in its own slot: 8-bit coverage in a box FreeType sizes, placed by bitmap_left and bitmap_top. */
	struct FSlotBitmap
	{
		TArray<uint8> Coverage;
		int32 Width = 0;
		int32 Rows = 0;
		int32 Left = 0;
		int32 Top = 0;

		/** The coverage of the pixel Column pixels right of the pen's column whose top edge is RowTop pixels above the baseline; 0 outside the box. */
		uint8 At(int32 Column, int32 RowTop) const
		{
			const int32 x = Column - Left;
			const int32 y = Top - RowTop;
			return x >= 0 && x < Width && y >= 0 && y < Rows ? Coverage[y * Width + x] : 0;
		}
	};

	/**
	 * FreeType's own bitmap of a glyph hinted as the rasterizer hints it, its outline moved right by Shift (26.6) and drawn by
	 * FT_Render_Glyph, in a box FreeType sizes and places itself. The overlap flag is cleared first: FreeType draws an outline
	 * that has it oversampled, and the rasterizer's FT_Outline_Get_Bitmap ignores it.
	 */
	bool RenderMovedInSlot(FT_Face Face, uint32 Codepoint, int32 Size26Dot6, bool bAutohinted, FT_Pos Shift, FSlotBitmap& Out)
	{
		if (FT_Set_Char_Size(Face, 0, Size26Dot6, 72, 72) != 0 || FT_Load_Glyph(Face, FT_Get_Char_Index(Face, Codepoint), LightLoad(bAutohinted)) != 0
			|| Face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		{
			return false;
		}
		FT_Outline_Translate(&Face->glyph->outline, Shift, 0);
		Face->glyph->outline.flags &= ~FT_OUTLINE_OVERLAP;
		if (FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL) != 0)
		{
			return false;
		}
		const FT_Bitmap& Bitmap = Face->glyph->bitmap;
		Out.Width = (int32)Bitmap.width;
		Out.Rows = (int32)Bitmap.rows;
		Out.Left = Face->glyph->bitmap_left;
		Out.Top = Face->glyph->bitmap_top;
		Out.Coverage.SetNumZeroed(FMath::Max(Out.Width * Out.Rows, 1));
		for (int32 Row = 0; Row < Out.Rows; Row++)
		{
			UDreamUIFontData_FreeTypeRender::ReadGlyphRow(Bitmap, Row, Out.Coverage.GetData() + Row * Out.Width, Out.Width);
		}
		return true;
	}

	/**
	 * Byte Phase of a coverage glyph's texels against FreeType's own bitmap, pixel by pixel where each lies (the two boxes
	 * differ): the largest difference, in 8-bit steps. OutInkOutsideBox counts FreeType's inked pixels outside the glyph's box.
	 */
	int32 LargestDifference(const FDreamGlyphCoverageResult& Glyph, int32 Phase, const FSlotBitmap& Reference, int32& OutInkOutsideBox)
	{
		int32 Largest = 0;
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			for (int32 x = 0; x < Glyph.Width; x++)
			{
				const int32 Own = Glyph.Pixels[(y * Glyph.Width + x) * 4 + Phase];
				Largest = FMath::Max(Largest, FMath::Abs(Own - (int32)Reference.At(Glyph.Left + x, Glyph.Top - y)));
			}
		}
		OutInkOutsideBox = 0;
		for (int32 y = 0; y < Reference.Rows; y++)
		{
			for (int32 x = 0; x < Reference.Width; x++)
			{
				const int32 Column = Reference.Left + x - Glyph.Left;
				const int32 Row = Glyph.Top - (Reference.Top - y);
				if (Reference.Coverage[y * Reference.Width + x] != 0 && (Column < 0 || Column >= Glyph.Width || Row < 0 || Row >= Glyph.Height))
				{
					OutInkOutsideBox++;
				}
			}
		}
		return Largest;
	}

	/** The 1/64 px FreeType keeps an outline to, and the 8-bit coverage an edge is read back through on each side. */
	constexpr float EdgeTolerance = 1.0f / 64.0f + 2.0f / 255.0f;

	/**
	 * Where a glyph's unhinted outline has its ink, in pixels right of the pen's column and above the baseline: FreeType's own
	 * fill of the outline at eight times the size, its ink-weighted centre scaled back down -- the outline's centre to within a
	 * small fraction of a pixel, where a fill at the size itself would count each edge pixel's ink at the pixel's centre.
	 */
	bool OutlineInkCentre(FT_Face Face, uint32 Codepoint, int32 Size26Dot6, double& OutX, double& OutY)
	{
		constexpr int32 Oversample = 8;
		if (FT_Set_Char_Size(Face, 0, (FT_F26Dot6)Size26Dot6 * Oversample, 72, 72) != 0
			|| FT_Load_Glyph(Face, FT_Get_Char_Index(Face, Codepoint), UnhintedLoad) != 0
			|| FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL) != 0)
		{
			return false;
		}
		const FT_Bitmap& Bitmap = Face->glyph->bitmap;
		const int32 Width = (int32)Bitmap.width;
		TArray<uint8> Row;
		Row.SetNumZeroed(FMath::Max(Width, 1));
		double Ink = 0.0;
		double MomentX = 0.0;
		double MomentY = 0.0;
		for (int32 y = 0; y < (int32)Bitmap.rows; y++)
		{
			UDreamUIFontData_FreeTypeRender::ReadGlyphRow(Bitmap, y, Row.GetData(), Width);
			for (int32 x = 0; x < Width; x++)
			{
				const double Coverage = Row[x] / 255.0;
				Ink += Coverage;
				MomentX += (Face->glyph->bitmap_left + x + 0.5) * Coverage;
				MomentY += (Face->glyph->bitmap_top - y - 0.5) * Coverage;
			}
		}
		if (Ink <= 0.0)
		{
			return false;
		}
		OutX = MomentX / Ink / Oversample;
		OutY = MomentY / Ink / Oversample;
		return true;
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoveragePhaseTest,
	"DreamGUI.Text.GlyphCoverage.EachPhaseIsTheGlyphMovedAQuarterPixelRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Byte p of every texel is phase p, the outline moved p/4 px right: FreeType's own bitmap of the same hinted outline moved that
 * far, pixel for pixel where it lands, with none of its ink outside the box the four share. (The ink-weighted mean of the pixel
 * centres is no measure of the move: it counts each pixel's ink at the pixel's centre wherever in the pixel the ink lies, so the
 * 1.15 px stems of an H at 12 px moved half a pixel read as 0.44 px.) Their ink together is at most a column wider than the
 * unmoved phase's, since the last phase reaches 3/4 px further; the box is no wider than FreeType's own bitmap of the same
 * hinted outline unmoved, plus that one pixel. Latin from Roboto and CJK from DroidSansFallback, at sizes small text is drawn at.
 */
bool FDreamGlyphCoveragePhaseTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	FTestFace Droid(EngineFont(TEXT("DroidSansFallback.ttf")));
	if (!TestTrue(TEXT("Roboto and DroidSansFallback open"), Roboto.Face != nullptr && Droid.Face != nullptr))return false;

	struct FCase
	{
		FT_Face Face;
		uint32 Codepoint;
	};
	const FCase Cases[] = {
		{ Roboto.Face, 'o' }, { Roboto.Face, 'H' }, { Roboto.Face, 'x' }, { Roboto.Face, 'g' },
		{ Droid.Face, 0x4E16 }, { Droid.Face, 0x6C38 },
	};
	for (const int32 Pixels : { 12, 14, 16 })
	{
		for (const FCase& Case : Cases)
		{
			const FString What = FString::Printf(TEXT("U+%04X at %d px"), Case.Codepoint, Pixels);
			FDreamGlyphCoverageResult Glyph;
			if (!RasterizeCodepoint(*this, What, Case.Face, Case.Codepoint, MakeParams(Pixels * 64), Glyph))continue;

			for (int32 Phase = 0; Phase < 4; Phase++)
			{
				FSlotBitmap Moved;
				if (!TestTrue(FString::Printf(TEXT("%s: FreeType draws the outline moved %.2f px right"), *What, Phase * 0.25),
					RenderMovedInSlot(Case.Face, Case.Codepoint, Pixels * 64, Glyph.bAutohinted, Phase * 16, Moved)))continue;
				int32 InkOutsideBox = 0;
				const int32 Difference = LargestDifference(Glyph, Phase, Moved, InkOutsideBox);
				// A move off by 1/64 px changes an edge pixel by 4 steps; one step is left for rounding.
				TestTrue(FString::Printf(TEXT("%s: phase %d is FreeType's own bitmap of the outline moved %.2f px right, pixel for pixel (%d of 255 apart at most)"),
					*What, Phase, Phase * 0.25, Difference), Difference <= 1);
				TestEqual(FString::Printf(TEXT("%s: none of the ink of phase %d falls outside the shared box (pixels)"), *What, Phase), InkOutsideBox, 0);
			}

			int32 PhaseZeroFirst = 0, PhaseZeroLast = 0, UnionFirst = 0, UnionLast = 0;
			if (TestTrue(FString::Printf(TEXT("%s: has ink"), *What), InkColumns(Glyph, 0x1, PhaseZeroFirst, PhaseZeroLast) && InkColumns(Glyph, 0xF, UnionFirst, UnionLast)))
			{
				TestTrue(FString::Printf(TEXT("%s: the four phases' ink (%d columns) is at most a column wider than the unmoved phase's (%d)"), *What,
					UnionLast - UnionFirst + 1, PhaseZeroLast - PhaseZeroFirst + 1),
					UnionLast - UnionFirst <= PhaseZeroLast - PhaseZeroFirst + 1);
			}
			const int32 OwnWidth = FreeTypeBitmapWidth(Case.Face, Case.Codepoint, Pixels * 64, Glyph.bAutohinted);
			TestTrue(FString::Printf(TEXT("%s: the box (%d px) is FreeType's own bitmap of the unmoved phase (%d px) and a pixel more at most"), *What, Glyph.Width, OwnWidth),
				OwnWidth > 0 && Glyph.Width <= OwnWidth + 1);
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageLightHintingTest,
	"DreamGUI.Text.GlyphCoverage.LightHintingSnapsRowsAndLeavesColumnsWhereTheOutlineHasThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The light target hints y alone. Rows: the top of an x lands on a pixel boundary, so its top row of ink is as dark as the
 * row under it, where unhinted it is a fraction of that (Roboto's x-height is 6.34, 7.40 and 8.45 px at these sizes) --
 * which also shows the measure can tell. Columns: the stems of l and H keep the unhinted outline's edges to the 1/64 px
 * FreeType keeps outlines in, read back from the coverage of the texels the edges cross. The layout places glyphs by
 * unhinted advances; a hinted width would not fit them. Roboto at whole-pixel sizes keeps its own bytecode hints.
 */
bool FDreamGlyphCoverageLightHintingTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;

	for (const int32 Pixels : { 12, 14, 16 })
	{
		const int32 Size = Pixels * 64;
		FDreamGlyphCoverageResult Hinted, Unhinted;
		if (RasterizeCodepoint(*this, FString::Printf(TEXT("x at %d px"), Pixels), Roboto.Face, 'x', MakeParams(Size), Hinted)
			&& RasterizeCodepoint(*this, FString::Printf(TEXT("x at %d px unhinted"), Pixels), Roboto.Face, 'x', MakeParams(Size, EDreamGlyphHinting::None), Unhinted))
		{
			TestFalse(FString::Printf(TEXT("%d px: Roboto is hinted by its own bytecode, not the autohinter"), Pixels), Hinted.bAutohinted);
			const float Snapped = TopRowRatio(Hinted, 0);
			const float Loose = TopRowRatio(Unhinted, 0);
			TestTrue(FString::Printf(TEXT("%d px: the x-height row of x is %.2f of the row under it hinted (at least 0.9)"), Pixels, Snapped), Snapped >= 0.9f);
			TestTrue(FString::Printf(TEXT("%d px: and %.2f of it unhinted, a row the measure sees is not snapped"), Pixels, Loose), Loose < 0.8f);
		}

		for (const uint32 Codepoint : { uint32('l'), uint32('H') })
		{
			const FString What = FString::Printf(TEXT("%c at %d px"), (TCHAR)Codepoint, Pixels);
			FDreamGlyphCoverageResult Glyph;
			float OutlineLeft = 0.0f, OutlineRight = 0.0f, InkLeft = 0.0f, InkRight = 0.0f;
			if (!RasterizeCodepoint(*this, What, Roboto.Face, Codepoint, MakeParams(Size), Glyph))continue;
			if (!TestTrue(FString::Printf(TEXT("%s: the unhinted outline loads"), *What), OutlineExtent(Roboto.Face, Codepoint, Size, UnhintedLoad, OutlineLeft, OutlineRight)))continue;
			// The second row from the top crosses the stems alone, above H's bar, and lies wholly inside them.
			if (!TestTrue(FString::Printf(TEXT("%s: a row through the stems has ink"), *What), RowEdges(Glyph, 1, 0, InkLeft, InkRight)))continue;
			TestTrue(FString::Printf(TEXT("%s: the ink starts at %.4f px, the unhinted outline at %.4f"), *What, InkLeft, OutlineLeft), FMath::Abs(InkLeft - OutlineLeft) <= EdgeTolerance);
			TestTrue(FString::Printf(TEXT("%s: the ink ends at %.4f px, the unhinted outline at %.4f"), *What, InkRight, OutlineRight), FMath::Abs(InkRight - OutlineRight) <= EdgeTolerance);
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageFractionalSizeTest,
	"DreamGUI.Text.GlyphCoverage.AFractionalSizeIsNotHintedAtTheWholePpemAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Roboto's head.flags ask for whole-pixel ppems, and FreeType's TrueType driver scales a natively hinted glyph by the ppem
 * rounded to an integer: at 18.67 px (14 px text under a 4/3 canvas scale) an H comes out 19 px wide, under advances
 * measured at 18.67. That trap is shown first, then the rasterizer's way round it: such a size goes to the autohinter,
 * which keeps the size, and the H's ink is as wide as the unhinted outline.
 */
bool FDreamGlyphCoverageFractionalSizeTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;
	const int32 Size = FMath::RoundToInt(18.667f * 64.0f);

	float UnhintedLeft = 0.0f, UnhintedRight = 0.0f, NativeLeft = 0.0f, NativeRight = 0.0f;
	if (!TestTrue(TEXT("H loads unhinted and with Roboto's own hints"), OutlineExtent(Roboto.Face, 'H', Size, UnhintedLoad, UnhintedLeft, UnhintedRight)
		&& OutlineExtent(Roboto.Face, 'H', Size, LightLoad(false), NativeLeft, NativeRight)))return false;
	const float UnhintedWidth = UnhintedRight - UnhintedLeft;
	const float NativeWidth = NativeRight - NativeLeft;
	TestTrue(FString::Printf(TEXT("the trap is there: Roboto's own hints make H %.3f px wide at 18.67 px, unhinted it is %.3f"), NativeWidth, UnhintedWidth),
		NativeWidth - UnhintedWidth > 0.1f);

	FDreamGlyphCoverageResult Glyph;
	float InkLeft = 0.0f, InkRight = 0.0f;
	if (!RasterizeCodepoint(*this, TEXT("H at 18.67 px"), Roboto.Face, 'H', MakeParams(Size), Glyph))return false;
	TestTrue(TEXT("a fractional size of a face that asks for whole ppems goes to the autohinter"), Glyph.bAutohinted);
	if (!TestTrue(TEXT("a row through H's stems has ink"), RowEdges(Glyph, 1, 0, InkLeft, InkRight)))return false;
	TestTrue(FString::Printf(TEXT("H's ink is %.4f px wide, the unhinted outline %.4f"), InkRight - InkLeft, UnhintedWidth),
		FMath::Abs((InkRight - InkLeft) - UnhintedWidth) <= 2.0f * EdgeTolerance);
	TestTrue(FString::Printf(TEXT("and starts where the outline does (%.4f px, %.4f)"), InkLeft, UnhintedLeft), FMath::Abs(InkLeft - UnhintedLeft) <= EdgeTolerance);

	// The same size in whole pixels keeps Roboto's own hints.
	FDreamGlyphCoverageResult Whole;
	if (RasterizeCodepoint(*this, TEXT("H at 19 px"), Roboto.Face, 'H', MakeParams(19 * 64), Whole))
	{
		TestFalse(TEXT("a whole-pixel size keeps the face's own hints"), Whole.bAutohinted);
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageHinterTest,
	"DreamGUI.Text.GlyphCoverage.AFaceWithNoHintsOfItsOwnIsAutohinted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * DroidSansFallback's glyphs carry no instructions; its font program is one empty function and its control value
 * program sets dropout control. FreeType's own test for an unhinted font (no font program at all) does not catch it, and
 * its bytecode interpreter would snap nothing, so the rasterizer sends it to the autohinter (its CJK module), at whole
 * sizes too. Roboto has real hints and keeps them, unless the font's setting forces the autohinter or turns hinting off.
 */
bool FDreamGlyphCoverageHinterTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	FTestFace Droid(EngineFont(TEXT("DroidSansFallback.ttf")));
	if (!TestTrue(TEXT("Roboto and DroidSansFallback open"), Roboto.Face != nullptr && Droid.Face != nullptr))return false;

	for (const int32 Pixels : { 12, 14 })
	{
		for (const uint32 Codepoint : { 0x4E16u, 0x6C38u })
		{
			FDreamGlyphCoverageResult Glyph;
			const FString What = FString::Printf(TEXT("DroidSansFallback U+%04X at %d px"), Codepoint, Pixels);
			if (RasterizeCodepoint(*this, What, Droid.Face, Codepoint, MakeParams(Pixels * 64), Glyph))
			{
				TestTrue(FString::Printf(TEXT("%s is autohinted"), *What), Glyph.bAutohinted);
			}
		}
	}

	FDreamGlyphCoverageResult Auto, Forced, Unhinted;
	if (RasterizeCodepoint(*this, TEXT("Roboto a, Auto"), Roboto.Face, 'a', MakeParams(12 * 64), Auto))
	{
		TestFalse(TEXT("Roboto keeps its own hints"), Auto.bAutohinted);
	}
	if (RasterizeCodepoint(*this, TEXT("Roboto a, Autohint"), Roboto.Face, 'a', MakeParams(12 * 64, EDreamGlyphHinting::Autohint), Forced))
	{
		TestTrue(TEXT("Autohint forces the autohinter"), Forced.bAutohinted);
	}
	if (RasterizeCodepoint(*this, TEXT("Roboto a, None"), Roboto.Face, 'a', MakeParams(12 * 64, EDreamGlyphHinting::None), Unhinted))
	{
		TestFalse(TEXT("None hints nothing"), Unhinted.bAutohinted);
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageClearTypeFaceTest,
	"DreamGUI.Text.GlyphCoverage.ANativeClearTypeFaceIsAutohintedSoItsColumnsStay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font that declares itself native ClearType (INSTCTRL in its prep, as Arial does) takes FreeType's v40 interpreter out
 * of the backward compatibility in which it ignores moves in x, so its own hints would snap stems sideways and its glyphs
 * would stop matching the unhinted advances. Such a face goes to the autohinter, whose light mode leaves x alone: the
 * stems of l and H keep the unhinted outline's edges. Skipped where Windows' Arial is not there; how far Arial's own hints
 * would have moved them is logged.
 */
bool FDreamGlyphCoverageClearTypeFaceTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	const FString WindowsDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
	const FString ArialPath = WindowsDir.IsEmpty() ? FString() : FPaths::Combine(WindowsDir, TEXT("Fonts"), TEXT("arial.ttf"));
	if (ArialPath.IsEmpty() || !FPaths::FileExists(ArialPath))
	{
		AddInfo(TEXT("No Arial (arial.ttf) on this machine; a native ClearType face was not checked."));
		return true;
	}
	FTestFace Arial(ArialPath);
	if (!TestNotNull(TEXT("Arial opens"), Arial.Face))return false;

	for (const int32 Pixels : { 12, 16 })
	{
		const int32 Size = Pixels * 64;
		for (const uint32 Codepoint : { uint32('l'), uint32('H') })
		{
			const FString What = FString::Printf(TEXT("Arial %c at %d px"), (TCHAR)Codepoint, Pixels);
			FDreamGlyphCoverageResult Glyph;
			float OutlineLeft = 0.0f, OutlineRight = 0.0f, NativeLeft = 0.0f, NativeRight = 0.0f, InkLeft = 0.0f, InkRight = 0.0f;
			if (!RasterizeCodepoint(*this, What, Arial.Face, Codepoint, MakeParams(Size), Glyph))continue;
			TestTrue(FString::Printf(TEXT("%s is autohinted"), *What), Glyph.bAutohinted);
			if (!TestTrue(FString::Printf(TEXT("%s: the outline loads unhinted and with Arial's own hints"), *What),
				OutlineExtent(Arial.Face, Codepoint, Size, UnhintedLoad, OutlineLeft, OutlineRight)
				&& OutlineExtent(Arial.Face, Codepoint, Size, LightLoad(false), NativeLeft, NativeRight)))continue;
			AddInfo(FString::Printf(TEXT("%s: Arial's own hints would move its left and right edges by %.3f and %.3f px"), *What,
				NativeLeft - OutlineLeft, NativeRight - OutlineRight));
			if (!TestTrue(FString::Printf(TEXT("%s: a row through the stems has ink"), *What), RowEdges(Glyph, 1, 0, InkLeft, InkRight)))continue;
			TestTrue(FString::Printf(TEXT("%s: the ink starts at %.4f px, the unhinted outline at %.4f"), *What, InkLeft, OutlineLeft), FMath::Abs(InkLeft - OutlineLeft) <= EdgeTolerance);
			TestTrue(FString::Printf(TEXT("%s: the ink ends at %.4f px, the unhinted outline at %.4f"), *What, InkRight, OutlineRight), FMath::Abs(InkRight - OutlineRight) <= EdgeTolerance);
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageBoldTest,
	"DreamGUI.Text.GlyphCoverage.SyntheticBoldWidensTheInkByItsStrengthAndKeepsTheLeftEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Synthetic bold emboldens the hinted outline by B on both axes, which keeps its left and bottom edges and grows it right
 * and up: the stem of an l is B wider, its left edge where it was -- the field's bold keeps the left bearing too -- and its
 * right edge B further right. Measured on a row inside the stem in both, 5 to 6 px above the baseline.
 */
bool FDreamGlyphCoverageBoldTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;
	const int32 Size = 16 * 64;

	FDreamGlyphCoverageResult Regular;
	float RegularLeft = 0.0f, RegularRight = 0.0f;
	if (!RasterizeCodepoint(*this, TEXT("l at 16 px"), Roboto.Face, 'l', MakeParams(Size), Regular)
		|| !TestTrue(TEXT("l's stem has ink 5 px up"), RowEdges(Regular, RowAbove(Regular, 5), 0, RegularLeft, RegularRight)))return false;
	const float RegularInk = RowInk(Regular, RowAbove(Regular, 5), 0);

	for (const float Bold : { 0.5f, 1.0f })
	{
		const FString What = FString::Printf(TEXT("l at 16 px emboldened by %.2f px"), Bold);
		FDreamGlyphCoverageResult Emboldened;
		float BoldLeft = 0.0f, BoldRight = 0.0f;
		if (!RasterizeCodepoint(*this, What, Roboto.Face, 'l', MakeParams(Size, EDreamGlyphHinting::Auto, Bold), Emboldened))continue;
		const int32 Row = RowAbove(Emboldened, 5);
		if (!TestTrue(FString::Printf(TEXT("%s: the stem has ink 5 px up"), *What), RowEdges(Emboldened, Row, 0, BoldLeft, BoldRight)))continue;
		const float Widened = RowInk(Emboldened, Row, 0) - RegularInk;
		TestTrue(FString::Printf(TEXT("%s: the stem is %.3f px wider"), *What, Widened), FMath::Abs(Widened - Bold) <= 0.03f);
		TestTrue(FString::Printf(TEXT("%s: its left edge stays (%.4f px, %.4f regular)"), *What, BoldLeft, RegularLeft), FMath::Abs(BoldLeft - RegularLeft) <= EdgeTolerance);
		TestTrue(FString::Printf(TEXT("%s: its right edge moves by the strength (%.4f px, %.4f regular)"), *What, BoldRight, RegularRight),
			FMath::Abs(BoldRight - RegularRight - Bold) <= EdgeTolerance);
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageItalicTest,
	"DreamGUI.Text.GlyphCoverage.SyntheticItalicLeansTheGlyphAndKeepsItsHintedRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The italic shear comes after hinting, about the baseline, so it moves ink sideways only: the box keeps its rows, every
 * row keeps the ink it had (a shear leaves each horizontal slice as long as it was), the x's top row stays snapped, and
 * the ink's centre moves right by the slope times its height.
 */
bool FDreamGlyphCoverageItalicTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;
	const int32 Size = 14 * 64;
	const float Slope = 0.25f;

	FDreamGlyphCoverageResult Upright, Italic;
	if (!RasterizeCodepoint(*this, TEXT("x at 14 px"), Roboto.Face, 'x', MakeParams(Size), Upright)
		|| !RasterizeCodepoint(*this, TEXT("x at 14 px, italic"), Roboto.Face, 'x', MakeParams(Size, EDreamGlyphHinting::Auto, 0.0f, Slope), Italic))return false;
	TestEqual(TEXT("the italic box has the same top"), Italic.Top, Upright.Top);
	TestEqual(TEXT("and the same rows"), Italic.Height, Upright.Height);
	if (Italic.Top != Upright.Top || Italic.Height != Upright.Height)return false;

	for (int32 y = 0; y < Upright.Height; y++)
	{
		const float UprightInk = RowInk(Upright, y, 0);
		const float ItalicInk = RowInk(Italic, y, 0);
		TestTrue(FString::Printf(TEXT("row %d keeps its ink: %.3f px upright, %.3f italic"), y, UprightInk, ItalicInk), FMath::Abs(UprightInk - ItalicInk) <= 0.05f);
	}
	const float Snapped = TopRowRatio(Italic, 0);
	TestTrue(FString::Printf(TEXT("the italic x's top row is %.2f of the row under it (at least 0.9)"), Snapped), Snapped >= 0.9f);
	const double Lean = CentroidX(Italic, 0) - CentroidX(Upright, 0);
	const double Expected = Slope * CentroidY(Upright, 0);
	TestTrue(FString::Printf(TEXT("the ink leans %.3f px right, the slope times its centre's height is %.3f"), Lean, Expected), FMath::Abs(Lean - Expected) <= 0.08);
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageNoOutlineTest,
	"DreamGUI.Text.GlyphCoverage.AGlyphWithNoOutlineIsLeftToTheField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A space has an outline with nothing in it, and a colour strike font (the editor's NotoColorEmoji) none at all: the
 * rasterizer says no to both, and the caller draws the field quad -- empty for the one, the emoji's own path for the other.
 */
bool FDreamGlyphCoverageNoOutlineTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;
	FDreamGlyphCoverageResult Glyph;
	const uint32 Space = FT_Get_Char_Index(Roboto.Face, ' ');
	TestTrue(TEXT("Roboto has a space"), Space != 0);
	TestFalse(TEXT("a space is no coverage glyph"), FDreamGlyphCoverage::Rasterize(Roboto.Face, Space, MakeParams(12 * 64), Glyph));
	TestTrue(TEXT("and leaves nothing behind"), Glyph.Width == 0 && Glyph.Height == 0 && Glyph.Pixels.Num() == 0);

	const FString EmojiPath = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts/NotoColorEmoji.ttf"));
	FTestFace Emoji(EmojiPath);
	if (Emoji.Face == nullptr)
	{
		AddInfo(FString::Printf(TEXT("No %s here; a strike font was not checked."), *EmojiPath));
		return true;
	}
	const uint32 Grinning = FT_Get_Char_Index(Emoji.Face, 0x1F600);
	TestTrue(TEXT("NotoColorEmoji has U+1F600"), Grinning != 0);
	TestFalse(TEXT("a glyph of a strike font is no coverage glyph"), FDreamGlyphCoverage::Rasterize(Emoji.Face, Grinning, MakeParams(12 * 64), Glyph));
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphCoverageUnhintedTest,
	"DreamGUI.Text.GlyphCoverage.AnUnhintedGlyphHasItsInkWhereTheFieldGlyphHasIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Small text with a thin outline draws its face from an unhinted coverage glyph (EDreamUICoverageGlyphFlags::Unhinted, which
 * the font rasterizes with hinting None) over the field's effects copy, so the face has to sit where the field glyph does.
 * The field is made from the unhinted outline (FDreamGlyphSdf), so that is the reference: FreeType's fill of the outline at
 * eight times the size, its ink centre scaled back. The unhinted glyph's unmoved phase has its ink centre there within
 * 0.15 px, across and up. The hinted glyph of an x or an H, its x-height or cap height snapped to a row, is another raster:
 * the flag changes what is drawn. Latin from Roboto and CJK from DroidSansFallback, at sizes small text is drawn at.
 */
bool FDreamGlyphCoverageUnhintedTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphCoverageTestLocal;
	FTestFace Roboto(EngineFont(TEXT("Roboto-Regular.ttf")));
	FTestFace Droid(EngineFont(TEXT("DroidSansFallback.ttf")));
	if (!TestTrue(TEXT("Roboto and DroidSansFallback open"), Roboto.Face != nullptr && Droid.Face != nullptr))return false;

	struct FCase
	{
		FT_Face Face;
		uint32 Codepoint;
		/** Roboto's x-height (0.528 em) and cap height (0.711 em) are no whole pixel at any of the sizes: hinting moves the top. */
		bool bHintingMovesTop;
	};
	const FCase Cases[] = {
		{ Roboto.Face, 'x', true }, { Roboto.Face, 'H', true }, { Roboto.Face, 'e', false }, { Roboto.Face, 'g', false },
		{ Droid.Face, 0x4E16, false },
	};
	for (const int32 Pixels : { 10, 12, 14, 16 })
	{
		for (const FCase& Case : Cases)
		{
			const FString What = FString::Printf(TEXT("U+%04X at %d px"), Case.Codepoint, Pixels);
			FDreamGlyphCoverageResult Hinted, Unhinted;
			if (!RasterizeCodepoint(*this, What, Case.Face, Case.Codepoint, MakeParams(Pixels * 64), Hinted)
				|| !RasterizeCodepoint(*this, What + TEXT(" unhinted"), Case.Face, Case.Codepoint, MakeParams(Pixels * 64, EDreamGlyphHinting::None), Unhinted))continue;
			if (Case.bHintingMovesTop)
			{
				TestTrue(FString::Printf(TEXT("%s: the unhinted raster is not the hinted one"), *What),
					Unhinted.Pixels != Hinted.Pixels || Unhinted.Width != Hinted.Width || Unhinted.Top != Hinted.Top);
			}
			double OutlineX = 0.0, OutlineY = 0.0;
			if (!TestTrue(FString::Printf(TEXT("%s: FreeType fills the outline at eight times the size"), *What), OutlineInkCentre(Case.Face, Case.Codepoint, Pixels * 64, OutlineX, OutlineY)))continue;
			const double InkX = CentroidX(Unhinted, 0);
			const double InkY = CentroidY(Unhinted, 0);
			TestTrue(FString::Printf(TEXT("%s: the unhinted ink's centre is %.3f px across, the outline's %.3f"), *What, InkX, OutlineX), FMath::Abs(InkX - OutlineX) <= 0.15);
			TestTrue(FString::Printf(TEXT("%s: and %.3f px up, the outline's %.3f"), *What, InkY, OutlineY), FMath::Abs(InkY - OutlineY) <= 0.15);
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no coverage glyphs."));
#endif
	return true;
}

#endif
