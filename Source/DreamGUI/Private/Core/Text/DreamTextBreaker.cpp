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

	/**
	 * A code point that extends the cluster before it, for a build without ICU (UAX #29's Extend and ZWJ, the part of them
	 * text meets): the combining diacritical blocks, the joiners, the variation selectors and their supplement, the skin
	 * tone modifiers -- which stay on whatever they follow, "a" included -- and the tag characters.
	 */
	bool IsGraphemeExtender(uint32 C)
	{
		return (C >= 0x0300 && C <= 0x036F) || (C >= 0x1AB0 && C <= 0x1AFF) || (C >= 0x1DC0 && C <= 0x1DFF)
			|| (C >= 0x20D0 && C <= 0x20FF) || (C >= 0xFE20 && C <= 0xFE2F) || (C >= 0xFE00 && C <= 0xFE0F)
			|| C == 0x200C || C == 0x200D
			|| (C >= FDreamUIText_CodePoint::UNICODE_SKIN_TONE_START && C <= FDreamUIText_CodePoint::UNICODE_SKIN_TONE_END)
			|| (C >= FDreamUIText_CodePoint::UNICODE_TAG_START && C <= FDreamUIText_CodePoint::UNICODE_CANCEL_TAG)
			|| (C >= 0xE0100 && C <= 0xE01EF);
	}

	/**
	 * GB9c: Consonant [Extend Linker]* Linker [Extend Linker]* x Consonant. A conjunct is one character to a reader -- a
	 * caret inside it, or a line break inside it, splits what is written as one letter. Looks back no further than First.
	 */
	bool JoinsConjunct(const TArray<uint32>& Codepoints, int32 First, int32 Index)
	{
		if (!IsConjunctConsonant(Codepoints[Index]))
		{
			return false;
		}
		int32 j = Index - 1;
		bool bLinker = false;
		while (j >= First && (IsConjunctLinker(Codepoints[j]) || IsConjunctExtend(Codepoints[j])))
		{
			bLinker |= IsConjunctLinker(Codepoints[j]);
			j--;
		}
		return bLinker && j >= First && IsConjunctConsonant(Codepoints[j]);
	}

	/**
	 * The subset of UAX #14 a build without ICU breaks by, for one pair of neighbours: after spaces, on either side of an
	 * ideograph, after a hyphen; never a closing mark starting a line or an opening bracket ending one.
	 */
	bool CanBreakBetweenFallback(uint32 Prev, uint32 Cur)
	{
		// Breaking BEFORE a space is pointless -- ICU reports the boundary after the run of spaces, and
		// the layout hangs trailing whitespace outside the line either way.
		if (FDreamTextBreaker::IsBreakingSpace(Cur))
		{
			return false;
		}
		bool bAllowed = false;
		if (FDreamTextBreaker::IsBreakingSpace(Prev))
		{
			bAllowed = true;//UAX #14 LB18: break after spaces
		}
		else if (FDreamTextBreaker::IsCJKCodepoint(Prev) || FDreamTextBreaker::IsCJKCodepoint(Cur))
		{
			// LB8a/LB21/ID: an ideograph may start or end a line, so the boundary between one and
			// anything else is a break -- which is the whole reason CJK wraps at all.
			bAllowed = true;
		}
		else if (Prev == '-' && !(Cur >= '0' && Cur <= '9'))
		{
			bAllowed = true;//LB21b-ish: break after a hyphen, but not inside 3-4
		}
		// Kinsoku, the part every implementation keeps: no closing mark starts a line, no opening
		// bracket ends one.
		return bAllowed && !FDreamTextBreaker::IsClosingPunctuation(Cur) && !FDreamTextBreaker::IsOpeningPunctuation(Prev);
	}

#if !UE_ENABLE_ICU
	void LogNoIcuOnce()
	{
		// No ICU means no line-break rules and no word dictionary: the engine's legacy iterator breaks on
		// whitespace and nothing else, so a script that does not write spaces never wrapped at all. The
		// fallback is the part of UAX #14 that matters for that -- ideographs break per character,
		// kinsoku keeps the marks where they belong -- computed straight from the code points.
		// PhraseWrap has no dictionary to consult here, so a CJK run breaks per character.
		static bool bLoggedNoICU = false;
		if (!bLoggedNoICU)
		{
			bLoggedNoICU = true;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This target was built without ICU (UE_ENABLE_ICU=0): line breaking uses DreamGUI's own per-code-point fallback (CJK breaks per character, kinsoku respected). Phrase wrap needs ICU's dictionary and is ignored. (reported once)")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
#endif
}

void FDreamTextBreaker::ComputeGraphemeStarts(const FString& PlainText, const TArray<int32>& ElementPlainStart,
	const TArray<uint32>& ElementCodepoints, TBitArray<>& OutGraphemeStart)
{
	const int32 ElementCount = ElementPlainStart.Num();
	OutGraphemeStart.Init(true, ElementCount);
	if (ElementCount <= 1)return;
	FDreamTextBoundarySpan Span;
	Span.PlainText = &PlainText;
	Span.ElementPlainStart = &ElementPlainStart;
	Span.ElementCodepoints = &ElementCodepoints;
	Span.FirstElement = 0;
	Span.EndElement = ElementCount;
	Span.PlainBegin = 0;
	Span.PlainEnd = PlainText.Len();
	int32 Last = 0;
	ComputeBoundaries(EDreamTextBoundaryKind::Grapheme, Span, 0, ElementCount, OutGraphemeStart, [](int32, bool) { return false; }, Last);
}

int32 FDreamTextBreaker::ComputeBoundaries(EDreamTextBoundaryKind Kind, const FDreamTextBoundarySpan& Span, int32 Begin, int32 End,
	TBitArray<>& OutBits, TFunctionRef<bool(int32 Element, bool bBoundary)> Stop, int32& OutLast)
{
	using namespace DreamTextBreakerLocal;

	OutLast = Begin - 1;
	Begin = FMath::Max(Begin, Span.FirstElement);
	End = FMath::Min(End, Span.EndElement);
	if (Begin >= End)return 0;
	const TArray<int32>& PlainStarts = *Span.ElementPlainStart;
	const TArray<uint32>& Codepoints = *Span.ElementCodepoints;
	const bool bGrapheme = Kind == EDreamTextBoundaryKind::Grapheme;

	// One element's bit as it is kept: the span's first element starts a cluster and ends no line, and a consonant a virama
	// joins to the one before it starts no cluster (GB9c, which the engine's ICU predates).
	auto Write = [&](int32 Element, bool bBoundary)
	{
		if (Element == Span.FirstElement)
		{
			bBoundary = bGrapheme;
		}
		else if (bGrapheme && bBoundary && JoinsConjunct(Codepoints, Span.FirstElement, Element))
		{
			bBoundary = false;
		}
		OutBits[Element] = bBoundary;
		OutLast = Element;
		return Stop(Element, bBoundary);
	};

	if (bGrapheme)
	{
		// Text with nothing at or above U+0300 is all single code points: every element starts a cluster, and ICU is not asked.
		bool bAnyCombining = false;
		for (int32 i = Span.FirstElement; i < Span.EndElement; i++)
		{
			if (Codepoints[i] >= 0x0300)
			{
				bAnyCombining = true;
				break;
			}
		}
		if (!bAnyCombining)
		{
			for (int32 i = Begin; i < End; i++)
			{
				if (Write(i, true))break;
			}
			return 0;
		}
	}

#if !UE_ENABLE_ICU
	if (bGrapheme)
	{
		for (int32 i = Begin; i < End; i++)
		{
			const uint32 C = Codepoints[i];
			// GB11: a pictograph after a joiner that follows a pictograph is one emoji with them. An emoji element holds its
			// own ZWJ sequence already; this joins one whose base asked for no emoji presentation ("U+2764 U+200D U+1F525").
			const bool bJoinedPictograph = i - 2 >= Span.FirstElement && Codepoints[i - 1] == FDreamUIText_CodePoint::UNICODE_ZWJ
				&& FDreamUIText_CodePoint::IsExtendedPictographic(Codepoints[i - 2]) && FDreamUIText_CodePoint::IsExtendedPictographic(C);
			if (Write(i, !IsGraphemeExtender(C) && !bJoinedPictograph))break;
		}
	}
	else if (Kind == EDreamTextBoundaryKind::Line)
	{
		LogNoIcuOnce();
		for (int32 i = Begin; i < End; i++)
		{
			if (Write(i, i > Span.FirstElement && CanBreakBetweenFallback(Codepoints[i - 1], Codepoints[i])))break;
		}
	}
	else
	{
		for (int32 i = Begin; i < End; i++)
		{
			if (Write(i, false))break;
		}
	}
	return 0;
#else
	FIterators& Iterators = FIterators::Get();
	IBreakIterator& Iterator = bGrapheme ? *Iterators.Character
		: (Kind == EDreamTextBoundaryKind::Line ? *Iterators.Line : *Iterators.Word);
	const int32 SpanLength = Span.PlainEnd - Span.PlainBegin;
	Iterator.SetStringRef(FStringView(**Span.PlainText + Span.PlainBegin, SpanLength));
	// From the span's start ICU walks forward as it always did. From inside it, ICU's own random access (following) backs up
	// to a point its rules call safe and walks forward from there, which finds the boundaries a walk from the start finds --
	// asked from a boundary of that walk, for the engine's line iterator (see the header).
	const int32 From = PlainStarts[Begin] - Span.PlainBegin;
	int32 Boundary = From > 0 ? Iterator.MoveToCandidateAfter(From - 1) : Iterator.MoveToNext();
	int32 WalkedTo = From;
	for (int32 i = Begin; i < End; i++)
	{
		const int32 Offset = PlainStarts[i] - Span.PlainBegin;
		while (Boundary != INDEX_NONE && Boundary < Offset)
		{
			Boundary = Iterator.MoveToNext();
		}
		WalkedTo = i + 1 < Span.EndElement ? PlainStarts[i + 1] - Span.PlainBegin : SpanLength;
		if (Write(i, Boundary == Offset))break;
	}
	Iterator.ClearString();
	return FMath::Max(WalkedTo - From, 0);
#endif
}

void FDreamTextBreaker::CombineBreakOpportunities(const TArray<uint32>& ElementCodepoints, const TBitArray<>& LineBoundaries,
	const TBitArray<>* WordBoundaries, EDreamTextPhraseWrap PhraseWrap, int32 Begin, int32 End, TBitArray<>& OutCanBreakBefore)
{
#if UE_ENABLE_ICU
	const bool bPhrase = PhraseWrap != EDreamTextPhraseWrap::Off && WordBoundaries != nullptr;
#else
	// No dictionary without ICU: a CJK run breaks per character, as the log said when the line rules were asked.
	(void)PhraseWrap;
	const bool bPhrase = false;
#endif
	for (int32 i = Begin; i < End; i++)
	{
		bool bBreak = LineBoundaries[i];
		// Inside a CJK run the line rules allow a break everywhere; the dictionary says where the
		// words are. Between a CJK character and anything else the line rules already decided.
		if (bBreak && bPhrase && i > 0 && IsCJKCodepoint(ElementCodepoints[i]) && IsCJKCodepoint(ElementCodepoints[i - 1]))
		{
			bBreak = (*WordBoundaries)[i];
		}
		OutCanBreakBefore[i] = bBreak;
	}
}

bool FDreamTextBreaker::IsDictionaryLineBreakCodepoint(uint32 C)
{
	return (C >= 0x0E00 && C <= 0x0EFF)    // Thai, Lao
		|| (C >= 0x1000 && C <= 0x109F)    // Myanmar
		|| (C >= 0x1780 && C <= 0x17FF)    // Khmer
		|| (C >= 0x1950 && C <= 0x19FF)    // Tai Le, New Tai Lue, Khmer symbols
		|| (C >= 0x1A20 && C <= 0x1AAF)    // Tai Tham
		|| (C >= 0xA9E0 && C <= 0xA9FF)    // Myanmar extended-B
		|| (C >= 0xAA60 && C <= 0xAADF);   // Myanmar extended-A, Tai Viet
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
		OutCanBreakBefore[i] = DreamTextBreakerLocal::CanBreakBetweenFallback(ElementCodepoints[i - 1], ElementCodepoints[i]);
	}
}

void FDreamTextBreaker::ComputeBreakOpportunities(const FString& PlainText, const TArray<int32>& ElementPlainStart,
	const TArray<uint32>& ElementCodepoints, EDreamTextPhraseWrap PhraseWrap, TBitArray<>& OutCanBreakBefore)
{
	const int32 ElementCount = ElementPlainStart.Num();
	OutCanBreakBefore.Init(false, ElementCount);
	if (ElementCount == 0)return;
	FDreamTextBoundarySpan Span;
	Span.PlainText = &PlainText;
	Span.ElementPlainStart = &ElementPlainStart;
	Span.ElementCodepoints = &ElementCodepoints;
	Span.FirstElement = 0;
	Span.EndElement = ElementCount;
	Span.PlainBegin = 0;
	Span.PlainEnd = PlainText.Len();
	auto NoStop = [](int32, bool) { return false; };
	int32 Last = 0;
	TBitArray<> LineBoundaries;
	LineBoundaries.Init(false, ElementCount);
	ComputeBoundaries(EDreamTextBoundaryKind::Line, Span, 0, ElementCount, LineBoundaries, NoStop, Last);
	const bool bPhrase = PhraseWrap != EDreamTextPhraseWrap::Off;
	TBitArray<> WordBoundaries;
	if (bPhrase)
	{
		WordBoundaries.Init(false, ElementCount);
		ComputeBoundaries(EDreamTextBoundaryKind::Word, Span, 0, ElementCount, WordBoundaries, NoStop, Last);
	}
	CombineBreakOpportunities(ElementCodepoints, LineBoundaries, bPhrase ? &WordBoundaries : nullptr, PhraseWrap, 0, ElementCount,
		OutCanBreakBefore);
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
