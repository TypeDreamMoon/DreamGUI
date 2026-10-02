// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Core/Text/DreamGlyphColor.h"
#if WITH_FREETYPE
THIRD_PARTY_INCLUDES_START
#include <ft2build.h>
#include FT_FREETYPE_H
THIRD_PARTY_INCLUDES_END
#endif

/*
 * The colour glyph rasterizer on its own, against FreeType: the engine's NotoColorEmoji (CBDT, one 32-bit strike at 109
 * ppem), which the editor ships, and Windows' Segoe UI Emoji (COLR layers) where the machine has it. No font asset, atlas
 * or RHI: a face opened on a FreeType library of the test's own, the way the rasterizer's worker opens its faces.
 */
namespace DreamGlyphColorTestLocal
{
#if WITH_FREETYPE
	/** A font file's face on a FreeType library of its own. FreeType reads the bytes in place, so they live as long as the face. */
	struct FTestFace
	{
		TArray<uint8> Bytes;
		FT_Library Library = nullptr;
		FT_Face Face = nullptr;

		explicit FTestFace(const FString& Path)
		{
			if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || Bytes.Num() == 0 || FT_Init_FreeType(&Library) != 0)
			{
				Library = nullptr;
				return;
			}
			if (FT_New_Memory_Face(Library, Bytes.GetData(), Bytes.Num(), 0, &Face) != 0)
			{
				Face = nullptr;
			}
		}

		~FTestFace()
		{
			if (Face != nullptr)
			{
				FT_Done_Face(Face);
			}
			if (Library != nullptr)
			{
				FT_Done_FreeType(Library);
			}
		}

		FTestFace(const FTestFace&) = delete;
		FTestFace& operator=(const FTestFace&) = delete;
	};

	FString NotoColorEmojiPath()
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Editor/Slate/Fonts/NotoColorEmoji.ttf"));
	}

	/** Windows' COLR emoji font, or an empty path when there is no Windows folder to look in. */
	FString SegoeEmojiPath()
	{
		const FString WindowsDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
		return WindowsDir.IsEmpty() ? FString() : FPaths::Combine(WindowsDir, TEXT("Fonts"), TEXT("seguiemj.ttf"));
	}

	/** What NotoColorEmoji's strike holds for U+1F600, as its CBLC/CBDT tables give it: 136 x 128 pixels, its top 101 above the baseline, advancing 136. */
	constexpr double NotoStrikePixelsPerEm = 109.0;
	constexpr int32 NotoGrinningWidth = 136;
	constexpr int32 NotoGrinningHeight = 128;
	constexpr double NotoGrinningTop = 101.0;
	constexpr double NotoGrinningAdvance = 136.0;

	FDreamGlyphColorParams MakeParams(int32 TargetPixelSize, float ReachEm = 0.0f)
	{
		FDreamGlyphColorParams Params;
		Params.TargetPixelSize = TargetPixelSize;
		Params.ReachEm = ReachEm;
		return Params;
	}

	/** Every texel's alpha is at least each of its colour bytes. */
	bool IsPremultiplied(const FDreamGlyphColorResult& Glyph)
	{
		for (int32 Index = 0; Index + 3 < Glyph.Pixels.Num(); Index += 4)
		{
			const uint8* Texel = Glyph.Pixels.GetData() + Index;
			if (Texel[3] < FMath::Max3(Texel[0], Texel[1], Texel[2]))
			{
				return false;
			}
		}
		return true;
	}

	/** Every byte of the outer Padding texels on each side is zero. */
	bool PaddingIsClear(const FDreamGlyphColorResult& Glyph, int32 Padding)
	{
		for (int32 y = 0; y < Glyph.Height; y++)
		{
			for (int32 x = 0; x < Glyph.Width; x++)
			{
				const bool bPadding = x < Padding || y < Padding || x >= Glyph.Width - Padding || y >= Glyph.Height - Padding;
				const uint8* Texel = Glyph.Pixels.GetData() + (y * Glyph.Width + x) * 4;
				if (bPadding && (Texel[0] | Texel[1] | Texel[2] | Texel[3]) != 0)
				{
					return false;
				}
			}
		}
		return true;
	}

	uint64 TotalAlpha(const FDreamGlyphColorResult& Glyph)
	{
		uint64 Total = 0;
		for (int32 Index = 3; Index < Glyph.Pixels.Num(); Index += 4)
		{
			Total += Glyph.Pixels[Index];
		}
		return Total;
	}

	/** How many 30-degree hue bins at least four mostly opaque, clearly coloured texels fall into. */
	int32 CountHues(const FDreamGlyphColorResult& Glyph)
	{
		int32 Bins[12] = {};
		for (int32 Index = 0; Index + 3 < Glyph.Pixels.Num(); Index += 4)
		{
			const uint8* Texel = Glyph.Pixels.GetData() + Index;
			if (Texel[3] < 128)continue;
			// Premultiplied: the colour is the bytes over alpha. Hue is the same in any transfer curve, so no decoding.
			const float Alpha = Texel[3];
			const FLinearColor Hsv = FLinearColor(Texel[2] / Alpha, Texel[1] / Alpha, Texel[0] / Alpha).LinearRGBToHSV();
			if (Hsv.G < 0.3f || Hsv.B < 0.2f)continue;
			Bins[FMath::Clamp(FMath::FloorToInt32(Hsv.R / 30.0f), 0, 11)]++;
		}
		int32 Hues = 0;
		for (const int32 Count : Bins)
		{
			Hues += Count >= 4 ? 1 : 0;
		}
		return Hues;
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorSizeBucketTest,
	"DreamGUI.Text.GlyphColor.SizeBucketsStepByOnePixelThenTwoThenFourThenEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The size a colour glyph is rasterized at: the device size rounded up to a step of 1 px up to 32, 2 px up to 64, 4 px up to
 * 128 and 8 px up to the cap of 256, never below 1. Rounded up, so the GPU only ever shrinks the bitmap, by a step at most;
 * a size a hair over a step (a 4/3 canvas scale times 12 px) stays on it rather than going up one.
 */
bool FDreamGlyphColorSizeBucketTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		float Size;
		int32 Bucket;
	};
	const FCase Cases[] = {
		{ -3.0f, 1 }, { 0.0f, 1 }, { 0.4f, 1 }, { 1.0f, 1 }, { 1.5f, 2 },
		{ 12.0f, 12 }, { 12.3f, 13 }, { 12.0f * 1.33334f, 16 }, { 15.9996f, 16 }, { 31.5f, 32 }, { 32.0f, 32 },
		{ 32.2f, 34 }, { 33.0f, 34 }, { 34.0f, 34 }, { 34.5f, 36 }, { 63.0f, 64 }, { 64.0f, 64 },
		{ 64.5f, 68 }, { 66.0f, 68 }, { 68.0f, 68 }, { 127.0f, 128 }, { 128.0f, 128 },
		{ 129.0f, 136 }, { 136.0f, 136 }, { 250.0f, 256 }, { 256.0f, 256 }, { 300.0f, 256 }, { 100000.0f, 256 },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%.4f px goes to the %d px bucket"), Case.Size, Case.Bucket), FDreamGlyphColor::GetSizeBucket(Case.Size), Case.Bucket);
	}

	// Across the range: never below the size (but for the 1/64 px of slack), never a whole step above it, never decreasing.
	int32 Previous = 0;
	for (float Size = 0.5f; Size <= 300.0f; Size += 0.25f)
	{
		const int32 Bucket = FDreamGlyphColor::GetSizeBucket(Size);
		const float Step = Size <= 32.0f ? 1.0f : Size <= 64.0f ? 2.0f : Size <= 128.0f ? 4.0f : 8.0f;
		const bool bInStep = Size > 256.0f ? Bucket == 256 : (Bucket >= Size - 1.0f / 64.0f && Bucket < Size + Step);
		if (!TestTrue(FString::Printf(TEXT("%.2f px: bucket %d is within a step above it"), Size, Bucket), bInStep && Bucket >= Previous))
		{
			break;
		}
		Previous = Bucket;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorStrikeChoiceTest,
	"DreamGUI.Text.GlyphColor.AStrikeIsChosenExactElseTheSmallestAboveElseTheLargest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Slate's rule for picking an embedded strike: the one at exactly the size, else the smallest larger one (shrunk on the
 * CPU), else the largest (enlarged on the GPU). Checked on a face of four strikes listed out of order, one of them disabled
 * as FreeType disables a strike with impossible dimensions, and on NotoColorEmoji's single 109 ppem strike.
 */
bool FDreamGlyphColorStrikeChoiceTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphColorTestLocal;
	// The strike choice reads only the face's list of strikes, so a face record with nothing else in it will do.
	FT_Bitmap_Size Strikes[5];
	FMemory::Memzero(Strikes);
	const int32 PixelsPerEm[UE_ARRAY_COUNT(Strikes)] = { 64, 20, 160, 32, 0 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Strikes); Index++)
	{
		Strikes[Index].x_ppem = PixelsPerEm[Index] * 64;
		Strikes[Index].y_ppem = PixelsPerEm[Index] * 64;
		Strikes[Index].size = PixelsPerEm[Index] * 64;
	}
	FT_FaceRec Face;
	FMemory::Memzero(Face);
	Face.face_flags = FT_FACE_FLAG_FIXED_SIZES;
	Face.num_fixed_sizes = (FT_Int)UE_ARRAY_COUNT(Strikes);
	Face.available_sizes = Strikes;
	TestEqual(TEXT("32 px: the 32 ppem strike, exactly"), FDreamGlyphColor::ChooseStrike(&Face, 32.0f), 3);
	TestEqual(TEXT("20 px: the 20 ppem strike, exactly"), FDreamGlyphColor::ChooseStrike(&Face, 20.0f), 1);
	TestEqual(TEXT("10 px: the smallest above it, 20"), FDreamGlyphColor::ChooseStrike(&Face, 10.0f), 1);
	TestEqual(TEXT("33 px: the smallest above it, 64"), FDreamGlyphColor::ChooseStrike(&Face, 33.0f), 0);
	TestEqual(TEXT("100 px: the smallest above it, 160"), FDreamGlyphColor::ChooseStrike(&Face, 100.0f), 2);
	TestEqual(TEXT("200 px, above them all: the largest, 160"), FDreamGlyphColor::ChooseStrike(&Face, 200.0f), 2);
	TestEqual(TEXT("0 px: the smallest strike there is, the disabled one not counting"), FDreamGlyphColor::ChooseStrike(&Face, 0.0f), 1);

	FT_FaceRec NoStrikes;
	FMemory::Memzero(NoStrikes);
	TestEqual(TEXT("a face with no strikes has none to choose"), FDreamGlyphColor::ChooseStrike(&NoStrikes, 32.0f), (int32)INDEX_NONE);
	TestEqual(TEXT("nor has no face"), FDreamGlyphColor::ChooseStrike(nullptr, 32.0f), (int32)INDEX_NONE);

	FTestFace Noto(NotoColorEmojiPath());
	if (!TestNotNull(TEXT("the engine's NotoColorEmoji opens"), Noto.Face))return false;
	if (!TestTrue(TEXT("NotoColorEmoji has one strike, at 109 ppem"), Noto.Face->num_fixed_sizes == 1 && Noto.Face->available_sizes[0].y_ppem == 109 * 64))return false;
	for (const float Size : { 12.0f, 109.0f, 200.0f })
	{
		TestEqual(FString::Printf(TEXT("NotoColorEmoji at %.0f px draws from its one strike"), Size), FDreamGlyphColor::ChooseStrike(Noto.Face, Size), 0);
	}
#else
	AddInfo(TEXT("Built without FreeType: no colour glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorStrikeGlyphTest,
	"DreamGUI.Text.GlyphColor.AStrikeGlyphComesBackPremultipliedPaddedAndPlaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * U+1F600 from NotoColorEmoji's 109 ppem strike: a colour bitmap glyph (a space, which the strike leaves out, is not). At
 * sizes below the strike it is shrunk to the size and its texels per em are the size; at or above it, kept as it is at
 * 109. Either way it comes back premultiplied (alpha at least every colour byte), placed at the strike's bearings scaled
 * the same way, and padded by a texel of (0,0,0,0) plus the reach -- here 0, and a tenth of an em.
 */
bool FDreamGlyphColorStrikeGlyphTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphColorTestLocal;
	FTestFace Noto(NotoColorEmojiPath());
	if (!TestNotNull(TEXT("the engine's NotoColorEmoji opens"), Noto.Face))return false;
	const uint32 Grinning = FT_Get_Char_Index(Noto.Face, 0x1F600);
	const uint32 Space = FT_Get_Char_Index(Noto.Face, ' ');
	if (!TestTrue(TEXT("NotoColorEmoji has U+1F600"), Grinning != 0))return false;
	TestTrue(TEXT("U+1F600 is a colour strike glyph"), FDreamGlyphColor::GetColorKind(Noto.Face, Grinning) == EDreamGlyphColorKind::Bitmap);
	TestTrue(TEXT("the space, which the strike leaves out, is no colour glyph"), FDreamGlyphColor::GetColorKind(Noto.Face, Space) == EDreamGlyphColorKind::None);

	for (const int32 Target : { 12, 32, 64, 109, 200 })
	{
		for (const float ReachEm : { 0.0f, 0.1f })
		{
			const FString What = FString::Printf(TEXT("U+1F600 at %d px, reach %.1f em"), Target, ReachEm);
			FDreamGlyphColorResult Glyph;
			if (!TestTrue(FString::Printf(TEXT("%s rasterizes"), *What), FDreamGlyphColor::Rasterize(Noto.Face, Grinning, MakeParams(Target, ReachEm), Glyph)))continue;
			const double Scale = FMath::Min(1.0, Target / NotoStrikePixelsPerEm);
			const float TexelsPerEm = (float)FMath::Min((double)Target, NotoStrikePixelsPerEm);
			const int32 Padding = 1 + FMath::CeilToInt32(ReachEm * TexelsPerEm);
			TestTrue(FString::Printf(TEXT("%s: a bitmap glyph"), *What), Glyph.Kind == EDreamGlyphColorKind::Bitmap);
			TestEqual(FString::Printf(TEXT("%s: texels per em"), *What), Glyph.TexelsPerEm, TexelsPerEm, 1e-4f);
			TestEqual(FString::Printf(TEXT("%s: width"), *What), Glyph.Width, FMath::CeilToInt32(NotoGrinningWidth * Scale - 1e-6) + 2 * Padding);
			TestEqual(FString::Printf(TEXT("%s: height"), *What), Glyph.Height, FMath::CeilToInt32(NotoGrinningHeight * Scale - 1e-6) + 2 * Padding);
			TestEqual(FString::Printf(TEXT("%s: left edge, the padding left of the strike's"), *What), Glyph.Left, -(float)Padding, 1e-3f);
			TestEqual(FString::Printf(TEXT("%s: top edge, the strike's scaled and the padding above it"), *What), Glyph.Top, (float)(NotoGrinningTop * Scale) + Padding, 1e-3f);
			if (!TestEqual(FString::Printf(TEXT("%s: four bytes a texel"), *What), Glyph.Pixels.Num(), Glyph.Width * Glyph.Height * 4))continue;
			TestTrue(FString::Printf(TEXT("%s: has ink"), *What), TotalAlpha(Glyph) > 0);
			TestTrue(FString::Printf(TEXT("%s: premultiplied"), *What), IsPremultiplied(Glyph));
			TestTrue(FString::Printf(TEXT("%s: padded by %d texels of (0,0,0,0)"), *What, Padding), PaddingIsClear(Glyph, Padding));
		}
	}
#else
	AddInfo(TEXT("Built without FreeType: no colour glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorAreaFilterTest,
	"DreamGUI.Text.GlyphColor.ShrinkingAStrikeKeepsItsInk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A strike larger than the size is shrunk with an exact area filter: every texel averages what its footprint covers, so
 * the glyph's total alpha scales by exactly the square of the scale, give or take the rounding of each texel to 8 bits.
 * Within 1% of the strike's own total, scaled, at sizes from 12 px to just under the strike's 109.
 */
bool FDreamGlyphColorAreaFilterTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphColorTestLocal;
	FTestFace Noto(NotoColorEmojiPath());
	if (!TestNotNull(TEXT("the engine's NotoColorEmoji opens"), Noto.Face))return false;
	const uint32 Grinning = FT_Get_Char_Index(Noto.Face, 0x1F600);
	FDreamGlyphColorResult Strike;
	if (!TestTrue(TEXT("U+1F600 rasterizes at the strike's own size"), Grinning != 0 && FDreamGlyphColor::Rasterize(Noto.Face, Grinning, MakeParams(109), Strike)))return false;
	const double StrikeAlpha = (double)TotalAlpha(Strike);
	if (!TestTrue(TEXT("the strike's glyph has ink"), StrikeAlpha > 0.0))return false;

	for (const int32 Target : { 12, 20, 32, 48, 64, 100 })
	{
		FDreamGlyphColorResult Glyph;
		if (!TestTrue(FString::Printf(TEXT("U+1F600 rasterizes at %d px"), Target), FDreamGlyphColor::Rasterize(Noto.Face, Grinning, MakeParams(Target), Glyph)))continue;
		const double Scale = Target / NotoStrikePixelsPerEm;
		const double Kept = (double)TotalAlpha(Glyph) / (StrikeAlpha * Scale * Scale);
		TestTrue(FString::Printf(TEXT("%d px keeps %.2f%% of the strike's alpha, scaled (within 1%%)"), Target, Kept * 100.0), FMath::Abs(Kept - 1.0) <= 0.01);
	}
#else
	AddInfo(TEXT("Built without FreeType: no colour glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorStrikeAdvanceTest,
	"DreamGUI.Text.GlyphColor.AStrikeFaceAdvancesByItsStrikeNotItsHmtx",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A strike face's glyph advances by the strike's own advance scaled to the size: NotoColorEmoji's 136 at 109 ppem is
 * 39.93 px at 32 px -- what Chrome measures -- where hmtx (2550/2048 em) would say 39.84. The rasterized glyph carries the
 * same advance in its texels. A glyph the strike leaves out, and a face with no strikes, have no strike advance.
 */
bool FDreamGlyphColorStrikeAdvanceTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphColorTestLocal;
	FTestFace Noto(NotoColorEmojiPath());
	if (!TestNotNull(TEXT("the engine's NotoColorEmoji opens"), Noto.Face))return false;
	const uint32 Grinning = FT_Get_Char_Index(Noto.Face, 0x1F600);
	if (!TestTrue(TEXT("NotoColorEmoji has U+1F600"), Grinning != 0))return false;

	for (const int32 Size : { 32, 200 })
	{
		const double Expected = NotoGrinningAdvance * Size / NotoStrikePixelsPerEm;
		float Advance = 0.0f;
		if (TestTrue(FString::Printf(TEXT("%d px: U+1F600 has a strike advance"), Size), FDreamGlyphColor::GetStrikeAdvance(Noto.Face, Grinning, (float)Size, Advance)))
		{
			TestEqual(FString::Printf(TEXT("%d px: it is the strike's 136 scaled"), Size), (double)Advance, Expected, 1e-3);
		}
		FDreamGlyphColorResult Glyph;
		if (TestTrue(FString::Printf(TEXT("%d px: U+1F600 rasterizes"), Size), FDreamGlyphColor::Rasterize(Noto.Face, Grinning, MakeParams(Size), Glyph) && Glyph.TexelsPerEm > 0.0f))
		{
			TestEqual(FString::Printf(TEXT("%d px: the glyph's advance, texels at its texels per em, is the same"), Size), (double)Glyph.Advance * Size / Glyph.TexelsPerEm, Expected, 1e-3);
		}
	}
	TestEqual(TEXT("at 32 px that is the 39.93 px Chrome measures"), NotoGrinningAdvance * 32.0 / NotoStrikePixelsPerEm, 39.93, 0.005);

	float Advance = 0.0f;
	TestFalse(TEXT("the space, which the strike leaves out, has no strike advance"), FDreamGlyphColor::GetStrikeAdvance(Noto.Face, FT_Get_Char_Index(Noto.Face, ' '), 32.0f, Advance));

	FTestFace Roboto(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")));
	if (!TestNotNull(TEXT("Roboto opens"), Roboto.Face))return false;
	const uint32 Letter = FT_Get_Char_Index(Roboto.Face, 'A');
	TestFalse(TEXT("an outline face has no strike advance"), FDreamGlyphColor::GetStrikeAdvance(Roboto.Face, Letter, 32.0f, Advance));
	TestTrue(TEXT("and no colour glyphs"), FDreamGlyphColor::GetColorKind(Roboto.Face, Letter) == EDreamGlyphColorKind::None);
	FDreamGlyphColorResult Glyph;
	TestFalse(TEXT("so nothing to rasterize in colour"), FDreamGlyphColor::Rasterize(Roboto.Face, Letter, MakeParams(32), Glyph));
#else
	AddInfo(TEXT("Built without FreeType: no colour glyphs."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlyphColorLayersTest,
	"DreamGUI.Text.GlyphColor.ColorLayersAreBlendedIntoOneImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Segoe UI Emoji's COLR table has v0 layers for every emoji (beside v1 paints, which FreeType does not draw). U+1F308, a
 * rainbow, is six layers in six colours: FreeType blends them in palette 0 into one premultiplied image at the size, with
 * at least three hues in it. A letter of the same face is a plain outline: no colour kind, nothing to rasterize here.
 * Skipped where Windows' fonts are not there.
 */
bool FDreamGlyphColorLayersTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamGlyphColorTestLocal;
	const FString Path = SegoeEmojiPath();
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		AddInfo(TEXT("No Segoe UI Emoji (seguiemj.ttf) on this machine; COLR layers were not checked."));
		return true;
	}
	FTestFace Segoe(Path);
	if (!TestNotNull(TEXT("Segoe UI Emoji opens"), Segoe.Face))return false;
	const uint32 Rainbow = FT_Get_Char_Index(Segoe.Face, 0x1F308);
	const uint32 Letter = FT_Get_Char_Index(Segoe.Face, 'A');
	if (!TestTrue(TEXT("Segoe UI Emoji has U+1F308 and A"), Rainbow != 0 && Letter != 0))return false;
	TestTrue(TEXT("U+1F308 is COLR layers"), FDreamGlyphColor::GetColorKind(Segoe.Face, Rainbow) == EDreamGlyphColorKind::Layers);
	TestTrue(TEXT("A is a plain outline"), FDreamGlyphColor::GetColorKind(Segoe.Face, Letter) == EDreamGlyphColorKind::None);

	FDreamGlyphColorResult Glyph;
	if (!TestTrue(TEXT("U+1F308 rasterizes at 32 px"), FDreamGlyphColor::Rasterize(Segoe.Face, Rainbow, MakeParams(32), Glyph)))return false;
	TestTrue(TEXT("as layers"), Glyph.Kind == EDreamGlyphColorKind::Layers);
	TestEqual(TEXT("at 32 texels per em"), Glyph.TexelsPerEm, 32.0f, 1e-4f);
	TestTrue(TEXT("advancing by its hmtx width"), Glyph.Advance > 0.0f);
	if (!TestEqual(TEXT("four bytes a texel"), Glyph.Pixels.Num(), Glyph.Width * Glyph.Height * 4))return false;
	TestTrue(TEXT("with ink"), TotalAlpha(Glyph) > 0);
	TestTrue(TEXT("premultiplied"), IsPremultiplied(Glyph));
	TestTrue(TEXT("padded by a texel of (0,0,0,0)"), PaddingIsClear(Glyph, 1));
	const int32 Hues = CountHues(Glyph);
	TestTrue(FString::Printf(TEXT("in colour: %d distinct hues (at least 3)"), Hues), Hues >= 3);

	FDreamGlyphColorResult Plain;
	TestFalse(TEXT("A is not rasterized as a colour glyph"), FDreamGlyphColor::Rasterize(Segoe.Face, Letter, MakeParams(32), Plain));
#else
	AddInfo(TEXT("Built without FreeType: no colour glyphs."));
#endif
	return true;
}

#endif
