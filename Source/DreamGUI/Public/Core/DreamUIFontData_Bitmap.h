// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "DreamUIFontData_Bitmap.generated.h"

struct FDreamUIBitmapCharKey
{
public:
	FDreamUIBitmapCharKey() {}
	FDreamUIBitmapCharKey(const FDreamUIGlyphKey& InGlyph, float InCharSize, bool InIsBold)
	{
		this->Glyph = InGlyph;
		// The 26.6 size FreeType is asked to rasterize at. A uint16 of the size truncated it, so every size from 16 to
		// 16.99 shared the one 16px glyph.
		this->CharSize26Dot6 = FMath::RoundToInt(FMath::Max(InCharSize, 1.0f) * 64.0f);
		this->bBold = InIsBold;
	}
	FDreamUIGlyphKey Glyph;
	int32 CharSize26Dot6 = 0;
	bool bBold = false;
	bool operator==(const FDreamUIBitmapCharKey& other)const
	{
		return this->Glyph == other.Glyph && this->CharSize26Dot6 == other.CharSize26Dot6 && this->bBold == other.bBold;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIBitmapCharKey& other)
	{
		return HashCombine(GetTypeHash(other.Glyph), GetTypeHash(other.CharSize26Dot6), GetTypeHash(other.bBold));
	}
};

/**
 * Bitmap font asset for render text.
 * NOTE!!! This type is not maintained anymore, new features will not implement, use DistanceField font instead.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIFontData_Bitmap : public UDreamUIFontData_FreeTypeRender
{
	GENERATED_BODY()
protected:
	/** angle of italic style in degree */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float ItalicAngle = 15.0f;
	/** bold size radio for bold style, large number create more bold effect */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float BoldRatio = 0.06f;
public:
	//Begin UDreamUIFontData_FreeTypeRender interface
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale, float InExpandMeshSize) const override;
	/**
	 * How much wider, in em, a bold glyph's advance is than the regular one's: BoldRatio, the strength the raster is
	 * emboldened by. FreeType's embolden makes the outline that much wider and the advance grows by the same.
	 */
	virtual float GetBoldRatio() override { return BoldRatio; }
	//End UDreamUIFontData_FreeTypeRender interface
protected:
	TMap<FDreamUIBitmapCharKey, FDreamUICharData> CharDataMap;
	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)override;
	virtual void InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const override;

	virtual bool GetCharDataFromCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& OutResult)override;
	virtual void AddCharDataToCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& CharData)override;
	virtual bool RenderGlyph(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;
	virtual int32 GetCharDataCacheCount() const override;

	virtual bool GetSupportDynamicPixelsPerUnit()override { return true; }
	virtual EDreamUIFontTextureMark GetFontTextureMark() override{ return EDreamUIFontTextureMark::Bitmap; }
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
