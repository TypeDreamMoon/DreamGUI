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
#include "RHICommandList.h"
#include "RHIResources.h"
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
#if WITH_EDITOR
		SubFaces = CacheSubFaces(Library, InFontBinary);
		if (SubFaces.Num() > 0)
		{
			FontFace = FMath::Clamp(FontFace, 0, SubFaces.Num() - 1);
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
	bInitFailed = false;
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
	// Shader-side bold keeps one atlas glyph per face; only the advance knows about the weight.
	const bool bShaderBold = IsBold && IsBoldSynthesizedInShader();
	const bool bAtlasBold = IsBold && !bShaderBold;
	if (!GetCharDataFromCache(Key, CharSize, bAtlasBold, Result))//if charData not cached, then create it and add to cache
	{
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
				return MakePendingCharData(Key, CharSize, IsBold);
			}
			if (!TakeSyncGlyphBudget())
			{
				FDreamGlyphRasterizer* Worker = GetOrCreateRasterizer();
				// A face the worker has no bytes for fails every job it is given; fall through to the
				// synchronous path for it instead of handing out a quad that never lands.
				if (Worker != nullptr && Worker->HasFaceSource(Key.FaceIndex))
				{
					FDreamGlyphRasterizer::FJob Job;
					Job.Key = Key;
					Job.CharSize = CharSize;
					Job.bBold = bAtlasBold;
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
		if (!RenderGlyph(Key, CharSize, bAtlasBold, glyphBitmap))//no valid glyph
		{
			return Result;
		}

		FDreamUICharData uiCharData;
		if (!InsertGlyphBitmap(glyphBitmap, uiCharData))
		{
			return Result;
		}
		AddCharDataToCache(Key, CharSize, bAtlasBold, uiCharData);
		GetCharDataFromCache(Key, CharSize, bAtlasBold, Result);
	}
	if (bShaderBold)
	{
		Result.XAdvance += CharSize * GetBoldRatio();
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
		int32 RegisteredFaces = 0;
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
			const TArray<uint8>* Bytes = nullptr;
			if (Owner->TempFontBinaryArray.Num() > 0)
			{
				Bytes = &Owner->TempFontBinaryArray;
			}
			else if (Owner->FontBinaryArray.Num() > 0)
			{
				Bytes = &Owner->FontBinaryArray;
			}
#if WITH_EDITOR
			// In the editor an EngineFont keeps its bytes in the face asset and nowhere else -- both
			// binary arrays are empty -- so the worker used to open no face at all and fail every job
			// it was handed, silently, and the glyph never arrived.
			else if (Owner->FontType == EDreamUIDynamicFontDataType::EngineFont
				&& IsValid(Owner->EngineFont) && Owner->EngineFont->GetFontFaceData()->HasData())
			{
				Bytes = &Owner->EngineFont->GetFontFaceData()->GetData();
			}
#endif
			if (Bytes == nullptr)continue;
			NewRasterizer->SetFaceSource(FaceIndex, MakeShared<const TArray<uint8>, ESPMode::ThreadSafe>(*Bytes), Owner->FontFace);
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
	for (FDreamGlyphRasterizer::FResult& Result : Results)
	{
		FAsyncGlyphRequest Request;
		Request.Glyph = Result.Job.Key;
		Request.CharSize = IsGlyphCacheSizeIndependent() ? 0.0f : Result.Job.CharSize;
		Request.bBold = Result.Job.bBold;
		PendingAsyncGlyphs.Remove(Request);
		if (!Result.bSucceeded)
		{
			if (!bLoggedAsyncGlyphFailure)
			{
				bLoggedAsyncGlyphFailure = true;
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Font:%s, face %d glyph %u failed to rasterize on a worker; falling back to synchronous glyphs. (reported once per font)")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), Result.Job.Key.FaceIndex, Result.Job.Key.GlyphIndex);
			}
			bAnyFailed = true;
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
	// A failure counts too: the quad handed out for a pending glyph is empty, so without a relayout
	// that character would simply never appear again.
	if (bAnyLanded || bAnyFailed)
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
	// Covers teardown as well as a reload: BeginDestroy, DeinitFreeType and InitFreeType all come
	// through here, and the pooled textures are sized and formatted for the atlas being dropped.
	ReleaseAtlasStagingPool();
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

