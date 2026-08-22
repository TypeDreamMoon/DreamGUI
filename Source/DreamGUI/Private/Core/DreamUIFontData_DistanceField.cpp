// Copyright 2019-present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamText.h"
#include "Materials/MaterialInterface.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#define SDF_IMPLEMENTATION
#include "Core/Components/DreamWidget.h"
#include "Engine/Texture2DArray.h"
#include "Utils/sdf/sdf.h"
#include "Core/Text/DreamGlyphSdf.h"
#include "UObject/DreamGUIObjectVersion.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#define LOCTEXT_NAMESPACE "DreamUIFontData_DistanceField"

UDreamUIFontData_DistanceField::UDreamUIFontData_DistanceField()
{
	RectPackCellSizeType = EDreamUIAtlasTextureSizeType::SIZE_512x512;

	// Whatever the project lists, in the order it lists them -- the picker shows this array as-is.
	for (const TSoftObjectPtr<UMaterialInterface>& Preset : UDreamGUISettings::Get()->TextEffectPresetMaterials)
	{
		if (UMaterialInterface* Material = UDreamGUISettings::LoadSetting(Preset, TEXT("TextEffectPresetMaterials")))
		{
			PresetMaterials.Add(Material);
		}
	}
}

bool UDreamUIFontData_DistanceField::GetCharDataFromCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& OutResult)
{
	auto CharKey = FDreamUIDistanceFieldCharKey(Glyph, IsBold);
	if (auto charData = CharDataMap.Find(CharKey))
	{
		OutResult = FDreamUICharData(*charData);
		const float vertexOffset = GetQuadShrinkTexels();
		OutResult.Width -= vertexOffset + vertexOffset;
		OutResult.Height -= vertexOffset + vertexOffset;
		OutResult.XOffset += vertexOffset;
		OutResult.YOffset -= vertexOffset;
		float uvOffset = vertexOffset * OneDivideTextureSize;
		OutResult.MinUV.X += uvOffset;
		OutResult.MaxUV.Y -= uvOffset;
		OutResult.MaxUV.X -= uvOffset;
		OutResult.MinUV.Y += uvOffset;
		//scale char by font size
		float scale = CharSize * OneDivideFontSize;
		OutResult.Width *= scale;
		OutResult.Height *= scale;
		OutResult.XOffset *= scale;
		OutResult.YOffset *= scale;
		OutResult.XAdvance *= scale;
		return true;
	}
	return false;
}
void UDreamUIFontData_DistanceField::AddCharDataToCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& CharData)
{
	CharDataMap.Add(FDreamUIDistanceFieldCharKey(Glyph, IsBold), CharData);
}

bool UDreamUIFontData_DistanceField::RenderGlyph(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FGlyphBitmap& OutResult)
{
#if WITH_FREETYPE
	if (SdfSource == EDreamUISdfSource::OutlineMultiChannel)
	{
		FDreamGlyphSdfResult Sdf;
		if (!FDreamGlyphSdf::GenerateMTSDF(GetFreeTypeFace(Glyph.FaceIndex), Glyph.GlyphIndex, (float)SampleFontSize, (float)SDFRadius, IsBold ? SampleFontSize * BoldRatio : 0.0f, Sdf))
		{
			return false;
		}
		OutResult.width = Sdf.Width;
		OutResult.height = Sdf.Height;
		OutResult.hOffset = Sdf.Left;
		OutResult.vOffset = Sdf.Top;
		OutResult.hAdvance = Sdf.Advance;
		OutResult.buffer = MoveTemp(Sdf.Pixels);
		OutResult.pixelSize = 4;
		return true;
	}
	auto slot = RenderGlyphOnFreeType(GetFreeTypeFace(Glyph.FaceIndex), Glyph.GlyphIndex, SampleFontSize, IsBold ? SampleFontSize * BoldRatio : 0);
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
	TArray<unsigned char> sdfResult;
	sdfResult.SetNumUninitialized(sourceBuffer.Num());
	FMemory::Memzero(sourceBuffer.GetData(), sourceBuffer.Num());
	FMemory::Memzero(sdfResult.GetData(), sourceBuffer.Num());
	int sourceBufferOffset = SDFRadius * glyphWidth + SDFRadius;
	int freetypeBufferOffset = 0;
	for (int h = 0, maxH = slot->bitmap.rows, maxW = slot->bitmap.width; h < maxH; h++)
	{
		FMemory::Memcpy(sourceBuffer.GetData() + sourceBufferOffset, slot->bitmap.buffer + freetypeBufferOffset, maxW);
		sourceBufferOffset += glyphWidth;
		freetypeBufferOffset += maxW;
	}
	sdfBuildDistanceFieldNoAlloc(sdfResult.GetData(), glyphWidth, SDFRadius, sourceBuffer.GetData(), glyphWidth, glyphHeight, glyphWidth, sdfTemp.GetData());
	//UE_LOG(DreamGUI, Error, TEXT("Gen sdf time: %f(ms)"), (FDateTime::Now() - time).GetTotalMilliseconds());
	OutResult.width = glyphWidth;
	OutResult.height = glyphHeight;
	OutResult.hOffset = slot->bitmap_left - SDFRadius;
	OutResult.vOffset = slot->bitmap_top + SDFRadius;
	OutResult.hAdvance = slot->metrics.horiAdvance * ONE_DIVIDE_64;
	OutResult.buffer = MoveTemp(sdfResult);
	OutResult.pixelSize = 1;
	return true;
#else
	return false;
#endif
}
void UDreamUIFontData_DistanceField::ClearCharDataCache()
{
	CharDataMap.Empty();
	LineHeight = VerticalOffset = -1;
	CachedAscent = CachedDescent = -1.0f;
}

UTexture2DArray* UDreamUIFontData_DistanceField::CreateFontTexture(int InTextureSize, int InSliceCount)
{
	static int TextureNameSuffix = 0;
	auto NewTexture = NewObject<UTexture2DArray>(
		GetTransientPackage()
		, FName(*FString::Printf(TEXT("DreamUIFontData_DistanceField_Texture_%d"), TextureNameSuffix++))
		, RF_Transient);
	const auto PixelFormat = SdfSource == EDreamUISdfSource::OutlineMultiChannel ? PF_B8G8R8A8 : PF_R8;

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
	PlatformData->Mips.Add(Mip);
	auto DataSize = (int64)GPixelFormats[PixelFormat].BlockBytes * NumBlocksX * NumBlocksY * InSliceCount;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* DataPtr = Mip->BulkData.Realloc(DataSize);
	if (!CopyFontTextureAtlasData(DataPtr, DataSize))
	{
		FMemory::Memzero(DataPtr, DataSize);
	}
	Mip->BulkData.Unlock();
	
	NewTexture->CompressionSettings = TextureCompressionSettings::TC_DistanceFieldFont;
	NewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	NewTexture->NeverStream = true;
	NewTexture->SRGB = false;
	NewTexture->Filter = TextureFilter::TF_Bilinear;
	NewTexture->UpdateResource();

	return NewTexture;
}

void UDreamUIFontData_DistanceField::ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)
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

void UDreamUIFontData_DistanceField::PrepareForLayout(float InExpandMeshSize)
{
	OneDivideFontSize = 1.0f / SampleFontSize;
	ExpandMeshSize = InExpandMeshSize;
}

float UDreamUIFontData_DistanceField::GetQuadShrinkTexels() const
{
	// Shrink the quad to the glyph to cut the empty area of the spread; 0.02 em stays so an edge
	// right at the bounds still has its anti-aliasing band. ExpandMeshSize keeps that much of the spread.
	const float Keep = ExpandMeshSize > 0 ? ExpandMeshSize : 0.0f;
	return (SDFRadius - Keep) - SampleFontSize * 0.02f;
}

FDreamTextGlyphPaintStyle UDreamUIFontData_DistanceField::GetGlyphPaintStyle(const FVector2f& InWorldScale) const
{
	FDreamTextGlyphPaintStyle Style;
	Style.ItalicSlope = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
	if (SdfSource == EDreamUISdfSource::OutlineMultiChannel)
	{
		Style.bMultiChannelField = true;
		Style.EmTexels = (float)SampleFontSize;
		Style.FieldSpreadTexels = (float)SDFRadius;
		Style.QuadMarginTexels = SDFRadius - GetQuadShrinkTexels();
		Style.TexelToUV = OneDivideTextureSize;
		Style.BoldDilateEm = BoldRatio * 0.5f;
	}
	return Style;
}

float UDreamUIFontData_DistanceField::GetKerning(uint32 leftCharIndex, uint32 rightCharIndex, float charSize)
{
	auto KerningPair = FDreamUIDistanceFieldFontKerningPair(leftCharIndex, rightCharIndex);
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
float UDreamUIFontData_DistanceField::GetLineHeight(float fontSize)
{
	if (LineHeight == -1)
	{
		LineHeight = Super::GetLineHeight(SampleFontSize);
	}
	return LineHeight * fontSize * OneDivideFontSize;
}
float UDreamUIFontData_DistanceField::GetVerticalOffset(float fontSize)
{
	if (VerticalOffset == -1)
	{
		VerticalOffset = Super::GetVerticalOffset(SampleFontSize);
	}
	return (VerticalOffset + AdditionalVerticalOffset) * fontSize * OneDivideFontSize;
}
float UDreamUIFontData_DistanceField::GetAscent(float fontSize)
{
	if (CachedAscent < 0.0f)
	{
		CachedAscent = Super::GetAscent(SampleFontSize);
	}
	// A positive AdditionalVerticalOffset lifts the glyphs: the baseline moves up inside the same box.
	return (CachedAscent - AdditionalVerticalOffset) * fontSize * OneDivideFontSize;
}
float UDreamUIFontData_DistanceField::GetDescent(float fontSize)
{
	if (CachedDescent < 0.0f)
	{
		CachedDescent = Super::GetDescent(SampleFontSize);
	}
	return (CachedDescent + AdditionalVerticalOffset) * fontSize * OneDivideFontSize;
}
UMaterialInterface* UDreamUIFontData_DistanceField::GetFontMaterial()
{
	return nullptr;
}

#if WITH_EDITOR
void UDreamUIFontData_DistanceField::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ReloadFont();
}
#endif

void UDreamUIFontData_DistanceField::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamUIFontData_DistanceField::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDreamGUIObjectVersion::GUID);
	Super::Serialize(Ar);
	// Assets saved before the outline field existed were authored against the bitmap one -- and
	// against a material that samples one channel -- so they keep it until someone switches them.
	if (Ar.IsLoading() && Ar.CustomVer(FDreamGUIObjectVersion::GUID) < FDreamGUIObjectVersion::SdfSourceOnFont)
	{
		SdfSource = EDreamUISdfSource::BitmapSingleChannel;
	}
}
#undef LOCTEXT_NAMESPACE
