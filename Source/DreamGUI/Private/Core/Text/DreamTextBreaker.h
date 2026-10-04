// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Templates/Function.h"
#include "Core/DreamUITextData.h"

/** Which of ICU's iterators a boundary analysis runs (FDreamTextBreaker::ComputeBoundaries). */
enum class EDreamTextBoundaryKind : uint8
{
	/** Extended grapheme clusters (UAX #29), Indic conjuncts kept whole: what ComputeGraphemeStarts gives. */
	Grapheme,
	/** Where a line may break (UAX #14) as ICU's line iterator finds it, before phrase wrap narrows CJK runs. */
	Line,
	/** Word boundaries (UAX #29 and ICU's dictionaries): what phrase wrap narrows a CJK run's breaks to. */
	Word,
};

/**
 * A stretch of the layout's plain text a boundary analysis runs over: one paragraph or several in a row, each with the hard
 * break that ends it. No rule of the three iterators looks across a hard break, so what is found inside the stretch is
 * what a run over the whole text finds.
 */
struct FDreamTextBoundarySpan
{
	/** The layout's plain text, and each element's first code unit in it and its code point (as the functions below take them). */
	const FString* PlainText = nullptr;
	const TArray<int32>* ElementPlainStart = nullptr;
	const TArray<uint32>* ElementCodepoints = nullptr;
	/** The stretch's elements, [FirstElement, EndElement), and its code units, [PlainBegin, PlainEnd). */
	int32 FirstElement = 0;
	int32 EndElement = 0;
	int32 PlainBegin = 0;
	int32 PlainEnd = 0;
};

/**
 * Where a line may end. The answer comes from ICU's line-break rules (UAX #14) over the plain text --
 * English breaks at spaces and after hyphens, CJK breaks between any two ideographs except where
 * kinsoku forbids it -- optionally narrowed, for CJK runs, to the word boundaries ICU's dictionary
 * finds, which is what turns "break anywhere" into "break between words".
 */
class DREAMGUI_API FDreamTextBreaker
{
public:
	/**
	 * The boundaries of one kind at the starts of the elements [Begin, End) of a span: bit i of OutBits (one bit per element
	 * of the layout, sized by the caller) says whether one stands at element i's start. ICU is handed the whole span and
	 * reaches Begin's start by its own random access (following), so a window inside a paragraph comes out as a run over
	 * the whole paragraph gives it -- what lets an edit be analysed again around itself alone -- provided the element
	 * before Begin starts at a boundary of that run: the engine's line iterator skips a Hangul word whole from wherever it
	 * is asked, and asked from inside one it steps over a boundary the whole run found there. The span's first element is a
	 * grapheme start and no line or word boundary, whatever stands before it.
	 *
	 * Grapheme is ICU's character iterator with GB9c (a text with nothing at or above U+0300 asks ICU nothing); Line and Word
	 * are ICU's line and word iterators for the game's current culture (its locale's tailorings, such as the Japanese and
	 * Chinese line rules, where the packaged data has them), with the engine's Hangul rule
	 * (Localization.HangulTextWrappingMethod) on the line iterator. Without ICU, graphemes come from the code points, Line
	 * from ComputeFallbackBreakOpportunities' rules, and Word marks nothing.
	 *
	 * @param Stop     Asked with each element once its bit is written (the bit as written): true ends the walk there.
	 * @param OutLast  The last element written; Begin - 1 when there was none.
	 * @return The UTF-16 code units ICU walked, from Begin's start to the end of the last element written; 0 when ICU was not asked.
	 */
	static int32 ComputeBoundaries(EDreamTextBoundaryKind Kind, const FDreamTextBoundarySpan& Span, int32 Begin, int32 End,
		TBitArray<>& OutBits, TFunctionRef<bool(int32 Element, bool bBoundary)> Stop, int32& OutLast);

	/**
	 * Break opportunities from raw line and word boundaries, as ComputeBreakOpportunities makes them: under phrase wrap, a
	 * break between two CJK characters stands only where a word ends (and phrase wrap does nothing without ICU, which has
	 * the dictionary). Writes OutCanBreakBefore's bits for the elements [Begin, End).
	 */
	static void CombineBreakOpportunities(const TArray<uint32>& ElementCodepoints, const TBitArray<>& LineBoundaries,
		const TBitArray<>* WordBoundaries, EDreamTextPhraseWrap PhraseWrap, int32 Begin, int32 End, TBitArray<>& OutCanBreakBefore);

	/**
	 * Scripts ICU breaks lines in with a dictionary rather than by rule: Thai, Lao, Myanmar, Khmer and the Tai scripts. The
	 * dictionary reads a run of them whole, so an edit inside one has the whole run read again.
	 */
	static bool IsDictionaryLineBreakCodepoint(uint32 Codepoint);

	/**
	 * @param PlainText          The text as laid out: tags stripped, image placeholders as spaces.
	 * @param ElementPlainStart  For each layout element, the index of its first UTF-16 unit in PlainText.
	 * @param ElementCodepoints  For each layout element, its code point (to tell CJK runs apart).
	 * @param PhraseWrap         Whether CJK runs may only break at dictionary word boundaries.
	 * @param OutCanBreakBefore  One bit per element: a line may end just before this element.
	 */
	static void ComputeBreakOpportunities(const FString& PlainText, const TArray<int32>& ElementPlainStart,
		const TArray<uint32>& ElementCodepoints, EDreamTextPhraseWrap PhraseWrap, TBitArray<>& OutCanBreakBefore);

	/**
	 * The subset of UAX #14 this plugin computes for itself: break after spaces, break on either side
	 * of an ideograph, break after a hyphen, and never put a closing mark at the start of a line or an
	 * opening bracket at the end of one. It is what a build without ICU uses -- the engine's legacy
	 * iterator breaks on whitespace alone, so CJK never wrapped at all -- and it is a pure function of
	 * the code points, so it is asserted on directly whether or not the build has ICU.
	 */
	static void ComputeFallbackBreakOpportunities(const TArray<uint32>& ElementCodepoints, TBitArray<>& OutCanBreakBefore);

	/**
	 * One bit per element: an extended grapheme cluster (UAX #29) starts at this element, so a caret may stand before it
	 * and a line may be cut there. ICU's character iterator decides, plus the Indic conjunct rule of Unicode 15.1 (GB9c:
	 * a consonant, a virama and the next consonant are one cluster) that the engine's ICU predates. Text with nothing at
	 * or above U+0300 is all single code points and never asks ICU.
	 */
	static void ComputeGraphemeStarts(const FString& PlainText, const TArray<int32>& ElementPlainStart,
		const TArray<uint32>& ElementCodepoints, TBitArray<>& OutGraphemeStart);

	/** Han, Kana, Hangul: the scripts whose "words" the line-break rules cannot see. */
	static bool IsCJKCodepoint(uint32 Codepoint);
	/** A space a line may break after: ASCII space and tab, the ideographic space, the en/em family. */
	static bool IsBreakingSpace(uint32 Codepoint);

	/**
	 * Where the per-character fallback may cut an unbreakable run. The rules said "nowhere"; the
	 * fallback has to pick somewhere, and a browser's break-all still keeps closing punctuation off
	 * the start of a line and opening brackets off the end of one. Returns the element index to
	 * break before, searching back from BreakBefore to just after LineStart, or INDEX_NONE when no
	 * cut on this line is safe -- then the run overflows, which is also what a browser does. With
	 * ClusterStarts, only an element whose bit is set is a cut: a base and its marks, a conjunct and
	 * a ligature stay on one line.
	 */
	static int32 FindKinsokuSafeFallback(const TArray<uint32>& ElementCodepoints, int32 LineStart, int32 BreakBefore, const TBitArray<>* ClusterStarts = nullptr);
	static bool IsClosingPunctuation(uint32 Codepoint);
	static bool IsOpeningPunctuation(uint32 Codepoint);

	/*
	 * What the iterators were made for: what a test reads to see a culture switch reach line breaking, which on most text
	 * looks the same in every locale. Game thread.
	 */
	/** The culture the line, word and character iterators were last made for; empty before the first boundary analysis. */
	static FString GetIteratorCultureName();
	/** How many times the iterators have been made: once, and again after each switch of the game's culture. */
	static int32 GetIteratorBuildCount();
	/** The iterators carry that culture's own ICU locale; false when ICU could not make them for it and the engine's, made for the default culture, stand in. */
	static bool AreIteratorsForCulture();
};
