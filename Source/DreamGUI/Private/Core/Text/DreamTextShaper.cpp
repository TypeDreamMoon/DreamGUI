// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextShaper.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#if WITH_HARFBUZZ
#include "hb.h"
// The ICU module is linked wherever HarfBuzz is (HarfBuzz takes its Unicode functions from it), so its bidi API is
// there to read the embedding levels from: the engine's TextBiDi reports a direction per run and no levels.
#if UE_ENABLE_ICU
THIRD_PARTY_INCLUDES_START
#include <unicode/ubidi.h>
THIRD_PARTY_INCLUDES_END
#endif
#endif

bool FDreamTextShaper::CanShape(UDreamUIFontData_BaseObject* Font)
{
#if WITH_HARFBUZZ
	return Font != nullptr && Font->GetShapingFont(0, 16.0f) != nullptr;
#else
	return false;
#endif
}

#if WITH_HARFBUZZ
namespace DreamTextShaperLocal
{
	struct FItem
	{
		uint8 Level = 0;
		hb_script_t Script = HB_SCRIPT_COMMON;
		int32 FaceIndex = 0;
		float Size = 0.0f;
		bool bBold = false;
		bool bUnshaped = false;

		bool SameRun(const FItem& Other) const
		{
			return Level == Other.Level && Script == Other.Script && FaceIndex == Other.FaceIndex
				&& Size == Other.Size && bBold == Other.bBold && bUnshaped == Other.bUnshaped;
		}
	};

	bool ScriptIsNeutral(hb_script_t Script)
	{
		return Script == HB_SCRIPT_COMMON || Script == HB_SCRIPT_INHERITED || Script == HB_SCRIPT_UNKNOWN;
	}

	/** What HarfBuzz is handed for an element: a tab is drawn as the space it stands for. */
	uint32 ShapingCodepoint(uint32 C)
	{
		return C == '\t' ? (uint32)' ' : C;
	}

	/**
	 * Whether a run's contextual alternates (calt) are a matter of style: in the alphabets whose letters stand alone, and
	 * in runs of nothing but digits and punctuation, a font uses them for flourishes and for drawing several characters
	 * as one (a coding font's arrows, a script face's connections), never for a form the script requires. Every other
	 * script's calt is left at the font's default: the fonts of the joining scripts (Arabic, Syriac, Mongolian, N'Ko...)
	 * and of the Brahmic ones can keep required forms there.
	 */
	bool ContextualAlternatesAreStylistic(hb_script_t Script)
	{
		return Script == HB_SCRIPT_LATIN || Script == HB_SCRIPT_GREEK || Script == HB_SCRIPT_CYRILLIC
			|| Script == HB_SCRIPT_ARMENIAN || Script == HB_SCRIPT_GEORGIAN || Script == HB_SCRIPT_COMMON;
	}

	/**
	 * Whether the bidi algorithm can have anything to say about a code point: a right-to-left letter (Hebrew, Arabic,
	 * Syriac, Thaana, NKo, Samaritan, Mandaic and their presentation forms, the right-to-left scripts of the
	 * supplementary planes), an Arabic digit, or an explicit direction control.
	 */
	bool CanTurnRightToLeft(uint32 C)
	{
		return (C >= 0x0590 && C <= 0x08FF)
			|| (C >= 0xFB1D && C <= 0xFDFF)
			|| (C >= 0xFE70 && C <= 0xFEFF)
			|| (C >= 0x10800 && C <= 0x10FFF)
			|| (C >= 0x1E800 && C <= 0x1EFFF)
			|| C == 0x200E || C == 0x200F || C == 0x061C
			|| (C >= 0x202A && C <= 0x202E)
			|| (C >= 0x2066 && C <= 0x2069);
	}

#if UE_ENABLE_ICU
	/**
	 * ICU's resolved embedding levels, one per UTF-16 unit of the paragraph: rules W1 to I2 of UAX #9, and L1 at the
	 * paragraph's end. False when ICU refuses the string, and the caller falls back to the engine's directions.
	 */
	bool ResolveUnitLevelsWithICU(const FString& Plain, uint8 ParagraphLevel, TArray<uint8>& OutUnitLevels)
	{
		const int32 Length = Plain.Len();
		TArray<UChar> Units;
		Units.SetNumUninitialized(Length);
		for (int32 i = 0; i < Length; i++)
		{
			Units[i] = (UChar)Plain[i];
		}
		UErrorCode Status = U_ZERO_ERROR;
		UBiDi* BiDi = ubidi_openSized(Length, 0, &Status);
		if (BiDi == nullptr)
		{
			return false;
		}
		bool bResolved = false;
		if (U_SUCCESS(Status))
		{
			ubidi_setPara(BiDi, Units.GetData(), Length, (UBiDiLevel)ParagraphLevel, nullptr, &Status);
		}
		if (U_SUCCESS(Status))
		{
			const UBiDiLevel* Levels = ubidi_getLevels(BiDi, &Status);
			if (U_SUCCESS(Status) && Levels != nullptr)
			{
				OutUnitLevels.SetNumUninitialized(Length);
				for (int32 i = 0; i < Length; i++)
				{
					OutUnitLevels[i] = (uint8)(Levels[i] & ~UBIDI_LEVEL_OVERRIDE);
				}
				bResolved = true;
			}
		}
		ubidi_close(BiDi);
		return bResolved;
	}
#endif

	/**
	 * Bidi embedding level per element, from the paragraph as UTF-16. Levels rather than one direction per element are
	 * what lets a line be put in visual order by rule L2: a number inside right-to-left text is a level-2 run that reads
	 * left to right inside the level-1 run around it.
	 */
	void ResolveLevels(const TArray<FDreamShapeElement>& Elements, EDreamTextFlowDirection FlowDirection, TArray<uint8>& OutLevels, bool& OutBaseRightToLeft)
	{
		OutLevels.Init(0, Elements.Num());
		OutBaseRightToLeft = false;
		// A paragraph laid out left to right with not one character that could turn it: every element is at level 0 --
		// with no right-to-left letter and no embedding, the bidi algorithm resolves everything to the paragraph's level
		// -- and asking it costs far more than the rest of a short label's layout.
		if (FlowDirection != EDreamTextFlowDirection::RightToLeft)
		{
			bool bAnyThatCanTurn = false;
			for (const FDreamShapeElement& Element : Elements)
			{
				if (CanTurnRightToLeft(Element.Codepoint))
				{
					bAnyThatCanTurn = true;
					break;
				}
			}
			if (!bAnyThatCanTurn)
			{
				return;
			}
		}
		FString Plain;
		TArray<int32> PlainStart;
		PlainStart.SetNumUninitialized(Elements.Num());
		for (int32 i = 0; i < Elements.Num(); i++)
		{
			PlainStart[i] = Plain.Len();
			const uint32 C = Elements[i].Codepoint;
			if (C >= 0x10000)
			{
				const uint32 V = C - 0x10000;
				Plain.AppendChar((TCHAR)(0xD800 + (V >> 10)));
				Plain.AppendChar((TCHAR)(0xDC00 + (V & 0x3FF)));
			}
			else
			{
				Plain.AppendChar((TCHAR)C);
			}
		}

		// Auto asks the bidi algorithm, which reads the first strong character and answers left-to-right
		// for a string that has none. A forced direction says so instead, which is what a UI whose
		// direction is the game's setting rather than the string's content needs.
		switch (FlowDirection)
		{
		case EDreamTextFlowDirection::LeftToRight: OutBaseRightToLeft = false; break;
		case EDreamTextFlowDirection::RightToLeft: OutBaseRightToLeft = true; break;
		default: OutBaseRightToLeft = TextBiDi::ComputeBaseDirection(Plain) == TextBiDi::ETextDirection::RightToLeft; break;
		}
		const uint8 ParagraphLevel = OutBaseRightToLeft ? 1 : 0;

		TArray<uint8> UnitLevels;
		bool bResolved = false;
#if UE_ENABLE_ICU
		bResolved = ResolveUnitLevelsWithICU(Plain, ParagraphLevel, UnitLevels);
#endif
		if (!bResolved)
		{
			// The engine's bidi reports a direction per run and no level. One level above the paragraph's for the
			// runs that go against it is right for every text that has no embedding inside an embedding.
			UnitLevels.Init(ParagraphLevel, Plain.Len());
			TArray<TextBiDi::FTextDirectionInfo> Infos;
			TextBiDi::ComputeTextDirection(Plain, OutBaseRightToLeft ? TextBiDi::ETextDirection::RightToLeft : TextBiDi::ETextDirection::LeftToRight, Infos);
			for (const TextBiDi::FTextDirectionInfo& Info : Infos)
			{
				const bool bRTL = Info.TextDirection == TextBiDi::ETextDirection::RightToLeft;
				const uint8 Level = bRTL ? 1 : (OutBaseRightToLeft ? 2 : 0);
				const int32 End = FMath::Min(Info.StartIndex + Info.Length, Plain.Len());
				for (int32 p = FMath::Max(0, Info.StartIndex); p < End; p++)
				{
					UnitLevels[p] = Level;
				}
			}
		}
		for (int32 i = 0; i < Elements.Num(); i++)
		{
			OutLevels[i] = UnitLevels.IsValidIndex(PlainStart[i]) ? UnitLevels[PlainStart[i]] : ParagraphLevel;
		}
	}

	/**
	 * The face a grapheme cluster is drawn from: one face for the whole cluster, so a base and its marks never come from
	 * two fonts. A bold or italic cluster tries the font's face for that style first, then the regular chain in order.
	 * Styled faces are never part of that chain. A space or a comma between two words of a fallback face's script is
	 * drawn by the first face that has it too, not by the face of the words around it: what Blink's shaper keeps (it
	 * reshapes with a fallback only the clusters the font before it could not draw) and what Slate's per-grapheme fallback
	 * picks. Taking the neighbours' face instead set Hebrew text with Arial's wider spaces and broke its lines earlier
	 * than both.
	 */
	int32 ChooseFace(UDreamUIFontData_BaseObject* Font, int32 FaceCount, const TArray<FDreamShapeElement>& Elements, int32 ClusterStart, int32 ClusterEnd)
	{
		auto HasCluster = [Font, &Elements, ClusterStart, ClusterEnd](int32 Face)
		{
			for (int32 k = ClusterStart; k < ClusterEnd; k++)
			{
				if (!Font->FaceHasCodepoint(Face, ShapingCodepoint(Elements[k].Codepoint)))
				{
					return false;
				}
			}
			return true;
		};
		const FDreamShapeElement& Base = Elements[ClusterStart];
		const uint32 BaseCodepoint = ShapingCodepoint(Base.Codepoint);
		const int32 StyledFace = (Base.bBold || Base.bItalic) ? Font->GetStyledFace(Base.bBold, Base.bItalic) : 0;
		if (StyledFace > 0 && HasCluster(StyledFace))
		{
			return StyledFace;
		}
		for (int32 F = 0; F < FaceCount; F++)
		{
			if (HasCluster(F))
			{
				return F;
			}
		}
		// No face has the whole cluster: the first that has its base, so the letter itself is never a box.
		if (StyledFace > 0 && Font->FaceHasCodepoint(StyledFace, BaseCodepoint))
		{
			return StyledFace;
		}
		for (int32 F = 0; F < FaceCount; F++)
		{
			if (Font->FaceHasCodepoint(F, BaseCodepoint))
			{
				return F;
			}
		}
		return 0;//nobody has it: the primary face's .notdef
	}
}
#endif

bool FDreamTextShaper::ShapeParagraph(const TArray<FDreamShapeElement>& Elements, UDreamUIFontData_BaseObject* Font, bool bUseKerning, EDreamTextFlowDirection FlowDirection, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft, bool bLigatures, TArray<uint8>* OutBidiLevels)
{
	OutRuns.Reset();
	OutBaseRightToLeft = false;
	if (OutBidiLevels != nullptr)
	{
		OutBidiLevels->Reset();
	}
#if !WITH_HARFBUZZ
	return false;
#else
	using namespace DreamTextShaperLocal;
	if (!CanShape(Font) || Elements.Num() == 0)
	{
		return false;
	}

	// Itemize: level, script, face, style, per grapheme cluster.
	TArray<uint8> Levels;
	bool bBaseRightToLeft = false;
	ResolveLevels(Elements, FlowDirection, Levels, bBaseRightToLeft);
	OutBaseRightToLeft = bBaseRightToLeft;

	hb_unicode_funcs_t* Unicode = hb_unicode_funcs_get_default();
	const int32 FaceCount = Font->GetFaceCount();
	TArray<FItem> Items;
	Items.SetNum(Elements.Num());
	hb_script_t LastScript = HB_SCRIPT_COMMON;
	int32 ClusterStart = INDEX_NONE;
	for (int32 i = 0; i < Elements.Num(); i++)
	{
		const FDreamShapeElement& E = Elements[i];
		FItem& Item = Items[i];
		Item.Level = Levels[i];
		Item.Size = E.Size;
		Item.bBold = E.bBold;
		Item.bUnshaped = E.bUnshaped;
		if (E.bUnshaped)
		{
			ClusterStart = INDEX_NONE;
			continue;
		}
		// The rest of a grapheme cluster rides with the element that starts it -- same face, same script, same run --
		// so a base and its combining marks are shaped together and one font draws all of them.
		if (!E.bGraphemeStart && ClusterStart != INDEX_NONE)
		{
			Item = Items[ClusterStart];
			Levels[i] = Item.Level;
			continue;
		}
		ClusterStart = i;
		int32 ClusterEnd = i + 1;
		while (ClusterEnd < Elements.Num() && !Elements[ClusterEnd].bGraphemeStart && !Elements[ClusterEnd].bUnshaped)
		{
			ClusterEnd++;
		}
		const uint32 C = ShapingCodepoint(E.Codepoint);
		// Neutral characters (punctuation, spaces, marks) take the script of what came before them,
		// so a run is not cut on every comma.
		hb_script_t Script = hb_unicode_script(Unicode, C);
		const bool bNeutral = ScriptIsNeutral(Script);
		if (bNeutral)
		{
			Script = LastScript;
		}
		else
		{
			LastScript = Script;
		}
		Item.Script = Script;
		Item.FaceIndex = ChooseFace(Font, FaceCount, Elements, i, ClusterEnd);
	}
	// Leading neutrals before the first scripted character take that script.
	for (int32 i = 0; i < Items.Num(); i++)
	{
		if (Items[i].bUnshaped)continue;
		if (!ScriptIsNeutral(Items[i].Script))
		{
			for (int32 j = 0; j < i; j++)
			{
				if (!Items[j].bUnshaped && ScriptIsNeutral(Items[j].Script))Items[j].Script = Items[i].Script;
			}
			break;
		}
	}
	if (OutBidiLevels != nullptr)
	{
		*OutBidiLevels = Levels;
	}

	// Cut runs and shape each.
	const float BoldRatio = Font->GetBoldRatio();
	// 'locl' picks between the forms a script shares across languages -- the Han glyphs Chinese and
	// Japanese draw differently, Serbian Cyrillic italics. That choice has to follow the culture the
	// GAME is running in; hb_language_get_default() reads the process locale, which is the machine's.
	const FString LanguageName = FInternationalization::Get().GetCurrentLanguage()->GetName();
	const hb_language_t GameLanguage = LanguageName.IsEmpty()
		? hb_language_get_default()
		: hb_language_from_string(TCHAR_TO_UTF8(*LanguageName), -1);
	hb_buffer_t* Buffer = hb_buffer_create();
	// Each run is shaped with the paragraph around it as context, the way a browser hands the shaper its text: a cut
	// that is not the script's own -- a colour, size or weight that changes mid-word, a tag edge -- leaves an Arabic
	// letter its joined form. A glyph's cluster is an index into this array, so it is the glyph's element.
	TArray<hb_codepoint_t> Codepoints;
	Codepoints.SetNumUninitialized(Elements.Num());
	for (int32 i = 0; i < Elements.Num(); i++)
	{
		Codepoints[i] = (hb_codepoint_t)ShapingCodepoint(Elements[i].Codepoint);
	}
	int32 RunStart = 0;
	while (RunStart < Elements.Num())
	{
		int32 RunEnd = RunStart + 1;
		while (RunEnd < Elements.Num() && Items[RunEnd].SameRun(Items[RunStart]) && !Elements[RunEnd].bRunBreakBefore)
		{
			RunEnd++;
		}
		const FItem& Item = Items[RunStart];
		if (!Item.bUnshaped)
		{
			const bool bRightToLeft = (Item.Level & 1) != 0;
			FDreamShapedRun Run;
			Run.ElementStart = RunStart;
			Run.ElementEnd = RunEnd;
			Run.bRightToLeft = bRightToLeft;
			Run.BidiLevel = Item.Level;
			Run.FaceIndex = Item.FaceIndex;
			Run.Size = Item.Size;
			Run.bBold = Item.bBold;
			// A real bold face has its weight in its outlines and advances already; only bold that has to be made up
			// widens the advances and emboldens the glyphs.
			Run.bSyntheticBold = Item.bBold && !EnumHasAnyFlags(Font->GetFaceStyleFlags(Item.FaceIndex), EDreamUIFontFaceStyle::Bold);

			hb_font_t* HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(Item.FaceIndex, Item.Size));
			if (HBFont == nullptr)
			{
				HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(0, Item.Size));
			}
			if (HBFont != nullptr)
			{
				hb_buffer_clear_contents(Buffer);
				hb_buffer_set_direction(Buffer, bRightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
				hb_buffer_set_script(Buffer, Item.Script);
				hb_buffer_set_language(Buffer, GameLanguage);
				hb_buffer_set_cluster_level(Buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
				// The run's own code points, with up to five on either side as pre- and post-context.
				hb_buffer_add_codepoints(Buffer, Codepoints.GetData(), Codepoints.Num(), (unsigned int)RunStart, RunEnd - RunStart);

				// Kerning as asked. Ligatures and contextual alternates as a browser has them when the caller allows
				// them: one glyph may then cover several characters, which the layout splits its carets across.
				// Otherwise liga and clig are off so a code point keeps its own glyph, which is what per-character
				// animation needs, and calt goes off with them where it is only a matter of style.
				hb_feature_t Features[4];
				int32 FeatureCount = 0;
				Features[FeatureCount++] = { HB_TAG('k','e','r','n'), bUseKerning ? 1u : 0u, 0, (unsigned int)-1 };
				Features[FeatureCount++] = { HB_TAG('l','i','g','a'), bLigatures ? 1u : 0u, 0, (unsigned int)-1 };
				Features[FeatureCount++] = { HB_TAG('c','l','i','g'), bLigatures ? 1u : 0u, 0, (unsigned int)-1 };
				if (!bLigatures && ContextualAlternatesAreStylistic(Item.Script))
				{
					Features[FeatureCount++] = { HB_TAG('c','a','l','t'), 0u, 0, (unsigned int)-1 };
				}
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextShape);
					hb_shape(HBFont, Buffer, Features, (unsigned int)FeatureCount);
				}

				unsigned int GlyphCount = 0;
				hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &GlyphCount);
				hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, &GlyphCount);
				// Synthetic bold widens a cluster by the emboldening once, on its last glyph: a combining mark has no
				// advance of its own to widen, and giving it one pushed it off the letter it sits on.
				const float BoldAdvance = Run.bSyntheticBold ? Item.Size * BoldRatio : 0.0f;
				Run.Glyphs.Reserve(GlyphCount);
				for (unsigned int g = 0; g < GlyphCount; g++)
				{
					const bool bLastOfCluster = g + 1 == GlyphCount || Infos[g + 1].cluster != Infos[g].cluster;
					FDreamShapedGlyph Glyph;
					Glyph.FaceIndex = Item.FaceIndex;
					Glyph.GlyphIndex = Infos[g].codepoint;
					Glyph.ElementIndex = (int32)Infos[g].cluster;
					Glyph.XAdvance = Positions[g].x_advance / 64.0f + (bLastOfCluster ? BoldAdvance : 0.0f);
					// y_advance is deliberately dropped: the layout is horizontal only, so it is zero
					// for every run it ever asks for. It comes back with vertical text, not before.
					Glyph.XOffset = Positions[g].x_offset / 64.0f;
					Glyph.YOffset = Positions[g].y_offset / 64.0f;
					Run.Glyphs.Add(Glyph);
				}
			}
			OutRuns.Add(MoveTemp(Run));
		}
		RunStart = RunEnd;
	}
	hb_buffer_destroy(Buffer);
	return true;
#endif
}
