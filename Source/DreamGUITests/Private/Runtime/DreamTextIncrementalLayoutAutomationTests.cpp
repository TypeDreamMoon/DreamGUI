// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Math/RandomStream.h"
#include "HAL/IConsoleManager.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextShapeCache.h"
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

#include <initializer_list>

/*
 * Retained incremental layout (FDreamTextLayoutState). A layout that builds on what the last one kept has to come out as a
 * layout from nothing does, field for field, whatever the edit was -- that is held over seeded runs of random edits on
 * multi-paragraph rich text in Latin, Arabic, CJK, Thai, emoji sequences, tags, tabs, justification and wrapping, on the
 * mock font and on the engine's own fonts. Then that it does only the work the edit asks for, read off the layout's and the
 * shaper's counters; and that whatever a kept layout cannot see -- the font's fallbacks, its atlas, the language, the
 * console switch -- makes the next layout start from nothing. The edits run under every setting of the switches of what an
 * incremental layout does by itself, a third of them where a boundary is easiest to get wrong, and each layout is held
 * against a fresh one twice: its display list, and the state it kept (FDreamTextLayoutEngine::DebugCompareStates).
 */
namespace DreamTextIncrementalLayoutTestLocal
{
	using DreamTests::FScopedGameWorld;

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	UDreamUIFontData_DistanceField* MakeFileFont(UWorld* World, const TCHAR* Name)
	{
		// The tests read quads back right away, so glyphs are rasterized on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(EngineFont(Name), false);
		Font->InitFont();
		return Font;
	}

	/** Roboto, with Noto Naskh Arabic, Droid Sans Fallback (CJK) and Noto Sans Thai behind it. */
	UDreamUIFontData_DistanceField* MakeMixedScriptFont(UWorld* World)
	{
		UDreamUIFontData_DistanceField* Roboto = MakeFileFont(World, TEXT("Roboto-Regular.ttf"));
		Roboto->SetFallbackFonts({ MakeFileFont(World, TEXT("NotoNaskhArabicUI-Regular.ttf")),
			MakeFileFont(World, TEXT("DroidSansFallback.ttf")), MakeFileFont(World, TEXT("NotoSansThai-Regular.ttf")) });
		return Roboto;
	}

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, float Width = 420.0f, float Height = 2000.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = Height;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
		In.bUseKerning = true;
		In.Font = Font;
		return In;
	}

	/** A console variable set for the test's length, and put back after. */
	struct FScopedConsoleInt
	{
		IConsoleVariable* Variable = nullptr;
		int32 Before = 0;

		FScopedConsoleInt(const TCHAR* Name, int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable != nullptr)
			{
				Before = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FScopedConsoleInt()
		{
			if (Variable != nullptr)
			{
				Variable->Set(Before, ECVF_SetByCode);
			}
		}
		void Set(int32 Value)
		{
			if (Variable != nullptr)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
	};

	/** The first field in which two display lists differ (FDreamTextLayoutEngine::DebugCompareDisplayLists), or an empty string. */
	FString FirstDifference(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B)
	{
		FString Difference;
		FDreamTextLayoutEngine::DebugCompareDisplayLists(A, B, Difference);
		return Difference;
	}

	/**
	 * Holds what a layout of In through State made against a layout from nothing, made into a list and a state of its own,
	 * and fails the test with the first difference: the display lists field for field, boxes and paint pieces included, and the
	 * states -- every element's measurement, glyph and boundary bit, every paragraph, line and line-local coordinate
	 * (FDreamTextLayoutEngine::DebugCompareStates). With bFreshWithoutShapeCache the fresh layout shapes every run whole: its
	 * display list is the same either way (the cache's promise), which holds the windows an edit is shaped through against
	 * the cuts the cache makes; its state then says other segments start, so states are compared with the cache on only.
	 */
	bool MatchesFresh(FAutomationTestBase& Test, const FDreamTextLayoutInput& In, const FDreamTextLayoutState& State, const FDreamTextDisplayList& Kept,
		const FString& What, bool bFreshWithoutShapeCache = false)
	{
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutState FreshState;
		if (bFreshWithoutShapeCache)
		{
			FScopedConsoleInt NoCache(TEXT("DreamGUI.Text.ShapeCache"), 0);
			FDreamTextLayoutEngine::Layout(In, Fresh, &FreshState);
		}
		else
		{
			FDreamTextLayoutEngine::Layout(In, Fresh, &FreshState);
		}
		FString Difference;
		if (!FDreamTextLayoutEngine::DebugCompareDisplayLists(Kept, Fresh, Difference))
		{
			Test.AddError(FString::Printf(TEXT("%s: the layout built on the kept one is not the fresh one -- %s. The text: \"%s\""), *What, *Difference,
				*In.Content.ReplaceCharWithEscapedChar()));
			return false;
		}
		if (!bFreshWithoutShapeCache && State.HasLayout() && FreshState.HasLayout()
			&& !FDreamTextLayoutEngine::DebugCompareStates(State, FreshState, Difference))
		{
			Test.AddError(FString::Printf(TEXT("%s: what the layout kept is not what a fresh layout keeps -- %s. The text: \"%s\""), *What, *Difference,
				*In.Content.ReplaceCharWithEscapedChar()));
			return false;
		}
		return true;
	}

	/** Lays In out through State, then holds what came out against a layout from nothing (MatchesFresh). */
	bool LaysOutAsFresh(FAutomationTestBase& Test, const FDreamTextLayoutInput& In, FDreamTextLayoutState& State, FDreamTextDisplayList& OutKept,
		const FString& What, bool bFreshWithoutShapeCache = false)
	{
		FDreamTextLayoutEngine::Layout(In, OutKept, &State);
		return MatchesFresh(Test, In, State, OutKept, What, bFreshWithoutShapeCache);
	}

	/** One setting of the switches of what an incremental layout does by itself: all on, and each of them off on its own. */
	struct FIncrementalSwitches
	{
		const TCHAR* Name;
		int32 Parse;
		int32 Measure;
		int32 InPlace;
	};
	const FIncrementalSwitches SwitchMatrix[] =
	{
		{ TEXT("every switch on"), 1, 1, 1 },
		{ TEXT("DreamGUI.Text.IncrementalParse 0"), 0, 1, 1 },
		{ TEXT("DreamGUI.Text.IncrementalMeasure 0"), 1, 0, 1 },
		{ TEXT("DreamGUI.Text.InPlaceDisplayList 0"), 1, 1, 0 },
	};

	/** The three switches set for a scope, and put back after. */
	struct FScopedIncrementalSwitches
	{
		FScopedConsoleInt Parse;
		FScopedConsoleInt Measure;
		FScopedConsoleInt InPlace;

		explicit FScopedIncrementalSwitches(const FIncrementalSwitches& Switches)
			: Parse(TEXT("DreamGUI.Text.IncrementalParse"), Switches.Parse)
			, Measure(TEXT("DreamGUI.Text.IncrementalMeasure"), Switches.Measure)
			, InPlace(TEXT("DreamGUI.Text.InPlaceDisplayList"), Switches.InPlace)
		{
		}
	};

	/*
	 * The fuzz's material. Words in four scripts, emoji sequences, combining marks and a conjunct, spaces, tabs and newlines;
	 * rich text adds tags of every kind the parser knows, character references and inline images.
	 */
	const TCHAR* const FuzzWords[] =
	{
		TEXT("the"), TEXT("quick"), TEXT("brown"), TEXT("fox"), TEXT("jumps"), TEXT("over"), TEXT("lazy"), TEXT("dog"), TEXT("office"),
		TEXT("waffle"), TEXT("AVATAR"), TEXT("Toyota"), TEXT("layout"), TEXT("incremental"), TEXT("re-break"), TEXT("3.14"), TEXT("(nested)"),
		TEXT("\u0645\u0631\u062D\u0628\u0627"), TEXT("\u0628\u0627\u0644\u0639\u0631\u0628\u064A\u0629"), TEXT("\u0644\u0627"),
		TEXT("\u65E5\u672C\u8A9E"), TEXT("\u6F22\u5B57\u304B\u306A\u4EEE\u540D"), TEXT("\u3002"), TEXT("\u300C\u5F15\u7528\u300D"),
		TEXT("\u4E2D\u6587\u6BB5\u843D\u6392\u7248"), TEXT("\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22"), TEXT("\u0E2A\u0E27\u0E31\u0E2A\u0E14\u0E35"),
		TEXT("\uD55C\uAD6D\uC5B4"), TEXT("\uBB38\uC7A5\uC744"), TEXT("\u1797\u17B6\u179F\u17B6\u1781\u17D2\u1798\u17C2\u179A"),
		TEXT("\u1780\u1798\u17D2\u1796\u17BB\u1787\u17B6"),
		TEXT("\U0001F44D\U0001F3FD"), TEXT("\U0001F468\u200D\U0001F469\u200D\U0001F467"), TEXT("\U0001F1EF\U0001F1F5"), TEXT("1\uFE0F\u20E3"),
		TEXT("\u2764\uFE0F"), TEXT("\U0001F600"), TEXT("e\u0301te\u0301"), TEXT("\u0915\u094D\u0937\u0924\u094D\u0930"),
	};
	const TCHAR* const FuzzSpaces[] = { TEXT(" "), TEXT(" "), TEXT(" "), TEXT("  "), TEXT("\t"), TEXT("\n"), TEXT("\r\n") };
	const TCHAR* const FuzzTags[] =
	{
		TEXT("<b>"), TEXT("</b>"), TEXT("<i>"), TEXT("</i>"), TEXT("<u>"), TEXT("</u>"), TEXT("<s>"), TEXT("</s>"), TEXT("<size=30>"),
		TEXT("</size>"), TEXT("<color=#ff0000>"), TEXT("</color>"), TEXT("<sup>"), TEXT("</sup>"), TEXT("<sub>"), TEXT("</sub>"),
		TEXT("<warn>"), TEXT("</warn>"), TEXT("<a=link>"), TEXT("</a>"), TEXT("<lang=ja>"), TEXT("<lang=zh-Hans>"), TEXT("</lang>"),
		TEXT("<img=Icon/>"), TEXT("&lt;"), TEXT("&amp;"), TEXT("<gradient=Gold>"), TEXT("</gradient>"),
	};

	template <typename T, int32 N>
	const TCHAR* Pick(FRandomStream& Random, T (&Array)[N])
	{
		return Array[Random.RandRange(0, N - 1)];
	}

	/** A run of words, spaces and (for rich text) tags. */
	FString MakePiece(FRandomStream& Random, bool bRich, int32 Words)
	{
		FString Piece;
		for (int32 k = 0; k < Words; k++)
		{
			if (bRich && Random.RandRange(0, 5) == 0)
			{
				Piece += Pick(Random, FuzzTags);
			}
			Piece += Pick(Random, FuzzWords);
			Piece += Random.RandRange(0, 9) == 0 ? Pick(Random, FuzzSpaces) : TEXT(" ");
		}
		return Piece;
	}

	bool IsLowSurrogate(TCHAR C)
	{
		return C >= 0xDC00 && C <= 0xDFFF;
	}

	/** Position moved back off the second half of a surrogate pair, so that nothing cuts a character in two. */
	int32 SnapPosition(const FString& Text, int32 Position)
	{
		Position = FMath::Clamp(Position, 0, Text.Len());
		if (Position > 0 && Position < Text.Len() && IsLowSurrogate(Text[Position]))
		{
			Position--;
		}
		return Position;
	}

	/** A position in Text that does not fall inside a surrogate pair: anywhere, or its end. */
	int32 RandomPosition(FRandomStream& Random, const FString& Text, bool bAtEnd = false)
	{
		return SnapPosition(Text, bAtEnd ? Text.Len() : Random.RandRange(0, Text.Len()));
	}

	/** One random edit, as someone at a keyboard makes them: typing, deleting, replacing, pasting, splitting and joining paragraphs, undoing. */
	FString Edit(FRandomStream& Random, const FString& Text, const TArray<FString>& History, bool bRich)
	{
		const int32 Kind = Text.Len() > 1500 ? Random.RandRange(25, 64) : Random.RandRange(0, 99);
		FString Result = Text;
		if (Kind < 25)
		{
			// A keystroke, at the end half the time.
			const TCHAR* const Keys[] = { TEXT("a"), TEXT("e"), TEXT("t"), TEXT(" "), TEXT("\u0645"), TEXT("\u6587"), TEXT("\u0E01"), TEXT("."), TEXT("\t") };
			Result.InsertAt(RandomPosition(Random, Text, Random.RandRange(0, 1) == 0), Pick(Random, Keys));
		}
		else if (Kind < 40)
		{
			// A backspace: one code point, both halves of a surrogate pair.
			const int32 Position = RandomPosition(Random, Text);
			if (Position > 0)
			{
				const int32 Units = Position >= 2 && IsLowSurrogate(Text[Position - 1]) ? 2 : 1;
				Result.RemoveAt(Position - Units, Units);
			}
		}
		else if (Kind < 55)
		{
			Result.InsertAt(RandomPosition(Random, Text), MakePiece(Random, bRich, Random.RandRange(1, 3)));
		}
		else if (Kind < 65)
		{
			const int32 Start = RandomPosition(Random, Text);
			const int32 End = SnapPosition(Text, Start + Random.RandRange(1, 24));
			if (End > Start)
			{
				Result.RemoveAt(Start, End - Start);
			}
		}
		else if (Kind < 75)
		{
			const int32 Start = RandomPosition(Random, Text);
			const int32 End = FMath::Max(SnapPosition(Text, Start + Random.RandRange(1, 12)), Start);
			Result = Text.Mid(0, Start) + MakePiece(Random, bRich, Random.RandRange(1, 2)) + Text.Mid(End);
		}
		else if (Kind < 83)
		{
			// A paste of a few lines.
			FString Paste = MakePiece(Random, bRich, Random.RandRange(2, 6)) + TEXT("\n") + MakePiece(Random, bRich, Random.RandRange(2, 6));
			Result.InsertAt(RandomPosition(Random, Text), Paste);
		}
		else if (Kind < 92)
		{
			// Enter, or a backspace at a line's start that joins two paragraphs.
			int32 Newline = INDEX_NONE;
			if (Random.RandRange(0, 1) == 0 && Text.FindChar(TEXT('\n'), Newline))
			{
				const int32 Count = Text.Len();
				const int32 From = Random.RandRange(0, Count - 1);
				Newline = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (Newline == INDEX_NONE)
				{
					Text.FindChar(TEXT('\n'), Newline);
				}
				Result.RemoveAt(Newline, 1);
			}
			else
			{
				Result.InsertAt(RandomPosition(Random, Text), TEXT("\n"));
			}
		}
		else if (History.Num() > 0)
		{
			// Undo: back to what the text was a few edits ago.
			Result = History[FMath::Max(0, History.Num() - Random.RandRange(1, 3))];
		}
		return Result;
	}

	/**
	 * What an edit next to a boundary types, one code point at a time: combining marks, the parts of emoji sequences (a joiner,
	 * a presentation selector, a skin tone, the halves of a flag, a keycap's mark), Hangul jamo, Thai and Khmer letters and
	 * signs, a first strong right-to-left letter, an ideograph, and the characters rich-text markup is made of.
	 */
	const TCHAR* const BoundaryCodepoints[] =
	{
		TEXT("\u0301"), TEXT("\u0308"), TEXT("\u200D"), TEXT("\uFE0F"), TEXT("\U0001F3FD"), TEXT("\U0001F468"), TEXT("\U0001F469"),
		TEXT("\U0001F1EF"), TEXT("\U0001F1F5"), TEXT("\u20E3"), TEXT("1"), TEXT("\u1100"), TEXT("\u1161"), TEXT("\u11A8"),
		TEXT("\u0E01"), TEXT("\u0E31"), TEXT("\u1798"), TEXT("\u17D2"), TEXT("\u05D0"), TEXT("\u0645"), TEXT("\u65E5"), TEXT("a"),
		TEXT(","), TEXT("&"), TEXT("<"), TEXT(">"),
	};

	/**
	 * One edit where layout is easiest to get wrong: a code point typed at a boundary it may join or move -- after a letter, a
	 * mark or an emoji part, between a CR and its LF, after a Hangul, Thai, Khmer or CJK character, next to a tab, right after
	 * or before rich-text markup, before a character reference or an image -- or one code point deleted there.
	 */
	FString BoundaryEdit(FRandomStream& Random, const FString& Text, bool bRich)
	{
		TArray<int32> Places;
		for (int32 i = 0; i <= Text.Len(); i++)
		{
			const TCHAR Before = i > 0 ? Text[i - 1] : 0;
			const TCHAR After = i < Text.Len() ? Text[i] : 0;
			if (IsLowSurrogate(After))
			{
				continue;
			}
			const bool bAtMarkup = bRich && (Before == '>' || After == '<' || After == '&' || (i + 3 <= Text.Len() && Text.Mid(i, 3) == TEXT("lt;")));
			const bool bAtTab = Before == '\t' || After == '\t';
			const bool bInCrLf = Before == '\r' && After == '\n';
			const bool bAfterOther = Before >= 0x0300;
			if (bAtMarkup || bAtTab || bInCrLf || bAfterOther || (Before != 0 && FChar::IsAlpha(Before)))
			{
				Places.Add(i);
			}
		}
		const int32 At = Places.Num() > 0 ? Places[Random.RandRange(0, Places.Num() - 1)] : Text.Len();
		FString Result = Text;
		if (At > 0 && Random.RandRange(0, 4) == 0)
		{
			const int32 Units = At >= 2 && IsLowSurrogate(Text[At - 1]) ? 2 : 1;
			Result.RemoveAt(At - Units, Units);
			return Result;
		}
		Result.InsertAt(At, Pick(Random, BoundaryCodepoints));
		return Result;
	}

	/**
	 * Seeded edits on a text laid out through one state, each compared with a layout from nothing (LaysOutAsFresh), the fresh
	 * layout made every other edit with the shape cache off. A third of the edits are made at a boundary (BoundaryEdit). Now
	 * and then the box changes width, which keeps every paragraph's measurement and breaks and places every line again.
	 */
	bool RunEditFuzz(FAutomationTestBase& Test, const FDreamTextLayoutInput& Base, int32 Seed, int32 Edits, const FString& What,
		FDreamTextLayoutStats& OutStats)
	{
		FRandomStream Random(Seed);
		FDreamTextLayoutInput In = Base;
		const bool bRich = Base.bRichText;
		In.Content.Reset();
		const int32 Paragraphs = Random.RandRange(3, 6);
		for (int32 p = 0; p < Paragraphs; p++)
		{
			In.Content += MakePiece(Random, bRich, Random.RandRange(3, 14));
			if (p + 1 < Paragraphs)
			{
				In.Content += TEXT("\n");
			}
		}
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		TArray<FString> History;
		FDreamTextLayoutEngine::ResetStats();
		for (int32 Step = 0; Step <= Edits; Step++)
		{
			if (Step > 0)
			{
				History.Add(In.Content);
				In.Content = Random.RandRange(0, 2) == 0 ? BoundaryEdit(Random, In.Content, bRich) : Edit(Random, In.Content, History, bRich);
				if (Random.RandRange(0, 19) == 0)
				{
					In.Width = Base.Width * Random.FRandRange(0.6f, 1.4f);
				}
			}
			if (!LaysOutAsFresh(Test, In, State, Kept, FString::Printf(TEXT("%s, seed %d, edit %d"), *What, Seed, Step), Step % 2 == 1))
			{
				return false;
			}
		}
		OutStats = FDreamTextLayoutEngine::GetStats();
		return true;
	}

	/** Words of English, seeded: a paragraph of about Length characters. */
	FString MakeEnglish(int32 Seed, int32 Length)
	{
		const TCHAR* const Words[] = { TEXT("the"), TEXT("of"), TEXT("layout"), TEXT("text"), TEXT("which"), TEXT("paragraph"), TEXT("measure"),
			TEXT("glyph"), TEXT("quick"), TEXT("brown"), TEXT("fox"), TEXT("jumps"), TEXT("over"), TEXT("lazy"), TEXT("dog"), TEXT("and"), TEXT("a"),
			TEXT("line"), TEXT("break"), TEXT("kept"), TEXT("edit"), TEXT("typed"), TEXT("again"), TEXT("never"), TEXT("before") };
		FRandomStream Random(Seed);
		FString Text;
		while (Text.Len() < Length)
		{
			Text += Words[Random.RandRange(0, (int32)UE_ARRAY_COUNT(Words) - 1)];
			Text += TEXT(" ");
		}
		return Text.Left(Length);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutFuzzMockTest,
	"DreamGUI.Text.IncrementalLayout.EveryEditLaysOutAsALayoutFromNothingWouldOnTheMockFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutFuzzMockTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(FDreamTextLayoutInput&)> Setup;
	};
	const FCase Cases[] =
	{
		{ TEXT("plain text, wrapped"), [](FDreamTextLayoutInput& In) {} },
		{ TEXT("rich text, wrapped and justified, the last line too, middle-aligned"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Justify;
				In.LastLineAlign = EDreamTextLastLineAlign::Justify;
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Middle;
			} },
		{ TEXT("rich text on one line a paragraph, centred, letter-spaced"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
				In.FontSpace = FVector2f(2.0f, 3.0f);
			} },
		{ TEXT("rich text, wrapped, right-aligned, inter-character justification, tab size 4, negative letter spacing"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Right;
				In.TabSize = 4.0f;
				In.FontSpace = FVector2f(-1.0f, 0.0f);
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
				In.Pivot = FVector2f(0.2f, 0.7f);
			} },
		{ TEXT("plain text, wrapped under an ellipsis in a short box"), [](FDreamTextLayoutInput& In)
			{
				In.OverflowType = EDreamUITextOverflowType::Ellipsis;
				In.bAutoWrapText = true;
				In.Height = 160.0f;
			} },
		{ TEXT("rich text, a line each, middle ellipsis"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.OverflowType = EDreamUITextOverflowType::MiddleEllipsis;
			} },
	};
	int64 Reused = 0;
	int64 LinesReused = 0;
	int64 Spliced = 0;
	for (const FIncrementalSwitches& Switches : SwitchMatrix)
	{
		FScopedIncrementalSwitches Scoped(Switches);
		// Every switch on runs the whole fuzz; each of them off on its own, a third of it.
		const bool bAllOn = Switches.Parse == 1 && Switches.Measure == 1 && Switches.InPlace == 1;
		for (int32 c = 0; c < (int32)UE_ARRAY_COUNT(Cases); c++)
		{
			FDreamTextLayoutInput In = MakeInput(Font, FString());
			Cases[c].Setup(In);
			FDreamTextLayoutStats Stats;
			if (!RunEditFuzz(*this, In, 1000 + c, bAllOn ? 150 : 50, FString::Printf(TEXT("%s (%s)"), Cases[c].Name, Switches.Name), Stats))
			{
				continue;
			}
			Reused += Stats.ParagraphsReused + Stats.ParagraphsPositional;
			LinesReused += Stats.LinesReused;
			Spliced += Stats.ElementsSpliced;
		}
	}
	// Equality alone would hold for a layout that never took anything from what it kept.
	TestTrue(TEXT("The edits took paragraphs from the kept layout"), Reused > 0);
	TestTrue(TEXT("and lines' placements"), LinesReused > 0);
	TestTrue(TEXT("and spliced edits into what it kept"), Spliced > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutFuzzShapedTest,
	"DreamGUI.Text.IncrementalLayout.EveryEditLaysOutAsALayoutFromNothingWouldOnShapedFonts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutFuzzShapedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	UDreamUIFontData_DistanceField* Font = MakeMixedScriptFont(TestWorld.World);

	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(FDreamTextLayoutInput&)> Setup;
	};
	const FCase Cases[] =
	{
		{ TEXT("rich text, wrapped, ligatures on"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.bAllowLigatures = true;
			} },
		{ TEXT("plain text, wrapped, justified between characters, phrase wrap"), [](FDreamTextLayoutInput& In)
			{
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Justify;
				In.TextJustify = EDreamTextJustify::InterCharacter;
				In.PhraseWrap = EDreamTextPhraseWrap::CJKDictionary;
			} },
		{ TEXT("rich text, wrapped, right to left, centred"), [](FDreamTextLayoutInput& In)
			{
				In.bRichText = true;
				In.FlowDirection = EDreamTextFlowDirection::RightToLeft;
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
				In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Bottom;
			} },
		{ TEXT("plain text, a line each, Japanese, letter-spaced"), [](FDreamTextLayoutInput& In)
			{
				In.OverflowType = EDreamUITextOverflowType::HorizontalOverflow;
				In.Language = TEXT("ja");
				In.FontSpace = FVector2f(1.5f, 0.0f);
			} },
		{ TEXT("plain text, wrapped, in Korean, Hangul kept whole per word"), [](FDreamTextLayoutInput& In)
			{
				In.Language = TEXT("ko");
				In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
			} },
	};
	int64 Reused = 0;
	int64 LinesReused = 0;
	int64 Spliced = 0;
	for (const FIncrementalSwitches& Switches : SwitchMatrix)
	{
		FScopedIncrementalSwitches Scoped(Switches);
		// Every switch on runs the whole fuzz; each of them off on its own, a third of it.
		const bool bAllOn = Switches.Parse == 1 && Switches.Measure == 1 && Switches.InPlace == 1;
		for (int32 c = 0; c < (int32)UE_ARRAY_COUNT(Cases); c++)
		{
			FDreamTextLayoutInput In = MakeInput(Font, FString());
			Cases[c].Setup(In);
			FDreamTextLayoutStats Stats;
			if (!RunEditFuzz(*this, In, 2000 + c, bAllOn ? 100 : 33, FString::Printf(TEXT("%s (%s)"), Cases[c].Name, Switches.Name), Stats))
			{
				continue;
			}
			Reused += Stats.ParagraphsReused + Stats.ParagraphsPositional;
			LinesReused += Stats.LinesReused;
			Spliced += Stats.ElementsSpliced;
		}
	}
	// Equality alone would hold for a layout that never took anything from what it kept.
	TestTrue(TEXT("The edits took paragraphs from the kept layout"), Reused > 0);
	TestTrue(TEXT("and lines' placements"), LinesReused > 0);
	TestTrue(TEXT("and spliced edits into what it kept"), Spliced > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutKeystrokeTest,
	"DreamGUI.Text.IncrementalLayout.AKeystrokeAtTheEndOfALongParagraphShapesAndPlacesAtMostTwoOfEach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutKeystrokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(7, 2000), 600.0f);
	FDreamTextLayoutState State;
	FDreamTextDisplayList Kept;
	if (!LaysOutAsFresh(*this, In, State, Kept, TEXT("The paragraph as it is")))
	{
		return false;
	}
	TestTrue(TEXT("The paragraph wraps into thirty lines or more"), Kept.Lines.Num() >= 30);
	const TCHAR* const Keys = TEXT("text, typed on");
	for (int32 Key = 0; Keys[Key] != 0; Key++)
	{
		In.Content.AppendChar(Keys[Key]);
		FDreamTextShapeCache::ResetStats();
		FDreamTextLayoutEngine::ResetStats();
		FDreamTextLayoutEngine::Layout(In, Kept, &State);
		const FDreamTextShapeCache::FStats Shaped = FDreamTextShapeCache::GetStats();
		const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
		const FString What = FString::Printf(TEXT("Keystroke %d ('%c')"), Key, Keys[Key]);
		TestEqual(What + TEXT(" built on the kept layout"), Laid.IncrementalLayouts, (int64)1);
		TestTrue(What + FString::Printf(TEXT(" made at most 2 hb_shape calls (%lld)"), Shaped.ShapeCalls), Shaped.ShapeCalls <= 2);
		TestTrue(What + FString::Printf(TEXT(" placed at most 2 lines (%lld)"), Laid.LinesPlaced), Laid.LinesPlaced <= 2);
		TestEqual(What + TEXT(": every other line kept its placement"), Laid.LinesPlaced + Laid.LinesReused, (int64)Kept.Lines.Num());
		TestEqual(What + TEXT(" measured the one paragraph"), Laid.ParagraphsMeasured, (int64)1);
		TestTrue(What + FString::Printf(TEXT(" asked ICU about the end of the paragraph only (%lld code units)"), Laid.IcuCodeUnits),
			Laid.IcuCodeUnits < 200);
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Kept, Fresh);
		TestTrue(What + TEXT(" laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutOneOfTwentyTest,
	"DreamGUI.Text.IncrementalLayout.TypingInOneOfTwentyParagraphsMeasuresThatParagraphAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutOneOfTwentyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Mock = NewObject<UDreamTextTestFont>(TestWorld.World);
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	FString Content;
	for (int32 p = 0; p < 20; p++)
	{
		Content += MakeEnglish(100 + p, 100);
		if (p < 19)
		{
			Content += TEXT("\n");
		}
	}
	for (UDreamUIFontData_BaseObject* Font : { (UDreamUIFontData_BaseObject*)Mock, (UDreamUIFontData_BaseObject*)Roboto })
	{
		const FString Name = Font == Mock ? TEXT("The mock font") : TEXT("Roboto");
		FDreamTextLayoutInput In = MakeInput(Font, Content, 500.0f);
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		LaysOutAsFresh(*this, In, State, Kept, Name + TEXT(", before the edit"));
		// A word typed into the eighth paragraph, at its middle.
		int32 Position = 0;
		for (int32 p = 0; p < 7; p++)
		{
			Position = In.Content.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Position) + 1;
		}
		Position += 50;
		for (int32 Key = 0; Key < 4; Key++)
		{
			In.Content.InsertAt(Position + Key, TEXT("word")[Key]);
			FDreamTextLayoutEngine::ResetStats();
			FDreamTextLayoutEngine::Layout(In, Kept, &State);
			const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
			const FString What = Name + FString::Printf(TEXT(", keystroke %d"), Key);
			TestEqual(What + TEXT(": one paragraph measured"), Laid.ParagraphsMeasured, (int64)1);
			TestEqual(What + TEXT(": nineteen taken as they were, where they stood or found by their content"),
				Laid.ParagraphsReused + Laid.ParagraphsPositional, (int64)19);
			TestTrue(What + FString::Printf(TEXT(": only the edited paragraph's lines placed again (%lld)"), Laid.LinesPlaced), Laid.LinesPlaced <= 5);
			TestEqual(What + TEXT(": every other line kept its placement"), Laid.LinesPlaced + Laid.LinesReused, (int64)Kept.Lines.Num());
			FDreamTextDisplayList Fresh;
			FDreamTextLayoutEngine::Layout(In, Fresh);
			const FString Difference = FirstDifference(Kept, Fresh);
			TestTrue(What + TEXT(": laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutInvalidationTest,
	"DreamGUI.Text.IncrementalLayout.FallbacksAnAtlasFlushALanguageOrTheSwitchMakeTheNextLayoutStartFromNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutInvalidationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	Roboto->SetFallbackFonts({ Arabic, Droid });

	FDreamTextLayoutInput In = MakeInput(Roboto, TEXT("A first paragraph \u65E5\u672C\u8A9E and \u0645\u0631\u062D\u0628\u0627.\nA second one, 12345.\nA third."), 300.0f);
	FDreamTextLayoutState State;
	FDreamTextDisplayList Kept;
	// Lays out with the state and says whether it built on what was kept; the result is held against a fresh layout.
	auto LayOut = [&](const TCHAR* What)
	{
		FDreamTextLayoutEngine::ResetStats();
		FDreamTextLayoutEngine::Layout(In, Kept, &State);
		const bool bBuiltOn = FDreamTextLayoutEngine::GetStats().IncrementalLayouts > 0;
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Kept, Fresh);
		TestTrue(FString(What) + TEXT(": laid out as a layout from nothing would: ") + Difference, Difference.IsEmpty());
		return bBuiltOn;
	};

	LayOut(TEXT("The first layout"));
	TestTrue(TEXT("The same text again builds on what was kept"), LayOut(TEXT("The same text again")));

	Roboto->SetFallbackFonts({ Droid, Arabic });
	TestFalse(TEXT("Fallbacks swapped: the kept layout is not built on"), LayOut(TEXT("Fallbacks swapped")));
	TestTrue(TEXT("and the layout after it builds on that one"), LayOut(TEXT("After the swap")));

	FDreamUIFontFallback Japanese;
	Japanese.Font = Droid;
	Japanese.Cultures = TEXT("ja");
	FDreamUIFontFallback Arab;
	Arab.Font = Arabic;
	Roboto->SetFallbacks({ Arab, Japanese });
	TestFalse(TEXT("SetFallbacks: the kept layout is not built on"), LayOut(TEXT("SetFallbacks")));
	LayOut(TEXT("After SetFallbacks"));

	Roboto->FlushGlyphAtlas();
	TestFalse(TEXT("The atlas flushed: the kept layout, whose quads pointed into the old atlas, is not built on"), LayOut(TEXT("Atlas flushed")));
	LayOut(TEXT("After the flush"));

	In.Language = TEXT("ja");
	TestFalse(TEXT("Another language: the kept layout is not built on"), LayOut(TEXT("Language ja")));
	In.Content += TEXT(" More.");
	TestTrue(TEXT("An edit after it builds on the layout in the new language"), LayOut(TEXT("An edit in ja")));

	{
		FScopedConsoleInt Off(TEXT("DreamGUI.Text.IncrementalLayout"), 0);
		In.Content += TEXT(" And more.");
		TestFalse(TEXT("DreamGUI.Text.IncrementalLayout 0: nothing is built on"), LayOut(TEXT("Switched off")));
		TestFalse(TEXT("and nothing is kept"), State.HasLayout());
	}
	In.Content += TEXT(" Then some.");
	TestFalse(TEXT("Switched on again: the first layout has nothing to build on"), LayOut(TEXT("Switched on again")));
	TestTrue(TEXT("and keeps one"), State.HasLayout());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutCacheTest,
	"DreamGUI.Text.IncrementalLayout.TheGeometryCacheKeepsALayoutWhenAskedOrWhenALongTextChangesTwiceInARow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIncrementalLayoutCacheTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);

	// Asked for: from the first layout on.
	{
		FDreamUITextGeometryCache Cache;
		Cache.SetIncrementalLayout(true);
		FDreamTextLayoutInput In = MakeInput(Font, TEXT("A short field\nbeing typed into"));
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestTrue(TEXT("A text asked to keeps its layout"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("!");
		Cache.SetLayoutInput(In);
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("and the next layout builds on it"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)1);
		Cache.MarkDirty();
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("MarkDirty drops what was kept"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)0);
		FDreamTextDisplayList Fresh;
		FDreamTextLayoutEngine::Layout(In, Fresh);
		const FString Difference = FirstDifference(Cache.GetDisplayList(), Fresh);
		TestTrue(TEXT("The cache's display list is a fresh layout's: ") + Difference, Difference.IsEmpty());
		Cache.SetIncrementalLayout(false);
		In.Content += TEXT("?");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("Asked no more, a short text lets go of it"), Cache.IsIncrementalLayoutActive());
	}

	// By itself: a long text whose content changed in two layouts in a row.
	{
		FDreamUITextGeometryCache Cache;
		FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(3, 300));
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("A long text laid out once keeps nothing"), Cache.IsIncrementalLayoutActive());
		In.Width += 20.0f;
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("nor laid out again at another width"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("a");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("nor after one edit"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("b");
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestTrue(TEXT("but after a second edit in a row it keeps its layout"), Cache.IsIncrementalLayoutActive());
		In.Content += TEXT("c");
		Cache.SetLayoutInput(In);
		FDreamTextLayoutEngine::ResetStats();
		Cache.EnsureLayout();
		TestEqual(TEXT("and the next edit builds on it"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)1);
		In.Content = MakeEnglish(3, 100);
		Cache.SetLayoutInput(In);
		Cache.EnsureLayout();
		TestFalse(TEXT("Cut under 256 elements, it lets go of it"), Cache.IsIncrementalLayoutActive());
	}

	// A short text edited over and over never keeps one.
	{
		FDreamUITextGeometryCache Cache;
		FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(4, 100));
		for (int32 Edit = 0; Edit < 5; Edit++)
		{
			In.Content += TEXT("x");
			Cache.SetLayoutInput(In);
			Cache.EnsureLayout();
		}
		TestFalse(TEXT("A short text edited five times keeps nothing"), Cache.IsIncrementalLayoutActive());
	}

	// The switch turns the asked-for kind off too.
	{
		FDreamUITextGeometryCache Cache;
		Cache.SetIncrementalLayout(true);
		FScopedConsoleInt Off(TEXT("DreamGUI.Text.IncrementalLayout"), 0);
		Cache.SetLayoutInput(MakeInput(Font, TEXT("Typed into, switched off")));
		Cache.EnsureLayout();
		TestFalse(TEXT("With DreamGUI.Text.IncrementalLayout 0 even a text that asks keeps nothing"), Cache.IsIncrementalLayoutActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutWindowTest,
	"DreamGUI.Text.IncrementalLayout.AKeystrokeReadsItsWindowAloneAndTakesEveryOtherParagraphWhereItStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What a keystroke in one of twenty paragraphs costs under each setting of the switches. With them all on it is seen from
 * the text: the elements around the edit are read again (no more than four), the typed one spliced in, the nineteen
 * paragraphs it did not touch taken where they stand without a hash, and the display list edited where it is, nothing
 * copied. Each switch off falls back to what round-4 layouts did for its part.
 */
bool FDreamTextIncrementalLayoutWindowTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	FString Content;
	for (int32 p = 0; p < 20; p++)
	{
		Content += MakeEnglish(200 + p, 100);
		if (p < 19)
		{
			Content += TEXT("\n");
		}
	}
	for (const FIncrementalSwitches& Switches : SwitchMatrix)
	{
		FScopedIncrementalSwitches Scoped(Switches);
		// A long paragraph is seen from the text only when it can be measured through a window.
		const bool bFromTheText = Switches.Parse == 1 && Switches.Measure == 1;
		FDreamTextLayoutInput In = MakeInput(Font, Content, 500.0f);
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		if (!LaysOutAsFresh(*this, In, State, Kept, FString(Switches.Name) + TEXT(", before the edit")))
		{
			continue;
		}
		// The end of the eighth paragraph, before its newline.
		int32 Position = 0;
		for (int32 p = 0; p < 8; p++)
		{
			Position = In.Content.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Position) + 1;
		}
		const int32 EndOfEighth = Position - 1;
		for (int32 Key = 0; Key < 3; Key++)
		{
			In.Content.InsertAt(EndOfEighth + Key, TEXT("abc")[Key]);
			FDreamTextLayoutEngine::ResetStats();
			FDreamTextLayoutEngine::Layout(In, Kept, &State);
			const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
			const FString What = FString::Printf(TEXT("%s, keystroke %d"), Switches.Name, Key);
			TestEqual(What + TEXT(": built on the kept layout"), Laid.IncrementalLayouts, (int64)1);
			TestEqual(What + TEXT(": measured the one paragraph"), Laid.ParagraphsMeasured, (int64)1);
			if (bFromTheText)
			{
				TestTrue(What + FString::Printf(TEXT(": read at most four elements again (%lld)"), Laid.ElementsParsed), Laid.ElementsParsed <= 4);
				TestEqual(What + TEXT(": spliced in the one typed"), Laid.ElementsSpliced, (int64)1);
				TestEqual(What + TEXT(": took the nineteen others where they stood"), Laid.ParagraphsPositional, (int64)19);
				TestEqual(What + TEXT(": hashed none"), Laid.ParagraphsHashed, (int64)0);
			}
			else
			{
				TestEqual(What + TEXT(": read every element"), Laid.ElementsParsed, (int64)Kept.ElementCount);
				TestEqual(What + TEXT(": found the nineteen others by their content"), Laid.ParagraphsReused, (int64)19);
			}
			if (Switches.InPlace == 1)
			{
				TestEqual(What + TEXT(": copied no item"), Laid.ItemsCopied, (int64)0);
			}
			else
			{
				TestTrue(What + TEXT(": copied the kept lines"), Laid.ItemsCopied > 0);
			}
			TestEqual(What + TEXT(": every line placed or kept"), Laid.LinesPlaced + Laid.LinesReused, (int64)Kept.Lines.Num());
			MatchesFresh(*this, In, State, Kept, What);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutStateOracleTest,
	"DreamGUI.Text.IncrementalLayout.TheStateComparisonFindsAKeptLayoutAFreshOneAndTellsTwoTextsApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The oracle the fuzz relies on: FDreamTextLayoutEngine::DebugCompareStates says a state an edit was built into holds what
 * a layout from nothing keeps, and says two different layouts differ, and where.
 */
bool FDreamTextIncrementalLayoutStateOracleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(21, 300) + TEXT("\n") + MakeEnglish(22, 120), 300.0f);
	FDreamTextLayoutState Kept;
	FDreamTextDisplayList KeptList;
	FDreamTextLayoutEngine::Layout(In, KeptList, &Kept);
	In.Content.InsertAt(150, TEXT("typed "));
	FDreamTextLayoutEngine::ResetStats();
	FDreamTextLayoutEngine::Layout(In, KeptList, &Kept);
	TestEqual(TEXT("The edit built on the kept layout"), FDreamTextLayoutEngine::GetStats().IncrementalLayouts, (int64)1);

	FDreamTextLayoutState Fresh;
	FDreamTextDisplayList FreshList;
	FDreamTextLayoutEngine::Layout(In, FreshList, &Fresh);
	FString Difference;
	const bool bSameState = FDreamTextLayoutEngine::DebugCompareStates(Kept, Fresh, Difference);
	TestTrue(TEXT("The state the edit was built into is a fresh layout's: ") + Difference, bSameState);
	const bool bSameList = FDreamTextLayoutEngine::DebugCompareDisplayLists(KeptList, FreshList, Difference);
	TestTrue(TEXT("and so is its display list: ") + Difference, bSameList);

	FDreamTextLayoutInput OtherIn = In;
	OtherIn.Content.InsertAt(40, TEXT("x"));
	FDreamTextLayoutState Other;
	FDreamTextDisplayList OtherList;
	FDreamTextLayoutEngine::Layout(OtherIn, OtherList, &Other);
	TestFalse(TEXT("Two texts' states differ"), FDreamTextLayoutEngine::DebugCompareStates(Fresh, Other, Difference));
	TestFalse(TEXT("and the comparison says where"), Difference.IsEmpty());
	TestFalse(TEXT("Their display lists differ"), FDreamTextLayoutEngine::DebugCompareDisplayLists(FreshList, OtherList, Difference));
	TestFalse(TEXT("and that comparison says where too"), Difference.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutVerifyTest,
	"DreamGUI.Text.IncrementalLayout.VerifyIncrementalHoldsEveryLayoutBuiltOnAKeptOneAgainstOneFromNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * DreamGUI.Text.VerifyIncremental 1 lays every layout that built on a kept one out again from nothing and compares the two,
 * display list and state, the second layout's work left out of the counters. Edits that come out right are all verified
 * and none differs.
 */
bool FDreamTextIncrementalLayoutVerifyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	if (!TestNotNull(TEXT("The switch is there outside shipping builds"), IConsoleManager::Get().FindConsoleVariable(TEXT("DreamGUI.Text.VerifyIncremental"))))
	{
		return false;
	}
	FScopedConsoleInt Verify(TEXT("DreamGUI.Text.VerifyIncremental"), 1);
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	FDreamTextLayoutInput In = MakeInput(Font, MakeEnglish(31, 400) + TEXT("\n") + MakeEnglish(32, 200), 300.0f);
	FDreamTextLayoutState State;
	FDreamTextDisplayList List;
	FDreamTextLayoutEngine::ResetStats();
	FDreamTextLayoutEngine::Layout(In, List, &State);
	for (int32 Key = 0; Key < 6; Key++)
	{
		In.Content.InsertAt((Key * 97) % In.Content.Len(), TEXT("x"));
		FDreamTextLayoutEngine::Layout(In, List, &State);
	}
	const FDreamTextLayoutStats Stats = FDreamTextLayoutEngine::GetStats();
	TestEqual(TEXT("Seven layouts counted, the verifying ones not among them"), Stats.Layouts, (int64)7);
	TestEqual(TEXT("Every layout built on a kept one was verified"), Stats.VerifiedLayouts, Stats.IncrementalLayouts);
	TestEqual(TEXT("which is every edit"), Stats.VerifiedLayouts, (int64)6);
	TestEqual(TEXT("and none came out different"), Stats.VerifyMismatches, (int64)0);
	TestTrue(TEXT("What was kept is kept"), State.HasLayout());
	FDreamTextDisplayList Fresh;
	FDreamTextLayoutEngine::Layout(In, Fresh);
	const FString Difference = FirstDifference(List, Fresh);
	TestTrue(TEXT("The last display list is a fresh layout's: ") + Difference, Difference.IsEmpty());
	return true;
}

namespace DreamTextIncrementalLayoutTestLocal
{
	/** A string of code points, given as numbers so this file stays ASCII. */
	FString FromCodepoints(std::initializer_list<uint32> Values)
	{
		FString Text;
		for (const uint32 Value : Values)
		{
			if (Value >= 0x10000)
			{
				Text.AppendChar((TCHAR)(0xD800 + ((Value - 0x10000) >> 10)));
				Text.AppendChar((TCHAR)(0xDC00 + ((Value - 0x10000) & 0x3FF)));
			}
			else
			{
				Text.AppendChar((TCHAR)Value);
			}
		}
		return Text;
	}

	/** One edit of the text: Remove code units taken out at At, then Insert put there. */
	struct FWindowEdit
	{
		int32 At = 0;
		int32 Remove = 0;
		const TCHAR* Insert = TEXT("");
		const TCHAR* What = TEXT("");
	};

	/**
	 * Lays In out once without a state (so every glyph the edits use is in the atlas already), then through a state, then
	 * makes each edit in turn. After each one: the layout built on the kept one, measured the one paragraph, and measured it
	 * through a window -- fewer elements than the paragraph has, shaped through the shape cache -- and what it made, display
	 * list and kept state both, is what a layout from nothing makes (MatchesFresh).
	 */
	void RunWindowEdits(FAutomationTestBase& Test, FDreamTextLayoutInput In, const TArray<FWindowEdit>& Edits, const FString& Name)
	{
		FDreamTextDisplayList Warm;
		FDreamTextLayoutEngine::Layout(In, Warm);
		FDreamTextLayoutState State;
		FDreamTextDisplayList Kept;
		if (!LaysOutAsFresh(Test, In, State, Kept, Name + TEXT(", before the edits")))
		{
			return;
		}
		for (const FWindowEdit& Step : Edits)
		{
			if (Step.Remove > 0)
			{
				In.Content.RemoveAt(Step.At, Step.Remove);
			}
			const FString Inserted(Step.Insert);
			if (!Inserted.IsEmpty())
			{
				In.Content.InsertAt(Step.At, Inserted);
			}
			FDreamTextLayoutEngine::ResetStats();
			FDreamTextShapeCache::ResetStats();
			FDreamTextLayoutEngine::Layout(In, Kept, &State);
			const FDreamTextLayoutStats Laid = FDreamTextLayoutEngine::GetStats();
			const FDreamTextShapeCache::FStats Shaped = FDreamTextShapeCache::GetStats();
			const FString What = Name + TEXT(", ") + Step.What;
			Test.TestEqual(What + TEXT(": built on the kept layout"), Laid.IncrementalLayouts, (int64)1);
			Test.TestEqual(What + TEXT(": measured the one paragraph"), Laid.ParagraphsMeasured, (int64)1);
			Test.TestTrue(What + FString::Printf(TEXT(": measured it through a window (%lld of its %d elements)"), Laid.WindowElements, Kept.ElementCount),
				Laid.WindowElements > 0 && Laid.WindowElements < (int64)Kept.ElementCount);
			Test.TestTrue(What + FString::Printf(TEXT(": shaped the window through the shape cache (%lld lookups)"), Shaped.Lookups), Shaped.Lookups > 0);
			Test.TestTrue(What + TEXT(": kept a layout, which is held against a fresh one's"), State.HasLayout());
			MatchesFresh(Test, In, State, Kept, What);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutLeadingNeutralWindowTest,
	"DreamGUI.Text.IncrementalLayout.AWindowAtTheStartOfAParagraphThatOpensWithPunctuationDigitsOrAnEmojiShapesAsTheWholeParagraphDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A window at its paragraph's start is itemized from Common, as the whole paragraph is, so the neutrals the paragraph opens
 * with ("(nested)", "3.14", an emoji, a keycap) take the first script of their own after them. Seeded with no script at all
 * (HB_SCRIPT_INVALID, which is no neutral) they kept that, ran apart from the word after them, and the kept state's scripts and
 * runs were not a fresh layout's. Each edit is made in the paragraph's first two segments of a long left-to-right paragraph on
 * a shaped font, measured through a window (the counters say so), and held against a layout from nothing: kept state and
 * display list.
 */
bool FDreamTextIncrementalLayoutLeadingNeutralWindowTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	FScopedIncrementalSwitches Switches(SwitchMatrix[0]);
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	struct FOpening
	{
		const TCHAR* Name;
		FString Token;
		/** The token has letters a keystroke inside it can sit next to. */
		bool bLettersInside;
	};
	const FOpening Openings[] =
	{
		{ TEXT("A paragraph opening with \"(nested)\""), FString(TEXT("(nested)")), true },
		{ TEXT("A paragraph opening with \"3.14\""), FString(TEXT("3.14")), false },
		{ TEXT("A paragraph opening with an emoji (U+1F600)"), FromCodepoints({ 0x1F600 }), false },
		{ TEXT("A paragraph opening with a keycap (1 U+FE0F U+20E3)"), FromCodepoints({ '1', 0xFE0F, 0x20E3 }), false },
	};
	for (int32 k = 0; k < (int32)UE_ARRAY_COUNT(Openings); k++)
	{
		const FOpening& Opening = Openings[k];
		// The token, a space, then words: long enough to be measured through a window, and nothing in it right to left.
		const FDreamTextLayoutInput In = MakeInput(Font, Opening.Token + TEXT(" ") + MakeEnglish(41 + k, 300));
		const int32 WordStart = Opening.Token.Len() + 1;
		TArray<FWindowEdit> Edits;
		Edits.Add({ WordStart, 0, TEXT("e"), TEXT("a letter typed at the start of the word after it") });
		Edits.Add({ WordStart, 1, TEXT(""), TEXT("that letter deleted") });
		Edits.Add({ WordStart - 1, 1, TEXT(""), TEXT("the space after it deleted") });
		Edits.Add({ WordStart - 1, 0, TEXT(" "), TEXT("the space typed again") });
		if (Opening.bLettersInside)
		{
			Edits.Add({ 3, 0, TEXT("e"), TEXT("a letter typed inside it") });
			Edits.Add({ 3, 1, TEXT(""), TEXT("that letter deleted") });
		}
		RunWindowEdits(*this, In, Edits, Opening.Name);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIncrementalLayoutWindowAfterImageTest,
	"DreamGUI.Text.IncrementalLayout.AWindowThatStartsAfterAnInlineImageTakesTheScriptFromBeforeTheImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An inline image (rich text's <img>) is never shaped, and the itemizer carries the script before it past it. A window that
 * starts right after one -- an edit in the second segment after the image -- is seeded with that script, the last one of its
 * own before the window, not with the image's (none): seeded with none, the neutrals the window starts with ("(nested)") ran
 * apart from the words after them. Then windows that start on the image itself. Each edit is measured through a window (the
 * counters say so) and held against a layout from nothing: kept state and display list.
 */
bool FDreamTextIncrementalLayoutWindowAfterImageTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextIncrementalLayoutTestLocal;
	FScopedGameWorld TestWorld;
	FScopedConsoleInt Incremental(TEXT("DreamGUI.Text.IncrementalLayout"), 1);
	FScopedConsoleInt ShapeCache(TEXT("DreamGUI.Text.ShapeCache"), 1);
	FScopedIncrementalSwitches Switches(SwitchMatrix[0]);
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));

	FDreamTextLayoutInput In = MakeInput(Font, FString());
	In.bRichText = true;
	In.Content = FString(TEXT("Words before <img=Icon/>(nested) more ")) + MakeEnglish(45, 300);
	const int32 Nested = In.Content.Find(TEXT("(nested) more"), ESearchCase::CaseSensitive);
	if (!TestTrue(TEXT("The text has the words after its image"), Nested != INDEX_NONE))
	{
		return false;
	}
	FDreamTextDisplayList Probe;
	FDreamTextLayoutEngine::Layout(In, Probe);
	if (!TestEqual(TEXT("The text has its inline image"), Probe.Images.Num(), 1))
	{
		return false;
	}
	// "more": the second word after the image. Its first letter is the second segment after "(nested)", which is the one the
	// image is right before.
	const int32 MoreAt = Nested + 9;
	TArray<FWindowEdit> Edits;
	Edits.Add({ MoreAt, 0, TEXT("e"), TEXT("a letter typed at the start of the second word after the image (the window starts right after the image)") });
	Edits.Add({ MoreAt, 1, TEXT(""), TEXT("that letter deleted (the window starts right after the image)") });
	Edits.Add({ MoreAt - 1, 1, TEXT(""), TEXT("the space before that word deleted (the window starts on the image)") });
	Edits.Add({ MoreAt - 1, 0, TEXT(" "), TEXT("the space typed again (the window starts on the image)") });
	RunWindowEdits(*this, In, Edits, TEXT("Rich text with an inline image"));
	return true;
}

#endif
