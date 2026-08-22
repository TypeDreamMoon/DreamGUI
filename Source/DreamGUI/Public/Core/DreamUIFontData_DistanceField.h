// Copyright 2019-present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "DreamUIFontData_DistanceField.generated.h"

struct FDreamUIDistanceFieldCharKey
{
public:
	FDreamUIDistanceFieldCharKey() {}
	FDreamUIDistanceFieldCharKey(uint32 InCharCode, bool InBold)
	{
		this->CharCode = InCharCode;
		this->bBold = InBold;
	}
	uint32 CharCode = 0;
	bool bBold = false;
	bool operator==(const FDreamUIDistanceFieldCharKey& other)const
	{
		return this->CharCode == other.CharCode && this->bBold == other.bBold;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIDistanceFieldCharKey& other)
	{
		return HashCombine(GetTypeHash(other.CharCode), GetTypeHash(other.bBold));
	}
};
struct FDreamUIDistanceFieldFontKerningPair
{
public:
	FDreamUIDistanceFieldFontKerningPair() {}
	FDreamUIDistanceFieldFontKerningPair(const uint32& InLeftCharCode, const uint32& InRightCharCode)
	{
		this->LeftCharCode = InLeftCharCode;
		this->RightCharCode = InRightCharCode;
	}
	uint32 LeftCharCode = 0;
	uint32 RightCharCode = 0;
	bool operator==(const FDreamUIDistanceFieldFontKerningPair& other)const
	{
		return this->LeftCharCode == other.LeftCharCode && this->RightCharCode == other.RightCharCode;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIDistanceFieldFontKerningPair& other)
	{
		return HashCombine(GetTypeHash(other.LeftCharCode), GetTypeHash(other.RightCharCode));
	}
};

/** SDF(Signed Distance Field) Font asset for render smooth scaled sdf text. */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIFontData_DistanceField : public UDreamUIFontData_FreeTypeRender
{
	GENERATED_BODY()
public:
	UDreamUIFontData_DistanceField();
private:
	/** Font size when render glyph. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (UIMin = "16", UIMax = "100"))
		int SampleFontSize = 64;
	/** The radius of the distance field in pixels. Normally just use 1/4 of FontSize */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (UIMin = "1"))
		int SDFRadius = 16;
	/** Angle of italic style in degree */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float ItalicAngle = 15.0f;
	/**
	 * bold size radio for bold style, large number create more bold effect.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (UIMin = "0.0", UIMax = "1.0"))
		float BoldRatio = 0.08f;
	/** -1 means not set yet. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI", Transient)
		int LineHeight = -1;
	/** -1 means not set yet. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI", Transient)
		int VerticalOffset = -1;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI", Transient)
	float AdditionalVerticalOffset = 0.0f;

public:
	//Begin UDreamUIFontData_BaseObject interface
	virtual UMaterialInterface* GetFontMaterial()override;
	virtual void PushCharData(
		uint32 charCode, FVector2f lineOffset, FVector2f fontSpace, const FDreamUICharData& charData,
		const DreamUIRichTextParser::FRichTextParseResult& richTextProperty,
		int verticesStartIndex, int indicesStartIndex,
		int& outAdditionalVerticesCount, int& outAdditionalIndicesCount,
		TArray<FDreamUIOriginVertexData>& originVertices, TArray<FDreamUIMeshVertex>& vertices, TArray<FDreamUIMeshIndex>& triangleIndices
	)override;
	virtual void PrepareForPushCharData(UDreamText* InText)override;
	virtual void PrepareForLayout(float InExpandMeshSize)override;
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale) const override;
	virtual bool GetRequireNormalAndTangent()override;
	virtual float GetKerning(uint32 leftCharIndex, uint32 rightCharIndex, float charSize) override;
	virtual float GetLineHeight(float fontSize) override;
	virtual float GetVerticalOffset(float fontSize) override;
	virtual bool GetShouldAffectByPixelPerfect() override{ return false; }
	virtual bool GetNeedObjectScale() override{ return true; }//sdf font need scale value in material
	virtual EDreamUIFontTextureMark GetFontTextureMark() override{ return EDreamUIFontTextureMark::DistanceField; }
	virtual float GetBoldRatio() override{ return BoldRatio; }
	//End UDreamUIFontData_BaseObject interface
	float GetSampleFontSize()const{return SampleFontSize;}
protected:
	float ItalicSlop = 0.0f; float OneDivideFontSize = 1.0f; float ExpandMeshSize = 0; float ObjectScale = 0;
	TMap<FDreamUIDistanceFieldCharKey, FDreamUICharData> CharDataMap;
	TMap<FDreamUIDistanceFieldFontKerningPair, int16> KerningPairsMap;
	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(uint32 CharCode, float CharSize, bool IsBold, FDreamUICharData& OutResult)override;
	virtual void AddCharDataToCache(uint32 CharCode, float CharSize, bool IsBold, FDreamUICharData& CharData)override;
	virtual bool RenderGlyph(uint32 CharCode, float CharSize, bool IsBold, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	//SDF font already have space between glyphs
	virtual int32 Get_SPACE_NEED_EXPEND()const override { return 0; };
	virtual int32 Get_SPACE_BETWEEN_GLYPH()const override { return 0; };
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	virtual void PostInitProperties()override;
};
