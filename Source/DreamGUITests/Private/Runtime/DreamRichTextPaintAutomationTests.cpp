// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/FRichTextParser.h"

/*
 * `<gradient=Name>` in the markup, and a custom style's paint: what the parser and the style data say about the paint of
 * each character, before the layout turns that into an item's paint and the text resolves the name. The rule they feed
 * is the layout's -- whichever of the paint and the colour was set by the innermost tag wins -- so what is asserted here
 * is the orders: the paint's beside the colour's, and a style's beside both.
 */
namespace DreamRichTextPaintTestLocal
{
	using namespace DreamUIRichTextParser;

	/** What the parser said about one character of the text. */
	struct FParsedCharacter
	{
		TCHAR Character = 0;
		FName PaintName;
		int32 PaintOrder = 0;
		bool bPaintRemoved = false;
		int32 ColorOrder = 0;
		bool bHasColor = false;
	};

	/** InMarkup through the parser the way the layout runs it (FLayoutRun::Preprocess): every tag acted on, then the character. */
	TArray<FParsedCharacter> ParseCharacters(const FString& InMarkup, int32 InFlags = static_cast<int32>(0xffffffffu))
	{
		FRichTextParser Parser;
		FRichTextParseResult Result;
		Parser.Clear();
		Parser.Prepare(16.0f, FColor::White, false, false, false, false, InFlags, Result);
		TArray<FParsedCharacter> Characters;
		const int Length = InMarkup.Len();
		for (int CharIndex = 0; CharIndex < Length; CharIndex++)
		{
			while (CharIndex < Length && Parser.Parse(InMarkup, Length, CharIndex, Result))
			{
			}
			if (CharIndex >= Length)
			{
				break;
			}
			FParsedCharacter& Parsed = Characters.AddDefaulted_GetRef();
			Parsed.Character = InMarkup[CharIndex];
			Parsed.PaintName = Result.PaintName;
			Parsed.PaintOrder = Result.PaintOrder;
			Parsed.bPaintRemoved = Result.bPaintRemoved;
			Parsed.ColorOrder = Result.ColorOrder;
			Parsed.bHasColor = Result.HasColor;
		}
		return Characters;
	}

	/** Whether the paint and not the colour is what an item of this character draws with: the layout's rule. */
	bool PaintWins(const FParsedCharacter& InCharacter)
	{
		return !InCharacter.PaintName.IsNone() && InCharacter.PaintOrder > InCharacter.ColorOrder;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextGradientTagTest,
	"DreamGUI.Text.RichText.AGradientTagPaintsUntilItClosesAndTheInnermostOfItAndAColourWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextGradientTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextPaintTestLocal;

	const TArray<FParsedCharacter> Coloured = ParseCharacters(TEXT("<gradient=Gold>ab<color=red>c</color>d</gradient>e"));
	if (!TestEqual(TEXT("five characters, the tags taken out"), Coloured.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("a is painted Gold"), Coloured[0].PaintName, FName(TEXT("Gold")));
	TestTrue(TEXT("and the paint wins"), PaintWins(Coloured[0]));
	TestTrue(TEXT("b too"), PaintWins(Coloured[1]));
	TestTrue(TEXT("c is inside a <color> inside the gradient: still named"), Coloured[2].PaintName == FName(TEXT("Gold")) && Coloured[2].bHasColor);
	TestFalse(TEXT("but the colour is the innermost, so c is solid"), PaintWins(Coloured[2]));
	TestTrue(TEXT("d is painted again once the colour closes"), PaintWins(Coloured[3]));
	TestTrue(TEXT("e is after the gradient: no tag paint"), Coloured[4].PaintName.IsNone() && Coloured[4].PaintOrder == 0);
	TestFalse(TEXT("and nothing took the paint away"), Coloured[4].bPaintRemoved);

	const TArray<FParsedCharacter> Inside = ParseCharacters(TEXT("<color=red>a<gradient=Gold>b</gradient>c</color>"));
	if (TestEqual(TEXT("three characters"), Inside.Num(), 3))
	{
		TestFalse(TEXT("a is red"), PaintWins(Inside[0]));
		TestTrue(TEXT("a gradient inside a colour paints"), PaintWins(Inside[1]));
		TestFalse(TEXT("and after it, red again"), PaintWins(Inside[2]));
	}

	const TArray<FParsedCharacter> Nested = ParseCharacters(TEXT("<gradient=A>a<gradient=B>b</gradient>c</gradient>"));
	if (TestEqual(TEXT("three characters"), Nested.Num(), 3))
	{
		TestEqual(TEXT("the outer gradient"), Nested[0].PaintName, FName(TEXT("A")));
		TestEqual(TEXT("the inner one inside it"), Nested[1].PaintName, FName(TEXT("B")));
		TestEqual(TEXT("the outer one again once the inner closes"), Nested[2].PaintName, FName(TEXT("A")));
		TestEqual(TEXT("with the outer one's order"), Nested[2].PaintOrder, Nested[0].PaintOrder);
		TestTrue(TEXT("the inner opened later"), Nested[1].PaintOrder > Nested[0].PaintOrder);
	}

	// Two runs of one name are two runs: the order tells them apart.
	const TArray<FParsedCharacter> Twice = ParseCharacters(TEXT("<gradient=A>a</gradient><gradient=A>b</gradient>"));
	if (TestEqual(TEXT("two characters"), Twice.Num(), 2))
	{
		TestTrue(TEXT("one name, two orders"), Twice[0].PaintName == Twice[1].PaintName && Twice[0].PaintOrder != Twice[1].PaintOrder);
	}

	// The name is read as a custom tag's is, so CSS written without spaces is a name too.
	const TArray<FParsedCharacter> Css = ParseCharacters(TEXT("<gradient=linear-gradient(90deg,red,blue)>x</gradient>"));
	if (TestEqual(TEXT("one character"), Css.Num(), 1))
	{
		TestEqual(TEXT("named by its CSS"), Css[0].PaintName, FName(TEXT("linear-gradient(90deg,red,blue)")));
	}

	// What is not a gradient tag is text.
	const FString Stray = TEXT("a</gradient>b");
	TestEqual(TEXT("a close with nothing open is text"), ParseCharacters(Stray).Num(), Stray.Len());
	const FString Unnamed = TEXT("<gradient=>x");
	TestEqual(TEXT("a gradient with no name is text"), ParseCharacters(Unnamed).Num(), Unnamed.Len());
	const FString Unclosed = TEXT("<gradient=Gold");
	TestEqual(TEXT("an unfinished tag is text"), ParseCharacters(Unclosed).Num(), Unclosed.Len());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextGradientFilterTest,
	"DreamGUI.Text.RichText.WithItsFilterFlagOffAGradientTagIsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextGradientFilterTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextPaintTestLocal;
	const int32 WithoutGradient = static_cast<int32>(0xffffffffu & ~(1u << static_cast<uint32>(EDreamUIText_RichTextTagFilterFlags::Gradient)));
	const FString Markup = TEXT("<gradient=Gold>a</gradient><b>b</b>");
	const TArray<FParsedCharacter> Characters = ParseCharacters(Markup, WithoutGradient);
	// The gradient's markup stays as text, all 26 of its characters, and the bold tag still works: 26 + 'a' + 'b'.
	TestEqual(TEXT("the gradient tags are text, the bold tag is not"), Characters.Num(), Markup.Len() - 7);
	bool bAnyPainted = false;
	for (const FParsedCharacter& Character : Characters)
	{
		bAnyPainted |= !Character.PaintName.IsNone();
	}
	TestFalse(TEXT("and nothing is painted"), bAnyPainted);
	TestTrue(TEXT("the flag is on in the default mask"), (0xffffffffu & (1u << static_cast<uint32>(EDreamUIText_RichTextTagFilterFlags::Gradient))) != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextCustomStylePaintTest,
	"DreamGUI.Text.RichText.ACustomStyleKeepsSetsOrTakesAwayThePaintByItsOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextCustomStylePaintTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIRichTextParser;

	FDreamUIRichTextCustomStyleItemData Style;
	FRichTextParseResult Untouched;
	Untouched.PaintName = TEXT("Gold");
	Untouched.PaintOrder = 1;

	FRichTextParseResult Kept = Untouched;
	Style.ApplyToRichTextParseResult(Kept, 3, TEXT("Warn"));
	TestTrue(TEXT("KeepOrigin leaves the paint of the tags around"), Kept.PaintName == FName(TEXT("Gold")) && Kept.PaintOrder == 1);

	Style.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	FRichTextParseResult Set = Untouched;
	Style.ApplyToRichTextParseResult(Set, 3, TEXT("Warn"));
	TestEqual(TEXT("Set names the paint after the style"), Set.PaintName, FName(TEXT("Warn")));
	TestEqual(TEXT("at the style's tag's order"), Set.PaintOrder, 3);
	TestFalse(TEXT("and paints"), Set.bPaintRemoved);
	FRichTextParseResult Unnamed = Untouched;
	Style.ApplyToRichTextParseResult(Unnamed, 3);
	TestTrue(TEXT("Set without a name has nothing to set"), Unnamed.PaintName == FName(TEXT("Gold")) && Unnamed.PaintOrder == 1);
	FRichTextParseResult Deeper = Untouched;
	Deeper.PaintOrder = 5;
	Style.ApplyToRichTextParseResult(Deeper, 3, TEXT("Warn"));
	TestTrue(TEXT("a gradient opened inside the style keeps its paint"), Deeper.PaintName == FName(TEXT("Gold")) && Deeper.PaintOrder == 5);

	Style.paintType = EDreamUIRichTextCustomStyleData_PaintType::None;
	FRichTextParseResult Removed = Untouched;
	Style.ApplyToRichTextParseResult(Removed, 3, TEXT("Plain"));
	TestTrue(TEXT("None takes the paint away"), Removed.PaintName.IsNone() && Removed.bPaintRemoved);
	TestEqual(TEXT("at the style's order, to be ranked against a colour"), Removed.PaintOrder, 3);
	FRichTextParseResult KeptInside = Untouched;
	KeptInside.PaintOrder = 5;
	Style.ApplyToRichTextParseResult(KeptInside, 3, TEXT("Plain"));
	TestTrue(TEXT("but not from a gradient opened inside it"), !KeptInside.bPaintRemoved && KeptInside.PaintName == FName(TEXT("Gold")));

	// The default order applies everything: a style applied on its own, as the existing callers apply it.
	FRichTextParseResult Everything = Untouched;
	Style.ApplyToRichTextParseResult(Everything);
	TestTrue(TEXT("with no order given, None still takes the paint away"), Everything.bPaintRemoved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextOverlongTagTest,
	"DreamGUI.Text.RichText.ATagNameLongerThanAnFNameHoldsIsTextAndNoCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextOverlongTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextPaintTestLocal;
	// An FName asserts on more than NAME_SIZE - 1 characters, and a player can type a tag that long into a field.
	const FString LongName = FString::ChrN(NAME_SIZE + 10, TEXT('a'));
	const FString Gradient = FString::Printf(TEXT("<gradient=%s>x</gradient>"), *LongName);
	TestEqual(TEXT("an overlong gradient name is text"), ParseCharacters(Gradient).Num(), Gradient.Len());
	const FString Custom = FString::Printf(TEXT("<%s>x"), *LongName);
	TestEqual(TEXT("so is an overlong custom tag"), ParseCharacters(Custom).Num(), Custom.Len());
	const FString Language = FString::Printf(TEXT("<lang=%s>x"), *LongName);
	TestEqual(TEXT("and an overlong language"), ParseCharacters(Language).Num(), Language.Len());
	const FString Image = FString::Printf(TEXT("<img=%s/>x"), *LongName);
	TestEqual(TEXT("and an overlong image"), ParseCharacters(Image).Num(), Image.Len());
	const FString JustFits = FString::Printf(TEXT("<gradient=%s>x</gradient>"), *FString::ChrN(NAME_SIZE - 1, TEXT('b')));
	TestEqual(TEXT("one that fits is a tag"), ParseCharacters(JustFits).Num(), 1);
	return true;
}

#endif
