// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Text/DreamGlyphRasterizer.h"
#include "DreamGUI.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Core/Components/DreamText.h"
#include "TextureResource.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2DArray.h"
#include "Internationalization/Culture.h"
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
#endif

namespace
{
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> PendingFontTextureUploads;
	/** Fonts with glyphs on a worker, drained every frame with the texture uploads. */
	TSet<TWeakObjectPtr<UDreamUIFontData_FreeTypeRender>> FontsWithAsyncGlyphs;
	int32 AsyncGlyphSyncBudgetOverride = -1;
	uint64 SyncGlyphBudgetFrame = 0;
	int32 SyncGlyphsThisFrame = 0;
}

void UDreamUIFontData_FreeTypeRender::UpdateFontOnCultureChanged()
{
	FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (CultureFontMap.Contains(CurrentCulture))
		EngineFont = CultureFontMap[CurrentCulture].LoadSynchronous();

	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
		FontBinaryArray.Empty();//clear cache font data when switch to EngineFont
	}

#if WITH_FREETYPE
	DeinitFreeType();
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
	FT_Error error = 0;
	error = FT_Init_FreeType(&Library);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return;
	}

	auto NewFontFace = [&error, this](const TArray<uint8>& InFontBinary) {
#if WITH_EDITOR
		SubFaces = CacheSubFaces(Library, InFontBinary);
		if (SubFaces.Num() > 0)
		{
			FontFace = FMath::Clamp(FontFace, 0, SubFaces.Num());
#endif
			error = FT_New_Memory_Face(Library, InFontBinary.GetData(), InFontBinary.Num(), FontFace, &Face);
#if WITH_EDITOR
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, have no face!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		}
#endif
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

	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		Face = nullptr;
		return;
	}
	else
	{
		UE_LOG(DreamGUI, Log, TEXT("[%s].%d Success, font:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		bAlreadyInitialized = true;
		bHasKerning = FT_HAS_KERNING(Face) != 0;
		InitHarfBuzz();

		ReleaseFontTexture();
		CurrentTextureSlice = 0;
		auto RectPackCellSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType);
		BinPack = rbp::MaxRectsBinPack(RectPackCellSize, RectPackCellSize);
		auto TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
		BinPack.PrepareRectCellsForText(TextureSize, TextureSize, FreeRectCells, RectPackCellSize, false);
		RenewFontTexture();
		OneDivideTextureSize = 1.0f / TextureSize;

		ClearCharDataCache();
	}
}

void UDreamUIFontData_FreeTypeRender::DeinitFreeType()
{
	bAlreadyInitialized = false;
	// The worker keeps itself (and the font bytes it reads) alive until its task ends; results for
	// this font are simply dropped with it.
	Rasterizer.Reset();
	PendingAsyncGlyphs.Reset();
	FontsWithAsyncGlyphs.Remove(this);
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
	FreeRectCells.Empty();
	BinPack = rbp::MaxRectsBinPack(256, 256);
#if WITH_EDITORONLY_DATA
	SubFaces.Reset();
#endif
	FontFace = 0;
	bHasKerning = false;
	ClearCharDataCache();
}
#endif

#if WITH_FREETYPE
FT_GlyphSlot UDreamUIFontData_FreeTypeRender::RenderGlyphOnFreeType(FT_FaceRec_* InFace, uint32 GlyphIndex, float CharSize, float BoldSize)
{
	if (InFace == nullptr)
	{
		return nullptr;
	}
	auto error = FT_Set_Pixel_Sizes(InFace, 0, CharSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font '%s' FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
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

FT_FaceRec_* UDreamUIFontData_FreeTypeRender::GetFreeTypeFace(int32 FaceIndex)
{
	if (FaceIndex == 0)
	{
		InitFreeType();
		return bAlreadyInitialized ? Face : nullptr;
	}
	const int32 FallbackIndex = FaceIndex - 1;
	if (FallbackIndex < 0 || FallbackIndex >= FallbackFontArray.Num())
	{
		return nullptr;
	}
	UDreamUIFontData_FreeTypeRender* Fallback = FallbackFontArray[FallbackIndex];
	if (Fallback == nullptr || Fallback == this)
	{
		return nullptr;
	}
	return Fallback->GetFreeTypeFace(0);
}
#endif

#if WITH_HARFBUZZ
// The engine's HarfBuzz is built to allocate through these hooks. SlateCore defines its own copy
// inside its DLL; a module that links the static library needs one of its own.
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
#endif
}

void UDreamUIFontData_FreeTypeRender::DeinitHarfBuzz()
{
#if WITH_HARFBUZZ
	if (HarfBuzzFont != nullptr)
	{
		hb_font_destroy(HarfBuzzFont);
		HarfBuzzFont = nullptr;
	}
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
	return 1 + FallbackFontArray.Num();
}

bool UDreamUIFontData_FreeTypeRender::FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint)
{
#if WITH_FREETYPE
	if (FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex))
	{
		return FT_Get_Char_Index(TargetFace, Codepoint) != 0;
	}
#endif
	return false;
}

void* UDreamUIFontData_FreeTypeRender::GetShapingFont(int32 FaceIndex, float FontSize)
{
#if WITH_HARFBUZZ && WITH_FREETYPE
	if (FaceIndex != 0)
	{
		const int32 FallbackIndex = FaceIndex - 1;
		if (FallbackIndex < 0 || FallbackIndex >= FallbackFontArray.Num())return nullptr;
		UDreamUIFontData_FreeTypeRender* Fallback = FallbackFontArray[FallbackIndex];
		return (Fallback != nullptr && Fallback != this) ? Fallback->GetShapingFont(0, FontSize) : nullptr;
	}
	InitFreeType();
	if (HarfBuzzFont == nullptr)
	{
		return nullptr;
	}
	const int32 Scale = FMath::RoundToInt(FontSize * 64.0f);
	hb_font_set_scale(HarfBuzzFont, Scale, Scale);
	return HarfBuzzFont;
#else
	return nullptr;
#endif
}

bool UDreamUIFontData_FreeTypeRender::ResolveCodepoint(uint32 Codepoint, FDreamUIGlyphKey& OutKey)
{
#if WITH_FREETYPE
	const int32 FaceCount = GetFaceCount();
	for (int32 FaceIndex = 0; FaceIndex < FaceCount; FaceIndex++)
	{
		FT_FaceRec_* TargetFace = GetFreeTypeFace(FaceIndex);
		if (TargetFace == nullptr)continue;
		const uint32 GlyphIndex = FT_Get_Char_Index(TargetFace, Codepoint);
		if (GlyphIndex != 0)
		{
			OutKey = FDreamUIGlyphKey(FaceIndex, GlyphIndex);
			return true;
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
#endif
}

float UDreamUIFontData_FreeTypeRender::GetKerning(uint32 LeftCharCode, uint32 RightCharCode, float CharSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return 0;
	if (!bHasKerning)return 0;
	auto error = FT_Set_Pixel_Sizes(Face, 0, CharSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	FT_Vector kerning;
	error = FT_Get_Kerning(Face, FT_Get_Char_Index(Face, LeftCharCode), FT_Get_Char_Index(Face, RightCharCode), FT_KERNING_DEFAULT, &kerning);
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
float UDreamUIFontData_FreeTypeRender::GetLineHeight(float FontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return FontSize;
	auto error = FT_Set_Pixel_Sizes(Face, 0, FontSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return FontSize;
	}
	return LineHeightType == EDreamUIDynamicFontLineHeightType::FromFontFace ? (Face->size->metrics.height * ONE_DIVIDE_64) : FontSize;
#else
	return fontSize;
#endif
}
float UDreamUIFontData_FreeTypeRender::GetAscent(float FontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return Super::GetAscent(FontSize);
	auto error = FT_Set_Pixel_Sizes(Face, 0, FontSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return Super::GetAscent(FontSize);
	}
	// FontSizeAsLineHeight keeps the font's own proportions inside the smaller box.
	const float Ascender = Face->size->metrics.ascender * ONE_DIVIDE_64;
	if (LineHeightType == EDreamUIDynamicFontLineHeightType::FontSizeAsLineHeight)
	{
		const float Descender = -Face->size->metrics.descender * ONE_DIVIDE_64;
		const float Sum = Ascender + Descender;
		return Sum > 0.0f ? FontSize * (Ascender / Sum) : FontSize * 0.8f;
	}
	return Ascender;
#else
	return Super::GetAscent(FontSize);
#endif
}

float UDreamUIFontData_FreeTypeRender::GetDescent(float FontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return Super::GetDescent(FontSize);
	auto error = FT_Set_Pixel_Sizes(Face, 0, FontSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return Super::GetDescent(FontSize);
	}
	const float Descender = -Face->size->metrics.descender * ONE_DIVIDE_64;
	if (LineHeightType == EDreamUIDynamicFontLineHeightType::FontSizeAsLineHeight)
	{
		const float Ascender = Face->size->metrics.ascender * ONE_DIVIDE_64;
		const float Sum = Ascender + Descender;
		return Sum > 0.0f ? FontSize * (Descender / Sum) : FontSize * 0.2f;
	}
	return Descender;
#else
	return Super::GetDescent(FontSize);
#endif
}

float UDreamUIFontData_FreeTypeRender::GetVerticalOffset(float FontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return FontSize;
	auto error = FT_Set_Pixel_Sizes(Face, 0, FontSize);
	if (error)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	return -((Face->size->metrics.ascender + Face->size->metrics.descender) * ONE_DIVIDE_64) * 0.5f;
#else
	return fontSize;
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
	if (bAlreadyInitialized)
	{
		DeinitFreeType();
	}
#endif
}

void UDreamUIFontData_FreeTypeRender::SetFallbackFonts(const TArray<UDreamUIFontData_FreeTypeRender*>& InFallbacks)
{
	FallbackFontArray.Reset();
	for (UDreamUIFontData_FreeTypeRender* Fallback : InFallbacks)
	{
		if (Fallback != nullptr && Fallback != this)
		{
			FallbackFontArray.Add(Fallback);
		}
	}
	ClearCharDataCache();
}

void UDreamUIFontData_FreeTypeRender::SetEngineFont(UFontFace* Value)
{
	EngineFont = Value;
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::GetCharData(uint32 CharCode, float CharSize, bool IsBold)
{
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
	auto Result = FDreamUICharData();
	if (CharSize <= 0.0f)return Result;
	const FDreamUIGlyphKey Key(FaceIndex, GlyphIndex);
	if (!GetCharDataFromCache(Key, CharSize, IsBold, Result))//if charData not cached, then create it and add to cache
	{
		// Off-thread when the font can and the frame's synchronous budget is spent.
		float PixelsPerEm = 0.0f, SpreadPixels = 0.0f, BoldPixels = 0.0f;
		if (UDreamUISettings::GetAsyncGlyphRasterization() && GetAsyncRasterParams(CharSize, IsBold, PixelsPerEm, SpreadPixels, BoldPixels))
		{
			FAsyncGlyphRequest Request;
			Request.Glyph = Key;
			Request.CharSize = IsGlyphCacheSizeIndependent() ? 0.0f : CharSize;
			Request.bBold = IsBold;
			if (PendingAsyncGlyphs.Contains(Request))
			{
				return MakePendingCharData(Key, CharSize, IsBold);
			}
			if (!TakeSyncGlyphBudget())
			{
				if (FDreamGlyphRasterizer* Worker = GetOrCreateRasterizer())
				{
					FDreamGlyphRasterizer::FJob Job;
					Job.Key = Key;
					Job.CharSize = CharSize;
					Job.bBold = IsBold;
					Job.PixelsPerEm = PixelsPerEm;
					Job.SpreadPixels = SpreadPixels;
					Job.BoldPixels = BoldPixels;
					Worker->Enqueue(Job);
					PendingAsyncGlyphs.Add(Request);
					FontsWithAsyncGlyphs.Add(this);
					return MakePendingCharData(Key, CharSize, IsBold);
				}
			}
		}

		FGlyphBitmap glyphBitmap;
		if (!RenderGlyph(Key, CharSize, IsBold, glyphBitmap))//no valid glyph
		{
			return Result;
		}

		FDreamUICharData uiCharData;
		if (!InsertGlyphBitmap(glyphBitmap, uiCharData))
		{
			return Result;
		}
		AddCharDataToCache(Key, CharSize, IsBold, uiCharData);
		GetCharDataFromCache(Key, CharSize, IsBold, Result);
	}
	return Result;
}

bool UDreamUIFontData_FreeTypeRender::InsertGlyphBitmap(const FGlyphBitmap& InGlyphBitmap, FDreamUICharData& OutResult)
{
	for (int32 Attempt = 0; Attempt < 64; Attempt++)
	{
		if (PackRectAndInsertChar(InGlyphBitmap, BinPack, OutResult))
		{
			return true;
		}
		if (FreeRectCells.Num() > 0)//use free cells
		{
			BinPack.DoRectCellsForText(FreeRectCells[FreeRectCells.Num() - 1]);
			FreeRectCells.RemoveAt(FreeRectCells.Num() - 1, 1, EAllowShrinking::No);
		}
		else//no free cells, move to next slice of Texture2DArray
		{
			CurrentTextureSlice++;
			UE_LOG(DreamGUI, Log, TEXT("[%s].%d Expend Texture2DArray slice to: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, Texture->GetArraySize() + 1);
			//add new slice to Texture2DArray
			auto RectPackCellSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(RectPackCellSizeType);
			BinPack = rbp::MaxRectsBinPack(RectPackCellSize, RectPackCellSize);
			auto TextureSize = UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(TextureSizeType);
			BinPack.PrepareRectCellsForText(TextureSize, TextureSize, FreeRectCells, RectPackCellSize, false);

			FreeRectCells.RemoveAt(FreeRectCells.Num() - 1, 1, EAllowShrinking::No);

			RenewFontTexture();
			OneDivideTextureSize = 1.0f / TextureSize;
		}
	}
	UE_LOG(DreamGUI, Error, TEXT("[%s].%d Font:%s, a %dx%d glyph never fit the atlas."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), (int32)InGlyphBitmap.width, (int32)InGlyphBitmap.height);
	return false;
}

FDreamUICharData UDreamUIFontData_FreeTypeRender::MakePendingCharData(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold)
{
	FDreamUICharData Result;
	Result.bPending = true;
#if WITH_FREETYPE
	// The advance alone, unscaled, so the line lays out where it will end up once the quad lands.
	if (FT_FaceRec_* TargetFace = GetFreeTypeFace(Glyph.FaceIndex))
	{
		if (FT_Load_Glyph(TargetFace, Glyph.GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0 && TargetFace->units_per_EM != 0)
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

void UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(int32 Budget)
{
	AsyncGlyphSyncBudgetOverride = Budget;
	SyncGlyphBudgetFrame = 0;
}

FDreamGlyphRasterizer* UDreamUIFontData_FreeTypeRender::GetOrCreateRasterizer()
{
#if WITH_FREETYPE
	if (!Rasterizer.IsValid())
	{
		InitFreeType();
		if (!bAlreadyInitialized)
		{
			return nullptr;
		}
		TSharedRef<FDreamGlyphRasterizer, ESPMode::ThreadSafe> NewRasterizer = MakeShared<FDreamGlyphRasterizer, ESPMode::ThreadSafe>();
		// The worker reads its own copies of the font files; fallbacks contribute theirs by face index.
		const int32 FaceCount = GetFaceCount();
		for (int32 FaceIndex = 0; FaceIndex < FaceCount; FaceIndex++)
		{
			UDreamUIFontData_FreeTypeRender* Owner = this;
			if (FaceIndex > 0)
			{
				const int32 FallbackIndex = FaceIndex - 1;
				Owner = FallbackFontArray.IsValidIndex(FallbackIndex) ? FallbackFontArray[FallbackIndex] : nullptr;
				if (Owner == nullptr || Owner == this)continue;
				Owner->InitFreeType();
			}
			const TArray<uint8>& Bytes = Owner->TempFontBinaryArray.Num() > 0 ? Owner->TempFontBinaryArray : Owner->FontBinaryArray;
			if (Bytes.Num() == 0)continue;
			NewRasterizer->SetFaceSource(FaceIndex, MakeShared<const TArray<uint8>, ESPMode::ThreadSafe>(Bytes), Owner->FontFace);
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
		return;
	}
	TArray<FDreamGlyphRasterizer::FResult> Results;
	Rasterizer->Drain(Results);
	if (Results.Num() == 0)
	{
		return;
	}
	bool bAnyLanded = false;
	for (FDreamGlyphRasterizer::FResult& Result : Results)
	{
		FAsyncGlyphRequest Request;
		Request.Glyph = Result.Job.Key;
		Request.CharSize = IsGlyphCacheSizeIndependent() ? 0.0f : Result.Job.CharSize;
		Request.bBold = Result.Job.bBold;
		PendingAsyncGlyphs.Remove(Request);
		if (!Result.bSucceeded)
		{
			continue;
		}
		FDreamUICharData Existing;
		if (GetCharDataFromCache(Result.Job.Key, Result.Job.CharSize, Result.Job.bBold, Existing))
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
			AddCharDataToCache(Result.Job.Key, Result.Job.CharSize, Result.Job.bBold, CharData);
			bAnyLanded = true;
		}
	}
	if (bAnyLanded)
	{
		OnGlyphsReady.Broadcast();
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

bool UDreamUIFontData_FreeTypeRender::PackRectAndInsertChar(const FGlyphBitmap& InGlyphBitmap, rbp::MaxRectsBinPack& InOutBinPack, FDreamUICharData& OutResult)
{
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
	const auto SPACE_NEED_EXPEND = this->Get_SPACE_NEED_EXPEND();
	const auto SPACE_NEED_EXPENDx2 = SPACE_NEED_EXPEND + SPACE_NEED_EXPEND;
	const auto SPACE_BETWEEN_GLYPH_RECT = this->Get_SPACE_BETWEEN_GLYPH() + SPACE_NEED_EXPEND;
	const auto SPACE_BETWEEN_GLYPH_RECTx2 = SPACE_BETWEEN_GLYPH_RECT + SPACE_BETWEEN_GLYPH_RECT;

	int charRectWidth = InGlyphBitmap.width + SPACE_BETWEEN_GLYPH_RECTx2;
	int charRectHeight = InGlyphBitmap.height + SPACE_BETWEEN_GLYPH_RECTx2;
	auto method = rbp::MaxRectsBinPack::RectBestAreaFit;

	auto packedRect = InOutBinPack.Insert(charRectWidth, charRectHeight, method);
	if (packedRect.height <= 0)//means this area cannot fit the char
	{
		return false;
	}
	else//this area can fit the char, so copy pixel color into texture
	{
		//remove space
		packedRect.x += SPACE_BETWEEN_GLYPH_RECT;
		packedRect.y += SPACE_BETWEEN_GLYPH_RECT;
		packedRect.width -= SPACE_BETWEEN_GLYPH_RECTx2;
		packedRect.height -= SPACE_BETWEEN_GLYPH_RECTx2;

		if (!UpdateFontTextureRegion(
			packedRect.x,
			packedRect.y,
			CurrentTextureSlice,
			InGlyphBitmap.width,
			InGlyphBitmap.height,
			packedRect.width * InGlyphBitmap.pixelSize,
			InGlyphBitmap.pixelSize,
			InGlyphBitmap.buffer))
		{
			return false;
		}

		OutResult.Width = InGlyphBitmap.width + SPACE_NEED_EXPENDx2;
		OutResult.Height = InGlyphBitmap.height + SPACE_NEED_EXPENDx2;
		OutResult.XOffset = InGlyphBitmap.hOffset - SPACE_NEED_EXPEND;
		OutResult.YOffset = InGlyphBitmap.vOffset + SPACE_NEED_EXPEND;
		OutResult.XAdvance = InGlyphBitmap.hAdvance;
		OutResult.MinUV.X = OneDivideTextureSize * (packedRect.x - SPACE_NEED_EXPEND);
		OutResult.MaxUV.Y = OneDivideTextureSize * (packedRect.y - SPACE_NEED_EXPEND + OutResult.Height);
		OutResult.MaxUV.X = OneDivideTextureSize * (packedRect.x - SPACE_NEED_EXPEND + OutResult.Width);
		OutResult.MinUV.Y = OneDivideTextureSize * (packedRect.y - SPACE_NEED_EXPEND);
		OutResult.SliceIndex = CurrentTextureSlice;
		return true;
	}
}
void UDreamUIFontData_FreeTypeRender::ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)
{

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

	DirtyFontTextureSlices.Add(Slice);
	PendingFontTextureUploads.Add(this);
	return true;
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
	const uint32 RowDataSize = TextureSize * FontTextureBytesPerPixel;
	const int64 SliceDataSize = RowDataSize * TextureSize;
	TArray<int32> SlicesToUpload = DirtyFontTextureSlices.Array();
	SlicesToUpload.Sort();

	TArray<uint8> UploadData;
	UploadData.SetNumUninitialized(static_cast<int32>(SliceDataSize * SlicesToUpload.Num()));
	for (int32 UploadIndex = 0; UploadIndex < SlicesToUpload.Num(); ++UploadIndex)
	{
		FMemory::Memcpy(
			UploadData.GetData() + UploadIndex * SliceDataSize,
			FontTextureAtlasData.GetData() + SlicesToUpload[UploadIndex] * SliceDataSize,
			SliceDataSize);
	}

	FTextureResource* TextureResource = Texture->GetResource();
	DirtyFontTextureSlices.Reset();
	ENQUEUE_RENDER_COMMAND(FDreamUIFontData_FlushFontTexture)(
		[TextureResource, TextureSize, RowDataSize, SliceDataSize, SlicesToUpload = MoveTemp(SlicesToUpload), UploadData = MoveTemp(UploadData)](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TextureRHI = TextureResource->GetTexture2DArrayRHI();
			if (!ensure(TextureRHI != nullptr && TextureRHI->IsValid()))
			{
				return;
			}
			for (int32 UploadIndex = 0; UploadIndex < SlicesToUpload.Num(); ++UploadIndex)
			{
				uint32 DestStride = 0;
				uint8* DestData = static_cast<uint8*>(RHICmdList.LockTexture2DArray(
					TextureRHI, SlicesToUpload[UploadIndex], 0, RLM_WriteOnly, DestStride, false));
				if (ensure(DestData != nullptr && DestStride >= RowDataSize))
				{
					const uint8* SourceData = UploadData.GetData() + UploadIndex * SliceDataSize;
					for (int32 Row = 0; Row < TextureSize; ++Row)
					{
						FMemory::Memcpy(DestData + static_cast<int64>(Row) * DestStride, SourceData + static_cast<int64>(Row) * RowDataSize, RowDataSize);
					}
				}
				RHICmdList.UnlockTexture2DArray(TextureRHI, SlicesToUpload[UploadIndex], 0, false);
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
			if (Font->PendingAsyncGlyphs.Num() == 0)
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
	FontTextureAtlasData.Reset();
	FontTextureBytesPerPixel = 0;
	CurrentTextureSlice = 0;
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
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (RectPackCellSizeType > TextureSizeType)
		{
			RectPackCellSizeType = TextureSizeType;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFace)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontType)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, LineHeightType)
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
}

void UDreamUIFontData_FreeTypeRender::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (FontType == EDreamUIDynamicFontDataType::EngineFont)
	{
		FontBinaryArray = EngineFont->GetFontFaceData()->GetData();
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

