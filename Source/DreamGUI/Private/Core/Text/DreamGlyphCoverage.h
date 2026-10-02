// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FT_FaceRec_;

/** Which hinter shapes a coverage glyph. Value for value the font's setting, EDreamUICoverageHinting. */
enum class EDreamGlyphHinting : uint8
{
	/**
	 * FT_LOAD_TARGET_LIGHT: the face's own hints when it has them (the v40 interpreter hints vertically only, as the CFF and
	 * Type 1 hinters do), FreeType's autohinter for a TrueType face without -- no fpgm and at most 7 bytes of prep, which
	 * FreeType autohints of its own accord, or glyphs without instructions (maxp) beside a stub of an fpgm, which it would
	 * hand to an interpreter with nothing to do (DroidSansFallback). The autohinter as well for a TrueType face whose
	 * programs use INSTCTRL, the native ClearType fonts (Arial, Segoe UI) whose x hints the v40 interpreter applies, and
	 * when the size is fractional and a hinted TrueType face sets head.flags bit 3, whose native hints would round the ppem
	 * to a whole pixel.
	 */
	Auto,
	/** FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT for every face but a tricky one, which FreeType keeps on its own bytecode. */
	Autohint,
	/** FT_LOAD_NO_HINTING: the outline at its exact size. */
	None,
};

/** What a coverage glyph is rasterized with. */
struct FDreamGlyphCoverageParams
{
	/** Pixels per em, 26.6 fixed point, set with FT_Set_Char_Size at 72 dpi: round(GlyphSize * RasterScale * 64). */
	int32 Size26Dot6 = 0;
	EDreamGlyphHinting Hinting = EDreamGlyphHinting::Auto;
	/**
	 * Synthetic bold: FT_Outline_EmboldenXY by this many pixels on both axes, after hinting. That keeps the outline's left and
	 * bottom edges where they were and grows it right and up by this much, so the left bearing stays, as in the field's bold.
	 * 0 for none.
	 */
	float BoldPixels = 0.0f;
	/** Synthetic italic: the outline sheared about the baseline by this slope (tan of the angle), after hinting. 0 for none. */
	float ItalicSlope = 0.0f;
};

/** A coverage glyph's four subpixel phases, ready for the atlas. */
struct FDreamGlyphCoverageResult
{
	/** The box all four phases fit in, in whole pixels: the unmoved phase's control box floored and ceiled, one pixel wider. */
	int32 Width = 0;
	int32 Height = 0;
	/** The box's left edge in pixels right of the pen's pixel column, and its top edge in pixels above the baseline row. */
	int32 Left = 0;
	int32 Top = 0;
	/**
	 * Width * Height * 4 bytes, rows top first: byte p of each pixel is phase p's 8-bit coverage, phase p being the outline
	 * moved right by p/4 px (p * 16 in 26.6). So B, G, R, A hold phases 0, 1, 2, 3, which is PF_B8G8R8A8 as the atlas
	 * stores it. Linear coverage: no gamma is applied here.
	 */
	TArray<uint8> Pixels;
	/** The autohinter shaped it, chosen by Auto or forced by Autohint. Tests read it. */
	bool bAutohinted = false;
};

/**
 * Coverage glyphs for small text: an outline hinted for its pixel size with FreeType's light target, which snaps rows
 * only, so stems and the x-height sit on pixels while widths and spacing stay what the unhinted advances say (the
 * autohinter's CJK module, unlike its Latin one, hints x at the light target too, and may move vertical stems sideways);
 * then rasterized four times at quarter-pixel offsets, the phases the painter picks from by where the pen falls. Pure and
 * thread-agnostic: the font calls it on the game thread for synchronous glyphs and the rasterizer's worker for the rest,
 * each with an FT_Face of its own, since a face is not thread-safe.
 */
class DREAMGUI_API FDreamGlyphCoverage
{
public:
	/**
	 * Rasterize one glyph of a face. False when the glyph does not load, has no outline (a bitmap-only face) or an empty one
	 * (a space), and the caller draws it from the field instead. Leaves the face's size set to Params.Size26Dot6.
	 */
	static bool Rasterize(FT_FaceRec_* Face, uint32 GlyphIndex, const FDreamGlyphCoverageParams& Params, FDreamGlyphCoverageResult& Out);
};
