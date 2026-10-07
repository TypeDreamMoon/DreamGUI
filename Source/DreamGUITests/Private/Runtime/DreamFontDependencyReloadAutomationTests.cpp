// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Engine/Texture2DArray.h"
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

	UDreamUIFontData_DistanceField* MakeFieldFont(UWorld* InWorld, const TCHAR* InFileName)
	{
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(InWorld);
		FEnumProperty* CoverageProperty = FindFProperty<FEnumProperty>(Font->GetClass(), TEXT("SmallTextCoverage"));
		check(CoverageProperty != nullptr);
		CoverageProperty->GetUnderlyingProperty()->SetIntPropertyValue(CoverageProperty->ContainerPtrToValuePtr<void>(Font), (int64)EDreamUISmallTextCoverage::On);
		Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), InFileName), false);
		Font->InitFont();
		Font->PrepareForLayout(0.0f);
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
	// Atlas reset notifies registered texts, which reenter InitFont/GetFontTexture while they take the new texture.
	UDreamWidget* RegisteredRoot = NewObject<UDreamWidget>(TestWorld.World);
	RegisteredRoot->OnRegister();
	ON_SCOPE_EXIT { RegisteredRoot->DestroyWidget(); };
	UDreamText* RegisteredText = RegisteredRoot->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("the cache test has a text on a registered widget"), RegisteredText))return false;
	RegisteredText->SetFont(Primary);
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
	UTexture2DArray* WarmAtlas = Primary->GetFontTexture();
	TestTrue(TEXT("without reload reading the font keeps its atlas"), Primary->GetFontTexture() == WarmAtlas);

	// Keep the primary's fallback list and the fallback object unchanged. Only that object's own face is reloaded.
	Fallback->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	const FDreamUIFontFaceIdentity AfterIdentity = Primary->GetFaceIdentity(1);
	TestTrue(TEXT("the same fallback owner now has a new face epoch"), AfterIdentity.Owner == BeforeIdentity.Owner && AfterIdentity.Epoch != BeforeIdentity.Epoch);
	// The shaper's existing identity check refreshes its cmap cache; dependent raster/metric caches must refresh too.
	TestTrue(TEXT("the primary sees the reloaded fallback's Arabic cmap"), Primary->FaceHasCodepoint(1, 0x0645));
	TestTrue(TEXT("the registered text takes a new atlas after dependency reload"), RegisteredText->GetTextureToCreateGeometry() != WarmAtlas);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFontFallbackReloadFieldWorkerTest,
	"DreamGUI.Text.FontFallback.AReloadedFallbackDropsOldWorkerResultsAndRefreshesFieldsAndCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFallbackReloadFieldWorkerTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamFontDependencyReloadTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
	const bool bSavedAsync = Settings->bAsyncGlyphRasterization;
	IConsoleVariable* CoverageVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamGUI.Text.SmallTextCoverage"));
	if (!TestNotNull(TEXT("the coverage switch exists"), CoverageVariable))return false;
	const int32 SavedCoverage = CoverageVariable->GetInt();
	Settings->bAsyncGlyphRasterization = true;
	CoverageVariable->Set(1, ECVF_SetByCode);
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT
	{
		Settings->bAsyncGlyphRasterization = bSavedAsync;
		CoverageVariable->Set(SavedCoverage, ECVF_SetByCode);
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1);
	};
	UDreamUIFontData_DistanceField* Primary = MakeFieldFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_Bitmap* Fallback = MakeFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	UDreamUIFontData_DistanceField* Reference = MakeFieldFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	// An unused style dependency must stay lazy while synchronous fallback glyphs and cached metrics are read.
	UDreamUIFontData_Bitmap* UnusedStyle = NewObject<UDreamUIFontData_Bitmap>(TestWorld.World);
	UnusedStyle->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Italic.ttf")), false);
	Primary->SetFallbackFonts({ Fallback });
	Primary->SetStyleFonts(UnusedStyle, nullptr, nullptr);
	const FDreamUICharData WarmField = Primary->GetGlyphData(1, 0, Size, false);
	const FDreamUICharData ReferenceField = Reference->GetGlyphData(0, 0, Size, false);
	if (!TestTrue(TEXT("both real faces have a field for glyph zero"), WarmField.Width > 0 && ReferenceField.Width > 0))return false;
	FDreamUICoverageGlyph WarmCoverage;
	if (!TestTrue(TEXT("the original fallback warms its coverage glyph"), Primary->GetCoverageGlyph(1, 0, 13 * 64, EDreamUICoverageGlyphFlags::None, WarmCoverage) && !WarmCoverage.bPending && WarmCoverage.Width > 0))return false;
	TArray<uint8> WarmPixels;
	if (!TestTrue(TEXT("the original coverage has readable texels"), Primary->GetCoverageGlyphTexelsForTesting(WarmCoverage, WarmPixels)))return false;
	UTexture2DArray* WarmAtlas = Primary->GetFontTexture();
	const uint32 WarmCoverageEpoch = Primary->GetCoverageEpoch();
	ExpectGeometry(*this, TEXT("without reload the field is stable"), Primary->GetGlyphData(1, 0, Size, false), WarmField);
	FDreamUICoverageGlyph Repeated;
	TestTrue(TEXT("without reload coverage keeps its atlas location"), Primary->GetCoverageGlyph(1, 0, 13 * 64, EDreamUICoverageGlyphFlags::None, Repeated)
		&& Repeated.MinUV == WarmCoverage.MinUV && Repeated.MaxUV == WarmCoverage.MaxUV && Repeated.SliceIndex == WarmCoverage.SliceIndex);
	TestTrue(TEXT("without reload the field and coverage keep their atlas and epoch"), Primary->GetFontTexture() == WarmAtlas && Primary->GetCoverageEpoch() == WarmCoverageEpoch);
	FDreamUIFontMemoryInfo UnusedInfo;
	UnusedStyle->GetMemoryInfo(UnusedInfo);
	TestEqual(TEXT("cache checks do not load an unused style font"), UnusedInfo.FaceBytes, int64(0));

	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(0);
	TestTrue(TEXT("a second field glyph goes to the old worker"), Primary->GetGlyphData(1, 4, Size, false).bPending);
	FDreamUICoverageGlyph PendingCoverage;
	TestTrue(TEXT("a second coverage size goes to the old worker"), Primary->GetCoverageGlyph(1, 0, 14 * 64, EDreamUICoverageGlyphFlags::None, PendingCoverage) && PendingCoverage.bPending);
	TestEqual(TEXT("both jobs are pending"), Primary->GetPendingAsyncGlyphCount(), 2);
	Fallback->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	// No shaping or glyph query first: the drain itself must reject the old worker and its completed results.
	Primary->WaitForAsyncGlyphs();
	FDreamUIFontMemoryInfo AfterDiscard;
	Primary->GetMemoryInfo(AfterDiscard);
	TestEqual(TEXT("reload discards all old field entries before a new query"), AfterDiscard.FieldGlyphs, 0);
	TestEqual(TEXT("reload discards all old coverage entries before a new query"), AfterDiscard.CoverageGlyphs, 0);
	TestEqual(TEXT("reload clears both pending sets"), Primary->GetPendingAsyncGlyphCount(), 0);
	TestTrue(TEXT("reload advances the coverage epoch"), Primary->GetCoverageEpoch() != WarmCoverageEpoch);

	TestTrue(TEXT("the replacement field goes to a new worker"), Primary->GetGlyphData(1, 0, Size, false).bPending);
	FDreamUICoverageGlyph NewPendingCoverage;
	TestTrue(TEXT("the replacement coverage goes to a new worker"), Primary->GetCoverageGlyph(1, 0, 13 * 64, EDreamUICoverageGlyphFlags::None, NewPendingCoverage) && NewPendingCoverage.bPending);
	Primary->WaitForAsyncGlyphs();
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	const FDreamUICharData AfterField = Primary->GetGlyphData(1, 0, Size, false);
	TestFalse(TEXT("the replacement worker has supplied its field"), AfterField.bPending);
	ExpectGeometry(*this, TEXT("the replacement worker field matches the direct new face"), AfterField, ReferenceField);
	TestTrue(TEXT("the replacement really differs from the original field"), !SameGeometry(WarmField, ReferenceField));
	FDreamUICoverageGlyph AfterCoverage, ReferenceCoverage;
	if (!TestTrue(TEXT("the replacement worker supplies coverage"), Primary->GetCoverageGlyph(1, 0, 13 * 64, EDreamUICoverageGlyphFlags::None, AfterCoverage) && !AfterCoverage.bPending)
		|| !TestTrue(TEXT("the replacement reference supplies coverage directly"), Reference->GetCoverageGlyph(0, 0, 13 * 64, EDreamUICoverageGlyphFlags::None, ReferenceCoverage) && !ReferenceCoverage.bPending))return false;
	TestEqual(TEXT("new coverage width matches"), AfterCoverage.Width, ReferenceCoverage.Width);
	TestEqual(TEXT("new coverage height matches"), AfterCoverage.Height, ReferenceCoverage.Height);
	TestEqual(TEXT("new coverage left bearing matches"), AfterCoverage.BitmapLeft, ReferenceCoverage.BitmapLeft);
	TestEqual(TEXT("new coverage top bearing matches"), AfterCoverage.BitmapTop, ReferenceCoverage.BitmapTop);
	TArray<uint8> AfterPixels, ReferencePixels;
	if (!TestTrue(TEXT("the new worker coverage texels are readable"), Primary->GetCoverageGlyphTexelsForTesting(AfterCoverage, AfterPixels))
		|| !TestTrue(TEXT("the direct new coverage texels are readable"), Reference->GetCoverageGlyphTexelsForTesting(ReferenceCoverage, ReferencePixels)))return false;
	TestTrue(TEXT("the new worker coverage bytes match the direct replacement raster"), AfterPixels == ReferencePixels);
	TestTrue(TEXT("the real replacement raster differs from the original"), WarmPixels != ReferencePixels || WarmCoverage.Width != ReferenceCoverage.Width || WarmCoverage.Height != ReferenceCoverage.Height);
	return true;
#else
	AddWarning(TEXT("This real-font dependency test requires FreeType."));
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFontStyleReloadWarmCachesTest,
	"DreamGUI.Text.FontFallback.AReloadedStyleFaceRefreshesWarmMetricsGlyphsAndDecorations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontStyleReloadWarmCachesTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamFontDependencyReloadTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIFontData_Bitmap* Primary = MakeFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_Bitmap* Style = MakeFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	UDreamUIFontData_Bitmap* Reference = MakeFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Primary->SetStyleFonts(Style, nullptr, nullptr);
	const int32 StyledFace = Primary->GetStyledFace(true, false);
	if (!TestTrue(TEXT("the explicit bold face is available"), StyledFace != 0))return false;
	FMetrics Before;
	TestTrue(TEXT("the style metrics warm"), Primary->GetFaceMetrics(StyledFace, Size, Before.Ascent, Before.Descent, Before.LineHeight));
	Primary->GetGlyphData(StyledFace, 0, Size, false);
	float Underline = 0, UnderlineThickness = 0, Strike = 0, StrikeThickness = 0;
	Primary->GetDecorationMetrics(StyledFace, Size, Underline, UnderlineThickness, Strike, StrikeThickness);
	Style->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	FMetrics After, Expected;
	TestTrue(TEXT("the reloaded style measures through the primary"), Primary->GetFaceMetrics(StyledFace, Size, After.Ascent, After.Descent, After.LineHeight));
	TestTrue(TEXT("the reloaded style measures directly"), Reference->GetFaceMetrics(0, Size, Expected.Ascent, Expected.Descent, Expected.LineHeight));
	TestEqual(TEXT("the new style ascent matches"), After.Ascent, Expected.Ascent, Tolerance);
	TestEqual(TEXT("the new style descent matches"), After.Descent, Expected.Descent, Tolerance);
	TestEqual(TEXT("the new style line height matches"), After.LineHeight, Expected.LineHeight, Tolerance);
	ExpectGeometry(*this, TEXT("the reloaded style glyph matches"), Primary->GetGlyphData(StyledFace, 0, Size, false), Reference->GetGlyphData(0, 0, Size, false));
	float ExpectedUnderline = 0, ExpectedUnderlineThickness = 0, ExpectedStrike = 0, ExpectedStrikeThickness = 0;
	const bool bExpectedDecoration = Reference->GetDecorationMetrics(0, Size, ExpectedUnderline, ExpectedUnderlineThickness, ExpectedStrike, ExpectedStrikeThickness);
	const bool bActualDecoration = Primary->GetDecorationMetrics(StyledFace, Size, Underline, UnderlineThickness, Strike, StrikeThickness);
	TestEqual(TEXT("the reloaded style decoration availability matches"), bActualDecoration, bExpectedDecoration);
	if (bExpectedDecoration)
	{
		TestEqual(TEXT("the new style underline position matches"), Underline, ExpectedUnderline, Tolerance);
		TestEqual(TEXT("the new style underline thickness matches"), UnderlineThickness, ExpectedUnderlineThickness, Tolerance);
		TestEqual(TEXT("the new style strikethrough position matches"), Strike, ExpectedStrike, Tolerance);
		TestEqual(TEXT("the new style strikethrough thickness matches"), StrikeThickness, ExpectedStrikeThickness, Tolerance);
	}
	return true;
#else
	AddWarning(TEXT("This real-font dependency test requires FreeType."));
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFontFallbackReloadStaticTextTest,
	"DreamGUI.Text.FontFallback.AReloadedFallbackInvalidatesAnUneditedTextBeforeItsNextTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontFallbackReloadStaticTextTest::RunTest(const FString& Parameters)
{
#if WITH_FREETYPE
	using namespace DreamFontDependencyReloadTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIFontData_Bitmap* Primary = MakeFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_Bitmap* Fallback = MakeFont(TestWorld.World, TEXT("DroidSansFallback.ttf"));
	UDreamUIFontData_Bitmap* ReferencePrimary = MakeFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_Bitmap* ReferenceFallback = MakeFont(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Primary->SetFallbackFonts({ Fallback });
	ReferencePrimary->SetFallbackFonts({ ReferenceFallback });
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	Root->SetWidth(1000.0f);
	Root->SetHeight(600.0f);
	Root->OnRegister();
	ON_SCOPE_EXIT { Root->DestroyWidget(); };
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("the fixture has a canvas"), Canvas))return false;
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	auto AddText = [Root, &TestWorld](UDreamUIFontData_Bitmap* InFont)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World);
		Child->SetWidth(600.0f);
		Child->SetHeight(120.0f);
		Child->OnRegister();
		if (!Child->TrySetParent(Root, false))return static_cast<UDreamText*>(nullptr);
		UDreamText* Text = Child->CreateNewVisual<UDreamText>();
		if (Text != nullptr)
		{
			Text->SetFont(InFont);
			Text->SetFontSize(Size);
			Text->SetText(FText::FromString(TEXT("\u0645\u0645")));
		}
		return Text;
	};
	UDreamText* Text = AddText(Primary);
	UDreamText* ReferenceText = AddText(ReferencePrimary);
	UDreamUIManagerWorldSubsystem* Manager = TestWorld.World->GetSubsystem<UDreamUIManagerWorldSubsystem>();
	if (!TestNotNull(TEXT("the fixture has a live text"), Text) || !TestNotNull(TEXT("the fixture has a reference text"), ReferenceText)
		|| !TestNotNull(TEXT("the fixture has a manager"), Manager))return false;
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	// Keep the cache object itself: the visual's accessor would lay out again and query the font, masking a missed notification.
	const FDreamUITextGeometryCache& Cache = Text->GetCacheTextGeometryData();
	const FDreamUITextGeometryCache& ReferenceCache = ReferenceText->GetCacheTextGeometryData();
	const int32 WarmLayouts = Cache.GetLayoutRunCount();
	const FVector2f WarmSize = Cache.GetPreferredSize();
	const FVector2f ExpectedSize = ReferenceCache.GetPreferredSize();
	UTexture2DArray* WarmAtlas = Primary->GetFontTexture();
	TestFalse(TEXT("the settled text has a clean layout"), Cache.IsLayoutDirty());
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("an unchanged tick retains the layout"), Cache.GetLayoutRunCount(), WarmLayouts);
	TestTrue(TEXT("an unchanged tick retains the atlas"), Primary->GetFontTexture() == WarmAtlas);
	TestTrue(TEXT("the two real text faces produce different extents"), !WarmSize.Equals(ExpectedSize, Tolerance));
	Fallback->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/NotoNaskhArabicUI-Regular.ttf")), false);
	TestTrue(TEXT("fallback reload dirties the unedited primary text without another font query"), Cache.IsLayoutDirty());
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("the next tick lays the unedited text out again"), Cache.GetLayoutRunCount() > WarmLayouts);
	TestEqual(TEXT("the new text width matches a freshly loaded family"), Cache.GetPreferredSize().X, ExpectedSize.X, Tolerance);
	TestEqual(TEXT("the new text height matches a freshly loaded family"), Cache.GetPreferredSize().Y, ExpectedSize.Y, Tolerance);
	TestTrue(TEXT("the next tick paints from a replacement atlas"), Primary->GetFontTexture() != WarmAtlas);
	return true;
#else
	AddWarning(TEXT("This real-font dependency test requires FreeType."));
	return true;
#endif
}

#endif
