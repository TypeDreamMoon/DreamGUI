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
 * The language a text is in (FDreamTextLayoutInput::Language, a rich-text <lang=xx>) decides which fallback faces its
 * characters come from, as each face's cultures say, and a scaled face (CSS size-adjust) draws its glyphs and measures
 * its line box at the text's size times its scale. The mock font's faces are made up -- what each has, its cultures, its
 * scale, a width that tells them apart -- and go through the same face resolution as the shaper's; the engine's GenEi
 * Gothic is held against its own metrics.
 */
namespace DreamTextLanguageTestLocal
{
	using DreamTests::FScopedGameWorld;

	constexpr uint32 KANJI = 0x6F22;//漢

	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, const FString& Language, float Width = 600.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = 200.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.Language = Language;
		In.Font = Font;
		return In;
	}

	/**
	 * Three faces: Latin only; then two that have the ellipsis and the CJK ideographs, one meant for Japanese and twice as
	 * wide, one for Simplified Chinese and three times as wide.
	 */
	UDreamTextTestFont* MakeCJKFamily(UWorld* World)
	{
		UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(World);
		Font->bMockHasKerning = false;
		Font->MockFaces.SetNum(3);
		Font->MockFaces[0].Has = { FInt32Interval(0x20, 0x7E) };
		Font->MockFaces[1].Has = { FInt32Interval(0x2026, 0x2026), FInt32Interval(0x4E00, 0x9FFF) };
		Font->MockFaces[1].Cultures = { TEXT("ja") };
		Font->MockFaces[1].WidthScale = 2.0f;
		Font->MockFaces[2].Has = Font->MockFaces[1].Has;
		Font->MockFaces[2].Cultures = { TEXT("zh-Hans") };
		Font->MockFaces[2].WidthScale = 3.0f;
		return Font;
	}

	/** The glyph items of the text, in order. */
	TArray<const FDreamTextGlyphItem*> GlyphsOf(const FDreamTextDisplayList& DL)
	{
		TArray<const FDreamTextGlyphItem*> Glyphs;
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.Kind == EDreamTextItemKind::Glyph && Item.bCountsAsVisible)Glyphs.Add(&Item);
		}
		return Glyphs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageChoosesTheFaceTest,
	"DreamGUI.Text.Language.TheTextsLanguageChoosesTheFaceWhoseCulturesMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLanguageChoosesTheFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLanguageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = MakeCJKFamily(TestWorld.World);
	const float KanjiAdvance = Font->GetCharData(KANJI, 24.0f, false).XAdvance;

	auto FaceOfKanji = [Font](const TCHAR* Language, float& OutAdvance)
	{
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("a漢"), Language), DL);
		const TArray<const FDreamTextGlyphItem*> Glyphs = GlyphsOf(DL);
		OutAdvance = Glyphs.Num() == 2 ? Glyphs[1]->Glyph.XAdvance : 0.0f;
		return Glyphs.Num() == 2 ? Glyphs[1]->Glyph.FaceIndex : INDEX_NONE;
	};
	float Advance = 0.0f;
	TestEqual(TEXT("Japanese text takes the ideograph from the Japanese face"), FaceOfKanji(TEXT("ja"), Advance), 1);
	TestEqual(TEXT("which draws it twice as wide"), Advance, KanjiAdvance * 2.0f, 0.001f);
	TestEqual(TEXT("Simplified Chinese text takes it from the Chinese face"), FaceOfKanji(TEXT("zh-Hans"), Advance), 2);
	TestEqual(TEXT("which draws it three times as wide"), Advance, KanjiAdvance * 3.0f, 0.001f);
	TestEqual(TEXT("a language no face is meant for takes the first face that has it"), FaceOfKanji(TEXT("en"), Advance), 1);

	// The Latin letter is the primary face's in every language.
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("a漢"), TEXT("zh-Hans")), DL);
	const TArray<const FDreamTextGlyphItem*> Glyphs = GlyphsOf(DL);
	if (TestEqual(TEXT("two glyphs"), Glyphs.Num(), 2))
	{
		TestEqual(TEXT("a is the primary face's"), Glyphs[0]->Glyph.FaceIndex, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageTagTest,
	"DreamGUI.Text.Language.ALangTagSwitchesTheFaceOfWhatItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLanguageTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLanguageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = MakeCJKFamily(TestWorld.World);

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("漢<lang=zh-Hans>漢<lang=ja>漢</lang>漢</lang>漢"), TEXT("ja"));
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	const TArray<const FDreamTextGlyphItem*> Glyphs = GlyphsOf(DL);
	if (TestEqual(TEXT("the tags are markup: five ideographs"), Glyphs.Num(), 5))
	{
		TestEqual(TEXT("outside the tags, the text's own language"), Glyphs[0]->Glyph.FaceIndex, 1);
		TestEqual(TEXT("inside <lang=zh-Hans>, Chinese"), Glyphs[1]->Glyph.FaceIndex, 2);
		TestEqual(TEXT("a tag nested inside takes over, as <size> does"), Glyphs[2]->Glyph.FaceIndex, 1);
		TestEqual(TEXT("and its end gives the outer tag back"), Glyphs[3]->Glyph.FaceIndex, 2);
		TestEqual(TEXT("after both, the text's own again"), Glyphs[4]->Glyph.FaceIndex, 1);
	}

	// With the tag filtered out it is no markup at all: its characters are text.
	FDreamTextLayoutInput Filtered = In;
	Filtered.RichTextFilterFlags = (int32)(0xffffffffu & ~(1u << (uint32)EDreamUIText_RichTextTagFilterFlags::Language));
	FDreamTextDisplayList FilteredDL;
	FDreamTextLayoutEngine::Layout(Filtered, FilteredDL);
	TestTrue(TEXT("a filtered <lang> tag is literal text"), GlyphsOf(FilteredDL).Num() > 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageScaledFaceTest,
	"DreamGUI.Text.Language.AScaledFaceWidensItsGlyphsAndGrowsItsLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A fallback scaled by 1.25 draws its glyphs at the text's size times 1.25, and the line it is on is as tall as its box
 * at that size (Chrome's size-adjust grows the line; Slate's ScalingFactor does not). Underlines and letter spacing stay
 * the text's own.
 */
bool FDreamTextLanguageScaledFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLanguageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	Font->bMockHasKerning = false;
	Font->MockFaces.SetNum(2);
	Font->MockFaces[0].Has = { FInt32Interval(0x20, 0x7E) };
	Font->MockFaces[1].Has = { FInt32Interval(0x4E00, 0x9FFF) };

	auto Layout = [Font](float Scale, FDreamTextDisplayList& OutDL)
	{
		Font->MockFaces[1].Scale = Scale;
		FDreamTextLayoutInput In = MakeInput(Font, TEXT("a漢"), TEXT("ja"));
		In.FontSpace.X = 2.0f;
		FDreamTextLayoutEngine::Layout(In, OutDL);
	};
	FDreamTextDisplayList Plain;
	Layout(1.0f, Plain);
	FDreamTextDisplayList Scaled;
	Layout(1.25f, Scaled);
	const TArray<const FDreamTextGlyphItem*> PlainGlyphs = GlyphsOf(Plain);
	const TArray<const FDreamTextGlyphItem*> ScaledGlyphs = GlyphsOf(Scaled);
	if (!TestEqual(TEXT("two glyphs unscaled"), PlainGlyphs.Num(), 2) || !TestEqual(TEXT("two glyphs scaled"), ScaledGlyphs.Num(), 2))return false;

	TestEqual(TEXT("the ideograph is drawn at 24"), PlainGlyphs[1]->GlyphSize, 24.0f, 0.001f);
	TestEqual(TEXT("and from the scaled face at 30"), ScaledGlyphs[1]->GlyphSize, 30.0f, 0.001f);
	TestEqual(TEXT("the letter stays at 24"), ScaledGlyphs[0]->GlyphSize, 24.0f, 0.001f);
	TestEqual(TEXT("its advance is the 30 px one"), ScaledGlyphs[1]->Glyph.XAdvance, Font->GetCharData(KANJI, 30.0f, false).XAdvance, 0.001f);
	TestEqual(TEXT("1.25 times the unscaled one"), ScaledGlyphs[1]->Glyph.XAdvance, PlainGlyphs[1]->Glyph.XAdvance * 1.25f, 0.001f);
	TestEqual(TEXT("letter spacing is not scaled"), ScaledGlyphs[1]->AdvanceWithSpace - ScaledGlyphs[1]->Glyph.XAdvance, 2.0f, 0.001f);

	// The mock's line box is 1.25 times its size: 30 for the 24 px text, 37.5 once the ideograph is drawn at 30.
	TestEqual(TEXT("unscaled, the line is the text's"), Plain.PreferredSize.Y, 24.0f * 1.25f, 0.001f);
	TestEqual(TEXT("scaled, the face's box at 30 grows it"), Scaled.PreferredSize.Y, 30.0f * 1.25f, 0.001f);
	TestTrue(TEXT("and the paragraph widens with the glyph"), Scaled.PreferredSize.X > Plain.PreferredSize.X + 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageEllipsisTest,
	"DreamGUI.Text.Language.AnEllipsisIsSetInTheLanguageOfTheTextItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLanguageEllipsisTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLanguageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamTextTestFont* Font = MakeCJKFamily(TestWorld.World);

	auto FaceOfDots = [Font](const TCHAR* Content, bool bRich)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content, TEXT("ja"), 200.0f);
		In.bRichText = bRich;
		In.OverflowType = EDreamUITextOverflowType::Ellipsis;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.Codepoint == 0x2026 && Item.bEmit)return Item.Glyph.FaceIndex;
		}
		return (int32)INDEX_NONE;
	};
	// The primary face has no ellipsis; the face the elided text's language prefers draws it.
	TestEqual(TEXT("Japanese text ends in the Japanese face's ellipsis"), FaceOfDots(TEXT("漢漢漢漢漢漢漢漢漢漢漢漢"), false), 1);
	TestEqual(TEXT("Chinese inside a Japanese text ends in the Chinese face's"),
		FaceOfDots(TEXT("<lang=zh-Hans>漢漢漢漢漢漢漢漢漢漢漢漢</lang>"), true), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLanguageGenEiLineBoxTest,
	"DreamGUI.Text.Language.AScaledJapaneseFallbackGrowsTheLineToItsOwnBoxTimesItsScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Roboto with the editor's GenEi Gothic behind it for Japanese, line boxes from each face's DirectWrite-style metrics:
 * GenEi sets USE_TYPO_METRICS with an 880/-120 typo box and a 500 line gap, so its line is 1.5 em -- and a scaled GenEi's
 * is 1.5 em times the scale, taller than Roboto's own line at the text's size.
 */
bool FDreamTextLanguageGenEiLineBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLanguageTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	auto MakeFont = [&TestWorld](const FString& Path)
	{
		UDreamUIFontData_DistanceField* EngineFont = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
		EngineFont->SetFontFilePath(Path, false);
		EngineFont->InitFont();
		return EngineFont;
	};
	UDreamUIFontData_DistanceField* Roboto = MakeFont(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")));
	UDreamUIFontData_DistanceField* GenEi = MakeFont(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts/GenEiGothicPro-Regular.otf")));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	Roboto->SetVerticalMetrics(EDreamUIFontVerticalMetrics::Platform);

	for (const float Scale : { 1.0f, 1.25f })
	{
		FDreamUIFontFallback Japanese;
		Japanese.Font = GenEi;
		Japanese.Cultures = TEXT("ja");
		Japanese.Scale = Scale;
		Roboto->SetFallbacks({ Japanese });
		FDreamTextLayoutInput In = MakeInput(Roboto, TEXT("直角骨写今令"), TEXT("ja"), 1000.0f);
		In.FontSize = 32.0f;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		const TArray<const FDreamTextGlyphItem*> Glyphs = GlyphsOf(DL);
		if (!TestEqual(*FString::Printf(TEXT("scale %.2f: six ideographs"), Scale), Glyphs.Num(), 6))continue;
		for (const FDreamTextGlyphItem* Glyph : Glyphs)
		{
			TestEqual(*FString::Printf(TEXT("scale %.2f: drawn from GenEi"), Scale), Glyph->Glyph.FaceIndex, 1);
			TestEqual(*FString::Printf(TEXT("scale %.2f: at 32 times the scale"), Scale), Glyph->GlyphSize, 32.0f * Scale, 0.001f);
		}
		TestEqual(*FString::Printf(TEXT("scale %.2f: the line is 1.5 em times the scale"), Scale), DL.PreferredSize.Y, 1.5f * 32.0f * Scale, 0.05f);
	}
	return true;
}

#endif
