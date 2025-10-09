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
	FLexUIFontKeyData(const TCHAR& inCharCode, const uint16& inCharSize)
	{
		this->charCode = inCharCode;
		this->charSize = inCharSize;
	}
	TCHAR charCode = 0;
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
 * Font asset for UIText to render
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
		TCHAR charCode, const FVector2f& lineOffset, const FVector2f& fontSpace, const FLexUICharData_HighPrecision& charData,
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
	virtual UTexture2D* CreateFontTexture(int InTextureSize)override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(const TCHAR& charCode, const float& charSize, FLexUICharData_HighPrecision& OutResult)override;
	virtual void AddCharDataToCache(const TCHAR& charCode, const float& charSize, const FLexUICharData& charData)override;
	virtual void ScaleDownUVofCachedChars()override;
	virtual bool RenderGlyph(const TCHAR& charCode, const float& charSize, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	virtual bool GetSupportDynamicPixelsPerUnit()override { return true; }
	virtual float GetFontTextureMark() override{ return 2; }
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
