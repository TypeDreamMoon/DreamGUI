// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "DreamUIFontData_BaseObject.generated.h"


struct FDreamUICharData
{
	float Width = 0;
	float Height = 0;
	float XOffset = 0;
	float YOffset = 0;
	float XAdvance = 0;
	/** Zero for an entry with no quad (a space, a pending or missing glyph): TVector2's default constructor leaves its members unset. */
	FVector2f MinUV = FVector2f::ZeroVector;
	FVector2f MaxUV = FVector2f::ZeroVector;
	int32 SliceIndex = 0;//texture index in Texture2DArray
	/** Which face of the family the glyph came from: 0 is the font itself, then its fallbacks. */
	int32 FaceIndex = 0;
	/**
	 * The glyph's index in that face, 0 being its .notdef: with FaceIndex, what a coverage glyph is fetched by
	 * (UDreamUIFontData_BaseObject::GetCoverageGlyph). Set on every entry a font hands out, pending ones included.
	 */
	uint32 GlyphIndex = 0;
	/** The glyph is being rasterized off-thread: advance is right, the quad is empty until the font's OnGlyphsReady. */
	bool bPending = false;
	/**
	 * The quad samples a colour bitmap -- an emoji from a CBDT/sbix strike or COLRv0 layers, premultiplied BGRA -- not a
	 * field: the painter writes DreamTextQuadCode::ColorFace / ColorEffects for it and ColorTexelsPerEm in UV3.y. The quad
	 * is the glyph's whole padded cell, the padding (0,0,0,0), so an underlay sampled at UV minus an offset stays inside it.
	 */
	bool bColor = false;
	/** A colour glyph's texels per em in its cell: what the shader turns the underlay offset (in em) into UV with. 0 otherwise. */
	float ColorTexelsPerEm = 0.0f;

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

/** What a face is by design, as opposed to what the layout asks of it. */
enum class EDreamUIFontFaceStyle : uint8
{
	None = 0,
	Bold = 1 << 0,
	Italic = 1 << 1,
};
ENUM_CLASS_FLAGS(EDreamUIFontFaceStyle);

/** Synthetic styles baked into a coverage glyph's raster. Part of its cache key. */
enum class EDreamUICoverageGlyphFlags : uint8
{
	None = 0,
	/** Emboldened by the font's bold ratio times the size: FreeType keeps the left and bottom edges and grows the glyph right and up by that much. */
	SyntheticBold = 1 << 0,
	/** Sheared about the baseline by the font's italic slope, after hinting. */
	SyntheticItalic = 1 << 1,
};
ENUM_CLASS_FLAGS(EDreamUICoverageGlyphFlags);

/**
 * A coverage glyph in a font's atlas, for small text: the glyph hinted for its pixel size and rasterized as 8-bit
 * coverage four times, the outline moved right by 0, 1/4, 2/4 and 3/4 px, into the four channels of one cell -- bytes
 * B, G, R, A hold phases 0, 1, 2, 3, so sampled as float4(R, G, B, A) phase p is component 2, 1, 0, 3. The four share
 * one box, their union. Lengths are device pixels at the size asked for, measured from the pen's pixel -- its whole
 * column and its baseline row -- since the painter places the box on whole device pixels and samples its texels 1:1.
 */
struct FDreamUICoverageGlyph
{
	/** Left edge of the box, in pixels right of the pen's column; may be negative. */
	int32 BitmapLeft = 0;
	/** Top edge of the box, in pixels above the baseline row. Row 0 of the bitmap is its top row. */
	int32 BitmapTop = 0;
	/** The box in pixels, which is the box in texels. */
	int32 Width = 0;
	int32 Height = 0;
	/** The box's texels exactly: MinUV is its top-left corner, MaxUV its bottom-right (V down, as in FDreamUICharData). */
	FVector2f MinUV = FVector2f::ZeroVector;
	FVector2f MaxUV = FVector2f::ZeroVector;
	/** Slice of the font's Texture2DArray. */
	int32 SliceIndex = 0;
	/** Asked for and not in the atlas yet: draw the field quad this time; the font's OnCoverageGlyphsChanged says when it landed. */
	bool bPending = false;
};

/** Who a face is beyond one font's face indices: what a cache shared by every font -- the shape cache -- keys runs by. */
struct FDreamUIFontFaceIdentity
{
	/** The font asset whose own face (its face 0) this is: a fallback's or a style face's is that font, not the one asking. */
	FObjectKey Owner;
	/** That asset's face epoch, moved on every time its face is (re)initialized: a reload, a new file, a culture font swap. */
	uint32 Epoch = 0;

	/** False for a face nobody can name: nothing is cached for it. */
	bool IsValid() const { return Owner != FObjectKey(); }
	bool operator==(const FDreamUIFontFaceIdentity& Other) const { return Owner == Other.Owner && Epoch == Other.Epoch; }
	friend uint32 GetTypeHash(const FDreamUIFontFaceIdentity& Identity) { return HashCombineFast(GetTypeHash(Identity.Owner), ::GetTypeHash(Identity.Epoch)); }
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
	/**
	 * Texels per em at the size the atlas was rasterized at; 0 when not applicable. Negative when the shader's small-text
	 * correction is off for this font: the magnitude is still the size (see DreamUIText_ShadeField).
	 */
	virtual float GetAtlasEmTexels() const { return 0.0f; }

	/**
	 * Shaping interface. A font is a list of faces -- its own first, then its fallbacks in lookup
	 * order -- and a shaped glyph names a face and a glyph index rather than a code point. A font
	 * that cannot shape returns null from GetShapingFont, and layout falls back to one glyph per
	 * code point through GetCharData. GetFaceCount counts that list only: a font's own bold, italic
	 * and bold-italic faces have indices past it, reached through GetStyledFace, so a regular run
	 * never falls back to a bold face.
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
	/**
	 * Metrics of one face of the family: 0 is this font, then its fallbacks in lookup order. A line box
	 * has to fit every face the line actually used -- a CJK fallback under a Latin primary has a taller
	 * em, and measuring the line from the primary alone is what pushes its glyphs out of the box.
	 * False when the face does not exist, or when the font has nothing per-face to say, and the caller
	 * then falls back to GetAscent/GetDescent/GetLineHeight.
	 */
	virtual bool GetFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight)
	{
		if (FaceIndex != 0)return false;
		OutAscent = GetAscent(FontSize);
		OutDescent = GetDescent(FontSize);
		OutLineHeight = GetLineHeight(FontSize);
		return true;
	}
	/** The face a run with this style should try first: the font's own bold, italic or bold-italic face when it has one, else 0 (the primary). */
	virtual int32 GetStyledFace(bool bBold, bool bItalic) { return 0; }
	/** Whether a face is itself bold and/or italic, so the layout does not embolden or slant it a second time. */
	virtual EDreamUIFontFaceStyle GetFaceStyleFlags(int32 FaceIndex) { return EDreamUIFontFaceStyle::None; }

	/**
	 * What face resolution (FDreamFontFaceResolver) knows of the regular faces: each fallback entry's ranges, cultures,
	 * scale and preference over the primary, and whether emoji go to colour faces first. Rebuilt by the font when its
	 * fallbacks change, and only then. The base keeps no table: every face takes every code point, in order, at scale 1.
	 */
	virtual const FDreamFontFaceTable& GetFaceTable()
	{
		static const FDreamFontFaceTable EmptyTable{};
		return EmptyTable;
	}
	/**
	 * Whether a face -- regular or style -- is a colour face (FT_HAS_COLOR: CBDT/CBLC, sbix, COLR). Asked face by face as
	 * the resolver reaches it, so a fallback is not loaded before its coverage would be; cached until the faces change.
	 */
	virtual bool IsColorFace(int32 FaceIndex) { return false; }
	/**
	 * The glyph for a code point from a face the caller chose, at CharSize -- that face's .notdef when it lacks the code
	 * point -- for a layout that does not shape and resolves faces itself. The base has one face and answers GetCharData.
	 */
	virtual FDreamUICharData GetFaceCharData(int32 FaceIndex, uint32 CharCode, float CharSize, bool IsBold) { return GetCharData(CharCode, CharSize, IsBold); }
	/** Who a face is, for the shape cache. Invalid, the default, means its runs are never cached. */
	virtual FDreamUIFontFaceIdentity GetFaceIdentity(int32 FaceIndex) { return FDreamUIFontFaceIdentity(); }
	/**
	 * Moves on whenever something changes how this font lays text out that a text's layout input cannot see: fallbacks
	 * edited, vertical metrics or line height type, style faces, a face reloaded. What a layout keeps across edits
	 * (FDreamUITextGeometryCache::SetIncrementalLayout) is checked against it. 0 for a font that never changes.
	 */
	virtual uint32 GetLayoutEpoch() const { return 0; }
	/**
	 * Where the font puts its underline and strikethrough at this size, in pixels: positions are of the line's centre,
	 * measured upwards from the baseline (an underline is negative), thicknesses are full heights. False when the face has no such data.
	 */
	virtual bool GetDecorationMetrics(int32 FaceIndex, float FontSize, float& OutUnderlinePosition, float& OutUnderlineThickness, float& OutStrikethroughPosition, float& OutStrikethroughThickness) { return false; }
	virtual bool GetShouldAffectByPixelPerfect() { return true; }
	virtual bool GetSupportDynamicPixelsPerUnit() { return false; }
	virtual EDreamUIFontTextureMark GetFontTextureMark() { return EDreamUIFontTextureMark::None; }
	virtual float GetBoldRatio() { return 0; }

	/**
	 * Called once before a layout asks for glyphs. The expand size is the one knob a layout has that
	 * changes what a glyph measures (SDF fonts grow their quads by it).
	 */
	virtual void PrepareForLayout(float InExpandMeshSize) {}
	/**
	 * How the painter should build this font's quads. InWorldScale is the text widget's world scale,
	 * InExpandMeshSize the asking text's own expand size -- passed in rather than read off the font,
	 * because a font asset is shared and paint does not run in step with layout: a text whose layout is
	 * still clean would otherwise paint with whichever text laid out last.
	 */
	virtual FDreamTextGlyphPaintStyle GetGlyphPaintStyle(const FVector2f& InWorldScale, float InExpandMeshSize) const { return FDreamTextGlyphPaintStyle(); }

	/**
	 * Small text from coverage glyphs (FDreamUICoverageGlyph). Whether this font can draw small sizes that way at all: a
	 * distance-field font on the outline (multi-channel, BGRA) field whose coverage is on -- its own SmallTextCoverage, or
	 * the project's UDreamGUISettings::bSmallTextCoverage when that is Inherit.
	 */
	virtual bool SupportsCoverageGlyphs() const { return false; }
	/** The most device pixels per em an item may have and still be drawn from coverage: the font's own limit, else the project's. */
	virtual float GetCoverageMaxPixelSize() const { return 0.0f; }
	/**
	 * The coverage glyph for a glyph of a face at a raster size, made on first use: on the spot while the frame's coverage
	 * budget lasts (UDreamUISettings::GetCoverageGlyphSyncBudgetPerFrame), on the rasterizer's worker after that, in which
	 * case it comes back bPending. Game thread only. False when the glyph cannot be drawn from coverage at all -- no
	 * support, a colour or bitmap-only face, a raster that failed -- and the caller draws its field quad and does not wait.
	 * Every glyph handed out goes stale when the font's coverage cells are flushed (their budget ran out, or the whole
	 * atlas was flushed); OnCoverageGlyphsChanged is broadcast then.
	 * @param Size26Dot6  Pixels per em to rasterize at, 26.6 fixed point: round(GlyphSize * RasterScale * 64).
	 * @param Flags       Synthetic styles to bake into the raster.
	 */
	virtual bool GetCoverageGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph) { return false; }


	virtual void AddUIText(UDreamText* InText) {}
	virtual void RemoveUIText(UDreamText* InText) {}

	UDreamUIFontEmojiData* GetEmojiData()const{return EmojiData;}
	const TArray<TObjectPtr<UMaterialInterface>>& GetPresetMaterials()const{return PresetMaterials;}

	static UDreamUIFontData_BaseObject* GetDefaultFont();

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
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
	DECLARE_EVENT(UDreamUIFontData_BaseObject, FDreamUIFontCoverageGlyphsEvent);
	/**
	 * Called on the game thread when coverage glyphs handed out as pending have landed (or failed), and when the font's
	 * coverage cells were flushed. A repaint, never a relayout: a coverage glyph replaces a quad at paint time and changes
	 * no advance, which is why this is not OnGlyphsReady.
	 */
	FDreamUIFontCoverageGlyphsEvent OnCoverageGlyphsChanged;
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UDreamUIFontEmojiData> EmojiData;

	/**
	 * Put materials here so DreamText can easily select OverrideMaterial from this array.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TArray<TObjectPtr<UMaterialInterface>> PresetMaterials;
private:
	/** Listen to EmojiData's changes, once, whichever emoji asset the font holds now. */
	void BindEmojiData();
};
