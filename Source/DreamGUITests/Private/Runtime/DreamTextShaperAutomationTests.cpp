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
#include "DreamScopedWorld.h"
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
	using DreamTests::FScopedGameWorld;

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	UDreamUIFontData_DistanceField* MakeFontFromPath(UWorld* World, const FString& Path)
	{
		// Tests read glyphs back right away, so rasterize them on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(Path, false);
		Font->InitFont();
		return Font;
	}

	UDreamUIFontData_DistanceField* MakeFileFont(UWorld* World, const TCHAR* Name)
	{
		return MakeFontFromPath(World, EngineFont(Name));
	}

	/** A font the editor ships for scripts the runtime fonts do not cover (Devanagari among them). */
	UDreamUIFontData_DistanceField* MakeEditorFileFont(UWorld* World, const TCHAR* Name)
	{
		return MakeFontFromPath(World, FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts"), Name));
	}

	/** Roboto with Noto Naskh Arabic behind it: Latin, digits and the ellipsis from the one, Arabic from the other. */
	UDreamUIFontData_DistanceField* MakeLatinArabicFont(UWorld* World)
	{
		UDreamUIFontData_DistanceField* Roboto = MakeFileFont(World, TEXT("Roboto-Regular.ttf"));
		UDreamUIFontData_DistanceField* Arabic = MakeFileFont(World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
		Roboto->SetFallbackFonts({ Arabic });
		return Roboto;
	}

	/** A layout input at 32 px in a box of the given size, top left aligned, centred pivot. */
	FDreamTextLayoutInput MakeInput(UDreamUIFontData_BaseObject* Font, const FString& Content, float Width = 1000.0f, float Height = 200.0f)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = Width;
		In.Height = Height;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 32.0f;
		In.bUseKerning = true;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.Font = Font;
		return In;
	}

	/** The left-most pen of an element's emitted glyphs, or a huge number when it drew none. */
	float ElementX(const FDreamTextDisplayList& DL, int32 ElementIndex)
	{
		float X = MAX_FLT;
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.ElementIndex == ElementIndex && Item.bEmit)X = FMath::Min(X, Item.Pen.X);
		}
		return X;
	}

	/** The caret that names a character offset on the first line, or null. */
	const FDreamUITextCaretProperty* FindCaret(const FDreamTextDisplayList& DL, int32 CharIndex)
	{
		if (DL.Lines.Num() == 0)return nullptr;
		for (const FDreamUITextCaretProperty& Caret : DL.Lines[0].CaretPropertyList)
		{
			if (Caret.CharIndex == CharIndex)return &Caret;
		}
		return nullptr;
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
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("AVAVAV")), Font, true, EDreamTextFlowDirection::Auto, Kerned, bRTL);
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("AVAVAV")), Font, false, EDreamTextFlowDirection::Auto, Unkerned, bRTL);
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
	FDreamTextShaper::ShapeParagraph(Elements(Word), Font, true, EDreamTextFlowDirection::Auto, Runs, bRTL);
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
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("م")), Font, true, EDreamTextFlowDirection::Auto, Alone, bRTL);
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
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("Hi 世界 ok")), Roboto, true, EDreamTextFlowDirection::Auto, Runs, bRTL);
	// "Hi " on Roboto, "世界" on Droid, then the space after them back on Roboto, the first face that has it -- as Blink
	// and Slate draw it -- in a run of its own (it takes the ideographs' script, "ok" has Latin's), and "ok" on Roboto.
	if (!TestEqual(TEXT("four runs"), Runs.Num(), 4))return false;
	TestEqual(TEXT("first run on the primary face"), Runs[0].FaceIndex, 0);
	TestEqual(TEXT("CJK run on the fallback face"), Runs[1].FaceIndex, 1);
	TestEqual(TEXT("CJK run covers the two ideographs alone"), Runs[1].ElementEnd - Runs[1].ElementStart, 2);
	TestEqual(TEXT("the space after them is the primary face's"), Runs[2].FaceIndex, 0);
	TestEqual(TEXT("...one element"), Runs[2].ElementEnd - Runs[2].ElementStart, 1);
	TestEqual(TEXT("last run on the primary face"), Runs[3].FaceIndex, 0);
	TestEqual(TEXT("last run is the last word"), Runs[3].ElementEnd - Runs[3].ElementStart, 2);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperForcedDirectionTest,
	"DreamGUI.Text.Shaper.AForcedFlowDirectionOverridesWhatTheStringLooksLike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextShaperForcedDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	// "123" has no strong character at all, so the bidi algorithm has nothing to read and answers
	// left-to-right. A UI whose direction is the game's setting rather than the string's content has
	// to be able to say otherwise, which is what UMG's TextFlowDirection is for.
	TArray<FDreamShapedRun> Runs;
	bool bRTL = true;
	FDreamTextShaper::ShapeParagraph(Elements(TEXT("123")), Font, true, EDreamTextFlowDirection::Auto, Runs, bRTL);
	TestFalse(TEXT("a neutral string reads left to right by itself"), bRTL);

	FDreamTextShaper::ShapeParagraph(Elements(TEXT("123")), Font, true, EDreamTextFlowDirection::RightToLeft, Runs, bRTL);
	TestTrue(TEXT("forcing right-to-left says so"), bRTL);

	// And forcing the other way survives a string that WOULD have read right to left.
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Arabic)))
	{
		const FString Word = TEXT("مرحبا");
		FDreamTextShaper::ShapeParagraph(Elements(Word), Arabic, true, EDreamTextFlowDirection::Auto, Runs, bRTL);
		TestTrue(TEXT("Arabic reads right to left by itself"), bRTL);
		FDreamTextShaper::ShapeParagraph(Elements(Word), Arabic, true, EDreamTextFlowDirection::LeftToRight, Runs, bRTL);
		TestFalse(TEXT("forcing left-to-right says so"), bRTL);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextFallbackFaceLineBoxTest,
	"DreamGUI.Text.Shaper.ALineIsAsTallAsTheFallbackFaceOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextFallbackFaceLineBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	Roboto->SetFallbackFonts({ Droid });
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Roboto)))return false;

	// A distance-field font scales every metric from its sample size, and the scale is set up when a
	// layout starts -- so ask for it the way a layout does before reading any metric back.
	Roboto->PrepareForLayout(0.0f);

	// Face 0 is Roboto, face 1 is the CJK fallback. A CJK em is fuller than a Latin one, so the two
	// faces do not agree on where the line's edges are -- which is the whole point: a line with CJK on
	// it used to be measured from Roboto alone and the glyphs hung out of it.
	float PrimaryAscent = 0.0f, PrimaryDescent = 0.0f, PrimaryLineHeight = 0.0f;
	float FallbackAscent = 0.0f, FallbackDescent = 0.0f, FallbackLineHeight = 0.0f;
	const bool bPrimary = Roboto->GetFaceMetrics(0, 32.0f, PrimaryAscent, PrimaryDescent, PrimaryLineHeight);
	const bool bFallback = Roboto->GetFaceMetrics(1, 32.0f, FallbackAscent, FallbackDescent, FallbackLineHeight);
	if (!TestTrue(TEXT("the primary face answers"), bPrimary))return false;
	if (!TestTrue(TEXT("the fallback face answers too"), bFallback))return false;
	TestFalse(TEXT("a face that does not exist answers nothing"),
		Roboto->GetFaceMetrics(7, 32.0f, FallbackAscent, FallbackDescent, FallbackLineHeight));
	Roboto->GetFaceMetrics(1, 32.0f, FallbackAscent, FallbackDescent, FallbackLineHeight);
	// Asking twice must give the same answer: the second one comes out of the per-face cache.
	float CachedAscent = 0.0f, CachedDescent = 0.0f, CachedLineHeight = 0.0f;
	Roboto->GetFaceMetrics(1, 32.0f, CachedAscent, CachedDescent, CachedLineHeight);
	TestEqual(TEXT("the cached answer is the same answer"), CachedAscent, FallbackAscent, 0.0001f);
	TestEqual(TEXT("descent too"), CachedDescent, FallbackDescent, 0.0001f);
	TestEqual(TEXT("line height too"), CachedLineHeight, FallbackLineHeight, 0.0001f);

	// Now the thing that matters: a line with a fallback glyph on it is at least as tall as that face.
	auto ParagraphHeight = [&](const FString& Content)
	{
		FDreamTextLayoutInput In;
		In.Content = Content;
		In.Width = 1000.0f;
		In.Height = 400.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 32.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.Font = Roboto;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		return DL.PreferredSize.Y;
	};
	const float LatinOnly = ParagraphHeight(TEXT("Latin"));
	const float WithCJK = ParagraphHeight(TEXT("Latin 世界"));
	TestTrue(TEXT("the latin line measures something"), LatinOnly > 0.0f);
	// The two faces may agree on the box, in which case nothing grows -- but it may never SHRINK, and
	// the line must never be shorter than the fallback face's own box.
	TestTrue(TEXT("a line with a fallback glyph is not shorter than one without"), WithCJK >= LatinOnly - 0.01f);
	TestTrue(TEXT("and it fits the fallback face's box"), WithCJK >= FallbackAscent + FallbackDescent - 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextRightToLeftEllipsisTest,
	"DreamGUI.Text.Breaker.ARightToLeftLineIsCutAndElidedAtItsLeftEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextRightToLeftEllipsisTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Arabic)))return false;

	auto Layout = [&](EDreamUITextOverflowType Overflow, float Width, FDreamTextDisplayList& Out)
	{
		FDreamTextLayoutInput In;
		In.Content = TEXT("مرحبا بالعالم مرحبا بالعالم");
		In.Width = Width;
		In.Height = 200.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 32.0f;
		In.OverflowType = Overflow;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.Font = Arabic;
		FDreamTextLayoutEngine::Layout(In, Out);
	};

	FDreamTextDisplayList Wide;
	Layout(EDreamUITextOverflowType::HorizontalOverflow, 2000.0f, Wide);
	int32 WideEmitted = 0;
	for (const auto& Item : Wide.Items)if (Item.bEmit)WideEmitted++;
	if (!TestTrue(TEXT("the wide box draws the whole string"), WideEmitted > 0))return false;
	TestFalse(TEXT("and does not truncate"), Wide.bTruncated);

	// A right-to-left line reads from its right edge, so what does not fit is at the LEFT. It used to
	// be cut nowhere at all: the clamp only ever ran in the left-to-right branch.
	FDreamTextDisplayList Cut;
	Layout(EDreamUITextOverflowType::Truncate, 150.0f, Cut);
	TestTrue(TEXT("a narrow box truncates right-to-left text"), Cut.bTruncated);
	int32 CutEmitted = 0;
	float CutRight = -MAX_FLT;
	for (const auto& Item : Cut.Items)
	{
		if (!Item.bEmit)continue;
		CutEmitted++;
		CutRight = FMath::Max(CutRight, Item.Pen.X + Item.Glyph.XOffset + Item.Glyph.Width);
	}
	TestTrue(TEXT("it drops glyphs"), CutEmitted < WideEmitted);
	TestTrue(TEXT("and what is left fits the box"), CutRight <= 150.0f * 0.5f + 1.0f);

	FDreamTextDisplayList Elided;
	Layout(EDreamUITextOverflowType::Ellipsis, 150.0f, Elided);
	TestTrue(TEXT("and so does the ellipsis policy"), Elided.bTruncated);
	const FDreamTextGlyphItem* Dots = nullptr;
	float LeftMost = MAX_FLT;
	for (const auto& Item : Elided.Items)
	{
		if (!Item.bEmit)continue;
		if (Item.Codepoint == 0x2026)Dots = &Item;
		LeftMost = FMath::Min(LeftMost, Item.Pen.X);
	}
	if (TestNotNull(TEXT("an ellipsis is drawn"), Dots))
	{
		// On the LEFT, because that is the end of a right-to-left line, not the start of it.
		TestEqual(TEXT("the ellipsis is the left-most thing on the line"), Dots->Pen.X, LeftMost, 0.01f);
	}

	// A wrapped right-to-left paragraph that runs past the bottom of its box ends its last visible line in an ellipsis
	// too -- and on that line's left, where it ends, rather than appended on its right, where it starts. Roboto stands in
	// front of the Arabic face for the ellipsis glyph, which the Arabic face does not have.
	UDreamUIFontData_DistanceField* LatinArabic = MakeLatinArabicFont(TestWorld.World);
	FDreamTextLayoutInput Wrapped = MakeInput(LatinArabic, TEXT("مرحبا بالعالم مرحبا بالعالم مرحبا بالعالم مرحبا بالعالم"), 200.0f, 100.0f);
	Wrapped.OverflowType = EDreamUITextOverflowType::Ellipsis;
	Wrapped.bAutoWrapText = true;
	FDreamTextDisplayList WrappedDL;
	FDreamTextLayoutEngine::Layout(Wrapped, WrappedDL);
	TestTrue(TEXT("the wrapped paragraph is cut at the bottom of its box"), WrappedDL.bTruncated);
	const FDreamTextGlyphItem* WrappedDots = nullptr;
	for (const auto& Item : WrappedDL.Items)
	{
		if (Item.bEmit && Item.Codepoint == 0x2026)WrappedDots = &Item;
	}
	if (TestNotNull(TEXT("its last line has an ellipsis"), WrappedDots))
	{
		TestEqual(TEXT("on the last line that fits"), WrappedDots->LineIndex, WrappedDL.Lines.Num() - 1);
		for (const auto& Item : WrappedDL.Items)
		{
			if (!Item.bEmit || Item.LineIndex != WrappedDots->LineIndex || &Item == WrappedDots)continue;
			if (Item.Pen.X < WrappedDots->Pen.X - 0.01f)
			{
				AddError(FString::Printf(TEXT("U+%04X at %.2f is left of the ellipsis at %.2f"), Item.Codepoint, Item.Pen.X, WrappedDots->Pen.X));
				break;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBidiAcrossRunsTest,
	"DreamGUI.Text.Shaper.ARightToLeftPhraseReadsInItsOwnOrderAcrossRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Right-to-left text was reordered only inside one shaped run, and a left-to-right paragraph placed its runs in logical
 * order, so a right-to-left phrase spanning several runs -- a number in it, a word in bold -- had its words laid out in
 * reading order from the left, and the reader met the second word first. A line is put in visual order by its bidi
 * levels (UAX #9 rule L2).
 */
bool FDreamTextBidiAcrossRunsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeLatinArabicFont(TestWorld.World);
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	// g0 o1 _2 [meem3 ra4 ha5 ba6 alef7] _8 1-9 2-10 _11 [ain12 alef13 lam14 meem15] _16 n17 o18 w19
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT("go مرحبا 12 عالم now")), DL);
	TestTrue(TEXT("the phrase's first word is right of its last"), ElementX(DL, 3) > ElementX(DL, 12));
	TestTrue(TEXT("the number inside it still reads left to right"), ElementX(DL, 9) < ElementX(DL, 10));
	TestTrue(TEXT("and sits between the two words"), ElementX(DL, 12) < ElementX(DL, 9) && ElementX(DL, 10) < ElementX(DL, 3));
	for (int32 Element = 1; Element < 20; Element++)
	{
		const float X = ElementX(DL, Element);
		if (X != MAX_FLT && X < ElementX(DL, 0))
		{
			AddError(FString::Printf(TEXT("element %d is left of the line's first word"), Element));
			break;
		}
	}
	TestTrue(TEXT("the left-to-right words keep their places at the ends"), ElementX(DL, 17) > ElementX(DL, 3));

	// A bold word starts a run of its own; the phrase still reads right to left across it.
	FDreamTextLayoutInput Bold = MakeInput(Font, TEXT("ok <b>مرحبا</b> عالم"));
	Bold.bRichText = true;
	FDreamTextDisplayList BoldDL;
	FDreamTextLayoutEngine::Layout(Bold, BoldDL);
	// o0 k1 _2 [meem3 .. alef7] _8 [ain9 .. meem12]
	TestTrue(TEXT("the bold word, read first, is the right one"), ElementX(BoldDL, 3) > ElementX(BoldDL, 9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPendingStrokeTest,
	"DreamGUI.Text.Atlas.AnUnderlineStillRasterizingIsReportedAsPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The underline is drawn from the font's '_', and the ellipsis is a glyph too; neither checked whether that glyph was
 * still on the rasterizer's worker. With the frame's synchronous budget spent, an underline under letters already in
 * the atlas came out as an empty quad and the layout never said it was waiting, so nothing laid the text out again
 * when the glyph landed: the underline stayed missing until the text changed.
 */
bool FDreamTextPendingStrokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'a')))return false;
	// The letters are in the atlas; the '_' is not.
	Font->GetCharData('a', 32.0f, false);
	Font->GetCharData('b', 32.0f, false);
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab"));
	In.bUnderline = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	TestTrue(TEXT("the layout says a glyph is still on its way"), DL.bHasPendingGlyphs);
	int32 Emitted = 0;
	for (const auto& Item : DL.Items)
	{
		if (Item.bEmit)Emitted++;
	}
	TestEqual(TEXT("the letters themselves are drawn"), Emitted, 2);

	Font->WaitForAsyncGlyphs();
	FDreamTextDisplayList Landed;
	FDreamTextLayoutEngine::Layout(In, Landed);
	TestFalse(TEXT("once it landed nothing is pending"), Landed.bHasPendingGlyphs);
	bool bUnderlined = Landed.Items.Num() > 0;
	for (const auto& Item : Landed.Items)
	{
		bUnderlined &= Item.Style.bUnderline;
	}
	TestTrue(TEXT("and the underline is there"), bUnderlined);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBoldClusterAdvanceTest,
	"DreamGUI.Text.Shaper.SyntheticBoldWidensAClusterOnceNotEachMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Synthetic bold added the font's bold ratio to every glyph's advance, so each zero-width combining mark gained an
 * advance of its own and pushed the text after it -- and itself -- off its letter. A cluster is emboldened once.
 */
bool FDreamTextBoldClusterAdvanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto has the combining acute"), Font->FaceHasCodepoint(0, 0x0301)))return false;

	auto Width = [Font](const TCHAR* Content, EDreamUITextFontStyle Style)
	{
		FDreamTextLayoutInput In = MakeInput(Font, Content);
		In.FontStyle = Style;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		return DL.PreferredSize.X;
	};
	const float AccentedBold = Width(TEXT("x\U00000301x"), EDreamUITextFontStyle::Bold);
	const float PlainBold = Width(TEXT("xx"), EDreamUITextFontStyle::Bold);
	TestEqual(TEXT("an accent adds no width to bold text"), AccentedBold, PlainBold, 0.01f);
	TestEqual(TEXT("and bold adds one emboldening per letter"), PlainBold - Width(TEXT("xx"), EDreamUITextFontStyle::None),
		2.0f * 32.0f * Font->GetBoldRatio(), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextNegativeSpacingInkTest,
	"DreamGUI.Text.Shaper.NegativeLetterSpacingNeverMakesALineNarrowerThanItsInk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Negative letter spacing pulls the pen back over ink it has already drawn. The line's right edge was the pen after the
 * last element that still advanced, less one spacing, so a tightly tracked heading measured narrower than what it drew
 * and centred off to one side. A line reaches its ink: the preferred width covers it, and centring centres it.
 */
bool FDreamTextNegativeSpacingInkTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	auto InkExtent = [](const FDreamTextDisplayList& DL, float& OutLeft, float& OutRight)
	{
		OutLeft = MAX_FLT;
		OutRight = -MAX_FLT;
		for (const auto& Item : DL.Items)
		{
			if (!Item.bEmit)continue;
			OutLeft = FMath::Min(OutLeft, Item.Pen.X + Item.Glyph.XOffset);
			OutRight = FMath::Max(OutRight, Item.Pen.X + Item.Glyph.XOffset + Item.Glyph.Width);
		}
	};
	FDreamTextLayoutInput Left = MakeInput(Font, TEXT("This is heading 2"));
	Left.FontSpace.X = -5.0f;
	FDreamTextDisplayList LeftDL;
	FDreamTextLayoutEngine::Layout(Left, LeftDL);
	const float BoxLeft = Left.Width * (0.5f - Left.Pivot.X) - Left.Width * 0.5f;
	float InkLeft = 0.0f, InkRight = 0.0f;
	InkExtent(LeftDL, InkLeft, InkRight);
	const float Width = LeftDL.PreferredSize.X;
	TestTrue(*FString::Printf(TEXT("the preferred width %.2f reaches the ink's right edge %.2f"), Width, InkRight - BoxLeft), Width >= InkRight - BoxLeft - 0.01f);

	FDreamTextLayoutInput Centred = Left;
	Centred.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Center;
	FDreamTextDisplayList CentredDL;
	FDreamTextLayoutEngine::Layout(Centred, CentredDL);
	float CentredInkLeft = 0.0f, CentredInkRight = 0.0f;
	InkExtent(CentredDL, CentredInkLeft, CentredInkRight);
	// Centred, the line [0, Width] sits around the box's centre, which is 0 here; the ink sits in it as it sat left aligned.
	const float LineStart = -Width * 0.5f;
	TestEqual(TEXT("the centred ink starts where the centred line puts it"), CentredInkLeft, LineStart + (InkLeft - BoxLeft), 0.05f);
	TestEqual(TEXT("and ends inside the centred line"), CentredInkRight, LineStart + (InkRight - BoxLeft), 0.05f);
	TestTrue(TEXT("so it is no further right of centre than the line's own half"), CentredInkRight <= Width * 0.5f + 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCursiveSpacingTest,
	"DreamGUI.Text.Shaper.LetterSpacingNeverOpensACursiveWord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Letter spacing was added after every element, Arabic letters included, which pulls a joined word apart into broken
 * pieces. CSS Text asks that cursive scripts not be letter-spaced at all, and Slate does not space them either: an
 * Arabic word lays out the same with spacing as without.
 */
bool FDreamTextCursiveSpacingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Font)))return false;

	auto Pens = [Font](float Spacing)
	{
		FDreamTextLayoutInput In = MakeInput(Font, TEXT("مرحبا"), 400.0f);
		In.FontSpace.X = Spacing;
		FDreamTextDisplayList DL;
		FDreamTextLayoutEngine::Layout(In, DL);
		TArray<float> Result;
		for (const auto& Item : DL.Items)
		{
			if (Item.bEmit)Result.Add(Item.Pen.X);
		}
		return Result;
	};
	const TArray<float> Unspaced = Pens(0.0f);
	const TArray<float> Spaced = Pens(3.0f);
	if (!TestEqual(TEXT("the same glyphs"), Spaced.Num(), Unspaced.Num()))return false;
	for (int32 i = 0; i < Spaced.Num(); i++)
	{
		TestEqual(*FString::Printf(TEXT("glyph %d has not moved"), i), Spaced[i], Unspaced[i], 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLigatureTest,
	"DreamGUI.Text.Shaper.LigaturesFormWhenAllowedAndTheirCaretsSplitThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * liga and clig were forced off so every code point kept a glyph of its own, which is what carets and per-character
 * animation assumed. A browser forms them; so does this, when the text allows it and nothing spaces its letters, and a
 * ligature's carets split its advance evenly by the clusters it covers. Roboto's one ligature is fi: "office" draws
 * o, f, fi, c, e, and the caret between the f and the i inside the fi stands strictly inside that glyph.
 */
bool FDreamTextLigatureTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	auto CountGlyphs = [](const FDreamTextDisplayList& DL)
	{
		int32 Count = 0;
		for (const auto& Item : DL.Items)
		{
			if (Item.Kind == EDreamTextItemKind::Glyph)Count++;
		}
		return Count;
	};
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("office"));
	In.bAllowLigatures = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	TestTrue(TEXT("a ligature draws fewer glyphs than there are letters"), CountGlyphs(DL) < 6);
	const FDreamUITextCaretProperty* BeforeF = FindCaret(DL, 2);
	const FDreamUITextCaretProperty* BeforeI = FindCaret(DL, 3);
	const FDreamUITextCaretProperty* BeforeC = FindCaret(DL, 4);
	if (TestNotNull(TEXT("a caret before the fi"), BeforeF) && TestNotNull(TEXT("a caret between f and i"), BeforeI)
		&& TestNotNull(TEXT("a caret after the fi"), BeforeC))
	{
		TestTrue(TEXT("the caret between f and i is strictly inside the ligature"),
			BeforeI->CaretPosition.X > BeforeF->CaretPosition.X + 0.01f && BeforeI->CaretPosition.X < BeforeC->CaretPosition.X - 0.01f);
		TestEqual(TEXT("halfway across it, as it covers two clusters"), BeforeI->CaretPosition.X,
			(BeforeF->CaretPosition.X + BeforeC->CaretPosition.X) * 0.5f, 0.01f);
	}

	FDreamTextLayoutInput Spaced = In;
	Spaced.FontSpace.X = 3.0f;
	FDreamTextDisplayList SpacedDL;
	FDreamTextLayoutEngine::Layout(Spaced, SpacedDL);
	TestEqual(TEXT("letter spacing keeps every letter its own glyph"), CountGlyphs(SpacedDL), 6);

	FDreamTextLayoutInput Disallowed = In;
	Disallowed.bAllowLigatures = false;
	FDreamTextDisplayList DisallowedDL;
	FDreamTextLayoutEngine::Layout(Disallowed, DisallowedDL);
	TestEqual(TEXT("and so does a text that does not allow ligatures"), CountGlyphs(DisallowedDL), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextConjunctTest,
	"DreamGUI.Text.Breaker.ADevanagariConjunctIsNeverSplitAcrossLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The per-character fallback cut an over-long word between any two elements, so a conjunct -- a consonant, a virama and
 * the next consonant, one letter to a reader -- could be split over two lines. It cuts between clusters only.
 */
bool FDreamTextConjunctTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeEditorFileFont(TestWorld.World, TEXT("NotoSansDevanagari-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Sans Devanagari shapes"), FDreamTextShaper::CanShape(Font)))return false;

	// kssa (0..2), tra (3..5), jnya (6..8)
	FDreamTextLayoutInput In = MakeInput(Font, TEXT("क्षत्रज्ञ"), 1.0f, 600.0f);
	In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	In.WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	TestEqual(TEXT("three conjuncts, three lines"), DL.Lines.Num(), 3);
	for (int32 Line = 0; Line < DL.Lines.Num(); Line++)
	{
		const TArray<FDreamUITextCaretProperty>& Carets = DL.Lines[Line].CaretPropertyList;
		if (Carets.Num() == 0)continue;
		const int32 First = Carets[0].CharIndex;
		TestTrue(*FString::Printf(TEXT("line %d starts on a conjunct, at offset %d"), Line, First), First == 0 || First == 3 || First == 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextFontUnderlineTest,
	"DreamGUI.Text.Shaper.AnUnderlineSitsWhereTheFontPutsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An underline was the font's '_' glyph stretched across each letter, wherever and however thick that glyph happened to
 * be. A font says where its underline goes and how thick it is, and the stroke is drawn there.
 */
bool FDreamTextFontUnderlineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;

	FDreamTextLayoutInput In = MakeInput(Font, TEXT("ab"));
	In.bUnderline = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	float Position = 0.0f, Thickness = 0.0f, StrikePosition = 0.0f, StrikeThickness = 0.0f;
	if (!TestTrue(TEXT("Roboto has underline metrics"), Font->GetDecorationMetrics(0, 32.0f, Position, Thickness, StrikePosition, StrikeThickness)))return false;
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, FDreamTextPaintParams(), Geometry, Chars);
	if (!TestEqual(TEXT("two glyphs and one stroke"), Geometry.OriginVertices.Num(), 3 * 4))return false;
	if (!TestTrue(TEXT("a is laid out"), DL.Items.Num() > 0))return false;
	const float Baseline = DL.Items[0].Pen.Y;
	const float StrokeBottom = Geometry.OriginVertices[8].Position.Z;
	const float StrokeTop = Geometry.OriginVertices[10].Position.Z;
	TestEqual(TEXT("the stroke is centred where the font puts its underline"), (StrokeTop + StrokeBottom) * 0.5f, Baseline + Position, 0.01f);
	TestEqual(TEXT("and is as thick as the font says"), StrokeTop - StrokeBottom, FMath::Max(Thickness, 1.0f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextRightToLeftStartTest,
	"DreamGUI.Text.Shaper.ARightToLeftParagraphStartsAtTheRightAndListsItsVisualRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Alignment ignored the paragraph's direction, so an Arabic paragraph with the default (Left) alignment hugged the left
 * edge where UMG and a browser start it at the right; and a right-to-left character's caret stood on its trailing
 * edge. Left means start in a right-to-left paragraph, a caret stands on its cluster's leading edge, and every line
 * reports its directional runs for selections to be drawn from.
 */
bool FDreamTextRightToLeftStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Arabic = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Arabic)))return false;

	FDreamTextLayoutInput In = MakeInput(Arabic, TEXT("مرحبا"), 400.0f);
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	const float BoxRight = In.Width * (0.5f - In.Pivot.X) + In.Width * 0.5f;
	const FDreamTextGlyphItem* Meem = nullptr;
	for (const auto& Item : DL.Items)
	{
		if (Item.ElementIndex == 0 && Item.bEmit)Meem = &Item;
	}
	if (TestNotNull(TEXT("the first letter is drawn"), Meem))
	{
		TestEqual(TEXT("the line starts at the box's right edge"), Meem->Pen.X + Meem->DecorationOffset + Meem->AdvanceWithSpace, BoxRight, 0.01f);
	}
	const FDreamUITextCaretProperty* First = FindCaret(DL, 0);
	if (TestNotNull(TEXT("a caret before the first letter"), First))
	{
		TestEqual(TEXT("on its leading edge, the right"), First->CaretPosition.X, BoxRight, 0.01f);
	}

	UDreamUIFontData_DistanceField* LatinArabic = MakeLatinArabicFont(TestWorld.World);
	FDreamTextDisplayList Mixed;
	FDreamTextLayoutEngine::Layout(MakeInput(LatinArabic, TEXT("ab مرحبا cd")), Mixed);
	if (TestEqual(TEXT("three runs: left to right, right to left, left to right"), Mixed.VisualRuns.Num(), 3))
	{
		TestFalse(TEXT("the first runs left to right"), Mixed.VisualRuns[0].bRightToLeft);
		TestTrue(TEXT("the middle one right to left"), Mixed.VisualRuns[1].bRightToLeft);
		TestFalse(TEXT("the last left to right"), Mixed.VisualRuns[2].bRightToLeft);
		TestEqual(TEXT("the middle one is the Arabic word"), Mixed.VisualRuns[1].SourceStart, 3);
		TestEqual(TEXT("all of it"), Mixed.VisualRuns[1].SourceEnd, 8);
		for (int32 i = 1; i < 3; i++)
		{
			TestTrue(*FString::Printf(TEXT("run %d is right of run %d"), i, i - 1), Mixed.VisualRuns[i].Left >= Mixed.VisualRuns[i - 1].Right - 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextStyledFaceTest,
	"DreamGUI.Text.Shaper.ABoldRunIsDrawnFromTheRealBoldFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Bold and italic were always made up -- an emboldened, sheared regular glyph -- even for a font that comes with real
 * bold and italic faces. A styled run is drawn from the font's face for that style when it has one, with none of the
 * synthetic advance or dilation; what the font lacks is still made up.
 */
bool FDreamTextStyledFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Regular = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Bold = MakeFileFont(TestWorld.World, TEXT("Roboto-Bold.ttf"));
	Regular->SetStyleFonts(Bold, nullptr, nullptr);
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Regular)))return false;

	FDreamTextLayoutInput In = MakeInput(Regular, TEXT("<b>m</b>"));
	In.bRichText = true;
	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(In, DL);
	FDreamTextDisplayList BoldAlone;
	FDreamTextLayoutEngine::Layout(MakeInput(Bold, TEXT("m")), BoldAlone);
	if (TestEqual(TEXT("one glyph"), DL.Items.Num(), 1) && TestEqual(TEXT("one glyph from the bold font"), BoldAlone.Items.Num(), 1))
	{
		const FDreamTextGlyphItem& M = DL.Items[0];
		TestTrue(TEXT("the m is bold"), M.Style.bBold);
		TestFalse(TEXT("and none of it is made up"), M.Style.bSyntheticBold);
		TestEqual(TEXT("its advance is the bold face's own"), M.AdvanceWithSpace, BoldAlone.Items[0].AdvanceWithSpace, 0.01f);
	}

	FDreamTextLayoutInput Italic = MakeInput(Regular, TEXT("<i>m</i>"));
	Italic.bRichText = true;
	FDreamTextDisplayList ItalicDL;
	FDreamTextLayoutEngine::Layout(Italic, ItalicDL);
	if (TestEqual(TEXT("one italic glyph"), ItalicDL.Items.Num(), 1))
	{
		TestTrue(TEXT("with no italic face, italic is still made up"), ItalicDL.Items[0].Style.bSyntheticItalic);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShaperJoinsAcrossRunsTest,
	"DreamGUI.Text.Shaper.ALetterJoinsItsNeighbourAcrossACutBetweenRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Each run was shaped as if nothing stood around it, so a cut the script does not make -- a tag's edge while ligatures
 * are on, a size or weight that changes mid-word -- left the Arabic letters on either side of it in their isolated and
 * final forms. Every run is shaped with the paragraph around it as context, as a browser shapes it, and the letters
 * join across the cut.
 */
bool FDreamTextShaperJoinsAcrossRunsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	if (!TestTrue(TEXT("Noto Naskh shapes"), FDreamTextShaper::CanShape(Font)))return false;

	// The glyph that draws an element: of the glyphs of its cluster, the one that advances, since a joiner riding with a
	// letter is drawn as a glyph of no width.
	auto GlyphOf = [](const TArray<FDreamShapedRun>& Runs, int32 ElementIndex)
	{
		uint32 Found = 0;
		float FoundAdvance = -1.0f;
		for (const FDreamShapedRun& Run : Runs)
		{
			for (const FDreamShapedGlyph& Glyph : Run.Glyphs)
			{
				if (Glyph.ElementIndex == ElementIndex && Glyph.XAdvance > FoundAdvance)
				{
					Found = Glyph.GlyphIndex;
					FoundAdvance = Glyph.XAdvance;
				}
			}
		}
		return Found;
	};
	auto Shape = [Font](const TArray<FDreamShapeElement>& InElements)
	{
		TArray<FDreamShapedRun> Runs;
		bool bRightToLeft = false;
		FDreamTextShaper::ShapeParagraph(InElements, Font, true, EDreamTextFlowDirection::Auto, Runs, bRightToLeft);
		return Runs;
	};
	// The meem alone, and joined to nothing but a zero-width joiner: its isolated and its initial form, with no letter
	// after it for a contextual alternate to look at. The ra alone is in its isolated form.
	const TArray<FDreamShapedRun> IsolatedMeem = Shape(Elements(TEXT("م")));
	const TArray<FDreamShapedRun> InitialMeem = Shape(Elements(TEXT("م\U0000200D")));
	const TArray<FDreamShapedRun> IsolatedRa = Shape(Elements(TEXT("ر")));
	if (!TestNotEqual(TEXT("the font draws the initial meem apart from the isolated one"), GlyphOf(InitialMeem, 0), GlyphOf(IsolatedMeem, 0)))return false;

	// A cut after the meem, where the end of a tag puts one while ligatures are on.
	const FString Word = TEXT("مرحبا");
	TArray<FDreamShapeElement> CutElements = Elements(Word);
	CutElements[1].bRunBreakBefore = true;
	const TArray<FDreamShapedRun> Cut = Shape(CutElements);
	if (!TestEqual(TEXT("the cut makes two runs"), Cut.Num(), 2))return false;
	TestEqual(TEXT("cut off from its word, the meem still takes its initial form"), (int32)GlyphOf(Cut, 0), (int32)GlyphOf(InitialMeem, 0));
	TestNotEqual(TEXT("and the ra after the cut still joins to it"), GlyphOf(Cut, 1), GlyphOf(IsolatedRa, 0));
	bool bGlyphsInTheirRuns = true;
	for (const FDreamShapedRun& Run : Cut)
	{
		for (const FDreamShapedGlyph& Glyph : Run.Glyphs)
		{
			bGlyphsInTheirRuns &= Glyph.ElementIndex >= Run.ElementStart && Glyph.ElementIndex < Run.ElementEnd;
		}
	}
	TestTrue(TEXT("every glyph names an element of its own run"), bGlyphsInTheirRuns);

	// Through the layout: a first letter in a colour of its own is drawn joined, the same glyph as in the plain word.
	FDreamTextLayoutInput Rich = MakeInput(Font, TEXT("<color=red>م</color>رحبا"));
	Rich.bRichText = true;
	Rich.bAllowLigatures = true;
	FDreamTextLayoutInput Plain = MakeInput(Font, Word);
	Plain.bAllowLigatures = true;
	FDreamTextDisplayList RichDL;
	FDreamTextDisplayList PlainDL;
	FDreamTextLayoutEngine::Layout(Rich, RichDL);
	FDreamTextLayoutEngine::Layout(Plain, PlainDL);
	auto FindFirstLetter = [](const FDreamTextDisplayList& DL) -> const FDreamTextGlyphItem*
	{
		for (const FDreamTextGlyphItem& Item : DL.Items)
		{
			if (Item.ElementIndex == 0 && Item.Kind == EDreamTextItemKind::Glyph)return &Item;
		}
		return nullptr;
	};
	const FDreamTextGlyphItem* RichMeem = FindFirstLetter(RichDL);
	const FDreamTextGlyphItem* PlainMeem = FindFirstLetter(PlainDL);
	if (TestNotNull(TEXT("the coloured meem is laid out"), RichMeem) && TestNotNull(TEXT("the plain meem is laid out"), PlainMeem))
	{
		TestTrue(TEXT("the coloured meem has its colour"), RichMeem->Style.bHasColor);
		TestEqual(TEXT("it advances as the joined meem does"), RichMeem->Glyph.XAdvance, PlainMeem->Glyph.XAdvance, 0.01f);
		TestTrue(TEXT("and is drawn from the joined meem's place in the atlas"),
			RichMeem->Glyph.MinUV == PlainMeem->Glyph.MinUV && RichMeem->Glyph.MaxUV == PlainMeem->Glyph.MaxUV
			&& RichMeem->Glyph.SliceIndex == PlainMeem->Glyph.SliceIndex);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextShapedMarkOnASpaceTest,
	"DreamGUI.Text.Shaper.ACombiningMarkShapedOntoASpaceIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The shaper puts a combining mark written on a space into the space's cluster, and a cluster led by a space drew
 * nothing, so the mark never showed. The space's own glyph draws nothing and the mark's is drawn.
 */
bool FDreamTextShapedMarkOnASpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextShaperTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto shapes"), FDreamTextShaper::CanShape(Font)))return false;
	if (!TestTrue(TEXT("Roboto has the combining acute"), Font->FaceHasCodepoint(0, 0x0301)))return false;

	FDreamTextDisplayList DL;
	FDreamTextLayoutEngine::Layout(MakeInput(Font, TEXT(" \U00000301")), DL);
	FDreamUIGeometry Geometry;
	TArray<FDreamUITextCharProperty> Chars;
	FDreamTextPainter::Paint(DL, FDreamTextPaintParams(), Geometry, Chars);
	TestEqual(TEXT("one glyph quad"), Geometry.OriginVertices.Num(), 4);
	TestEqual(TEXT("for one character"), Chars.Num(), 1);
	return true;
}

#endif
