// Copyright 2019-present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_DistanceField.h"
#include "DreamGUI.h"
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
#include "HAL/IConsoleManager.h"
#if WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#define LOCTEXT_NAMESPACE "DreamUIFontData_DistanceField"

namespace DreamFontDataDistanceFieldLocal
{
	/**
	 * DreamGUI.Text.SmallTextCoverage at 0: small-text coverage off for every font, the ones set On included. The project's
	 * answer (UDreamGUISettings::IsSmallTextCoverageEnabled) is for the fonts that inherit it, and cannot tell 0 from a project
	 * that has it off, so the variable itself is read. Found once, by name: it is registered with the module.
	 */
	bool IsSmallTextCoverageForcedOff()
	{
		static IConsoleVariable* CoverageVariable = nullptr;
		if (CoverageVariable == nullptr)
		{
			CoverageVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamGUI.Text.SmallTextCoverage"), false);
		}
		return CoverageVariable != nullptr && CoverageVariable->GetInt() == 0;
	}
}

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
	// Field glyphs only. A colour glyph (an emoji) is a bitmap at a size, not a field: it is kept and scaled by the base
	// class (UDreamUIFontData_FreeTypeRender::GetColorGlyphData) and never passes through the shrink and scale below.
	auto CharKey = FDreamUIDistanceFieldCharKey(Glyph, IsBold);
	if (auto charData = CharDataMap.Find(CharKey))
	{
		OutResult = FDreamUICharData(*charData);
		// Layout time: PrepareForLayout has just put the laying-out text's expand size on the font.
		const float vertexOffset = GetQuadShrinkTexels(ExpandMeshSize);
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
	// Locals, not statics: a static here is shared state that nothing serialises (the game-thread check
	// in GetGlyphData is all that ever made it safe) and that stays resident at the largest glyph the
	// process ever drew. The distance field build below dwarfs the allocation.
	TArray<unsigned char> sourceBuffer;
	TArray<unsigned char> sdfTemp;
	sourceBuffer.SetNumUninitialized(glyphWidth * glyphHeight);
	sdfTemp.SetNumUninitialized(sourceBuffer.Num() * sizeof(float) * 3);
	TArray<unsigned char> sdfResult;
	sdfResult.SetNumUninitialized(sourceBuffer.Num());
	FMemory::Memzero(sourceBuffer.GetData(), sourceBuffer.Num());
	FMemory::Memzero(sdfResult.GetData(), sourceBuffer.Num());
	int sourceBufferOffset = SDFRadius * glyphWidth + SDFRadius;
	// Through ReadGlyphRow: the rows are `pitch` bytes apart (a negative pitch runs them bottom-up) and
	// a strike embedded in the font is 1 bit per pixel, neither of which a flat memcpy of width bytes is.
	for (int h = 0, maxH = slot->bitmap.rows, maxW = slot->bitmap.width; h < maxH; h++)
	{
		UDreamUIFontData_FreeTypeRender::ReadGlyphRow(slot->bitmap, h, sourceBuffer.GetData() + sourceBufferOffset, maxW);
		sourceBufferOffset += glyphWidth;
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
	// Kerning is cached at SampleFontSize off the same face the char data came from, so it goes stale
	// with it: a reloaded or re-faced font kept the old pairs forever.
	KerningPairsMap.Empty();
	// Ascent and descent are no longer cached here: they come from the base class's per-face cache,
	// which DeinitFreeType drops, so they cannot survive a reload the way these two used to.
	LineHeight = VerticalOffset = -1;
}

int32 UDreamUIFontData_DistanceField::GetCharDataCacheCount() const
{
	return CharDataMap.Num();
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

void UDreamUIFontData_DistanceField::PrepareForLayout(float InExpandMeshSize)
{
	OneDivideFontSize = 1.0f / SampleFontSize;
	ExpandMeshSize = InExpandMeshSize;
}

float UDreamUIFontData_DistanceField::GetQuadShrinkTexels(float InExpandMeshSize) const
{
	// Shrink the quad to the glyph to cut the empty area of the spread; 0.02 em stays so an edge
	// right at the bounds still has its anti-aliasing band. ExpandMeshSize keeps that much of the spread,
	// and no more than the spread there is: a glyph's atlas cell is its bounds plus SDFRadius texels, so
	// a quad grown past that read its neighbours in the atlas and drew pieces of other letters around
	// every character.
	const float MaxKeep = FMath::Max(SDFRadius - SampleFontSize * 0.02f, 0.0f);
	const float Keep = FMath::Clamp(InExpandMeshSize, 0.0f, MaxKeep);
	return (SDFRadius - Keep) - SampleFontSize * 0.02f;
}

FDreamTextGlyphPaintStyle UDreamUIFontData_DistanceField::GetGlyphPaintStyle(const FVector2f& InWorldScale, float InExpandMeshSize) const
{
	FDreamTextGlyphPaintStyle Style;
	Style.ItalicSlope = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
	// Both sources are a field with the same convention (0.5 on the edge, +-SDFRadius texels of range),
	// so both take the text style, and both render bold as a dilation of the regular glyph: the same
	// growth FreeType's embolden gives, without its self-intersections, and tunable per text.
	Style.bDistanceField = true;
	Style.EmTexels = (float)SampleFontSize;
	Style.FieldSpreadTexels = (float)SDFRadius;
	Style.QuadMarginTexels = SDFRadius - GetQuadShrinkTexels(InExpandMeshSize);
	Style.TexelToUV = OneDivideTextureSize;
	Style.BoldDilateEm = BoldRatio * 0.5f;
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
	// Through GetFaceMetrics, which is where the sample-size-to-font-size scaling lives and which the
	// base class caches per face and size. The three members that used to cache these were never
	// dropped on a reload, so a font swapped in the editor kept the old face's metrics.
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!GetFaceMetrics(0, fontSize, Ascent, Descent, LineHeightValue))return fontSize;
	LineHeight = LineHeightValue;//shown in the details panel
	return LineHeightValue;
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
	// A positive AdditionalVerticalOffset lifts the glyphs: the baseline moves up inside the same box.
	// GetFaceMetrics applies it, and the base class's cache is the one that survives a reload.
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!GetFaceMetrics(0, fontSize, Ascent, Descent, LineHeightValue))return Super::GetAscent(fontSize);
	return Ascent;
}
float UDreamUIFontData_DistanceField::GetDescent(float fontSize)
{
	float Ascent = 0.0f, Descent = 0.0f, LineHeightValue = 0.0f;
	if (!GetFaceMetrics(0, fontSize, Ascent, Descent, LineHeightValue))return Super::GetDescent(fontSize);
	return Descent;
}
bool UDreamUIFontData_DistanceField::GetFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight)
{
	// The field is rasterized once, at SampleFontSize, so every metric of every face is that one scaled
	// linearly -- the same arithmetic GetAscent/GetDescent/GetLineHeight do for the primary face.
	float SampleAscent = 0.0f, SampleDescent = 0.0f, SampleLineHeight = 0.0f;
	if (!Super::GetFaceMetrics(FaceIndex, (float)SampleFontSize, SampleAscent, SampleDescent, SampleLineHeight))
	{
		return false;
	}
	const float Scale = FontSize * OneDivideFontSize;
	OutAscent = (SampleAscent - AdditionalVerticalOffset) * Scale;
	OutDescent = (SampleDescent + AdditionalVerticalOffset) * Scale;
	OutLineHeight = SampleLineHeight * Scale;
	return true;
}

UMaterialInterface* UDreamUIFontData_DistanceField::GetFontMaterial()
{
	return nullptr;
}

float UDreamUIFontData_DistanceField::GetAtlasEmTexels() const
{
	// The shader's small-text correction is switched by the sign: FontAtlasInfo has no fifth component, and the built-in
	// shader and MF_DreamUI_Shade both take this one as it is (DreamUIText.ush reads its magnitude).
	return UDreamGUISettings::Get()->bSmallTextCorrection ? (float)SampleFontSize : -(float)SampleFontSize;
}

bool UDreamUIFontData_DistanceField::SupportsCoverageGlyphs() const
{
#if WITH_FREETYPE
	// Coverage cells share the atlas with the field, four phases to a BGRA texel: the single-channel field's R8 atlas has
	// no room for them.
	if (SdfSource != EDreamUISdfSource::OutlineMultiChannel)
	{
		return false;
	}
	// The project's answer goes through the console variables (UDreamGUISettings::IsSmallTextCoverageEnabled), so an A/B
	// switch reaches every font; its 0 turns off a font set On as well.
	switch (SmallTextCoverage)
	{
	case EDreamUISmallTextCoverage::On:
		return !DreamFontDataDistanceFieldLocal::IsSmallTextCoverageForcedOff();
	case EDreamUISmallTextCoverage::Off:
		return false;
	default:
		return UDreamGUISettings::IsSmallTextCoverageEnabled();
	}
#else
	return false;
#endif
}

float UDreamUIFontData_DistanceField::GetCoverageMaxPixelSize() const
{
	return SmallTextMaxPixelSize > 0.0f ? SmallTextMaxPixelSize : UDreamGUISettings::GetSmallTextMaxPixelSize();
}

float UDreamUIFontData_DistanceField::GetColorGlyphReachEm() const
{
	return SampleFontSize > 0 ? (float)SDFRadius / (float)SampleFontSize : 0.0f;
}

void UDreamUIFontData_DistanceField::GetCoverageRasterStyle(uint8& OutHinting, float& OutBoldEm, float& OutItalicSlope) const
{
	// The same synthetic styles the field draws: bold grows the stroke by BoldRatio em in all, italic leans by ItalicAngle.
	OutHinting = (uint8)CoverageHinting;
	OutBoldEm = BoldRatio;
	OutItalicSlope = FMath::Tan(FMath::DegreesToRadians(ItalicAngle));
}

#if WITH_EDITOR
void UDreamUIFontData_DistanceField::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Not on every step of a slider being dragged: the reload comes with the value it is let go at.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
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
	// Assets saved before the outline field existed have no SdfSource, so they take the class default, the outline field.
	// They used to be moved to the bitmap-derived field here instead, which is what rounded the corners of every such
	// font: one channel cannot hold a sharp corner. The shader decodes both kinds the same way, so nothing an old asset was
	// authored against needs the old field. The warning is said once per load of such a package, and only for a package
	// (a persistent archive). An in-memory copy never gets here as old anyway: a loading archive with no versions of its
	// own reads with the versions registered now.
#if WITH_EDITOR
	if (Ar.IsLoading() && Ar.IsPersistent() && Ar.CustomVer(FDreamGUIObjectVersion::GUID) < FDreamGUIObjectVersion::SdfSourceOnFont)
	{
		UE_LOG(DreamGUI, Warning, TEXT("Font %s was saved before distance-field fonts chose their field. It now uses the multi-channel field from the glyph outlines (Sdf Source: Outline Multi Channel), which keeps corners sharp; resave it to keep that."), *GetPathName());
	}
#endif
	// Bold used to be FreeType's embolden at 0.08 em, chosen when the atlas baked it. As a field
	// dilation the same growth is a blob on CJK glyphs, so a font still on that old default moves to
	// the new one; a value someone set on purpose is kept.
	if (Ar.IsLoading() && Ar.CustomVer(FDreamGUIObjectVersion::GUID) < FDreamGUIObjectVersion::BoldAsDilation)
	{
		if (FMath::IsNearlyEqual(BoldRatio, 0.08f, 1e-4f))
		{
			BoldRatio = 0.04f;
		}
	}
}
#undef LOCTEXT_NAMESPACE
