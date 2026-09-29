// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Core/DreamUITextData.h"

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

	/** Han, Kana, Hangul: the scripts whose "words" the line-break rules cannot see. */
	static bool IsCJKCodepoint(uint32 Codepoint);
	/** A space a line may break after: ASCII space and tab, the ideographic space, the en/em family. */
	static bool IsBreakingSpace(uint32 Codepoint);

	/**
	 * Where the per-character fallback may cut an unbreakable run. The rules said "nowhere"; the
	 * fallback has to pick somewhere, and a browser's break-all still keeps closing punctuation off
	 * the start of a line and opening brackets off the end of one. Returns the element index to
	 * break before, searching back from BreakBefore to just after LineStart, or INDEX_NONE when no
	 * cut on this line is safe -- then the run overflows, which is also what a browser does.
	 */
	static int32 FindKinsokuSafeFallback(const TArray<uint32>& ElementCodepoints, int32 LineStart, int32 BreakBefore);
	static bool IsClosingPunctuation(uint32 Codepoint);
	static bool IsOpeningPunctuation(uint32 Codepoint);
};
