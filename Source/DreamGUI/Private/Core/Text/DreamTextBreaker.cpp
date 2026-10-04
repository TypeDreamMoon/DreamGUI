// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextBreaker.h"
#include "DreamGUI.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"

// ICU's own break iterators, made for the game's culture, wherever this module has ICU's headers: the targets that link
// HarfBuzz, which DreamGUI.Build.cs gives ICU too (the shaper reads bidi levels from it there). They read the text as
// UTF-16 in place, so a build whose TCHAR is not UTF-16 takes the engine's iterators.
#if UE_ENABLE_ICU && WITH_HARFBUZZ && !PLATFORM_TCHAR_IS_UTF8CHAR
#define DREAMTEXTBREAKER_CULTURE_ICU 1
THIRD_PARTY_INCLUDES_START
#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/utext.h>
#if !IS_MONOLITHIC
#include <unicode/putil.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#endif
THIRD_PARTY_INCLUDES_END
#if !IS_MONOLITHIC
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif
#else
#define DREAMTEXTBREAKER_CULTURE_ICU 0
#endif

namespace DreamTextBreakerLocal
{
	enum class EIteratorKind : uint8
	{
		Character,
		Word,
		Line,
	};

	/** Hangul syllables, which the engine's line iterator keeps whole as words. */
	bool IsHangulSyllable(uint32 C)
	{
		return C >= 0xAC00 && C <= 0xD7A3;
	}

#if DREAMTEXTBREAKER_CULTURE_ICU && !IS_MONOLITHIC
	/**
	 * ICU is a static library on these targets, so a modular build (the editor, and -game run from its binaries) gives this
	 * module a copy of ICU of its own, apart from the one Core loads its data into. Without data that copy makes no break
	 * iterator at all and the engine's default-culture ones would always stand in, so the editor would break lines by the
	 * machine's language while a packaged game breaks them by the game's. Point this copy at the loose data the engine
	 * reads, through the engine's file system as Core does (FICUInternationalization::Initialize), once. A monolithic game
	 * has one ICU, which Core has set up.
	 */
	struct FDreamTextBreakerIcuData
	{
		/** The data's own folder, normalized: requests outside it are refused, as Core refuses them. */
		FString DataDirectory;
		bool bTried = false;
		bool bReady = false;

		static FDreamTextBreakerIcuData& Get()
		{
			static FDreamTextBreakerIcuData Value;
			return Value;
		}

#if defined(WITH_ICU_V78) && WITH_ICU_V78
		static UBool U_CALLCONV Open(const void* InContext, void** OutFileContext, void** OutContents, const char* InPath, int32_t* OutLength)
#else
		static UBool U_CALLCONV Open(const void* InContext, void** OutFileContext, void** OutContents, const char* InPath)
#endif
		{
			*OutFileContext = nullptr;
			*OutContents = nullptr;
			const FDreamTextBreakerIcuData* This = static_cast<const FDreamTextBreakerIcuData*>(InContext);
			FString PathName = StringCast<TCHAR>(InPath).Get();
			FPaths::NormalizeFilename(PathName);
			if (This == nullptr || !PathName.StartsWith(This->DataDirectory))
			{
				return false;
			}
			TArray<uint8>* Bytes = new TArray<uint8>();
			if (!FFileHelper::LoadFileToArray(*Bytes, *PathName, FILEREAD_Silent) || Bytes->Num() == 0)
			{
				delete Bytes;
				return false;
			}
			*OutFileContext = Bytes;
			*OutContents = Bytes->GetData();
#if defined(WITH_ICU_V78) && WITH_ICU_V78
			*OutLength = Bytes->Num();
#endif
			return true;
		}

		static void U_CALLCONV Close(const void* InContext, void* const InFileContext, void* const InContents)
		{
			delete static_cast<TArray<uint8>*>(InFileContext);
		}

		/** Whether this module's ICU can read its data; set up on the first call (game thread). */
		static bool Ensure()
		{
			FDreamTextBreakerIcuData& Data = Get();
			if (Data.bTried)
			{
				return Data.bReady;
			}
			Data.bTried = true;
#if defined(WITH_ICU_V78) && WITH_ICU_V78
			const TCHAR* DataFolder = TEXT("icudt78l");
#elif defined(WITH_ICU_V64) && WITH_ICU_V64
			const TCHAR* DataFolder = TEXT("icudt64l");
#else
			const TCHAR* DataFolder = TEXT("icudt53l");
#endif
			const FString Candidates[] =
			{
				FPaths::ProjectContentDir() / TEXT("Internationalization"),
				FPaths::EngineContentDir() / TEXT("Internationalization"),
			};
			for (const FString& Candidate : Candidates)
			{
				if (FPaths::DirectoryExists(Candidate / DataFolder))
				{
					UErrorCode Status = U_ZERO_ERROR;
					Data.DataDirectory = Candidate / DataFolder / TEXT("");
					FPaths::NormalizeFilename(Data.DataDirectory);
					u_setDataDirectory(StringCast<char>(*Candidate).Get());
					udata_setFileAccess(UDATA_FILES_FIRST, &Status);
					u_setDataFileFunctions(&Data, &FDreamTextBreakerIcuData::Open, &FDreamTextBreakerIcuData::Close, &Status);
					u_init(&Status);
					Data.bReady = U_SUCCESS(Status) != 0;
					if (!Data.bReady)
					{
						UE_LOG(DreamGUI, Warning, TEXT("Line breaking falls back to the engine's iterators in this build: ICU could not read its data from %s (%hs)."), *Candidate, u_errorName(Status));
					}
					break;
				}
			}
			return Data.bReady;
		}
	};
#endif

	/** Localization.HangulTextWrappingMethod as the engine's line iterator reads it: 0 per syllable, anything else per word (the default). */
	bool IsHangulWrappedPerWord()
	{
		static IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.HangulTextWrappingMethod"));
		return Variable == nullptr || Variable->GetInt() != 0;
	}

	/**
	 * One kind of boundary, walked over a stretch of the layout's plain text: ICU's iterator for the game's culture, or --
	 * for a culture ICU cannot make one for, and on a build without ICU's headers here -- the engine's, which FBreakIterator
	 * always makes for the default culture, the machine's.
	 */
	struct FBoundaryIterator
	{
		EIteratorKind Kind = EIteratorKind::Line;
#if DREAMTEXTBREAKER_CULTURE_ICU
		static_assert(sizeof(TCHAR) == sizeof(UChar), "the text is handed to ICU as the UTF-16 it is");
		/** The culture's own iterator; null when the engine's stands in. */
		TUniquePtr<icu::BreakIterator> Culture;
		const TCHAR* Chars = nullptr;
		int32 Length = 0;
		int32 Current = 0;
#endif
		TSharedPtr<IBreakIterator> Engine;

		~FBoundaryIterator()
		{
#if DREAMTEXTBREAKER_CULTURE_ICU
			// Kept to the end of the process, by when ICU may have dropped the data its rules point into: left to the process
			// to free rather than freed after that.
			(void)Culture.Release();
#endif
		}

		void SetText(FStringView Text)
		{
#if DREAMTEXTBREAKER_CULTURE_ICU
			if (Culture.IsValid())
			{
				Chars = Text.GetData();
				Length = Text.Len();
				Current = 0;
				UText Handle = UTEXT_INITIALIZER;
				UErrorCode Status = U_ZERO_ERROR;
				utext_openUChars(&Handle, reinterpret_cast<const UChar*>(Chars), Length, &Status);
				// The iterator keeps a shallow copy of the handle: the text itself is read in place until the next SetText.
				Culture->setText(&Handle, Status);
				utext_close(&Handle);
				return;
			}
#endif
			Engine->SetStringRef(Text);
		}

		/** The first boundary after Offset, or INDEX_NONE past the last. */
		int32 Following(int32 Offset)
		{
#if DREAMTEXTBREAKER_CULTURE_ICU
			if (Culture.IsValid())
			{
				int32 At = Offset;
				// The engine's line iterator, asked from a Hangul syllable, looks on from the last syllable of its word, so a Korean
				// word is never broken inside (Localization.HangulTextWrappingMethod 1). Done the same here, so lines break
				// where they always did.
				if (Kind == EIteratorKind::Line && At >= 0 && At < Length && IsHangulSyllable((uint32)Chars[At]) && IsHangulWrappedPerWord())
				{
					while (At + 1 < Length && IsHangulSyllable((uint32)Chars[At + 1]))
					{
						At++;
					}
				}
				Current = Culture->following(At);
				return Current == icu::BreakIterator::DONE ? INDEX_NONE : Current;
			}
#endif
			return Engine->MoveToCandidateAfter(Offset);
		}

		/** The boundary after the last one found. */
		int32 Next()
		{
#if DREAMTEXTBREAKER_CULTURE_ICU
			if (Culture.IsValid())
			{
				if (Current == icu::BreakIterator::DONE)
				{
					return INDEX_NONE;
				}
				// The engine's line iterator steps on as it is asked from a position: the Hangul rule applies at every step.
				if (Kind == EIteratorKind::Line)
				{
					return Following(Current);
				}
				Current = Culture->next();
				return Current == icu::BreakIterator::DONE ? INDEX_NONE : Current;
			}
#endif
			return Engine->MoveToNext();
		}

		/** Lets go of the text, which belongs to the layout. */
		void ClearText()
		{
#if DREAMTEXTBREAKER_CULTURE_ICU
			if (Culture.IsValid())
			{
				static const UChar NoText = 0;
				UText Handle = UTEXT_INITIALIZER;
				UErrorCode Status = U_ZERO_ERROR;
				utext_openUChars(&Handle, &NoText, 0, &Status);
				Culture->setText(&Handle, Status);
				utext_close(&Handle);
				Chars = nullptr;
				Length = 0;
				Current = 0;
				return;
			}
#endif
			Engine->ClearString();
		}
	};

	/**
	 * The three iterators boundary analysis runs. Layout runs on the game thread, and making an ICU iterator loads and copies a
	 * rule set, so one of each is made and kept. Line breaking follows the game's current culture, as the layout's measure key
	 * says it does (a culture switch lays every kept layout out again): the iterators are made for that culture's ICU locale
	 * -- the Japanese and Chinese line tailorings, where the packaged ICU data has them; the dictionaries of the scripts
	 * written without spaces serve every locale -- and made again when the culture changes. FBreakIterator alone would not
	 * do: it always clones the default culture's, the machine's language. A culture with no tailoring of its own gets ICU's
	 * root rules; one ICU cannot make a locale or iterators for falls back to the engine's.
	 */
	struct FIterators
	{
		/** The culture they were made for, how many times they were made, and whether they carry its locale. */
		FString CultureName;
		int32 BuildCount = 0;
		bool bForCulture = false;
		FBoundaryIterator Character;
		FBoundaryIterator Word;
		FBoundaryIterator Line;

		/** The iterators as they are, made or not. */
		static FIterators& Instance()
		{
			static FIterators Value;
			return Value;
		}

		/** The iterators for the game's culture as it is now. */
		static FIterators& Get()
		{
			check(IsInGameThread());
			FIterators& Iterators = Instance();
			const FCultureRef Current = FInternationalization::Get().GetCurrentCulture();
			if (Iterators.BuildCount == 0 || !Iterators.CultureName.Equals(Current->GetName(), ESearchCase::CaseSensitive))
			{
				Iterators.Build(Current->GetName());
			}
			return Iterators;
		}

		void Build(const FString& InCultureName)
		{
			CultureName = InCultureName;
			BuildCount++;
			Character.Kind = EIteratorKind::Character;
			Word.Kind = EIteratorKind::Word;
			Line.Kind = EIteratorKind::Line;
			bForCulture = false;
#if DREAMTEXTBREAKER_CULTURE_ICU
			// The locale as the engine makes one from a culture's name ("ja-JP", "zh-Hans"); ICU reads either separator.
			const icu::Locale Locale(TCHAR_TO_ANSI(*InCultureName));
			bForCulture = !Locale.isBogus();
#if !IS_MONOLITHIC
			bForCulture = bForCulture && FDreamTextBreakerIcuData::Ensure();
#endif
			auto Make = [&Locale](icu::BreakIterator* (*Factory)(const icu::Locale&, UErrorCode&)) -> icu::BreakIterator*
			{
				UErrorCode Status = U_ZERO_ERROR;
				icu::BreakIterator* Made = Factory(Locale, Status);
				if (Made != nullptr && U_FAILURE(Status))
				{
					delete Made;
					Made = nullptr;
				}
				return Made;
			};
			if (bForCulture)
			{
				Character.Culture.Reset(Make(&icu::BreakIterator::createCharacterInstance));
				Word.Culture.Reset(Make(&icu::BreakIterator::createWordInstance));
				Line.Culture.Reset(Make(&icu::BreakIterator::createLineInstance));
				bForCulture = Character.Culture.IsValid() && Word.Culture.IsValid() && Line.Culture.IsValid();
			}
			if (!bForCulture)
			{
				Character.Culture.Reset();
				Word.Culture.Reset();
				Line.Culture.Reset();
			}
#endif
			if (bForCulture)
			{
				Character.Engine.Reset();
				Word.Engine.Reset();
				Line.Engine.Reset();
			}
			else
			{
				Character.Engine = FBreakIterator::CreateCharacterBoundaryIterator();
				Word.Engine = FBreakIterator::CreateWordBreakIterator();
				Line.Engine = FBreakIterator::CreateLineBreakIterator();
			}
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
	FBoundaryIterator& Iterator = bGrapheme ? Iterators.Character
		: (Kind == EDreamTextBoundaryKind::Line ? Iterators.Line : Iterators.Word);
	const int32 SpanLength = Span.PlainEnd - Span.PlainBegin;
	Iterator.SetText(FStringView(**Span.PlainText + Span.PlainBegin, SpanLength));
	// From the span's start ICU walks forward as it always did. From inside it, ICU's own random access (following) backs up
	// to a point its rules call safe and walks forward from there, which finds the boundaries a walk from the start finds --
	// asked from a boundary of that walk, for the line iterator's Hangul rule (see the header).
	const int32 From = PlainStarts[Begin] - Span.PlainBegin;
	int32 Boundary = From > 0 ? Iterator.Following(From - 1) : Iterator.Next();
	int32 WalkedTo = From;
	for (int32 i = Begin; i < End; i++)
	{
		const int32 Offset = PlainStarts[i] - Span.PlainBegin;
		while (Boundary != INDEX_NONE && Boundary < Offset)
		{
			Boundary = Iterator.Next();
		}
		WalkedTo = i + 1 < Span.EndElement ? PlainStarts[i + 1] - Span.PlainBegin : SpanLength;
		if (Write(i, Boundary == Offset))break;
	}
	Iterator.ClearText();
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

FString FDreamTextBreaker::GetIteratorCultureName()
{
	return DreamTextBreakerLocal::FIterators::Instance().CultureName;
}

int32 FDreamTextBreaker::GetIteratorBuildCount()
{
	return DreamTextBreakerLocal::FIterators::Instance().BuildCount;
}

bool FDreamTextBreaker::AreIteratorsForCulture()
{
	return DreamTextBreakerLocal::FIterators::Instance().bForCulture;
}
