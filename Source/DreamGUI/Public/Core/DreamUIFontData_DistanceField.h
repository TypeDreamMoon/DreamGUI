// Copyright 2019-present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "DreamUIFontData_DistanceField.generated.h"

struct FDreamUIDistanceFieldCharKey
{
public:
	FDreamUIDistanceFieldCharKey() {}
	FDreamUIDistanceFieldCharKey(const FDreamUIGlyphKey& InGlyph, bool InBold)
	{
		this->Glyph = InGlyph;
		this->bBold = InBold;
	}
	FDreamUIGlyphKey Glyph;
	bool bBold = false;
	bool operator==(const FDreamUIDistanceFieldCharKey& other)const
	{
		return this->Glyph == other.Glyph && this->bBold == other.bBold;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIDistanceFieldCharKey& other)
	{
		return HashCombine(GetTypeHash(other.Glyph), GetTypeHash(other.bBold));
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

/** How the distance field is produced. */
UENUM(BlueprintType)
enum class EDreamUISdfSource : uint8
{
	/**
	 * Multi-channel field from the glyph outline (msdfgen): sharp corners from the median of three
	 * channels, and a true distance in alpha that effects -- blur, glow, shadows, outlines -- widen into.
	 */
	OutlineMultiChannel,
	/** Single-channel field derived from a rasterized bitmap (the original method). Corners round off. */
	BitmapSingleChannel,
};

/** SDF(Signed Distance Field) Font asset for render smooth scaled sdf text. */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIFontData_DistanceField : public UDreamUIFontData_FreeTypeRender
{
	GENERATED_BODY()
public:
	UDreamUIFontData_DistanceField();
private:
	/** Outline multi-channel is the default; bitmap single-channel is what assets made before it get. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamUISdfSource SdfSource = EDreamUISdfSource::OutlineMultiChannel;
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
	/** Ascent and descent at SampleFontSize; negative means not cached yet. */
	float CachedAscent = -1.0f;
	float CachedDescent = -1.0f;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI", Transient)
	float AdditionalVerticalOffset = 0.0f;

public:
	//Begin UDreamUIFontData_BaseObject interface
	virtual UMaterialInterface* GetFontMaterial()override;
	virtual void PrepareForLayout(float InExpandMeshSize)override;
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale) const override;
	virtual bool GetRequireNormalAndTangent()override;
	virtual float GetKerning(uint32 leftCharIndex, uint32 rightCharIndex, float charSize) override;
	virtual float GetLineHeight(float fontSize) override;
	virtual float GetVerticalOffset(float fontSize) override;
	virtual float GetAscent(float fontSize) override;
	virtual float GetDescent(float fontSize) override;
	virtual bool GetShouldAffectByPixelPerfect() override{ return false; }
	virtual bool GetNeedObjectScale() override{ return true; }//sdf font need scale value in material
	virtual EDreamUIFontTextureMark GetFontTextureMark() override{ return SdfSource == EDreamUISdfSource::OutlineMultiChannel ? EDreamUIFontTextureMark::Mtsdf : EDreamUIFontTextureMark::DistanceField; }
	EDreamUISdfSource GetSdfSource() const { return SdfSource; }
	/** The distance range on each side of the edge, in pixels at SampleFontSize. */
	int32 GetSdfRadius() const { return SDFRadius; }
	virtual float GetAtlasFieldRangeTexels() const override { return 2.0f * SDFRadius; }
	virtual float GetAtlasEmTexels() const override { return (float)SampleFontSize; }
	virtual bool GetAsyncRasterParams(float CharSize, bool IsBold, float& OutPixelsPerEm, float& OutSpreadPixels, float& OutBoldPixels) const override
	{
		if (SdfSource != EDreamUISdfSource::OutlineMultiChannel)return false;
		OutPixelsPerEm = (float)SampleFontSize;
		OutSpreadPixels = (float)SDFRadius;
		OutBoldPixels = IsBold ? SampleFontSize * BoldRatio : 0.0f;
		return true;
	}
	virtual bool IsGlyphCacheSizeIndependent() const override { return true; }
	virtual float GetBoldRatio() override{ return BoldRatio; }
	//End UDreamUIFontData_BaseObject interface
	float GetSampleFontSize()const{return SampleFontSize;}
protected:
	float OneDivideFontSize = 1.0f; float ExpandMeshSize = 0;
	TMap<FDreamUIDistanceFieldCharKey, FDreamUICharData> CharDataMap;
	TMap<FDreamUIDistanceFieldFontKerningPair, int16> KerningPairsMap;
	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)override;
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize)override;

	virtual bool GetCharDataFromCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& OutResult)override;
	virtual void AddCharDataToCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& CharData)override;
	virtual bool RenderGlyph(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FGlyphBitmap& OutResult)override;
	virtual void ClearCharDataCache()override;

	//SDF font already have space between glyphs
	virtual int32 Get_SPACE_NEED_EXPEND()const override { return 0; };
	virtual int32 Get_SPACE_BETWEEN_GLYPH()const override { return 0; };
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	virtual void PostInitProperties()override;
	virtual void Serialize(FArchive& Ar)override;
};
