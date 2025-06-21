// Copyright 2019-present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIFontData_FreeTypeRender.h"
#include "LexUIFontData_DistanceField.generated.h"

struct FLexUIDistanceFieldFontKerningPair
{
public:
	FLexUIDistanceFieldFontKerningPair() {}
	FLexUIDistanceFieldFontKerningPair(const TCHAR& InLeftCharCode, const TCHAR& InRightCharCode)
	{
		this->LeftCharCode = InLeftCharCode;
		this->RightCharCode = InRightCharCode;
	}
	TCHAR LeftCharCode = 0;
	TCHAR RightCharCode = 0;
	bool operator==(const FLexUIDistanceFieldFontKerningPair& other)const
	{
		return this->LeftCharCode == other.LeftCharCode && this->RightCharCode == other.RightCharCode;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FLexUIDistanceFieldFontKerningPair& other)
	{
		return HashCombine(GetTypeHash(other.LeftCharCode), GetTypeHash(other.RightCharCode));
	}
};

/** SDF(Signed Distance Field) Font asset for UIText to render smooth scaled sdf font. */
UCLASS(BlueprintType)
class LGUI_API ULexUIFontData_DistanceField : public ULexUIFontData_FreeTypeRender
{
	GENERATED_BODY()
public:
	ULexUIFontData_DistanceField();
private:
	/** Use these material to render SDF font for UIText, include clip material. */
	UPROPERTY(EditAnywhere, Category = "LGUI SDF Font")
		TObjectPtr<UMaterialInterface> SDFDefaultMaterial;
	/** Font size when render glyph. */
	UPROPERTY(EditAnywhere, Category = "LGUI SDF Font", meta = (UIMin = "16", UIMax = "100"))
		int FontSize = 64;
	/** The radius of the distance field in pixels. Will automatically set to a quarter of FontSize since LGUI 3.4.11. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI SDF Font")
		int SDFRadius = 16;
	/** Angle of italic style in degree */
	UPROPERTY(EditAnywhere, Category = "LGUI SDF Font")
		float ItalicAngle = 15.0f;
	/**
	 * bold size radio for bold style, large number create more bold effect.
	 * this parameter is related with SDFRadius & FontSize, smaller SDFRadius & FontSize will need larger BoldRatio to render.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI SDF Font", meta = (UIMin = "0.0", UIMax = "1.0"))
		float BoldRatio = 0.1f;
	/** -1 means not set yet. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI SDF Font", Transient)
		int LineHeight = -1;
	/** -1 means not set yet. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI SDF Font", Transient)
		int VerticalOffset = -1;

public:
	//Begin ULGUIFontDataBaseObject interface
	virtual UMaterialInterface* GetFontMaterial()override;
	virtual void PushCharData(
		TCHAR charCode, const FVector2f& lineOffset, const FVector2f& fontSpace, const FLexUICharData_HighPrecision& charData,
		const LexUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FLexUIOriginVertexData>& originVertices, TArray<FLexUIMeshVertex>& vertices, TArray<FLexUIMeshIndexBufferType>& triangleIndices
	)override;
	virtual void PrepareForPushCharData(UUIText* InText)override;
	virtual bool GetRequireNormalAndTangent()override;
	virtual float GetKerning(const TCHAR& leftCharIndex, const TCHAR& rightCharIndex, const float& charSize) override;
	virtual float GetLineHeight(const float& fontSize) override;
	virtual float GetVerticalOffset(const float& fontSize) override;
	virtual bool GetShouldAffectByPixelPerfect() override{ return false; }
	virtual bool GetNeedObjectScale() override{ return true; }//sdf font need scale value in material
	//End ULGUIFontDataBaseObject interface
protected:
	float italicSlop = 0.0f; float oneDivideFontSize = 1.0f; float objectScale = 1.0f;
	TMap<TCHAR, FLexUICharData> charDataMap;
	TMap<FLexUIDistanceFieldFontKerningPair, int16> KerningPairsMap;
	virtual UTexture2D* CreateFontTexture(int InTextureSize)override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(const TCHAR& charCode, const float& charSize, FLexUICharData_HighPrecision& OutResult)override;
	virtual void AddCharDataToCache(const TCHAR& charCode, const float& charSize, const FLexUICharData& charData)override;
	virtual void ScaleDownUVofCachedChars()override;
	virtual bool RenderGlyph(const TCHAR& charCode, const float& charSize, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	//SDF font already have space between glyphs
	virtual int32 Get_SPACE_NEED_EXPEND()const override { return 0; };
	virtual int32 Get_SPACE_BETWEEN_GLYPH()const override { return 0; };
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
	virtual void PostInitProperties()override;
};
