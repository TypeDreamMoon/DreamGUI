// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformMisc.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUISettings.h"
#include "Core/Text/DreamGlyphSdf.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/DreamGUIObjectVersion.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include "DreamScopedWorld.h"
#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
THIRD_PARTY_INCLUDES_END
#endif

/*
 * The font assets themselves: how a face is opened and kept, what a face is (fallback, bold, italic),
 * what the tables say about its lines, and how glyphs get into the atlas. Real fonts the engine ships
 * (Engine/Content/Slate/Fonts), opened as files the way a project's own font is.
 */
namespace DreamFontDataTestLocal
{
	using DreamTests::FScopedGameWorld;

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	template<typename TFont>
	TFont* MakeFileFont(UWorld* World, const FString& Path)
	{
		// Tests read glyphs back right away, so rasterize them on the spot whatever the frame budget says.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		TFont* Font = NewObject<TFont>(World);
		Font->SetFontFilePath(Path, false);
		Font->InitFont();
		return Font;
	}

	UDreamUIFontData_DistanceField* MakeFieldFont(UWorld* World, const TCHAR* Name)
	{
		return MakeFileFont<UDreamUIFontData_DistanceField>(World, EngineFont(Name));
	}

	/** Set a protected enum property the way the details panel would, before the font is first used. */
	void SetEnumProperty(UObject* Object, const TCHAR* Name, int64 Value)
	{
		if (FEnumProperty* Property = FindFProperty<FEnumProperty>(Object->GetClass(), Name))
		{
			Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), Value);
		}
	}

	/** What happens between two frames, as far as fonts are concerned. */
	void NextFrame()
	{
		GFrameCounter++;
		UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();
	}

#if WITH_FREETYPE
	/** Which contours of a glyph's outline to wind the other way before using it: none, all, or one by index (0, 1, ...). */
	constexpr int32 ReverseNone = -1;
	constexpr int32 ReverseAll = -2;

	/** Reverse one contour of an outline in place, keeping its first point first, as FT_Outline_Reverse does to all. */
	void ReverseContour(FT_Outline& Outline, int32 ContourIndex)
	{
		const int32 First = ContourIndex == 0 ? 0 : (int32)Outline.contours[ContourIndex - 1] + 1;
		const int32 Last = (int32)Outline.contours[ContourIndex];
		for (int32 A = First + 1, B = Last; A < B; A++, B--)
		{
			Swap(Outline.points[A], Outline.points[B]);
			Swap(Outline.tags[A], Outline.tags[B]);
		}
	}

	void ReverseContours(FT_Outline& Outline, int32 Which)
	{
		for (int32 Contour = 0; Contour < (int32)Outline.n_contours; Contour++)
		{
			if (Which == ReverseAll || Which == Contour)
			{
				ReverseContour(Outline, Contour);
			}
		}
	}

	/** FreeType's own fill of a glyph outline, unhinted: the reference for which side of the outline a point is on. */
	struct FCoverage
	{
		TArray<uint8> Pixels;
		int32 Width = 0;
		int32 Rows = 0;
		int32 Left = 0;
		int32 Top = 0;
	};

	/** FreeType's fill of the glyph at PixelsPerEm, with the same contours reversed as the field gets: nonzero, as it fills any outline. */
	bool RenderReference(FT_Face Face, uint32 GlyphIndex, float PixelsPerEm, int32 Reverse, FCoverage& Out)
	{
		if (FT_Set_Char_Size(Face, 0, (FT_F26Dot6)FMath::RoundToInt(PixelsPerEm * 64.0f), 72, 72) != 0)return false;
		if (FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)return false;
		ReverseContours(Face->glyph->outline, Reverse);
		if (FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL) != 0)return false;
		const FT_Bitmap& Bitmap = Face->glyph->bitmap;
		Out.Width = (int32)Bitmap.width;
		Out.Rows = (int32)Bitmap.rows;
		Out.Left = Face->glyph->bitmap_left;
		Out.Top = Face->glyph->bitmap_top;
		Out.Pixels.SetNumZeroed(FMath::Max(Out.Width * Out.Rows, 1));
		for (int32 Row = 0; Row < Out.Rows; Row++)
		{
			UDreamUIFontData_FreeTypeRender::ReadGlyphRow(Bitmap, Row, Out.Pixels.GetData() + Row * Out.Width, Out.Width);
		}
		return true;
	}

	/** The outline field of the same glyph with the same contours reversed: 16 px of spread at PixelsPerEm, as a font's atlas has. */
	bool MakeField(FT_Face Face, uint32 GlyphIndex, float PixelsPerEm, int32 Reverse, FDreamGlyphSdfResult& Out)
	{
		if (FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP) != 0)return false;
		ReverseContours(Face->glyph->outline, Reverse);
		return FDreamGlyphSdf::GenerateMTSDFFromOutline(&Face->glyph->outline, PixelsPerEm / (float)Face->units_per_EM, 16.0f, 0.0f, Out);
	}

	/**
	 * Where a field's inside and FreeType's fill disagree, over the texels FreeType fills completely or leaves empty (a
	 * partly covered texel is on the edge and is not looked at). A texel counts only when the median and the true distance
	 * both disagree, so an msdfgen colour artifact in the median alone is not mistaken for a hole.
	 */
	struct FFillComparison
	{
		int32 Checked = 0;
		/** Filled completely by FreeType, yet more than half a pixel outside in the field: the pixel's centre is at least half a pixel inside. */
		int32 MissingInside = 0;
		/** Left empty by FreeType, yet more than three pixels inside in the field. */
		int32 ExtraInside = 0;
	};

	FFillComparison CompareFill(const FDreamGlyphSdfResult& Field, const FCoverage& Reference)
	{
		// A 16 px spread puts 8 levels in a pixel around 128, the edge.
		FFillComparison Result;
		for (int32 y = 0; y < Field.Height; y++)
		{
			for (int32 x = 0; x < Field.Width; x++)
			{
				// The texel's centre in pixels from the glyph origin, y up; then the reference pixel holding that point.
				const float X = Field.Left + x + 0.5f;
				const float Y = Field.Top - y - 0.5f;
				const int32 Col = FMath::FloorToInt(X - Reference.Left);
				const int32 Row = FMath::FloorToInt(Reference.Top - Y);
				const bool bInReference = Col >= 0 && Col < Reference.Width && Row >= 0 && Row < Reference.Rows;
				const uint8 Cover = bInReference ? Reference.Pixels[Row * Reference.Width + Col] : 0;
				if (Cover != 0 && Cover != 255)
				{
					continue;
				}
				Result.Checked++;
				const uint8* Px = Field.Pixels.GetData() + (y * Field.Width + x) * 4;
				const int32 B = Px[0], G = Px[1], R = Px[2], A = Px[3];
				const int32 Median = FMath::Max(FMath::Min(R, G), FMath::Min(FMath::Max(R, G), B));
				if (Cover == 255 && Median < 124 && A < 124)
				{
					Result.MissingInside++;
				}
				if (Cover == 0 && Median > 152 && A > 152)
				{
					Result.ExtraInside++;
				}
			}
		}
		return Result;
	}

	/** Field and FreeType agree on the inside of the glyph, with the given contours reversed in both. */
	void TestFieldMatchesFreeType(FAutomationTestBase& Test, FT_Face Face, uint32 Codepoint, int32 Reverse)
	{
		const FString Case = Reverse == ReverseNone ? FString::Printf(TEXT("U+%04X"), Codepoint)
			: Reverse == ReverseAll ? FString::Printf(TEXT("U+%04X, every contour reversed"), Codepoint)
			: FString::Printf(TEXT("U+%04X, contour %d reversed"), Codepoint, Reverse);
		const uint32 GlyphIndex = FT_Get_Char_Index(Face, Codepoint);
		const float PixelsPerEm = 64.0f;
		FCoverage Reference;
		FDreamGlyphSdfResult Field;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the font has it and FreeType fills it"), *Case), GlyphIndex != 0 && RenderReference(Face, GlyphIndex, PixelsPerEm, Reverse, Reference)))
		{
			return;
		}
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the field generates"), *Case), MakeField(Face, GlyphIndex, PixelsPerEm, Reverse, Field)))
		{
			return;
		}
		const FFillComparison Fill = CompareFill(Field, Reference);
		Test.TestTrue(FString::Printf(TEXT("%s: the comparison sees the glyph (%d texels)"), *Case, Fill.Checked), Fill.Checked > 500);
		Test.TestTrue(FString::Printf(TEXT("%s: nothing FreeType fills is outside the field (%d texels are)"), *Case, Fill.MissingInside), Fill.MissingInside == 0);
		Test.TestTrue(FString::Printf(TEXT("%s: nothing FreeType leaves empty is inside it (%d texels are)"), *Case, Fill.ExtraInside), Fill.ExtraInside == 0);
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackOnlyAtlasTest,
	"DreamGUI.Text.Font.AFontWhoseOwnFaceFailsStillDrawsItsFallbackGlyphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font whose own file is missing still resolves code points to its fallbacks, but its atlas was only made when its own
 * face loaded. The first fallback glyph found no texture and an empty packer: on the way to growing the atlas a log line
 * read the size of the missing texture and crashed the editor, and with logging compiled out the glyph went to a second
 * slice of a one-slice texture and was never uploaded. Checked both ways a glyph lands, from the worker and on the spot.
 */
bool FDreamFontFallbackOnlyAtlasTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto loads"), Roboto->FaceHasCodepoint(0, 'A')))return false;

	// The missing file is reported when the font first tries to open it.
	AddExpectedErrorPlain(TEXT("not exist"), EAutomationExpectedErrorFlags::Contains, 0);
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	Font->SetFontFilePath(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DreamGUITests_NoSuchFont.ttf")), false);
	Font->SetFallbackFonts({ Roboto });
	TestFalse(TEXT("the font's own face does not load"), Font->FaceHasCodepoint(0, 'A'));
	TestTrue(TEXT("its fallback does"), Font->FaceHasCodepoint(1, 'A'));
	TestNull(TEXT("and it has no atlas yet"), Font->GetFontTexture());
	Font->PrepareForLayout(0.0f);

	// From the worker first, so the glyph reaches the atlas while the font still has none.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	const FDreamUICharData Pending = Font->GetCharData('A', 32.0f, false);
	TestEqual(TEXT("A comes from the fallback face"), Pending.FaceIndex, 1);
	TestTrue(TEXT("and goes to the worker"), Pending.bPending);
	Font->WaitForAsyncGlyphs();
	const FDreamUICharData Landed = Font->GetCharData('A', 32.0f, false);
	TestFalse(TEXT("the worker's glyph landed"), Landed.bPending);
	TestTrue(TEXT("with a quad"), Landed.IsValid() && Landed.Width > 0.0f && Landed.Height > 0.0f);

	// Then on the spot.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	const FDreamUICharData Sync = Font->GetCharData('B', 32.0f, false);
	TestTrue(TEXT("a synchronous fallback glyph has a quad"), Sync.IsValid() && Sync.Width > 0.0f && Sync.Height > 0.0f);

	UTexture2DArray* Atlas = Font->GetFontTexture();
	if (!TestNotNull(TEXT("the font made an atlas for its fallback's glyphs"), Atlas))return false;
	TestTrue(TEXT("both glyphs sit in a slice the atlas has"), Landed.SliceIndex < Atlas->GetArraySize() && Sync.SliceIndex < Atlas->GetArraySize());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontCollectionFaceTest,
	"DreamGUI.Text.Font.ACollectionFaceSurvivesAReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Picking another face of a font collection reloads the font, and every reload went through DeinitFreeType, which set
 * the face index back to 0: a .ttc could only ever show its first face, and a culture switch or a new file path dropped
 * the choice too. Checked on any machine by the index outliving a reset, and on a two-face collection Windows ships
 * (Cambria and Cambria Math) when there is one: the second face is the one opened, before and after a reload.
 */
bool FDreamFontCollectionFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	FIntProperty* FaceProperty = FindFProperty<FIntProperty>(UDreamUIFontData_FreeTypeRender::StaticClass(), TEXT("FontFace"));
	if (!TestNotNull(TEXT("the face index is a property"), FaceProperty))return false;

	// Any machine: a reset keeps the index, and a load clamps it to the faces the file has.
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	FaceProperty->SetPropertyValue_InContainer(Roboto, 1);
	Roboto->SetFontFilePath(EngineFont(TEXT("Roboto-Regular.ttf")), false);
	TestEqual(TEXT("a reset keeps the face index"), FaceProperty->GetPropertyValue_InContainer(Roboto), 1);
	Roboto->InitFont();
	TestEqual(TEXT("a one-face file clamps it when it loads"), FaceProperty->GetPropertyValue_InContainer(Roboto), 0);
	TestTrue(TEXT("and loads instead of failing"), Roboto->FaceHasCodepoint(0, 'A'));

	FString WindowsDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
	const FString Collection = FPaths::Combine(WindowsDir, TEXT("Fonts"), TEXT("cambria.ttc"));
	if (WindowsDir.IsEmpty() || !FPaths::FileExists(Collection))
	{
		AddInfo(TEXT("No two-face collection on this machine; the face index was checked on its own."));
		return true;
	}
#if WITH_FREETYPE
	UDreamUIFontData_DistanceField* Cambria = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	Cambria->SetFontFilePath(Collection, false);
	FaceProperty->SetPropertyValue_InContainer(Cambria, 1);
	Cambria->InitFont();
	FT_FaceRec_* Face = Cambria->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("the collection loads"), Face))return false;
	TestEqual(TEXT("it opened the second face"), (int32)(Face->face_index & 0xFFFF), 1);
	const FString SecondFamily = UTF8_TO_TCHAR(Face->family_name);

	// What the details panel does when the face is picked: the property changes and the font reloads.
	FPropertyChangedEvent Changed(FaceProperty);
	static_cast<UObject*>(Cambria)->PostEditChangeProperty(Changed);
	Face = Cambria->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("the collection reloads"), Face))return false;
	TestEqual(TEXT("the reload opened the second face again"), (int32)(Face->face_index & 0xFFFF), 1);
	TestEqual(TEXT("the same family"), FString(UTF8_TO_TCHAR(Face->family_name)), SecondFamily);
	TestEqual(TEXT("and the index still says so"), FaceProperty->GetPropertyValue_InContainer(Cambria), 1);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontFallbackSwapTest,
	"DreamGUI.Text.Font.SwappingFallbacksDrawsAndMeasuresTheNewFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * SetFallbackFonts only cleared the glyph cache. The worker made for the old fallbacks kept the faces it had opened by
 * index, so a glyph id of the new fallback was drawn from the old fallback's outlines -- a wrong glyph, then cached --
 * and the face-metrics cache went on answering with the old fallback's line box. Checked with every new glyph sent to
 * the worker: the glyph that lands is the one the new face makes on the spot, and face 1 measures as the new face.
 */
bool FDreamFontFallbackSwapTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeFieldFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	// The reference: the same Arabic letter made on the spot by a font that never had another fallback.
	UDreamUIFontData_DistanceField* Reference = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	Reference->SetFallbackFonts({ Arabic });
	for (UDreamUIFontData_DistanceField* Font : { Roboto, Droid, Arabic, Reference })
	{
		Font->PrepareForLayout(0.0f);
	}
	const uint32 Meem = 0x0645;
	const FDreamUICharData Expected = Reference->GetCharData(Meem, 64.0f, false);
	if (!TestTrue(TEXT("the reference glyph has a quad"), Expected.IsValid() && Expected.Width > 0.0f))return false;
	float ExpectedAscent = 0.0f, ExpectedDescent = 0.0f, ExpectedLineHeight = 0.0f;
	if (!TestTrue(TEXT("the Arabic face measures"), Arabic->GetFaceMetrics(0, 32.0f, ExpectedAscent, ExpectedDescent, ExpectedLineHeight)))return false;

	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };

	// The worker and the metrics cache, both made over the first fallback.
	Roboto->SetFallbackFonts({ Droid });
	const FDreamUICharData Ideograph = Roboto->GetCharData(0x4E16, 64.0f, false);
	TestTrue(TEXT("a CJK glyph goes to the worker"), Ideograph.bPending);
	Roboto->WaitForAsyncGlyphs();
	float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
	TestTrue(TEXT("face 1 measures as the CJK face"), Roboto->GetFaceMetrics(1, 32.0f, Ascent, Descent, LineHeight));

	// Swap the fallback; the Arabic letter goes to the worker too.
	Roboto->SetFallbackFonts({ Arabic });
	const FDreamUICharData Pending = Roboto->GetCharData(Meem, 64.0f, false);
	TestEqual(TEXT("the Arabic letter is on face 1"), Pending.FaceIndex, 1);
	TestTrue(TEXT("and goes to the worker"), Pending.bPending);
	Roboto->WaitForAsyncGlyphs();
	const FDreamUICharData Landed = Roboto->GetCharData(Meem, 64.0f, false);
	if (!TestFalse(TEXT("it landed"), Landed.bPending))return false;
	TestEqual(TEXT("its quad is the new face's: width"), Landed.Width, Expected.Width, 0.01f);
	TestEqual(TEXT("height"), Landed.Height, Expected.Height, 0.01f);
	TestEqual(TEXT("left"), Landed.XOffset, Expected.XOffset, 0.01f);
	TestEqual(TEXT("top"), Landed.YOffset, Expected.YOffset, 0.01f);
	TestEqual(TEXT("advance"), Landed.XAdvance, Expected.XAdvance, 0.01f);

	TestTrue(TEXT("face 1 still measures"), Roboto->GetFaceMetrics(1, 32.0f, Ascent, Descent, LineHeight));
	TestEqual(TEXT("face 1's ascent is the new fallback's"), Ascent, ExpectedAscent, 0.001f);
	TestEqual(TEXT("descent"), Descent, ExpectedDescent, 0.001f);
	TestEqual(TEXT("line height"), LineHeight, ExpectedLineHeight, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontBitmapBoldRatioTest,
	"DreamGUI.Text.Font.ABitmapFontReportsTheBoldItsRasterAdds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A bitmap font emboldens its raster -- FreeType's embolden by BoldRatio of the size, which widens the outline and the
 * advance by that much -- but reported a bold ratio of 0, so a shaped bold run kept the regular advances and its letters
 * crowded into each other. The ratio it reports must be what its bold raster adds to an advance, per em.
 */
bool FDreamFontBitmapBoldRatioTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_Bitmap* Font = MakeFileFont<UDreamUIFontData_Bitmap>(TestWorld.World, EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'm')))return false;
	const float Size = 32.0f;
	const FDreamUICharData Regular = Font->GetCharData('m', Size, false);
	const FDreamUICharData Bold = Font->GetCharData('m', Size, true);
	if (!TestTrue(TEXT("both glyphs rasterize"), Regular.IsValid() && Bold.IsValid()))return false;
	TestTrue(TEXT("the font reports a bold ratio"), Font->GetBoldRatio() > 0.0f);
	// The raster adds the bold to a 26.6 advance, so it is good to a 64th of a pixel.
	TestEqual(TEXT("and it is what the bold raster adds to the advance, per em"), Bold.XAdvance - Regular.XAdvance, Size * Font->GetBoldRatio(), 0.02f);
	TestTrue(TEXT("the bold raster is wider too"), Bold.Width > Regular.Width);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontOldAssetFieldTest,
	"DreamGUI.Text.Font.AnOldDistanceFieldFontLoadsAsAnOutlineField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A distance-field font saved before fonts chose their field has no SdfSource, and loading it forced the field derived
 * from a bitmap: one channel, which rounds every corner by a texel or two, the main reason text corners came out round.
 * It now keeps the class default, the outline field, and the editor names the asset once so it can be resaved.
 */
bool FDreamFontOldAssetFieldTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Saved = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	TArray<uint8> Bytes;
	FObjectWriter Writer(Saved, Bytes);

	UDreamUIFontData_DistanceField* Loaded = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	AddExpectedMessagePlain(TEXT("resave it"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	{
		// Read back as a package of that age is: persistent, and older than the version that added the property.
		FObjectReader Reader(Bytes);
		Reader.SetIsPersistent(true);
		Reader.SetCustomVersion(FDreamGUIObjectVersion::GUID, FDreamGUIObjectVersion::BeforeCustomVersionWasAdded, TEXT("DreamGUIObjectVersion"));
		static_cast<UObject*>(Loaded)->Serialize(Reader);
	}
	TestEqual(TEXT("the old font is an outline field"), (int32)Loaded->GetSdfSource(), (int32)EDreamUISdfSource::OutlineMultiChannel);
	TestEqual(TEXT("and its texts decode it as one"), (int32)Loaded->GetFontTextureMark(), (int32)EDreamUIFontTextureMark::Mtsdf);

	// A copy made in memory reads with the versions registered now (a loading archive with none of its own takes them),
	// so it never looked old, before or after the change. This only shows it stays an outline field and does not warn:
	// the expected warning above is counted exactly once.
	UDreamUIFontData_DistanceField* Copy = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	{
		FObjectReader Reader(Copy, Bytes);
	}
	TestEqual(TEXT("an in-memory copy is an outline field too"), (int32)Copy->GetSdfSource(), (int32)EDreamUISdfSource::OutlineMultiChannel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontContourWindingTest,
	"DreamGUI.Text.Font.AnOutlineFieldFillsWhatFreeTypeFillsWhicheverWayItsContoursAreWound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The outline field took every contour's winding as the font gave it, and msdfgen reads a contour wound against the
 * rest as a hole in nothing: the dot of an i wound the other way vanished from the field. FreeType fills an outline by
 * the nonzero rule, under which that dot is still filled -- and under which a counter wound like its outer contour is
 * filled in, so reversing one contour is not always only a change of direction. Every case is checked against
 * FreeType's own fill of the same outline with the same contours reversed: as Roboto has it; with every contour
 * reversed (a glyph wound backwards as a whole); and with each contour reversed on its own -- the dot or the stem of i,
 * a bar of = (the fill is unchanged, and the field must repair the winding), the counters of O and 8 (FreeType fills
 * them in, and the field must not punch them back out).
 */
bool FDreamFontContourWindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
#if WITH_FREETYPE
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	FT_FaceRec_* Face = Font->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("Roboto loads"), Face))return false;
	for (const uint32 Codepoint : { uint32('i'), uint32('='), uint32('O'), uint32('8') })
	{
		TestFieldMatchesFreeType(*this, Face, Codepoint, ReverseNone);
		TestFieldMatchesFreeType(*this, Face, Codepoint, ReverseAll);
		const uint32 GlyphIndex = FT_Get_Char_Index(Face, Codepoint);
		if (GlyphIndex == 0 || FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
		{
			AddError(FString::Printf(TEXT("U+%04X does not load"), Codepoint));
			continue;
		}
		const int32 ContourCount = (int32)Face->glyph->outline.n_contours;
		TestTrue(FString::Printf(TEXT("U+%04X has more than one contour"), Codepoint), ContourCount >= 2);
		for (int32 Contour = 0; Contour < ContourCount; Contour++)
		{
			TestFieldMatchesFreeType(*this, Face, Codepoint, Contour);
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no outline fields."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontOverlappingContoursTest,
	"DreamGUI.Text.Font.OverlappingContoursStayFilledWhereTheyOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * TrueType contours may overlap: Roboto builds a and e with ogonek (U+0105, U+0119) from the letter and an ogonek that
 * runs into it, and DroidSansFallback draws Hangul syllables such as U+C624 with strokes that cross. FreeType fills
 * them by the nonzero rule, so an overlap is simply filled. msdfgen's orientContours reads windings by the even-odd
 * rule and turned some of these correctly wound contours round, which made the overlap a hole in the field. A
 * reorientation is now kept only when the nonzero fill is unchanged. Checked against FreeType's own fill of each glyph:
 * every texel FreeType fills completely, the overlaps included, must be inside the field.
 */
bool FDreamFontOverlappingContoursTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
#if WITH_FREETYPE
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	FT_FaceRec_* RobotoFace = Roboto->GetFreeTypeFace(0);
	FT_FaceRec_* DroidFace = Droid->GetFreeTypeFace(0);
	if (!TestTrue(TEXT("both fonts load"), RobotoFace != nullptr && DroidFace != nullptr))return false;
	for (const uint32 Codepoint : { 0x0105u, 0x0119u })
	{
		TestFieldMatchesFreeType(*this, RobotoFace, Codepoint, ReverseNone);
	}
	for (const uint32 Codepoint : { 0xC624u, 0xD68Cu, 0xD64Du, 0xC62Cu })
	{
		TestFieldMatchesFreeType(*this, DroidFace, Codepoint, ReverseNone);
	}
#else
	AddInfo(TEXT("Built without FreeType: no outline fields."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontStyleFaceTest,
	"DreamGUI.Text.Font.ABoldFaceIsAFaceOfItsOwnPastTheFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font can now name its real bold, italic and bold-italic faces. They are faces of the font -- shaped, rasterized
 * into its atlas and measured like a fallback -- but with indices past the fallbacks, so a regular run never falls back
 * to a bold face, and each says what it is so the layout does not embolden or slant it a second time. Checked with
 * Roboto Bold as Roboto's bold face, beside a CJK fallback.
 */
bool FDreamFontStyleFaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* RobotoBold = MakeFieldFont(TestWorld.World, TEXT("Roboto-Bold.ttf"));
	UDreamUIFontData_DistanceField* Droid = MakeFieldFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	Roboto->SetFallbackFonts({ Droid });
	Roboto->PrepareForLayout(0.0f);
	const int32 RegularFaces = Roboto->GetFaceCount();
	TestEqual(TEXT("no bold face yet"), Roboto->GetStyledFace(true, false), 0);

	Roboto->SetStyleFonts(RobotoBold, nullptr, nullptr);
	TestEqual(TEXT("a style face is not counted with the fallbacks"), Roboto->GetFaceCount(), RegularFaces);
	const int32 BoldFace = Roboto->GetStyledFace(true, false);
	TestEqual(TEXT("the bold face comes right after them"), BoldFace, RegularFaces);
	TestEqual(TEXT("italic has no face"), Roboto->GetStyledFace(false, true), 0);
	TestEqual(TEXT("bold-italic takes the bold face"), Roboto->GetStyledFace(true, true), BoldFace);
	TestEqual(TEXT("regular takes the font's own"), Roboto->GetStyledFace(false, false), 0);
	TestTrue(TEXT("the bold face is bold by design"), Roboto->GetFaceStyleFlags(BoldFace) == EDreamUIFontFaceStyle::Bold);
	TestTrue(TEXT("the font's own face is not"), Roboto->GetFaceStyleFlags(0) == EDreamUIFontFaceStyle::None);
	TestTrue(TEXT("nor is the fallback"), Roboto->GetFaceStyleFlags(1) == EDreamUIFontFaceStyle::None);
	TestTrue(TEXT("the bold face has Latin"), Roboto->FaceHasCodepoint(BoldFace, 'W'));
	TestNotNull(TEXT("and shapes"), Roboto->GetShapingFont(BoldFace, 32.0f));
	FDreamUIGlyphKey Key;
	TestTrue(TEXT("W resolves"), Roboto->ResolveCodepoint('W', Key));
	TestEqual(TEXT("a regular W comes from the font's own face, never the bold one"), Key.FaceIndex, 0);

#if WITH_FREETYPE
	// Its glyphs are its own: the bold W is the bold face's outline with the bold face's advance.
	FT_FaceRec_* RegularFt = Roboto->GetFreeTypeFace(0);
	FT_FaceRec_* BoldFt = Roboto->GetFreeTypeFace(BoldFace);
	if (!TestTrue(TEXT("both faces are open"), RegularFt != nullptr && BoldFt != nullptr && RegularFt != BoldFt))return false;
	const uint32 RegularW = FT_Get_Char_Index(RegularFt, 'W');
	const uint32 BoldW = FT_Get_Char_Index(BoldFt, 'W');
	auto DesignAdvance = [](FT_FaceRec_* InFace, uint32 InGlyph, float InSize)
	{
		FT_Load_Glyph(InFace, InGlyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
		return (float)(InFace->glyph->metrics.horiAdvance * (double)InSize / (double)InFace->units_per_EM);
	};
	const float Size = 32.0f;
	const FDreamUICharData Regular = Roboto->GetGlyphData(0, RegularW, Size, false);
	const FDreamUICharData Bold = Roboto->GetGlyphData(BoldFace, BoldW, Size, false);
	if (!TestTrue(TEXT("both Ws rasterize into the font's atlas"), Regular.IsValid() && Bold.IsValid()))return false;
	TestEqual(TEXT("the bold W says which face it came from"), Bold.FaceIndex, BoldFace);
	TestEqual(TEXT("the regular W has Roboto's advance"), Regular.XAdvance, DesignAdvance(RegularFt, RegularW, Size), 0.01f);
	TestEqual(TEXT("the bold W has Roboto Bold's"), Bold.XAdvance, DesignAdvance(BoldFt, BoldW, Size), 0.01f);
	TestTrue(TEXT("which is not Roboto's"), FMath::Abs(Bold.XAdvance - Regular.XAdvance) > 0.3f);
	float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
	TestTrue(TEXT("the bold face measures"), Roboto->GetFaceMetrics(BoldFace, Size, Ascent, Descent, LineHeight));
	float UnderlinePosition = 0.0f, UnderlineThickness = 0.0f, StrikePosition = 0.0f, StrikeThickness = 0.0f;
	TestTrue(TEXT("and has decorations"), Roboto->GetDecorationMetrics(BoldFace, Size, UnderlinePosition, UnderlineThickness, StrikePosition, StrikeThickness));
#endif

	// A font is never its own style face.
	Roboto->SetStyleFonts(Roboto, nullptr, nullptr);
	TestEqual(TEXT("a font is not its own bold face"), Roboto->GetStyledFace(true, false), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontDecorationMetricsTest,
	"DreamGUI.Text.Font.UnderlineAndStrikethroughComeFromTheFontTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Decorations were drawn from a glyph of the font ('_' and '-'), not from where the font says its underline and
 * strikethrough go. The font now reports both: the underline from 'post' (FreeType gives its centre), the strikethrough
 * from OS/2 (whose position is the stroke's top), scaled linearly, in the pixels its other metrics are in.
 */
bool FDreamFontDecorationMetricsTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	Roboto->PrepareForLayout(0.0f);
	const float Size = 32.0f;
	float UnderlinePosition = 0.0f, UnderlineThickness = 0.0f, StrikePosition = 0.0f, StrikeThickness = 0.0f;
	if (!TestTrue(TEXT("Roboto has decoration metrics"), Roboto->GetDecorationMetrics(0, Size, UnderlinePosition, UnderlineThickness, StrikePosition, StrikeThickness)))return false;
	TestTrue(TEXT("the underline is below the baseline"), UnderlinePosition < 0.0f);
	TestTrue(TEXT("and thinner than 4 px"), UnderlineThickness > 0.0f && UnderlineThickness < 4.0f);
	float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
	if (!TestTrue(TEXT("Roboto measures"), Roboto->GetFaceMetrics(0, Size, Ascent, Descent, LineHeight)))return false;
	TestTrue(TEXT("the strikethrough is above the baseline"), StrikePosition > 0.0f);
	TestTrue(TEXT("and below the ascent"), StrikePosition < Ascent);
	TestTrue(TEXT("and thinner than 4 px"), StrikeThickness > 0.0f && StrikeThickness < 4.0f);

#if WITH_FREETYPE
	FT_FaceRec_* Face = Roboto->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("Roboto is open"), Face))return false;
	const float Scale = Size / (float)Face->units_per_EM;
	TestEqual(TEXT("the underline is FreeType's centre of the post table's"), UnderlinePosition, Face->underline_position * Scale, 0.001f);
	TestEqual(TEXT("as thick as the post table says"), UnderlineThickness, Face->underline_thickness * Scale, 0.001f);
	const TT_OS2* OS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(Face, FT_SFNT_OS2));
	if (TestNotNull(TEXT("Roboto has an OS/2 table"), OS2))
	{
		TestEqual(TEXT("the strikethrough is as thick as OS/2 says"), StrikeThickness, OS2->yStrikeoutSize * Scale, 0.001f);
		TestEqual(TEXT("and its centre is half that below OS/2's top"), StrikePosition, (OS2->yStrikeoutPosition - OS2->yStrikeoutSize * 0.5f) * Scale, 0.001f);
	}
#endif
	// Twice the size, twice the numbers: they scale linearly.
	float UnderlinePosition2 = 0.0f, UnderlineThickness2 = 0.0f, StrikePosition2 = 0.0f, StrikeThickness2 = 0.0f;
	Roboto->GetDecorationMetrics(0, Size * 2.0f, UnderlinePosition2, UnderlineThickness2, StrikePosition2, StrikeThickness2);
	TestEqual(TEXT("they scale with the size"), StrikePosition2, StrikePosition * 2.0f, 0.001f);
	TestFalse(TEXT("a face the font does not have has none"), Roboto->GetDecorationMetrics(9, Size, UnderlinePosition2, UnderlineThickness2, StrikePosition2, StrikeThickness2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontVerticalMetricsTest,
	"DreamGUI.Text.Font.EachVerticalMetricsSourceReadsItsOwnTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font's line box came from FreeType's size metrics alone (hhea, grid-fitted). A font can now say which of its tables
 * to build it from, the way browsers on different platforms do. Checked on Roboto against the tables themselves: the
 * default is exactly what FreeType's size metrics were, and every other choice is its table scaled to the size.
 */
bool FDreamFontVerticalMetricsTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
#if WITH_FREETYPE
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Roboto = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	FT_FaceRec_* Face = Roboto->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("Roboto is open"), Face))return false;
	const float Size = 32.0f;

	float Ascent = 0.0f, Descent = 0.0f, LineHeight = 0.0f;
	if (!TestTrue(TEXT("Roboto measures"), Roboto->ComputeFaceMetrics(0, Size, Ascent, Descent, LineHeight)))return false;
	// What the font measured before there was a choice: FreeType's size metrics at this size.
	FT_Set_Char_Size(Face, 0, (FT_F26Dot6)FMath::RoundToInt(Size * 64.0f), 72, 72);
	TestEqual(TEXT("the default ascent is FreeType's"), Ascent, Face->size->metrics.ascender / 64.0f, 0.0001f);
	TestEqual(TEXT("the default descent is FreeType's"), Descent, -Face->size->metrics.descender / 64.0f, 0.0001f);
	TestEqual(TEXT("the default line height is FreeType's"), LineHeight, Face->size->metrics.height / 64.0f, 0.0001f);

	const TT_HoriHeader* Hhea = static_cast<const TT_HoriHeader*>(FT_Get_Sfnt_Table(Face, FT_SFNT_HHEA));
	const TT_OS2* OS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(Face, FT_SFNT_OS2));
	if (!TestTrue(TEXT("Roboto has hhea and OS/2"), Hhea != nullptr && OS2 != nullptr))return false;
	const float Scale = Size / (float)Face->units_per_EM;
	const float HheaAscent = Hhea->Ascender, HheaDescent = -Hhea->Descender, HheaGap = Hhea->Line_Gap;
	const float TypoAscent = OS2->sTypoAscender, TypoDescent = -OS2->sTypoDescender, TypoGap = OS2->sTypoLineGap;
	const float WinAscent = OS2->usWinAscent, WinDescent = OS2->usWinDescent;
	const bool bUseTypo = (OS2->fsSelection & (1 << 7)) != 0;
	const float ExternalLeading = FMath::Max(0.0f, (HheaAscent + HheaDescent + HheaGap) - (WinAscent + WinDescent));
	struct FExpected
	{
		EDreamUIFontVerticalMetrics Source;
		const TCHAR* Name;
		float Ascent;
		float Descent;
		float LineHeight;
	};
	const FExpected Expectations[] =
	{
		{ EDreamUIFontVerticalMetrics::Hhea, TEXT("hhea"), HheaAscent, HheaDescent, HheaAscent + HheaDescent + HheaGap },
		{ EDreamUIFontVerticalMetrics::Typo, TEXT("typo"), TypoAscent, TypoDescent, TypoAscent + TypoDescent + TypoGap },
		{ EDreamUIFontVerticalMetrics::Win, TEXT("win"), WinAscent, WinDescent, WinAscent + WinDescent },
		{ EDreamUIFontVerticalMetrics::Platform, TEXT("platform"),
			bUseTypo ? TypoAscent : WinAscent, bUseTypo ? TypoDescent : WinDescent,
			bUseTypo ? TypoAscent + TypoDescent + TypoGap : WinAscent + WinDescent + ExternalLeading },
	};
	for (const FExpected& Expected : Expectations)
	{
		Roboto->SetVerticalMetrics(Expected.Source);
		if (!TestTrue(FString::Printf(TEXT("%s: Roboto measures"), Expected.Name), Roboto->ComputeFaceMetrics(0, Size, Ascent, Descent, LineHeight)))continue;
		TestEqual(*FString::Printf(TEXT("%s: ascent"), Expected.Name), Ascent, Expected.Ascent * Scale, 0.001f);
		TestEqual(*FString::Printf(TEXT("%s: descent"), Expected.Name), Descent, Expected.Descent * Scale, 0.001f);
		TestEqual(*FString::Printf(TEXT("%s: line height"), Expected.Name), LineHeight, Expected.LineHeight * Scale, 0.001f);
	}

	// The distance-field font measures at its sample size and scales: an unrounded source scales to the same numbers.
	Roboto->SetVerticalMetrics(EDreamUIFontVerticalMetrics::Typo);
	Roboto->PrepareForLayout(0.0f);
	TestTrue(TEXT("the field font measures"), Roboto->GetFaceMetrics(0, Size, Ascent, Descent, LineHeight));
	TestEqual(TEXT("its typo ascent scales from its sample size"), Ascent, TypoAscent * Scale, 0.001f);
	TestEqual(TEXT("its typo line height too"), LineHeight, (TypoAscent + TypoDescent + TypoGap) * Scale, 0.001f);
#else
	AddInfo(TEXT("Built without FreeType: no font tables."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontEmojiBindingTest,
	"DreamGUI.Text.Font.AnEmojiEditReachesTheTextsOfALoadedFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A font listened to its emoji asset from PostInitProperties, which runs before a loaded asset's properties are read:
 * for every font that came from disk EmojiData was still empty there, so nothing was bound and an edit of the emoji
 * asset never refreshed a text. Checked by giving the font its emoji asset the way loading does, after PostInitProperties.
 */
bool FDreamFontEmojiBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontEmojiData* Emoji = NewObject<UDreamUIFontEmojiData>(TestWorld.World);
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	FObjectProperty* EmojiProperty = FindFProperty<FObjectProperty>(UDreamUIFontData_BaseObject::StaticClass(), TEXT("EmojiData"));
	if (!TestNotNull(TEXT("the emoji asset is a property"), EmojiProperty))return false;
	EmojiProperty->SetObjectPropertyValue_InContainer(Font, Emoji);
	Font->SetFlags(RF_NeedPostLoad);
	Font->ConditionalPostLoad();

	int32 Refreshes = 0;
	Font->OnEmojiDataChanged.AddLambda([&Refreshes]() { Refreshes++; });
	Emoji->BroadcastOnDataChange();
	TestEqual(TEXT("an emoji edit refreshes the font's texts, once"), Refreshes, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontBitmapFractionalSizeTest,
	"DreamGUI.Text.Font.ABitmapGlyphIsRasterizedAtTheFractionalSizeAskedFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A bitmap font set its raster size with FT_Set_Pixel_Sizes, which takes whole pixels, and cached glyphs under the size
 * cut to a uint16, while the shaper places them at the fractional size: a 16.9 px glyph was the 16 px one, drawn smaller
 * than its advance. The raster now asks FreeType for the size with its fraction, and the cache keeps sizes apart.
 */
bool FDreamFontBitmapFractionalSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_Bitmap* Font = MakeFileFont<UDreamUIFontData_Bitmap>(TestWorld.World, EngineFont(TEXT("Roboto-Regular.ttf")));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'W')))return false;
	const FDreamUICharData Whole = Font->GetCharData('W', 16.0f, false);
	const FDreamUICharData Fraction = Font->GetCharData('W', 16.9f, false);
	if (!TestTrue(TEXT("both rasterize"), Whole.IsValid() && Fraction.IsValid()))return false;
	TestTrue(TEXT("16.9 px is a glyph of its own, not the 16 px one"), Whole.MinUV != Fraction.MinUV || Whole.SliceIndex != Fraction.SliceIndex);
#if WITH_FREETYPE
	// FreeType's own hinted advance at each size, loaded as the font loads it.
	FT_FaceRec_* Face = Font->GetFreeTypeFace(0);
	if (!TestNotNull(TEXT("Roboto is open"), Face))return false;
	const uint32 Glyph = FT_Get_Char_Index(Face, 'W');
	auto HintedAdvance = [Face, Glyph](float InSize)
	{
		FT_Set_Char_Size(Face, 0, (FT_F26Dot6)FMath::RoundToInt(InSize * 64.0f), 72, 72);
		FT_Load_Glyph(Face, Glyph, FT_LOAD_DEFAULT);
		return Face->glyph->metrics.horiAdvance / 64.0f;
	};
	const float WholeAdvance = HintedAdvance(16.0f);
	const float FractionAdvance = HintedAdvance(16.9f);
	if (!TestNotEqual(TEXT("the two sizes have different advances in FreeType"), WholeAdvance, FractionAdvance))return false;
	TestEqual(TEXT("the 16 px glyph has FreeType's 16 px advance"), Whole.XAdvance, WholeAdvance, 0.001f);
	TestEqual(TEXT("the 16.9 px glyph has FreeType's 16.9 px advance"), Fraction.XAdvance, FractionAdvance, 0.001f);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontEngineFontCookTest,
	"DreamGUI.Text.Font.CookingAnEngineFontWithNoFaceReportsInsteadOfCrashing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Cooking a font set to use an engine font face read the face without looking: a font asset with the type set and no
 * face assigned crashed the cook. It now names the asset and cooks it with no font data.
 */
bool FDreamFontEngineFontCookTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
#if WITH_EDITOR
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	Font->SetFontType(EDreamUIDynamicFontDataType::EngineFont);
	Font->SetEngineFont(nullptr);
	AddExpectedErrorPlain(TEXT("has none set"), EAutomationExpectedErrorFlags::Contains, 1);
	Font->BeginCacheForCookedPlatformData(nullptr);
	TestTrue(TEXT("the cook goes on"), true);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontAtlasFlushTimingTest,
	"DreamGUI.Text.Font.AFullAtlasIsFlushedBetweenFramesNotInTheMiddleOfOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A full atlas was flushed on the spot, inside whichever layout asked for one glyph too many: every glyph handed out
 * earlier in that frame -- the same text's first letters, every text laid out before it -- pointed into an atlas that no
 * longer held it until each laid out again. And when the glyphs on screen needed more than the budget, it was flushed
 * again every frame. Now the atlas grows past its budget for the rest of the frame and is flushed before the next
 * frame's first glyph; a flush that would only throw away what is being refilled raises the budget instead.
 */
bool FDreamFontAtlasFlushTimingTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
	const int32 SavedSlices = Settings->MaxFontAtlasSlices;
	Settings->MaxFontAtlasSlices = 2;
	ON_SCOPE_EXIT { Settings->MaxFontAtlasSlices = SavedSlices; };
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);

	// A 256x256 atlas packed as a single cell, and glyphs sampled at 120 px with a 40 px spread: an uppercase letter's
	// field is over 128 texels each way, so every slice holds exactly one.
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(TestWorld.World);
	SetEnumProperty(Font, TEXT("TextureSizeType"), (int64)EDreamUIAtlasTextureSizeType::SIZE_256x256);
	SetEnumProperty(Font, TEXT("RectPackCellSizeType"), (int64)EDreamUIAtlasTextureSizeType::SIZE_256x256);
	if (FIntProperty* SampleSize = FindFProperty<FIntProperty>(Font->GetClass(), TEXT("SampleFontSize")))
	{
		SampleSize->SetPropertyValue_InContainer(Font, 120);
	}
	if (FIntProperty* Radius = FindFProperty<FIntProperty>(Font->GetClass(), TEXT("SDFRadius")))
	{
		Radius->SetPropertyValue_InContainer(Font, 40);
	}
	Font->SetFontFilePath(EngineFont(TEXT("Roboto-Regular.ttf")), false);
	Font->InitFont();
	Font->PrepareForLayout(0.0f);
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'A')))return false;
	// The atlas the font started with settles: frames go by with nothing on the worker.
	NextFrame();
	NextFrame();
	NextFrame();

	auto AskForLetters = [Font]()
	{
		Font->GetCharData('B', 64.0f, false);
		Font->GetCharData('C', 64.0f, false);
	};

	// One frame asks for three glyphs, one more than the two-slice budget holds.
	const FDreamUICharData First = Font->GetCharData('A', 64.0f, false);
	AskForLetters();
	if (!TestNotNull(TEXT("the font has an atlas"), Font->GetFontTexture()))return false;
	TestEqual(TEXT("the atlas grew past its budget instead of flushing in the middle of the frame"), Font->GetFontTexture()->GetArraySize(), 3);
	const FDreamUICharData FirstAgain = Font->GetCharData('A', 64.0f, false);
	TestTrue(TEXT("and the frame's first glyph is still where it was handed out"), FirstAgain.MinUV == First.MinUV && FirstAgain.SliceIndex == First.SliceIndex);

	// The next frame's first glyph flushes it.
	NextFrame();
	Font->GetCharData('A', 64.0f, false);
	TestEqual(TEXT("the next frame starts again from one slice"), Font->GetFontTexture()->GetArraySize(), 1);

	// Refilled past the budget straight away: what is on screen needs more than it, so the atlas keeps growing and the
	// frame after does not flush it again.
	AddExpectedMessagePlain(TEXT("need more than"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AskForLetters();
	const int32 Grown = Font->GetFontTexture()->GetArraySize();
	TestEqual(TEXT("the refill grows the atlas past its budget"), Grown, 3);
	NextFrame();
	Font->GetCharData('A', 64.0f, false);
	TestEqual(TEXT("and the frame after keeps it instead of flushing every frame"), Font->GetFontTexture()->GetArraySize(), Grown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontExpandedQuadStaysInItsCellTest,
	"DreamGUI.Text.Font.AnExpandedGlyphQuadNeverLeavesItsAtlasCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text's ExpandMeshSize keeps that much of each glyph's field around its quad, but a glyph's atlas cell holds only
 * SDFRadius texels of field around the glyph. A larger value grew every quad past its cell, and the text drew pieces of
 * the glyphs next to it in the atlas around each character (61.7 on the default 16-texel field). This checks that a
 * quad grows with the value up to its cell's edge and not a texel further, in size and in UVs.
 */
bool FDreamFontExpandedQuadStaysInItsCellTest::RunTest(const FString& Parameters)
{
	using namespace DreamFontDataTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIFontData_DistanceField* Font = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestTrue(TEXT("Roboto loads"), Font->FaceHasCodepoint(0, 'H')))return false;
	// At the sample size, where a quad's size is in atlas texels.
	auto CharAt = [Font](float InExpand)
	{
		Font->PrepareForLayout(InExpand);
		return Font->GetCharData('H', 64.0f, false);
	};
	const FDreamUICharData Tight = CharAt(0.0f);
	const FDreamUICharData Some = CharAt(4.0f);
	const FDreamUICharData WholeField = CharAt((float)Font->GetSdfRadius());
	const FDreamUICharData Huge = CharAt(61.743332f);
	if (!TestTrue(TEXT("H has a quad"), Tight.IsValid() && Tight.Width > 0.0f && Tight.Height > 0.0f))return false;
	TestTrue(TEXT("a small expansion grows the quad"), Some.Width > Tight.Width && Some.Height > Tight.Height);
	TestTrue(TEXT("...and its UV rect"), Some.MinUV.X < Tight.MinUV.X && Some.MaxUV.X > Tight.MaxUV.X);
	TestEqual(TEXT("an expansion past the field draws the quad of the whole field: its width"), Huge.Width, WholeField.Width);
	TestEqual(TEXT("...its height"), Huge.Height, WholeField.Height);
	TestEqual(TEXT("...its UVs (min x)"), Huge.MinUV.X, WholeField.MinUV.X);
	TestEqual(TEXT("...(min y)"), Huge.MinUV.Y, WholeField.MinUV.Y);
	TestEqual(TEXT("...(max x)"), Huge.MaxUV.X, WholeField.MaxUV.X);
	TestEqual(TEXT("...(max y)"), Huge.MaxUV.Y, WholeField.MaxUV.Y);

	// The cell is the glyph's bounds plus SDFRadius texels on each side; the tight quad sits SDFRadius less 0.02 em inside it.
	UTexture2DArray* Atlas = Font->GetFontTexture();
	if (!TestNotNull(TEXT("the font has an atlas"), Atlas))return false;
	const float TexelUV = 1.0f / (float)FMath::Max(Atlas->GetSizeX(), 1);
	const float CellMarginUV = ((float)Font->GetSdfRadius() - 64.0f * 0.02f) * TexelUV;
	const float Tolerance = 0.01f * TexelUV;
	TestTrue(TEXT("no quad reaches past its cell on the left"), Tight.MinUV.X - Huge.MinUV.X <= CellMarginUV + Tolerance);
	TestTrue(TEXT("...or on the right"), Huge.MaxUV.X - Tight.MaxUV.X <= CellMarginUV + Tolerance);
	return true;
}

#endif
