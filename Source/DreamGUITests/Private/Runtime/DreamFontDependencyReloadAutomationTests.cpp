// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

namespace DreamFontDependencyReloadTestLocal
{
	static constexpr float Size = 32.0f;
	static constexpr float Tolerance = 0.0001f;

	UDreamUIFontData_Bitmap* MakeFont(UWorld* InWorld, const TCHAR* InFileName)
	{
		UDreamUIFontData_Bitmap* Font = NewObject<UDreamUIFontData_Bitmap>(InWorld);
		Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), InFileName), false);
		Font->InitFont();
		return Font;
	}

	struct FMetrics
	{
		float Ascent = 0.0f;
		float Descent = 0.0f;
		float LineHeight = 0.0f;
	};

	bool SameGeometry(const FDreamUICharData& InLeft, const FDreamUICharData& InRight)
	{
		return FMath::IsNearlyEqual(InLeft.Width, InRight.Width, Tolerance)
			&& FMath::IsNearlyEqual(InLeft.Height, InRight.Height, Tolerance)
			&& FMath::IsNearlyEqual(InLeft.XOffset, InRight.XOffset, Tolerance)
			&& FMath::IsNearlyEqual(InLeft.YOffset, InRight.YOffset, Tolerance)
			&& FMath::IsNearlyEqual(InLeft.XAdvance, InRight.XAdvance, Tolerance);
	}

	void ExpectGeometry(FAutomationTestBase& InTest, const FString& InWhat, const FDreamUICharData& InActual, const FDreamUICharData& InExpected)
	{
		// Each font has its own atlas packing, so UVs and slice locations need not match.
		InTest.TestEqual(InWhat + TEXT(" width"), InActual.Width, InExpected.Width, Tolerance);
		InTest.TestEqual(InWhat + TEXT(" height"), InActual.Height, InExpected.Height, Tolerance);
		InTest.TestEqual(InWhat + TEXT(" horizontal offset"), InActual.XOffset, InExpected.XOffset, Tolerance);
		InTest.TestEqual(InWhat + TEXT(" vertical offset"), InActual.YOffset, InExpected.YOffset, Tolerance);
		InTest.TestEqual(InWhat + TEXT(" advance"), InActual.XAdvance, InExpected.XAdvance, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFontFallbackReloadWarmCachesTest,
	"DreamGUI.Text.FontFallback.AReloadedFallbackRefreshesWarmPrimaryMetricsAndGlyphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFallbackReloadWarmCachesTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamFontDependencyReloadTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIFontData_Bitmap* Primary = MakeFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_Bitmap* Fallback = MakeFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	UDreamUIFontData_Bitmap* NewFaceReference = MakeFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Primary->SetFallbackFonts({ Fallback });
	const FDreamUIFontFaceIdentity BeforeIdentity = Primary->GetFaceIdentity(1);
	if (!TestTrue(TEXT("the initial fallback loads"), BeforeIdentity.IsValid())
		|| !TestTrue(TEXT("the replacement reference loads"), NewFaceReference->GetFaceIdentity(0).IsValid()))
	{
		return false;
	}

	FMetrics Before;
	FMetrics Repeated;
	FMetrics Expected;
	if (!TestTrue(TEXT("the primary warms its fallback metrics"), Primary->GetFaceMetrics(1, Size, Before.Ascent, Before.Descent, Before.LineHeight))
		|| !TestTrue(TEXT("the unmodified fallback measures again"), Primary->GetFaceMetrics(1, Size, Repeated.Ascent, Repeated.Descent, Repeated.LineHeight))
		|| !TestTrue(TEXT("the replacement face measures directly"), NewFaceReference->GetFaceMetrics(0, Size, Expected.Ascent, Expected.Descent, Expected.LineHeight)))
	{
		return false;
	}
	TestEqual(TEXT("without reload cached ascent stays stable"), Repeated.Ascent, Before.Ascent, Tolerance);
	TestEqual(TEXT("without reload cached descent stays stable"), Repeated.Descent, Before.Descent, Tolerance);
	TestEqual(TEXT("without reload cached line height stays stable"), Repeated.LineHeight, Before.LineHeight, Tolerance);
	TestTrue(TEXT("the two real faces have different line metrics"), !FMath::IsNearlyEqual(Before.LineHeight, Expected.LineHeight, Tolerance)
		|| !FMath::IsNearlyEqual(Before.Ascent, Expected.Ascent, Tolerance) || !FMath::IsNearlyEqual(Before.Descent, Expected.Descent, Tolerance));

	TArray<FDreamUICharData> ExpectedGlyphs;
	bool bDifferentGlyph = false;
	// Glyph ids belong to a face, and replacing its file must invalidate even an id that is in both files.
	for (uint32 GlyphIndex = 0; GlyphIndex < 5; ++GlyphIndex)
	{
		const FDreamUICharData Warm = Primary->GetGlyphData(1, GlyphIndex, Size, false);
		const FDreamUICharData Again = Primary->GetGlyphData(1, GlyphIndex, Size, false);
		const FDreamUICharData New = NewFaceReference->GetGlyphData(0, GlyphIndex, Size, false);
		ExpectGeometry(*this, FString::Printf(TEXT("without reload glyph %u stays stable"), GlyphIndex), Again, Warm);
		TestTrue(TEXT("without reload the glyph keeps its atlas location"), Again.MinUV == Warm.MinUV && Again.MaxUV == Warm.MaxUV && Again.SliceIndex == Warm.SliceIndex);
		ExpectedGlyphs.Add(New);
		bDifferentGlyph |= !SameGeometry(Warm, New);
	}
	TestTrue(TEXT("the replacement contains a different rasterized glyph at one of the same ids"), bDifferentGlyph);

	// Keep the primary's fallback list and the fallback object unchanged. Only that object's own face is reloaded.
	Fallback->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	const FDreamUIFontFaceIdentity AfterIdentity = Primary->GetFaceIdentity(1);
	TestTrue(TEXT("the same fallback owner now has a new face epoch"), AfterIdentity.Owner == BeforeIdentity.Owner && AfterIdentity.Epoch != BeforeIdentity.Epoch);
	// The shaper's existing identity check refreshes its cmap cache; dependent raster/metric caches must refresh too.
	TestTrue(TEXT("the primary sees the reloaded fallback's Arabic cmap"), Primary->FaceHasCodepoint(1, 0x0645));

	FMetrics After;
	if (TestTrue(TEXT("the primary measures the reloaded fallback"), Primary->GetFaceMetrics(1, Size, After.Ascent, After.Descent, After.LineHeight)))
	{
		TestEqual(TEXT("reloaded fallback ascent matches the new face"), After.Ascent, Expected.Ascent, Tolerance);
		TestEqual(TEXT("reloaded fallback descent matches the new face"), After.Descent, Expected.Descent, Tolerance);
		TestEqual(TEXT("reloaded fallback line height matches the new face"), After.LineHeight, Expected.LineHeight, Tolerance);
	}
	for (uint32 GlyphIndex = 0; GlyphIndex < 5; ++GlyphIndex)
	{
		const FDreamUICharData Actual = Primary->GetGlyphData(1, GlyphIndex, Size, false);
		const FDreamUICharData Direct = Fallback->GetGlyphData(0, GlyphIndex, Size, false);
		const FString What = FString::Printf(TEXT("reloaded fallback glyph %u"), GlyphIndex);
		ExpectGeometry(*this, What + TEXT(" direct reference"), Direct, ExpectedGlyphs[GlyphIndex]);
		ExpectGeometry(*this, What + TEXT(" through the primary"), Actual, Direct);
	}
	return true;
#else
	AddWarning(TEXT("This real-font dependency test requires FreeType."));
	return true;
#endif
}

#endif
