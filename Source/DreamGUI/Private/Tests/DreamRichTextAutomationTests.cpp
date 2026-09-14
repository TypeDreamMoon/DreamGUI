// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/FRichTextParser.h"
#include "Core/DreamUIRichTextCustomStyleData.h"

/*
 * The rich-text markup parser and the custom style data that feeds it, on their own: both are plain
 * structs over a string, so neither needs a font, a widget or a canvas to be asserted on. Nothing
 * covered either of them before, which is how a float font size could be quantised by the first tag
 * and a custom tag could silently clear the bold it was nested inside.
 */
namespace DreamRichTextTestLocal
{
	using namespace DreamUIRichTextParser;

	/** A parser prepared the way FLayoutRun::Prepare prepares it, with every tag kind enabled. */
	struct FPreparedParser
	{
		FRichTextParser Parser;
		FRichTextParseResult Result;

		explicit FPreparedParser(float OriginSize, FColor OriginColor = FColor::White, bool bBold = false, bool bItalic = false
			, bool bUnderline = false, bool bStrikethrough = false)
		{
			Parser.Clear();
			Parser.Prepare(OriginSize, OriginColor, bBold, bItalic, bUnderline, bStrikethrough, 0xffffffff, Result);
		}

		/** Parses the tag that starts at InIndex, leaving InIndex where the parser left it. */
		bool ParseAt(const FString& Text, int& InIndex)
		{
			return Parser.Parse(Text, Text.Len(), InIndex, Result);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextFractionalSizeTest,
	"DreamGUI.Text.RichText.AFractionalFontSizeSurvivesTheFirstTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextFractionalSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextTestLocal;

	// The size the text asked for is a float -- a style sheet computed it, an animation is moving it,
	// DynamicPixelsPerUnit derived it. Every tag recomputes the running size against the origin, so an
	// integer origin put a visible step in the middle of a line at the first tag.
	FPreparedParser Prepared(16.5f);
	TestEqual(TEXT("the size starts where the text asked"), Prepared.Result.Size, 16.5f, KINDA_SMALL_NUMBER);

	const FString Text = TEXT("ab<b>cd</b>");
	int Index = 2;
	TestTrue(TEXT("<b> is a tag"), Prepared.ParseAt(Text, Index));
	TestTrue(TEXT("and it turned bold on"), Prepared.Result.Bold);
	TestEqual(TEXT("the size after the tag is the size before it"), Prepared.Result.Size, 16.5f, KINDA_SMALL_NUMBER);

	// <size=+N> is relative to that same origin, so it carries the fraction too.
	FPreparedParser Relative(16.5f);
	const FString SizedText = TEXT("<size=+2>x</size>");
	int SizedIndex = 0;
	TestTrue(TEXT("<size=+2> is a tag"), Relative.ParseAt(SizedText, SizedIndex));
	TestEqual(TEXT("relative sizes count from the real origin"), Relative.Result.Size, 18.5f, KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextColourNamesTest,
	"DreamGUI.Text.RichText.ColourNamesAndShortHexParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextColourNamesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextTestLocal;

	struct FCase { const TCHAR* Markup; FColor Expected; };
	const TArray<FCase> Cases = {
		{ TEXT("<color=cyan>x</color>"),      FColor(0, 255, 255, 255) },
		{ TEXT("<color=aqua>x</color>"),      FColor(0, 255, 255, 255) },
		{ TEXT("<color=magenta>x</color>"),   FColor(255, 0, 255, 255) },
		{ TEXT("<color=fuchsia>x</color>"),   FColor(255, 0, 255, 255) },
		{ TEXT("<color=red>x</color>"),       FColor(255, 0, 0, 255) },
		{ TEXT("<color=GREY>x</color>"),      FColor(128, 128, 128, 255) },
		{ TEXT("<color=teal>x</color>"),      FColor(0, 128, 128, 255) },
		{ TEXT("<color=navy>x</color>"),      FColor(0, 0, 128, 255) },
		{ TEXT("<color=transparent>x</color>"), FColor(0, 0, 0, 0) },
		// `green` keeps the (0,255,0) it has always meant here; CSS calls that `lime`, and `lime` is
		// an alias for it rather than a second definition that would disagree with shipped content.
		{ TEXT("<color=green>x</color>"),     FColor(0, 255, 0, 255) },
		{ TEXT("<color=lime>x</color>"),      FColor(0, 255, 0, 255) },
		{ TEXT("<color=#0f0>x</color>"),      FColor(0, 255, 0, 255) },
		{ TEXT("<color=#0f08>x</color>"),     FColor(0, 255, 0, 136) },
		{ TEXT("<color=#00ff00>x</color>"),   FColor(0, 255, 0, 255) },
		{ TEXT("<color=rgb(0,255,0)>x</color>"),        FColor(0, 255, 0, 255) },
		{ TEXT("<color=rgb(0, 255, 0)>x</color>"),      FColor(0, 255, 0, 255) },
		// A decimal alpha is CSS's 0..1; a whole one is 0..255, which is the form the hex spellings use.
		{ TEXT("<color=rgba(0,255,0,0.5)>x</color>"),   FColor(0, 255, 0, 128) },
		{ TEXT("<color=rgba(0,255,0,64)>x</color>"),    FColor(0, 255, 0, 64) },
	};
	for (const FCase& Case : Cases)
	{
		FPreparedParser Prepared(16.0f);
		const FString Text = Case.Markup;
		int Index = 0;
		if (!TestTrue(*FString::Printf(TEXT("%s parses"), Case.Markup), Prepared.ParseAt(Text, Index)))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s gives a colour"), Case.Markup), Prepared.Result.HasColor);
		const FColor Parsed = Prepared.Result.Color;
		TestEqual(*FString::Printf(TEXT("%s red"), Case.Markup), (int32)Parsed.R, (int32)Case.Expected.R);
		TestEqual(*FString::Printf(TEXT("%s green"), Case.Markup), (int32)Parsed.G, (int32)Case.Expected.G);
		TestEqual(*FString::Printf(TEXT("%s blue"), Case.Markup), (int32)Parsed.B, (int32)Case.Expected.B);
		TestEqual(*FString::Printf(TEXT("%s alpha"), Case.Markup), (int32)Parsed.A, (int32)Case.Expected.A);
	}

	// A colour the parser does not know is not a tag at all: the markup is left as literal text, which
	// is the only escape the syntax has.
	const TCHAR* NotColours[] =
	{
		TEXT("<color=chartreuse>x</color>"),
		TEXT("<color=rgb(0,255)>x</color>"),        //too few channels
		TEXT("<color=rgb(0,255,0,9)>x</color>"),    //too many for rgb()
		TEXT("<color=rgb(0,255,0>x</color>"),       //never closed
		TEXT("<color=#0f>x</color>"),               //not a hex length
		TEXT("<color=#0g0>x</color>"),              //not hex
	};
	for (const TCHAR* Markup : NotColours)
	{
		FPreparedParser Unknown(16.0f);
		const FString UnknownText = Markup;
		int UnknownIndex = 0;
		TestFalse(*FString::Printf(TEXT("%s is not a tag"), Markup), Unknown.ParseAt(UnknownText, UnknownIndex));
		TestFalse(*FString::Printf(TEXT("%s colours nothing"), Markup), Unknown.Result.HasColor);
	}

	// The tag's alpha is the author's; the hierarchy's fade is applied by the painter, so that fading a
	// rich text never has to re-parse it.
	FPreparedParser Faded(16.0f);
	const FString FadedText = TEXT("<color=#ff000080>x</color>");
	int FadedIndex = 0;
	TestTrue(TEXT("#rrggbbaa parses"), Faded.ParseAt(FadedText, FadedIndex));
	TestEqual(TEXT("and keeps the alpha that was written"), (int32)Faded.Result.Color.A, 0x80);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextCustomStyleKeepOriginTest,
	"DreamGUI.Text.RichText.ACustomStyleLeavesTheFlagsItDoesNotSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextCustomStyleKeepOriginTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextTestLocal;

	// <b>before <mytag>inside</mytag> after</b>: the custom style says nothing about bold, so `inside`
	// has to stay bold. It used to be written flat over all four flags, so a custom tag cleared every
	// one of them -- the three fields that already had a KeepOrigin were the shape this was missing.
	FDreamUIRichTextCustomStyleItemData Style;
	FRichTextParseResult Value;
	Value.Bold = Value.Italic = Value.Underline = Value.Strikethrough = true;
	Style.ApplyToRichTextParseResult(Value);
	TestTrue(TEXT("bold survives a style that does not mention it"), Value.Bold);
	TestTrue(TEXT("so does italic"), Value.Italic);
	TestTrue(TEXT("and underline"), Value.Underline);
	TestTrue(TEXT("and strikethrough"), Value.Strikethrough);

	// On and Off still do what a style is for.
	Style.boldType = EDreamUIRichTextCustomStyleData_BoolType::Off;
	Style.underlineType = EDreamUIRichTextCustomStyleData_BoolType::On;
	FRichTextParseResult Switched;
	Switched.Bold = true;
	Switched.Underline = false;
	Style.ApplyToRichTextParseResult(Switched);
	TestFalse(TEXT("Off turns bold off"), Switched.Bold);
	TestTrue(TEXT("On turns underline on"), Switched.Underline);

	// An asset authored before the enums existed carries its bools, and they mean what they meant.
	FDreamUIRichTextCustomStyleItemData Legacy;
	Legacy.bold = true;
	Legacy.strikethrough = true;
	TestTrue(TEXT("an old asset has something to upgrade"), Legacy.UpgradeLegacyBools());
	TestTrue(TEXT("bold becomes On"), Legacy.boldType == EDreamUIRichTextCustomStyleData_BoolType::On);
	TestTrue(TEXT("strikethrough becomes On"), Legacy.strikethroughType == EDreamUIRichTextCustomStyleData_BoolType::On);
	TestTrue(TEXT("what it did not set stays KeepOrigin"), Legacy.italicType == EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin);
	TestFalse(TEXT("and upgrading twice changes nothing"), Legacy.UpgradeLegacyBools());
	return true;
}

#endif
