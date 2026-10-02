// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamGlyphCoverage.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"

#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
THIRD_PARTY_INCLUDES_END

namespace DreamGlyphCoverageLocal
{
	/** A side longer than this is no small-text glyph -- a coverage cell is 512 texels square -- and is refused rather than allocated. */
	constexpr int32 MaxSide = 1024;
	/** FreeType autohints a TrueType face of its own accord when it has no font program and a control value program this short. */
	constexpr FT_ULong FreeTypeUnhintedPrepBytes = 7;
	/** A font program at most this long defines nothing a glyph could be hinted with: DroidSansFallback's is one empty function in 7 bytes. */
	constexpr FT_ULong StubFontProgramBytes = 16;

	/** Whole pixels from 26.6, rounded down and up. */
	int32 FloorPixels(FT_Pos Value)
	{
		return (int32)(Value >> 6);
	}

	int32 CeilPixels(FT_Pos Value)
	{
		return (int32)((Value + 63) >> 6);
	}

	/** The length of one of a face's sfnt tables; 0 when it has none. */
	FT_ULong GetTableLength(FT_Face Face, FT_ULong Tag)
	{
		FT_ULong Length = 0;
		return FT_Load_Sfnt_Table(Face, Tag, 0, nullptr, &Length) == 0 ? Length : 0;
	}

	/**
	 * Whether a TrueType face's bytecode hints its glyphs. FreeType autohints a face only when it has no font program and at
	 * most 7 bytes of control value program. A face whose glyphs carry no instructions but which keeps a stub of either --
	 * DroidSansFallback has an empty function in its fpgm and dropout control in its prep -- goes to the interpreter
	 * instead, which then snaps nothing and only scales the outline, at a ppem rounded to a whole pixel at that (see
	 * ShouldAutohint). maxp's instruction size is not trusted on its own, as FreeType does not trust it: a font program of
	 * any real length means hints.
	 */
	bool HasTrueTypeHints(FT_Face Face)
	{
		const FT_ULong FontProgramBytes = GetTableLength(Face, TTAG_fpgm);
		if (FontProgramBytes == 0 && GetTableLength(Face, TTAG_prep) <= FreeTypeUnhintedPrepBytes)
		{
			return false;
		}
		const TT_MaxProfile* Maxp = static_cast<const TT_MaxProfile*>(FT_Get_Sfnt_Table(Face, FT_SFNT_MAXP));
		const bool bGlyphsHaveNoInstructions = Maxp != nullptr && Maxp->version >= 0x00010000 && Maxp->maxSizeOfInstructions == 0;
		return !(bGlyphsHaveNoInstructions && FontProgramBytes <= StubFontProgramBytes);
	}

	/**
	 * Whether one of a face's TrueType programs executes INSTCTRL. Its selector 3 is how a font declares itself native
	 * ClearType, and FreeType's v40 interpreter then drops the backward compatibility in which it ignores every move in x,
	 * and applies the font's x hints: stems snapped sideways, widths no longer those the unhinted advances were measured
	 * with. Microsoft's own fonts (Arial, Segoe UI) do it in their prep. Any INSTCTRL counts; push data is skipped, so only
	 * opcodes are read.
	 */
	bool ProgramUsesInstructionControl(FT_Face Face, FT_ULong Tag)
	{
		FT_ULong Length = GetTableLength(Face, Tag);
		TArray<uint8> Code;
		Code.SetNumUninitialized((int32)Length);
		if (Length == 0 || FT_Load_Sfnt_Table(Face, Tag, 0, Code.GetData(), &Length) != 0)
		{
			return false;
		}
		for (int32 Index = 0; Index < Code.Num();)
		{
			const uint8 Opcode = Code[Index++];
			const int32 Count = Index < Code.Num() ? Code[Index] : 0;
			if (Opcode == 0x8E)
			{
				// INSTCTRL
				return true;
			}
			if (Opcode == 0x40)
			{
				// NPUSHB: a count, then that many bytes.
				Index += 1 + Count;
			}
			else if (Opcode == 0x41)
			{
				// NPUSHW: a count, then that many words.
				Index += 1 + 2 * Count;
			}
			else if (Opcode >= 0xB0 && Opcode <= 0xB7)
			{
				// PUSHB[n]: n + 1 bytes.
				Index += Opcode - 0xAF;
			}
			else if (Opcode >= 0xB8 && Opcode <= 0xBF)
			{
				// PUSHW[n]: n + 1 words.
				Index += 2 * (Opcode - 0xB7);
			}
		}
		return false;
	}

	/**
	 * Which hinter shapes the glyph (EDreamGlyphHinting). Decided here and passed to FreeType as FT_LOAD_FORCE_AUTOHINT or
	 * FT_LOAD_NO_AUTOHINT, so the answer is known. Left to itself FreeType would autohint every TrueType face at the light
	 * target: its TrueType driver does not count as a light hinter, though with the v40 interpreter it hints y alone -- for
	 * fonts that do not ask it otherwise (ProgramUsesInstructionControl).
	 */
	bool ShouldAutohint(FT_Face Face, const FDreamGlyphCoverageParams& Params)
	{
		// A tricky face needs its own bytecode to come out right at all, and FreeType gives it that whatever the flags ask.
		if (Params.Hinting == EDreamGlyphHinting::None || FT_IS_TRICKY(Face))
		{
			return false;
		}
		if (Params.Hinting == EDreamGlyphHinting::Autohint || (Face->face_flags & FT_FACE_FLAG_HINTER) == 0)
		{
			return true;
		}
		// CFF and Type 1 outlines: their own hinters hint lightly, and at any size.
		if (!FT_IS_SFNT(Face) || GetTableLength(Face, TTAG_glyf) == 0)
		{
			return false;
		}
		// head.flags bit 3 asks for whole-pixel ppems, and FreeType's TrueType driver honours it by scaling a hinted glyph by the
		// ppem rounded to an integer (tt_size_reset): an 18.67 px glyph would come out 19 px wide under its 18.67 px advance.
		// The autohinter scales by the size as it is.
		const TT_Header* Head = static_cast<const TT_Header*>(FT_Get_Sfnt_Table(Face, FT_SFNT_HEAD));
		if ((Params.Size26Dot6 & 63) != 0 && Head != nullptr && (Head->Flags & (1 << 3)) != 0)
		{
			return true;
		}
		return !HasTrueTypeHints(Face) || ProgramUsesInstructionControl(Face, TTAG_prep) || ProgramUsesInstructionControl(Face, TTAG_fpgm);
	}
}
#endif

bool FDreamGlyphCoverage::Rasterize(FT_FaceRec_* Face, uint32 GlyphIndex, const FDreamGlyphCoverageParams& Params, FDreamGlyphCoverageResult& Out)
{
	Out = FDreamGlyphCoverageResult();
#if !WITH_FREETYPE
	return false;
#else
	using namespace DreamGlyphCoverageLocal;
	// A face with no outlines -- a strike font, which is what sbix faces count as too -- has nothing to hint or to move.
	if (Face == nullptr || Face->glyph == nullptr || Params.Size26Dot6 <= 0 || !FT_IS_SCALABLE(Face))
	{
		return false;
	}
	if (FT_Set_Char_Size(Face, 0, (FT_F26Dot6)Params.Size26Dot6, 72, 72) != 0)
	{
		return false;
	}

	// Never an embedded bitmap: the phases are the outline moved, and a strike cannot move by a quarter of a pixel.
	const bool bAutohint = ShouldAutohint(Face, Params);
	FT_Int32 LoadFlags = FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM;
	if (Params.Hinting == EDreamGlyphHinting::None)
	{
		LoadFlags |= FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT;
	}
	else
	{
		// The light target snaps rows only: stems and the x-height land on pixels, while widths and positions stay what the
		// unhinted advances the layout placed the glyph with say.
		LoadFlags |= FT_LOAD_TARGET_LIGHT;
		LoadFlags |= bAutohint ? FT_LOAD_FORCE_AUTOHINT : FT_LOAD_NO_AUTOHINT;
	}
	if (FT_Load_Glyph(Face, GlyphIndex, LoadFlags) != 0 || Face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
	{
		return false;
	}
	FT_Outline& Outline = Face->glyph->outline;
	if (Outline.n_points == 0 || Outline.n_contours == 0)
	{
		// A space: nothing to draw, and the field's quad, as empty, is what the caller falls back to.
		return false;
	}

	// Synthetic styles, on the hinted outline. EmboldenXY keeps the outline's left and bottom edges where they were and grows
	// it right and up by the strength, so the left bearing stays where the field's bold keeps it. A failure (an outline with
	// no orientation) leaves the glyph regular rather than undrawn.
	if (Params.BoldPixels > 0.0f)
	{
		const FT_Pos Strength = (FT_Pos)FMath::RoundToInt(Params.BoldPixels * 64.0f);
		if (Strength > 0)
		{
			FT_Outline_EmboldenXY(&Outline, Strength, Strength);
		}
	}
	// Italic shears about the baseline after hinting: x moves by the slope times the height, and the snapped rows stay snapped.
	if (Params.ItalicSlope != 0.0f)
	{
		FT_Matrix Shear;
		Shear.xx = 0x10000;
		Shear.xy = (FT_Fixed)FMath::RoundToInt(Params.ItalicSlope * 65536.0f);
		Shear.yx = 0;
		Shear.yy = 0x10000;
		FT_Outline_Transform(&Outline, &Shear);
	}

	// The box the four phases share: the unmoved phase's control box in whole pixels, one pixel wider for the last phase's 3/4 px.
	FT_BBox ControlBox;
	FT_Outline_Get_CBox(&Outline, &ControlBox);
	const int32 BoxLeft = FloorPixels(ControlBox.xMin);
	const int32 BoxRight = CeilPixels(ControlBox.xMax) + 1;
	const int32 BoxBottom = FloorPixels(ControlBox.yMin);
	const int32 BoxTop = CeilPixels(ControlBox.yMax);
	const int32 Width = BoxRight - BoxLeft;
	const int32 Height = BoxTop - BoxBottom;
	if (Width <= 0 || Height <= 0 || Width > MaxSide || Height > MaxSide)
	{
		return false;
	}

	// Each phase is rendered into a grey bitmap the size of the box and interleaved into byte p of every texel. FreeType's
	// raster puts outline y = 0 on the bitmap's bottom row and, with a positive pitch, row 0 at the top; the outline is moved
	// so the box's bottom-left corner is that origin.
	TArray<uint8> PhaseBuffer;
	PhaseBuffer.SetNumUninitialized(Width * Height);
	TArray<uint8> Row;
	Row.SetNumUninitialized(Width);
	FT_Bitmap Target;
	FMemory::Memzero(&Target, sizeof(Target));
	Target.rows = (unsigned int)Height;
	Target.width = (unsigned int)Width;
	Target.pitch = Width;
	Target.buffer = PhaseBuffer.GetData();
	Target.num_grays = 256;
	Target.pixel_mode = FT_PIXEL_MODE_GRAY;
	Out.Pixels.SetNumZeroed(Width * Height * 4);
	FT_Outline_Translate(&Outline, -(FT_Pos)BoxLeft * 64, -(FT_Pos)BoxBottom * 64);
	for (int32 Phase = 0; Phase < 4; Phase++)
	{
		if (Phase > 0)
		{
			// A quarter of a pixel further right: 16 in 26.6, exact.
			FT_Outline_Translate(&Outline, 16, 0);
		}
		FMemory::Memzero(PhaseBuffer.GetData(), PhaseBuffer.Num());
		if (FT_Outline_Get_Bitmap(Face->glyph->library, &Outline, &Target) != 0)
		{
			Out = FDreamGlyphCoverageResult();
			return false;
		}
		for (int32 y = 0; y < Height; y++)
		{
			UDreamUIFontData_FreeTypeRender::ReadGlyphRow(Target, y, Row.GetData(), Width);
			uint8* Texel = Out.Pixels.GetData() + (int64)y * Width * 4 + Phase;
			for (int32 x = 0; x < Width; x++)
			{
				Texel[x * 4] = Row[x];
			}
		}
	}
	Out.Width = Width;
	Out.Height = Height;
	Out.Left = BoxLeft;
	Out.Top = BoxTop;
	Out.bAutohinted = bAutohint;
	return true;
#endif
}
