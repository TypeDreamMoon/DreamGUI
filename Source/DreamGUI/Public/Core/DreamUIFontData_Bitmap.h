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
	FDreamUIBitmapCharKey(uint32 InCharCode, uint16 InCharSize, bool InIsBold)
	{
		this->CharCode = InCharCode;
		this->CharSize = InCharSize;
		this->bBold = InIsBold;
	}
	uint32 CharCode = 0;
	uint16 CharSize = 0;
	bool bBold = false;
	bool operator==(const FDreamUIBitmapCharKey& other)const
	{
		return this->CharCode == other.CharCode && this->CharSize == other.CharSize && this->bBold == other.bBold;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIBitmapCharKey& other)
	{
		return HashCombine(GetTypeHash(other.CharCode), GetTypeHash(other.CharSize), GetTypeHash(other.bBold));
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
	virtual void PushCharData(
		uint32 charCode, FVector2f lineOffset, FVector2f fontSpace, const FDreamUICharData& charData,
		const DreamUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FDreamUIOriginVertexData>& originVertices, TArray<FDreamUIMeshVertex>& vertices, TArray<FDreamUIMeshIndex>& triangleIndices
	)override;
	virtual void PrepareForPushCharData(UDreamText* InText)override;
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale) const override;
	//End UDreamUIFontData_FreeTypeRender interface
protected:
	float BoldSize; float ItalicSlop;
	TMap<FDreamUIBitmapCharKey, FDreamUICharData> CharDataMap;
	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)override;
	virtual void InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(uint32 CharCode, float CharSize, bool IsBold, FDreamUICharData& OutResult)override;
	virtual void AddCharDataToCache(uint32 CharCode, float CharSize, bool IsBold, FDreamUICharData& CharData)override;
	virtual bool RenderGlyph(uint32 CharCode, float CharSize, bool IsBold, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	virtual bool GetSupportDynamicPixelsPerUnit()override { return true; }
	virtual EDreamUIFontTextureMark GetFontTextureMark() override{ return EDreamUIFontTextureMark::Bitmap; }
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
