// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextBreaker.h"
#include "DreamGUI.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

namespace DreamTextBreakerLocal
{
	/**
	 * Layout runs on the game thread, and creating an ICU iterator clones a rule set, so keep one of
	 * each and hand it out. Rebuilt when the culture changes, since the line rules are per locale.
	 */
	struct FIterators
	{
		TSharedPtr<IBreakIterator> Line;
		TSharedPtr<IBreakIterator> Word;
		TSharedPtr<IBreakIterator> Character;
		FDelegateHandle CultureChangedHandle;

		static FIterators& Get()
		{
			static FIterators Instance;
			check(IsInGameThread());
			if (!Instance.CultureChangedHandle.IsValid())
			{
				Instance.CultureChangedHandle = FInternationalization::Get().OnCultureChanged().AddLambda([]()
				{
					FIterators& Self = FIterators::Get();
					Self.Line.Reset();
					Self.Word.Reset();
					Self.Character.Reset();
				});
			}
			if (!Instance.Line.IsValid())
			{
				Instance.Line = FBreakIterator::CreateLineBreakIterator();
			}
			if (!Instance.Word.IsValid())
			{
				Instance.Word = FBreakIterator::CreateWordBreakIterator();
			}
			if (!Instance.Character.IsValid())
			{
				Instance.Character = FBreakIterator::CreateCharacterBoundaryIterator();
			}
			return Instance;
		}
	};

	/**
	 * Collects every boundary an iterator reports for the string. The iterator reports them in increasing order, so
	 * the array comes out sorted and is read with a cursor that only moves forward; no set has to be built per layout.
	 */
	void CollectBoundaries(IBreakIterator& Iterator, const FString& Text, TArray<int32>& OutBoundaries)
	{
		OutBoundaries.Reset();
		Iterator.SetStringRef(Text);
		Iterator.ResetToBeginning();
		int32 Boundary;
		while ((Boundary = Iterator.MoveToNext()) != INDEX_NONE)
		{
			OutBoundaries.Add(Boundary);
		}
		Iterator.ClearString();
	}

	/** Whether Position is in the sorted Boundaries, advancing InOutCursor; positions must be asked in increasing order. */
	bool IsBoundaryAt(const TArray<int32>& Boundaries, int32 Position, int32& InOutCursor)
	{
		while (InOutCursor < Boundaries.Num() && Boundaries[InOutCursor] < Position)
		{
			InOutCursor++;
		}
		return InOutCursor < Boundaries.Num() && Boundaries[InOutCursor] == Position;
	}

	/** Indic_Conjunct_Break=Linker (Unicode 15.1): the viramas that join two consonants into a conjunct. */
	bool IsConjunctLinker(uint32 C)
	{
		return C == 0x094D || C == 0x09CD || C == 0x0ACD || C == 0x0B4D || C == 0x0C4D || C == 0x0D4D;
	}

	/** Indic_Conjunct_Break=Consonant (Unicode 15.1): Devanagari, Bengali, Gujarati, Oriya, Telugu and Malayalam consonants. */
	bool IsConjunctConsonant(uint32 C)
	{
		return (C >= 0x0915 && C <= 0x0939) || (C >= 0x0958 && C <= 0x095F) || (C >= 0x0978 && C <= 0x097F)
			|| (C >= 0x0995 && C <= 0x09A8) || (C >= 0x09AA && C <= 0x09B0) || C == 0x09B2 || (C >= 0x09B6 && C <= 0x09B9)
			|| C == 0x09DC || C == 0x09DD || C == 0x09DF || C == 0x09F0 || C == 0x09F1
			|| (C >= 0x0A95 && C <= 0x0AA8) || (C >= 0x0AAA && C <= 0x0AB0) || C == 0x0AB2 || C == 0x0AB3 || (C >= 0x0AB5 && C <= 0x0AB9) || C == 0x0AF9
			|| (C >= 0x0B15 && C <= 0x0B28) || (C >= 0x0B2A && C <= 0x0B30) || C == 0x0B32 || C == 0x0B33 || (C >= 0x0B35 && C <= 0x0B39)
			|| C == 0x0B5C || C == 0x0B5D || C == 0x0B5F || C == 0x0B71
			|| (C >= 0x0C15 && C <= 0x0C28) || (C >= 0x0C2A && C <= 0x0C39) || (C >= 0x0C58 && C <= 0x0C5A)
			|| (C >= 0x0D15 && C <= 0x0D3A);
	}

	/** What may sit between a consonant and its virama, or after the virama, without ending the conjunct: nuktas and the joiner. */
	bool IsConjunctExtend(uint32 C)
	{
		return C == 0x093C || C == 0x09BC || C == 0x0ABC || C == 0x0B3C || C == 0x0C3C || C == 0x0D3B || C == 0x0D3C || C == 0x200D;
	}

	/** A mark that extends the cluster before it, for a build without ICU: the combining diacritical blocks, the joiners and the variation selectors. */
	bool IsGraphemeExtender(uint32 C)
	{
		return (C >= 0x0300 && C <= 0x036F) || (C >= 0x1AB0 && C <= 0x1AFF) || (C >= 0x1DC0 && C <= 0x1DFF)
			|| (C >= 0x20D0 && C <= 0x20FF) || (C >= 0xFE20 && C <= 0xFE2F) || (C >= 0xFE00 && C <= 0xFE0F)
			|| C == 0x200C || C == 0x200D;
	}
}

void FDreamTextBreaker::ComputeGraphemeStarts(const FString& PlainText, const TArray<int32>& ElementPlainStart,
	const TArray<uint32>& ElementCodepoints, TBitArray<>& OutGraphemeStart)
{
	using namespace DreamTextBreakerLocal;

	const int32 ElementCount = ElementPlainStart.Num();
	OutGraphemeStart.Init(true, ElementCount);
	if (ElementCount <= 1)return;
	bool bAnyCombining = false;
	for (const uint32 C : ElementCodepoints)
	{
		if (C >= 0x0300)
		{
			bAnyCombining = true;
			break;
		}
	}
	if (!bAnyCombining)return;

#if UE_ENABLE_ICU
	FIterators& Iterators = FIterators::Get();
	TArray<int32> Boundaries;
	CollectBoundaries(*Iterators.Character, PlainText, Boundaries);
	int32 Cursor = 0;
	for (int32 i = 1; i < ElementCount; i++)
	{
		OutGraphemeStart[i] = IsBoundaryAt(Boundaries, ElementPlainStart[i], Cursor);
	}
#else
	for (int32 i = 1; i < ElementCount; i++)
	{
		OutGraphemeStart[i] = !IsGraphemeExtender(ElementCodepoints[i]);
	}
#endif

	// GB9c: Consonant [Extend Linker]* Linker [Extend Linker]* x Consonant. A conjunct is one character to a reader --
	// a caret inside it, or a line break inside it, splits what is written as one letter.
	for (int32 i = 1; i < ElementCount; i++)
	{
		if (!OutGraphemeStart[i] || !IsConjunctConsonant(ElementCodepoints[i]))continue;
		int32 j = i - 1;
		bool bLinker = false;
		while (j >= 0 && (IsConjunctLinker(ElementCodepoints[j]) || IsConjunctExtend(ElementCodepoints[j])))
		{
			bLinker |= IsConjunctLinker(ElementCodepoints[j]);
			j--;
		}
		if (bLinker && j >= 0 && IsConjunctConsonant(ElementCodepoints[j]))
		{
			OutGraphemeStart[i] = false;
		}
	}
}

bool FDreamTextBreaker::IsCJKCodepoint(uint32 C)
{
	return (C >= 0x2E80 && C <= 0x2FDF)    // CJK radicals, Kangxi radicals
		|| (C >= 0x3040 && C <= 0x30FF)    // Hiragana, Katakana
		|| (C >= 0x3100 && C <= 0x312F)    // Bopomofo
		|| (C >= 0x3130 && C <= 0x318F)    // Hangul compatibility jamo
		|| (C >= 0x31A0 && C <= 0x31FF)    // Bopomofo ext, Katakana phonetic ext
		|| (C >= 0x3400 && C <= 0x4DBF)    // CJK ext A
		|| (C >= 0x4E00 && C <= 0x9FFF)    // CJK unified
		|| (C >= 0xAC00 && C <= 0xD7AF)    // Hangul syllables
		|| (C >= 0xF900 && C <= 0xFAFF)    // CJK compatibility ideographs
		|| (C >= 0x20000 && C <= 0x3134F); // CJK ext B..G
}

bool FDreamTextBreaker::IsBreakingSpace(uint32 C)
{
	return C == ' ' || C == '\t' || C == 0x3000/*ideographic space*/ || C == 0x1680 || (C >= 0x2000 && C <= 0x200A);
}

void FDreamTextBreaker::ComputeFallbackBreakOpportunities(const TArray<uint32>& ElementCodepoints, TBitArray<>& OutCanBreakBefore)
{
	const int32 ElementCount = ElementCodepoints.Num();
	OutCanBreakBefore.Init(false, ElementCount);
	for (int32 i = 1; i < ElementCount; i++)
	{
		const uint32 Prev = ElementCodepoints[i - 1];
		const uint32 Cur = ElementCodepoints[i];
		// Breaking BEFORE a space is pointless -- ICU reports the boundary after the run of spaces, and
		// the layout hangs trailing whitespace outside the line either way.
		if (IsBreakingSpace(Cur))continue;

		bool bAllowed = false;
		if (IsBreakingSpace(Prev))
		{
			bAllowed = true;//UAX #14 LB18: break after spaces
		}
		else if (IsCJKCodepoint(Prev) || IsCJKCodepoint(Cur))
		{
			// LB8a/LB21/ID: an ideograph may start or end a line, so the boundary between one and
			// anything else is a break -- which is the whole reason CJK wraps at all.
			bAllowed = true;
		}
		else if (Prev == '-' && !(Cur >= '0' && Cur <= '9'))
		{
			bAllowed = true;//LB21b-ish: break after a hyphen, but not inside 3-4
		}
		if (!bAllowed)continue;
		// Kinsoku, the part every implementation keeps: no closing mark starts a line, no opening
		// bracket ends one.
		if (IsClosingPunctuation(Cur))continue;
		if (IsOpeningPunctuation(Prev))continue;
		OutCanBreakBefore[i] = true;
	}
}

void FDreamTextBreaker::ComputeBreakOpportunities(const FString& PlainText, const TArray<int32>& ElementPlainStart,
	const TArray<uint32>& ElementCodepoints, EDreamTextPhraseWrap PhraseWrap, TBitArray<>& OutCanBreakBefore)
{
	using namespace DreamTextBreakerLocal;

	const int32 ElementCount = ElementPlainStart.Num();
	OutCanBreakBefore.Init(false, ElementCount);
	if (ElementCount == 0)return;

#if !UE_ENABLE_ICU
	// No ICU means no line-break rules and no word dictionary: the engine's legacy iterator breaks on
	// whitespace and nothing else, so a script that does not write spaces never wrapped at all. The
	// fallback below is the part of UAX #14 that matters for that -- ideographs break per character,
	// kinsoku keeps the marks where they belong -- computed straight from the code points.
	// PhraseWrap has no dictionary to consult here, so a CJK run breaks per character.
	static bool bLoggedNoICU = false;
	if (!bLoggedNoICU)
	{
		bLoggedNoICU = true;
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This target was built without ICU (UE_ENABLE_ICU=0): line breaking uses DreamGUI's own per-code-point fallback (CJK breaks per character, kinsoku respected). Phrase wrap needs ICU's dictionary and is ignored. (reported once)")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
	ComputeFallbackBreakOpportunities(ElementCodepoints, OutCanBreakBefore);
	return;
#else

	FIterators& Iterators = FIterators::Get();

	TArray<int32> LineBoundaries;
	CollectBoundaries(*Iterators.Line, PlainText, LineBoundaries);

	TArray<int32> WordBoundaries;
	const bool bPhrase = PhraseWrap != EDreamTextPhraseWrap::Off;
	if (bPhrase)
	{
		CollectBoundaries(*Iterators.Word, PlainText, WordBoundaries);
	}

	int32 LineCursor = 0;
	int32 WordCursor = 0;
	for (int32 i = 1; i < ElementCount; i++)
	{
		const int32 Start = ElementPlainStart[i];
		if (!IsBoundaryAt(LineBoundaries, Start, LineCursor))continue;
		if (bPhrase && IsCJKCodepoint(ElementCodepoints[i]) && IsCJKCodepoint(ElementCodepoints[i - 1]))
		{
			// Inside a CJK run the line rules allow a break everywhere; the dictionary says where the
			// words are. Between a CJK character and anything else the line rules already decided.
			if (!IsBoundaryAt(WordBoundaries, Start, WordCursor))continue;
		}
		OutCanBreakBefore[i] = true;
	}
#endif
}

bool FDreamTextBreaker::IsClosingPunctuation(uint32 C)
{
	switch (C)
	{
	case ',': case '.': case ';': case ':': case '?': case '!': case ')': case ']': case '}':
	case 0x2019: case 0x201D:                                   // ’ ”
	case 0x3001: case 0x3002: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B:
	case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF01: case 0xFF09: case 0xFF3D: case 0xFF5D: case 0xFF60:
		return true;
	default:
		return false;
	}
}

bool FDreamTextBreaker::IsOpeningPunctuation(uint32 C)
{
	switch (C)
	{
	case '(': case '[': case '{':
	case 0x2018: case 0x201C:                                   // ‘ “
	case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301A:
	case 0xFF08: case 0xFF3B: case 0xFF5B: case 0xFF5F:
		return true;
	default:
		return false;
	}
}

int32 FDreamTextBreaker::FindKinsokuSafeFallback(const TArray<uint32>& ElementCodepoints, int32 LineStart, int32 BreakBefore, const TBitArray<>* ClusterStarts)
{
	for (int32 j = BreakBefore; j > LineStart; j--)
	{
		if (ClusterStarts != nullptr && ClusterStarts->IsValidIndex(j) && !(*ClusterStarts)[j])continue;
		if (IsClosingPunctuation(ElementCodepoints[j]))continue;
		if (IsOpeningPunctuation(ElementCodepoints[j - 1]))continue;
		return j;
	}
	return INDEX_NONE;
}
