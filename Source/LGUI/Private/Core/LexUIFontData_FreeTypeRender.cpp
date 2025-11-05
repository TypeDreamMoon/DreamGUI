// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIFontData_FreeTypeRender.h"
#include "LGUI.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Core/Components/LexText.h"
#include "Core/LexUISettings.h"
#include "Utils/LexUIUtils.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Engine/FontFace.h"
#include "Internationalization/Culture.h"
#include "Rendering/Texture2DResource.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

void ULexUIFontData_FreeTypeRender::UpdateFontOnCultureChanged()
{
	FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (CultureFontMap.Contains(CurrentCulture))
		UnrealFont = CultureFontMap[CurrentCulture].LoadSynchronous();

	if (FontType == ELexUIDynamicFontDataType::UnrealFont)
	{
		FontBinaryArray.Empty();//clear cache font data when switch to UnrealFont
	}

#if WITH_FREETYPE
	DeinitFreeType();
	InitFreeType();
#endif
	for (auto textItem : RenderTextArray)
	{
		if (textItem.IsValid())
		{
			textItem->ApplyFontTextureChange();
		}
	}

	{
		int powerValue = 0;
		while (RectPackCellSize > 0)
		{
			RectPackCellSize = RectPackCellSize >> 1;
			powerValue++;
		}
		RectPackCellSize = 1;
		while (powerValue > 0)
		{
			RectPackCellSize = RectPackCellSize << 1;
			powerValue--;
		}
		
		RectPackCellSize = FMath::Clamp(RectPackCellSize, 64, ULexUISettings::ConvertAtlasTextureSizeTypeToSize(InitialSize));
	}
}

void ULexUIFontData_FreeTypeRender::FinishDestroy()
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
TArray<FString> ULexUIFontData_FreeTypeRender::CacheSubFaces(FT_LibraryRec_* InFTLibrary, const TArray<uint8>& InMemory)
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

void ULexUIFontData_FreeTypeRender::InitFreeType()
{
	if (bAlreadyInitialized)return;
	FT_Error error = 0;
	error = FT_Init_FreeType(&Library);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
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
			UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, have no face!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		}
#endif
	};

	if (FontType == ELexUIDynamicFontDataType::UnrealFont)
	{
		if (IsValid(UnrealFont))
		{
			if (UnrealFont->GetFontFaceData()->HasData())
			{
				NewFontFace(UnrealFont->GetFontFaceData()->GetData());
			}
			else
			{
				if (!FFileHelper::LoadFileToArray(TempFontBinaryArray, *UnrealFont->GetFontFilename()))
				{
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Failed to load or process '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *UnrealFont->GetFontFilename());
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
			UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, trying to load Unreal's font face, but not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
			return;
		}
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
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Font:%s, file: \"%s\" not exist! Will use cache data"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
				}
				else
				{
					UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, file: \"%s\" not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
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
					UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, file: \"%s\" not exist!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), *FontFilePathStr);
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
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		Face = nullptr;
		return;
	}
	else
	{
		UE_LOG(LGUI, Log, TEXT("[%s].%d Success, font:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		bAlreadyInitialized = true;
		bHasKerning = FT_HAS_KERNING(Face) != 0;

		Texture = nullptr;
		TextureSize = ULexUISettings::ConvertAtlasTextureSizeTypeToSize(InitialSize);
		BinPack = rbp::MaxRectsBinPack(RectPackCellSize, RectPackCellSize);
		if (InitialSize != ELexUIAtlasTextureSizeType::SIZE_256x256)
		{
			BinPack.PrepareExpendSizeForText(TextureSize, TextureSize, FreeRects, RectPackCellSize, false);
		}
		RenewFontTexture(0, TextureSize);
		OneDivideTextureSize = 1.0f / TextureSize;

		ClearCharDataCache();
	}
}

void ULexUIFontData_FreeTypeRender::DeinitFreeType()
{
	bAlreadyInitialized = false;
	if (Library != nullptr)
	{
		auto error = FT_Done_FreeType(Library);
		if (error)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Font:%s, error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()), ANSI_TO_TCHAR(GetErrorMessage(error)));
		}
		else
		{
			UE_LOG(LGUI, Log, TEXT("[%s].%d Success, font:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetName()));
		}
	}
	Face = nullptr;
	Library = nullptr;
	FreeRects.Empty();
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
FT_GlyphSlot ULexUIFontData_FreeTypeRender::RenderGlyphOnFreeType(const TCHAR& charCode, const float& charSize)
{
	InitFreeType();
	if (bAlreadyInitialized == false)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font '%s' is not initialized"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
		return nullptr;
	}

	auto error = FT_Set_Pixel_Sizes(Face, 0, charSize);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font '%s' FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	FT_GlyphSlot slot = Face->glyph;
	error = FT_Load_Glyph(Face, FT_Get_Char_Index(Face, charCode), FT_LOAD_DEFAULT);
	if (slot->glyph_index == 0//missing char in this font
		&& slot->metrics.width == 0 && slot->metrics.height == 0//some chars (/r, /n, space) only have width and height, no pixels
		)
	{
		if (FallbackFontArray.Num() > 0)
		{
			UE_LOG(LGUI, Log, TEXT("[%s].%d Font '%s' Can't find glyph (char:%s, code:%d), will search in fallbacks"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), *FString(1, &charCode), (int)charCode);
			for (int i = 0; i < FallbackFontArray.Num(); i++)
			{
				if (FallbackFontArray[i] == nullptr)continue;
				if (auto fallbackSlot = FallbackFontArray[i]->RenderGlyphOnFreeType(charCode, charSize))
				{
					return fallbackSlot;
				}
			}
		}
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font '%s' Can't find glyph (char:%s, code:%d) in fallbacks too"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), *FString(1, &charCode), (int)charCode);
		return nullptr;
	}
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font '%s' FT_Load_Glyph error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	error = FT_Render_Glyph(Face->glyph, FT_Render_Mode::FT_RENDER_MODE_NORMAL);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Font '%s' FT_Render_Glyph error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName(), ANSI_TO_TCHAR(GetErrorMessage(error)));
		return nullptr;
	}
	return slot;
}
#endif

UTexture2D* ULexUIFontData_FreeTypeRender::GetFontTexture()
{
	return Texture;
}

void ULexUIFontData_FreeTypeRender::PostLoad()
{
	Super::PostLoad();
	if (!bCultureFont)
		return;

	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &ULexUIFontData_FreeTypeRender::UpdateFontOnCultureChanged);

	FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	if (CultureFontMap.Contains(CurrentCulture))
		UnrealFont = CultureFontMap[CurrentCulture].LoadSynchronous();
}

void ULexUIFontData_FreeTypeRender::BeginDestroy()
{
	Super::BeginDestroy();
	if (!bCultureFont)
		return;

	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
}

void ULexUIFontData_FreeTypeRender::InitFont()
{
#if WITH_FREETYPE
	InitFreeType();
#endif
}

float ULexUIFontData_FreeTypeRender::GetKerning(const TCHAR& leftCharIndex, const TCHAR& rightCharIndex, const float& charSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return 0;
	if (!bHasKerning)return 0;
	auto error = FT_Set_Pixel_Sizes(Face, 0, charSize);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	FT_Vector kerning;
	error = FT_Get_Kerning(Face, FT_Get_Char_Index(Face, leftCharIndex), FT_Get_Char_Index(Face, rightCharIndex), FT_KERNING_DEFAULT, &kerning);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d FT_Get_Kerning error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	return kerning.x >> 6;
#else
	return 0;
#endif
}
float ULexUIFontData_FreeTypeRender::GetLineHeight(const float& fontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return fontSize;
	auto error = FT_Set_Pixel_Sizes(Face, 0, fontSize);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return fontSize;
	}
	return LineHeightType == ELexUIDynamicFontLineHeightType::FromFontFace ? (Face->size->metrics.height >> 6) : fontSize;
#else
	return fontSize;
#endif
}
float ULexUIFontData_FreeTypeRender::GetVerticalOffset(const float& fontSize)
{
#if WITH_FREETYPE
	if (Face == nullptr)return fontSize;
	auto error = FT_Set_Pixel_Sizes(Face, 0, fontSize);
	if (error)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d FT_Set_Pixel_Sizes error:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ANSI_TO_TCHAR(GetErrorMessage(error)));
		return 0;
	}
	return -((Face->size->metrics.ascender + Face->size->metrics.descender) >> 6) * 0.5f;
#else
	return fontSize;
#endif
}

void ULexUIFontData_FreeTypeRender::AddUIText(ULexText* InText)
{
	RenderTextArray.AddUnique(InText);
}
void ULexUIFontData_FreeTypeRender::RemoveUIText(ULexText* InText)
{
	RenderTextArray.Remove(InText);
}

FLexUICharData_HighPrecision ULexUIFontData_FreeTypeRender::GetCharData(const TCHAR& charCode, const float& charSize)
{
	auto Result = FLexUICharData_HighPrecision();
	if (charSize <= 0.0f)return Result;
	if (!GetCharDataFromCache(charCode, charSize, Result))//if charData not cached, then create it and add to cache
	{
		FGlyphBitmap glyphBitmap;
		if (!RenderGlyph(charCode, charSize, glyphBitmap))
		{
			return Result;
		}

		auto& calcBinpack = this->BinPack;
		auto& calcTexture = this->Texture;
		FLexUICharData uiCharData;
	PACK_AND_INSERT:
		if (PackRectAndInsertChar(glyphBitmap, calcBinpack, calcTexture, uiCharData))
		{

		}
		else
		{
			int32 newTextureSize = 0;
			if (FreeRects.Num() > 0)
			{
				calcBinpack.DoExpendSizeForText(FreeRects[FreeRects.Num() - 1]);
				FreeRects.RemoveAt(FreeRects.Num() - 1, 1, EAllowShrinking::No);
			}
			else
			{
				newTextureSize = TextureSize + TextureSize;
				UE_LOG(LGUI, Log, TEXT("[%s].%d Expend font texture size to:%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, newTextureSize);
				//expend by multiply 2
				calcBinpack.PrepareExpendSizeForText(newTextureSize, newTextureSize, FreeRects, RectPackCellSize);
				calcBinpack.DoExpendSizeForText(FreeRects[FreeRects.Num() - 1]);
				FreeRects.RemoveAt(FreeRects.Num() - 1, 1, EAllowShrinking::No);

				RenewFontTexture(TextureSize, newTextureSize);
				TextureSize = newTextureSize;
				OneDivideTextureSize = 1.0f / TextureSize;

				//scale down uv of prev chars
				ScaleDownUVofCachedChars();
				//tell UIText to scale down uv
				for (auto textItem : RenderTextArray)
				{
					if (textItem.IsValid())
					{
						textItem->ApplyFontTextureScaleUp();
					}
				}
			}

			goto PACK_AND_INSERT;
		}

		AddCharDataToCache(charCode, charSize, uiCharData);
		GetCharDataFromCache(charCode, charSize, Result);
	}
	return Result;
}

bool ULexUIFontData_FreeTypeRender::PackRectAndInsertChar(const FGlyphBitmap& InGlyphBitmap, rbp::MaxRectsBinPack& InOutBinpack, UTexture2D* InTexture, FLexUICharData& OutResult)
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

	auto packedRect = InOutBinpack.Insert(charRectWidth, charRectHeight, method);
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

		auto region = new FUpdateTextureRegion2D(packedRect.x, packedRect.y, 0, 0, InGlyphBitmap.width, InGlyphBitmap.height);
		UpdateFontTextureRegion(InTexture, region, packedRect.width * InGlyphBitmap.pixelSize, InGlyphBitmap.pixelSize, (uint8*)InGlyphBitmap.buffer);

		OutResult.Width = InGlyphBitmap.width + SPACE_NEED_EXPENDx2;
		OutResult.Height = InGlyphBitmap.height + SPACE_NEED_EXPENDx2;
		OutResult.XOffset = InGlyphBitmap.hOffset - SPACE_NEED_EXPEND;
		OutResult.YOffset = InGlyphBitmap.vOffset + SPACE_NEED_EXPEND;
		OutResult.XAdvance = InGlyphBitmap.hAdvance;
		OutResult.MinUV.X = OneDivideTextureSize * (packedRect.x - SPACE_NEED_EXPEND);
		OutResult.MaxUV.Y = OneDivideTextureSize * (packedRect.y - SPACE_NEED_EXPEND + OutResult.Height);
		OutResult.MaxUV.X = OneDivideTextureSize * (packedRect.x - SPACE_NEED_EXPEND + OutResult.Width);
		OutResult.MinUV.Y = OneDivideTextureSize * (packedRect.y - SPACE_NEED_EXPEND);
		return true;
	}
}
void ULexUIFontData_FreeTypeRender::ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)
{
	this->Texture = newTexture;
	TextureSize = newTextureSize;
	OneDivideTextureSize = 1.0f / TextureSize;
	//tell UIText to scale down uv
	for (auto textItem : RenderTextArray)
	{
		if (textItem.IsValid())
		{
			textItem->ApplyFontTextureScaleUp();
		}
	}
}

void ULexUIFontData_FreeTypeRender::UpdateFontTextureRegion(UTexture2D* InTexture, FUpdateTextureRegion2D* Region, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData)
{
	if (InTexture->GetResource())
	{
		struct FUpdateTextureRegionsData
		{
			FTexture2DResource* Texture2DResource;
			FUpdateTextureRegion2D* Region;
			uint32 SrcPitch;
			uint32 SrcBpp;
			uint8* SrcData;
		};
		FUpdateTextureRegionsData* RegionData = new FUpdateTextureRegionsData;

		auto Texture2DRes = (FTexture2DResource*)InTexture->GetResource();
		RegionData->Region = Region;
		RegionData->SrcPitch = SrcPitch;
		RegionData->SrcBpp = SrcBpp;
		RegionData->SrcData = SrcData;
		ENQUEUE_RENDER_COMMAND(FLexUIFontData_UpdateFontTextureRegionData)(
			[RegionData, Texture2DRes](FRHICommandListImmediate& RHICmdList)
			{
				
				RHICmdList.UpdateTexture2D(
					Texture2DRes->GetTexture2DRHI(),
					0,
					*RegionData->Region,
					RegionData->SrcPitch,
					RegionData->SrcData
					+ RegionData->Region->SrcY * RegionData->SrcPitch
					+ RegionData->Region->SrcX * RegionData->SrcBpp
				);
				FMemory::Free(RegionData->SrcData);
				FMemory::Free(RegionData->Region);
				delete RegionData;
			});
	}
}
void ULexUIFontData_FreeTypeRender::RenewFontTexture(int oldTextureSize, int newTextureSize)
{
	//get old texture pointer
	auto OldTexture = Texture;
	//create new texture
	Texture = CreateFontTexture(newTextureSize);
	Texture->AddToRoot();//@todo: is this really need to AddToRoot?

	//copy old texture to new one
	if (IsValid(OldTexture) && oldTextureSize > 0)
	{
		auto NewTexture = Texture;
		if (OldTexture->GetResource() != nullptr && NewTexture->GetResource() != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(FLexUIFontData_UpdateAndCopyFontTexture)(
				[OldTexture, NewTexture, oldTextureSize](FRHICommandListImmediate& RHICmdList)
			{
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition = FIntVector(0, 0, 0);
				CopyInfo.Size = FIntVector(oldTextureSize, oldTextureSize, 0);
				CopyInfo.DestPosition = FIntVector(0, 0, 0);
				RHICmdList.CopyTexture(
					((FTexture2DResource*)OldTexture->GetResource())->GetTexture2DRHI(),
					((FTexture2DResource*)NewTexture->GetResource())->GetTexture2DRHI(),
					CopyInfo
				);
				OldTexture->RemoveFromRoot();//ready for gc
			});
		}
		else
		{
			OldTexture->RemoveFromRoot();//ready for gc
		}
	}
}

#if WITH_EDITOR
void ULexUIFontData_FreeTypeRender::ReloadFont()
{
#if WITH_FREETYPE
	DeinitFreeType();
	InitFreeType();
#endif
	for (auto textItem : RenderTextArray)
	{
		if (textItem.IsValid())
		{
			textItem->ApplyFontTextureChange();
		}
	}
}
void ULexUIFontData_FreeTypeRender::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, FontFace)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, FontType)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, LineHeightType)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, UnrealFont)
			)
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_FreeTypeRender, FontType))
			{
				if (FontType == ELexUIDynamicFontDataType::UnrealFont)
				{
					FontBinaryArray.Empty();//clear cache font data when swich to UnrealFont
				}
			}
			ReloadFont();
		}

		{
			int powerValue = 0;
			while (RectPackCellSize > 0)
			{
				RectPackCellSize = RectPackCellSize >> 1;
				powerValue++;
			}
			RectPackCellSize = 1;
			while (powerValue > 0)
			{
				RectPackCellSize = RectPackCellSize << 1;
				powerValue--;
			}
			
			RectPackCellSize = FMath::Clamp(RectPackCellSize, 64, ULexUISettings::ConvertAtlasTextureSizeTypeToSize(InitialSize));
		}
	}
}
#endif

