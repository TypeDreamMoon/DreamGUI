// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIFontData_BaseObject.generated.h"


struct FDreamUICharData
{
	float Width = 0;
	float Height = 0;
	float XOffset = 0;
	float YOffset = 0;
	float XAdvance = 0;
	FVector2f MinUV;
	FVector2f MaxUV;
	int32 SliceIndex = 0;//texture index in Texture2DArray
	/** The glyph is being rasterized off-thread: advance is right, the quad is empty until the font's OnGlyphsReady. */
	bool bPending = false;

	bool IsValid()const
	{
		return Width > 0 || Height > 0 || XAdvance > 0;
	}

	FVector2f GetUV0()const
	{
		return FVector2f(MinUV.X, MaxUV.Y);
	}
	FVector2f GetUV3()const
	{
		return FVector2f(MaxUV.X, MinUV.Y);
	}
	FVector2f GetUV2()const
	{
		return FVector2f(MinUV.X, MinUV.Y);
	}
	FVector2f GetUV1()const
	{
		return FVector2f(MaxUV.X, MaxUV.Y);
	}
	FVector2f GetUVRange()const
	{
		return FVector2f(MaxUV.X - MinUV.X, MinUV.Y - MaxUV.Y);
	}
};

/** What the font atlas holds, which is what the shader switches on per widget (the FontMark record). */
enum class EDreamUIFontTextureMark : uint8
{
	None = 0, Bitmap = 1, DistanceField = 2,
	/** Multi-channel (RGB) plus true (A) signed distance field, from the glyph outline. */
	Mtsdf = 3,
};

/** How a font wants its glyph quads built: the part of "rendering" that is the font's business, not the painter's. */
struct FDreamTextGlyphPaintStyle
{
	/** tan(italic angle): how far the top edge of an italic quad leans right. */
	float ItalicSlope = 0.0f;
	/**
	 * Distance-field fonts (single-channel from a bitmap, or multi-channel from the outline): the
	 * painter packs layer + dilate into UV2.x and may grow quads into the field for the text style's
	 * effects. Synthetic bold is a dilation too when BoldDilateEm is set; otherwise the atlas bakes it.
	 */
	bool bDistanceField = false;
	/** Texels per em at the atlas's sample size. */
	float EmTexels = 0.0f;
	/** How far from the glyph the field is valid, in texels (the spread). */
	float FieldSpreadTexels = 0.0f;
	/** How much of that spread the layout's glyph quads already include, in texels. */
	float QuadMarginTexels = 0.0f;
	/** One atlas texel in UV units. */
	float TexelToUV = 0.0f;
	/** Synthetic bold as a face dilation, in em per side; 0 when bold is baked into the atlas. */
	float BoldDilateEm = 0.0f;
};

class UTexture2D;
class UTexture2DArray;
class UMaterialInterface;
class UDreamText;
class UDreamUIFontEmojiData;

/**
 * base font class, UIText can use a implemented asset object to render text
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIFontData_BaseObject : public UObject
{
	GENERATED_BODY()
public:
	virtual void InitFont()PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::InitFont, );

	virtual UMaterialInterface* GetFontMaterial()PURE_VIRTUAL(UDreamUIFontData_BaseObject::GetFontMaterial, return nullptr;);
	virtual UTexture2DArray* GetFontTexture()PURE_VIRTUAL(UDreamUIFontData_BaseObject::GetFontTexture, return nullptr;);
	virtual FDreamUICharData GetCharData(uint32 CharCode, float CharSize, bool IsBold) PURE_VIRTUAL(UDreamUIFontData::GetCharData, return FDreamUICharData(););
	virtual bool HasKerning() { return false; }
	/** Distance-field range of the atlas in texels (twice the spread); 0 for atlases that are not fields. */
	virtual float GetAtlasFieldRangeTexels() const { return 0.0f; }
	/** Texels per em at the size the atlas was rasterized at; 0 when not applicable. */
	virtual float GetAtlasEmTexels() const { return 0.0f; }

	/**
	 * Shaping interface. A font is a list of faces -- its own first, then its fallbacks in lookup
	 * order -- and a shaped glyph names a face and a glyph index rather than a code point. A font
	 * that cannot shape returns null from GetShapingFont, and layout falls back to one glyph per
	 * code point through GetCharData.
	 */
	virtual int32 GetFaceCount() { return 1; }
	/** Whether the face has a glyph for the code point: what decides which face a run is shaped with. */
	virtual bool FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint) { return true; }
	/** An hb_font_t* scaled to FontSize (26.6 units), or null. Opaque so HarfBuzz stays out of public headers. */
	virtual void* GetShapingFont(int32 FaceIndex, float FontSize) { return nullptr; }
	/** Atlas entry for a glyph of a face, rasterizing it on first use. */
	virtual FDreamUICharData GetGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize, bool bBold) { return FDreamUICharData(); }
	virtual float GetKerning(uint32 LeftCharIndex, uint32 RightCharIndex, float CharSize) { return 0; }
	virtual float GetLineHeight(float FontSize) { return FontSize; }
	/**
	 * Legacy centre correction: -(ascender + descender) / 2. Layout no longer reads it -- glyphs sit
	 * on a baseline -- but fonts that only know this still yield an ascent and descent through it.
	 */
	virtual float GetVerticalOffset(float FontSize) { return 0; }
	/** Baseline to the top of the font's box, at this size. Lines are stacked from these. */
	virtual float GetAscent(float FontSize) { return GetLineHeight(FontSize) * 0.5f - GetVerticalOffset(FontSize); }
	/** Baseline to the bottom of the font's box, at this size, as a positive distance. */
	virtual float GetDescent(float FontSize) { return GetLineHeight(FontSize) * 0.5f + GetVerticalOffset(FontSize); }
	virtual float GetFontSizeLimit() { return MAX_FLT; }
	virtual bool GetShouldAffectByPixelPerfect() { return true; }
	virtual bool GetSupportDynamicPixelsPerUnit() { return false; }
	virtual EDreamUIFontTextureMark GetFontTextureMark() { return EDreamUIFontTextureMark::None; }
	virtual float GetBoldRatio() { return 0; }

	/**
	 * Called once before a layout asks for glyphs. The expand size is the one knob a layout has that
	 * changes what a glyph measures (SDF fonts grow their quads by it).
	 */
	virtual void PrepareForLayout(float InExpandMeshSize) {}
	/** How the painter should build this font's quads. InWorldScale is the text widget's world scale. */
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale) const { return FDreamTextGlyphPaintStyle(); }


	virtual void AddUIText(UDreamText* InText) {}
	virtual void RemoveUIText(UDreamText* InText) {}

	UDreamUIFontEmojiData* GetEmojiData()const{return EmojiData;}
	const TArray<TObjectPtr<UMaterialInterface>>& GetPresetMaterials()const{return PresetMaterials;}

	static UDreamUIFontData_BaseObject* GetDefaultFont();

	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
#endif

	DECLARE_EVENT(UDreamUIFontData_BaseObject, FDreamUIFontEmojiDataRefreshEvent);
	/** Called when emoji data changed, and need DreamText to refresh. */
	FDreamUIFontEmojiDataRefreshEvent OnEmojiDataChanged;
	DECLARE_EVENT(UDreamUIFontData_BaseObject, FDreamUIFontGlyphsReadyEvent);
	/** Called on the game thread when glyphs that were handed out as pending have landed in the atlas. */
	FDreamUIFontGlyphsReadyEvent OnGlyphsReady;
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UDreamUIFontEmojiData> EmojiData;

	/**
	 * Put materials here so DreamText can easily select OverrideMaterial from this array.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TArray<TObjectPtr<UMaterialInterface>> PresetMaterials;
};
