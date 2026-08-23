// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/Text/DreamTextShaper.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamGlyphSdf.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
THIRD_PARTY_INCLUDES_END
#endif

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
		// Tests read glyphs back right away, so rasterize them on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
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
#if WITH_FREETYPE
	// The glyph ids the shaper hands back must be the fallback face's own, or the atlas draws the wrong glyph.
	if (Runs[1].Glyphs.Num() >= 2)
	{
		FT_FaceRec_* DroidFace = Roboto->GetFreeTypeFace(1);
		TestEqual(TEXT("first CJK glyph id is Droid's id for U+4E16"), Runs[1].Glyphs[0].GlyphIndex, (uint32)FT_Get_Char_Index(DroidFace, 0x4E16));
		TestEqual(TEXT("second CJK glyph id is Droid's id for U+754C"), Runs[1].Glyphs[1].GlyphIndex, (uint32)FT_Get_Char_Index(DroidFace, 0x754C));
		// And the outline rasterizer must see the same glyph: a CJK glyph at 48px is a few dozen pixels, not hundreds.
		FDreamGlyphSdfResult Sdf;
		TestTrue(TEXT("the fallback glyph rasterizes"), FDreamGlyphSdf::GenerateMTSDF(DroidFace, Runs[1].Glyphs[0].GlyphIndex, 48.0f, 8.0f, 0.0f, Sdf));
		TestTrue(TEXT("with a plausible size"), Sdf.Width > 20 && Sdf.Width < 90 && Sdf.Height > 20 && Sdf.Height < 90);
		TestTrue(TEXT("and a plausible advance"), Sdf.Advance > 30.0f && Sdf.Advance < 60.0f);
	}
#endif

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMtsdfGlyphTest,
	"DreamGUI.Text.Atlas.OutlineFieldHasInsideAndOutside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMtsdfGlyphTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'H')))return false;
	TestEqual(TEXT("a new font is an outline field"), (int32)Font->GetSdfSource(), (int32)EDreamUISdfSource::OutlineMultiChannel);
	TestEqual(TEXT("and says so through its mark"), (int32)Font->GetFontTextureMark(), (int32)EDreamUIFontTextureMark::Mtsdf);

	// 'H' at 64px: the field must have a clear inside (the stems) and outside (the spread), and the
	// three colour channels must agree with the true distance about where the edge is.
	const FDreamUICharData Data = Font->GetCharData('H', 64.0f, false);
	TestTrue(TEXT("the glyph has a quad"), Data.IsValid() && Data.Width > 10.0f && Data.Height > 10.0f);
	TestTrue(TEXT("and an advance"), Data.XAdvance > 10.0f);

	UTexture2DArray* Atlas = Font->GetFontTexture();
	if (!TestNotNull(TEXT("atlas texture"), Atlas))return false;
	TestEqual(TEXT("the atlas is four channels"), (int32)Atlas->GetPixelFormat(), (int32)PF_B8G8R8A8);

	// Rasterize the glyph once more through the generator and inspect the field directly.
	FDreamGlyphSdfResult Sdf;
	FDreamUIGlyphKey Key;
	if (!TestTrue(TEXT("H resolves to a glyph"), Font->ResolveCodepoint('H', Key)))return false;
	const bool bGenerated = FDreamGlyphSdf::GenerateMTSDF(Font->GetFreeTypeFace(Key.FaceIndex), Key.GlyphIndex, 64.0f, 16.0f, 0.0f, Sdf);
	if (!TestTrue(TEXT("the field generates"), bGenerated))return false;
	int32 Inside = 0, Outside = 0, Disagree = 0;
	for (int32 i = 0; i < Sdf.Width * Sdf.Height; i++)
	{
		const uint8* Px = Sdf.Pixels.GetData() + i * 4;
		const int32 B = Px[0], G = Px[1], R = Px[2], A = Px[3];
		const int32 Median = FMath::Max(FMath::Min(R, G), FMath::Min(FMath::Max(R, G), B));
		// With a 16px spread at 64px, a Roboto H stem (~6px) never gets further than ~3px inside: 0.5 + 3/32.
		if (A > 140)Inside++;
		if (A < 100)Outside++;
		if ((Median > 128) != (A > 128) && FMath::Abs(A - 128) > 24)Disagree++;
	}
	TestTrue(TEXT("some pixels are well inside"), Inside > 50);
	TestTrue(TEXT("some pixels are well outside"), Outside > 50);
	TestTrue(TEXT("the median of the colour field agrees with the true distance about the edge"), Disagree < (Sdf.Width * Sdf.Height) / 100);
	TestTrue(TEXT("the bitmap top sits above the baseline"), Sdf.Top > 0.0f);

	// Bold is a few pixels of emboldening, not a different glyph: at 64px with 3px of bold the
	// bitmap grows by a handful of pixels on each side and the advance by the bold amount.
	FDreamGlyphSdfResult Bold;
	if (TestTrue(TEXT("the bold field generates"), FDreamGlyphSdf::GenerateMTSDF(Font->GetFreeTypeFace(Key.FaceIndex), Key.GlyphIndex, 64.0f, 16.0f, 3.0f, Bold)))
	{
		TestTrue(TEXT("bold is only a little wider"), Bold.Width >= Sdf.Width && Bold.Width <= Sdf.Width + 8);
		TestTrue(TEXT("bold is only a little taller"), Bold.Height >= Sdf.Height && Bold.Height <= Sdf.Height + 8);
		TestEqual(TEXT("bold advance grows by the bold amount"), Bold.Advance, Sdf.Advance + 3.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMtsdfCjkBoundsTest,
	"DreamGUI.Text.Atlas.FallbackGlyphsHavePlausibleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMtsdfCjkBoundsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Droid = MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	if (!TestTrue(TEXT("Droid loads"), Droid->FaceHasCodepoint(0, 0x4E16)))return false;
	// Glyphs seen on screen with wildly wrong quads; every one must rasterize to a few dozen pixels at 48px.
	const TCHAR* Sample = TEXT("逐字渐变填充辉光描边投影模糊你好世界");
	for (int32 i = 0; Sample[i] != 0; i++)
	{
		FDreamUIGlyphKey Key;
		if (!Droid->ResolveCodepoint(Sample[i], Key))continue;
		FDreamGlyphSdfResult Sdf;
		if (!FDreamGlyphSdf::GenerateMTSDF(Droid->GetFreeTypeFace(0), Key.GlyphIndex, 48.0f, 8.0f, 0.0f, Sdf))continue;
		const bool bPlausible = Sdf.Width > 16 && Sdf.Width < 96 && Sdf.Height > 16 && Sdf.Height < 96 && Sdf.Left > -40.0f && Sdf.Left < 40.0f && Sdf.Top > -10.0f && Sdf.Top < 70.0f;
		TestTrue(FString::Printf(TEXT("U+%04X glyph %u: %dx%d at (%.1f, %.1f) advance %.1f"), (uint32)Sample[i], Key.GlyphIndex, Sdf.Width, Sdf.Height, Sdf.Left, Sdf.Top, Sdf.Advance), bPlausible);
		if (!bPlausible)
		{
			AddInfo(FString::Printf(TEXT("U+%04X glyph %u: %dx%d at (%.1f, %.1f) advance %.1f"), (uint32)Sample[i], Key.GlyphIndex, Sdf.Width, Sdf.Height, Sdf.Left, Sdf.Top, Sdf.Advance));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextAsyncGlyphsTest,
	"DreamGUI.Text.Atlas.AsyncGlyphsLandAndNotify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextAsyncGlyphsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'W')))return false;
	// No synchronous budget from here on: every new glyph goes to the worker.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };

	// A glyph that is not in the atlas comes back pending, with its advance but no quad.
	const FDreamUICharData Pending = Font->GetCharData('W', 32.0f, false);
	TestTrue(TEXT("first request is pending"), Pending.bPending);
	TestTrue(TEXT("pending glyph has its advance"), Pending.XAdvance > 15.0f && Pending.XAdvance < 45.0f);
	TestEqual(TEXT("pending glyph has no quad"), Pending.Width, 0.0f);
	TestEqual(TEXT("one glyph on the worker"), Font->GetPendingAsyncGlyphCount(), 1);
	// Asking again does not queue it twice.
	Font->GetCharData('W', 32.0f, false);
	TestEqual(TEXT("still one glyph on the worker"), Font->GetPendingAsyncGlyphCount(), 1);

	// The layout sees the pending quad and says so; the glyph is not emitted yet.
	FDreamTextLayoutInput In;
	In.Content = TEXT("W");
	In.Width = 200.0f;
	In.Height = 100.0f;
	In.Pivot = FVector2f(0.5f, 0.5f);
	In.FontSize = 32.0f;
	In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
	In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
	In.Font = Font;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	TestTrue(TEXT("layout reports pending glyphs"), DL.bHasPendingGlyphs);
	int32 Emitted = 0;
	for (const auto& Item : DL.Items) if (Item.bEmit)Emitted++;
	TestEqual(TEXT("nothing emitted while pending"), Emitted, 0);
	TestTrue(TEXT("but the line still has its width"), DL.PreferredSize.X > 15.0f);

	// When the worker is done, the font says so and the glyph has a real quad.
	bool bNotified = false;
	Font->OnGlyphsReady.AddLambda([&bNotified]() { bNotified = true; });
	Font->WaitForAsyncGlyphs();
	TestTrue(TEXT("the font announced the landing"), bNotified);
	TestEqual(TEXT("nothing left on the worker"), Font->GetPendingAsyncGlyphCount(), 0);
	const FDreamUICharData Ready = Font->GetCharData('W', 32.0f, false);
	TestFalse(TEXT("the glyph is no longer pending"), Ready.bPending);
	TestTrue(TEXT("and has a quad"), Ready.Width > 15.0f && Ready.Height > 15.0f);
	TestEqual(TEXT("with the advance the placeholder promised"), Ready.XAdvance, Pending.XAdvance, 0.05f);

	FDreamTextDisplayList DL2;
	FDreamTextLayoutEngine::Layout(In, DL2);
	TestFalse(TEXT("relayout has no pending glyphs"), DL2.bHasPendingGlyphs);
	Emitted = 0;
	for (const auto& Item : DL2.Items) if (Item.bEmit)Emitted++;
	TestEqual(TEXT("the glyph is emitted now"), Emitted, 1);
	TestEqual(TEXT("same width as the pending layout"), DL2.PreferredSize.X, DL.PreferredSize.X, 0.05f);

	// With the budget back, a new glyph is synchronous again.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	const FDreamUICharData Sync = Font->GetCharData('M', 32.0f, false);
	TestFalse(TEXT("budgeted glyph is synchronous"), Sync.bPending);
	TestTrue(TEXT("and complete"), Sync.Width > 15.0f);
	return true;
}

#endif
