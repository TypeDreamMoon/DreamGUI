// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/Components/DreamText.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif


bool UDreamUIFontData_Bitmap::GetCharDataFromCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& OutResult)
{
	auto fontKey = FDreamUIBitmapCharKey(Glyph, CharSize, IsBold);
	if (auto charData = CharDataMap.Find(fontKey))
	{
		OutResult = *charData;
		return true;
	}
	return false;
}
void UDreamUIFontData_Bitmap::AddCharDataToCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& CharData)
{
	CharDataMap.Add(FDreamUIBitmapCharKey(Glyph, CharSize, IsBold), CharData);
}

bool UDreamUIFontData_Bitmap::RenderGlyph(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FGlyphBitmap& OutResult)
{
#if WITH_FREETYPE
	auto slot = RenderGlyphOnFreeType(GetFreeTypeFace(Glyph.FaceIndex), Glyph.GlyphIndex, CharSize, IsBold ? CharSize * BoldRatio : 0);
	if (slot == nullptr)
	{
		return false;
	}
	OutResult.width = slot->bitmap.width;
	OutResult.height = slot->bitmap.rows;
	OutResult.hOffset = slot->bitmap_left;
	OutResult.vOffset = slot->bitmap_top;
	OutResult.hAdvance = slot->metrics.horiAdvance * ONE_DIVIDE_64;
	OutResult.pixelSize = 4;
	//pixel color
	const int32 GlyphWidth = (int32)slot->bitmap.width;
	const int32 GlyphHeight = (int32)slot->bitmap.rows;
	const int32 pixelCount = GlyphWidth * GlyphHeight;
	TArray<unsigned char> regionColorData;
	regionColorData.SetNumUninitialized(pixelCount * OutResult.pixelSize);
	FColor* regionColor = reinterpret_cast<FColor*>(regionColorData.GetData());
	// Row by row through ReadGlyphRow, because the rows are `pitch` bytes apart (possibly backwards)
	// and are not necessarily 8-bit grey; a linear walk of buffer[i] read garbage for a mono strike.
	TArray<uint8> RowCoverage;
	RowCoverage.SetNumUninitialized(FMath::Max(GlyphWidth, 1));
	for (int32 y = 0; y < GlyphHeight; y++)
	{
		ReadGlyphRow(slot->bitmap, y, RowCoverage.GetData(), GlyphWidth);
		for (int32 x = 0; x < GlyphWidth; x++)
		{
			auto& pixelColor = regionColor[y * GlyphWidth + x];
			pixelColor.R = pixelColor.G = pixelColor.B = 255;
			pixelColor.A = RowCoverage[x];
		}
	}
	OutResult.buffer = MoveTemp(regionColorData);
	return true;
#else
	return false;
#endif
}
void UDreamUIFontData_Bitmap::ClearCharDataCache()
{
	CharDataMap.Empty();
}

UTexture2DArray* UDreamUIFontData_Bitmap::CreateFontTexture(int InTextureSize, int InSliceCount)
{
	static int TextureNameSuffix = 0;
	auto NewTexture = NewObject<UTexture2DArray>(
		GetTransientPackage()
		, FName(*FString::Printf(TEXT("DreamUIFontData_Bitmap_Texture_%d"), TextureNameSuffix++))
		, RF_Transient);

	auto PixelFormat = PF_B8G8R8A8;
	auto PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = InTextureSize;
	PlatformData->SizeY = InTextureSize;
	PlatformData->PixelFormat = PixelFormat;
	PlatformData->SetNumSlices(InSliceCount);
	NewTexture->SetPlatformData(PlatformData);

	// Allocate first mipmap.
	int32 NumBlocksX = InTextureSize / GPixelFormats[PixelFormat].BlockSizeX;
	int32 NumBlocksY = InTextureSize / GPixelFormats[PixelFormat].BlockSizeY;
	FTexture2DMipMap* Mip = new FTexture2DMipMap(InTextureSize, InTextureSize, InSliceCount);
	NewTexture->GetPlatformData()->Mips.Add(Mip);
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* MipData = Mip->BulkData.Realloc((int64)GPixelFormats[PixelFormat].BlockBytes * NumBlocksX * NumBlocksY * InSliceCount);
	const int64 DataSize = static_cast<int64>(GPixelFormats[PixelFormat].BlockBytes) * NumBlocksX * NumBlocksY * InSliceCount;
	if (!CopyFontTextureAtlasData(MipData, DataSize))
	{
		InitializeFontTextureAtlasSlice(static_cast<uint8*>(MipData), DataSize);
	}
	Mip->BulkData.Unlock();

	NewTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	NewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	NewTexture->NeverStream = true;
	NewTexture->SRGB = false;
	NewTexture->Filter = TextureFilter::TF_Trilinear;
	NewTexture->UpdateResource();

	return NewTexture;
}

void UDreamUIFontData_Bitmap::InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const
{
	check(SliceDataSize % sizeof(FColor) == 0);
	FColor* PixelData = reinterpret_cast<FColor*>(SliceData);
	const int64 PixelCount = SliceDataSize / sizeof(FColor);
	constexpr FColor DefaultColor(255, 255, 255, 0);
	for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		PixelData[PixelIndex] = DefaultColor;
	}
}

FDreamTextGlyphPaintStyle UDreamUIFontData_Bitmap::GetGlyphPaintStyle(const FVector2f& InWorldScale, float InExpandMeshSize) const
{
	FDreamTextGlyphPaintStyle Style;
	Style.ItalicSlope = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
	return Style;
}

#if WITH_EDITOR
void UDreamUIFontData_Bitmap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_Bitmap, ItalicAngle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_Bitmap, BoldRatio)
			)
		{
			for (auto& textItem : RenderTextArray)
			{
				if (textItem.IsValid())
				{
					textItem->ApplyRecreateText();
				}
			}
		}
	}
}
#endif
