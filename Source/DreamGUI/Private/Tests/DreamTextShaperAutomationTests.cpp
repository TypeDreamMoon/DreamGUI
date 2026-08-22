// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/Text/DreamTextShaper.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Engine/World.h"

/*
 * Shaping against real fonts the engine ships. Roboto has GPOS kerning and no 'kern' table, which
 * is exactly the case the old FT_Get_Kerning path was blind to; Noto Naskh Arabic needs contextual
 * forms and runs right to left; DroidSansFallback covers CJK and stands in as a fallback face.
 */
namespace DreamTextShaperTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	UDreamUIFontData_DistanceField* MakeFileFont(UWorld* World, const TCHAR* Name)
	{
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(EngineFont(Name), false);
		Font->InitFont();
		return Font;
	}

	TArray<FDreamShapeElement> Elements(const FString& Text, float Size = 32.0f)
	{
		TArray<FDreamShapeElement> Result;
		for (int32 i = 0; i < Text.Len(); i++)
		{
			FDreamShapeElement E;
			E.Codepoint = Text[i];
			E.Size = Size;
			Result.Add(E);
		}
		return Result;
	}

	float TotalAdvance(const TArray<FDreamShapedRun>& Runs)
	{
		float Sum = 0.0f;
		for (const auto& Run : Runs) for (const auto& G : Run.Glyphs) Sum += G.XAdvance;
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperKerningTest,
	"DreamGUI.Text.Shaper.GPOSKerningTightensAV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShaperKerningTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	TArray<FDreamShapedRun> Kerned, Unkerned;
	bool bRTL = false;
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("AVAVAV")), Font, true, Kerned, bRTL);
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("AVAVAV")), Font, false, Unkerned, bRTL);
	TestEqual(TEXT("one run"), Kerned.Num(), 1);
	TestEqual(TEXT("one glyph per letter"), Kerned[0].Glyphs.Num(), 6);
	TestFalse(TEXT("Latin is left to right"), bRTL);
	// Roboto kerns A/V through GPOS only; the old path read just the 'kern' table and saw nothing.
	TestTrue(TEXT("kerning pulls A and V together"), TotalAdvance(Kerned) < TotalAdvance(Unkerned) - 1.0f);
	for (int32 g = 0; g < Kerned[0].Glyphs.Num(); g++)
	{
		TestEqual(*FString::Printf(TEXT("glyph %d belongs to element %d"), g, g), Kerned[0].Glyphs[g].ElementIndex, g);
	}
	TestTrue(TEXT("the font reports kerning"), Font->HasKerning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperArabicTest,
	"DreamGUI.Text.Shaper.ArabicJoinsAndRunsRightToLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShaperArabicTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Font)))return false;

	// "مرحبا": five letters; in a word the meem, ra, ha, ba take joined forms.
	const FString Word = TEXT("مرحبا");
	TArray<FDreamShapedRun> Runs;
	bool bRTL = false;
	FDreamTextShaper::ShapeParagraph(Elements(Word), Font, true, Runs, bRTL);
	if (!TestEqual(TEXT("one run"), Runs.Num(), 1))return false;
	TestTrue(TEXT("the paragraph is right to left"), bRTL);
	TestTrue(TEXT("the run is right to left"), Runs[0].bRightToLeft);
	TestEqual(TEXT("five glyphs"), Runs[0].Glyphs.Num(), 5);
	// Visual order runs left to right across the array, so clusters descend.
	for (int32 g = 1; g < Runs[0].Glyphs.Num(); g++)
	{
		TestTrue(TEXT("clusters descend in visual order"), Runs[0].Glyphs[g].ElementIndex < Runs[0].Glyphs[g - 1].ElementIndex);
	}
	// The isolated meem and the initial meem are different glyphs: contextual forms applied.
	TArray<FDreamShapedRun> Alone;
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("م")), Font, true, Alone, bRTL);
	if (TestEqual(TEXT("isolated meem is one glyph"), Alone.Num(), 1) && Alone[0].Glyphs.Num() == 1)
	{
		uint32 MeemInWord = 0;
		for (const auto& G : Runs[0].Glyphs) if (G.ElementIndex == 0)MeemInWord = G.GlyphIndex;
		TestNotEqual(TEXT("meem takes its initial form inside the word"), MeemInWord, Alone[0].Glyphs[0].GlyphIndex);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperFallbackTest,
	"DreamGUI.Text.Shaper.FallbackFaceTakesTheCodepointsThePrimaryLacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShaperFallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	Roboto->SetFallbackFonts({ Droid });
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;
	TestEqual(TEXT("two faces"), Roboto->GetFaceCount(), 2);
	TestFalse(TEXT("Roboto has no CJK"), Roboto->FaceHasCodepoint(0, 0x4E16));
	TestTrue(TEXT("Droid has CJK"), Roboto->FaceHasCodepoint(1, 0x4E16));

	TArray<FDreamShapedRun> Runs;
	bool bRTL = false;
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("Hi 世界 ok")), Roboto, true, Runs, bRTL);
	// "Hi " on Roboto, "世界 " on Droid (the space stays with the run before it), "ok" back on Roboto.
	if (!TestEqual(TEXT("three runs"), Runs.Num(), 3))return false;
	TestEqual(TEXT("first run on the primary face"), Runs[0].FaceIndex, 0);
	TestEqual(TEXT("CJK run on the fallback face"), Runs[1].FaceIndex, 1);
	TestEqual(TEXT("CJK run covers the two ideographs and the space after them"), Runs[1].ElementEnd - Runs[1].ElementStart, 3);
	TestEqual(TEXT("third run on the primary face"), Runs[2].FaceIndex, 0);
	TestEqual(TEXT("third run is the last word"), Runs[2].ElementEnd - Runs[2].ElementStart, 2);
	for (const auto& G : Runs[1].Glyphs)
	{
		TestTrue(TEXT("fallback glyphs are real glyphs, not .notdef"), G.GlyphIndex != 0);
	}

	// The atlas owner renders the fallback glyph through its own cache, keyed by face and glyph.
	const FDreamUICharData Data = Roboto->GetGlyphData(1, Runs[1].Glyphs[0].GlyphIndex, 32.0f, false);
	TestTrue(TEXT("the fallback glyph rasterizes into the primary atlas"), Data.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperLayoutTest,
	"DreamGUI.Text.Shaper.LayoutPlacesShapedGlyphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShaperLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Font->SetFallbackFonts({ Arabic });
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	FDreamTextLayoutInput In;
	In.Content = TEXT("AVATAR wave\nمرحبا hi");
	In.Width = 400.0f;
	In.Height = 200.0f;
	In.Pivot = FVector2f(0.5f, 0.5f);
	In.FontSize = 32.0f;
	In.bUseKerning = true;
	In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
	In.Font = Font;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);

	TestEqual(TEXT("two lines"), DL.Lines.Num(), 2);
	TestEqual(TEXT("eleven visible glyphs on the first line plus the second line's"), DL.VisibleCharCount, 10 + 7);
	// Kerning: with it on, "AVATAR" is narrower than with it off.
	In.bUseKerning = false;
	FDreamTextDisplayList Unkerned;
	FDreamTextLayoutEngine::Layout(In, Unkerned);
	TestTrue(TEXT("kerning narrows the paragraph"), DL.PreferredSize.X < Unkerned.PreferredSize.X);

	// Every emitted glyph has a real atlas entry and every char property covers its quads.
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPaintParams Params;
	FDreamTextPainter::Paint(DL, Params, Geometry, Chars);
	TestEqual(TEXT("one char property per visible character"), Chars.Num(), DL.VisibleCharCount);
	int32 Emitted = 0;
	for (const auto& Item : DL.Items)
	{
		if (!Item.bEmit)continue;
		Emitted++;
		if (!Item.Glyph.IsValid())
		{
			AddError(FString::Printf(TEXT("glyph U+%04X has no atlas entry"), Item.Codepoint));
			break;
		}
	}
	TestEqual(TEXT("four vertices per emitted glyph"), Geometry.OriginVertices.Num(), Emitted * 4);

	// The Arabic word sits on the second line, laid out right to left: the first letter (meem,
	// element 12) has the right-most glyph of the word.
	float MeemX = -FLT_MAX, AlefX = FLT_MAX;
	FString Dump;
	for (const auto& Item : DL.Items)
	{
		if (Item.LineIndex != 1)continue;
		Dump += FString::Printf(TEXT("[U+%04X el%d x=%.1f %s] "), Item.Codepoint, Item.ElementIndex, Item.Pen.X, Item.bEmit ? TEXT("emit") : TEXT("-"));
		if (!Item.bEmit)continue;
		if (Item.Codepoint == 0x0645)MeemX = Item.Pen.X;
		if (Item.Codepoint == 0x0627)AlefX = Item.Pen.X;
	}
	AddInfo(FString::Printf(TEXT("line 1 items: %s"), *Dump));
	TestTrue(TEXT("meem is right of alef"), MeemX > AlefX);
	return true;
}

#endif
