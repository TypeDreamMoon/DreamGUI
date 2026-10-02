// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/DreamUIFontData_BaseObject.h"

/**
 * HarfBuzz's output per word, kept so that laying out the same word again -- in another text, or the same text after an
 * edit somewhere else -- does not shape it again. Global, game thread only; FDreamTextShaper is its only reader and
 * writer. Runs are cut into segments at spaces, no-break spaces and tabs and around every CJK ideograph or symbol, and
 * a segment is keyed by its face (UDreamUIFontData_BaseObject::GetFaceIdentity: owner asset and face epoch), its
 * effective size, script, direction, HarfBuzz language, the kern/liga/clig/calt switches, its code points and up to five
 * code points of context on each side, cut at the first space. It holds raw glyph ids, clusters relative to the segment
 * and 26.6 advances and offsets: synthetic bold and face-index mapping are applied after a lookup, so one entry serves
 * every font that uses the face, and no UV is kept, so an atlas flush does not touch it. A face whose lookups involve
 * the space glyph has its runs cached whole instead of per word.
 *
 * Console: DreamGUI.Text.ShapeCache (1; 0 shapes every run whole, as before the cache), DreamGUI.Text.ShapeCacheKB
 * (4096, the LRU's byte budget), DreamGUI.Text.ShapeCacheFlush. Trimmed when the engine asks for memory back.
 */
class DREAMGUI_API FDreamTextShapeCache
{
public:
	/** What the shaper did since the last ResetStats: what the shaper's, the layout's and the benchmark's tests assert on. */
	struct FStats
	{
		/** hb_shape calls, the cache on or off. */
		int64 ShapeCalls = 0;
		/** Code points handed to those calls as the runs or segments to shape, context excluded. */
		int64 ShapedCodepoints = 0;
		/** Segments looked up, and how many of them the cache answered. */
		int64 Lookups = 0;
		int64 Hits = 0;
		/** Entries dropped to stay inside the byte budget. */
		int64 Evictions = 0;
	};

	/** DreamGUI.Text.ShapeCache is not 0. */
	static bool IsEnabled();
	/** Drop every entry (DreamGUI.Text.ShapeCacheFlush). The counters are kept. */
	static void Flush();
	static FStats GetStats();
	static void ResetStats();
	/** Entries held now, and the bytes they are counted at against DreamGUI.Text.ShapeCacheKB. */
	static int32 GetNumEntries();
	static int64 GetBytesUsed();

	/*
	 * The shaper's side. HarfBuzz stays out of this header as it stays out of the fonts': a shaping font is the void* that
	 * UDreamUIFontData_BaseObject::GetShapingFont hands out.
	 */

	/** One glyph as HarfBuzz made it. */
	struct FGlyph
	{
		/** The glyph's index in its face. */
		uint32 GlyphId = 0;
		/** HarfBuzz's cluster, counted from the segment's first code point. */
		int32 Cluster = 0;
		/** 26.6, at the key's size. Synthetic bold is not in it. */
		int32 XAdvance = 0;
		int32 XOffset = 0;
		int32 YOffset = 0;
	};

	/**
	 * Everything a segment's glyphs depend on. Codepoints holds the context before the segment (PreContext of them), the
	 * segment itself (SegmentLength), then the context after it -- the context only for a script whose shaper may read it
	 * (the joining scripts, and any script not known to be free of joining), and never past a space, which ends what
	 * HarfBuzz's joining looks at.
	 */
	struct FKey
	{
		enum EFlags : uint8
		{
			RightToLeft = 1 << 0,
			Kerning = 1 << 1,
			Ligatures = 1 << 2,
			NoContextualAlternates = 1 << 3,
		};

		FDreamUIFontFaceIdentity Face;
		/** The size the segment is shaped at: the run's style size times its face's scale. */
		float Size = 0.0f;
		/** hb_script_t. */
		uint32 Script = 0;
		/** hb_language_t: HarfBuzz interns languages, so the address names one. */
		const void* Language = nullptr;
		/** EFlags. */
		uint8 Flags = 0;
		int32 PreContext = 0;
		int32 SegmentLength = 0;
		TArray<uint32, TInlineAllocator<32>> Codepoints;
	};

	/**
	 * Which glyphs of a face a segment cut may stand next to. A run cut in two shapes the same as it does whole when no
	 * lookup that reads more than one glyph -- a ligature, a context, a pair, an attachment, a legacy kern pair -- can see
	 * the glyphs on both sides of the cut: when the glyph on one side is one no such lookup names, even after any
	 * substitution that could turn it into one that does. Worked out once per face (owner and epoch) from its GSUB, GPOS
	 * and kern tables.
	 */
	struct FFaceRules
	{
		/**
		 * False when some lookup of the face reads glyphs no list names -- a class-based context or pair that matches
		 * class 0, a lookup that skips base glyphs, Apple's state-machine tables -- or HarfBuzz reads across glyphs without a
		 * lookup (Syriac 'stch'; a face with no space glyph, whose hidden characters are deleted into their neighbours), so
		 * where it reaches cannot be told: the face's runs are cached whole, never cut.
		 */
		bool bSegmentable = false;
		/**
		 * Glyph ids that some lookup reading more than one glyph may touch, and those GDEF calls ligatures or marks (which a
		 * lookup may skip, reading past them), one bit per glyph of the face.
		 */
		TBitArray<> TouchedGlyphs;

		/** A glyph id past the face's glyphs is taken as touched: nothing is known of it. */
		bool IsTouched(uint32 GlyphId) const
		{
			return GlyphId >= (uint32)TouchedGlyphs.Num() || TouchedGlyphs[(int32)GlyphId];
		}
	};

	/** The rules of the face ShapingFont (an hb_font_t*) shapes with, worked out on first use. Null without HarfBuzz. Valid until the next call or Flush. */
	static const FFaceRules* GetFaceRules(const FDreamUIFontFaceIdentity& Face, void* ShapingFont);
	/** The glyphs stored under Key, or null (a miss); counted in Lookups and Hits. Valid until the next Add or Flush. */
	static const TArray<FGlyph>* Find(const FKey& Key);
	/** Store a segment's glyphs, dropping the least recently used entries past the budget. */
	static void Add(const FKey& Key, TConstArrayView<FGlyph> Glyphs);
	/** Count one hb_shape call over NumCodepoints code points (the shaper calls it whether or not the cache is on). */
	static void CountShape(int32 NumCodepoints);
};
