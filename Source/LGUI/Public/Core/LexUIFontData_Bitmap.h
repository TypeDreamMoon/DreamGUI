// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/LexUIFontData_FreeTypeRender.h"
#include "LexUIFontData_Bitmap.generated.h"

struct FLexUIFontKeyData
{
public:
	FLexUIFontKeyData() {}
	FLexUIFontKeyData(const uint32& inCharCode, const uint16& inCharSize)
	{
		this->charCode = inCharCode;
		this->charSize = inCharSize;
	}
	uint32 charCode = 0;
	uint16 charSize = 0;
	bool operator==(const FLexUIFontKeyData& other)const
	{
		return this->charCode == other.charCode && this->charSize == other.charSize;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FLexUIFontKeyData& other)
	{
		return HashCombine(GetTypeHash(other.charCode), GetTypeHash(other.charSize));
	}
};

/**
 * Bitmap font asset for render text.
 * NOTE!!! This type is not maintained anymore, new features will not implement, use DistanceField font instead.
 */
UCLASS(BlueprintType)
class LGUI_API ULexUIFontData_Bitmap : public ULexUIFontData_FreeTypeRender
{
	GENERATED_BODY()
protected:
	/** angle of italic style in degree */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float ItalicAngle = 15.0f;
	/** bold size radio for bold style, large number create more bold effect */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float BoldRatio = 0.015f;
public:
	//Begin ULexUIFontData_FreeTypeRender interface
	virtual void PushCharData(
		uint32 charCode, const FVector2f& lineOffset, const FVector2f& fontSpace, const FLexUICharData& charData,
		const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
	)override;
	virtual void PrepareForPushCharData(ULexText* InText)override;
	//End ULexUIFontData_FreeTypeRender interface
protected:
	float BoldSize; float ItalicSlop;
	TMap<FLexUIFontKeyData, FLexUICharData> CharDataMap;
	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)override;
	virtual UTexture2D* CreateIntermediateTexture(int InTextureSize) override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(const uint32& charCode, const float& charSize, FLexUICharData& OutResult)override;
	virtual void AddCharDataToCache(const uint32& charCode, const float& charSize, FLexUICharData& charData)override;
	virtual bool RenderGlyph(const uint32& charCode, const float& charSize, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	virtual bool GetSupportDynamicPixelsPerUnit()override { return true; }
	virtual uint8 GetFontTextureMark() override{ return 1; }
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
