// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextShaper.h"
#include "Engine/World.h"
#include "DreamTextTestFont.h"
#include "DreamScopedWorld.h"

/*
 * Justification (ParagraphHAlign Justify, TextJustify, LastLineAlign) at the display-list level. A justified line gets the
 * room it lacks of its target width spread evenly over its opportunities, in the same Spacing slot letter spacing uses,
 * so what follows spacing -- strokes, carets, selection runs -- follows the widened room too. The mock font has no
 * shaping, so its lines read left to right; a right-to-left line is checked against the engine's Arabic font.
 */
namespace DreamTextJustifyTestLocal
{
	using DreamTests::FScopedGameWorld;

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, float Width, float Height = 600.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = Height;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Justify;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
		In.Font = Font;
		return In;
	}

	float BoxLeft(const FDreamTextLayoutInput& In) { return In.Width * (0.5f - In.Pivot.X) - In.Width * 0.5f; }
	float BoxRight(const FDreamTextLayoutInput& In) { return BoxLeft(In) + In.Width; }

	/** Where an item's pen box starts and ends: what an underline under it covers, its share of the line's width included. */
	float PenBoxLeft(const FDreamTextGlyphItem& Item) { return Item.Pen.X + Item.DecorationOffset; }
	float PenBoxRight(const FDreamTextGlyphItem& Item) { return PenBoxLeft(Item) + Item.AdvanceWithSpace; }

	/** A line's extent from its items: every glyph and space, the whitespace hanging off its end left out. */
	bool LineExtent(const FDreamTextDisplayList& DL, int32 LineIndex, float& OutLeft, float& OutRight)
	{
		OutLeft = MAX_FLT;
		OutRight = -MAX_FLT;
		int32 LastContent = INDEX_NONE;
		for (int32 i = 0; i < DL.Items.Num(); i++)
		{
			const FDreamTextGlyphItem& Item = DL.Items[i];
			if (Item.LineIndex == LineIndex && Item.Kind != EDreamTextItemKind::Space)
			{
				LastContent = FMath::Max(LastContent, Item.ElementIndex);
			}
		}
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.LineIndex != LineIndex || Item.ElementIndex > LastContent)continue;
			OutLeft = FMath::Min(OutLeft, PenBoxLeft(Item));
			OutRight = FMath::Max(OutRight, PenBoxRight(Item));
		}
		return LastContent != INDEX_NONE;
	}

	/** Whether a line has a space between two of its words: somewhere a justified line can widen (the one hanging off its end is not). */
	bool HasInnerSpace(const FDreamTextDisplayList& DL, int32 LineIndex)
	{
		int32 LastContent = INDEX_NONE;
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.LineIndex == LineIndex && Item.Kind != EDreamTextItemKind::Space)
			{
				LastContent = FMath::Max(LastContent, Item.ElementIndex);
			}
		}
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.LineIndex == LineIndex && Item.Kind == EDreamTextItemKind::Space && Item.ElementIndex < LastContent)
			{
				return true;
			}
		}
		return false;
	}

	/** The item an element was laid out as, glyph or space; null when there is none. */
	const FDreamTextGlyphItem* FindElement(const FDreamTextDisplayList& DL, int32 ElementIndex)
	{
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.ElementIndex == ElementIndex && (Item.Kind == EDreamTextItemKind::Glyph || Item.Kind == EDreamTextItemKind::Space))
			{
				return &Item;
			}
		}
		return nullptr;
	}

	/** The room an item's cluster carries after it: its share of the pen box beyond its own advance. */
	float SpacingOf(const FDreamTextGlyphItem& Item) { return Item.AdvanceWithSpace - Item.Glyph.XAdvance; }

	const TCHAR* const Sentence = TEXT("The quick brown fox jumps over the lazy dog and keeps running through the field");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyWidthTest,
	"DreamGUI.Text.Justify.EveryWrappedLineReachesTheTargetWidthAndTheLastStartsAtItsStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextJustifyWidthTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	FDreamTextLayoutInput In = MakeInput(Font, Sentence, 200.0f);
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestTrue(TEXT("the sentence wraps"), DL.Lines.Num() > 2))return false;
	int32 Widened = 0;
	for (int32 Line = 0; Line + 1 < DL.Lines.Num(); Line++)
	{
		float Left = 0.0f, Right = 0.0f;
		if (!TestTrue(*FString::Printf(TEXT("line %d has content"), Line), LineExtent(DL, Line, Left, Right)))continue;
		TestEqual(*FString::Printf(TEXT("line %d starts on the box's left edge"), Line), Left, BoxLeft(In), 0.01f);
		// A line of one word ("running" at this width) has nowhere to widen and keeps its own width, as in CSS.
		if (HasInnerSpace(DL, Line))
		{
			TestEqual(*FString::Printf(TEXT("line %d reaches its right edge"), Line), Right, BoxRight(In), 0.01f);
			Widened++;
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("line %d, one word, is not widened"), Line), Right < BoxRight(In) - 1.0f);
		}
	}
	TestTrue(TEXT("the lines with spaces between their words were justified"), Widened >= 2);
	// The last line is set as a left-aligned one is.
	FDreamTextLayoutInput LeftAligned = In;
	LeftAligned.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	FDreamTextDisplayList Natural;
	FDreamTextLayoutEngine::Layout(LeftAligned, Natural);
	float LastLeft = 0.0f, LastRight = 0.0f, NaturalLeft = 0.0f, NaturalRight = 0.0f;
	if (TestEqual(TEXT("justifying moves no line break"), Natural.Lines.Num(), DL.Lines.Num())
		&& LineExtent(DL, DL.Lines.Num() - 1, LastLeft, LastRight) && LineExtent(Natural, Natural.Lines.Num() - 1, NaturalLeft, NaturalRight))
	{
		TestEqual(TEXT("the last line starts at the start edge"), LastLeft, BoxLeft(In), 0.01f);
		TestEqual(TEXT("and keeps its natural width"), LastRight - LastLeft, NaturalRight - NaturalLeft, 0.001f);
	}

	// A wrap width narrower than the box is the target, and the line still starts at the start edge.
	FDreamTextLayoutInput Narrow = In;
	Narrow.WrapTextAt = 150.0f;
	FDreamTextDisplayList NarrowDL;
	FDreamTextLayoutEngine::Layout(Narrow, NarrowDL);
	float NarrowLeft = 0.0f, NarrowRight = 0.0f;
	if (TestTrue(TEXT("the narrow column wraps"), NarrowDL.Lines.Num() > 2) && LineExtent(NarrowDL, 0, NarrowLeft, NarrowRight))
	{
		TestEqual(TEXT("a column narrower than the box is justified to the column"), NarrowRight - NarrowLeft, 150.0f, 0.01f);
		TestEqual(TEXT("from the box's start edge"), NarrowLeft, BoxLeft(Narrow), 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyEqualRoomTest,
	"DreamGUI.Text.Justify.EveryOpportunityGetsTheSameExtraRoomAndNothingElseMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextJustifyEqualRoomTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	FDreamTextLayoutInput In = MakeInput(Font, Sentence, 200.0f);
	FDreamTextDisplayList Justified;
	FDreamTextLayoutEngine::Layout(In, Justified);
	FDreamTextLayoutInput LeftAligned = In;
	LeftAligned.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	FDreamTextDisplayList Natural;
	FDreamTextLayoutEngine::Layout(LeftAligned, Natural);
	if (!TestEqual(TEXT("justifying moves no line break"), Justified.Lines.Num(), Natural.Lines.Num()))return false;

	float NaturalLeft = 0.0f, NaturalRight = 0.0f;
	if (!TestTrue(TEXT("the first line has content"), LineExtent(Natural, 0, NaturalLeft, NaturalRight)))return false;
	const float Room = In.Width - (NaturalRight - NaturalLeft);
	TestTrue(TEXT("the first line had room to fill"), Room > 1.0f);

	// The spaces between the words on the first line, not the one hanging off its end, share the room evenly; glyphs keep
	// their own advance.
	int32 LastContent = INDEX_NONE;
	for (const FDreamTextGlyphItem& Item : Natural.Items)
	{
		if (Item.LineIndex == 0 && Item.Kind != EDreamTextItemKind::Space)LastContent = FMath::Max(LastContent, Item.ElementIndex);
	}
	TArray<float> Extras;
	for (const FDreamTextGlyphItem& Item : Natural.Items)
	{
		if (Item.LineIndex != 0)continue;
		const FDreamTextGlyphItem* Wide = FindElement(Justified, Item.ElementIndex);
		if (!TestNotNull(TEXT("every element is laid out in both"), Wide))return false;
		const float Extra = Wide->AdvanceWithSpace - Item.AdvanceWithSpace;
		if (Item.Kind == EDreamTextItemKind::Space && Item.ElementIndex < LastContent)
		{
			Extras.Add(Extra);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("U+%04X keeps its width"), Item.Codepoint), Extra, 0.0f, 0.001f);
		}
	}
	if (!TestTrue(TEXT("the first line has spaces between its words"), Extras.Num() > 0))return false;
	float Sum = 0.0f;
	for (const float Extra : Extras)
	{
		TestEqual(TEXT("every space gets the same extra room"), Extra, Extras[0], 0.001f);
		Sum += Extra;
	}
	TestEqual(TEXT("and together they fill the line"), Sum, Room, 0.01f);

	// TextJustify None: nowhere to widen, so the line is set at its start like a left-aligned one.
	FDreamTextLayoutInput NoJustify = In;
	NoJustify.TextJustify = EDreamTextJustify::None;
	FDreamTextDisplayList NoneDL;
	FDreamTextLayoutEngine::Layout(NoJustify, NoneDL);
	float NoneLeft = 0.0f, NoneRight = 0.0f;
	if (LineExtent(NoneDL, 0, NoneLeft, NoneRight))
	{
		TestEqual(TEXT("TextJustify None widens nothing"), NoneRight - NoneLeft, NaturalRight - NaturalLeft, 0.001f);
		TestEqual(TEXT("and starts the line at its start"), NoneLeft, BoxLeft(In), 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyCJKTest,
	"DreamGUI.Text.Justify.CJKWidensBetweenEveryIdeographUnlessOnlyWordsMay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextJustifyCJKTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	const FString Content = TEXT("漢字仮名交じり文の行を両端に揃える組版の規則を確かめる");
	FDreamTextLayoutInput In = MakeInput(Font, Content, 200.0f);
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestTrue(TEXT("the CJK line wraps"), DL.Lines.Num() > 1))return false;
	float Left = 0.0f, Right = 0.0f;
	if (TestTrue(TEXT("its first line has content"), LineExtent(DL, 0, Left, Right)))
	{
		TestEqual(TEXT("text-justify auto fills a CJK line"), Right - Left, In.Width, 0.01f);
	}
	// Every boundary between two ideographs or kana takes the same room; the line's last character takes none.
	TArray<const FDreamTextGlyphItem*> FirstLine;
	for (const FDreamTextGlyphItem& Item : DL.Items)
	{
		if (Item.LineIndex == 0 && Item.Kind == EDreamTextItemKind::Glyph)FirstLine.Add(&Item);
	}
	if (TestTrue(TEXT("several characters on the first line"), FirstLine.Num() > 2))
	{
		const float Extra = SpacingOf(*FirstLine[0]);
		TestTrue(TEXT("the room is there"), Extra > 0.01f);
		for (int32 k = 0; k + 1 < FirstLine.Num(); k++)
		{
			TestEqual(*FString::Printf(TEXT("character %d gets the same room"), k), SpacingOf(*FirstLine[k]), Extra, 0.001f);
		}
		TestEqual(TEXT("the line's last character gets none"), SpacingOf(*FirstLine.Last()), 0.0f, 0.001f);
	}

	// Between words only: CJK has none, so the line is set at its start with its natural width.
	FDreamTextLayoutInput InterWord = In;
	InterWord.TextJustify = EDreamTextJustify::InterWord;
	FDreamTextDisplayList WordDL;
	FDreamTextLayoutEngine::Layout(InterWord, WordDL);
	FDreamTextLayoutInput LeftAligned = In;
	LeftAligned.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	FDreamTextDisplayList Natural;
	FDreamTextLayoutEngine::Layout(LeftAligned, Natural);
	float WordLeft = 0.0f, WordRight = 0.0f, NaturalLeft = 0.0f, NaturalRight = 0.0f;
	if (LineExtent(WordDL, 0, WordLeft, WordRight) && LineExtent(Natural, 0, NaturalLeft, NaturalRight))
	{
		TestEqual(TEXT("inter-word leaves a CJK line its natural width"), WordRight - WordLeft, NaturalRight - NaturalLeft, 0.001f);
		TestEqual(TEXT("at its start"), WordLeft, BoxLeft(In), 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyCursiveTest,
	"DreamGUI.Text.Justify.InterCharacterNeverWidensInsideAnArabicWord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Room between two joined letters tears the word apart, and there is no kashida to stretch it with: inter-character
 * justification widens between Latin letters and at spaces, never between two letters of a cursive script.
 */
bool FDreamTextJustifyCursiveTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("سلام abc سلام abc سلام abc سلام abc"), 260.0f);
	In.TextJustify = EDreamTextJustify::InterCharacter;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestTrue(TEXT("the line wraps"), DL.Lines.Num() > 1))return false;
	float Left = 0.0f, Right = 0.0f;
	if (TestTrue(TEXT("the first line has content"), LineExtent(DL, 0, Left, Right)))
	{
		TestEqual(TEXT("it is justified"), Right - Left, In.Width, 0.01f);
	}
	auto IsArabicLetter = [](uint32 C) { return C >= 0x0620 && C <= 0x064A; };
	auto IsLatinLetter = [](uint32 C) { return C >= 'a' && C <= 'z'; };
	int32 ArabicPairs = 0;
	int32 LatinPairs = 0;
	for (const FDreamTextGlyphItem& Item : DL.Items)
	{
		if (Item.LineIndex != 0 || Item.Kind != EDreamTextItemKind::Glyph)continue;
		const FDreamTextGlyphItem* Next = FindElement(DL, Item.ElementIndex + 1);
		if (Next == nullptr || Next->LineIndex != 0)continue;
		if (IsArabicLetter(Item.Codepoint) && IsArabicLetter(Next->Codepoint))
		{
			ArabicPairs++;
			TestEqual(TEXT("no room between two Arabic letters"), SpacingOf(Item), 0.0f, 0.001f);
		}
		else if (IsLatinLetter(Item.Codepoint) && IsLatinLetter(Next->Codepoint))
		{
			LatinPairs++;
			TestTrue(TEXT("room between two Latin letters"), SpacingOf(Item) > 0.01f);
		}
	}
	TestTrue(TEXT("the first line has Arabic letters side by side"), ArabicPairs > 0);
	TestTrue(TEXT("and Latin ones"), LatinPairs > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyCaretsTest,
	"DreamGUI.Text.Justify.CaretsAndSelectionRunsFollowTheWidenedRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextJustifyCaretsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	FDreamTextLayoutInput In = MakeInput(Font, Sentence, 200.0f);
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestTrue(TEXT("the sentence wraps"), DL.Lines.Num() > 1))return false;

	// Each caret stands at the leading edge of its character's pen box, wherever the widened spaces moved it.
	const TArray<FDreamUITextCaretProperty>& Carets = DL.Lines[0].CaretPropertyList;
	int32 Checked = 0;
	for (int32 c = 0; c + 1 < Carets.Num(); c++)
	{
		const FDreamTextGlyphItem* Item = FindElement(DL, Carets[c].CharIndex);
		if (Item == nullptr)continue;
		TestEqual(*FString::Printf(TEXT("caret %d stands where its character starts"), c), Carets[c].CaretPosition.X, PenBoxLeft(*Item), 0.01f);
		Checked++;
	}
	TestTrue(TEXT("the first line's carets were checked"), Checked > 5);

	// The line's selection run covers the whole justified width.
	float Left = 0.0f, Right = 0.0f;
	LineExtent(DL, 0, Left, Right);
	int32 Runs = 0;
	for (const FDreamTextVisualRun& Run : DL.VisualRuns)
	{
		if (Run.LineIndex != 0)continue;
		Runs++;
		TestEqual(TEXT("the run starts with the line"), Run.Left, Left, 0.01f);
		TestTrue(TEXT("and reaches its widened end"), Run.Right >= Right - 0.01f);
	}
	TestEqual(TEXT("one left-to-right run"), Runs, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyLastLineTest,
	"DreamGUI.Text.Justify.TheLastLineAndALineANewlineEndsAlignAsLastLineAlignSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextJustifyLastLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;

	// Two paragraphs: "ab cd" ends with a newline, "ef gh" ends the text. Neither wraps.
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab cd\nef gh"), 300.0f);
	auto Extent = [&In](EDreamTextLastLineAlign LastLineAlign, int32 Line, float& OutLeft, float& OutRight)
	{
		FDreamTextLayoutInput Aligned = In;
		Aligned.LastLineAlign = LastLineAlign;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(Aligned, DL);
		return DL.Lines.Num() == 2 && LineExtent(DL, Line, OutLeft, OutRight);
	};
	for (int32 Line = 0; Line < 2; Line++)
	{
		float Left = 0.0f, Right = 0.0f;
		if (TestTrue(TEXT("Auto lays out"), Extent(EDreamTextLastLineAlign::Auto, Line, Left, Right)))
		{
			TestEqual(*FString::Printf(TEXT("Auto: line %d starts at the start edge"), Line), Left, BoxLeft(In), 0.01f);
			TestTrue(*FString::Printf(TEXT("Auto: line %d is not widened"), Line), Right < BoxRight(In) - 1.0f);
		}
		if (TestTrue(TEXT("Start lays out"), Extent(EDreamTextLastLineAlign::Start, Line, Left, Right)))
		{
			TestEqual(*FString::Printf(TEXT("Start: line %d starts at the start edge"), Line), Left, BoxLeft(In), 0.01f);
		}
		if (TestTrue(TEXT("Center lays out"), Extent(EDreamTextLastLineAlign::Center, Line, Left, Right)))
		{
			TestEqual(*FString::Printf(TEXT("Center: line %d is centred"), Line), (Left + Right) * 0.5f, (BoxLeft(In) + BoxRight(In)) * 0.5f, 0.01f);
		}
		if (TestTrue(TEXT("End lays out"), Extent(EDreamTextLastLineAlign::End, Line, Left, Right)))
		{
			TestEqual(*FString::Printf(TEXT("End: line %d ends at the end edge"), Line), Right, BoxRight(In), 0.01f);
		}
		if (TestTrue(TEXT("Justify lays out"), Extent(EDreamTextLastLineAlign::Justify, Line, Left, Right)))
		{
			TestEqual(*FString::Printf(TEXT("Justify: line %d starts at the start edge"), Line), Left, BoxLeft(In), 0.01f);
			TestEqual(*FString::Printf(TEXT("Justify: line %d is justified too"), Line), Right, BoxRight(In), 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextJustifyRightToLeftTest,
	"DreamGUI.Text.Justify.ARightToLeftLineIsJustifiedBetweenItsWordsFromItsRightEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Arabic shaped with the engine's Noto Naskh: a right-to-left paragraph whose wrapped lines fill the box between their
 * words and never inside one, and whose last line starts at the right edge, its start.
 */
bool FDreamTextJustifyRightToLeftTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextJustifyTestLocal;
	FScopedGameWorld TestWorld;
	// Glyphs are read back right away, so they are rasterized on the spot whatever the frame budget says.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	Font->InitFont();
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Font)))return false;

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("مرحبا بالعالم هذا نص عربي طويل يلتف على عدة أسطر في الصندوق"), 260.0f);
	In.FontSize = 32.0f;
	In.TextJustify = EDreamTextJustify::InterCharacter;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	if (!TestTrue(TEXT("the Arabic paragraph wraps"), DL.Lines.Num() > 1))return false;
	for (int32 Line = 0; Line < DL.Lines.Num(); Line++)
	{
		float Left = 0.0f, Right = 0.0f;
		if (!LineExtent(DL, Line, Left, Right))continue;
		TestEqual(*FString::Printf(TEXT("line %d starts at the right edge"), Line), Right, BoxRight(In), 0.05f);
		if (Line + 1 < DL.Lines.Num())
		{
			TestEqual(*FString::Printf(TEXT("line %d is justified to the left edge"), Line), Left, BoxLeft(In), 0.05f);
		}
	}
	// The room went to the spaces: no glyph inside a word carries more than it does unjustified.
	FDreamTextLayoutInput Start = In;
	Start.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	FDreamTextDisplayList Natural;
	FDreamTextLayoutEngine::Layout(Start, Natural);
	if (!TestEqual(TEXT("justifying moves no line break"), Natural.Lines.Num(), DL.Lines.Num())
		|| !TestEqual(TEXT("and lays out the same items"), Natural.Items.Num(), DL.Items.Num()))
	{
		return false;
	}
	int32 Checked = 0;
	for (int32 i = 0; i < DL.Items.Num(); i++)
	{
		const FDreamTextGlyphItem& Item = DL.Items[i];
		if (Item.LineIndex != 0 || Item.Kind != EDreamTextItemKind::Glyph)continue;
		const FDreamTextGlyphItem* Next = FindElement(DL, Item.ElementIndex + 1);
		if (Next == nullptr || Next->Kind != EDreamTextItemKind::Glyph)continue;
		TestEqual(*FString::Printf(TEXT("no room inside a word after U+%04X"), Item.Codepoint), Item.AdvanceWithSpace, Natural.Items[i].AdvanceWithSpace, 0.001f);
		Checked++;
	}
	TestTrue(TEXT("letters inside words were checked"), Checked > 3);
	return true;
}

#endif
