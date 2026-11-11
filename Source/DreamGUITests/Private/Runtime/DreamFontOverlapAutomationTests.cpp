// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Core/Text/DreamGlyphSdf.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "DreamScopedWorld.h"
#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
THIRD_PARTY_INCLUDES_END
#endif

/*
 * Outlines whose contours overlap: strokes drawn as separate contours that run into each other, as a variable font draws
 * them and a static instance cut from one keeps them unless its overlaps are removed. msdfgen opens gaps and seams in the
 * field where they cross, so the field generator measures the overlap and the font says so once, with what to do about it.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontOverlapMeasureTest,
	"DreamGUI.Text.Font.AnOutlinesOverlappingContoursAreMeasured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Where two strokes of an outline fill the same point -- two contours that overlap, or one that loops back over itself --
 * the nonzero winding there is two. The field generator measures that area on the outline it draws: two 40-unit squares
 * that overlap by a 20-unit square measure 400 square pixels at a pixel a unit, the same squares only touching measure
 * nothing, and one contour that runs round both squares, crossing itself, measures the 400 again. A glyph drawn the usual
 * way -- one contour a shape, holes wound the other way -- measures nothing either.
 */
bool FDreamFontOverlapMeasureTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	struct FCase
	{
		const TCHAR* Name;
		TArray<TArray<FIntPoint>> Contours;
		float MinPixels;
		float MaxPixels;
	};
	// Clockwise with y up, as TrueType winds an outer contour.
	const TArray<FIntPoint> SquareA = { {0, 0}, {0, 40}, {40, 40}, {40, 0} };
	const TArray<FIntPoint> SquareB = { {20, 20}, {20, 60}, {60, 60}, {60, 20} };
	const TArray<FIntPoint> SquareC = { {40, 0}, {40, 40}, {80, 40}, {80, 0} };
	// One contour round both squares, crossing itself at (40, 20) and touching itself at (20, 40).
	const TArray<FIntPoint> Loop = { {0, 0}, {0, 40}, {20, 40}, {20, 60}, {60, 60}, {60, 20}, {20, 20}, {20, 40}, {40, 40}, {40, 0} };
	const FCase Cases[] =
	{
		{ TEXT("two overlapping squares"), { SquareA, SquareB }, 380.0f, 420.0f },
		{ TEXT("two squares that only touch"), { SquareA, SquareC }, 0.0f, 0.0f },
		{ TEXT("one contour looping back over itself"), { Loop }, 380.0f, 420.0f },
	};
	for (const FCase& Case : Cases)
	{
		TArray<FT_Vector> Points;
		// The element types FreeType declares, signed or not by version.
		using FTag = TRemovePointer<decltype(FT_Outline::tags)>::Type;
		using FEnd = TRemovePointer<decltype(FT_Outline::contours)>::Type;
		TArray<FTag> Tags;
		TArray<FEnd> Ends;
		for (const TArray<FIntPoint>& Contour : Case.Contours)
		{
			for (const FIntPoint& Point : Contour)
			{
				Points.Add(FT_Vector{ (FT_Pos)Point.X, (FT_Pos)Point.Y });
				Tags.Add((FTag)FT_CURVE_TAG_ON);
			}
			Ends.Add((FEnd)(Points.Num() - 1));
		}
		FT_Outline Outline = {};
		Outline.n_points = (decltype(Outline.n_points))Points.Num();
		Outline.n_contours = (decltype(Outline.n_contours))Ends.Num();
		Outline.points = Points.GetData();
		Outline.tags = Tags.GetData();
		Outline.contours = Ends.GetData();
		FDreamGlyphSdfResult Field;
		if (!TestTrue(FString::Printf(TEXT("%s: the field generates"), Case.Name), FDreamGlyphSdf::GenerateMTSDFFromOutline(&Outline, 1.0, 4.0f, 0.0f, Field)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: %.1f square pixels overlap, between %.0f and %.0f"), Case.Name, Field.OverlapPixels, Case.MinPixels, Case.MaxPixels),
			Field.OverlapPixels >= Case.MinPixels && Field.OverlapPixels <= Case.MaxPixels);
		TestEqual(FString::Printf(TEXT("%s: and the glyph says whether that is an overlap"), Case.Name), Field.HasOverlappingContours(), Case.MaxPixels > 0.0f);
	}

	// A font that draws each shape as one contour: Roboto's H, and its o with the hole wound the other way.
	TArray<uint8> Bytes;
	FT_Library Library = nullptr;
	if (TestTrue(TEXT("Roboto reads"), FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf"))))
		&& FT_Init_FreeType(&Library) == 0)
	{
		FT_Face Face = nullptr;
		if (TestTrue(TEXT("Roboto opens"), FT_New_Memory_Face(Library, Bytes.GetData(), Bytes.Num(), 0, &Face) == 0))
		{
			for (const TCHAR Char : FString(TEXT("Ho")))
			{
				FDreamGlyphSdfResult Field;
				if (TestTrue(FString::Printf(TEXT("Roboto's %c generates"), Char), FDreamGlyphSdf::GenerateMTSDF(Face, FT_Get_Char_Index(Face, Char), 64.0f, 16.0f, 0.0f, Field)))
				{
					TestEqual(FString::Printf(TEXT("Roboto's %c has no overlapping contours"), Char), Field.OverlapPixels, 0.0f);
				}
			}
			FT_Done_Face(Face);
		}
		FT_Done_FreeType(Library);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontOverlapWarningTest,
	"DreamGUI.Text.Font.AFontWithOverlappingContoursWarnsOnceAndSaysHowToRemoveThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font whose glyphs come with overlapping contours says so once, as its first such glyph is made -- naming the glyph and
 * how to take the overlaps out of the file (fontTools' removeOverlaps) -- and counts every such glyph, made on the spot or
 * on the worker. DroidSansFallback's U+C624 and U+C5B4 run one stroke into another. A copy of it outside the engine is a
 * project's own font and warns; the engine's own copy -- the default font's CJK fallback -- is not the project's to fix,
 * says so in the log alone and counts all the same.
 */
bool FDreamFontOverlapWarningTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	const FString EngineCopy = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/DroidSansFallback.ttf"));
	const FString ProjectCopy = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DreamGUITests"), TEXT("OverlappingContours.ttf"));
	if (!TestTrue(TEXT("a copy of DroidSansFallback outside the engine"), IFileManager::Get().Copy(*ProjectCopy, *EngineCopy) == COPY_OK))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	auto MakeFont = [&TestWorld](const FString& InPath)
	{
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
		Font->SetFontFilePath(InPath, false);
		Font->InitFont();
		Font->PrepareForLayout(0.0f);
		return Font;
	};

	// One warning in all, naming the file, the overlap and the way out of it.
	AddExpectedMessage(TEXT("OverlappingContours\\.ttf.*has overlapping contours.*removeOverlaps"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UDreamUIFontData_DistanceField* Project = MakeFont(ProjectCopy);
	TestEqual(TEXT("a font with nothing made yet has counted nothing"), Project->GetOverlappingContourGlyphCount(), 0);
	TestTrue(TEXT("U+C624 is made on the spot"), Project->GetCharData(0xC624, 64.0f, false).IsValid());
	TestEqual(TEXT("and counted"), Project->GetOverlappingContourGlyphCount(), 1);
	// The next one on the worker: counted as it lands, with no second warning.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	TestTrue(TEXT("U+C5B4 goes to the worker"), Project->GetCharData(0xC5B4, 64.0f, false).bPending);
	Project->WaitForAsyncGlyphs();
	TestEqual(TEXT("and is counted as it lands"), Project->GetOverlappingContourGlyphCount(), 2);
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	TestTrue(TEXT("a glyph with no overlap is made"), Project->GetCharData(0x4E16, 64.0f, false).IsValid());
	TestEqual(TEXT("and not counted"), Project->GetOverlappingContourGlyphCount(), 2);

	// The engine's own copy: counted, with no warning of its own.
	UDreamUIFontData_DistanceField* Shipped = MakeFont(EngineCopy);
	TestTrue(TEXT("the engine's U+C624 is made"), Shipped->GetCharData(0xC624, 64.0f, false).IsValid());
	TestEqual(TEXT("and counted on its font"), Shipped->GetOverlappingContourGlyphCount(), 1);
	IFileManager::Get().Delete(*ProjectCopy, false, true, true);
	return true;
}

#endif
