// Copyright 2019-present LexLiu. All Rights Reserved.

#include "Core/LexUIFontData_DistanceField.h"
#include "Core/Components/LexText.h"
#include "Core/LexUIManager.h"
#include "Utils/LexUIUtils.h"
#include "Materials/MaterialInterface.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#define SDF_IMPLEMENTATION
#include "Utils/sdf/sdf.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#define LOCTEXT_NAMESPACE "LexUIFontData_DistanceField"

ULexUIFontData_DistanceField::ULexUIFontData_DistanceField()
{
	InitialSize = ELexUIAtlasTextureSizeType::SIZE_1024x1024;
	RectPackCellSize = 1024;
}

bool ULexUIFontData_DistanceField::GetCharDataFromCache(const TCHAR& charCode, const float& charSize, FLexUICharData_HighPrecision& OutResult)
{
	if (auto charData = CharDataMap.Find(charCode))
	{
		OutResult = FLexUICharData_HighPrecision(*charData);
		float vertexOffset = SDFRadius - SampleFontSize * 0.02f;//slightly expand it in-case too sharp edge
		OutResult.Width -= vertexOffset + vertexOffset;
		OutResult.Height -= vertexOffset + vertexOffset;
		OutResult.XOffset += vertexOffset;
		OutResult.YOffset -= vertexOffset;
		float uvOffset = vertexOffset * OneDivideTextureSize;
		OutResult.MinUV.X += uvOffset;
		OutResult.MaxUV.Y -= uvOffset;
		OutResult.MaxUV.X -= uvOffset;
		OutResult.MinUV.Y += uvOffset;
		float scale = charSize * OneDivideFontSize;
		OutResult.Width *= scale;
		OutResult.Height *= scale;
		OutResult.XOffset *= scale;
		OutResult.YOffset *= scale;
		OutResult.XAdvance *= scale;
		return true;
	}
	return false;
}
void ULexUIFontData_DistanceField::AddCharDataToCache(const TCHAR& charCode, const float& charSize, const FLexUICharData& charData)
{
	CharDataMap.Add(charCode, charData);
}
void ULexUIFontData_DistanceField::ScaleDownUVofCachedChars()
{
	for (auto& charDataItem : CharDataMap)
	{
		auto& mapValue = charDataItem.Value;
		mapValue.MinUV.X *= 0.5f;
		mapValue.MaxUV.Y *= 0.5f;
		mapValue.MaxUV.X *= 0.5f;
		mapValue.MinUV.Y *= 0.5f;
	}
}
bool ULexUIFontData_DistanceField::RenderGlyph(const TCHAR& charCode, const float& charSize, FGlyphBitmap& OutResult)
{
#if WITH_FREETYPE
	auto slot = RenderGlyphOnFreeType(charCode, SampleFontSize);
	if (slot == nullptr)
	{
		return false;
	}
	//auto time = FDateTime::Now();
	int glyphWidth = slot->bitmap.width + SDFRadius + SDFRadius;
	int glyphHeight = slot->bitmap.rows + SDFRadius + SDFRadius;
	static TArray<unsigned char> sourceBuffer;
	static TArray<unsigned char> sdfTemp;
	sourceBuffer.SetNumUninitialized(glyphWidth * glyphHeight);
	sdfTemp.SetNumUninitialized(sourceBuffer.Num() * sizeof(float) * 3);
	unsigned char* sdfResult = new unsigned char[sourceBuffer.Num()];
	FMemory::Memzero(sourceBuffer.GetData(), sourceBuffer.Num());
	FMemory::Memzero(sdfResult, sourceBuffer.Num());
	int sourceBufferOffset = SDFRadius * glyphWidth + SDFRadius;
	int freetypeBufferOffset = 0;
	for (int h = 0, maxH = slot->bitmap.rows, maxW = slot->bitmap.width; h < maxH; h++)
	{
		FMemory::Memcpy(sourceBuffer.GetData() + sourceBufferOffset, slot->bitmap.buffer + freetypeBufferOffset, maxW);
		sourceBufferOffset += glyphWidth;
		freetypeBufferOffset += maxW;
	}
	sdfBuildDistanceFieldNoAlloc(sdfResult, glyphWidth, SDFRadius, sourceBuffer.GetData(), glyphWidth, glyphHeight, glyphWidth, sdfTemp.GetData());
	//UE_LOG(LGUI, Error, TEXT("Gen sdf time: %f(ms)"), (FDateTime::Now() - time).GetTotalMilliseconds());
	OutResult.width = glyphWidth;
	OutResult.height = glyphHeight;
	OutResult.hOffset = slot->bitmap_left - SDFRadius;
	OutResult.vOffset = slot->bitmap_top + SDFRadius;
	OutResult.hAdvance = slot->metrics.horiAdvance >> 6;
	OutResult.buffer = sdfResult;
	OutResult.pixelSize = 1;
	return true;
#else
	return false;
#endif
}
void ULexUIFontData_DistanceField::ClearCharDataCache()
{
	CharDataMap.Empty();
	LineHeight = VerticalOffset = -1;
}

UTexture2D* ULexUIFontData_DistanceField::CreateFontTexture(int InTextureSize)
{
	static int TextureNameSuffix = 0;
	auto ResultTexture = NewObject<UTexture2D>(
		GetTransientPackage(),
		FName(*FString::Printf(TEXT("LexUIFontData_DistanceField_Texture_%d"), TextureNameSuffix++)),
		EObjectFlags::RF_Transient
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
	void* dataPtr = Mip->BulkData.Realloc(DataSize);
	FMemory::Memzero(dataPtr, DataSize);
	Mip->BulkData.Unlock();
	ResultTexture->SetPlatformData(PlatformData);

	ResultTexture->CompressionSettings = TextureCompressionSettings::TC_DistanceFieldFont;
	ResultTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	ResultTexture->SRGB = false;
	ResultTexture->Filter = TextureFilter::TF_Trilinear;
	ResultTexture->UpdateResource();

	return ResultTexture;
}

void ULexUIFontData_DistanceField::ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)
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

void ULexUIFontData_DistanceField::PrepareForPushCharData(ULexText* InText)
{
	ItalicSlop = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
	OneDivideFontSize = 1.0f / SampleFontSize;
	SDFRadius = SampleFontSize * 0.25f;//use 1/4 of FontSize can get good result
}

bool ULexUIFontData_DistanceField::GetRequireNormalAndTangent()
{
	return true;//for tilt look
}

float ULexUIFontData_DistanceField::GetKerning(const TCHAR& leftCharIndex, const TCHAR& rightCharIndex, const float& charSize)
{
	auto KerningPair = FLexUIDistanceFieldFontKerningPair(leftCharIndex, rightCharIndex);
	if (auto KerningValuePtr = KerningPairsMap.Find(KerningPair))
	{
		return (*KerningValuePtr) * charSize * OneDivideFontSize;
	}
	else
	{
		auto KerningValue = Super::GetKerning(leftCharIndex, rightCharIndex, SampleFontSize);
		KerningPairsMap.Add(KerningPair, KerningValue);
		return KerningValue * charSize * OneDivideFontSize;
	}
}
float ULexUIFontData_DistanceField::GetLineHeight(const float& fontSize)
{
	if (LineHeight == -1)
	{
		LineHeight = Super::GetLineHeight(SampleFontSize);
	}
	return LineHeight * fontSize * OneDivideFontSize;
}
float ULexUIFontData_DistanceField::GetVerticalOffset(const float& fontSize)
{
	if (VerticalOffset == -1)
	{
		VerticalOffset = Super::GetVerticalOffset(SampleFontSize);
	}
	return VerticalOffset * fontSize * OneDivideFontSize;
}
UMaterialInterface* ULexUIFontData_DistanceField::GetFontMaterial()
{
	return nullptr;
}

void ULexUIFontData_DistanceField::PushCharData(
	TCHAR charCode, const FVector2f& inLineOffset, const FVector2f& fontSpace, const FLexUICharData_HighPrecision& charData,
	const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
	int verticesStartIndex, int indicesStartIndex,
	int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
	TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
)
{
	auto GetUnderlineOrStrikethroughCharGeo = [&](TCHAR charCode, float overrideFontSize)
	{
		auto charData = this->GetCharData(charCode, overrideFontSize);
		charData.YOffset += this->GetVerticalOffset(overrideFontSize);

		float uvX = (charData.MaxUV.X - charData.MinUV.X) * 0.5f + charData.MinUV.X;
		charData.MinUV.X = charData.MaxUV.X = uvX;
		return charData;
	};

	outAdditionalVerticesCount = 4;
	outAdditionalIndicesCount = 6;

	FLexUICharData_HighPrecision underlineCharGeo;
	FLexUICharData_HighPrecision strikethroughCharGeo;
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
	int32 newVerticesCount = verticesStartIndex + outAdditionalVerticesCount;
	FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, newVerticesCount, false);
	FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, newVerticesCount, false);

	int32 newIndicesCount = indicesStartIndex + outAdditionalIndicesCount;
	FLexUIGeometry::LexUIGeometrySetArrayNum(triangleIndices, newIndicesCount, false);

	auto lineOffset = inLineOffset;
	if (richTextProperty.SupOrSubMode == LexUIRichTextParser::ESupOrSubMode::Sup)
	{
		lineOffset.Y += richTextProperty.Size * 0.5f;
	}
	else if (richTextProperty.SupOrSubMode == LexUIRichTextParser::ESupOrSubMode::Sub)
	{
		lineOffset.Y -= richTextProperty.Size * 0.5f;
	}
	
	float boldUVExtendWidth = 0;
	float boldUVExtendHeight = 0;
	//position
	{
		float offsetX = lineOffset.X + charData.XOffset;
		float offsetY = lineOffset.Y + charData.YOffset;
		float charAdvanceWidth = charData.XAdvance + fontSpace.X;
		if (richTextProperty.Bold)
		{
			charAdvanceWidth += charAdvanceWidth * BoldRatio;
		}
		float x, y;

		int addVertCount = 0;
		{
			float charWidth = charData.Width;
			float charHeight = charData.Height;
			if (richTextProperty.Bold)
			{
				float boldExtendWidth = charWidth * BoldRatio;
				float boldExtendHeight = charHeight * BoldRatio;
				charWidth += boldExtendWidth * 2;
				charHeight += boldExtendHeight * 2;
				offsetX -= boldExtendWidth;
				offsetY += boldExtendHeight;

				auto uvRange = charData.GetUVRange();
				boldUVExtendWidth = boldExtendWidth / charData.Width * uvRange.X;
				boldUVExtendHeight = boldExtendHeight / charData.Height * uvRange.Y;
			}
			x = offsetX;
			y = offsetY - charHeight;
			auto& vert0 = originVertices[verticesStartIndex].Position;
			vert0 = FVector3f(0, x, y);
			x = charWidth + offsetX;
			auto& vert1 = originVertices[verticesStartIndex + 1].Position;
			vert1 = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			auto& vert2 = originVertices[verticesStartIndex + 2].Position;
			vert2 = FVector3f(0, x, y);
			x = charWidth + offsetX;
			auto& vert3 = originVertices[verticesStartIndex + 3].Position;
			vert3 = FVector3f(0, x, y);
			if (richTextProperty.Italic)
			{
				auto vert01ItalicOffset = (charHeight - charData.YOffset) * ItalicSlop;
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
			x = charAdvanceWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 1].Position = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			originVertices[verticesStartIndex + addVertCount + 2].Position = FVector3f(0, x, y);
			x = charAdvanceWidth + offsetX;
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
			x = charAdvanceWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 1].Position = FVector3f(0, x, y);
			x = offsetX;
			y = offsetY;
			originVertices[verticesStartIndex + addVertCount + 2].Position = FVector3f(0, x, y);
			x = charAdvanceWidth + offsetX;
			originVertices[verticesStartIndex + addVertCount + 3].Position = FVector3f(0, x, y);

			addVertCount += 4;
		}
	}
	//uv
	{
		int addVertCount = 0;
		{
			if (richTextProperty.Bold)
			{
				vertices[verticesStartIndex].TextureCoordinate[0] = charData.GetUV0() + FVector2f(-boldUVExtendWidth, -boldUVExtendHeight);
				vertices[verticesStartIndex + 1].TextureCoordinate[0] = charData.GetUV1() + FVector2f(boldUVExtendWidth, -boldUVExtendHeight);
				vertices[verticesStartIndex + 2].TextureCoordinate[0] = charData.GetUV2() + FVector2f(-boldUVExtendWidth, boldUVExtendHeight);
				vertices[verticesStartIndex + 3].TextureCoordinate[0] = charData.GetUV3() + FVector2f(boldUVExtendWidth, boldUVExtendHeight);
			}
			else
			{
				vertices[verticesStartIndex].TextureCoordinate[0] = charData.GetUV0();
				vertices[verticesStartIndex + 1].TextureCoordinate[0] = charData.GetUV1();
				vertices[verticesStartIndex + 2].TextureCoordinate[0] = charData.GetUV2();
				vertices[verticesStartIndex + 3].TextureCoordinate[0] = charData.GetUV3();
			}

			//bold and scale
			{
				auto tempBoldSize = richTextProperty.Bold ? BoldRatio * 0.5f : 0.0f;
				vertices[verticesStartIndex].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + 1].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + 2].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + 3].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
			}

			addVertCount = 4;
		}
		if (richTextProperty.Underline)
		{
			vertices[verticesStartIndex + addVertCount].TextureCoordinate[0] = underlineCharGeo.GetUV0();
			vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[0] = underlineCharGeo.GetUV1();
			vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[0] = underlineCharGeo.GetUV2();
			vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[0] = underlineCharGeo.GetUV3();

			//bold and scale, bold is not needed for underline and strikethrough, but scale is needed
			{
				auto tempBoldSize = 0.0f;
				vertices[verticesStartIndex + addVertCount].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
			}

			addVertCount += 4;
		}
		if (richTextProperty.Strikethrough)
		{
			vertices[verticesStartIndex + addVertCount].TextureCoordinate[0] = strikethroughCharGeo.GetUV0();
			vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[0] = strikethroughCharGeo.GetUV1();
			vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[0] = strikethroughCharGeo.GetUV2();
			vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[0] = strikethroughCharGeo.GetUV3();

			//bold and scale, bold is not needed for underline and strikethrough, but scale is needed
			{
				auto tempBoldSize = 0.0f;
				vertices[verticesStartIndex + addVertCount].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 1].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 2].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
				vertices[verticesStartIndex + addVertCount + 3].TextureCoordinate[2] = FVector2f(tempBoldSize, 0);
			}

			addVertCount += 4;
		}
	}
	//color
	{
		int addVertCount = 0;
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

		triangleIndices[indicesStartIndex] = verticesStartIndex;
		triangleIndices[indicesStartIndex + 1] = verticesStartIndex + 3;
		triangleIndices[indicesStartIndex + 2] = verticesStartIndex + 2;
		triangleIndices[indicesStartIndex + 3] = verticesStartIndex;
		triangleIndices[indicesStartIndex + 4] = verticesStartIndex + 1;
		triangleIndices[indicesStartIndex + 5] = verticesStartIndex + 3;

		addVertCount = 4;
		addIndCount = 6;

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

#if WITH_EDITOR
void ULexUIFontData_DistanceField::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (
			PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_DistanceField, SampleFontSize)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_DistanceField, SDFRadius)
			)
		{
			ReloadFont();
		}
		if (
			PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_DistanceField, ItalicAngle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_DistanceField, BoldRatio)
			)
		{
			for (auto& textItem : RenderTextArray)
			{
				if (textItem.IsValid())
				{
					textItem->ApplyFontMaterialChange();
				}
			}
		}
	}
}
#endif

void ULexUIFontData_DistanceField::PostInitProperties()
{
	Super::PostInitProperties();
}
#undef LOCTEXT_NAMESPACE
