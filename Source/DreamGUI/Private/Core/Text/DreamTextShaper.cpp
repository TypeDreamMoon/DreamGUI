// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextShaper.h"
#include "Core/Text/DreamTextShapeCache.h"
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

bool FDreamTextShaper::CanTurnRightToLeft(uint32 C)
{
	// A right-to-left letter (Hebrew, Arabic, Syriac, Thaana, NKo, Samaritan, Mandaic and their presentation forms, the
	// right-to-left scripts of the supplementary planes), an Arabic digit, or an explicit direction control.
	return (C >= 0x0590 && C <= 0x08FF)
		|| (C >= 0xFB1D && C <= 0xFDFF)
		|| (C >= 0xFE70 && C <= 0xFEFF)
		|| (C >= 0x10800 && C <= 0x10FFF)
		|| (C >= 0x1E800 && C <= 0x1EFFF)
		|| C == 0x200E || C == 0x200F || C == 0x061C
		|| (C >= 0x202A && C <= 0x202E)
		|| (C >= 0x2066 && C <= 0x2069);
}

#if WITH_HARFBUZZ
namespace DreamTextShaperLocal
{
	static_assert(sizeof(hb_codepoint_t) == sizeof(uint32), "the paragraph's code points are handed to HarfBuzz as they are");

	struct FItem
	{
		uint8 Level = 0;
		hb_script_t Script = HB_SCRIPT_COMMON;
		int32 FaceIndex = 0;
		float Size = 0.0f;
		bool bBold = false;
		bool bUnshaped = false;
		/** What the face resolver said of FaceIndex; it follows the face, so it cuts no run of its own. */
		bool bColorFace = false;
		uint8 LanguageIndex = 0;

		bool SameRun(const FItem& Other) const
		{
			return Level == Other.Level && Script == Other.Script && FaceIndex == Other.FaceIndex
				&& Size == Other.Size && bBold == Other.bBold && bUnshaped == Other.bUnshaped && LanguageIndex == Other.LanguageIndex;
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
				if (FDreamTextShaper::CanTurnRightToLeft(Element.Codepoint))
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
	 * The paragraph as HarfBuzz is handed it: each element's code point, then the rest of its sequence. A glyph's cluster
	 * is an index into Codepoints, and ElementOf maps it back to the element it belongs to.
	 */
	struct FParagraphCodepoints
	{
		TArray<uint32> Codepoints;
		/** Where each element's code points start, and one past the last element: their end. */
		TArray<int32> ElementStart;
		TArray<int32> ElementOf;

		void Build(const TArray<FDreamShapeElement>& Elements, const TArray<uint32>* Sequences)
		{
			Codepoints.Reset(Elements.Num());
			ElementOf.Reset(Elements.Num());
			ElementStart.SetNumUninitialized(Elements.Num() + 1);
			for (int32 i = 0; i < Elements.Num(); i++)
			{
				const FDreamShapeElement& Element = Elements[i];
				ElementStart[i] = Codepoints.Num();
				Codepoints.Add(ShapingCodepoint(Element.Codepoint));
				ElementOf.Add(i);
				if (Sequences != nullptr && Element.SequenceCount > 0 && Element.SequenceStart >= 0
					&& (int64)Element.SequenceStart + Element.SequenceCount <= Sequences->Num())
				{
					for (int32 k = 0; k < Element.SequenceCount; k++)
					{
						Codepoints.Add((*Sequences)[Element.SequenceStart + k]);
						ElementOf.Add(i);
					}
				}
			}
			ElementStart[Elements.Num()] = Codepoints.Num();
		}

		/** The code points of elements [First, End). */
		TConstArrayView<uint32> Range(int32 First, int32 End) const
		{
			return TConstArrayView<uint32>(Codepoints.GetData() + ElementStart[First], ElementStart[End] - ElementStart[First]);
		}
	};

	/** The game's current language as a table of one: kept between calls on the game thread, made into Storage elsewhere. */
	const TArray<FDreamTextLanguage>& GetGameLanguages(TArray<FDreamTextLanguage>& Storage)
	{
		const FString Current = FInternationalization::Get().GetCurrentLanguage()->GetName();
		if (IsInGameThread())
		{
			static TArray<FDreamTextLanguage> Kept;
			if (Kept.Num() != 1 || !Kept[0].Name.Equals(Current, ESearchCase::CaseSensitive))
			{
				Kept.Reset();
				Kept.Add(FDreamTextLanguage::Make(Current));
			}
			return Kept;
		}
		Storage.Reset();
		Storage.Add(FDreamTextLanguage::Make(Current));
		return Storage;
	}

	/**
	 * The hb_language_t a language is shaped in. HarfBuzz interns languages, so a name's answer never changes: it is kept on
	 * the language (FDreamTextLanguage::ShapingLanguage) and, on the game thread, by name, so the UTF-8 conversion and
	 * HarfBuzz's own lookup are paid once per language rather than once per call. hb_language_get_default() would read the
	 * process locale, which is the machine's: it answers only for a language with no name at all.
	 */
	hb_language_t ShapingLanguageOf(const FDreamTextLanguage& Language)
	{
		if (Language.ShapingLanguage != nullptr)
		{
			return static_cast<hb_language_t>(Language.ShapingLanguage);
		}
		hb_language_t Result = nullptr;
		if (Language.Name.IsEmpty())
		{
			Result = hb_language_get_default();
		}
		else if (IsInGameThread())
		{
			static TMap<FString, hb_language_t> KnownLanguages;
			if (const hb_language_t* Found = KnownLanguages.Find(Language.Name))
			{
				Result = *Found;
			}
			else
			{
				Result = hb_language_from_string(TCHAR_TO_UTF8(*Language.Name), -1);
				KnownLanguages.Add(Language.Name, Result);
			}
		}
		else
		{
			Result = hb_language_from_string(TCHAR_TO_UTF8(*Language.Name), -1);
		}
		Language.ShapingLanguage = Result;
		return Result;
	}

	/** The game thread's shaping buffer, made once and cleared before every use. */
	struct FSharedShapingBuffer
	{
		hb_buffer_t* Buffer = nullptr;
		bool bInUse = false;

		~FSharedShapingBuffer()
		{
			if (Buffer != nullptr)
			{
				hb_buffer_destroy(Buffer);
			}
		}
	};

	FSharedShapingBuffer& GetSharedShapingBuffer()
	{
		static FSharedShapingBuffer Shared;
		return Shared;
	}

	/**
	 * A shaping call's HarfBuzz buffer: on the game thread the one kept between calls, so a paragraph does not pay for
	 * making and freeing one; anywhere else, or for a call made while that one is in use, its own.
	 */
	struct FShapingBuffer
	{
		hb_buffer_t* Buffer = nullptr;
		bool bShared = false;

		FShapingBuffer()
		{
			if (IsInGameThread())
			{
				FSharedShapingBuffer& Shared = GetSharedShapingBuffer();
				if (!Shared.bInUse)
				{
					if (Shared.Buffer == nullptr)
					{
						Shared.Buffer = hb_buffer_create();
					}
					Shared.bInUse = true;
					Buffer = Shared.Buffer;
					bShared = true;
					return;
				}
			}
			Buffer = hb_buffer_create();
		}
		~FShapingBuffer()
		{
			if (bShared)
			{
				GetSharedShapingBuffer().bInUse = false;
			}
			else
			{
				hb_buffer_destroy(Buffer);
			}
		}
		FShapingBuffer(const FShapingBuffer&) = delete;
		FShapingBuffer& operator=(const FShapingBuffer&) = delete;
	};

	/*
	 * The shape cache's segments. A run is cut where its words end -- at spaces, and around CJK characters and symbols, as
	 * Blink's word cache cuts it -- but only where the cut changes nothing: HarfBuzz must shape the two sides apart exactly
	 * as it shapes them together. So never inside a cluster, and only next to a base no lookup of the face reaches past
	 * (FDreamTextShapeCache::FFaceRules). The one thing HarfBuzz reads across a cut is the joining context, which the key
	 * carries.
	 */

	bool IsWordDelimiter(uint32 C)
	{
		return C == 0x0020 || C == 0x00A0;
	}

	/** Characters that stand as words of their own: CJK ideographs, kana, bopomofo, the CJK symbols and fullwidth forms, and the emoji and pictographs. */
	bool IsCjkOrSymbol(uint32 C)
	{
		return (C >= 0x2E80 && C <= 0x2FFF)
			|| (C >= 0x3000 && C <= 0x33FF)
			|| (C >= 0x3400 && C <= 0x4DBF)
			|| (C >= 0x4E00 && C <= 0x9FFF)
			|| (C >= 0xF900 && C <= 0xFAFF)
			|| (C >= 0xFE30 && C <= 0xFE4F)
			|| (C >= 0xFF00 && C <= 0xFFEF)
			|| (C >= 0x20000 && C <= 0x3FFFF)
			|| (C >= 0x2600 && C <= 0x27BF)
			|| (C >= 0x1F000 && C <= 0x1FAFF);
	}

	/** Runs whose CJK letters may stand as words: inside another script's word a shaper reads its neighbours (Indic 'init' asks whether a letter stands before a syllable). */
	bool IsCjkScript(hb_script_t Script)
	{
		return Script == HB_SCRIPT_HAN || Script == HB_SCRIPT_HIRAGANA || Script == HB_SCRIPT_KATAKANA
			|| Script == HB_SCRIPT_BOPOMOFO || Script == HB_SCRIPT_COMMON;
	}

	/** Letters, marks, format and unassigned characters: what HarfBuzz's shapers count as part of a word (Cf through Mn). */
	bool IsWordLike(hb_unicode_general_category_t Category)
	{
		return Category >= HB_UNICODE_GENERAL_CATEGORY_FORMAT && Category <= HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK;
	}

	bool IsMarkOrFormat(hb_unicode_general_category_t Category)
	{
		return Category == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || Category == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK
			|| Category == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK || Category == HB_UNICODE_GENERAL_CATEGORY_FORMAT;
	}

	/**
	 * Code points HarfBuzz may fold into the cluster before them, which a cut must never part from it: marks and format
	 * characters (variation selectors, joiners, tags), skin tone modifiers, the halfwidth kana voicing marks, and the vowel
	 * and final jamo that compose with the jamo before them.
	 */
	bool IsClusterContinuation(hb_unicode_funcs_t* Unicode, uint32 C)
	{
		if (FDreamUIText_CodePoint::IsSkinToneModifier(C) || (C >= 0xFF9E && C <= 0xFF9F) || (C >= 0x1160 && C <= 0x11FF) || (C >= 0xD7B0 && C <= 0xD7FF))
		{
			return true;
		}
		return IsMarkOrFormat(hb_unicode_general_category(Unicode, C));
	}

	/**
	 * Whether HarfBuzz reads the code points around a run when it shapes it in this script. Only joining does: the Arabic
	 * shaper looks past the run's ends for the letters it joins to. A script not listed here is taken to read them, so a
	 * shaper this does not know of never gets a key without its context.
	 */
	bool ScriptReadsContext(hb_script_t Script)
	{
		switch (Script)
		{
		case HB_SCRIPT_COMMON: case HB_SCRIPT_INHERITED: case HB_SCRIPT_UNKNOWN:
		case HB_SCRIPT_LATIN: case HB_SCRIPT_GREEK: case HB_SCRIPT_CYRILLIC: case HB_SCRIPT_ARMENIAN: case HB_SCRIPT_GEORGIAN:
		case HB_SCRIPT_HEBREW: case HB_SCRIPT_THAANA: case HB_SCRIPT_ETHIOPIC: case HB_SCRIPT_CHEROKEE:
		case HB_SCRIPT_HAN: case HB_SCRIPT_HIRAGANA: case HB_SCRIPT_KATAKANA: case HB_SCRIPT_BOPOMOFO: case HB_SCRIPT_HANGUL: case HB_SCRIPT_YI:
		case HB_SCRIPT_THAI: case HB_SCRIPT_LAO: case HB_SCRIPT_TIBETAN: case HB_SCRIPT_MYANMAR: case HB_SCRIPT_KHMER:
		case HB_SCRIPT_DEVANAGARI: case HB_SCRIPT_BENGALI: case HB_SCRIPT_GURMUKHI: case HB_SCRIPT_GUJARATI: case HB_SCRIPT_ORIYA:
		case HB_SCRIPT_TAMIL: case HB_SCRIPT_TELUGU: case HB_SCRIPT_KANNADA: case HB_SCRIPT_MALAYALAM: case HB_SCRIPT_SINHALA:
			return false;
		default:
			return true;
		}
	}

	/** What a run's cuts are decided from. */
	struct FCutContext
	{
		const TArray<FDreamShapeElement>& Elements;
		const FParagraphCodepoints& Text;
		hb_unicode_funcs_t* Unicode;
		hb_font_t* Font;
		const FDreamTextShapeCache::FFaceRules& Rules;
		bool bRightToLeft;
		bool bCjkRun;
		/** The run's end, exclusive: the last cluster stops there. */
		int32 RunEnd;
	};

	/**
	 * A base no lookup of the face reaches past: what a cut may stand next to. Its cluster is code points [First, End) of
	 * the paragraph, the base first. The glyph checked must be the one HarfBuzz draws it with: not for a character the face
	 * lacks (HarfBuzz draws its decomposition, or the space glyph for a space), nor one with a canonical decomposition where
	 * HarfBuzz may draw its parts (the Brahmic, Khmer, Myanmar and universal shapers decompose whatever the face has, and
	 * every shaper does when marks follow it; the default shaper the CJK runs get keeps the face's glyph of one standing
	 * alone), nor one a variation selector or a mark of its cluster turns into another glyph. Unassigned code points are out
	 * too: HarfBuzz skips some of them as default-ignorable.
	 */
	bool IsQuietBase(const FCutContext& Cut, int32 First, int32 End)
	{
		const uint32 C = Cut.Text.Codepoints[First];
		const hb_unicode_general_category_t Category = hb_unicode_general_category(Cut.Unicode, C);
		if (IsClusterContinuation(Cut.Unicode, C) || Category == HB_UNICODE_GENERAL_CATEGORY_CONTROL || Category == HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED)
		{
			return false;
		}
		hb_codepoint_t Glyph = 0;
		hb_codepoint_t PartA = 0;
		hb_codepoint_t PartB = 0;
		const bool bMayDecompose = !Cut.bCjkRun || End - First > 1;
		if (!hb_font_get_nominal_glyph(Cut.Font, C, &Glyph) || (bMayDecompose && hb_unicode_decompose(Cut.Unicode, C, &PartA, &PartB))
			|| Cut.Rules.IsTouched(Glyph))
		{
			return false;
		}
		for (int32 Index = First + 1; Index < End; Index++)
		{
			const uint32 Next = Cut.Text.Codepoints[Index];
			hb_codepoint_t Other = 0;
			if ((hb_font_get_variation_glyph(Cut.Font, C, Next, &Other) && Cut.Rules.IsTouched(Other)) || hb_unicode_compose(Cut.Unicode, C, Next, &Other))
			{
				return false;
			}
		}
		// A right-to-left run draws a bracket's mirror image, which is another glyph when the face has it.
		if (Cut.bRightToLeft)
		{
			const hb_codepoint_t Mirrored = hb_unicode_mirroring(Cut.Unicode, C);
			hb_codepoint_t MirroredGlyph = 0;
			if (Mirrored != C && hb_font_get_nominal_glyph(Cut.Font, Mirrored, &MirroredGlyph) && Cut.Rules.IsTouched(MirroredGlyph))
			{
				return false;
			}
		}
		return true;
	}

	/** Whether the run may be cut between the cluster that starts at LeftCluster and the one that starts at RightCluster. */
	bool CanCutBefore(const FCutContext& Cut, int32 LeftCluster, int32 RightCluster)
	{
		const TArray<uint32>& Codepoints = Cut.Text.Codepoints;
		const int32 LeftStart = Cut.Text.ElementStart[LeftCluster];
		const int32 RightStart = Cut.Text.ElementStart[RightCluster];
		const uint32 Right = Codepoints[RightStart];
		const uint32 LeftBase = Codepoints[LeftStart];
		const uint32 LeftLast = Codepoints[RightStart - 1];
		// Never between a cluster and what HarfBuzz may fold into it, after a joiner, or between two regional indicators.
		if (IsClusterContinuation(Cut.Unicode, Right) || LeftLast == FDreamUIText_CodePoint::UNICODE_ZWJ
			|| (FDreamUIText_CodePoint::IsRegionalIndicator(Right) && FDreamUIText_CodePoint::IsRegionalIndicator(LeftLast)))
		{
			return false;
		}
		// Nor inside a fraction: HarfBuzz gives the digits on either side of a fraction slash their numerator and denominator
		// forms by reading them off the buffer, not through a lookup.
		if (Right == 0x2044 || LeftLast == 0x2044
			|| (hb_unicode_general_category(Cut.Unicode, Right) == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER
				&& hb_unicode_general_category(Cut.Unicode, LeftLast) == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER))
		{
			return false;
		}
		// Only where a word ends.
		auto IsWordEdge = [&Cut](uint32 C)
		{
			if (IsWordDelimiter(C))
			{
				return true;
			}
			return IsCjkOrSymbol(C) && (Cut.bCjkRun || !IsWordLike(hb_unicode_general_category(Cut.Unicode, C)));
		};
		if (!IsWordEdge(Right) && !IsWordEdge(LeftBase))
		{
			return false;
		}
		// And only where no lookup reaches across: one side of the cut is a base none of them touches. The right cluster
		// runs to the next element that starts one.
		int32 RightEnd = RightCluster + 1;
		while (RightEnd < Cut.RunEnd && !Cut.Elements[RightEnd].bGraphemeStart)
		{
			RightEnd++;
		}
		return IsQuietBase(Cut, RightStart, Cut.Text.ElementStart[RightEnd]) || (RightStart - LeftStart == 1 && IsQuietBase(Cut, LeftStart, RightStart));
	}

	/** A stretch of a run looked up and stored as one, and where its glyphs went. */
	struct FSegment
	{
		int32 ElementStart = 0;
		int32 ElementEnd = 0;
		/** The paragraph code point its glyphs' clusters are counted from. */
		int32 FlatBase = 0;
		/** Its glyphs in the run's scratch; INDEX_NONE until they are known. */
		int32 GlyphStart = INDEX_NONE;
		int32 GlyphCount = 0;
		/**
		 * How many segments its glyphs stand for: 1; more for neighbours shaped together whose glyphs could not be told
		 * apart (and so were not stored); 0 for a segment whose glyphs the one before it holds.
		 */
		int32 Span = 1;
	};

	using FSegmentArray = TArray<FSegment, TInlineAllocator<32>>;

	void FindSegments(const FCutContext& Cut, int32 RunStart, int32 RunEnd, FSegmentArray& OutSegments)
	{
		OutSegments.Reset();
		int32 SegmentStart = RunStart;
		if (Cut.Rules.bSegmentable)
		{
			int32 LeftCluster = RunStart;
			for (int32 Element = RunStart + 1; Element < RunEnd; Element++)
			{
				if (!Cut.Elements[Element].bGraphemeStart)
				{
					continue;
				}
				if (CanCutBefore(Cut, LeftCluster, Element))
				{
					FSegment& Segment = OutSegments.AddDefaulted_GetRef();
					Segment.ElementStart = SegmentStart;
					Segment.ElementEnd = Element;
					SegmentStart = Element;
				}
				LeftCluster = Element;
			}
		}
		FSegment& Last = OutSegments.AddDefaulted_GetRef();
		Last.ElementStart = SegmentStart;
		Last.ElementEnd = RunEnd;
	}

	/** What every key of a run shares. */
	struct FRunKey
	{
		FDreamUIFontFaceIdentity Face;
		float Size = 0.0f;
		hb_script_t Script = HB_SCRIPT_COMMON;
		hb_language_t Language = nullptr;
		uint8 Flags = 0;
		bool bReadsContext = false;
	};

	void MakeKey(const FRunKey& Run, const TArray<uint32>& Codepoints, int32 Begin, int32 End, FDreamTextShapeCache::FKey& OutKey)
	{
		OutKey.Face = Run.Face;
		OutKey.Size = Run.Size;
		OutKey.Script = (uint32)Run.Script;
		OutKey.Language = Run.Language;
		OutKey.Flags = Run.Flags;
		// Joining is what reads past a segment, and it never changes a space, so spaces alone need no context.
		bool bOnlySpaces = true;
		for (int32 Index = Begin; Index < End && bOnlySpaces; Index++)
		{
			bOnlySpaces = IsWordDelimiter(Codepoints[Index]);
		}
		int32 ContextStart = Begin;
		int32 ContextEnd = End;
		if (Run.bReadsContext && !bOnlySpaces)
		{
			// HarfBuzz keeps five code points of context on either side, and its joining stops at the first that does not
			// join: a space does not.
			constexpr int32 MaxContext = 5;
			while (ContextStart > 0 && Begin - ContextStart < MaxContext && !IsWordDelimiter(Codepoints[ContextStart - 1]))
			{
				ContextStart--;
			}
			while (ContextEnd < Codepoints.Num() && ContextEnd - End < MaxContext && !IsWordDelimiter(Codepoints[ContextEnd]))
			{
				ContextEnd++;
			}
		}
		OutKey.PreContext = Begin - ContextStart;
		OutKey.SegmentLength = End - Begin;
		OutKey.Codepoints.Reset();
		OutKey.Codepoints.Append(Codepoints.GetData() + ContextStart, ContextEnd - ContextStart);
	}

	/** What every HarfBuzz call of a run is set up with. */
	struct FRunSetup
	{
		hb_font_t* Font = nullptr;
		hb_direction_t Direction = HB_DIRECTION_LTR;
		hb_script_t Script = HB_SCRIPT_COMMON;
		hb_language_t Language = nullptr;
		hb_feature_t Features[4];
		int32 FeatureCount = 0;
	};

	/**
	 * Shapes code points [Begin, End) of the paragraph in one HarfBuzz call, with up to five on either side as pre- and
	 * post-context, and appends the glyphs to Out, clusters counted from Begin.
	 */
	void ShapeRange(hb_buffer_t* Buffer, const FRunSetup& Setup, const TArray<uint32>& Codepoints, int32 Begin, int32 End, TArray<FDreamTextShapeCache::FGlyph>& Out)
	{
		hb_buffer_clear_contents(Buffer);
		hb_buffer_set_direction(Buffer, Setup.Direction);
		hb_buffer_set_script(Buffer, Setup.Script);
		hb_buffer_set_language(Buffer, Setup.Language);
		hb_buffer_set_cluster_level(Buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
		hb_buffer_add_codepoints(Buffer, reinterpret_cast<const hb_codepoint_t*>(Codepoints.GetData()), Codepoints.Num(), (unsigned int)Begin, End - Begin);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextShape);
			hb_shape(Setup.Font, Buffer, Setup.Features, (unsigned int)Setup.FeatureCount);
		}
		FDreamTextShapeCache::CountShape(End - Begin);
		unsigned int GlyphCount = 0;
		const hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &GlyphCount);
		const hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, &GlyphCount);
		Out.Reserve(Out.Num() + (int32)GlyphCount);
		for (unsigned int g = 0; g < GlyphCount; g++)
		{
			FDreamTextShapeCache::FGlyph& Glyph = Out.AddDefaulted_GetRef();
			Glyph.GlyphId = Infos[g].codepoint;
			Glyph.Cluster = (int32)Infos[g].cluster - Begin;
			// y_advance is deliberately dropped: the layout is horizontal only, so it is zero for every run it ever asks
			// for. It comes back with vertical text, not before.
			Glyph.XAdvance = Positions[g].x_advance;
			Glyph.XOffset = Positions[g].x_offset;
			Glyph.YOffset = Positions[g].y_offset;
		}
	}

	/**
	 * A run through the shape cache: its segments looked up, the stretches of neighbouring segments the cache does not
	 * have shaped in one call each, and every segment of them stored. Fills each segment's glyph range in Shaped.
	 */
	void ShapeRunThroughCache(hb_buffer_t* Buffer, const FRunSetup& Setup, const FCutContext& Cut, const FRunKey& RunKey, int32 RunStart, int32 RunEnd,
		FSegmentArray& Segments, TArray<FDreamTextShapeCache::FGlyph>& Shaped, TArray<FDreamTextShapeCache::FGlyph>& Stretch, FDreamTextShapeCache::FKey& Key)
	{
		using FGlyph = FDreamTextShapeCache::FGlyph;
		const FParagraphCodepoints& Text = Cut.Text;
		FindSegments(Cut, RunStart, RunEnd, Segments);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextShape_Lookup);
			for (FSegment& Segment : Segments)
			{
				Segment.FlatBase = Text.ElementStart[Segment.ElementStart];
				MakeKey(RunKey, Text.Codepoints, Segment.FlatBase, Text.ElementStart[Segment.ElementEnd], Key);
				if (const TArray<FGlyph>* Found = FDreamTextShapeCache::Find(Key))
				{
					Segment.GlyphStart = Shaped.Num();
					Segment.GlyphCount = Found->Num();
					Shaped.Append(*Found);
				}
			}
		}
		const bool bRightToLeft = Setup.Direction == HB_DIRECTION_RTL;
		TArray<int32, TInlineAllocator<32>> SliceStart;
		TArray<int32, TInlineAllocator<32>> SliceCount;
		for (int32 First = 0; First < Segments.Num();)
		{
			if (Segments[First].GlyphStart != INDEX_NONE)
			{
				First++;
				continue;
			}
			int32 End = First + 1;
			while (End < Segments.Num() && Segments[End].GlyphStart == INDEX_NONE)
			{
				End++;
			}
			const int32 Begin = Segments[First].FlatBase;
			const int32 Finish = Text.ElementStart[Segments[End - 1].ElementEnd];
			Stretch.Reset();
			ShapeRange(Buffer, Setup, Text.Codepoints, Begin, Finish, Stretch);

			// Each glyph's segment, from its cluster. Clusters are monotone -- rising left to right, falling right to left --
			// so each segment's glyphs are one slice; a stretch where that does not hold, or where a segment came out with
			// no glyph of its own, is kept as it is and not stored.
			SliceStart.Init(INDEX_NONE, End - First);
			SliceCount.Init(0, End - First);
			int32 Current = bRightToLeft ? End - 1 : First;
			bool bSplit = true;
			for (int32 g = 0; g < Stretch.Num() && bSplit; g++)
			{
				const int32 Flat = Begin + Stretch[g].Cluster;
				if (bRightToLeft)
				{
					while (Current >= First && Flat < Segments[Current].FlatBase)
					{
						Current--;
					}
					bSplit = Current >= First && Flat < Text.ElementStart[Segments[Current].ElementEnd];
				}
				else
				{
					while (Current < End && Flat >= Text.ElementStart[Segments[Current].ElementEnd])
					{
						Current++;
					}
					bSplit = Current < End && Flat >= Segments[Current].FlatBase;
				}
				if (bSplit)
				{
					if (SliceStart[Current - First] == INDEX_NONE)
					{
						SliceStart[Current - First] = g;
					}
					SliceCount[Current - First]++;
				}
			}
			for (int32 Index = 0; Index < End - First && bSplit; Index++)
			{
				bSplit = SliceCount[Index] > 0;
			}
			if (bSplit)
			{
				for (int32 Index = First; Index < End; Index++)
				{
					FSegment& Segment = Segments[Index];
					Segment.GlyphStart = Shaped.Num();
					Segment.GlyphCount = SliceCount[Index - First];
					for (int32 g = SliceStart[Index - First]; g < SliceStart[Index - First] + Segment.GlyphCount; g++)
					{
						FGlyph Glyph = Stretch[g];
						Glyph.Cluster += Begin - Segment.FlatBase;
						Shaped.Add(Glyph);
					}
					MakeKey(RunKey, Text.Codepoints, Segment.FlatBase, Text.ElementStart[Segment.ElementEnd], Key);
					FDreamTextShapeCache::Add(Key, TConstArrayView<FGlyph>(Shaped.GetData() + Segment.GlyphStart, Segment.GlyphCount));
				}
			}
			else
			{
				FSegment& Block = Segments[First];
				Block.GlyphStart = Shaped.Num();
				Block.GlyphCount = Stretch.Num();
				Block.Span = End - First;
				Shaped.Append(Stretch);
				for (int32 Index = First + 1; Index < End; Index++)
				{
					Segments[Index].GlyphStart = Shaped.Num();
					Segments[Index].GlyphCount = 0;
					Segments[Index].Span = 0;
				}
			}
			First = End;
		}
	}

	/** Whether an element carries a default-ignorable code point beside others: one HarfBuzz may give a hidden glyph of its own. */
	bool HasIgnorableBesideOthers(const FParagraphCodepoints& Text, int32 Element)
	{
		const int32 Begin = Text.ElementStart[Element];
		const int32 End = Text.ElementStart[Element + 1];
		if (End - Begin < 2)
		{
			return false;
		}
		for (int32 Index = Begin; Index < End; Index++)
		{
			if (FDreamFontFaceResolver::IsDefaultIgnorable(Text.Codepoints[Index]))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * HarfBuzz hides a default-ignorable code point it formed into nothing -- a selector after a letter the face has no
	 * variant for, a joiner that joined nothing -- as the face's space glyph, with no advance and nothing to draw. In an
	 * element with a glyph of its own besides, that glyph is dropped: it would only be a quad of no size, which the painter
	 * and per-character animation count as a character's. An element keeps one glyph whatever happens.
	 */
	void DropHiddenIgnorables(TArray<FDreamShapedGlyph>& Glyphs, const FParagraphCodepoints& Text, uint32 SpaceGlyph)
	{
		auto IsHidden = [&Glyphs, SpaceGlyph](int32 Index)
		{
			const FDreamShapedGlyph& Glyph = Glyphs[Index];
			return Glyph.GlyphIndex == SpaceGlyph && Glyph.XAdvance == 0.0f && Glyph.XOffset == 0.0f && Glyph.YOffset == 0.0f;
		};
		int32 Write = 0;
		int32 First = 0;
		while (First < Glyphs.Num())
		{
			// An element's glyphs are contiguous.
			const int32 Element = Glyphs[First].ElementIndex;
			int32 End = First + 1;
			while (End < Glyphs.Num() && Glyphs[End].ElementIndex == Element)
			{
				End++;
			}
			bool bAnyHidden = false;
			bool bAnyShown = false;
			for (int32 Index = First; Index < End; Index++)
			{
				const bool bHidden = IsHidden(Index);
				bAnyHidden |= bHidden;
				bAnyShown |= !bHidden;
			}
			const bool bDrop = bAnyHidden && End - First > 1 && HasIgnorableBesideOthers(Text, Element);
			bool bKeptOne = false;
			for (int32 Index = First; Index < End; Index++)
			{
				// Written back in place: Write never passes Index.
				if (bDrop && IsHidden(Index) && (bAnyShown || bKeptOne))
				{
					continue;
				}
				Glyphs[Write++] = Glyphs[Index];
				bKeptOne = true;
			}
			First = End;
		}
		Glyphs.SetNum(Write);
	}

	/**
	 * One shaping call's view of its elements: their code points as HarfBuzz is handed them, their languages, the faces the
	 * resolver picks from, and the items the itemizer makes of them. ShapeParagraph runs it over a paragraph, ShapeWindow
	 * over a span of one; both itemize and shape with the same functions, so a window comes out as its paragraph does.
	 */
	struct FShapeContext
	{
		const TArray<FDreamShapeElement>& Elements;
		const FDreamShapeParams& Params;
		UDreamUIFontData_BaseObject* Font = nullptr;
		FParagraphCodepoints Text;
		/** Only an element with more than one code point can have a hidden glyph beside its own (DropHiddenIgnorables). */
		bool bHasSequences = false;
		TArray<FDreamTextLanguage> GameLanguageStorage;
		/** The caller's languages, or the game's current language for every element. */
		const TArray<FDreamTextLanguage>* Languages = nullptr;
		TArray<hb_language_t, TInlineAllocator<4>> HarfBuzzLanguages;
		hb_unicode_funcs_t* Unicode = nullptr;
		int32 FaceCount = 0;
		const FDreamFontFaceTable* FaceTable = nullptr;
		/** The bold, italic and bold-italic faces, asked for once each. */
		int32 StyledFaces[4] = { 0, INDEX_NONE, INDEX_NONE, INDEX_NONE };
		TArray<FItem> Items;
		TArray<uint8> Levels;

		FShapeContext(const TArray<FDreamShapeElement>& InElements, const FDreamShapeParams& InParams)
			: Elements(InElements), Params(InParams), Font(InParams.Font)
		{
		}

		uint8 LanguageIndexOf(const FDreamShapeElement& Element) const
		{
			return (int32)Element.LanguageIndex < Languages->Num() ? Element.LanguageIndex : 0;
		}
	};

	void PrepareContext(FShapeContext& Context)
	{
		Context.Text.Build(Context.Elements, Context.Params.SequenceCodepoints);
		Context.bHasSequences = Context.Text.Codepoints.Num() > Context.Elements.Num();
		// The languages the elements are in: the caller's, or the game's current language for every element. 'locl' picks
		// between the forms a script shares across languages -- the Han glyphs Chinese and Japanese draw differently, Serbian
		// Cyrillic italics -- so each run is shaped in its own language.
		Context.Languages = Context.Params.Languages != nullptr && Context.Params.Languages->Num() > 0
			? Context.Params.Languages : &GetGameLanguages(Context.GameLanguageStorage);
		Context.HarfBuzzLanguages.Reserve(Context.Languages->Num());
		for (const FDreamTextLanguage& Language : *Context.Languages)
		{
			Context.HarfBuzzLanguages.Add(ShapingLanguageOf(Language));
		}
		Context.Unicode = hb_unicode_funcs_get_default();
		Context.FaceCount = Context.Font->GetFaceCount();
		Context.FaceTable = &Context.Font->GetFaceTable();
		Context.Items.SetNum(Context.Elements.Num());
	}

	/** The face for elements [First, End) as one cluster, in the style and language of element StyleOf. */
	FDreamFontFaceChoice ResolveFaceFor(FShapeContext& Context, int32 First, int32 End, int32 StyleOf)
	{
		const FDreamShapeElement& Styled = Context.Elements[StyleOf];
		const int32 Style = (Styled.bBold ? 1 : 0) | (Styled.bItalic ? 2 : 0);
		if (Context.StyledFaces[Style] == INDEX_NONE)
		{
			Context.StyledFaces[Style] = Context.Font->GetStyledFace(Styled.bBold, Styled.bItalic);
		}
		FDreamFontFaceQuery Query;
		Query.Cluster = Context.Text.Range(First, End);
		Query.StyledFace = Context.StyledFaces[Style];
		Query.Cultures = (*Context.Languages)[Context.LanguageIndexOf(Styled)].PrioritizedCultureNames;
		Query.Presentation = FDreamFontFaceResolver::GetPresentation(Query.Cluster);
		Query.bAllowColorFaces = Context.Params.bAllowColorFaces;
		UDreamUIFontData_BaseObject* Font = Context.Font;
		return FDreamFontFaceResolver::Resolve(*Context.FaceTable, Context.FaceCount, Query,
			[Font](int32 FaceIndex, uint32 Codepoint) { return Font->FaceHasCodepoint(FaceIndex, Codepoint); },
			[Font](int32 FaceIndex) { return Font->IsColorFace(FaceIndex); });
	}

	/**
	 * Itemizes the elements from Begin until a cluster starts at End or later: level (from Context.Levels), script, language,
	 * face and style, per grapheme cluster. Neutral characters take the script before them, the first of them Seed, so a
	 * run is not cut on every comma. Returns the script an element after them that has none of its own takes.
	 * OutOwnScripts, when given, marks (from Begin) the elements of the clusters that have a script of their own.
	 */
	hb_script_t ItemizeRange(FShapeContext& Context, int32 Begin, int32 End, hb_script_t Seed, TBitArray<>* OutOwnScripts)
	{
		const TArray<FDreamShapeElement>& Elements = Context.Elements;
		hb_script_t LastScript = Seed;
		int32 ClusterStart = Begin;
		while (ClusterStart < End)
		{
			const FDreamShapeElement& E = Elements[ClusterStart];
			FItem& Item = Context.Items[ClusterStart];
			Item.Level = Context.Levels[ClusterStart];
			Item.Size = E.Size;
			Item.bBold = E.bBold;
			Item.bUnshaped = E.bUnshaped;
			Item.LanguageIndex = Context.LanguageIndexOf(E);
			if (E.bUnshaped)
			{
				ClusterStart++;
				continue;
			}
			int32 ClusterEnd = ClusterStart + 1;
			while (ClusterEnd < Elements.Num() && !Elements[ClusterEnd].bGraphemeStart && !Elements[ClusterEnd].bUnshaped)
			{
				ClusterEnd++;
			}
			hb_script_t Script = hb_unicode_script(Context.Unicode, Context.Text.Codepoints[Context.Text.ElementStart[ClusterStart]]);
			const bool bOwnScript = !ScriptIsNeutral(Script);
			if (bOwnScript)
			{
				LastScript = Script;
			}
			else
			{
				Script = LastScript;
			}
			Item.Script = Script;
			const FDreamFontFaceChoice Choice = ResolveFaceFor(Context, ClusterStart, ClusterEnd, ClusterStart);
			Item.FaceIndex = Choice.FaceIndex;
			Item.bColorFace = Choice.bColor;
			// The rest of a grapheme cluster rides with the element that starts it -- same level, script, size, weight and
			// language, same face -- so a base and its combining marks are shaped together and one font draws all of them.
			for (int32 k = ClusterStart + 1; k < ClusterEnd; k++)
			{
				Context.Items[k] = Item;
				Context.Levels[k] = Item.Level;
			}
			// Unless no face has the whole cluster -- "a" and a skin tone after it -- when each element takes the face that has
			// it, rather than the rest being drawn as boxes in the face of the first. It keeps the cluster's level.
			if (!Choice.bCoversCluster && ClusterEnd - ClusterStart > 1)
			{
				for (int32 k = ClusterStart; k < ClusterEnd; k++)
				{
					const FDreamFontFaceChoice Own = ResolveFaceFor(Context, k, k + 1, ClusterStart);
					Context.Items[k].FaceIndex = Own.FaceIndex;
					Context.Items[k].bColorFace = Own.bColor;
				}
			}
			if (OutOwnScripts != nullptr && bOwnScript)
			{
				for (int32 k = ClusterStart; k < ClusterEnd && k - Begin < OutOwnScripts->Num(); k++)
				{
					(*OutOwnScripts)[k - Begin] = true;
				}
			}
			ClusterStart = ClusterEnd;
		}
		return LastScript;
	}

	/** Neutrals before the first script of their own in [Begin, End) take that script, as at a paragraph's start. */
	void TakeLeadingScripts(FShapeContext& Context, int32 Begin, int32 End)
	{
		for (int32 i = Begin; i < End; i++)
		{
			if (Context.Items[i].bUnshaped)continue;
			if (!ScriptIsNeutral(Context.Items[i].Script))
			{
				for (int32 j = Begin; j < i; j++)
				{
					if (!Context.Items[j].bUnshaped && ScriptIsNeutral(Context.Items[j].Script))Context.Items[j].Script = Context.Items[i].Script;
				}
				break;
			}
		}
	}

	/**
	 * Cuts the itemized elements [Begin, End) into runs and shapes each: through the shape cache on the game thread, whole
	 * otherwise. OutSegmentStarts, when given, marks (from Begin) every element a segment starts at: each run's segments,
	 * and every unshaped element.
	 */
	void ShapeRunsInRange(FShapeContext& Context, hb_buffer_t* Buffer, int32 Begin, int32 End, TArray<FDreamShapedRun>& OutRuns, TBitArray<>* OutSegmentStarts)
	{
		using FGlyph = FDreamTextShapeCache::FGlyph;
		const TArray<FDreamShapeElement>& Elements = Context.Elements;
		const FDreamShapeParams& Params = Context.Params;
		const FParagraphCodepoints& Text = Context.Text;
		UDreamUIFontData_BaseObject* Font = Context.Font;
		const FDreamFontFaceTable& FaceTable = *Context.FaceTable;
		const int32 FaceCount = Context.FaceCount;
		const TArray<FItem>& Items = Context.Items;
		auto MarkSegment = [OutSegmentStarts, Begin](int32 Element)
		{
			if (OutSegmentStarts != nullptr && Element - Begin >= 0 && Element - Begin < OutSegmentStarts->Num())
			{
				(*OutSegmentStarts)[Element - Begin] = true;
			}
		};

		const float BoldRatio = Font->GetBoldRatio();
		// CSS size-adjust: a scaled face is shaped -- and its glyphs rasterized -- at the style size times its scale, so the
		// advances and offsets of its runs carry the scale. Style faces, past the table, are at 1.
		auto ScaleOf = [&FaceTable, FaceCount](int32 FaceIndex)
		{
			const float Scale = FaceIndex < FaceCount ? FaceTable.GetScale(FaceIndex) : 1.0f;
			return Scale > 0.0f ? Scale : 1.0f;
		};
		// The cache belongs to the game thread: a layout made anywhere else shapes every run whole.
		const bool bUseCache = FDreamTextShapeCache::IsEnabled() && IsInGameThread();
		TArray<FGlyph> Shaped;
		TArray<FGlyph> Stretch;
		FSegmentArray Segments;
		FDreamTextShapeCache::FKey Key;
		int32 RunStart = Begin;
		while (RunStart < End)
		{
			int32 RunEnd = RunStart + 1;
			while (RunEnd < End && Items[RunEnd].SameRun(Items[RunStart]) && !Elements[RunEnd].bRunBreakBefore)
			{
				RunEnd++;
			}
			const FItem& Item = Items[RunStart];
			if (Item.bUnshaped)
			{
				// Measured by the layout, never shaped: each one stands on its own.
				for (int32 Element = RunStart; Element < RunEnd; Element++)
				{
					MarkSegment(Element);
				}
			}
			else
			{
				int32 FaceIndex = Item.FaceIndex;
				bool bColorFace = Item.bColorFace;
				hb_font_t* HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(FaceIndex, Item.Size * ScaleOf(FaceIndex)));
				if (HBFont == nullptr && FaceIndex != 0)
				{
					// The face has no shaping font after all: the run is the primary face's, glyph ids and all.
					FaceIndex = 0;
					bColorFace = Font->IsColorFace(0);
					HBFont = static_cast<hb_font_t*>(Font->GetShapingFont(0, Item.Size * ScaleOf(0)));
				}
				FDreamShapedRun& Run = OutRuns.AddDefaulted_GetRef();
				Run.ElementStart = RunStart;
				Run.ElementEnd = RunEnd;
				Run.bRightToLeft = (Item.Level & 1) != 0;
				Run.BidiLevel = Item.Level;
				Run.FaceIndex = FaceIndex;
				Run.Size = Item.Size;
				Run.FaceScale = ScaleOf(FaceIndex);
				Run.bColorFace = bColorFace;
				Run.bBold = Item.bBold;
				// A real bold face has its weight in its outlines and advances already, and a colour glyph is never emboldened:
				// only bold that has to be made up widens the advances and emboldens the glyphs.
				Run.bSyntheticBold = Item.bBold && !bColorFace && !EnumHasAnyFlags(Font->GetFaceStyleFlags(FaceIndex), EDreamUIFontFaceStyle::Bold);
				MarkSegment(RunStart);

				if (HBFont != nullptr)
				{
					const float ShapingSize = Item.Size * Run.FaceScale;
					const bool bCaltOff = !Params.bLigatures && ContextualAlternatesAreStylistic(Item.Script);
					FRunSetup Setup;
					Setup.Direction = Run.bRightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
					Setup.Script = Item.Script;
					Setup.Language = Context.HarfBuzzLanguages[Item.LanguageIndex];
					// Kerning as asked. Ligatures and contextual alternates as a browser has them when the caller allows them:
					// one glyph may then cover several characters, which the layout splits its carets across. Otherwise liga and
					// clig are off so a code point keeps its own glyph, which is what per-character animation needs, and calt
					// goes off with them where it is only a matter of style. A ligature inside an element -- an emoji sequence --
					// comes from ccmp, which stays on.
					Setup.Features[Setup.FeatureCount++] = { HB_TAG('k','e','r','n'), Params.bUseKerning ? 1u : 0u, 0, (unsigned int)-1 };
					Setup.Features[Setup.FeatureCount++] = { HB_TAG('l','i','g','a'), Params.bLigatures ? 1u : 0u, 0, (unsigned int)-1 };
					Setup.Features[Setup.FeatureCount++] = { HB_TAG('c','l','i','g'), Params.bLigatures ? 1u : 0u, 0, (unsigned int)-1 };
					if (bCaltOff)
					{
						Setup.Features[Setup.FeatureCount++] = { HB_TAG('c','a','l','t'), 0u, 0, (unsigned int)-1 };
					}

					Shaped.Reset();
					Segments.Reset();
					const FDreamUIFontFaceIdentity Identity = bUseCache ? Font->GetFaceIdentity(FaceIndex) : FDreamUIFontFaceIdentity();
					const FDreamTextShapeCache::FFaceRules* Rules = Identity.IsValid() ? FDreamTextShapeCache::GetFaceRules(Identity, HBFont) : nullptr;
					// The font is shared between sizes; its scale is set again now that nothing else will ask for it before the
					// run is shaped.
					Setup.Font = static_cast<hb_font_t*>(Font->GetShapingFont(FaceIndex, ShapingSize));
					if (Setup.Font == nullptr)
					{
						Setup.Font = HBFont;
					}
					if (Rules != nullptr)
					{
						FRunKey RunKey;
						RunKey.Face = Identity;
						RunKey.Size = ShapingSize;
						RunKey.Script = Item.Script;
						RunKey.Language = Setup.Language;
						RunKey.Flags = (uint8)((Run.bRightToLeft ? FDreamTextShapeCache::FKey::RightToLeft : 0)
							| (Params.bUseKerning ? FDreamTextShapeCache::FKey::Kerning : 0)
							| (Params.bLigatures ? FDreamTextShapeCache::FKey::Ligatures : 0)
							| (bCaltOff ? FDreamTextShapeCache::FKey::NoContextualAlternates : 0));
						RunKey.bReadsContext = ScriptReadsContext(Item.Script);
						const FCutContext Cut{ Elements, Text, Context.Unicode, Setup.Font, *Rules, Run.bRightToLeft, IsCjkScript(Item.Script), RunEnd };
						ShapeRunThroughCache(Buffer, Setup, Cut, RunKey, RunStart, RunEnd, Segments, Shaped, Stretch, Key);
						for (const FSegment& Segment : Segments)
						{
							MarkSegment(Segment.ElementStart);
						}
					}
					else
					{
						// The whole run in one go, the paragraph around it as context: a glyph's cluster is its element's first
						// code point, or the first of the cluster it was merged into.
						FSegment& Whole = Segments.AddDefaulted_GetRef();
						Whole.ElementStart = RunStart;
						Whole.ElementEnd = RunEnd;
						Whole.FlatBase = Text.ElementStart[RunStart];
						Whole.GlyphStart = 0;
						ShapeRange(Buffer, Setup, Text.Codepoints, Whole.FlatBase, Text.ElementStart[RunEnd], Shaped);
						Whole.GlyphCount = Shaped.Num();
					}

					// The run's glyphs in visual order: its segments' own order left to right, backwards right to left. Every glyph
					// HarfBuzz made of an element's code points belongs to that element.
					const int32 RunFirstCodepoint = Text.ElementStart[RunStart];
					const int32 RunLastCodepoint = Text.ElementStart[RunEnd] - 1;
					Run.Glyphs.Reserve(Shaped.Num());
					for (int32 Step = 0; Step < Segments.Num(); Step++)
					{
						const FSegment& Segment = Segments[Run.bRightToLeft ? Segments.Num() - 1 - Step : Step];
						if (Segment.Span == 0)
						{
							continue;
						}
						for (int32 g = Segment.GlyphStart; g < Segment.GlyphStart + Segment.GlyphCount; g++)
						{
							const FGlyph& Source = Shaped[g];
							const int32 Codepoint = FMath::Clamp(Segment.FlatBase + Source.Cluster, RunFirstCodepoint, RunLastCodepoint);
							FDreamShapedGlyph& Glyph = Run.Glyphs.AddDefaulted_GetRef();
							Glyph.FaceIndex = FaceIndex;
							Glyph.GlyphIndex = Source.GlyphId;
							Glyph.ElementIndex = Text.ElementOf[Codepoint];
							Glyph.XAdvance = Source.XAdvance / 64.0f;
							Glyph.XOffset = Source.XOffset / 64.0f;
							Glyph.YOffset = Source.YOffset / 64.0f;
						}
					}
					hb_codepoint_t SpaceGlyph = 0;
					if (Context.bHasSequences && hb_font_get_nominal_glyph(Setup.Font, 0x0020, &SpaceGlyph))
					{
						DropHiddenIgnorables(Run.Glyphs, Text, SpaceGlyph);
					}
					// Synthetic bold widens an element by the emboldening once, on its last glyph: a combining mark has no advance
					// of its own to widen, and giving it one pushed it off the letter it sits on. A scaled face's glyphs are
					// emboldened at their own size.
					if (Run.bSyntheticBold)
					{
						const float BoldAdvance = ShapingSize * BoldRatio;
						for (int32 g = 0; g < Run.Glyphs.Num(); g++)
						{
							if (g + 1 == Run.Glyphs.Num() || Run.Glyphs[g + 1].ElementIndex != Run.Glyphs[g].ElementIndex)
							{
								Run.Glyphs[g].XAdvance += BoldAdvance;
							}
						}
					}
				}
			}
			RunStart = RunEnd;
		}
	}

	/** Each element's resolved script, as FDreamShapeAnalysis::Scripts has it, for the elements [Begin, End) indexed from Begin. */
	void WriteScripts(const FShapeContext& Context, int32 Begin, int32 End, TArray<uint32>& OutScripts)
	{
		OutScripts.SetNumZeroed(End - Begin);
		for (int32 i = Begin; i < End; i++)
		{
			const FItem& Item = Context.Items[i];
			OutScripts[i - Begin] = Item.bUnshaped ? 0u : (uint32)Item.Script;
		}
	}

	/** ShapeParagraph once HarfBuzz is there: see FDreamTextShaper::ShapeParagraph. */
	bool ShapeWithHarfBuzz(const TArray<FDreamShapeElement>& Elements, const FDreamShapeParams& Params, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft,
		TArray<uint8>* OutBidiLevels, FDreamShapeAnalysis* OutAnalysis)
	{
		if (!FDreamTextShaper::CanShape(Params.Font) || Elements.Num() == 0)
		{
			return false;
		}
		const int32 Count = Elements.Num();
		FShapeContext Context(Elements, Params);
		PrepareContext(Context);

		// Itemize: level, script, language, face, style, per grapheme cluster.
		bool bBaseRightToLeft = false;
		ResolveLevels(Elements, Params.FlowDirection, Context.Levels, bBaseRightToLeft);
		OutBaseRightToLeft = bBaseRightToLeft;
		TBitArray<>* OwnScripts = nullptr;
		if (OutAnalysis != nullptr)
		{
			OutAnalysis->OwnScripts.Init(false, Count);
			OutAnalysis->SegmentStarts.Init(false, Count);
			OwnScripts = &OutAnalysis->OwnScripts;
		}
		ItemizeRange(Context, 0, Count, HB_SCRIPT_COMMON, OwnScripts);
		// Leading neutrals before the first scripted character take that script.
		TakeLeadingScripts(Context, 0, Count);
		if (OutBidiLevels != nullptr)
		{
			*OutBidiLevels = Context.Levels;
		}
		if (OutAnalysis != nullptr)
		{
			WriteScripts(Context, 0, Count, OutAnalysis->Scripts);
		}

		// Cut runs and shape each.
		const FShapingBuffer Buffer;
		ShapeRunsInRange(Context, Buffer.Buffer, 0, Count, OutRuns, OutAnalysis != nullptr ? &OutAnalysis->SegmentStarts : nullptr);
		return true;
	}

	/** ShapeWindow once HarfBuzz is there: see FDreamTextShaper::ShapeWindow. */
	bool ShapeWindowWithHarfBuzz(const TArray<FDreamShapeElement>& SpanElements, const FDreamShapeParams& Params, const FDreamShapeWindow& Window,
		TArray<FDreamShapedRun>& OutRuns, FDreamShapeWindowResult& OutResult)
	{
		const int32 Count = SpanElements.Num();
		if (!FDreamTextShaper::CanShape(Params.Font) || Window.Begin < 0 || Window.End > Count || Window.Begin >= Window.End
			|| !FDreamTextShapeCache::IsEnabled() || !IsInGameThread())
		{
			return false;
		}
		FShapeContext Context(SpanElements, Params);
		PrepareContext(Context);
		// Nothing in the paragraph can turn right to left (the caller's promise): every level is the paragraph's, 0.
		Context.Levels.Init(0, Count);
		const int32 WindowCount = Window.End - Window.Begin;
		FDreamShapeAnalysis& Analysis = OutResult.Analysis;
		Analysis.OwnScripts.Init(false, WindowCount);
		Analysis.SegmentStarts.Init(false, WindowCount);
		// A paragraph's itemizer starts from Common, as ShapeWithHarfBuzz's does: a window at the paragraph's start (or handed no
		// seed) does too. Seeded with 0 (HB_SCRIPT_INVALID, which is not neutral) its leading neutrals would keep that script,
		// TakeLeadingScripts would pass them by, and they would run apart from the letters after them.
		const hb_script_t Seed = Window.bParagraphStart || Window.SeedScript == 0 ? HB_SCRIPT_COMMON : (hb_script_t)Window.SeedScript;
		const hb_script_t LastScript = ItemizeRange(Context, Window.Begin, Window.End, Seed, &Analysis.OwnScripts);
		OutResult.bAnyOwnScript = Analysis.OwnScripts.Find(true) != INDEX_NONE;
		if (Window.bParagraphStart)
		{
			TakeLeadingScripts(Context, Window.Begin, Window.End);
		}
		OutResult.LastScript = (uint32)LastScript;
		WriteScripts(Context, Window.Begin, Window.End, Analysis.Scripts);
		// The cluster after the window, itemized as the paragraph's itemizer reaches it: whether it joins the window's last run.
		OutResult.bLastRunContinues = false;
		if (Window.End < Count)
		{
			ItemizeRange(Context, Window.End, Window.End + 1, LastScript, nullptr);
			OutResult.bLastRunContinues = Context.Items[Window.End].SameRun(Context.Items[Window.End - 1]) && !SpanElements[Window.End].bRunBreakBefore;
		}

		const FShapingBuffer Buffer;
		ShapeRunsInRange(Context, Buffer.Buffer, Window.Begin, Window.End, OutRuns, &Analysis.SegmentStarts);
		return true;
	}
}
#endif

bool FDreamTextShaper::ShapeParagraph(const TArray<FDreamShapeElement>& Elements, const FDreamShapeParams& Params, TArray<FDreamShapedRun>& OutRuns, bool& OutBaseRightToLeft,
	TArray<uint8>* OutBidiLevels, FDreamShapeAnalysis* OutAnalysis)
{
	OutRuns.Reset();
	OutBaseRightToLeft = false;
	if (OutBidiLevels != nullptr)
	{
		OutBidiLevels->Reset();
	}
	if (OutAnalysis != nullptr)
	{
		*OutAnalysis = FDreamShapeAnalysis();
	}
#if WITH_HARFBUZZ
	return DreamTextShaperLocal::ShapeWithHarfBuzz(Elements, Params, OutRuns, OutBaseRightToLeft, OutBidiLevels, OutAnalysis);
#else
	return false;
#endif
}

bool FDreamTextShaper::ShapeWindow(const TArray<FDreamShapeElement>& SpanElements, const FDreamShapeParams& Params, const FDreamShapeWindow& Window,
	TArray<FDreamShapedRun>& OutRuns, FDreamShapeWindowResult& OutResult)
{
	OutRuns.Reset();
	OutResult = FDreamShapeWindowResult();
#if WITH_HARFBUZZ
	return DreamTextShaperLocal::ShapeWindowWithHarfBuzz(SpanElements, Params, Window, OutRuns, OutResult);
#else
	return false;
#endif
}

bool FDreamTextShaper::ScriptReadsContext(uint32 Script)
{
#if WITH_HARFBUZZ
	return DreamTextShaperLocal::ScriptReadsContext((hb_script_t)Script);
#else
	return true;
#endif
}
