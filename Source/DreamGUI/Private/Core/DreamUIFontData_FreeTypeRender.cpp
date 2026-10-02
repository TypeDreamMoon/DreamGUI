// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Text/DreamGlyphRasterizer.h"
#include "Core/Text/DreamGlyphColor.h"
#include "Core/Text/DreamGlyphCoverage.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "DreamGUI.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Core/Components/DreamText.h"
#include "TextureResource.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2DArray.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "UObject/DreamGUIObjectVersion.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#if WITH_HARFBUZZ
#include "hb.h"
#include "hb-ft.h"
#include "hb-ot.h"
#endif
#if WITH_FREETYPE
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_ADVANCES_H
#endif

namespace
{
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> PendingFontTextureUploads;
	/** Fonts with glyphs on a worker, drained every frame with the texture uploads. */
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsWithAsyncGlyphs;
	/** Fonts whose atlas was flushed and is filling up again; see bAtlasRefilling. */
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsRefillingAtlas;
	int32 AsyncGlyphSyncBudgetOverride = -1;
	uint64 SyncGlyphBudgetFrame = 0;
	int32 SyncGlyphsThisFrame = 0;
}

namespace DreamFreeTypeRenderLocal
{
	/** Fonts with a coverage flush to make or OnCoverageGlyphsChanged to broadcast at the end of FlushPendingFontTextures. */
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsWithCoverageWork;
	/** Fonts whose coverage glyphs were flushed and are being made again; see bCoverageRefilling. */
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsRefillingCoverage;
	uint64 SyncCoverageBudgetFrame = 0;
	int32 SyncCoverageGlyphsThisFrame = 0;
	/** Cached strike advances past this many are dropped together: an animated size would otherwise grow the cache for good. */
	constexpr int32 MaxStrikeAdvanceCacheEntries = 4096;

	/**
	 * The game's current language as face resolution takes it, for code points resolved outside any text (GetCharData,
	 * kerning). Made again only when the language changes: its prioritized culture names are a list of strings. Game thread.
	 */
	const FDreamTextLanguage& GetGameLanguage()
	{
		static FDreamTextLanguage Language;
		static FString LanguageName;
		static bool bMade = false;
		const FCultureRef CurrentLanguage = FInternationalization::Get().GetCurrentLanguage();
		const FString& Current = CurrentLanguage->GetName();
		if (!bMade || LanguageName != Current)
		{
			Language = FDreamTextLanguage::Make(FString());
			LanguageName = Current;
			bMade = true;
		}
		return Language;
	}

#if WITH_FREETYPE
	/** Units per em, from the head table when the face's own is 0: FreeType leaves it so for a face without outlines. */
	int32 GetUnitsPerEm(FT_FaceRec_* InFace)
	{
		if (InFace == nullptr)return 0;
		if (InFace->units_per_EM != 0)return InFace->units_per_EM;
		const TT_Header* Head = static_cast<const TT_Header*>(FT_Get_Sfnt_Table(InFace, FT_SFNT_HEAD));
		return Head != nullptr ? (int32)Head->Units_Per_EM : 0;
	}

	/** A glyph's hmtx advance at a size, in pixels; 0 when the face cannot say. Loads no outline and no bitmap. */
	float GetDesignAdvance(FT_FaceRec_* InFace, uint32 InGlyphIndex, float InPixelSize)
	{
		const int32 UnitsPerEm = GetUnitsPerEm(InFace);
		FT_Fixed Advance = 0;
		if (UnitsPerEm <= 0 || FT_Get_Advance(InFace, InGlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM, &Advance) != 0)
		{
			return 0.0f;
		}
		return (float)((double)Advance * (double)InPixelSize / (double)UnitsPerEm);
	}
#endif
}

void UDreamUIFontData_FreeTypeRender::UpdateFontOnCultureChanged()
{
	FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (CultureFontMap.Contains(CurrentCulture))
		EngineFont = CultureFontMap[CurrentCulture].LoadSynchronous();

#if WITH_FREETYPE
	// Tear the old face down first: FreeType reads FontBinaryArray in place, so its bytes must not
	// move while a face still points at them.
	DeinitFreeType();
#endif

	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
#if WITH_EDITOR
		FontBinaryArray.Empty();//clear cache font data when switch to EngineFont; the editor reads EngineFont directly
#else
		// Outside the editor these cached bytes are the only font there is -- a cooked UFontFace's
		// runtime payload is not usable by FreeType, which is why they are cached at cook time. Emptying
		// them first handed FT_New_Memory_Face nothing and killed the font for the rest of the session,
		// so only swap them once the culture's face has actually produced bytes.
		if (IsValid(EngineFont) && EngineFont->GetFontFaceData()->HasData())
		{
			FontBinaryArray = EngineFont->GetFontFaceData()->GetData();
		}
		else
		{
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, culture '%s' has no usable font data; keeping the font that was loaded."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *CurrentCulture);
		}
#endif
	}

#if WITH_FREETYPE
	InitFreeType();
#endif
}

void UDreamUIFontData_FreeTypeRender::FinishDestroy()
{
#if WITH_FREETYPE
	DeinitFreeType();
#endif
	Super::FinishDestroy();
}

#if WITH_FREETYPE
const char* GetErrorMessage(FT_Error err)
{
#undef __FTERRORS_H__
#define FT_ERRORDEF( e, v, s )  case e: return s;
#define FT_ERROR_START_LIST     switch (err) {
#define FT_ERROR_END_LIST       }
#include FT_ERRORS_H
	return "(Unknown error)";
}

namespace DreamFreeTypeMetricsLocal
{
	/**
	 * FT_Set_Pixel_Sizes takes whole pixels, so a 16.9pt text was measured at 16: line height, ascent
	 * and descent moved in steps under a scaled canvas instead of following the size. FT_Set_Char_Size
	 * at 72dpi is the same request in 26.6 fixed point, which keeps the fraction. Sizes under a pixel
	 * are clamped instead of reported: a widget that is momentarily zero-sized asks for one every
	 * frame, and FreeType answers Invalid_Pixel_Size to every one of them.
	 */
	FT_Error SetMetricSize(FT_FaceRec_* InFace, float InFontSize)
	{
		const float Clamped = FMath::Max(InFontSize, 1.0f);
		return FT_Set_Char_Size(InFace, 0, (FT_F26Dot6)FMath::RoundToInt(Clamped * 64.0f), 72, 72);
	}

	/**
	 * A face's ascender, descender (as a positive distance) and line spacing at a size, in pixels, from the metrics the
	 * option names. Every option but FreeType's own scales the table values linearly, the way browsers do; FreeType's
	 * grid-fits them, which is what this font has always reported and so stays the default.
	 */
	bool ReadVerticalMetrics(FT_FaceRec_* InFace, EDreamUIFontVerticalMetrics InSource, float InFontSize, float& OutAscender, float& OutDescender, float& OutLineSpacing)
	{
		const bool bScalable = FT_IS_SCALABLE(InFace) && InFace->units_per_EM != 0;
		if (bScalable && InSource == EDreamUIFontVerticalMetrics::FreeType)
		{
			if (SetMetricSize(InFace, InFontSize))
			{
				return false;
			}
			OutAscender = InFace->size->metrics.ascender * ONE_DIVIDE_64;
			OutDescender = -InFace->size->metrics.descender * ONE_DIVIDE_64;
			OutLineSpacing = InFace->size->metrics.height * ONE_DIVIDE_64;
			return true;
		}
		// Every other source reads the tables. So does every source for a face without outlines -- colour bitmap strikes,
		// CBDT/CBLC or sbix -- of which FreeType keeps no units per em and no ascender (they are left at 0), and which takes
		// no char size at all: FT_Set_Char_Size fails on it. Its head, hhea and OS/2 tables still say what its em and its
		// line are.
		const TT_HoriHeader* Hhea = static_cast<const TT_HoriHeader*>(FT_Get_Sfnt_Table(InFace, FT_SFNT_HHEA));
		const TT_OS2* OS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(InFace, FT_SFNT_OS2));
		const bool bHasOS2 = OS2 != nullptr && OS2->version != 0xFFFFu;
		const double UnitsPerEm = (double)DreamFreeTypeRenderLocal::GetUnitsPerEm(InFace);
		if (UnitsPerEm > 0.0 && (bScalable || Hhea != nullptr || bHasOS2))
		{
			// hhea, or what FreeType makes of a face without one: it fills the face's own fields from OS/2 then. A face without
			// outlines has nothing in those fields, so it takes OS/2's typo metrics straight away.
			double HheaAscender = 0.0, HheaDescender = 0.0, HheaLineGap = 0.0;
			if (Hhea != nullptr)
			{
				HheaAscender = Hhea->Ascender;
				HheaDescender = -(double)Hhea->Descender;
				HheaLineGap = Hhea->Line_Gap;
			}
			else if (bScalable)
			{
				HheaAscender = InFace->ascender;
				HheaDescender = -(double)InFace->descender;
				HheaLineGap = (double)InFace->height - (HheaAscender + HheaDescender);
			}
			if (HheaAscender == 0.0 && HheaDescender == 0.0 && bHasOS2 && !bScalable)
			{
				// What FreeType does for a scalable face whose hhea says nothing: typo metrics, else the win ones.
				const bool bHasTypo = OS2->sTypoAscender != 0 || OS2->sTypoDescender != 0;
				HheaAscender = bHasTypo ? (double)OS2->sTypoAscender : (double)OS2->usWinAscent;
				HheaDescender = bHasTypo ? -(double)OS2->sTypoDescender : (double)OS2->usWinDescent;
				HheaLineGap = bHasTypo ? (double)OS2->sTypoLineGap : 0.0;
			}
			double Ascender = HheaAscender, Descender = HheaDescender, LineGap = HheaLineGap;
			const bool bUseTypoMetrics = bHasOS2 && (OS2->fsSelection & (1u << 7)) != 0;
			if (bHasOS2 && (InSource == EDreamUIFontVerticalMetrics::Typo || (InSource == EDreamUIFontVerticalMetrics::Platform && bUseTypoMetrics)))
			{
				Ascender = OS2->sTypoAscender;
				Descender = -(double)OS2->sTypoDescender;
				LineGap = OS2->sTypoLineGap;
			}
			else if (bHasOS2 && InSource == EDreamUIFontVerticalMetrics::Win)
			{
				Ascender = OS2->usWinAscent;
				Descender = OS2->usWinDescent;
				LineGap = 0.0;
			}
			else if (bHasOS2 && InSource == EDreamUIFontVerticalMetrics::Platform)
			{
				// DirectWrite's line gap for a face without USE_TYPO_METRICS is GDI's external leading: whatever of the
				// hhea line spacing the win ascent and descent do not already cover.
				Ascender = OS2->usWinAscent;
				Descender = OS2->usWinDescent;
				LineGap = FMath::Max(0.0, (HheaAscender + HheaDescender + HheaLineGap) - (Ascender + Descender));
			}
			const double Scale = (double)InFontSize / UnitsPerEm;
			if (InSource == EDreamUIFontVerticalMetrics::FreeType)
			{
				// Only a face without outlines gets here with this source: FreeType's own grid-fitting of a scalable face's size
				// metrics, done by hand -- the ascender and the descender rounded outwards, the line spacing to the nearest pixel.
				OutAscender = (float)FMath::CeilToDouble(Ascender * Scale);
				OutDescender = (float)FMath::CeilToDouble(Descender * Scale);
				OutLineSpacing = (float)FMath::FloorToDouble((Ascender + Descender + LineGap) * Scale + 0.5);
				return true;
			}
			OutAscender = (float)(Ascender * Scale);
			OutDescender = (float)(Descender * Scale);
			OutLineSpacing = (float)((Ascender + Descender + LineGap) * Scale);
			return true;
		}
		// No tables to read (a bitmap font that is not an sfnt): the strike chosen for the size, its own metrics scaled to it.
		if (!bScalable && InFace->num_fixed_sizes > 0)
		{
			const int32 Strike = FDreamGlyphColor::ChooseStrike(InFace, InFontSize);
			if (Strike != INDEX_NONE && FT_Select_Size(InFace, Strike) == 0 && InFace->size->metrics.y_ppem > 0)
			{
				const double Scale = (double)InFontSize / (double)InFace->size->metrics.y_ppem;
				OutAscender = (float)(InFace->size->metrics.ascender * ONE_DIVIDE_64 * Scale);
				OutDescender = (float)(-InFace->size->metrics.descender * ONE_DIVIDE_64 * Scale);
				OutLineSpacing = (float)(InFace->size->metrics.height * ONE_DIVIDE_64 * Scale);
				return true;
			}
		}
		return false;
	}
}

#if WITH_EDITOR
TArray<FString> UDreamUIFontData_FreeTypeRender::CacheSubFaces(FT_LibraryRec_* InFTLibrary, const TArray<uint8>& InMemory)
{
	TArray<FString> Result;
	FT_Face FTFace = nullptr;
	FT_New_Memory_Face(InFTLibrary, InMemory.GetData(), static_cast<FT_Long>(InMemory.Num()), -1, &FTFace);
	if (FTFace)
	{
		const int32 NumFaces = FTFace->num_faces;
		FT_Done_Face(FTFace);
		FTFace = nullptr;

		Result.Reserve(NumFaces);
		for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			FT_New_Memory_Face(InFTLibrary, InMemory.GetData(), static_cast<FT_Long>(InMemory.Num()), FaceIndex, &FTFace);
			if (FTFace)
			{
				Result.Add(FString::Printf(TEXT("%s (%s)"), UTF8_TO_TCHAR(FTFace->family_name), UTF8_TO_TCHAR(FTFace->style_name)));
				FT_Done_Face(FTFace);
				FTFace = nullptr;
			}
		}
	}
	return Result;
}
#endif

void UDreamUIFontData_FreeTypeRender::InitFreeType()
{
	if (bAlreadyInitialized)return;
	// One failed attempt is enough. Every glyph request comes back through here, so without this a font
	// that cannot load re-ran the whole thing per glyph: an error line each time, and -- because the
	// early exits below never closed it -- one leaked FT_Library each time. A Deinit clears the flag,
	// which is what ReloadFont and a culture change already go through.
	if (bInitFailed)return;
	FT_Error error = 0;
	error = FT_Init_FreeType(&Library);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		Library = nullptr;
		bInitFailed = true;
		return;
	}

	// Every exit below owns the library it just opened; FreeType's faces are closed with it.
	auto FailInit = [this]()
	{
		if (Library != nullptr)
		{
			FT_Done_FreeType(Library);
			Library = nullptr;
		}
		Face = nullptr;
		bInitFailed = true;
	};

	auto NewFontFace = [&error, this](const TArray<uint8>& InFontBinary) {
		// FontFace is kept across reloads (a .ttc's second face stays the second face through an edit, a culture switch
		// or a new file path) and clamped here, in every build, to the faces this file has.
#if WITH_EDITOR
		SubFaces = CacheSubFaces(Library, InFontBinary);
		const int32 NumFaces = SubFaces.Num();
		if (NumFaces == 0)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, have no face!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
			return;
		}
#else
		// Face index -1 opens nothing and only reports how many faces the file holds.
		int32 NumFaces = 1;
		FT_Face CountingFace = nullptr;
		if (FT_New_Memory_Face(Library, InFontBinary.GetData(), InFontBinary.Num(), -1, &CountingFace) == 0 && CountingFace != nullptr)
		{
			NumFaces = FMath::Max((int32)CountingFace->num_faces, 1);
			FT_Done_Face(CountingFace);
		}
#endif
		FontFace = FMath::Clamp(FontFace, 0, NumFaces - 1);
		error = FT_New_Memory_Face(Library, InFontBinary.GetData(), InFontBinary.Num(), FontFace, &Face);
	};

	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
#if WITH_EDITOR
		//editor use data from EngineFont
		if (IsValid(EngineFont))
		{
			if (EngineFont->GetFontFaceData()->HasData())
			{
				NewFontFace(EngineFont->GetFontFaceData()->GetData());
			}
			else
			{
				if (!FFileHelper::LoadFileToArray(TempFontBinaryArray, *EngineFont->GetFontFilename()))
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Failed to load or process '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *EngineFont->GetFontFilename());
					FailInit();
					return;
				}
				else
				{
					NewFontFace(TempFontBinaryArray);
				}
			}
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, trying to load Unreal's font face, but not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
			FailInit();
			return;
		}
#else
		//from UE5.6, runtime use cached data, because UnrealFont's runtime data is not usable for freetype
		NewFontFace(FontBinaryArray);
#endif
	}
	else
	{
#if WITH_EDITOR
		if (true)
		{
			FString FontFilePathStr = FontFilePath;
			FontFilePathStr = bUseRelativeFilePath ? FPaths::ProjectDir() + FontFilePath : FontFilePath;
			if (!FPaths::FileExists(*FontFilePathStr))
			{
				if (FontBinaryArray.Num() > 0 && !bUseExternalFileOrEmbedInToUAsset)
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, file: \"%s\" not exist! Will use cache data"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
				}
				else
				{
					UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, file: \"%s\" not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
					FailInit();
					return;
				}
			}

			if (bUseExternalFileOrEmbedInToUAsset)
			{
				FFileHelper::LoadFileToArray(TempFontBinaryArray, *FontFilePathStr);
				NewFontFace(TempFontBinaryArray);
				if (error == 0)
				{
					FontBinaryArray.Empty();
				}
			}
			else
			{
				FFileHelper::LoadFileToArray(FontBinaryArray, *FontFilePathStr);
				NewFontFace(FontBinaryArray);
			}
		}
		else
#endif	
		{
			if (bUseExternalFileOrEmbedInToUAsset)
			{
				auto FontFilePathStr = bUseRelativeFilePath ? FPaths::ProjectDir() + FontFilePath : FontFilePath;
				if (!FPaths::FileExists(*FontFilePathStr))
				{
					UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, file: \"%s\" not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
					FailInit();
					return;
				}

				FontBinaryArray.Empty();
				FFileHelper::LoadFileToArray(TempFontBinaryArray, *FontFilePathStr);
				NewFontFace(TempFontBinaryArray);
			}
			else
			{
				NewFontFace(FontBinaryArray);
			}
		}
	}

	// A null face with no error is the editor's "have no face!" branch above: it never called
	// FT_New_Memory_Face, and the success path below dereferences Face (FT_HAS_KERNING) straight away.
	if (error || Face == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		FailInit();
		return;
	}
	else
	{
		UE_LOG(DreamGUI, Log, TEXT("[%s].%d Success, font:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		bAlreadyInitialized = true;
		bHasKerning = FT_HAS_KERNING(Face) != 0;
		// A face of its own again: what was shaped or laid out with the last one is not this one's.
		FaceEpoch++;
		LayoutEpoch++;
		InitHarfBuzz();

		// Texts drawing with this font get their atlas back here, and are told to lay out again: the way back from a
		// reload or a culture switch. A font that is only another font's fallback or style face has no texts of its own
		// -- its glyphs go into that font's atlas -- so it makes none; an atlas is 16MB at the default size. InitFont,
		// and the first glyph a font rasterizes, make one for a font that has none yet.
		if (RenderTextArray.Num() > 0)
		{
			FlushGlyphAtlas();
		}
	}
}
#endif

// Outside the FreeType block: nothing in it needs FreeType, and the atlas code that calls it is built without it too.
void UDreamUIFontData_FreeTypeRender::FlushGlyphAtlas()
{
	check(IsInGameThread());
	bAtlasFlushRequested = false;
	AtlasSliceThreshold = 0;
	bAtlasRefilling = true;
	AtlasFlushFrame = GFrameCounter;
	FontsRefillingAtlas.Add(this);
	// Releasing the atlas resets the packing too: the cell pool, both packers, and with them every coverage cell.
	ReleaseFontTexture();
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	// RenewFontTexture tells every text using this font that its atlas changed, which is what makes
	// them re-lay-out: a display list caches each glyph's UVs, and those all just moved.
	RenewFontTexture();
	AddAtlasSliceCells(0);
	OneDivideTextureSize = 1.0f / TextureSize;

	// The coverage glyphs go with the rest: the texts laying out again paint again, and ask for them anew.
	ClearAtlasCaches();
}

void UDreamUIFontData_FreeTypeRender::ResetAtlasPacking()
{
	FreeAtlasCells.Reset();
	FieldPacker = FAtlasPacker();
	CoveragePacker = FAtlasPacker();
	CoverageCells.Reset();
	RetiredCoverageCells.Reset();
	bCoverageFlushRequested = false;
}

void UDreamUIFontData_FreeTypeRender::ClearAtlasCaches()
{
	ClearCharDataCache();
	ColorGlyphs.Reset();
	CoverageGlyphs.Reset();
}

void UDreamUIFontData_FreeTypeRender::AddAtlasSliceCells(int32 Slice)
{
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 CellSize = FMath::Min(UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType), TextureSize);
	// Taken from the end of the array: pushed last-used first, so the packers take the top-left cell first and then go down
	// the first column, then the next -- the order the packer has always filled a slice in.
	for (int32 X = TextureSize - CellSize; X >= 0; X -= CellSize)
	{
		for (int32 Y = TextureSize - CellSize; Y >= 0; Y -= CellSize)
		{
			FAtlasCell& Cell = FreeAtlasCells.AddDefaulted_GetRef();
			Cell.Slice = Slice;
			Cell.X = X;
			Cell.Y = Y;
		}
	}
}

void UDreamUIFontData_FreeTypeRender::AddAtlasSlice()
{
	const int32 NewSlice = Texture != nullptr ? Texture->GetArraySize() : 0;
	// The slice count the atlas is about to have.
	UE_LOG(DreamGUI, Log, TEXT("[%s].%d Expend Texture2DArray slice to: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, NewSlice + 1);
	RenewFontTexture();
	AddAtlasSliceCells(NewSlice);
	OneDivideTextureSize = 1.0f / UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
}

void UDreamUIFontData_FreeTypeRender::TakeAtlasCell(FAtlasPacker& InOutPacker, bool bCoverage)
{
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 CellSize = FMath::Min(UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType), TextureSize);
	const FAtlasCell Cell = FreeAtlasCells.Pop(EAllowShrinking::No);
	rbp::Rect CellRect;
	CellRect.x = Cell.X;
	CellRect.y = Cell.Y;
	CellRect.width = CellSize;
	CellRect.height = CellSize;
	// What is left of the packer's last cell is given up, as it always was: glyphs rarely fit the scraps of a full cell.
	InOutPacker.Bin = rbp::MaxRectsBinPack(CellSize, CellSize);
	InOutPacker.Bin.DoRectCellsForText(CellRect);
	InOutPacker.Cell = Cell;
	InOutPacker.bHasCell = true;
	if (bCoverage)
	{
		CoverageCells.Add(Cell);
		const int32 Budget = UDreamUISettings::GetMaxCoverageCells();
		if (CoverageCells.Num() > (CoverageCellThreshold > 0 ? CoverageCellThreshold : Budget))
		{
			// Still refilling after the last coverage flush, and out of cells already: the texts on screen need more than the
			// budget, and another flush would only start the same refill again. Never past twice the budget: a font that keeps
			// coverage glyphs on the worker frame after frame never counts as settled.
			if (bCoverageRefilling && CoverageCells.Num() <= 2 * Budget)
			{
				CoverageCellThreshold = CoverageCells.Num();
				if (!bLoggedCoverageThresholdRaise)
				{
					bLoggedCoverageThresholdRaise = true;
					UE_LOG(DreamGUI, Log, TEXT("[%s].%d Font:%s, the small-text coverage glyphs on screen need more than the %d-cell budget (DreamUI settings: Max Coverage Cells), so they keep more cells, up to twice the budget, instead of being flushed every frame. (reported once per font)")
						, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), Budget);
				}
			}
			else
			{
				RequestCoverageFlush();
			}
		}
	}
}

bool UDreamUIFontData_FreeTypeRender::PackAtlasRect(bool bCoverage, int32 InWidth, int32 InHeight, int32& OutSlice, int32& OutX, int32& OutY)
{
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 CellSize = FMath::Min(UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType), TextureSize);
	if (InWidth <= 0 || InHeight <= 0)
	{
		return false;
	}
	if (InWidth > CellSize || InHeight > CellSize)
	{
		// No cell can ever hold it. Taking cell after cell for it, as the packer used to, only grew the atlas.
		if (!bLoggedGlyphLargerThanCell)
		{
			bLoggedGlyphLargerThanCell = true;
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, a %dx%d glyph is larger than the %d-texel cells the atlas is packed in, so it is not drawn. Lower the font's sample size, or raise its rect-pack cell size. (reported once per font)")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), InWidth, InHeight, CellSize);
		}
		return false;
	}
	// Cells a coverage flush took back in an earlier frame are free again from here on.
	ReleaseRetiredCoverageCells();
	FAtlasPacker& Packer = bCoverage ? CoveragePacker : FieldPacker;
	bool bFlushedForThisRect = false;
	for (int32 Attempt = 0; Attempt < 64; Attempt++)
	{
		if (Packer.bHasCell)
		{
			const rbp::Rect Packed = Packer.Bin.Insert(InWidth, InHeight, rbp::MaxRectsBinPack::RectBestAreaFit);
			if (Packed.height > 0)
			{
				OutSlice = Packer.Cell.Slice;
				OutX = Packed.x;
				OutY = Packed.y;
				return true;
			}
		}
		if (FreeAtlasCells.Num() > 0)
		{
			TakeAtlasCell(Packer, bCoverage);
			continue;
		}
		// No cell left anywhere: the atlas grows by a slice.
		const int32 SliceCount = Texture != nullptr ? Texture->GetArraySize() : 0;
		const int32 RHILimit = FMath::Max((int32)GMaxTextureArrayLayers, 1);
		if (SliceCount >= RHILimit)
		{
			if (bCoverage)
			{
				// The field glyphs are not flushed for coverage glyphs: this one is drawn from its field, and the coverage
				// glyphs give their cells back at the frame boundary.
				RequestCoverageFlush();
				return false;
			}
			if (bFlushedForThisRect)
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, a %dx%d glyph does not fit an empty %d-slice atlas. Raise DreamUI's atlas texture size, or the font's rect-pack cell size.")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), InWidth, InHeight, SliceCount);
				return false;
			}
			bFlushedForThisRect = true;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, the glyph atlas reached the %d slices the RHI can address; flushing it now and refilling on demand.")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), RHILimit);
			FlushGlyphAtlas();
			continue;
		}
		if (!bCoverage && SliceCount >= GetAtlasSliceThreshold())
		{
			// The atlas is as large as it is meant to get. It used to fail the glyph from here on, for the rest of the
			// session, because it only ever grew. A rect-packed atlas cannot give one glyph's rectangle to a glyph of
			// another size without repacking, so there is no "evict the least recently used glyph" to do; what there is --
			// and what Slate's own font cache does when its atlas fills -- is to throw the whole cache away and let it
			// refill on demand. That happens between frames (see bAtlasFlushRequested); until then the atlas grows past its
			// budget. Coverage glyphs keep to a budget of cells of their own instead (RequestCoverageFlush).
			RequestAtlasFlush(SliceCount);
		}
		AddAtlasSlice();
	}
	UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, a %dx%d glyph never fit the atlas."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), InWidth, InHeight);
	return false;
}

#if WITH_FREETYPE
void UDreamUIFontData_FreeTypeRender::DeinitFreeType()
{
	bAlreadyInitialized = false;
	bInitFailed = false;
	// Whatever was shaped or laid out with this face is not the next face's (the shape cache keys runs by this epoch).
	FaceEpoch++;
	LayoutEpoch++;
	// The worker keeps itself (and the font bytes it reads) alive until its task ends; results for
	// this font are simply dropped with it. Dropping our reference to the shared bytes here is what
	// makes the next rasterizer take the reloaded file rather than the one that was just replaced.
	Rasterizer.Reset();
	SharedFaceBytes.Reset();
	FaceMetricsCache.Reset();
	FaceDecorationCache.Reset();
	ColorGlyphInfos.Reset();
	ResetCodepointFaces();
	PendingAsyncGlyphs.Reset();
	PendingColorGlyphs.Reset();
	PendingCoverageGlyphs.Reset();
	FontsWithAsyncGlyphs.Remove(this);
	bAtlasFlushRequested = false;
	bAtlasRefilling = false;
	AtlasSliceThreshold = 0;
	FontsRefillingAtlas.Remove(this);
	bCoverageRefilling = false;
	CoverageCellThreshold = 0;
	DreamFreeTypeRenderLocal::FontsRefillingCoverage.Remove(this);
	DeinitHarfBuzz();
	ReleaseFontTexture();
	if (Library != nullptr)
	{
		auto error = FT_Done_FreeType(Library);
		if (error)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		}
		else
		{
			UE_LOG(DreamGUI, Log, TEXT("[%s].%d Success, font:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		}
	}
	Face = nullptr;
	Library = nullptr;
#if WITH_EDITORONLY_DATA
	SubFaces.Reset();
#endif
	// FontFace is not reset here. Every reload comes through this function -- picking another face of a .ttc in the
	// details panel, a culture switch, a new file path -- so resetting it made face 0 the only face a collection could
	// ever show. InitFreeType clamps it to the file it opens.
	bHasKerning = false;
	bCoverageGlyphsChanged = false;
	ClearAtlasCaches();
}
#endif

#if WITH_FREETYPE
FT_GlyphSlot UDreamUIFontData_FreeTypeRender::RenderGlyphOnFreeType(FT_FaceRec_* InFace, uint32 GlyphIndex, float CharSize, float BoldSize)
{
	if (InFace == nullptr)
	{
		return nullptr;
	}
	// The size with its fraction, as the shaper and the metrics have it: FT_Set_Pixel_Sizes took whole pixels, so a
	// 16.75px glyph was drawn at 16px under its 16.75px advance. (A hinted TrueType face that asks for integer ppems still
	// snaps to the nearest one; that is FreeType's rounding, not a truncation of ours.)
	auto error = DreamFreeTypeMetricsLocal::SetMetricSize(InFace, CharSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font '%s' FT_Set_Char_Size error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	FT_GlyphSlot slot = InFace->glyph;
	error = FT_Load_Glyph(InFace, GlyphIndex, FT_LOAD_DEFAULT);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font '%s' FT_Load_Glyph error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	if (BoldSize > 0)
	{
		error = FT_Outline_Embolden(&slot->outline, static_cast<FT_Pos>(BoldSize * 64.0f));
		if (error)
		{
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font '%s' FT_Outline_Embolden error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		}
	}
	error = FT_Render_Glyph(slot, FT_Render_Mode::FT_RENDER_MODE_NORMAL);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font '%s' FT_Render_Glyph error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	if (BoldSize > 0)
	{
		slot->metrics.horiAdvance += BoldSize * 64.0f;
	}
	return slot;
}

void UDreamUIFontData_FreeTypeRender::ReadGlyphRow(const FT_Bitmap_& InBitmap, int32 InRow, uint8* OutCoverage, int32 InCount)
{
	if (OutCoverage == nullptr || InCount <= 0)return;
	FMemory::Memzero(OutCoverage, InCount);
	if (InBitmap.buffer == nullptr)return;
	if (InRow < 0 || InRow >= (int32)InBitmap.rows)return;
	// A negative pitch means row 0 is the LAST row in memory and the rows walk backwards from there.
	const uint8* Row = InBitmap.pitch >= 0
		? InBitmap.buffer + (int64)InRow * InBitmap.pitch
		: InBitmap.buffer + (int64)((int32)InBitmap.rows - 1 - InRow) * (-InBitmap.pitch);
	const int32 Width = FMath::Min(InCount, (int32)InBitmap.width);
	switch (InBitmap.pixel_mode)
	{
	case FT_PIXEL_MODE_GRAY:
		FMemory::Memcpy(OutCoverage, Row, Width);
		break;
	case FT_PIXEL_MODE_MONO:
		// 1 bit per pixel, most significant bit first: the strikes CJK fonts embed at small sizes.
		for (int32 x = 0; x < Width; x++)
		{
			OutCoverage[x] = (Row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
		}
		break;
	case FT_PIXEL_MODE_GRAY2:
	case FT_PIXEL_MODE_GRAY4:
	{
		const int32 BitsPerPixel = InBitmap.pixel_mode == FT_PIXEL_MODE_GRAY2 ? 2 : 4;
		const int32 MaxValue = (1 << BitsPerPixel) - 1;
		const int32 PixelsPerByte = 8 / BitsPerPixel;
		for (int32 x = 0; x < Width; x++)
		{
			const int32 Shift = 8 - BitsPerPixel * ((x % PixelsPerByte) + 1);
			const int32 Value = (Row[x / PixelsPerByte] >> Shift) & MaxValue;
			OutCoverage[x] = (uint8)(Value * 255 / MaxValue);
		}
		break;
	}
	case FT_PIXEL_MODE_BGRA:
		// A colour strike: take its alpha, which is the coverage the atlas stores.
		for (int32 x = 0; x < Width; x++)
		{
			OutCoverage[x] = Row[x * 4 + 3];
		}
		break;
	default:
		// LCD modes are not asked for anywhere here; leaving the row blank beats reading it as grey.
		break;
	}
}

FT_FaceRec_* UDreamUIFontData_FreeTypeRender::GetFreeTypeFace(int32 FaceIndex)
{
	if (FaceIndex == 0)
	{
		InitFreeType();
		return bAlreadyInitialized ? Face : nullptr;
	}
	// A fallback or a style face is another font's own face 0, never that font's fallbacks or style faces in turn, so
	// fonts that name each other in a cycle cannot recurse here.
	UDreamUIFontData_FreeTypeRender* Owner = GetFaceOwner(FaceIndex);
	return Owner != nullptr ? Owner->GetFreeTypeFace(0) : nullptr;
}
#endif

UDreamUIFontData_FreeTypeRender* UDreamUIFontData_FreeTypeRender::GetFaceOwner(int32 FaceIndex)
{
	if (FaceIndex == 0)
	{
		return this;
	}
	if (FaceIndex < 0)
	{
		return nullptr;
	}
	// 1..N are the fallbacks; the three style faces follow them, in the order GetStyledFace hands them out.
	const int32 RegularFaceCount = GetFaceCount();
	UDreamUIFontData_FreeTypeRender* Owner = nullptr;
	if (FaceIndex < RegularFaceCount)
	{
		// An entry with no font keeps its index: an empty face.
		const int32 FallbackIndex = FaceIndex - 1;
		Owner = Fallbacks.IsValidIndex(FallbackIndex) ? Fallbacks[FallbackIndex].Font.Get() : nullptr;
	}
	else
	{
		switch (FaceIndex - RegularFaceCount)
		{
		case 0: Owner = BoldFont; break;
		case 1: Owner = ItalicFont; break;
		case 2: Owner = BoldItalicFont; break;
		default: break;
		}
	}
	return Owner != this ? Owner : nullptr;
}

int32 UDreamUIFontData_FreeTypeRender::GetStyledFace(bool bBold, bool bItalic)
{
	if (!bBold && !bItalic)
	{
		return 0;
	}
	const int32 FirstStyleFace = GetFaceCount();
	// A style face that is set but cannot be opened is no face at all: the run falls back to synthesizing the style on
	// this font's own face rather than being handed an index that has no glyphs.
#if WITH_FREETYPE
	auto Available = [this](int32 FaceIndex) { return GetFreeTypeFace(FaceIndex) != nullptr; };
#else
	auto Available = [this](int32 FaceIndex) { return GetFaceOwner(FaceIndex) != nullptr; };
#endif
	const int32 BoldFace = FirstStyleFace + 0;
	const int32 ItalicFace = FirstStyleFace + 1;
	const int32 BoldItalicFace = FirstStyleFace + 2;
	if (bBold && bItalic)
	{
		// The face that is both; failing that, one that is half of it, which the layout completes synthetically.
		if (Available(BoldItalicFace))return BoldItalicFace;
		if (Available(BoldFace))return BoldFace;
		if (Available(ItalicFace))return ItalicFace;
		return 0;
	}
	if (bBold)
	{
		return Available(BoldFace) ? BoldFace : 0;
	}
	return Available(ItalicFace) ? ItalicFace : 0;
}

EDreamUIFontFaceStyle UDreamUIFontData_FreeTypeRender::GetFaceStyleFlags(int32 FaceIndex)
{
	const int32 FirstStyleFace = GetFaceCount();
	if (FaceIndex < FirstStyleFace || GetFaceOwner(FaceIndex) == nullptr)
	{
		return EDreamUIFontFaceStyle::None;
	}
	switch (FaceIndex - FirstStyleFace)
	{
	case 0: return EDreamUIFontFaceStyle::Bold;
	case 1: return EDreamUIFontFaceStyle::Italic;
	case 2: return EDreamUIFontFaceStyle::Bold | EDreamUIFontFaceStyle::Italic;
	default: return EDreamUIFontFaceStyle::None;
	}
}

#if WITH_HARFBUZZ && !IS_MONOLITHIC
// The engine's HarfBuzz is built to allocate through these hooks. SlateCore defines its own copy
// inside its DLL; a module that links the static library needs one of its own. Not in a monolithic
// build, where SlateCore and this module are one image and SlateCore's copy is the one.
extern "C"
{
	void* HarfBuzzMalloc(size_t InSizeBytes)
	{
		return FMemory::Malloc(InSizeBytes);
	}
	void* HarfBuzzCalloc(size_t InNumItems, size_t InItemSizeBytes)
	{
		const size_t AllocSizeBytes = InNumItems * InItemSizeBytes;
		if (AllocSizeBytes > 0)
		{
			void* Ptr = FMemory::Malloc(AllocSizeBytes);
			FMemory::Memzero(Ptr, AllocSizeBytes);
			return Ptr;
		}
		return nullptr;
	}
	void* HarfBuzzRealloc(void* InPtr, size_t InSizeBytes)
	{
		return FMemory::Realloc(InPtr, InSizeBytes);
	}
	void HarfBuzzFree(void* InPtr)
	{
		FMemory::Free(InPtr);
	}
}
#endif

void UDreamUIFontData_FreeTypeRender::InitHarfBuzz()
{
	DeinitHarfBuzz();
#if WITH_HARFBUZZ && WITH_FREETYPE
	if (Face == nullptr)
	{
		return;
	}
	// The face reads its tables through FreeType; the font does its own OpenType metrics, so its
	// scale is independent of whatever pixel size the FreeType face was last set to for rendering.
	hb_face_t* HarfBuzzFace = hb_ft_face_create_referenced(Face);
	HarfBuzzFont = hb_font_create(HarfBuzzFace);
	hb_face_destroy(HarfBuzzFace);
	hb_ot_font_set_funcs(HarfBuzzFont);
	// A face drawn from colour bitmap strikes advances by the strike's own advance, which is what its bitmap was drawn to
	// and what browsers advance by -- Noto Color Emoji's 136 px at 109 ppem is 39.93 px at 32 px, where hmtx says 39.84.
	// HarfBuzz reads hmtx, so such a face shapes through a sub-font that answers the strike's advances and takes the rest
	// (cmap, GSUB, GPOS) from its parent.
	if (FT_HAS_COLOR(Face) && FT_HAS_FIXED_SIZES(Face) && Face->num_fixed_sizes > 0)
	{
		// The sub-font's functions: its own horizontal advances, everything else its parent's. Made once and never changed
		// again (immutable), so every strike font shares them; never destroyed, as they live as long as the module.
		static hb_font_funcs_t* const StrikeFontFuncs = []()
		{
			hb_font_funcs_t* NewFuncs = hb_font_funcs_create();
			hb_font_funcs_set_glyph_h_advance_func(NewFuncs, &UDreamUIFontData_FreeTypeRender::GetHarfBuzzStrikeAdvance, nullptr, nullptr);
			hb_font_funcs_make_immutable(NewFuncs);
			return NewFuncs;
		}();
		HarfBuzzStrikeFont = hb_font_create_sub_font(HarfBuzzFont);
		// The font data is this font, which owns the sub-font and destroys it before it goes (DeinitHarfBuzz).
		hb_font_set_funcs(HarfBuzzStrikeFont, StrikeFontFuncs, this, nullptr);
	}
#endif
}

void UDreamUIFontData_FreeTypeRender::DeinitHarfBuzz()
{
	StrikeAdvanceCache.Reset();
#if WITH_HARFBUZZ
	// The sub-font holds a reference to its parent; it goes first.
	if (HarfBuzzStrikeFont != nullptr)
	{
		hb_font_destroy(HarfBuzzStrikeFont);
		HarfBuzzStrikeFont = nullptr;
	}
	if (HarfBuzzFont != nullptr)
	{
		hb_font_destroy(HarfBuzzFont);
		HarfBuzzFont = nullptr;
	}
#endif
}

int32 UDreamUIFontData_FreeTypeRender::GetHarfBuzzStrikeAdvance(hb_font_t* InFont, void* InFontData, uint32 InGlyph, void* InUserData)
{
#if WITH_HARFBUZZ && WITH_FREETYPE
	UDreamUIFontData_FreeTypeRender* Font = static_cast<UDreamUIFontData_FreeTypeRender*>(InFontData);
	int XScale = 0;
	int YScale = 0;
	hb_font_get_scale(InFont, &XScale, &YScale);
	const uint64 CacheKey = ((uint64)InGlyph << 32) | (uint64)(uint32)XScale;
	if (const int32* Cached = Font->StrikeAdvanceCache.Find(CacheKey))
	{
		return *Cached;
	}
	int32 Advance = 0;
	float AdvancePixels = 0.0f;
	if (Font->Face != nullptr && FDreamGlyphColor::GetStrikeAdvance(Font->Face, InGlyph, XScale / 64.0f, AdvancePixels))
	{
		Advance = FMath::RoundToInt(AdvancePixels * 64.0f);
	}
	else
	{
		// A glyph the strikes do not hold (a space, a joiner): hmtx, as the parent font reads it, at this font's scale.
		hb_font_t* Parent = hb_font_get_parent(InFont);
		int ParentXScale = 0;
		int ParentYScale = 0;
		hb_font_get_scale(Parent, &ParentXScale, &ParentYScale);
		const hb_position_t ParentAdvance = hb_font_get_glyph_h_advance(Parent, InGlyph);
		Advance = ParentXScale != 0 ? (int32)((int64)ParentAdvance * XScale / ParentXScale) : 0;
	}
	if (Font->StrikeAdvanceCache.Num() >= DreamFreeTypeRenderLocal::MaxStrikeAdvanceCacheEntries)
	{
		Font->StrikeAdvanceCache.Reset();
	}
	Font->StrikeAdvanceCache.Add(CacheKey, Advance);
	return Advance;
#else
	return 0;
#endif
}

bool UDreamUIFontData_FreeTypeRender::HasKerning()
{
#if WITH_HARFBUZZ
	// GPOS kerning is applied by the shaper whenever the font has it; the old flag only saw 'kern'.
	if (HarfBuzzFont != nullptr)
	{
		return true;
	}
#endif
	return bHasKerning;
}

int32 UDreamUIFontData_FreeTypeRender::GetFaceCount()
{
	return 1 + Fallbacks.Num();
}

bool UDreamUIFontData_FreeTypeRender::FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint)
{
#if WITH_FREETYPE
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	if (TargetFace == nullptr)
	{
		return false;
	}
	// Remembered for the face as it is now. Its font is opened by now (GetFreeTypeFace), so its face epoch is the one of the
	// face in use: an answer kept under another epoch was another face's, and nothing kept is trusted any more.
	const bool bCacheable = FaceIndex >= 0 && FaceIndex < 64;
	const uint64 FaceBit = bCacheable ? (uint64)1 << FaceIndex : 0;
	if (bCacheable)
	{
		const UDreamUIFontData_FreeTypeRender* Owner = GetFaceOwner(FaceIndex);
		const uint32 OwnerEpoch = Owner != nullptr ? Owner->FaceEpoch : 0;
		if (CodepointFacesEpochs.Num() <= FaceIndex)
		{
			CodepointFacesEpochs.SetNumZeroed(FaceIndex + 1);
		}
		if (CodepointFacesEpochs[FaceIndex] != OwnerEpoch)
		{
			if (CodepointFacesEpochs[FaceIndex] != 0)
			{
				ResetCodepointFaces();
				CodepointFacesEpochs.SetNumZeroed(FaceIndex + 1);
			}
			CodepointFacesEpochs[FaceIndex] = OwnerEpoch;
		}
		if (const FCodepointFaces* Known = CodepointFaces.Find(Codepoint); Known != nullptr && (Known->Known & FaceBit) != 0)
		{
			return (Known->Has & FaceBit) != 0;
		}
	}
	bool bHas = false;
	const uint32 GlyphIndex = FT_Get_Char_Index(TargetFace, Codepoint);
	if (GlyphIndex == 0)
	{
		bHas = false;
	}
	else if (!FT_HAS_COLOR(TargetFace))
	{
		bHas = true;
	}
	// A colour face has a code point when what it holds for it can be drawn here; otherwise the next face, or the text's
	// emoji data, answers for it.
	else if (!CanHoldColorGlyphs())
	{
		// An atlas without colour (the single-channel field) draws a colour face's glyph only from its outline.
		bHas = FT_IS_SCALABLE(TargetFace)
			&& FT_Load_Glyph(TargetFace, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_IGNORE_TRANSFORM) == 0
			&& TargetFace->glyph->format == FT_GLYPH_FORMAT_OUTLINE
			&& TargetFace->glyph->outline.n_points > 0;
	}
	else
	{
		// COLRv1 paints with no v0 layers, or SVG alone: nothing FreeType composites, so nothing drawn.
		bHas = GetGlyphColorKind(FaceIndex, GlyphIndex) != (uint8)EDreamGlyphColorKind::Unsupported;
	}
	if (bCacheable)
	{
		// Text uses a few thousand code points at the most; a cache past that many is a stream of distinct ones, started again.
		if (CodepointFaces.Num() >= 65536)
		{
			CodepointFaces.Reset();
		}
		FCodepointFaces& Entry = CodepointFaces.FindOrAdd(Codepoint);
		Entry.Known |= FaceBit;
		if (bHas)
		{
			Entry.Has |= FaceBit;
		}
	}
	return bHas;
#else
	return false;
#endif
}

void UDreamUIFontData_FreeTypeRender::ResetCodepointFaces()
{
	CodepointFaces.Reset();
	CodepointFacesEpochs.Reset();
	// What a face's glyphs hold goes with what its cmap has: both were asked of the face that is no longer there.
	ColorGlyphInfos.Reset();
}

void* UDreamUIFontData_FreeTypeRender::GetShapingFont(int32 FaceIndex, float FontSize)
{
#if WITH_HARFBUZZ && WITH_FREETYPE
	if (FaceIndex != 0)
	{
		// A fallback or a style face shapes with its own font's tables, at its own face 0.
		UDreamUIFontData_FreeTypeRender* Owner = GetFaceOwner(FaceIndex);
		return Owner != nullptr ? Owner->GetShapingFont(0, FontSize) : nullptr;
	}
	InitFreeType();
	// A strike face's font advances by its strikes (InitHarfBuzz); its parent keeps its own scale, which the sub-font's
	// inherited functions convert from.
	hb_font_t* ShapingFont = HarfBuzzStrikeFont != nullptr ? HarfBuzzStrikeFont : HarfBuzzFont;
	if (ShapingFont == nullptr)
	{
		return nullptr;
	}
	const int32 Scale = FMath::RoundToInt(FontSize * 64.0f);
	hb_font_set_scale(ShapingFont, Scale, Scale);
	return ShapingFont;
#else
	return nullptr;
#endif
}

bool UDreamUIFontData_FreeTypeRender::ResolveCodepoint(uint32 Codepoint, FDreamUIGlyphKey& OutKey)
{
#if WITH_FREETYPE
	// One code point as a cluster of its own, in the game's language: the faces a text would try for it, in that order.
	const FDreamTextLanguage& Language = DreamFreeTypeRenderLocal::GetGameLanguage();
	FDreamFontFaceQuery Query;
	Query.Cluster = TConstArrayView<uint32>(&Codepoint, 1);
	Query.Cultures = Language.PrioritizedCultureNames;
	Query.Presentation = FDreamFontFaceResolver::GetPresentation(Query.Cluster);
	const FDreamFontFaceChoice Choice = FDreamFontFaceResolver::Resolve(this, Query);
	if (Choice.bCoversBase)
	{
		if (FT_FaceRec_* TargetFace = GetFreeTypeFace(Choice.FaceIndex))
		{
			const uint32 GlyphIndex = FT_Get_Char_Index(TargetFace, Codepoint);
			if (GlyphIndex != 0)
			{
				OutKey = FDreamUIGlyphKey(Choice.FaceIndex, GlyphIndex);
				return true;
			}
		}
	}
	// Nothing has it: the primary face's .notdef, so the text still takes up room.
	if (GetFreeTypeFace(0) != nullptr)
	{
		OutKey = FDreamUIGlyphKey(0, 0);
		return true;
	}
#endif
	return false;
}

const FDreamFontFaceTable& UDreamUIFontData_FreeTypeRender::GetFaceTable()
{
	// Rebuilt when the fallbacks changed -- or when an undo put another list back, which says nothing but the count.
	if (bFaceTableDirty || FaceTable.Faces.Num() != Fallbacks.Num() + 1)
	{
		FaceTable.Faces.Reset(Fallbacks.Num() + 1);
		// Face 0 is the font itself: what its entry would say is never read.
		FaceTable.Faces.AddDefaulted();
		for (const FDreamUIFontFallback& Entry : Fallbacks)
		{
			FDreamFontFaceInfo& Info = FaceTable.Faces.AddDefaulted_GetRef();
			Info.Ranges = Entry.Ranges;
			// "ja; zh-Hans;" is two cultures, as Slate reads the list.
			TArray<FString> Cultures;
			Entry.Cultures.ParseIntoArray(Cultures, TEXT(";"), true);
			for (FString& Culture : Cultures)
			{
				Culture.TrimStartAndEndInline();
				if (!Culture.IsEmpty())
				{
					Info.Cultures.Add(MoveTemp(Culture));
				}
			}
			// The property's own lower bound, for an entry made in code.
			Info.Scale = FMath::Max(Entry.Scale, 0.1f);
			Info.bPreferOverPrimary = Entry.bPreferOverPrimary;
		}
		FaceTable.bPreferColorEmoji = bPreferColorEmoji;
		bFaceTableDirty = false;
	}
	return FaceTable;
}

bool UDreamUIFontData_FreeTypeRender::IsColorFace(int32 FaceIndex)
{
#if WITH_FREETYPE
	if (!CanHoldColorGlyphs())
	{
		return false;
	}
	// Asked of the face as it is now, rather than remembered: a fallback that reloads with another file says so at once.
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	return TargetFace != nullptr && FT_HAS_COLOR(TargetFace);
#else
	return false;
#endif
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::GetFaceCharData(int32 FaceIndex, uint32 CharCode, float CharSize, bool IsBold)
{
	uint32 GlyphIndex = 0;
#if WITH_FREETYPE
	// That face's glyph for the code point, its .notdef (glyph 0) when it has none.
	if (FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex))
	{
		GlyphIndex = FT_Get_Char_Index(TargetFace, CharCode);
	}
#endif
	return GetGlyphData(FaceIndex, GlyphIndex, CharSize, IsBold);
}

FDreamUIFontFaceIdentity UDreamUIFontData_FreeTypeRender::GetFaceIdentity(int32 FaceIndex)
{
	FDreamUIFontFaceIdentity Identity;
#if WITH_FREETYPE
	UDreamUIFontData_FreeTypeRender* Owner = GetFaceOwner(FaceIndex);
	// Opened first, so the epoch named is the one of the face being shaped with, not the one before its first load. A face
	// that does not open is nobody's: nothing is cached for it.
	if (Owner == nullptr || Owner->GetFreeTypeFace(0) == nullptr)
	{
		return Identity;
	}
	Identity.Owner = FObjectKey(Owner);
	Identity.Epoch = Owner->FaceEpoch;
#endif
	return Identity;
}

uint32 UDreamUIFontData_FreeTypeRender::GetLayoutEpoch() const
{
	// A kept layout knows the faces it drew from (GetFaceIdentity), not the ones it only asked: a fallback that lacked a code
	// point, reloaded since with a file that has it, would now draw it in place of the face the kept layout took. Every
	// fallback and style font's face epoch is in this one, read as it is: a font not opened yet stays unopened, and opening
	// it later moves this on, which costs a layout from nothing once.
	uint32 Epoch = LayoutEpoch;
	auto FoldOwner = [this, &Epoch](const UDreamUIFontData_FreeTypeRender* Owner)
	{
		if (Owner != nullptr && Owner != this)
		{
			Epoch = HashCombineFast(Epoch, Owner->FaceEpoch);
		}
	};
	for (const FDreamUIFontFallback& Entry : Fallbacks)
	{
		FoldOwner(Entry.Font.Get());
	}
	FoldOwner(BoldFont.Get());
	FoldOwner(ItalicFont.Get());
	FoldOwner(BoldItalicFont.Get());
	return Epoch;
}

uint8 UDreamUIFontData_FreeTypeRender::GetGlyphColorKind(int32 FaceIndex, uint32 GlyphIndex)
{
#if WITH_FREETYPE
	const FDreamUIGlyphKey Key(FaceIndex, GlyphIndex);
	if (const FColorGlyphInfo* Known = ColorGlyphInfos.Find(Key); Known != nullptr && Known->bKindKnown)
	{
		return Known->Kind;
	}
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	const uint8 Kind = (uint8)(TargetFace != nullptr ? FDreamGlyphColor::GetColorKind(TargetFace, GlyphIndex) : EDreamGlyphColorKind::None);
	FColorGlyphInfo& Info = ColorGlyphInfos.FindOrAdd(Key);
	Info.Kind = Kind;
	Info.bKindKnown = true;
	return Kind;
#else
	return (uint8)EDreamGlyphColorKind::None;
#endif
}

UTexture2DArray* UDreamUIFontData_FreeTypeRender::GetFontTexture()
{
	return Texture;
}

void UDreamUIFontData_FreeTypeRender::PostLoad()
{
	Super::PostLoad();
	if (!bCultureFont)
		return;

	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDreamUIFontData_FreeTypeRender::UpdateFontOnCultureChanged);

	FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (CultureFontMap.Contains(CurrentCulture))
		EngineFont = CultureFontMap[CurrentCulture].LoadSynchronous();
}

void UDreamUIFontData_FreeTypeRender::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDreamGUIObjectVersion::GUID);
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		// Fallbacks were saved with plain font references before they had settings: each becomes an entry with the
		// defaults -- every code point, any language, scale 1 -- in the same order, so face index i is still entry i - 1. An
		// empty slot stays an empty face. The old list is emptied, and is saved empty from here on.
		if (Ar.CustomVer(FDreamGUIObjectVersion::GUID) < FDreamGUIObjectVersion::FontFallbackEntries && FallbackFontArray.Num() > 0)
		{
			Fallbacks.Reset(FallbackFontArray.Num());
			for (const TObjectPtr<UDreamUIFontData_FreeTypeRender>& Fallback : FallbackFontArray)
			{
				FDreamUIFontFallback& Entry = Fallbacks.AddDefaulted_GetRef();
				Entry.Font = Fallback;
			}
			FallbackFontArray.Empty();
		}
		bFaceTableDirty = true;
	}
}

void UDreamUIFontData_FreeTypeRender::BeginDestroy()
{
	if (bCultureFont)
	{
		if (OnCultureChangedDelegateHandle.IsValid())
		{
			FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
		}
	}
	ReleaseFontTexture();
	Super::BeginDestroy();
}

void UDreamUIFontData_FreeTypeRender::InitFont()
{
#if WITH_FREETYPE
	InitFreeType();
	// Called by what draws with the font, which wants its atlas (GetFontTexture) from the start.
	if (bAlreadyInitialized && Texture == nullptr)
	{
		FlushGlyphAtlas();
	}
#endif
}

float UDreamUIFontData_FreeTypeRender::GetKerning(uint32 LeftCharCode, uint32 RightCharCode, float CharSize)
{
#if WITH_FREETYPE
	if (!bHasKerning)return 0;
	// Kerning is a property of one face: a pair is only kerned when both characters come from the same
	// one. Asking the primary face for a pair it does not have (the CJK case, where both glyphs are on
	// a fallback) used to return the primary's kerning for two .notdefs.
	FDreamUIGlyphKey LeftKey, RightKey;
	if (!ResolveCodepoint(LeftCharCode, LeftKey) || !ResolveCodepoint(RightCharCode, RightKey))return 0;
	if (LeftKey.FaceIndex != RightKey.FaceIndex)return 0;
	FT_FaceRec_* TargetFace = GetFreeTypeFace(LeftKey.FaceIndex);
	// A face of bitmap strikes (emoji) takes no char size, and kerns nothing.
	if (TargetFace == nullptr || !FT_IS_SCALABLE(TargetFace))return 0;
	auto error = DreamFreeTypeMetricsLocal::SetMetricSize(TargetFace, CharSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Char_Size error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	FT_Vector kerning;
	error = FT_Get_Kerning(TargetFace, LeftKey.GlyphIndex, RightKey.GlyphIndex, FT_KERNING_DEFAULT, &kerning);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Get_Kerning error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	return kerning.x * ONE_DIVIDE_64;
#else
	return 0;
#endif
}

bool UDreamUIFontData_FreeTypeRender::GetFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight)
{
	return ComputeFaceMetrics(FaceIndex, FontSize, OutAscent, OutDescent, OutLineHeight);
}

bool UDreamUIFontData_FreeTypeRender::ComputeFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight)
{
#if WITH_FREETYPE
	// Every line asks for every face on it, every layout. The answer only changes when the font is
	// reloaded, and DeinitFreeType drops the cache then.
	const FFaceMetricsKey CacheKey{ FaceIndex, FontSize };
	if (const FFaceMetricsValue* Cached = FaceMetricsCache.Find(CacheKey))
	{
		OutAscent = Cached->Ascent;
		OutDescent = Cached->Descent;
		OutLineHeight = Cached->LineHeight;
		return true;
	}
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	if (TargetFace == nullptr)return false;
	// This font's choice of metrics applies to every face it lays out with, fallbacks and style faces included: a line
	// box built from two faces measured two different ways would not line up.
	float Ascender = 0.0f, Descender = 0.0f, LineSpacing = 0.0f;
	if (!DreamFreeTypeMetricsLocal::ReadVerticalMetrics(TargetFace, VerticalMetrics, FontSize, Ascender, Descender, LineSpacing))return false;
	// FontSizeAsLineHeight keeps the font's own proportions inside the smaller box.
	if (LineHeightType == EDreamUIDynamicFontLineHeightType::FontSizeAsLineHeight)
	{
		const float Sum = Ascender + Descender;
		OutAscent = Sum > 0.0f ? FontSize * (Ascender / Sum) : FontSize * 0.8f;
		OutDescent = Sum > 0.0f ? FontSize * (Descender / Sum) : FontSize * 0.2f;
		OutLineHeight = FontSize;
	}
	else
	{
		OutAscent = Ascender;
		OutDescent = Descender;
		OutLineHeight = LineHeightType == EDreamUIDynamicFontLineHeightType::FromFontFace ? LineSpacing : FontSize;
	}
	FaceMetricsCache.Add(CacheKey, FFaceMetricsValue{ OutAscent, OutDescent, OutLineHeight });
	return true;
#else
	return false;
#endif
}

bool UDreamUIFontData_FreeTypeRender::GetDecorationMetrics(int32 FaceIndex, float FontSize, float& OutUnderlinePosition, float& OutUnderlineThickness, float& OutStrikethroughPosition, float& OutStrikethroughThickness)
{
#if WITH_FREETYPE
	const FFaceDecorationMetrics* Metrics = FaceDecorationCache.Find(FaceIndex);
	if (Metrics == nullptr)
	{
		FFaceDecorationMetrics Value;
		FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
		if (TargetFace != nullptr && FT_IS_SCALABLE(TargetFace) && TargetFace->units_per_EM != 0 && TargetFace->underline_thickness > 0)
		{
			const float PerEm = 1.0f / (float)TargetFace->units_per_EM;
			// FreeType has already moved the 'post' table's underline position (the top of the stroke) to its centre.
			Value.UnderlinePosition = TargetFace->underline_position * PerEm;
			Value.UnderlineThickness = TargetFace->underline_thickness * PerEm;
			const TT_OS2* OS2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(TargetFace, FT_SFNT_OS2));
			if (OS2 != nullptr && OS2->version != 0xFFFFu && OS2->yStrikeoutSize > 0)
			{
				// The OpenType spec puts yStrikeoutPosition at the top of the stroke, not its centre.
				Value.StrikethroughThickness = OS2->yStrikeoutSize * PerEm;
				Value.StrikethroughPosition = (OS2->yStrikeoutPosition - OS2->yStrikeoutSize * 0.5f) * PerEm;
			}
			else
			{
				// No OS/2 strikeout: Slate's rule (FSlateFontRenderer::GetStrikeMetrics), the underline's thickness at 60% of
				// the face's line spacing, of which its line highlighter draws the top of the stroke half that far up.
				Value.StrikethroughThickness = Value.UnderlineThickness;
				Value.StrikethroughPosition = 0.3f * TargetFace->height * PerEm - Value.StrikethroughThickness * 0.5f;
			}
			Value.bValid = true;
		}
		Metrics = &FaceDecorationCache.Add(FaceIndex, Value);
	}
	if (!Metrics->bValid)
	{
		return false;
	}
	OutUnderlinePosition = Metrics->UnderlinePosition * FontSize;
	OutUnderlineThickness = Metrics->UnderlineThickness * FontSize;
	OutStrikethroughPosition = Metrics->StrikethroughPosition * FontSize;
	OutStrikethroughThickness = Metrics->StrikethroughThickness * FontSize;
	return true;
#else
	return false;
#endif
}

float UDreamUIFontData_FreeTypeRender::GetLineHeight(float FontSize)
{
#if WITH_FREETYPE
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!ComputeFaceMetrics(0, FontSize, Ascent, Descent, LineHeightValue))return FontSize;
	return LineHeightValue;
#else
	return FontSize;
#endif
}
float UDreamUIFontData_FreeTypeRender::GetAscent(float FontSize)
{
#if WITH_FREETYPE
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!ComputeFaceMetrics(0, FontSize, Ascent, Descent, LineHeightValue))return Super::GetAscent(FontSize);
	return Ascent;
#else
	return Super::GetAscent(FontSize);
#endif
}

float UDreamUIFontData_FreeTypeRender::GetDescent(float FontSize)
{
#if WITH_FREETYPE
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!ComputeFaceMetrics(0, FontSize, Ascent, Descent, LineHeightValue))return Super::GetDescent(FontSize);
	return Descent;
#else
	return Super::GetDescent(FontSize);
#endif
}

float UDreamUIFontData_FreeTypeRender::GetVerticalOffset(float FontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return FontSize;
	if (!FT_IS_SCALABLE(Face))
	{
		// A face without outlines takes no char size; its line comes from its tables, as its face metrics do.
		float Ascender = 0.0f, Descender = 0.0f, LineSpacing = 0.0f;
		if (!DreamFreeTypeMetricsLocal::ReadVerticalMetrics(Face, EDreamUIFontVerticalMetrics::FreeType, FontSize, Ascender, Descender, LineSpacing))return 0;
		return -(Ascender - Descender) * 0.5f;
	}
	auto error = DreamFreeTypeMetricsLocal::SetMetricSize(Face, FontSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Char_Size error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	return -((Face->size->metrics.ascender + Face->size->metrics.descender) * ONE_DIVIDE_64) * 0.5f;
#else
	return FontSize;
#endif
}

void UDreamUIFontData_FreeTypeRender::AddUIText(UDreamText* InText)
{
	RenderTextArray.AddUnique(InText);
}
void UDreamUIFontData_FreeTypeRender::RemoveUIText(UDreamText* InText)
{
	RenderTextArray.Remove(InText);
}

void UDreamUIFontData_FreeTypeRender::SetFontType(EDreamUIDynamicFontDataType Value)
{
	FontType = Value;
}

void UDreamUIFontData_FreeTypeRender::SetFontFilePath(const FString& InPath, bool bInRelativeToProjectDir)
{
	FontType = EDreamUIDynamicFontDataType::CustomFontFile;
	FontFilePath = InPath;
	bUseRelativeFilePath = bInRelativeToProjectDir;
	bUseExternalFileOrEmbedInToUAsset = true;
#if WITH_FREETYPE
	// A font whose last attempt failed is reset too: InitFreeType does not retry a failure, so a new path given to a
	// font that could not load the old one was never tried.
	if (bAlreadyInitialized || bInitFailed)
	{
		DeinitFreeType();
	}
#endif
}

void UDreamUIFontData_FreeTypeRender::SetFallbackFonts(const TArray<UDreamUIFontData_FreeTypeRender*>& InFallbacks)
{
	TArray<FDreamUIFontFallback> Entries;
	Entries.Reserve(InFallbacks.Num());
	for (UDreamUIFontData_FreeTypeRender* Fallback : InFallbacks)
	{
		FDreamUIFontFallback& Entry = Entries.AddDefaulted_GetRef();
		Entry.Font = Fallback;
	}
	SetFallbacks(Entries);
}

void UDreamUIFontData_FreeTypeRender::SetFallbacks(const TArray<FDreamUIFontFallback>& InFallbacks)
{
	Fallbacks.Reset(InFallbacks.Num());
	for (const FDreamUIFontFallback& Entry : InFallbacks)
	{
		if (Entry.Font != nullptr && Entry.Font != this)
		{
			Fallbacks.Add(Entry);
		}
	}
	ApplyFallbacksChanged();
}

void UDreamUIFontData_FreeTypeRender::ApplyFallbacksChanged()
{
	bFaceTableDirty = true;
	LayoutEpoch++;
	// Clearing the glyph cache was not enough: the worker kept reading the old fallback's file and drew the new face's
	// glyph ids from the old face's outlines, and the face metrics went on answering with the old line box.
	ResetFaceState();
	// A fallback's lines join the line box, at its scale: the texts' sizes may change, not only their glyphs.
	RecreateTexts();
}

void UDreamUIFontData_FreeTypeRender::RecreateTexts()
{
	for (const TWeakObjectPtr<UDreamText>& TextItem : RenderTextArray)
	{
		if (TextItem.IsValid())
		{
			TextItem->ApplyRecreateText();
		}
	}
}

void UDreamUIFontData_FreeTypeRender::SetStyleFonts(UDreamUIFontData_FreeTypeRender* InBold, UDreamUIFontData_FreeTypeRender* InItalic, UDreamUIFontData_FreeTypeRender* InBoldItalic)
{
	// A font is not its own style face; the regular face is what a missing style falls back to anyway.
	BoldFont = InBold != this ? InBold : nullptr;
	ItalicFont = InItalic != this ? InItalic : nullptr;
	BoldItalicFont = InBoldItalic != this ? InBoldItalic : nullptr;
	LayoutEpoch++;
	ResetFaceState();
}

void UDreamUIFontData_FreeTypeRender::SetVerticalMetrics(EDreamUIFontVerticalMetrics InVerticalMetrics)
{
	if (VerticalMetrics == InVerticalMetrics)
	{
		return;
	}
	VerticalMetrics = InVerticalMetrics;
	LayoutEpoch++;
	// Glyph quads do not depend on the line box, so the atlas stays; only the line metrics and the layouts built on them go.
	FaceMetricsCache.Reset();
	RecreateTexts();
}

void UDreamUIFontData_FreeTypeRender::ResetFaceState()
{
	Rasterizer.Reset();
	PendingAsyncGlyphs.Reset();
	PendingColorGlyphs.Reset();
	PendingCoverageGlyphs.Reset();
	FontsWithAsyncGlyphs.Remove(this);
	FaceMetricsCache.Reset();
	FaceDecorationCache.Reset();
	ColorGlyphInfos.Reset();
	ResetCodepointFaces();
	bFaceTableDirty = true;
	if (Texture != nullptr)
	{
		// The atlas holds glyphs keyed by face index, and the faces behind those indices just changed. Starting it again
		// clears the glyph caches -- field, colour and coverage -- and tells every text using this font to lay out again.
		FlushGlyphAtlas();
		return;
	}
	ClearAtlasCaches();
	for (const TWeakObjectPtr<UDreamText>& TextItem : RenderTextArray)
	{
		if (TextItem.IsValid())
		{
			TextItem->ApplyFontTextureChange();
		}
	}
}

void UDreamUIFontData_FreeTypeRender::SetEngineFont(UFontFace* Value)
{
	EngineFont = Value;
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::GetCharData(uint32 CharCode, float CharSize, bool IsBold)
{
#if !WITH_FREETYPE
	// Without FreeType every glyph is zero-sized and a whole run of text measures zero by zero. On a
	// dedicated server that is the intended trade (DreamGUI.Build.cs), but it used to be entirely
	// silent, so anyone who hit it on a client target had nothing to go on.
	static bool bLoggedNoFreeType = false;
	if (!bLoggedNoFreeType)
	{
		bLoggedNoFreeType = true;
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This target was built without FreeType (WITH_FREETYPE=0), so no glyph has a size and every text measures zero by zero. (reported once)")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
#endif
	FDreamUIGlyphKey Key;
	if (!ResolveCodepoint(CharCode, Key))
	{
		return FDreamUICharData();
	}
	return GetGlyphData(Key.FaceIndex, Key.GlyphIndex, CharSize, IsBold);
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::GetGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize, bool IsBold)
{
	checkf(IsInGameThread(), TEXT("DreamGUI dynamic font glyphs must be generated on the game thread."));
	// A flush asked for while an earlier frame was laid out happens here, before this frame hands out its first glyph,
	// so that no layout of this frame holds a glyph of the atlas that is about to go.
	if (bAtlasFlushRequested && AtlasFlushRequestFrame != GFrameCounter)
	{
		FlushGlyphAtlas();
	}
	// Every entry handed out says which glyph of which face it is -- the face sizes the line box, and the pair is what a
	// coverage glyph is fetched by -- empty and pending ones too.
	auto Stamped = [FaceIndex, GlyphIndex](FDreamUICharData InData)
	{
		InData.FaceIndex = FaceIndex;
		InData.GlyphIndex = GlyphIndex;
		return InData;
	};
	auto Result = FDreamUICharData();
	if (CharSize <= 0.0f)return Stamped(Result);
	const FDreamUIGlyphKey Key(FaceIndex, GlyphIndex);
#if WITH_FREETYPE
	// A colour face's colour glyphs never take the field path: msdfgen finds no outline in a strike's glyph and draws nothing,
	// and a COLR glyph's outline is only a silhouette. They are rasterized in colour, at a size, into a cache of their own.
	if (IsColorFace(FaceIndex))
	{
		const uint8 ColorKind = GetGlyphColorKind(FaceIndex, GlyphIndex);
		if (ColorKind == (uint8)EDreamGlyphColorKind::Bitmap || ColorKind == (uint8)EDreamGlyphColorKind::Layers)
		{
			return Stamped(GetColorGlyphData(FaceIndex, GlyphIndex, CharSize));
		}
	}
#endif
	// Shader-side bold keeps one atlas glyph per face; only the advance knows about the weight.
	const bool bShaderBold = IsBold && IsBoldSynthesizedInShader();
	const bool bAtlasBold = IsBold && !bShaderBold;
	if (!GetCharDataFromCache(Key, CharSize, bAtlasBold, Result))//if charData not cached, then create it and add to cache
	{
#if WITH_FREETYPE
		// A face with no outlines at all -- a bitmap strike's glyph that has no bitmap (a space), or any glyph of one on an
		// atlas that holds no colour -- has nothing to rasterize here: its advance, and no quad. Neither the worker nor the
		// synchronous path is asked, since both would fail on it every time it is asked.
		FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
		if (TargetFace != nullptr && !FT_IS_SCALABLE(TargetFace))
		{
			Result.XAdvance = GetUnrasterizedAdvance(FaceIndex, GlyphIndex, CharSize);
			return Stamped(Result);
		}
#endif
		// Off-thread when the font can and the frame's synchronous budget is spent.
		float PixelsPerEm = 0.0f, SpreadPixels = 0.0f, BoldPixels = 0.0f;
		if (UDreamUISettings::GetAsyncGlyphRasterization() && GetAsyncRasterParams(CharSize, bAtlasBold, PixelsPerEm, SpreadPixels, BoldPixels))
		{
			FAsyncGlyphRequest Request;
			Request.Glyph = Key;
			Request.CharSize = IsGlyphCacheSizeIndependent() ? 0.0f : CharSize;
			Request.bBold = bAtlasBold;
			if (PendingAsyncGlyphs.Contains(Request))
			{
				return Stamped(MakePendingCharData(Key, CharSize, IsBold));
			}
			if (!TakeSyncGlyphBudget())
			{
				FDreamGlyphRasterizer* Worker = GetOrCreateRasterizer();
				// A face the worker has no bytes for fails every job it is given; fall through to the
				// synchronous path for it instead of handing out a quad that never lands.
				if (Worker != nullptr && Worker->HasFaceSource(Key.FaceIndex))
				{
					FDreamGlyphRasterizer::FJob Job;
					Job.Kind = EDreamGlyphJobKind::Field;
					Job.Key = Key;
					Job.CharSize = CharSize;
					Job.bBold = bAtlasBold;
					Job.PixelsPerEm = PixelsPerEm;
					Job.SpreadPixels = SpreadPixels;
					Job.BoldPixels = BoldPixels;
					Worker->Enqueue(Job);
					PendingAsyncGlyphs.Add(Request);
					FontsWithAsyncGlyphs.Add(this);
					return Stamped(MakePendingCharData(Key, CharSize, IsBold));
				}
			}
		}

		FGlyphBitmap glyphBitmap;
		if (!RenderGlyph(Key, CharSize, bAtlasBold, glyphBitmap))//no valid glyph
		{
			return Stamped(Result);
		}

		FDreamUICharData uiCharData;
		if (!InsertGlyphBitmap(glyphBitmap, uiCharData))
		{
			return Stamped(Result);
		}
		AddCharDataToCache(Key, CharSize, bAtlasBold, uiCharData);
		GetCharDataFromCache(Key, CharSize, bAtlasBold, Result);
	}
	if (bShaderBold)
	{
		Result.XAdvance += CharSize * GetBoldRatio();
	}
	// Which face answered, so the layout can size the line box against the face that is actually on it.
	return Stamped(Result);
}

bool UDreamUIFontData_FreeTypeRender::InsertGlyphBitmap(const FGlyphBitmap& InGlyphBitmap, FDreamUICharData& OutResult)
{
	// The atlas used to be made only when this font's own face loaded. A font whose own face failed still resolves code
	// points to its fallbacks, and their glyphs have to land somewhere: with no texture the packer was empty, and the
	// first glyph went straight to growing a texture that did not exist. Whatever has no atlas yet gets one here.
	if (Texture == nullptr)
	{
		FlushGlyphAtlas();
	}
	if (InGlyphBitmap.width <= 0 || InGlyphBitmap.height <= 0)//glyph no need to display, could be space
	{
		OutResult.Width = InGlyphBitmap.width;
		OutResult.Height = InGlyphBitmap.height;
		OutResult.XOffset = InGlyphBitmap.hOffset;
		OutResult.YOffset = InGlyphBitmap.vOffset;
		OutResult.XAdvance = InGlyphBitmap.hAdvance;
		OutResult.MinUV.X = OutResult.MaxUV.Y = OutResult.MaxUV.X = OutResult.MinUV.Y = 0.0f;//(0,0) point is transparent
		return true;
	}
	const int32 SPACE_NEED_EXPEND = this->Get_SPACE_NEED_EXPEND();
	const int32 SPACE_NEED_EXPENDx2 = SPACE_NEED_EXPEND + SPACE_NEED_EXPEND;
	const int32 SPACE_BETWEEN_GLYPH_RECT = this->Get_SPACE_BETWEEN_GLYPH() + SPACE_NEED_EXPEND;
	const int32 GlyphWidth = (int32)InGlyphBitmap.width;
	const int32 GlyphHeight = (int32)InGlyphBitmap.height;
	int32 Slice = 0, PackedX = 0, PackedY = 0;
	if (!PackAtlasRect(false, GlyphWidth + 2 * SPACE_BETWEEN_GLYPH_RECT, GlyphHeight + 2 * SPACE_BETWEEN_GLYPH_RECT, Slice, PackedX, PackedY))
	{
		return false;
	}
	//remove space
	const int32 GlyphX = PackedX + SPACE_BETWEEN_GLYPH_RECT;
	const int32 GlyphY = PackedY + SPACE_BETWEEN_GLYPH_RECT;
	if (!UpdateFontTextureRegion(GlyphX, GlyphY, Slice, GlyphWidth, GlyphHeight, GlyphWidth * InGlyphBitmap.pixelSize, InGlyphBitmap.pixelSize, InGlyphBitmap.buffer))
	{
		return false;
	}
	OutResult.Width = InGlyphBitmap.width + SPACE_NEED_EXPENDx2;
	OutResult.Height = InGlyphBitmap.height + SPACE_NEED_EXPENDx2;
	OutResult.XOffset = InGlyphBitmap.hOffset - SPACE_NEED_EXPEND;
	OutResult.YOffset = InGlyphBitmap.vOffset + SPACE_NEED_EXPEND;
	OutResult.XAdvance = InGlyphBitmap.hAdvance;
	OutResult.MinUV.X = OneDivideTextureSize * (GlyphX - SPACE_NEED_EXPEND);
	OutResult.MaxUV.Y = OneDivideTextureSize * (GlyphY - SPACE_NEED_EXPEND + OutResult.Height);
	OutResult.MaxUV.X = OneDivideTextureSize * (GlyphX - SPACE_NEED_EXPEND + OutResult.Width);
	OutResult.MinUV.Y = OneDivideTextureSize * (GlyphY - SPACE_NEED_EXPEND);
	OutResult.SliceIndex = Slice;
	return true;
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::GetColorGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize)
{
#if WITH_FREETYPE
	auto MakeCharData = [CharSize](const FColorGlyphEntry& InEntry)
	{
		// Size-independent within its bucket: the GPU scales the stored bitmap the rest of the way, 6% at most.
		FDreamUICharData Data = InEntry.Texels;
		const float Scale = InEntry.TexelsPerEm > 0.0f ? CharSize / InEntry.TexelsPerEm : 0.0f;
		Data.Width *= Scale;
		Data.Height *= Scale;
		Data.XOffset *= Scale;
		Data.YOffset *= Scale;
		Data.XAdvance *= Scale;
		Data.bColor = true;
		Data.ColorTexelsPerEm = InEntry.TexelsPerEm;
		return Data;
	};
	const FColorGlyphKey ColorKey(FaceIndex, GlyphIndex, FDreamGlyphColor::GetSizeBucket(CharSize));
	if (const FColorGlyphEntry* Entry = ColorGlyphs.Find(ColorKey))
	{
		return MakeCharData(*Entry);
	}
	const FDreamUIGlyphKey Key(FaceIndex, GlyphIndex);
	if (PendingColorGlyphs.Contains(ColorKey))
	{
		return MakePendingCharData(Key, CharSize, false);
	}
	FDreamGlyphColorParams Params;
	Params.TargetPixelSize = ColorKey.SizeBucket;
	Params.ReachEm = GetColorGlyphReachEm();
	// Off-thread like a field glyph once the frame's synchronous budget -- shared with the field glyphs -- is spent.
	if (UDreamUISettings::GetAsyncGlyphRasterization() && !TakeSyncGlyphBudget())
	{
		FDreamGlyphRasterizer* Worker = GetOrCreateRasterizer();
		if (Worker != nullptr && Worker->HasFaceSource(FaceIndex))
		{
			FDreamGlyphRasterizer::FJob Job;
			Job.Kind = EDreamGlyphJobKind::Color;
			Job.Key = Key;
			Job.CharSize = CharSize;
			Job.Color = Params;
			Worker->Enqueue(Job);
			PendingColorGlyphs.Add(ColorKey);
			FontsWithAsyncGlyphs.Add(this);
			return MakePendingCharData(Key, CharSize, false);
		}
	}
	FColorGlyphEntry NewEntry;
	FDreamGlyphColorResult Color;
	if (!FDreamGlyphColor::Rasterize(GetFreeTypeFace(FaceIndex), GlyphIndex, Params, Color) || !InsertColorGlyph(Color, NewEntry))
	{
		NewEntry = MakeFailedColorGlyph(FaceIndex, GlyphIndex, ColorKey.SizeBucket);
	}
	return MakeCharData(ColorGlyphs.Add(ColorKey, NewEntry));
#else
	// Without FreeType no face is a colour face, and nothing asks.
	return FDreamUICharData();
#endif
}

bool UDreamUIFontData_FreeTypeRender::InsertColorGlyph(const FDreamGlyphColorResult& InColor, FColorGlyphEntry& OutEntry)
{
	if (Texture == nullptr)
	{
		FlushGlyphAtlas();
	}
	OutEntry = FColorGlyphEntry();
	OutEntry.TexelsPerEm = InColor.TexelsPerEm;
	FDreamUICharData& Texels = OutEntry.Texels;
	Texels.XAdvance = InColor.Advance;
	Texels.bColor = true;
	Texels.ColorTexelsPerEm = InColor.TexelsPerEm;
	if (InColor.TexelsPerEm <= 0.0f)
	{
		return false;
	}
	if (InColor.Width <= 0 || InColor.Height <= 0)
	{
		// Nothing to draw (an empty bitmap): the advance, and the transparent corner of the atlas.
		return true;
	}
	if (InColor.Pixels.Num() < InColor.Width * InColor.Height * 4)
	{
		return false;
	}
	// The bitmap already has its transparent padding; one more ring of (0,0,0,0) around the cell keeps a magnified quad's
	// edge texel from blending with whatever is packed next to it, or with the white the bitmap font's atlas starts as.
	const int32 PaddedWidth = InColor.Width + 2;
	const int32 PaddedHeight = InColor.Height + 2;
	int32 Slice = 0, PackedX = 0, PackedY = 0;
	if (!PackAtlasRect(false, PaddedWidth, PaddedHeight, Slice, PackedX, PackedY))
	{
		return false;
	}
	TArray<uint8> Padded;
	Padded.SetNumZeroed(PaddedWidth * PaddedHeight * 4);
	for (int32 Row = 0; Row < InColor.Height; Row++)
	{
		FMemory::Memcpy(Padded.GetData() + ((int64)(Row + 1) * PaddedWidth + 1) * 4, InColor.Pixels.GetData() + (int64)Row * InColor.Width * 4, InColor.Width * 4);
	}
	if (!UpdateFontTextureRegion(PackedX, PackedY, Slice, PaddedWidth, PaddedHeight, PaddedWidth * 4, 4, Padded))
	{
		return false;
	}
	// The quad is the bitmap's whole padded cell: an underlay sampled at UV minus its offset stays inside it.
	Texels.Width = InColor.Width;
	Texels.Height = InColor.Height;
	Texels.XOffset = InColor.Left;
	Texels.YOffset = InColor.Top;
	Texels.MinUV = FVector2f((PackedX + 1) * OneDivideTextureSize, (PackedY + 1) * OneDivideTextureSize);
	Texels.MaxUV = FVector2f((PackedX + 1 + InColor.Width) * OneDivideTextureSize, (PackedY + 1 + InColor.Height) * OneDivideTextureSize);
	Texels.SliceIndex = Slice;
	return true;
}

UDreamUIFontData_FreeTypeRender::FColorGlyphEntry UDreamUIFontData_FreeTypeRender::MakeFailedColorGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 SizeBucket)
{
	FColorGlyphEntry Entry;
	Entry.TexelsPerEm = (float)FMath::Max(SizeBucket, 1);
	Entry.Texels.XAdvance = GetUnrasterizedAdvance(FaceIndex, GlyphIndex, Entry.TexelsPerEm);
	Entry.Texels.bColor = true;
	Entry.Texels.ColorTexelsPerEm = Entry.TexelsPerEm;
	if (!bLoggedColorGlyphFailure)
	{
		bLoggedColorGlyphFailure = true;
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, the colour glyph %u of face %d could not be made at %d px; it takes its room and draws nothing. (reported once per font)")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), GlyphIndex, FaceIndex, SizeBucket);
	}
	return Entry;
}

float UDreamUIFontData_FreeTypeRender::GetUnrasterizedAdvance(int32 FaceIndex, uint32 GlyphIndex, float CharSize)
{
#if WITH_FREETYPE
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	if (TargetFace == nullptr)
	{
		return 0.0f;
	}
	// A colour strike face advances by its strike, as its shaping font does (GetHarfBuzzStrikeAdvance); anything else by hmtx.
	float Advance = 0.0f;
	if (FT_HAS_COLOR(TargetFace) && FT_HAS_FIXED_SIZES(TargetFace) && FDreamGlyphColor::GetStrikeAdvance(TargetFace, GlyphIndex, CharSize, Advance))
	{
		return Advance;
	}
	return DreamFreeTypeRenderLocal::GetDesignAdvance(TargetFace, GlyphIndex, CharSize);
#else
	return 0.0f;
#endif
}

int32 UDreamUIFontData_FreeTypeRender::GetAtlasSliceThreshold() const
{
	return AtlasSliceThreshold > 0 ? AtlasSliceThreshold : UDreamUISettings::GetMaxFontAtlasSlices();
}

void UDreamUIFontData_FreeTypeRender::RequestAtlasFlush(int32 InSliceCount)
{
	// Never past twice the budget: a font that keeps glyphs on the worker frame after frame never counts as settled, and
	// its atlas must not grow for good on that account.
	const int32 Budget = UDreamUISettings::GetMaxFontAtlasSlices();
	if (bAtlasRefilling && InSliceCount < 2 * Budget)
	{
		// Still refilling after the last flush, and already out of room: the glyphs on screen need more than the budget.
		AtlasSliceThreshold = InSliceCount + 1;
		if (!bLoggedAtlasThresholdRaise)
		{
			bLoggedAtlasThresholdRaise = true;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, the glyphs on screen need more than the %d-slice atlas budget (DreamUI settings: Max Font Atlas Slices), so the atlas grows past it, up to twice the budget, instead of flushing every frame. Too many distinct glyphs or font sizes are live at once -- a bitmap font caches a separate glyph per size, and Best Fit walks several sizes per search. (reported once per font)")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), Budget);
		}
		return;
	}
	if (!bAtlasFlushRequested)
	{
		bAtlasFlushRequested = true;
		AtlasFlushRequestFrame = GFrameCounter;
		UE_LOG(DreamGUI, Log, TEXT("[%s].%d Font:%s, the glyph atlas reached its %d-slice budget; it is flushed before the next frame's first glyph and refilled on demand.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), InSliceCount);
	}
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::MakePendingCharData(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold)
{
	FDreamUICharData Result;
	Result.bPending = true;
	Result.FaceIndex = Glyph.FaceIndex;
	Result.GlyphIndex = Glyph.GlyphIndex;
#if WITH_FREETYPE
	// The advance alone, unscaled, so the line lays out where it will end up once the quad lands.
	if (FT_FaceRec_* TargetFace = GetFreeTypeFace(Glyph.FaceIndex))
	{
		const uint8 ColorKind = IsColorFace(Glyph.FaceIndex) ? GetGlyphColorKind(Glyph.FaceIndex, Glyph.GlyphIndex) : (uint8)EDreamGlyphColorKind::None;
		if (ColorKind == (uint8)EDreamGlyphColorKind::Bitmap || ColorKind == (uint8)EDreamGlyphColorKind::Layers)
		{
			// A colour glyph's advance is its strike's (or hmtx for COLR layers), never emboldened; loading it without its
			// bitmap, as below, fails on a face that has nothing but bitmaps and left it at 0.
			Result.bColor = true;
			Result.XAdvance = GetUnrasterizedAdvance(Glyph.FaceIndex, Glyph.GlyphIndex, CharSize);
		}
		else if (FT_Load_Glyph(TargetFace, Glyph.GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0 && TargetFace->units_per_EM != 0)
		{
			Result.XAdvance = (float)(TargetFace->glyph->metrics.horiAdvance * ((double)CharSize / (double)TargetFace->units_per_EM));
			if (IsBold)
			{
				Result.XAdvance += CharSize * GetBoldRatio();
			}
		}
	}
#endif
	return Result;
}

bool UDreamUIFontData_FreeTypeRender::TakeSyncGlyphBudget()
{
	if (SyncGlyphBudgetFrame != GFrameCounter)
	{
		SyncGlyphBudgetFrame = GFrameCounter;
		SyncGlyphsThisFrame = 0;
	}
	const int32 Budget = AsyncGlyphSyncBudgetOverride >= 0 ? AsyncGlyphSyncBudgetOverride : UDreamUISettings::GetAsyncGlyphSyncBudgetPerFrame();
	if (SyncGlyphsThisFrame >= Budget)
	{
		return false;
	}
	SyncGlyphsThisFrame++;
	return true;
}

bool UDreamUIFontData_FreeTypeRender::TakeSyncCoverageBudget()
{
	using namespace DreamFreeTypeRenderLocal;
	if (SyncCoverageBudgetFrame != GFrameCounter)
	{
		SyncCoverageBudgetFrame = GFrameCounter;
		SyncCoverageGlyphsThisFrame = 0;
	}
	const int32 Budget = AsyncGlyphSyncBudgetOverride >= 0 ? AsyncGlyphSyncBudgetOverride : UDreamUISettings::GetCoverageGlyphSyncBudgetPerFrame();
	if (SyncCoverageGlyphsThisFrame >= Budget)
	{
		return false;
	}
	SyncCoverageGlyphsThisFrame++;
	return true;
}

void UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(int32 Budget)
{
	AsyncGlyphSyncBudgetOverride = Budget;
	SyncGlyphBudgetFrame = 0;
	DreamFreeTypeRenderLocal::SyncCoverageBudgetFrame = 0;
}

TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> UDreamUIFontData_FreeTypeRender::GetOrCreateSharedFaceBytes()
{
	if (SharedFaceBytes.IsValid())
	{
		return SharedFaceBytes;
	}
	const TArray<uint8>* Bytes = nullptr;
	if (TempFontBinaryArray.Num() > 0)
	{
		Bytes = &TempFontBinaryArray;
	}
	else if (FontBinaryArray.Num() > 0)
	{
		Bytes = &FontBinaryArray;
	}
#if WITH_EDITOR
	// In the editor an EngineFont keeps its bytes in the face asset and nowhere else -- both binary
	// arrays are empty -- so the worker used to open no face at all and fail every job it was handed,
	// silently, and the glyph never arrived.
	else if (FontType == EDreamUIDynamicFontDataType::EngineFont
		&& IsValid(EngineFont) && EngineFont->GetFontFaceData()->HasData())
	{
		Bytes = &EngineFont->GetFontFaceData()->GetData();
	}
#endif
	if (Bytes == nullptr)
	{
		return nullptr;
	}
	SharedFaceBytes = MakeShared<const TArray<uint8>, ESPMode::ThreadSafe>(*Bytes);
	return SharedFaceBytes;
}

FDreamGlyphRasterizer* UDreamUIFontData_FreeTypeRender::GetOrCreateRasterizer()
{
#if WITH_FREETYPE
	if (!Rasterizer.IsValid())
	{
		TSharedRef<FDreamGlyphRasterizer, ESPMode::ThreadSafe> NewRasterizer = MakeShared<FDreamGlyphRasterizer, ESPMode::ThreadSafe>();
		// The worker reads its own copies of the font files; fallbacks and style faces contribute theirs by face index
		// (the three style faces follow the fallbacks). A face that did not load on the game thread is left out, this
		// font's own included: its fallbacks can still supply glyphs, and a job for a face with no source goes to the
		// synchronous path instead.
		const int32 FaceCount = GetFaceCount() + 3;
		int32 RegisteredFaces = 0;
		for (int32 FaceIndex = 0; FaceIndex < FaceCount; FaceIndex++)
		{
			UDreamUIFontData_FreeTypeRender* Owner = GetFaceOwner(FaceIndex);
			if (Owner == nullptr)continue;
			Owner->InitFreeType();
			if (!Owner->bAlreadyInitialized || Owner->Face == nullptr)continue;
			// One buffer per font asset, shared: this used to deep-copy the whole font file per face per
			// rasterizer, and a CJK face is tens of megabytes.
			const TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> Bytes = Owner->GetOrCreateSharedFaceBytes();
			if (!Bytes.IsValid())continue;
			// The face the game thread opened, which is FontFace clamped to the file.
			NewRasterizer->SetFaceSource(FaceIndex, Bytes.ToSharedRef(), (int32)(Owner->Face->face_index & 0xFFFF));
			RegisteredFaces++;
		}
		if (RegisteredFaces == 0)
		{
			// Nothing for a worker to read: say so rather than hand back a rasterizer that fails every
			// job, and the caller rasterizes on the game thread instead.
			return nullptr;
		}
		Rasterizer = NewRasterizer;
	}
	return Rasterizer.Get();
#else
	return nullptr;
#endif
}

void UDreamUIFontData_FreeTypeRender::DrainAsyncGlyphs()
{
	check(IsInGameThread());
	if (!Rasterizer.IsValid())
	{
		PendingAsyncGlyphs.Reset();
		PendingColorGlyphs.Reset();
		PendingCoverageGlyphs.Reset();
		return;
	}
	TArray<FDreamGlyphRasterizer::FResult> Results;
	Rasterizer->Drain(Results);
	if (Results.Num() == 0)
	{
		return;
	}
	bool bAnyLanded = false;
	bool bAnyFailed = false;
	bool bAnyCoverage = false;
	for (FDreamGlyphRasterizer::FResult& Result : Results)
	{
		const FDreamGlyphRasterizer::FJob& Job = Result.Job;
		if (Job.Kind == EDreamGlyphJobKind::Coverage)
		{
			// Only a repaint is owed for these, whatever became of them: a text that drew a field quad in their place stops
			// waiting either way.
			bAnyCoverage = true;
			const FCoverageGlyphKey CoverageKey(Job.Key.FaceIndex, Job.Key.GlyphIndex, Job.Coverage.Size26Dot6, (uint8)Job.CoverageFlags, (uint8)Job.Coverage.Hinting);
			PendingCoverageGlyphs.Remove(CoverageKey);
			if (CoverageGlyphs.Contains(CoverageKey))
			{
				continue;
			}
			FCoverageGlyphEntry Entry;
			Entry.bFailed = !Result.bSucceeded
				|| !InsertCoverageGlyph(Result.Coverage.Width, Result.Coverage.Height, Result.Coverage.Left, Result.Coverage.Top, Result.Coverage.Pixels, Entry.Glyph);
			CoverageGlyphs.Add(CoverageKey, Entry);
			continue;
		}
		if (Job.Kind == EDreamGlyphJobKind::Color)
		{
			const FColorGlyphKey ColorKey(Job.Key.FaceIndex, Job.Key.GlyphIndex, Job.Color.TargetPixelSize);
			PendingColorGlyphs.Remove(ColorKey);
			if (ColorGlyphs.Contains(ColorKey))
			{
				continue;//a synchronous request beat the worker to it
			}
			// A colour glyph that cannot be made is not made again: its bytes are the same next time. It keeps its advance.
			FColorGlyphEntry Entry;
			if (Result.bSucceeded && InsertColorGlyph(Result.Color, Entry))
			{
				bAnyLanded = true;
			}
			else
			{
				Entry = MakeFailedColorGlyph(Job.Key.FaceIndex, Job.Key.GlyphIndex, Job.Color.TargetPixelSize);
				bAnyFailed = true;
			}
			ColorGlyphs.Add(ColorKey, Entry);
			continue;
		}
		FAsyncGlyphRequest Request;
		Request.Glyph = Job.Key;
		Request.CharSize = IsGlyphCacheSizeIndependent() ? 0.0f : Job.CharSize;
		Request.bBold = Job.bBold;
		PendingAsyncGlyphs.Remove(Request);
		if (!Result.bSucceeded)
		{
			if (!bLoggedAsyncGlyphFailure)
			{
				bLoggedAsyncGlyphFailure = true;
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, face %d glyph %u failed to rasterize on a worker; falling back to synchronous glyphs. (reported once per font)")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), Job.Key.FaceIndex, Job.Key.GlyphIndex);
			}
			bAnyFailed = true;
			continue;
		}
		FDreamUICharData Existing;
		if (GetCharDataFromCache(Job.Key, Job.CharSize, Job.bBold, Existing))
		{
			continue;//a synchronous request beat the worker to it
		}
		FGlyphBitmap Bitmap;
		Bitmap.width = Result.Sdf.Width;
		Bitmap.height = Result.Sdf.Height;
		Bitmap.hOffset = Result.Sdf.Left;
		Bitmap.vOffset = Result.Sdf.Top;
		Bitmap.hAdvance = Result.Sdf.Advance;
		Bitmap.buffer = MoveTemp(Result.Sdf.Pixels);
		Bitmap.pixelSize = 4;
		FDreamUICharData CharData;
		if (InsertGlyphBitmap(Bitmap, CharData))
		{
			AddCharDataToCache(Job.Key, Job.CharSize, Job.bBold, CharData);
			bAnyLanded = true;
		}
	}
	// A failure counts too: the quad handed out for a pending glyph is empty, so without a relayout
	// that character would simply never appear again.
	if (bAnyLanded || bAnyFailed)
	{
		OnGlyphsReady.Broadcast();
	}
	// Coverage glyphs change no advance, so they are a repaint, announced at the end of FlushPendingFontTextures: here a
	// text may be in the middle of anything.
	if (bAnyCoverage)
	{
		bCoverageGlyphsChanged = true;
		DreamFreeTypeRenderLocal::FontsWithCoverageWork.Add(this);
	}
}

void UDreamUIFontData_FreeTypeRender::WaitForAsyncGlyphs()
{
	check(IsInGameThread());
	if (Rasterizer.IsValid())
	{
		Rasterizer->WaitForAll();
		DrainAsyncGlyphs();
	}
	FontsWithAsyncGlyphs.Remove(this);
}

bool UDreamUIFontData_FreeTypeRender::GetCoverageGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph)
{
	check(IsInGameThread());
	OutGlyph = FDreamUICoverageGlyph();
#if WITH_FREETYPE
	if (Size26Dot6 <= 0 || !SupportsCoverageGlyphs())
	{
		return false;
	}
	uint8 Hinting = 0;
	float BoldEm = 0.0f, ItalicSlope = 0.0f;
	GetCoverageRasterStyle(Hinting, BoldEm, ItalicSlope);
	const FCoverageGlyphKey Key(FaceIndex, GlyphIndex, Size26Dot6, (uint8)Flags, Hinting);
	if (const FCoverageGlyphEntry* Entry = CoverageGlyphs.Find(Key))
	{
		if (Entry->bFailed)
		{
			return false;
		}
		OutGlyph = Entry->Glyph;
		return true;
	}
	if (PendingCoverageGlyphs.Contains(Key))
	{
		OutGlyph.bPending = true;
		return true;
	}
	// Never from coverage: a face that draws in colour, or one with no outline to hint. Remembered, so the painter learns it
	// once per glyph and size rather than every paint.
	FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
	if (TargetFace == nullptr || FT_HAS_COLOR(TargetFace) || !FT_IS_SCALABLE(TargetFace))
	{
		FCoverageGlyphEntry& Failed = CoverageGlyphs.Add(Key);
		Failed.bFailed = true;
		return false;
	}
	FDreamGlyphCoverageParams Params;
	Params.Size26Dot6 = Size26Dot6;
	Params.Hinting = (EDreamGlyphHinting)Hinting;
	Params.BoldPixels = EnumHasAnyFlags(Flags, EDreamUICoverageGlyphFlags::SyntheticBold) ? BoldEm * (Size26Dot6 / 64.0f) : 0.0f;
	Params.ItalicSlope = EnumHasAnyFlags(Flags, EDreamUICoverageGlyphFlags::SyntheticItalic) ? ItalicSlope : 0.0f;
	// Off-thread once the frame's coverage budget -- its own, not the field glyphs' -- is spent. The text draws the field
	// quad meanwhile and is told to repaint when this lands (OnCoverageGlyphsChanged).
	if (UDreamUISettings::GetAsyncGlyphRasterization() && !TakeSyncCoverageBudget())
	{
		FDreamGlyphRasterizer* Worker = GetOrCreateRasterizer();
		if (Worker != nullptr && Worker->HasFaceSource(FaceIndex))
		{
			FDreamGlyphRasterizer::FJob Job;
			Job.Kind = EDreamGlyphJobKind::Coverage;
			Job.Key = FDreamUIGlyphKey(FaceIndex, GlyphIndex);
			Job.CharSize = Size26Dot6 / 64.0f;
			Job.Coverage = Params;
			Job.CoverageFlags = Flags;
			Worker->Enqueue(Job);
			PendingCoverageGlyphs.Add(Key);
			FontsWithAsyncGlyphs.Add(this);
			OutGlyph.bPending = true;
			return true;
		}
	}
	// Made first and cached after: packing it can make the atlas, which starts the caches again.
	FDreamGlyphCoverageResult Raster;
	FCoverageGlyphEntry Entry;
	Entry.bFailed = !FDreamGlyphCoverage::Rasterize(TargetFace, GlyphIndex, Params, Raster)
		|| !InsertCoverageGlyph(Raster.Width, Raster.Height, Raster.Left, Raster.Top, Raster.Pixels, Entry.Glyph);
	CoverageGlyphs.Add(Key, Entry);
	if (Entry.bFailed)
	{
		return false;
	}
	OutGlyph = Entry.Glyph;
	return true;
#else
	return false;
#endif
}

bool UDreamUIFontData_FreeTypeRender::InsertCoverageGlyph(int32 InWidth, int32 InHeight, int32 InLeft, int32 InTop, const TArray<uint8>& InPixels, FDreamUICoverageGlyph& OutGlyph)
{
	if (Texture == nullptr)
	{
		FlushGlyphAtlas();
	}
	OutGlyph = FDreamUICoverageGlyph();
	OutGlyph.BitmapLeft = InLeft;
	OutGlyph.BitmapTop = InTop;
	OutGlyph.Width = FMath::Max(InWidth, 0);
	OutGlyph.Height = FMath::Max(InHeight, 0);
	if (InWidth <= 0 || InHeight <= 0)
	{
		// Nothing to draw (a space): no cell is needed, and the box is empty.
		return true;
	}
	if (InPixels.Num() < InWidth * InHeight * 4)
	{
		return false;
	}
	// One texel of zero all round: the painter samples texel centres 1:1, and nothing it reads is a neighbour's.
	const int32 PaddedWidth = InWidth + 2;
	const int32 PaddedHeight = InHeight + 2;
	int32 Slice = 0, PackedX = 0, PackedY = 0;
	if (!PackAtlasRect(true, PaddedWidth, PaddedHeight, Slice, PackedX, PackedY))
	{
		return false;
	}
	TArray<uint8> Padded;
	Padded.SetNumZeroed(PaddedWidth * PaddedHeight * 4);
	for (int32 Row = 0; Row < InHeight; Row++)
	{
		FMemory::Memcpy(Padded.GetData() + ((int64)(Row + 1) * PaddedWidth + 1) * 4, InPixels.GetData() + (int64)Row * InWidth * 4, InWidth * 4);
	}
	if (!UpdateFontTextureRegion(PackedX, PackedY, Slice, PaddedWidth, PaddedHeight, PaddedWidth * 4, 4, Padded))
	{
		return false;
	}
	// The box's texels exactly: corners on texel edges, V down.
	OutGlyph.MinUV = FVector2f((PackedX + 1) * OneDivideTextureSize, (PackedY + 1) * OneDivideTextureSize);
	OutGlyph.MaxUV = FVector2f((PackedX + 1 + InWidth) * OneDivideTextureSize, (PackedY + 1 + InHeight) * OneDivideTextureSize);
	OutGlyph.SliceIndex = Slice;
	return true;
}

void UDreamUIFontData_FreeTypeRender::RequestCoverageFlush()
{
	if (!bCoverageFlushRequested)
	{
		bCoverageFlushRequested = true;
		DreamFreeTypeRenderLocal::FontsWithCoverageWork.Add(this);
		if (!bLoggedCoverageFlush)
		{
			bLoggedCoverageFlush = true;
			UE_LOG(DreamGUI, Log, TEXT("[%s].%d Font:%s, the small-text coverage glyphs outgrew their %d atlas cells (DreamUI settings: Max Coverage Cells); they are flushed at the end of the frame and made again as the texts repaint. (reported once per font)")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), UDreamUISettings::GetMaxCoverageCells());
		}
	}
}

void UDreamUIFontData_FreeTypeRender::FlushCoverageGlyphs()
{
	bCoverageFlushRequested = false;
	CoverageGlyphs.Reset();
	// The cells are not handed back yet: this frame's draws still read them, and an upload or a field glyph packed into one
	// before the frame is drawn would show through. They go back to the pool when the next frame packs its first glyph.
	RetiredCoverageCells.Append(CoverageCells);
	CoverageCells.Reset();
	CoverageRetireFrame = GFrameCounter;
	CoveragePacker = FAtlasPacker();
	// The texts that drew from them repaint, and ask for their glyphs again: a refill, which the budget alone bounds again.
	CoverageCellThreshold = 0;
	bCoverageRefilling = true;
	CoverageFlushFrame = GFrameCounter;
	DreamFreeTypeRenderLocal::FontsRefillingCoverage.Add(this);
	bCoverageGlyphsChanged = true;
}

void UDreamUIFontData_FreeTypeRender::ReleaseRetiredCoverageCells()
{
	if (RetiredCoverageCells.Num() == 0 || CoverageRetireFrame == GFrameCounter)
	{
		return;
	}
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 CellSize = FMath::Min(UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType), TextureSize);
	const int32 SliceCount = Texture != nullptr ? Texture->GetArraySize() : 0;
	const int64 RowPitch = (int64)TextureSize * FontTextureBytesPerPixel;
	const int64 SliceBytes = RowPitch * TextureSize;
	for (const FAtlasCell& Cell : RetiredCoverageCells)
	{
		if (Cell.Slice >= SliceCount)
		{
			continue;
		}
		// Back to what an untouched cell holds -- zero on the outline field's atlas -- and uploaded so with the next glyphs.
		if (FontTextureBytesPerPixel > 0 && (int64)(Cell.Slice + 1) * SliceBytes <= FontTextureAtlasData.Num())
		{
			uint8* CellStart = FontTextureAtlasData.GetData() + Cell.Slice * SliceBytes + (int64)Cell.Y * RowPitch + (int64)Cell.X * FontTextureBytesPerPixel;
			for (int32 Row = 0; Row < CellSize; Row++)
			{
				InitializeFontTextureAtlasSlice(CellStart + Row * RowPitch, (int64)CellSize * FontTextureBytesPerPixel);
			}
			MarkAtlasRegionDirty(Cell.Slice, FIntRect(Cell.X, Cell.Y, Cell.X + CellSize, Cell.Y + CellSize));
			PendingFontTextureUploads.Add(this);
		}
		FreeAtlasCells.Add(Cell);
	}
	RetiredCoverageCells.Reset();
}

bool UDreamUIFontData_FreeTypeRender::InjectCoverageGlyphForTesting(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags,
	int32 Width, int32 Height, int32 Left, int32 Top, const TArray<uint8>& Pixels)
{
	check(IsInGameThread());
	if (!SupportsCoverageGlyphs() || Size26Dot6 <= 0 || Width < 0 || Height < 0 || Pixels.Num() < Width * Height * 4)
	{
		return false;
	}
	uint8 Hinting = 0;
	float BoldEm = 0.0f, ItalicSlope = 0.0f;
	GetCoverageRasterStyle(Hinting, BoldEm, ItalicSlope);
	const FCoverageGlyphKey Key(FaceIndex, GlyphIndex, Size26Dot6, (uint8)Flags, Hinting);
	FCoverageGlyphEntry Entry;
	if (!InsertCoverageGlyph(Width, Height, Left, Top, Pixels, Entry.Glyph))
	{
		return false;
	}
	// Whatever was there, made or on its way, gives way to the injected glyph.
	CoverageGlyphs.Add(Key, Entry);
	PendingCoverageGlyphs.Remove(Key);
	return true;
}

bool UDreamUIFontData_FreeTypeRender::EnsureFontTextureAtlasData(int32 SliceCount, int32 BytesPerPixel)
{
	if (SliceCount <= 0 || BytesPerPixel <= 0)
	{
		return false;
	}
	if (FontTextureBytesPerPixel == 0)
	{
		FontTextureBytesPerPixel = BytesPerPixel;
	}
	if (!ensureMsgf(FontTextureBytesPerPixel == BytesPerPixel,
		TEXT("Font atlas pixel size changed from %d to %d for %s."),
		FontTextureBytesPerPixel, BytesPerPixel, *GetPathName()))
	{
		return false;
	}

	const int64 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int64 SliceDataSize = TextureSize * TextureSize * FontTextureBytesPerPixel;
	const int64 RequiredDataSize = SliceDataSize * SliceCount;
	if (!ensureMsgf(RequiredDataSize <= MAX_int32,
		TEXT("Font atlas CPU data is too large for %s."), *GetPathName()))
	{
		return false;
	}

	const int32 OldDataSize = FontTextureAtlasData.Num();
	if (OldDataSize < RequiredDataSize)
	{
		FontTextureAtlasData.SetNumUninitialized(static_cast<int32>(RequiredDataSize));
		for (int64 SliceOffset = OldDataSize; SliceOffset < RequiredDataSize; SliceOffset += SliceDataSize)
		{
			InitializeFontTextureAtlasSlice(FontTextureAtlasData.GetData() + SliceOffset, SliceDataSize);
		}
	}
	return true;
}

void UDreamUIFontData_FreeTypeRender::InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const
{
	FMemory::Memzero(SliceData, SliceDataSize);
}

bool UDreamUIFontData_FreeTypeRender::UpdateFontTextureRegion(uint32 PosX, uint32 PosY, uint32 Slice, uint32 Width, uint32 Height, uint32 SrcPitch, uint32 SrcBpp, const TArray<uint8>& SrcData)
{
	check(IsInGameThread());
	const uint32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const uint64 RequiredSrcDataSize = Height > 0 ? static_cast<uint64>(Height - 1) * SrcPitch + static_cast<uint64>(Width) * SrcBpp : 0;
	if (!ensureMsgf(
		Width > 0 && Height > 0
		&& PosX + Width <= TextureSize
		&& PosY + Height <= TextureSize
		&& SrcPitch >= Width * SrcBpp
		&& RequiredSrcDataSize <= static_cast<uint64>(SrcData.Num()),
		TEXT("Invalid font atlas update for %s."), *GetPathName()))
	{
		return false;
	}
	if (!EnsureFontTextureAtlasData(Slice + 1, SrcBpp))
	{
		return false;
	}

	const int64 DestPitch = static_cast<int64>(TextureSize) * FontTextureBytesPerPixel;
	uint8* DestData = FontTextureAtlasData.GetData()
		+ static_cast<int64>(Slice) * TextureSize * DestPitch
		+ static_cast<int64>(PosY) * DestPitch
		+ static_cast<int64>(PosX) * FontTextureBytesPerPixel;
	const uint8* SourceData = SrcData.GetData();
	const SIZE_T RowDataSize = static_cast<SIZE_T>(Width) * FontTextureBytesPerPixel;
	for (uint32 Row = 0; Row < Height; ++Row)
	{
		FMemory::Memcpy(DestData + static_cast<int64>(Row) * DestPitch, SourceData + static_cast<int64>(Row) * SrcPitch, RowDataSize);
	}

	// The one place the CPU atlas is written, so the one place a dirty region is recorded. Note the
	// rect is the GLYPH's, not the packed rect: the padding around it was never touched.
	MarkAtlasRegionDirty(static_cast<int32>(Slice),
		FIntRect(static_cast<int32>(PosX), static_cast<int32>(PosY),
			static_cast<int32>(PosX + Width), static_cast<int32>(PosY + Height)));
	PendingFontTextureUploads.Add(this);
	return true;
}

namespace
{
	/** Staging textures are bucketed by size, rounded up to this, so similar regions share one. */
	constexpr int32 AtlasStagingSizeAlignment = 64;
	/** What the staging pool may hold between frames. One 256x256 BGRA region is 256KB. */
	constexpr int64 AtlasStagingPoolByteBudget = 4 * 1024 * 1024;
}

/**
 * Render-thread-only pool of scratch 2D textures used to land partial atlas updates.
 *
 * A Texture2DArray cannot be updated in place a region at a time. RHIUpdateTexture2D writes slice 0
 * whatever slice is asked for, and LockTexture2DArray(RLM_WriteOnly) hands back a fresh upload buffer
 * whose Unlock copies the WHOLE subresource -- so filling in part of it destroys the rest. The way
 * through is to write the region into a plain 2D texture and CopyTexture that into the slice at an
 * offset, which is what these are for.
 *
 * Every member is touched on the render thread only. The font holds the pool by TSharedPtr and gives
 * that reference to a render command when it goes away, so the FTextureRHIRefs are released there too.
 */
struct FDreamUIFontAtlasStagingPool
{
	/** Free textures by bucket extent. Everything in here has PixelFormat's format. */
	TMap<FIntPoint, TArray<FTextureRHIRef>> FreeTextures;
	/** The atlas format the pooled textures were made for; a change empties the pool. */
	EPixelFormat PixelFormat = PF_Unknown;
	/** Bytes currently parked in FreeTextures, against AtlasStagingPoolByteBudget. */
	int64 PooledBytes = 0;

	/** The bucket a region of this size lands in: rounded up, and never larger than a slice. */
	static FIntPoint BucketSizeFor(const FIntPoint& RegionSize, int32 MaxSize)
	{
		return FIntPoint(
			FMath::Min(Align(FMath::Max(RegionSize.X, 1), AtlasStagingSizeAlignment), MaxSize),
			FMath::Min(Align(FMath::Max(RegionSize.Y, 1), AtlasStagingSizeAlignment), MaxSize));
	}

	static int64 BytesFor(const FIntPoint& Size, EPixelFormat Format)
	{
		return static_cast<int64>(Size.X) * Size.Y * GPixelFormats[Format].BlockBytes;
	}

	/** A texture of exactly BucketSize, taken out of the pool or made. Null only if creation failed. */
	FTextureRHIRef Acquire(FRHICommandListImmediate& RHICmdList, const FIntPoint& BucketSize, EPixelFormat Format)
	{
		if (PixelFormat != Format)
		{
			// The font swapped atlas format (single channel <-> MTSDF). Nothing pooled can be copied
			// into the new atlas, so drop it all rather than keep textures that will never match.
			FreeTextures.Empty();
			PooledBytes = 0;
			PixelFormat = Format;
		}
		if (TArray<FTextureRHIRef>* Bucket = FreeTextures.Find(BucketSize))
		{
			if (Bucket->Num() > 0)
			{
				FTextureRHIRef Result = Bucket->Pop(EAllowShrinking::No);
				PooledBytes -= BytesFor(BucketSize, Format);
				return Result;
			}
		}
		return RHICmdList.CreateTexture(
			FRHITextureCreateDesc::Create2D(TEXT("DreamUIFontAtlasStaging"), BucketSize, Format)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::CopySrc));
	}

	/** Take a texture back, unless the pool is already holding its budget -- then just let it go. */
	void Release(FTextureRHIRef&& InTexture)
	{
		if (!InTexture.IsValid())
		{
			return;
		}
		const FIntPoint Size = InTexture->GetDesc().Extent;
		const int64 Bytes = BytesFor(Size, PixelFormat);
		if (PooledBytes + Bytes > AtlasStagingPoolByteBudget)
		{
			return;
		}
		PooledBytes += Bytes;
		FreeTextures.FindOrAdd(Size).Add(MoveTemp(InTexture));
	}
};

void UDreamUIFontData_FreeTypeRender::MarkAtlasRegionDirty(int32 Slice, const FIntRect& Region)
{
	check(IsInGameThread());
	if (Region.Width() <= 0 || Region.Height() <= 0)
	{
		return;
	}
	if (FIntRect* Existing = DirtyFontTextureSlices.Find(Slice))
	{
		// One rect per slice, not a list: a handful of glyphs landing in the same packing cell merge
		// into a small region, and scattered ones grow it until the upload decides a whole-slice lock
		// is cheaper. Tracking them separately would buy little and cost a rect list per slice.
		Existing->Union(Region);
	}
	else
	{
		DirtyFontTextureSlices.Add(Slice, Region);
	}
}

void UDreamUIFontData_FreeTypeRender::ReleaseAtlasStagingPool()
{
	if (!AtlasStagingPool.IsValid())
	{
		return;
	}
	// Queued behind any flush still in flight, and the textures are released wherever this reference
	// dies -- which is the render thread, because that is where the command runs.
	ENQUEUE_RENDER_COMMAND(FDreamUIFontData_ReleaseAtlasStagingPool)(
		[ReleasedPool = MoveTemp(AtlasStagingPool)](FRHICommandListImmediate& RHICmdList) mutable
		{
			ReleasedPool.Reset();
		});
}

bool UDreamUIFontData_FreeTypeRender::FlushFontTexture()
{
	check(IsInGameThread());
	if (DirtyFontTextureSlices.IsEmpty())
	{
		return true;
	}
	if (!IsValid(Texture) || Texture->GetResource() == nullptr)
	{
		return false;
	}

	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 BytesPerPixel = FontTextureBytesPerPixel;
	if (!ensure(BytesPerPixel > 0))
	{
		DirtyFontTextureSlices.Reset();
		return true;
	}
	const int64 SliceRowSize = static_cast<int64>(TextureSize) * BytesPerPixel;
	const int64 SliceDataSize = SliceRowSize * TextureSize;
	// Past half a slice the region path stops paying: the staging texture is then nearly slice-sized,
	// so it is an upload of about the same bytes plus a copy on top. Lock the whole slice instead.
	const int32 FullSliceAreaThreshold = (TextureSize * TextureSize) / 2;
	const int32 SliceCount = Texture->GetArraySize();

	struct FSliceUpload
	{
		int32 Slice = 0;
		/** Region within the slice; the whole slice when bFullSlice. */
		FIntRect Region;
		/** Where this upload's rows start in UploadData. Rows are packed at Region.Width() * bpp. */
		int64 DataOffset = 0;
		bool bFullSlice = false;
	};

	TArray<int32> DirtySlices;
	DirtyFontTextureSlices.GenerateKeyArray(DirtySlices);
	DirtySlices.Sort();

	TArray<FSliceUpload> Uploads;
	Uploads.Reserve(DirtySlices.Num());
	int64 TotalUploadBytes = 0;
	for (int32 Slice : DirtySlices)
	{
		// A slice can have gone away under a pending region: the font is renewed (which rebuilds the
		// texture from the CPU atlas anyway) or released between the write and this flush.
		if (Slice < 0 || Slice >= SliceCount
			|| static_cast<int64>(Slice + 1) * SliceDataSize > FontTextureAtlasData.Num())
		{
			continue;
		}
		FSliceUpload Upload;
		Upload.Slice = Slice;
		Upload.Region = DirtyFontTextureSlices[Slice];
		Upload.Region.Clip(FIntRect(0, 0, TextureSize, TextureSize));
		if (Upload.Region.Area() <= 0)
		{
			continue;
		}
		Upload.bFullSlice = Upload.Region.Area() >= FullSliceAreaThreshold;
		if (Upload.bFullSlice)
		{
			Upload.Region = FIntRect(0, 0, TextureSize, TextureSize);
		}
		Upload.DataOffset = TotalUploadBytes;
		TotalUploadBytes += static_cast<int64>(Upload.Region.Width()) * Upload.Region.Height() * BytesPerPixel;
		Uploads.Add(Upload);
	}
	DirtyFontTextureSlices.Reset();
	if (Uploads.Num() == 0)
	{
		return true;
	}
	// Every region is a subset of a distinct slice, so this cannot exceed the atlas, which
	// EnsureFontTextureAtlasData already holds under MAX_int32.
	if (!ensure(TotalUploadBytes <= MAX_int32))
	{
		return true;
	}

	TArray<uint8> UploadData;
	UploadData.SetNumUninitialized(static_cast<int32>(TotalUploadBytes));
	for (const FSliceUpload& Upload : Uploads)
	{
		const int64 UploadRowSize = static_cast<int64>(Upload.Region.Width()) * BytesPerPixel;
		const uint8* SliceStart = FontTextureAtlasData.GetData() + static_cast<int64>(Upload.Slice) * SliceDataSize;
		uint8* DestData = UploadData.GetData() + Upload.DataOffset;
		for (int32 Row = 0; Row < Upload.Region.Height(); ++Row)
		{
			FMemory::Memcpy(
				DestData + static_cast<int64>(Row) * UploadRowSize,
				SliceStart + static_cast<int64>(Upload.Region.Min.Y + Row) * SliceRowSize
					+ static_cast<int64>(Upload.Region.Min.X) * BytesPerPixel,
				static_cast<SIZE_T>(UploadRowSize));
		}
	}

	if (!AtlasStagingPool.IsValid())
	{
		AtlasStagingPool = MakeShared<FDreamUIFontAtlasStagingPool, ESPMode::ThreadSafe>();
	}

	FTextureResource* TextureResource = Texture->GetResource();
	ENQUEUE_RENDER_COMMAND(FDreamUIFontData_FlushFontTexture)(
		[TextureResource, TextureSize, BytesPerPixel, Pool = AtlasStagingPool,
			Uploads = MoveTemp(Uploads), UploadData = MoveTemp(UploadData)](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TextureRHI = TextureResource->GetTexture2DArrayRHI();
			if (!ensure(TextureRHI != nullptr && TextureRHI->IsValid()))
			{
				return;
			}
			const EPixelFormat AtlasFormat = TextureRHI->GetFormat();
			if (!ensure(GPixelFormats[AtlasFormat].BlockBytes == BytesPerPixel))
			{
				return;
			}

			// One staging texture per region, held until every copy has been issued: each is written
			// by its own lock/unlock and only read at the end, so two regions cannot share one.
			struct FPendingCopy
			{
				FTextureRHIRef Staging;
				FIntRect Region;
				int32 Slice = 0;
			};
			TArray<FPendingCopy> PendingCopies;
			PendingCopies.Reserve(Uploads.Num());

			for (const FSliceUpload& Upload : Uploads)
			{
				const uint8* SourceData = UploadData.GetData() + Upload.DataOffset;
				const int64 SourceRowSize = static_cast<int64>(Upload.Region.Width()) * BytesPerPixel;

				if (Upload.bFullSlice)
				{
					uint32 DestStride = 0;
					uint8* DestData = static_cast<uint8*>(RHICmdList.LockTexture2DArray(
						TextureRHI, Upload.Slice, 0, RLM_WriteOnly, DestStride, false));
					if (ensure(DestData != nullptr && DestStride >= static_cast<uint32>(SourceRowSize)))
					{
						for (int32 Row = 0; Row < Upload.Region.Height(); ++Row)
						{
							FMemory::Memcpy(DestData + static_cast<int64>(Row) * DestStride,
								SourceData + static_cast<int64>(Row) * SourceRowSize,
								static_cast<SIZE_T>(SourceRowSize));
						}
					}
					RHICmdList.UnlockTexture2DArray(TextureRHI, Upload.Slice, 0, false);
					continue;
				}

				const FIntPoint BucketSize = FDreamUIFontAtlasStagingPool::BucketSizeFor(Upload.Region.Size(), TextureSize);
				FTextureRHIRef Staging = Pool->Acquire(RHICmdList, BucketSize, AtlasFormat);
				if (!Staging.IsValid())
				{
					continue;
				}
				uint32 StagingStride = 0;
				uint8* StagingData = static_cast<uint8*>(RHICmdList.LockTexture2D(
					Staging.GetReference(), 0, RLM_WriteOnly, StagingStride, false));
				const bool bStagingWritten = StagingData != nullptr && StagingStride >= static_cast<uint32>(SourceRowSize);
				if (bStagingWritten)
				{
					for (int32 Row = 0; Row < Upload.Region.Height(); ++Row)
					{
						FMemory::Memcpy(StagingData + static_cast<int64>(Row) * StagingStride,
							SourceData + static_cast<int64>(Row) * SourceRowSize,
							static_cast<SIZE_T>(SourceRowSize));
					}
				}
				RHICmdList.UnlockTexture2D(Staging.GetReference(), 0, false);
				if (bStagingWritten)
				{
					FPendingCopy& Copy = PendingCopies.AddDefaulted_GetRef();
					Copy.Staging = MoveTemp(Staging);
					Copy.Region = Upload.Region;
					Copy.Slice = Upload.Slice;
				}
				else
				{
					Pool->Release(MoveTemp(Staging));
				}
			}

			if (PendingCopies.Num() > 0)
			{
				// Batched rather than per region: the same states as the engine's own
				// TransitionAndCopyTexture, and Unknown as the before-state so a pooled texture's
				// last declared state does not have to be tracked across frames.
				TArray<FRHITransitionInfo, TInlineAllocator<8>> ToCopy;
				ToCopy.Reserve(PendingCopies.Num() + 1);
				ToCopy.Add(FRHITransitionInfo(TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest));
				for (const FPendingCopy& Copy : PendingCopies)
				{
					ToCopy.Add(FRHITransitionInfo(Copy.Staging.GetReference(), ERHIAccess::Unknown, ERHIAccess::CopySrc));
				}
				RHICmdList.Transition(MakeArrayView(ToCopy.GetData(), ToCopy.Num()));

				for (const FPendingCopy& Copy : PendingCopies)
				{
					FRHICopyTextureInfo CopyInfo;
					CopyInfo.Size = FIntVector(Copy.Region.Width(), Copy.Region.Height(), 1);
					CopyInfo.SourcePosition = FIntVector(0, 0, 0);
					CopyInfo.DestPosition = FIntVector(Copy.Region.Min.X, Copy.Region.Min.Y, 0);
					CopyInfo.DestSliceIndex = static_cast<uint32>(Copy.Slice);
					CopyInfo.NumSlices = 1;
					RHICmdList.CopyTexture(Copy.Staging.GetReference(), TextureRHI, CopyInfo);
				}

				TArray<FRHITransitionInfo, TInlineAllocator<8>> ToRead;
				ToRead.Reserve(PendingCopies.Num() + 1);
				ToRead.Add(FRHITransitionInfo(TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
				for (const FPendingCopy& Copy : PendingCopies)
				{
					ToRead.Add(FRHITransitionInfo(Copy.Staging.GetReference(), ERHIAccess::CopySrc, ERHIAccess::SRVMask));
				}
				RHICmdList.Transition(MakeArrayView(ToRead.GetData(), ToRead.Num()));

				for (FPendingCopy& Copy : PendingCopies)
				{
					Pool->Release(MoveTemp(Copy.Staging));
				}
			}
		});
	return true;
}

void UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures()
{
	check(IsInGameThread());
	// Worker glyphs first, so their atlas writes ride this frame's upload.
	if (FontsWithAsyncGlyphs.Num() > 0)
	{
		TArray<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsToDrain = FontsWithAsyncGlyphs.Array();
		for (const auto& Font : FontsToDrain)
		{
			if (!Font.IsValid())
			{
				FontsWithAsyncGlyphs.Remove(Font);
				continue;
			}
			Font->DrainAsyncGlyphs();
			if (!Font->HasPendingAsyncGlyphs())
			{
				FontsWithAsyncGlyphs.Remove(Font);
			}
		}
	}
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsToFlush;
	Swap(FontsToFlush, PendingFontTextureUploads);
	for (const auto& Font : FontsToFlush)
	{
		if (Font.IsValid() && !Font->FlushFontTexture())
		{
			PendingFontTextureUploads.Add(Font);
		}
	}
	// A flushed atlas has settled once a whole frame has gone by since the flush -- the texts told about it have laid out
	// again -- with nothing of the font's left on the worker.
	for (auto It = FontsRefillingAtlas.CreateIterator(); It; ++It)
	{
		UDreamUIFontData_FreeTypeRender* Font = It->Get();
		if (Font == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Font->PendingAsyncGlyphs.Num() == 0 && Font->PendingColorGlyphs.Num() == 0 && GFrameCounter > Font->AtlasFlushFrame + 1)
		{
			Font->bAtlasRefilling = false;
			It.RemoveCurrent();
		}
	}
	// The same for flushed coverage glyphs: settled a whole frame after the flush with none of the font's on the worker.
	for (auto It = DreamFreeTypeRenderLocal::FontsRefillingCoverage.CreateIterator(); It; ++It)
	{
		UDreamUIFontData_FreeTypeRender* Font = It->Get();
		if (Font == nullptr)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Font->PendingCoverageGlyphs.Num() == 0 && GFrameCounter > Font->CoverageFlushFrame + 1)
		{
			Font->bCoverageRefilling = false;
			It.RemoveCurrent();
		}
	}
	// Coverage last, after this frame's uploads were queued: a coverage flush must leave this frame drawing from the cells
	// it painted with, and the texts it tells -- coverage glyphs landed, failed, or flushed -- have painted for this frame
	// already, so they repaint in the next. Only from here: a text may be in the middle of a paint anywhere else.
	if (DreamFreeTypeRenderLocal::FontsWithCoverageWork.Num() > 0)
	{
		TArray<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> CoverageFonts = DreamFreeTypeRenderLocal::FontsWithCoverageWork.Array();
		DreamFreeTypeRenderLocal::FontsWithCoverageWork.Reset();
		for (const TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>& Font : CoverageFonts)
		{
			if (!Font.IsValid())
			{
				continue;
			}
			if (Font->bCoverageFlushRequested)
			{
				Font->FlushCoverageGlyphs();
			}
			if (Font->bCoverageGlyphsChanged)
			{
				Font->bCoverageGlyphsChanged = false;
				Font->OnCoverageGlyphsChanged.Broadcast();
			}
		}
	}
}

bool UDreamUIFontData_FreeTypeRender::CopyFontTextureAtlasData(void* DestData, int64 DataSize) const
{
	if (FontTextureAtlasData.Num() != DataSize)
	{
		return false;
	}
	FMemory::Memcpy(DestData, FontTextureAtlasData.GetData(), DataSize);
	return true;
}

void UDreamUIFontData_FreeTypeRender::ReleaseFontTexture()
{
	check(IsInGameThread());
	PendingFontTextureUploads.Remove(this);
	DirtyFontTextureSlices.Reset();
	// Covers teardown as well as a reload: BeginDestroy, DeinitFreeType and InitFreeType all come
	// through here, and the pooled textures are sized and formatted for the atlas being dropped.
	ReleaseAtlasStagingPool();
	FontTextureAtlasData.Reset();
	FontTextureBytesPerPixel = 0;
	// No atlas, no cells: the packers and the coverage cells start again with the next one.
	ResetAtlasPacking();
	if (IsValid(Texture) && Texture->IsRooted())
	{
		Texture->RemoveFromRoot();
	}
	Texture = nullptr;
}
void UDreamUIFontData_FreeTypeRender::RenewFontTexture()
{
	check(IsInGameThread());
	UTexture2DArray* OldTexture = Texture;
	const int32 TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
	const int32 NewSliceCount = OldTexture ? OldTexture->GetArraySize() + 1 : 1;
	if (FontTextureBytesPerPixel > 0)
	{
		verify(EnsureFontTextureAtlasData(NewSliceCount, FontTextureBytesPerPixel));
	}
	Texture = CreateFontTexture(TextureSize, NewSliceCount);
	check(Texture);
	Texture->AddToRoot();

	if (OldTexture)
	{
		if (OldTexture->IsRooted())
		{
			OldTexture->RemoveFromRoot();
		}
	}

	for (auto textItem : RenderTextArray)
	{
		if (textItem.IsValid())
		{
			textItem->ApplyFontTextureChange();
		}
	}
}

#if WITH_EDITOR
void UDreamUIFontData_FreeTypeRender::ReloadFont()
{
#if WITH_FREETYPE
	DeinitFreeType();
	InitFreeType();
#endif
}
void UDreamUIFontData_FreeTypeRender::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// A slider being dragged (a fallback's Scale, say) reports every step: the faces are reset and the texts laid out again
	// once, for the value it is let go at, not on every frame of the drag.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (RectPackCellSizeType > TextureSizeType)
		{
			RectPackCellSizeType = TextureSizeType;
		}
		const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, Fallbacks))
		{
			// What SetFallbacks does, except that an entry with no font stays: it is one being filled in, and keeps its index.
			// A font is never its own fallback.
			for (FDreamUIFontFallback& Entry : Fallbacks)
			{
				if (Entry.Font == this)
				{
					Entry.Font = nullptr;
				}
			}
			ApplyFallbacksChanged();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, BoldFont)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, ItalicFont)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, BoldItalicFont))
		{
			// The same as SetStyleFonts: a font is never its own style face.
			SetStyleFonts(BoldFont, ItalicFont, BoldItalicFont);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bPreferColorEmoji))
		{
			// The faces stay; the order emoji try them in does not.
			bFaceTableDirty = true;
			LayoutEpoch++;
			RecreateTexts();
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFace)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontType)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, LineHeightType)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, VerticalMetrics)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, EngineFont)
			)
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontType))
			{
				if (FontType == EDreamUIDynamicFontDataType::EngineFont)
				{
					FontBinaryArray.Empty();//clear cache font data when swich to UnrealFont
				}
			}
			ReloadFont();
		}
	}
	else
	{
		// An undo, or a change the editor does not name: whatever the fallbacks are now, the table is built from them again,
		// and what was laid out against the old ones is not reused.
		bFaceTableDirty = true;
		LayoutEpoch++;
	}
}

void UDreamUIFontData_FreeTypeRender::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
		// A cooked game reads nothing but these bytes (a cooked UFontFace's payload is not usable by FreeType), so they are
		// taken here the way the editor's InitFreeType takes them: the face's own data, else its file for a face whose
		// loading policy keeps the data out of the asset. An EngineFont font with no face used to crash the cook here.
		if (!IsValid(EngineFont))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s uses an engine font but has none set; it is cooked with no font data and draws no text."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName());
			FontBinaryArray.Empty();
			return;
		}
		if (EngineFont->GetFontFaceData()->HasData())
		{
			FontBinaryArray = EngineFont->GetFontFaceData()->GetData();
		}
		else if (!FFileHelper::LoadFileToArray(FontBinaryArray, *EngineFont->GetFontFilename()))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, could not read the engine font's file '%s'; it is cooked with no font data."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), *EngineFont->GetFontFilename());
			FontBinaryArray.Empty();
		}
	}
}

void UDreamUIFontData_FreeTypeRender::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
		FontBinaryArray.Empty();
	}
}
#endif

