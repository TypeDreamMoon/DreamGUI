// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIFontData_Bitmap.h"
#include "Core/Components/LexText.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif


void ULexUIFontData_Bitmap::PushCharData(
	uint32 charCode, const FVector2f& inLineOffset, const FVector2f& fontSpace, const FLexUICharData& charData,
	const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
	int verticesStartIndex, int indicesStartIndex,
	int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
	TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
)
{
	auto GetUnderlineOrStrikethroughCharGeo = [&](uint32 charCode, float overrideFontSize)
	{
		auto charData = this->GetCharData(charCode, overrideFontSize);
		charData.YOffset += this->GetVerticalOffset(overrideFontSize);

		float uvX = (charData.MaxUV.X - charData.MinUV.X) * 0.5f + charData.MinUV.X;
		charData.MinUV.X = charData.MaxUV.X = uvX;
		return charData;
	};

	outAdditionalVerticesCount = 4;
	outAdditionalIndicesCount = 6;

	FLexUICharData underlineCharGeo;
	FLexUICharData strikethroughCharGeo;
	//underline and strikethrough should not exist at same char
	if (richTextProperty.Underline)
	{
		outAdditionalVerticesCount += 4;
		outAdditionalIndicesCount += 6;
		underlineCharGeo = GetUnderlineOrStrikethroughCharGeo('_', richTextProperty.Size);
	}
	if (richTextProperty.Strikethrough)
	{
		outAdditionalVerticesCount += 4;
		outAdditionalIndicesCount += 6;
		strikethroughCharGeo = GetUnderlineOrStrikethroughCharGeo('-', richTextProperty.Size);
	}
	if (richTextProperty.Bold)
	{
		outAdditionalVerticesCount += 12;
		outAdditionalIndicesCount += 18;
	}
	int32 newVerticesCount = verticesStartIndex + outAdditionalVerticesCount;
	FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, newVerticesCount);
	FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, newVerticesCount);

	int32 newIndicesCount = indicesStartIndex + outAdditionalIndicesCount;
	FLexUIGeometry::LexUIGeometrySetArrayNum(triangleIndices, newIndicesCount);

	auto lineOffset = inLineOffset;
	if (richTextProperty.SupOrSubMode == LexUIRichTextParser::ESupOrSubMode::Sup)
	{
		lineOffset.Y += richTextProperty.Size * 0.5f;
	}
	else if (richTextProperty.SupOrSubMode == LexUIRichTextParser::ESupOrSubMode::Sub)
	{
		lineOffset.Y -= richTextProperty.Size * 0.5f;
	}
	
	//position
	{
		float offsetX = lineOffset.X + charData.XOffset;
		float offsetY = lineOffset.Y + charData.YOffset;
		float charWidth = charData.XAdvance + fontSpace.X;
		float x, y;

		int addVertCount = 0;
		if (richTextProperty.Bold)
		{
			x = offsetX;
			y = offsetY - charData.Height;
			auto vert0 = FVector3f(0, x, y);
			x = charData.Width + offsetX;
			auto vert1 = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			auto vert2 = FVector3f(0, x, y);
			x = charData.Width + offsetX;
			auto vert3 = FVector3f(0, x, y);
			if (richTextProperty.Italic)
			{
				auto vert01ItalicOffset = (charData.Height - charData.YOffset) * ItalicSlop;
				vert0.Y -= vert01ItalicOffset;
				vert1.Y -= vert01ItalicOffset;
				auto vert23ItalicOffset = charData.YOffset * ItalicSlop;
				vert2.Y += vert23ItalicOffset;
				vert3.Y += vert23ItalicOffset;
			}
			//bold left
			originVertices[verticesStartIndex].Position = vert0 + FVector3f(0, -BoldSize, 0);
			originVertices[verticesStartIndex + 1].Position = vert1 + FVector3f(0, -BoldSize, 0);
			originVertices[verticesStartIndex + 2].Position = vert2 + FVector3f(0, -BoldSize, 0);
			originVertices[verticesStartIndex + 3].Position = vert3 + FVector3f(0, -BoldSize, 0);
			//bold right
			originVertices[verticesStartIndex + 4].Position = vert0 + FVector3f(0, BoldSize, 0);
			originVertices[verticesStartIndex + 5].Position = vert1 + FVector3f(0, BoldSize, 0);
			originVertices[verticesStartIndex + 6].Position = vert2 + FVector3f(0, BoldSize, 0);
			originVertices[verticesStartIndex + 7].Position = vert3 + FVector3f(0, BoldSize, 0);
			//bold top
			originVertices[verticesStartIndex + 8].Position = vert0 + FVector3f(0, 0, BoldSize);
			originVertices[verticesStartIndex + 9].Position = vert1 + FVector3f(0, 0, BoldSize);
			originVertices[verticesStartIndex + 10].Position = vert2 + FVector3f(0, 0, BoldSize);
			originVertices[verticesStartIndex + 11].Position = vert3 + FVector3f(0, 0, BoldSize);
			//bold bottom
			originVertices[verticesStartIndex + 12].Position = vert0 + FVector3f(0, 0, -BoldSize);
			originVertices[verticesStartIndex + 13].Position = vert1 + FVector3f(0, 0, -BoldSize);
			originVertices[verticesStartIndex + 14].Position = vert2 + FVector3f(0, 0, -BoldSize);
			originVertices[verticesStartIndex + 15].Position = vert3 + FVector3f(0, 0, -BoldSize);

			addVertCount = 16;
		}
		else
		{
			x = offsetX;
			y = offsetY - charData.Height;
			auto& vert0 = originVertices[verticesStartIndex].Position;
			vert0 = FVector3f(0, x, y);
			x = charData.Width + offsetX;
			auto& vert1 = originVertices[verticesStartIndex + 1].Position;
			vert1 = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			auto& vert2 = originVertices[verticesStartIndex + 2].Position;
			vert2 = FVector3f(0, x, y);
			x = charData.Width + offsetX;
			auto& vert3 = originVertices[verticesStartIndex + 3].Position;
			vert3 = FVector3f(0, x, y);
			if (richTextProperty.Italic)
			{
				auto vert01ItalicOffset = (charData.Height - charData.YOffset) * ItalicSlop;
				vert0.Y -= vert01ItalicOffset;
				vert1.Y -= vert01ItalicOffset;
				auto vert23ItalicOffset = charData.YOffset * ItalicSlop;
				vert2.Y += vert23ItalicOffset;
				vert3.Y += vert23ItalicOffset;
			}

			addVertCount = 4;
		}

		if (richTextProperty.Underline)
		{
			offsetX = lineOffset.X;
			offsetY = lineOffset.Y + underlineCharGeo.YOffset;
			x = offsetX;
			y = offsetY - underlineCharGeo.Height;
			originVertices[verticesStartIndex + addVertCount].Position = FVector3f(0, x, y);
			x = charWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 1].Position = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			originVertices[verticesStartIndex + addVertCount + 2].Position = FVector3f(0, x, y);
			x = charWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 3].Position = FVector3f(0, x, y);

			addVertCount += 4;
		}
		if (richTextProperty.Strikethrough)
		{
			offsetX = lineOffset.X;
			offsetY = lineOffset.Y + strikethroughCharGeo.YOffset;
			x = offsetX;
			y = offsetY - strikethroughCharGeo.Height;
			originVertices[verticesStartIndex + addVertCount].Position = FVector3f(0, x, y);
			x = charWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 1].Position = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			originVertices[verticesStartIndex + addVertCount + 2].Position = FVector3f(0, x, y);
			x = charWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 3].Position = FVector3f(0, x, y);

			addVertCount += 4;
		}
	}
	//uv
	{
		int addVertCount = 0;
		if (richTextProperty.Bold)
		{
			//bold left
			vertices[verticesStartIndex].TextureCoordinate[0] = charData.GetUV0();
			vertices[verticesStartIndex + 1].TextureCoordinate[0] = charData.GetUV1();
			vertices[verticesStartIndex + 2].TextureCoordinate[0] = charData.GetUV2();
			vertices[verticesStartIndex + 3].TextureCoordinate[0] = charData.GetUV3();
			//bold right
			vertices[verticesStartIndex + 4].TextureCoordinate[0] = charData.GetUV0();
			vertices[verticesStartIndex + 5].TextureCoordinate[0] = charData.GetUV1();
			vertices[verticesStartIndex + 6].TextureCoordinate[0] = charData.GetUV2();
			vertices[verticesStartIndex + 7].TextureCoordinate[0] = charData.GetUV3();
			//bold top
			vertices[verticesStartIndex + 8].TextureCoordinate[0] = charData.GetUV0();
			vertices[verticesStartIndex + 9].TextureCoordinate[0] = charData.GetUV1();
			vertices[verticesStartIndex + 10].TextureCoordinate[0] = charData.GetUV2();
			vertices[verticesStartIndex + 11].TextureCoordinate[0] = charData.GetUV3();
			//bold bottom
			vertices[verticesStartIndex + 12].TextureCoordinate[0] = charData.GetUV0();
			vertices[verticesStartIndex + 13].TextureCoordinate[0] = charData.GetUV1();
			vertices[verticesStartIndex + 14].TextureCoordinate[0] = charData.GetUV2();
			vertices[verticesStartIndex + 15].TextureCoordinate[0] = charData.GetUV3();

			addVertCount = 16;
		}
		else
		{
			vertices[verticesStartIndex].TextureCoordinate[0] = charData.GetUV0();
			vertices[verticesStartIndex + 1].TextureCoordinate[0] = charData.GetUV1();
			vertices[verticesStartIndex + 2].TextureCoordinate[0] = charData.GetUV2();
			vertices[verticesStartIndex + 3].TextureCoordinate[0] = charData.GetUV3();

			addVertCount = 4;
		}
		if (richTextProperty.Underline)
		{
			vertices[verticesStartIndex + addVertCount].TextureCoordinate[0] = underlineCharGeo.GetUV0();
			vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[0] = underlineCharGeo.GetUV1();
			vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[0] = underlineCharGeo.GetUV2();
			vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[0] = underlineCharGeo.GetUV3();

			addVertCount += 4;
		}
		if (richTextProperty.Strikethrough)
		{
			vertices[verticesStartIndex + addVertCount].TextureCoordinate[0] = strikethroughCharGeo.GetUV0();
			vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[0] = strikethroughCharGeo.GetUV1();
			vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[0] = strikethroughCharGeo.GetUV2();
			vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[0] = strikethroughCharGeo.GetUV3();

			addVertCount += 4;
		}
	}
	//color
	{
		int addVertCount = 0;
		if (richTextProperty.Bold)
		{
			//bold left
			vertices[verticesStartIndex].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 1].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 2].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 3].Color = richTextProperty.Color;
			//bold right
			vertices[verticesStartIndex + 4].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 5].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 6].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 7].Color = richTextProperty.Color;
			//bold top
			vertices[verticesStartIndex + 8].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 9].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 10].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 11].Color = richTextProperty.Color;
			//bold bottom
			vertices[verticesStartIndex + 12].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 13].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 14].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 15].Color = richTextProperty.Color;

			addVertCount = 16;
		}
		else
		{
			vertices[verticesStartIndex].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 1].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 2].Color = richTextProperty.Color;
			vertices[verticesStartIndex + 3].Color = richTextProperty.Color;

			addVertCount = 4;
		}
		if (richTextProperty.Underline)
		{
			vertices[verticesStartIndex + addVertCount].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 1].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 2].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 3].Color = richTextProperty.Color;

			addVertCount += 4;
		}
		if (richTextProperty.Strikethrough)
		{
			vertices[verticesStartIndex + addVertCount].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 1].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 2].Color = richTextProperty.Color;
			vertices[verticesStartIndex + addVertCount + 3].Color = richTextProperty.Color;

			addVertCount += 4;
		}
	}
	//triangle
	{
		int addVertCount = 0;
		int addIndCount = 0;
		if (richTextProperty.Bold)
		{
			//bold left
			triangleIndices[indicesStartIndex] = verticesStartIndex;
			triangleIndices[indicesStartIndex + 1] = verticesStartIndex + 3;
			triangleIndices[indicesStartIndex + 2] = verticesStartIndex + 2;
			triangleIndices[indicesStartIndex + 3] = verticesStartIndex;
			triangleIndices[indicesStartIndex + 4] = verticesStartIndex + 1;
			triangleIndices[indicesStartIndex + 5] = verticesStartIndex + 3;
			//bold right
			triangleIndices[indicesStartIndex + 6] = verticesStartIndex + 4;
			triangleIndices[indicesStartIndex + 7] = verticesStartIndex + 7;
			triangleIndices[indicesStartIndex + 8] = verticesStartIndex + 6;
			triangleIndices[indicesStartIndex + 9] = verticesStartIndex + 4;
			triangleIndices[indicesStartIndex + 10] = verticesStartIndex + 5;
			triangleIndices[indicesStartIndex + 11] = verticesStartIndex + 7;
			//bold top
			triangleIndices[indicesStartIndex + 12] = verticesStartIndex + 8;
			triangleIndices[indicesStartIndex + 13] = verticesStartIndex + 11;
			triangleIndices[indicesStartIndex + 14] = verticesStartIndex + 10;
			triangleIndices[indicesStartIndex + 15] = verticesStartIndex + 8;
			triangleIndices[indicesStartIndex + 16] = verticesStartIndex + 9;
			triangleIndices[indicesStartIndex + 17] = verticesStartIndex + 11;
			//bold bottom
			triangleIndices[indicesStartIndex + 18] = verticesStartIndex + 12;
			triangleIndices[indicesStartIndex + 19] = verticesStartIndex + 15;
			triangleIndices[indicesStartIndex + 20] = verticesStartIndex + 14;
			triangleIndices[indicesStartIndex + 21] = verticesStartIndex + 12;
			triangleIndices[indicesStartIndex + 22] = verticesStartIndex + 13;
			triangleIndices[indicesStartIndex + 23] = verticesStartIndex + 15;

			addVertCount = 16;
			addIndCount = 24;
		}
		else
		{
			triangleIndices[indicesStartIndex] = verticesStartIndex;
			triangleIndices[indicesStartIndex + 1] = verticesStartIndex + 3;
			triangleIndices[indicesStartIndex + 2] = verticesStartIndex + 2;
			triangleIndices[indicesStartIndex + 3] = verticesStartIndex;
			triangleIndices[indicesStartIndex + 4] = verticesStartIndex + 1;
			triangleIndices[indicesStartIndex + 5] = verticesStartIndex + 3;

			addVertCount = 4;
			addIndCount = 6;
		}
		if (richTextProperty.Underline)
		{
			triangleIndices[indicesStartIndex + addIndCount] = verticesStartIndex + addVertCount;
			triangleIndices[indicesStartIndex + addIndCount + 1] = verticesStartIndex + addVertCount + 3;
			triangleIndices[indicesStartIndex + addIndCount + 2] = verticesStartIndex + addVertCount + 2;
			triangleIndices[indicesStartIndex + addIndCount + 3] = verticesStartIndex + addVertCount;
			triangleIndices[indicesStartIndex + addIndCount + 4] = verticesStartIndex + addVertCount + 1;
			triangleIndices[indicesStartIndex + addIndCount + 5] = verticesStartIndex + addVertCount + 3;

			addVertCount += 4;
			addIndCount += 6;
		}
		if (richTextProperty.Strikethrough)
		{
			triangleIndices[indicesStartIndex + addIndCount] = verticesStartIndex + addVertCount;
			triangleIndices[indicesStartIndex + addIndCount + 1] = verticesStartIndex + addVertCount + 3;
			triangleIndices[indicesStartIndex + addIndCount + 2] = verticesStartIndex + addVertCount + 2;
			triangleIndices[indicesStartIndex + addIndCount + 3] = verticesStartIndex + addVertCount;
			triangleIndices[indicesStartIndex + addIndCount + 4] = verticesStartIndex + addVertCount + 1;
			triangleIndices[indicesStartIndex + addIndCount + 5] = verticesStartIndex + addVertCount + 3;

			addVertCount += 4;
			addIndCount += 6;
		}
	}
}


bool ULexUIFontData_Bitmap::GetCharDataFromCache(const uint32& charCode, const float& charSize, FLexUICharData& OutResult)
{
	auto fontKey = FLexUIFontKeyData(charCode, charSize);
	if (auto charData = CharDataMap.Find(fontKey))
	{
		OutResult = *charData;
		return true;
	}
	return false;
}
void ULexUIFontData_Bitmap::AddCharDataToCache(const uint32& charCode, const float& charSize, FLexUICharData& charData)
{
	CharDataMap.Add(FLexUIFontKeyData(charCode, charSize), charData);
}

bool ULexUIFontData_Bitmap::RenderGlyph(const uint32& charCode, const float& charSize, FGlyphBitmap& OutResult)
{
#if WITH_FREETYPE
	//InSlot->bitmap_left equals (InSlot->metrics.horiBearingX >> 6), InSlot->bitmap_top equals (InSlot->metrics.horiBearingY >> 6)

	auto slot = RenderGlyphOnFreeType(charCode, charSize);
	if (slot == nullptr)
	{
		return false;
	}
	OutResult.width = slot->bitmap.width;
	OutResult.height = slot->bitmap.rows;
	OutResult.hOffset = slot->bitmap_left;
	OutResult.vOffset = slot->bitmap_top;
	OutResult.hAdvance = slot->metrics.horiAdvance / 64.0f;
	OutResult.pixelSize = 4;
	//pixel color
	int pixelCount = OutResult.width * OutResult.height;
	TArray<unsigned char> regionColorData;
	regionColorData.SetNumUninitialized(pixelCount * OutResult.pixelSize);
	FColor* regionColor = reinterpret_cast<FColor*>(regionColorData.GetData());
	for (int i = 0; i < pixelCount; i++)
	{
		auto& pixelColor = regionColor[i];
		pixelColor.R = pixelColor.G = pixelColor.B = 255;
		pixelColor.A = slot->bitmap.buffer[i];
	}
	OutResult.buffer = MoveTemp(regionColorData);
	return true;
#else
	return false;
#endif
}
void ULexUIFontData_Bitmap::ClearCharDataCache()
{
	CharDataMap.Empty();
}

UTexture2DArray* ULexUIFontData_Bitmap::CreateFontTexture(int InTextureSize, int InSliceCount)
{
	static int TextureNameSuffix = 0;
	auto NewTexture = NewObject<UTexture2DArray>(
		GetTransientPackage()
		, FName(*FString::Printf(TEXT("LexUIFontData_Bitmap_Texture_%d"), TextureNameSuffix++))
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
	FColor* PixelPtr = static_cast<FColor*>(MipData);
	constexpr FColor DefaultColor = FColor(255, 255, 255, 0);
	for (int i = 0, count = InTextureSize * InTextureSize * InSliceCount; i < count; i++)
	{
		PixelPtr[i] = DefaultColor;
	}
	Mip->BulkData.Unlock();

	NewTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	NewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	NewTexture->SRGB = false;
	NewTexture->Filter = TextureFilter::TF_Trilinear;
	NewTexture->UpdateResource();

	return NewTexture;
}

UTexture2D* ULexUIFontData_Bitmap::CreateIntermediateTexture(int InTextureSize)
{
	static int TextureNameSuffix = 0;
	auto ResultTexture = NewObject<UTexture2D>(
		GetTransientPackage(),
		FName(*FString::Printf(TEXT("LexUIFontData_Bitmap_Intermediate_%d"), TextureNameSuffix++)),
		RF_Transient
	);
	auto PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = InTextureSize;
	PlatformData->SizeY = InTextureSize;
	PlatformData->PixelFormat = PF_R8;
	// Allocate first mipmap.
	int32 NumBlocksX = InTextureSize / GPixelFormats[PF_R8].BlockSizeX;
	int32 NumBlocksY = InTextureSize / GPixelFormats[PF_R8].BlockSizeY;
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	PlatformData->Mips.Add(Mip);
	Mip->SizeX = InTextureSize;
	Mip->SizeY = InTextureSize;
	int DataSize = NumBlocksX * NumBlocksY * GPixelFormats[PF_R8].BlockBytes;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* DataPtr = Mip->BulkData.Realloc(DataSize);
	FMemory::Memzero(DataPtr, DataSize);
	Mip->BulkData.Unlock();
	ResultTexture->SetPlatformData(PlatformData);

	ResultTexture->CompressionSettings = TextureCompressionSettings::TC_DistanceFieldFont;
	ResultTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	ResultTexture->SRGB = false;
	ResultTexture->Filter = TextureFilter::TF_Bilinear;
	ResultTexture->UpdateResource();

	return ResultTexture;
}

void ULexUIFontData_Bitmap::ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)
{
	Super::ApplyPackingAtlasTextureExpand(newTexture, newTextureSize);
	//scale down uv of prev chars
	for (auto& charDataItem : CharDataMap)
	{
		auto& mapValue = charDataItem.Value;
		mapValue.MinUV.X *= 0.5f;
		mapValue.MaxUV.Y *= 0.5f;
		mapValue.MaxUV.X *= 0.5f;
		mapValue.MinUV.Y *= 0.5f;
	}
}

void ULexUIFontData_Bitmap::PrepareForPushCharData(ULexText* InText)
{
	BoldSize = InText->GetFontSize() * BoldRatio;
	ItalicSlop = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
}

#if WITH_EDITOR
void ULexUIFontData_Bitmap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_Bitmap, ItalicAngle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_Bitmap, BoldRatio)
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
