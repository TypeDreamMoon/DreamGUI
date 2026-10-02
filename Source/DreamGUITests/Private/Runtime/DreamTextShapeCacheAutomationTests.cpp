// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/Text/DreamTextShaper.h"
#include "Core/Text/DreamTextShapeCache.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

/*
 * The word-level shape cache against the engine's own fonts. What matters most is that it changes nothing: every run
 * comes out glyph for glyph as the shaper makes it with the cache off. Then that it saves the work it is there to save,
 * stays inside its budget, and forgets a face that was reloaded.
 */
namespace DreamTextShapeCacheTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamUIFontData_DistanceField* MakeFont(UWorld* World, const FString& Path)
	{
		// The tests read glyph ids, not atlas quads, but a font that rasterizes on the spot behaves as the others' do.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(Path, false);
		Font->InitFont();
		return Font;
	}

	FString RuntimeFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	FString EditorFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts"), Name);
	}

	/** DreamGUI.Text.ShapeCache and DreamGUI.Text.ShapeCacheKB set as the console sets them, and put back after. */
	struct FScopedShapeCacheSettings
	{
		int32 OldEnabled = 1;
		int32 OldBudgetKB = 4096;

		FScopedShapeCacheSettings()
		{
			OldEnabled = Get(TEXT("DreamGUI.Text.ShapeCache"), 1);
			OldBudgetKB = Get(TEXT("DreamGUI.Text.ShapeCacheKB"), 4096);
		}
		~FScopedShapeCacheSettings()
		{
			Set(TEXT("DreamGUI.Text.ShapeCache"), OldEnabled);
			Set(TEXT("DreamGUI.Text.ShapeCacheKB"), OldBudgetKB);
			FDreamTextShapeCache::Flush();
		}
		void SetEnabled(bool bEnabled)
		{
			Set(TEXT("DreamGUI.Text.ShapeCache"), bEnabled ? 1 : 0);
		}
		void SetBudgetKB(int32 BudgetKB)
		{
			Set(TEXT("DreamGUI.Text.ShapeCacheKB"), BudgetKB);
		}

		static int32 Get(const TCHAR* Name, int32 Default)
		{
			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			return Variable != nullptr ? Variable->GetInt() : Default;
		}
		static void Set(const TCHAR* Name, int32 Value)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
	};

	/** A paragraph of elements, an emoji sequence as one element: its first code point, the rest in the sequence array. */
	struct FParagraph
	{
		TArray<FDreamShapeElement> Elements;
		TArray<uint32> Sequences;
		bool bLigatures = false;

		/** One element per code point of Text, each its own cluster. */
		FParagraph& Add(const FString& Text)
		{
			for (int32 Index = 0; Index < Text.Len(); Index++)
			{
				uint32 Codepoint = Text[Index];
				if (Codepoint >= 0xD800 && Codepoint <= 0xDBFF && Index + 1 < Text.Len())
				{
					Codepoint = 0x10000 + ((Codepoint - 0xD800) << 10) + ((uint32)Text[Index + 1] - 0xDC00);
					Index++;
				}
				FDreamShapeElement& Element = Elements.AddDefaulted_GetRef();
				Element.Codepoint = Codepoint;
				Element.Size = 32.0f;
			}
			return *this;
		}

		/** One element of a whole sequence. */
		FParagraph& AddSequence(const TArray<uint32>& Codepoints)
		{
			FDreamShapeElement& Element = Elements.AddDefaulted_GetRef();
			Element.Codepoint = Codepoints[0];
			Element.Size = 32.0f;
			Element.SequenceStart = Sequences.Num();
			Element.SequenceCount = Codepoints.Num() - 1;
			for (int32 Index = 1; Index < Codepoints.Num(); Index++)
			{
				Sequences.Add(Codepoints[Index]);
			}
			return *this;
		}

		TArray<FDreamShapedRun> Shape(UDreamUIFontData_BaseObject* Font) const
		{
			FDreamShapeParams Params;
			Params.Font = Font;
			Params.bUseKerning = true;
			Params.bLigatures = bLigatures;
			Params.SequenceCodepoints = &Sequences;
			TArray<FDreamShapedRun> Runs;
			bool bRightToLeft = false;
			FDreamTextShaper::ShapeParagraph(Elements, Params, Runs, bRightToLeft);
			return Runs;
		}
	};

	FParagraph MakeParagraph(const FString& InText)
	{
		FParagraph Paragraph;
		Paragraph.Add(InText);
		return Paragraph;
	}

	/** Every field of every run and glyph the same, floats bit for bit; the first difference reported. */
	bool SameRuns(FAutomationTestBase& Test, const FString& What, const TArray<FDreamShapedRun>& Expected, const TArray<FDreamShapedRun>& Actual)
	{
		if (Expected.Num() != Actual.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: %d runs, expected %d"), *What, Actual.Num(), Expected.Num()));
			return false;
		}
		for (int32 RunIndex = 0; RunIndex < Expected.Num(); RunIndex++)
		{
			const FDreamShapedRun& A = Expected[RunIndex];
			const FDreamShapedRun& B = Actual[RunIndex];
			const bool bSameRun = A.ElementStart == B.ElementStart && A.ElementEnd == B.ElementEnd && A.bRightToLeft == B.bRightToLeft
				&& A.BidiLevel == B.BidiLevel && A.FaceIndex == B.FaceIndex && A.Size == B.Size && A.FaceScale == B.FaceScale
				&& A.bColorFace == B.bColorFace && A.bBold == B.bBold && A.bSyntheticBold == B.bSyntheticBold && A.Glyphs.Num() == B.Glyphs.Num();
			if (!bSameRun)
			{
				Test.AddError(FString::Printf(TEXT("%s: run %d differs (elements %d-%d face %d, %d glyphs; expected %d-%d face %d, %d glyphs)"), *What, RunIndex,
					B.ElementStart, B.ElementEnd, B.FaceIndex, B.Glyphs.Num(), A.ElementStart, A.ElementEnd, A.FaceIndex, A.Glyphs.Num()));
				return false;
			}
			for (int32 GlyphIndex = 0; GlyphIndex < A.Glyphs.Num(); GlyphIndex++)
			{
				const FDreamShapedGlyph& GA = A.Glyphs[GlyphIndex];
				const FDreamShapedGlyph& GB = B.Glyphs[GlyphIndex];
				if (GA.FaceIndex != GB.FaceIndex || GA.GlyphIndex != GB.GlyphIndex || GA.ElementIndex != GB.ElementIndex
					|| GA.XAdvance != GB.XAdvance || GA.XOffset != GB.XOffset || GA.YOffset != GB.YOffset)
				{
					Test.AddError(FString::Printf(TEXT("%s: run %d glyph %d is glyph %u of element %d advancing %.4f, expected glyph %u of element %d advancing %.4f"),
						*What, RunIndex, GlyphIndex, GB.GlyphIndex, GB.ElementIndex, GB.XAdvance, GA.GlyphIndex, GA.ElementIndex, GA.XAdvance));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheSameRunsTest,
	"DreamGUI.Text.ShapeCache.RunsComeOutTheSameWithTheCacheOnAndOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A cut that HarfBuzz would have shaped differently -- a kern pair across it, a ligature, a joining letter, a mark --
 * draws stale or wrong glyphs. Latin with kerning next to spaces and across no-break spaces, ligatures, Arabic alone and
 * mixed with Latin, Devanagari (whose font positions spaces, so its runs are kept whole), CJK and emoji sequences: each
 * is shaped with the cache off, then on twice -- cold, then warm after another text has filled it -- and must not move.
 */
bool FDreamTextShapeCacheSameRunsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	UWorld* World = TestWorld.World;
	UDreamUIFontData_DistanceField* Roboto = MakeFont(World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	UDreamUIFontData_DistanceField* Naskh = MakeFont(World, RuntimeFont(TEXT("NotoNaskhArabicUI-Regular.ttf")));
	UDreamUIFontData_DistanceField* LatinArabic = MakeFont(World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	LatinArabic->SetFallbackFonts({ MakeFont(World, RuntimeFont(TEXT("NotoNaskhArabicUI-Regular.ttf"))) });
	UDreamUIFontData_DistanceField* Devanagari = MakeFont(World, EditorFont(TEXT("NotoSansDevanagari-Regular.ttf")));
	UDreamUIFontData_DistanceField* LatinCjk = MakeFont(World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	LatinCjk->SetFallbackFonts({ MakeFont(World, RuntimeFont(TEXT("DroidSansFallback.ttf"))) });
	UDreamUIFontData_DistanceField* LatinEmoji = MakeFont(World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	LatinEmoji->SetFallbackFonts({ MakeFont(World, EditorFont(TEXT("NotoColorEmoji.ttf"))) });
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	TestTrue(TEXT("Roboto names its face, so the cache keeps its words"), Roboto->GetFaceIdentity(0).IsValid());

	struct FCase
	{
		const TCHAR* Name;
		UDreamUIFontData_DistanceField* Font;
		FParagraph Paragraph;
	};
	TArray<FCase> Cases;
	Cases.Add({ TEXT("Latin kerned next to spaces"), Roboto, MakeParagraph(TEXT("AVA To. Wa Yo, Ty AV LT 'quoted' fly V. W. AVAVA")) });
	Cases.Add({ TEXT("Latin kerned across no-break spaces"), Roboto, MakeParagraph(TEXT("A\u00A0V T\u00A0o AV\u00A0AV\u00A0Ta")) });
	Cases.Add({ TEXT("Latin with tabs"), Roboto, MakeParagraph(TEXT("AV\tTo\t\tWa")) });
	FParagraph Ligatures = MakeParagraph(TEXT("office fi fly affine baffle"));
	Ligatures.bLigatures = true;
	Cases.Add({ TEXT("Latin with ligatures"), Roboto, Ligatures });
	Cases.Add({ TEXT("Arabic"), Naskh, MakeParagraph(TEXT("\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645 \u0645\u0631\u062D\u0628\u0627 \u0644\u0627 \u0633\u0644\u0627\u0645")) });
	Cases.Add({ TEXT("Arabic among Latin"), LatinArabic, MakeParagraph(TEXT("go \u0645\u0631\u062D\u0628\u0627 12 \u0639\u0627\u0644\u0645 now \u0645\u0631\u062D\u0628\u0627")) });
	Cases.Add({ TEXT("Devanagari"), Devanagari, MakeParagraph(TEXT("\u0928\u092E\u0938\u094D\u0924\u0947 \u0926\u0941\u0928\u093F\u092F\u093E \u0915\u094D\u0937\u0924\u094D\u0930\u091C\u094D\u091E \u0915\u093F")) });
	Cases.Add({ TEXT("CJK"), LatinCjk, MakeParagraph(TEXT("\u65E5\u672C\u8A9E\u306E\u30C6\u30AD\u30B9\u30C8\u3001\u6F22\u5B57\u3002\u4E2D\u6587 \u6D4B\u8BD5 Hello \u4E16\u754C\u300C\u5F15\u7528\u300D")) });
	FParagraph Emoji;
	Emoji.Add(TEXT("Hi "));
	Emoji.AddSequence({ 0x1F44D, 0x1F3FD });
	Emoji.Add(TEXT(" and "));
	Emoji.AddSequence({ 0x1F1EF, 0x1F1F5 });
	Emoji.AddSequence({ 0x1F600 });
	Emoji.AddSequence({ 0x1F600 });
	Emoji.Add(TEXT(" "));
	Emoji.AddSequence({ 0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467 });
	Emoji.AddSequence({ '1', 0xFE0F, 0x20E3 });
	Emoji.AddSequence({ 0x2764, 0xFE0F });
	Cases.Add({ TEXT("emoji sequences"), LatinEmoji, Emoji });

	// What fills the cache between the cold and the warm pass: the same words in other company.
	const FParagraph Filler = MakeParagraph(TEXT("To. Wa AV office \u0645\u0631\u062D\u0628\u0627 \u6F22\u5B57 Hello and Hi "));

	for (const FCase& Case : Cases)
	{
		Settings.SetEnabled(false);
		const TArray<FDreamShapedRun> Off = Case.Paragraph.Shape(Case.Font);
		if (!TestTrue(FString::Printf(TEXT("%s: shaped"), Case.Name), Off.Num() > 0))
		{
			continue;
		}
		Settings.SetEnabled(true);
		FDreamTextShapeCache::Flush();
		const TArray<FDreamShapedRun> Cold = Case.Paragraph.Shape(Case.Font);
		SameRuns(*this, FString::Printf(TEXT("%s, cache cold"), Case.Name), Off, Cold);
		Filler.Shape(Case.Font);
		FDreamTextShapeCache::ResetStats();
		const TArray<FDreamShapedRun> Warm = Case.Paragraph.Shape(Case.Font);
		SameRuns(*this, FString::Printf(TEXT("%s, cache warm"), Case.Name), Off, Warm);
		const FDreamTextShapeCache::FStats Stats = FDreamTextShapeCache::GetStats();
		TestTrue(FString::Printf(TEXT("%s: the warm pass is answered by the cache"), Case.Name), Stats.Hits > 0 && Stats.ShapeCalls == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheReuseTest,
	"DreamGUI.Text.ShapeCache.AWordShapedBeforeIsNotShapedAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShapeCacheReuseTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	Settings.SetEnabled(true);
	UDreamUIFontData_DistanceField* Roboto = MakeFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	FDreamTextShapeCache::Flush();
	FDreamTextShapeCache::ResetStats();

	// brave, space, new, space, world: five segments, missed together and shaped in one call.
	MakeParagraph(TEXT("brave new world")).Shape(Roboto);
	FDreamTextShapeCache::FStats Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("five words and spaces looked up"), Stats.Lookups, (int64)5);
	TestEqual(TEXT("none there yet"), Stats.Hits, (int64)0);
	TestEqual(TEXT("so the run is shaped, in one call"), Stats.ShapeCalls, (int64)1);
	TestEqual(TEXT("of its fifteen code points"), Stats.ShapedCodepoints, (int64)15);
	TestTrue(TEXT("and the cache holds them"), FDreamTextShapeCache::GetNumEntries() >= 3 && FDreamTextShapeCache::GetBytesUsed() > 0);

	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("brave new world")).Shape(Roboto);
	Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("the same text again calls HarfBuzz not once"), Stats.ShapeCalls, (int64)0);
	TestEqual(TEXT("every segment found"), Stats.Hits, Stats.Lookups);

	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("world new brave")).Shape(Roboto);
	Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("the same words in another text need no shaping either"), Stats.ShapeCalls, (int64)0);

	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("brave old world")).Shape(Roboto);
	Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("a new word is shaped alone"), Stats.ShapeCalls, (int64)1);
	TestEqual(TEXT("three code points of it"), Stats.ShapedCodepoints, (int64)3);
	TestEqual(TEXT("the rest found"), Stats.Hits, (int64)4);

	// The switch off: every run shaped whole, nothing looked up, the calls still counted.
	Settings.SetEnabled(false);
	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("brave new world")).Shape(Roboto);
	Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("with the cache off nothing is looked up"), Stats.Lookups, (int64)0);
	TestEqual(TEXT("and the run is shaped"), Stats.ShapeCalls, (int64)1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheWholeRunTest,
	"DreamGUI.Text.ShapeCache.AFaceWhoseLookupsReadTheSpaceIsCachedARunAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Noto Sans Devanagari positions marks with a context that reads the space ('dist'), so a word shaped apart from the
 * space next to it could come out otherwise: its runs are cached whole, Blink's rule for such a font.
 */
bool FDreamTextShapeCacheWholeRunTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	Settings.SetEnabled(true);
	UDreamUIFontData_DistanceField* Devanagari = MakeFont(TestWorld.World, EditorFont(TEXT("NotoSansDevanagari-Regular.ttf")));
	UDreamUIFontData_DistanceField* Roboto = MakeFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Noto Sans Devanagari shapes"), FDreamTextShaper::CanShape(Devanagari)))return false;
	FDreamTextShapeCache::Flush();
	FDreamTextShapeCache::ResetStats();

	const FParagraph Hindi = MakeParagraph(TEXT("\u0928\u092E\u0938\u094D\u0924\u0947 \u0926\u0941\u0928\u093F\u092F\u093E \u0928\u092E\u0938\u094D\u0924\u0947"));
	const TArray<FDreamShapedRun> Runs = Hindi.Shape(Devanagari);
	TestEqual(TEXT("one run"), Runs.Num(), 1);
	TestEqual(TEXT("looked up as one segment"), FDreamTextShapeCache::GetStats().Lookups, (int64)1);
	FDreamTextShapeCache::ResetStats();
	Hindi.Shape(Devanagari);
	TestEqual(TEXT("and found whole the second time"), FDreamTextShapeCache::GetStats().Hits, (int64)1);

	// Roboto's lookups never touch the space: the same shape of text is cut into its words.
	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("hello there hello")).Shape(Roboto);
	TestEqual(TEXT("a Latin run is looked up word by word"), FDreamTextShapeCache::GetStats().Lookups, (int64)5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheBudgetTest,
	"DreamGUI.Text.ShapeCache.ATinyBudgetKeepsOnlyTheMostRecentWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShapeCacheBudgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	UDreamUIFontData_DistanceField* Roboto = MakeFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	const FParagraph Alphabet = MakeParagraph(TEXT("alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar papa"));
	Settings.SetEnabled(false);
	const TArray<FDreamShapedRun> Off = Alphabet.Shape(Roboto);

	Settings.SetEnabled(true);
	Settings.SetBudgetKB(1);
	FDreamTextShapeCache::Flush();
	FDreamTextShapeCache::ResetStats();
	const TArray<FDreamShapedRun> On = Alphabet.Shape(Roboto);
	SameRuns(*this, TEXT("shaped under a 1 KB budget"), Off, On);
	const FDreamTextShapeCache::FStats Stats = FDreamTextShapeCache::GetStats();
	TestTrue(TEXT("words were dropped to keep inside it"), Stats.Evictions > 0);
	TestTrue(*FString::Printf(TEXT("the cache stays inside its budget (%lld bytes)"), FDreamTextShapeCache::GetBytesUsed()), FDreamTextShapeCache::GetBytesUsed() <= 1024);
	TestTrue(TEXT("holding fewer entries than it was given"), FDreamTextShapeCache::GetNumEntries() < 31);

	// The last word stored is the most recent, and still there; the first is long gone.
	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("papa")).Shape(Roboto);
	TestEqual(TEXT("the most recent word is kept"), FDreamTextShapeCache::GetStats().Hits, (int64)1);
	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("alpha")).Shape(Roboto);
	TestEqual(TEXT("the oldest is not"), FDreamTextShapeCache::GetStats().Hits, (int64)0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheEpochTest,
	"DreamGUI.Text.ShapeCache.AReloadedFaceIsShapedAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Entries are keyed by the face's owner and its epoch, which moves on every time the face is loaded again: a new file
 * behind the same asset must never be drawn with the old file's glyph ids.
 */
bool FDreamTextShapeCacheEpochTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	Settings.SetEnabled(true);
	const FString Path = RuntimeFont(TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Roboto = MakeFont(TestWorld.World, Path);
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	FDreamTextShapeCache::Flush();
	const FParagraph Word = MakeParagraph(TEXT("epoch"));
	Word.Shape(Roboto);
	FDreamTextShapeCache::ResetStats();
	Word.Shape(Roboto);
	TestEqual(TEXT("the word is found"), FDreamTextShapeCache::GetStats().Hits, (int64)1);
	const FDreamUIFontFaceIdentity Before = Roboto->GetFaceIdentity(0);

	// The same file again is still a reload: the face is opened anew.
	Roboto->SetFontFilePath(Path, false);
	FDreamTextShapeCache::ResetStats();
	Word.Shape(Roboto);
	const FDreamUIFontFaceIdentity After = Roboto->GetFaceIdentity(0);
	TestTrue(TEXT("the reload moved the face's epoch on"), After.IsValid() && After.Epoch != Before.Epoch);
	FDreamTextShapeCache::FStats Stats = FDreamTextShapeCache::GetStats();
	TestEqual(TEXT("after it the word is not found"), Stats.Hits, (int64)0);
	TestEqual(TEXT("and is shaped again"), Stats.ShapeCalls, (int64)1);
	FDreamTextShapeCache::ResetStats();
	Word.Shape(Roboto);
	TestEqual(TEXT("then found under the new epoch"), FDreamTextShapeCache::GetStats().Hits, (int64)1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapeCacheFlushTest,
	"DreamGUI.Text.ShapeCache.TheFlushCommandEmptiesItAndKeepsTheCounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShapeCacheFlushTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShapeCacheTestLocal;
	FScopedGameWorld TestWorld;
	FScopedShapeCacheSettings Settings;
	Settings.SetEnabled(true);
	UDreamUIFontData_DistanceField* Roboto = MakeFont(TestWorld.World, RuntimeFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	FDreamTextShapeCache::ResetStats();
	MakeParagraph(TEXT("flush me")).Shape(Roboto);
	TestTrue(TEXT("the cache holds the words"), FDreamTextShapeCache::GetNumEntries() > 0);
	const int64 Lookups = FDreamTextShapeCache::GetStats().Lookups;

	IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(TEXT("DreamGUI.Text.ShapeCacheFlush"));
	if (!TestNotNull(TEXT("the flush command is registered"), Command) || !TestNotNull(TEXT("as a command"), Command->AsCommand()))
	{
		return false;
	}
	Command->AsCommand()->Execute(TArray<FString>(), nullptr, *GLog);
	TestEqual(TEXT("the command empties the cache"), FDreamTextShapeCache::GetNumEntries(), 0);
	TestEqual(TEXT("down to its last byte"), FDreamTextShapeCache::GetBytesUsed(), (int64)0);
	TestEqual(TEXT("the counters are kept"), FDreamTextShapeCache::GetStats().Lookups, Lookups);
	return true;
}

#endif
