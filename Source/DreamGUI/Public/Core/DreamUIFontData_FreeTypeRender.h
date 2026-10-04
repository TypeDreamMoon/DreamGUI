// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RHI.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "DreamUISettings.h"
#include "DreamUIFontData_FreeTypeRender.generated.h"

class UDreamText;
class FDreamGlyphRasterizer;
/** A colour glyph out of the rasterizer (Private/Core/Text/DreamGlyphColor.h). */
struct FDreamGlyphColorResult;
/** Render-thread staging textures for partial atlas uploads; defined in DreamUIFontData_FreeTypeRender.cpp. */
struct FDreamUIFontAtlasStagingPool;

#if WITH_FREETYPE
struct FT_GlyphSlotRec_;
struct FT_LibraryRec_;
struct FT_FaceRec_;
struct FT_Bitmap_;
#endif
struct hb_font_t;

/** A glyph of one of a font's faces: the unit the atlas caches and the shaper produces. */
struct FDreamUIGlyphKey
{
	int32 FaceIndex = 0;
	uint32 GlyphIndex = 0;
	FDreamUIGlyphKey() {}
	FDreamUIGlyphKey(int32 InFaceIndex, uint32 InGlyphIndex) : FaceIndex(InFaceIndex), GlyphIndex(InGlyphIndex) {}
	bool operator==(const FDreamUIGlyphKey& Other) const { return FaceIndex == Other.FaceIndex && GlyphIndex == Other.GlyphIndex; }
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUIGlyphKey& Key) { return HashCombine(GetTypeHash(Key.FaceIndex), GetTypeHash(Key.GlyphIndex)); }
};

UENUM(BlueprintType)
enum class EDreamUIDynamicFontDataType :uint8
{
	/** Use custom external font file */
	CustomFontFile,
	/**
	 * Use existing UnrealEngine's font.
	 * Note: if UnrealEngine's font use 'Lazy Load' loading policy, then DreamUI will load target font file by itself.
	 */
	EngineFont,
};

UENUM(BlueprintType)
enum class EDreamUIDynamicFontLineHeightType :uint8
{
	/** Get line height from font face data */
	FromFontFace,
	/** Use font size as line height */
	FontSizeAsLineHeight,
};

/** Which of a face's own vertical metrics its ascent, descent and line spacing are read from. */
UENUM(BlueprintType)
enum class EDreamUIFontVerticalMetrics : uint8
{
	/** FreeType's size metrics: the hhea ascender, descender and line gap, grid-fitted (ascender rounded up, descender down, line spacing to the nearest pixel). What DreamGUI has always used. */
	FreeType UMETA(DisplayName = "FreeType (hhea, grid-fitted)"),
	/** The hhea ascender, descender and line gap, scaled without rounding. What CoreText on macOS lays text out with. */
	Hhea UMETA(DisplayName = "hhea"),
	/** OS/2 sTypoAscender, sTypoDescender and sTypoLineGap, scaled without rounding: the metrics the OpenType spec recommends for line layout. Falls back to hhea when the face has no OS/2 table. */
	Typo UMETA(DisplayName = "OS/2 Typo"),
	/** OS/2 usWinAscent and usWinDescent with no line gap: GDI's line box, tall enough that no glyph is clipped. Falls back to hhea when the face has no OS/2 table. */
	Win UMETA(DisplayName = "OS/2 Win"),
	/**
	 * What DirectWrite reports on Windows, and so what Chrome on Windows lays text out with: the typo metrics when the face sets
	 * OS/2 fsSelection bit 7 (USE_TYPO_METRICS), otherwise usWinAscent and usWinDescent with GDI's external leading as the line
	 * gap, max(0, hhea lineGap - ((usWinAscent + usWinDescent) - (hhea ascender - hhea descender))). The same rule on every
	 * platform DreamGUI runs on. Falls back to hhea when the face has no OS/2 table.
	 */
	Platform UMETA(DisplayName = "Platform (DirectWrite)"),
};

#define ONE_DIVIDE_64 0.015625f //(1.0f / 64.0f)

class UDreamUIFontData_FreeTypeRender;

/**
 * One face a font falls back to for what its own face lacks: which font, for which characters and which languages, at
 * what scale. Entry i of a font's Fallbacks is its face index i + 1, as it always was; the resolver
 * (FDreamFontFaceResolver) decides the order they are tried in from these settings.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIFontFallback
{
	GENERATED_BODY()

	/** The fallback font. Its own face is used -- never its fallbacks or style faces in turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	TObjectPtr<UDreamUIFontData_FreeTypeRender> Font = nullptr;
	/** Code point ranges, inclusive, this face may be used for, matched against a cluster's base; empty means every code point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	TArray<FInt32Interval> Ranges;
	/**
	 * Cultures this face is meant for, semicolon-separated as Slate writes them ("ja", "zh-Hans;zh-Hant"); empty means any
	 * language. Matched against the text's language (UDreamText::Language, a rich-text <lang=xx>) and the names it falls
	 * back to, so "zh" matches a Chinese text of any script.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FString Cultures;
	/**
	 * CSS size-adjust: glyphs from this face are shaped and rasterized at the text's size times this, and the line box is
	 * measured at that size, so a face scaled up grows its lines (as in Chrome; Slate's ScalingFactor does not).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "2.0"))
	float Scale = 1.0f;
	/** Win over the font's own face for the code points in Ranges when the text's language matches Cultures (Slate's sub-fonts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	bool bPreferOverPrimary = false;
};

/**
 * Font asset for UIText to render
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIFontData_FreeTypeRender : public UDreamUIFontData_BaseObject
{
	GENERATED_BODY()
protected:
	friend class FDreamUIFontDataCustomization;

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIDynamicFontDataType FontType = EDreamUIDynamicFontDataType::CustomFontFile;
	/** Font file path, absolute path or relative to ProjectDir */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FString FontFilePath;
	/** Font file use relative path(relative to ProjectDir) or absolute path. After build your game, remember to copy your font file to target path, unless "useExternalFileOrEmbedInToUAsset" is false */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseRelativeFilePath = true;
	/** When in build, use external file or embed into uasset. But in editor, will always load from fontFilePath. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseExternalFileOrEmbedInToUAsset = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<class UFontFace> EngineFont;

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bCultureFont = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition="bCultureFont"))
	TMap<FString, TSoftObjectPtr<class UFontFace>> CultureFontMap;
	void UpdateFontOnCultureChanged();
	FDelegateHandle OnCultureChangedDelegateHandle;

	/** Which face of a font collection (.ttc) to open. Kept across reloads; a file with fewer faces opens its last one. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		int FontFace = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIDynamicFontLineHeightType LineHeightType = EDreamUIDynamicFontLineHeightType::FromFontFace;
	/** Which of each face's own metrics the line box is built from, for this font's own face, its fallbacks and its style faces alike. LineHeightType then applies on top. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIFontVerticalMetrics VerticalMetrics = EDreamUIFontVerticalMetrics::FreeType;
	/** Current using font face has kerning? */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient, AdvancedDisplay)
		bool bHasKerning = false;
	
	/**
	 * when packing char pixel into one single atlas texture, DreamUI will use this size to create a blank Texture2DArray, then insert char pixel.
	*/
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamUIAtlasTextureSizeType TextureSizeType = EDreamUIAtlasTextureSizeType::SIZE_2048x2048;
	/**
	 * rect pack use small cells to pack glyphs, and move to next cell if current cell is full. smaller value get better performance, but leave more garbage area.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	EDreamUIAtlasTextureSizeType RectPackCellSizeType = EDreamUIAtlasTextureSizeType::SIZE_256x256;

	/**
	 * Texture of this font. Kept out of undo: it is the atlas the font made, not something edited, and an undo that put an
	 * older pointer back left the live atlas rooted with nothing pointing at it.
	 */
	UPROPERTY(VisibleAnywhere, NonTransactional, Category = "DreamGUI")
		TObjectPtr<UTexture2DArray> Texture;

	/**
	 * The faces tried for what this font's own face lacks. Entry i is face index i + 1. Which one draws a character is
	 * decided by FDreamFontFaceResolver from each entry's ranges, cultures and preference, and the text's language.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TArray<FDreamUIFontFallback> Fallbacks;
	/**
	 * The fallbacks of an asset saved before they had settings (FDreamGUIObjectVersion::FontFallbackEntries): moved into
	 * Fallbacks, with default settings and in the same order, when the asset loads, and empty from then on. Not edited.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UDreamUIFontData_FreeTypeRender>> FallbackFontArray;
	/**
	 * Clusters asking for emoji presentation (a pictograph that defaults to it, U+FE0F, a flag, a keycap, a skin tone, a
	 * ZWJ sequence) try the colour faces among this font's faces first, as browsers do; off, every cluster takes the faces
	 * in order. A cluster in text presentation always tries the monochrome faces first.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bPreferColorEmoji = true;
	/**
	 * The real bold face of this font. A bold run is drawn from it -- its own outlines and advances -- instead of from this
	 * font's face made bolder. Its primary face is rendered into this font's atlas, the way a fallback's is.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamUIFontData_FreeTypeRender> BoldFont;
	/** The real italic face of this font, used for italic runs instead of slanting this font's face. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamUIFontData_FreeTypeRender> ItalicFont;
	/** The real bold-italic face of this font. Without it a bold-italic run takes BoldFont and is slanted, or ItalicFont and is emboldened. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamUIFontData_FreeTypeRender> BoldItalicFont;

	virtual void FinishDestroy()override;

	/** when draw a rectangle, need to expend 1 pixel to avoid too sharp pixel at edge */
	virtual int32 Get_SPACE_NEED_EXPEND()const { return 1; };
	/** space between glyph in texture */
	virtual int32 Get_SPACE_BETWEEN_GLYPH()const { return 1; };
public:
	//Begin UObject
	virtual void PostLoad()override;
	virtual void BeginDestroy()override;
	/** Moves the FallbackFontArray of an asset saved before FDreamGUIObjectVersion::FontFallbackEntries into Fallbacks. */
	virtual void Serialize(FArchive& Ar)override;
	//End UObject

	//Begin UDreamUIFontData_BaseObject interface
	virtual void InitFont()override;
	virtual UMaterialInterface* GetFontMaterial()override { return nullptr; }
	virtual UTexture2DArray* GetFontTexture()override;
	virtual FDreamUICharData GetCharData(uint32 CharCode, float CharSize, bool IsBold)override;
	virtual bool HasKerning()override;
	virtual int32 GetFaceCount()override;
	/**
	 * The face's cmap has the code point, and what it has can be drawn here: a colour face's glyph that has only colour
	 * data DreamGUI does not draw (COLRv1 paints, SVG) does not count, nor, on a font whose atlas holds no colour (the
	 * single-channel field), one with no outline. A fallback entry's ranges and cultures are the resolver's business.
	 */
	virtual bool FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint)override;
	/** A face drawn from colour bitmap strikes shapes through a font whose advances are the strike's own (E6), not hmtx's. */
	virtual void* GetShapingFont(int32 FaceIndex, float FontSize)override;
	/** Field (or bitmap) glyphs from the atlas; a colour face's colour glyphs from their own cache, by size bucket. */
	virtual FDreamUICharData GetGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize, bool bBold)override;
	/**
	 * Face and glyph index a code point resolves to, through FDreamFontFaceResolver in the game's current language (the
	 * fallbacks' ranges, cultures and preference, colour faces first for emoji); the primary face's .notdef when no face has
	 * it, false only when not even the primary face loads.
	 */
	bool ResolveCodepoint(uint32 Codepoint, FDreamUIGlyphKey& OutKey);
	virtual const FDreamFontFaceTable& GetFaceTable()override;
	/** FT_HAS_COLOR of the face; false for every face of a font whose atlas cannot hold colour (the single-channel field). */
	virtual bool IsColorFace(int32 FaceIndex)override;
	virtual FDreamUICharData GetFaceCharData(int32 FaceIndex, uint32 CharCode, float CharSize, bool IsBold)override;
	virtual FDreamUIFontFaceIdentity GetFaceIdentity(int32 FaceIndex)override;
	/**
	 * This font's own (LayoutEpoch) with the face epoch of every fallback and style font folded in: a layout kept for edits
	 * asked those fonts whether they have a code point, and a reloaded one may answer otherwise. Read off the fonts as they
	 * are; nothing is loaded to answer.
	 */
	virtual uint32 GetLayoutEpoch() const override;
	virtual bool GetCoverageGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, FDreamUICoverageGlyph& OutGlyph)override;
	/** CoverageEpoch while the font supports coverage glyphs (SupportsCoverageGlyphs), else 0. */
	virtual uint32 GetCoverageEpoch() const override;
	virtual void MoveCoverageHold(uint32 InFromEpoch, uint32 InToEpoch) override;
	/**
	 * The atlas as it is now (a font that has made none reports none). Its cells add up -- field, coverage, retired and free
	 * -- to the slices' cells; retired counts every group not given back yet. FaceBytes is this asset's own: its embedded or
	 * loaded file and the worker's shared copy of it (a style face, like a fallback, is a font asset that reports its own).
	 */
	virtual void GetMemoryInfo(FDreamUIFontMemoryInfo& OutInfo) const override;
	virtual float GetKerning(uint32 LeftCharCode, uint32 RightCharCode, float CharSize)override;
	virtual bool GetFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight)override;
	/**
	 * This class's own face metrics, NOT virtual: GetAscent/GetDescent/GetLineHeight go through it so
	 * that a subclass which scales them (the distance-field font does, from its sample size) cannot
	 * have its own scaling applied twice by a virtual call that came back down into it.
	 */
	bool ComputeFaceMetrics(int32 FaceIndex, float FontSize, float& OutAscent, float& OutDescent, float& OutLineHeight);
	/** BoldFont, ItalicFont or BoldItalicFont's index (GetFaceCount() + 0, 1, 2) when the font has that face and it loads, else 0. */
	virtual int32 GetStyledFace(bool bBold, bool bItalic)override;
	/** Bold for BoldFont's index, Italic for ItalicFont's, both for BoldItalicFont's; None for this font's own face and its fallbacks. */
	virtual EDreamUIFontFaceStyle GetFaceStyleFlags(int32 FaceIndex)override;
	/**
	 * From the face's tables, scaled linearly to FontSize: the underline from 'post' (FreeType's underline_position, which
	 * is already the stroke's centre), the strikethrough from OS/2 (yStrikeoutPosition is the stroke's top). A face with no
	 * OS/2 strikeout takes Slate's: the underline's thickness, placed where Slate draws it. False for a face that is not
	 * scalable or has no underline data.
	 */
	virtual bool GetDecorationMetrics(int32 FaceIndex, float FontSize, float& OutUnderlinePosition, float& OutUnderlineThickness, float& OutStrikethroughPosition, float& OutStrikethroughThickness)override;
protected:
	/**
	 * Face metrics, per face and per size. Reading them costs an FT size request and a metrics read,
	 * and a line asks for every face on it every time it is laid out; a fallback face used to pay that
	 * on every miss. Dropped whenever the font is reloaded, since that is when the faces change.
	 */
	struct FFaceMetricsKey
	{
		int32 FaceIndex = 0;
		float FontSize = 0.0f;
		bool operator==(const FFaceMetricsKey& Other) const { return FaceIndex == Other.FaceIndex && FontSize == Other.FontSize; }
		friend FORCEINLINE uint32 GetTypeHash(const FFaceMetricsKey& Key) { return HashCombine(::GetTypeHash(Key.FaceIndex), ::GetTypeHash(Key.FontSize)); }
	};
	struct FFaceMetricsValue
	{
		float Ascent = 0.0f;
		float Descent = 0.0f;
		float LineHeight = 0.0f;
	};
	TMap<FFaceMetricsKey, FFaceMetricsValue> FaceMetricsCache;
	/** A face's underline and strikethrough in em, which scale linearly; dropped with the face metrics. */
	struct FFaceDecorationMetrics
	{
		bool bValid = false;
		float UnderlinePosition = 0.0f;
		float UnderlineThickness = 0.0f;
		float StrikethroughPosition = 0.0f;
		float StrikethroughThickness = 0.0f;
	};
	TMap<int32, FFaceDecorationMetrics> FaceDecorationCache;
	/** The font asset whose primary face a face index names: this font, a fallback, or a style face. Null when the slot is empty or names this font again. */
	UDreamUIFontData_FreeTypeRender* GetFaceOwner(int32 FaceIndex);
	/**
	 * What has to go when the faces behind the indices change (fallbacks or style faces swapped): the worker, which opened
	 * the old files by index; the glyphs it was still making; the face metrics; and the glyph cache. Every text using the
	 * font is told, so it lays out again against the new faces.
	 */
	void ResetFaceState();
	/** What follows a new fallback list (SetFallbacks, an edit): the faces reset, the table rebuilt, the layout epoch moved on, the texts laid out again. */
	void ApplyFallbacksChanged();
	/** Lay out every text using the font again, and its widget's layout with it: a line box of the font may have changed. */
	void RecreateTexts();
	/** GetFaceTable's answer, built from Fallbacks and bPreferColorEmoji; rebuilt on the next ask once they changed. */
	FDreamFontFaceTable FaceTable;
	bool bFaceTableDirty = true;
	/**
	 * FaceHasCodepoint's answers -- the resolver asks them face after face for every cluster of every layout -- asked of
	 * FreeType once per code point and face: bit i of Known says face i was asked, bit i of Has what it answered. Faces past
	 * 63 are asked every time. Kept for the faces as they are now: dropped when the faces behind the indices change, and
	 * when the font a face belongs to reloads (CodepointFacesIdentities).
	 */
	struct FCodepointFaces
	{
		uint64 Known = 0;
		uint64 Has = 0;
	};
	TMap<uint32, FCodepointFaces> CodepointFaces;
	/**
	 * Who each face was when its answers were kept (GetFaceIdentity): the font and its face epoch; invalid for a face none
	 * were kept for. The font as well as the epoch -- two fonts opened as often have the same epoch number, so a fallback
	 * put back in place by an undo, which resets nothing, passed a check of the number alone with the other font's answers.
	 */
	TArray<FDreamUIFontFaceIdentity> CodepointFacesIdentities;
	/** Forget what the faces were found to hold: their code points, and their glyphs' colour kinds (ColorGlyphInfos). */
	void ResetCodepointFaces();
	/** FDreamUIFontFaceIdentity::Epoch of this font's own face: moves on every time the face is initialized or torn down. */
	uint32 FaceEpoch = 0;
	/** This font's own part of what GetLayoutEpoch answers; the fallback and style fonts' face epochs are folded in there. */
	uint32 LayoutEpoch = 0;
public:
	virtual float GetLineHeight(float FontSize)override;
	virtual float GetVerticalOffset(float FontSize)override;
	virtual float GetAscent(float FontSize)override;
	virtual float GetDescent(float FontSize)override;
	virtual float GetFontSizeLimit()override { return 200.0f; }//limit font size to 200. too large font size will result in extreme large texture

	virtual void AddUIText(UDreamText* InText)override;
	virtual void RemoveUIText(UDreamText* InText)override;
	//End UDreamUIFontData_BaseObject interface

	/** Upload every font atlas slice dirtied while UI geometry was generated this frame. */
	static void FlushPendingFontTextures();

	void SetFontType(EDreamUIDynamicFontDataType Value);
	void SetEngineFont(UFontFace* Value);
	/** Point the font at a file -- absolute, or relative to the project directory -- and reload it on next use. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFontFilePath(const FString& InPath, bool bInRelativeToProjectDir);
	/** Replace the fallback list with these fonts, each an entry with default settings (every code point, any language, scale 1). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFallbackFonts(const TArray<UDreamUIFontData_FreeTypeRender*>& InFallbacks);
	/**
	 * Replace the fallback list. Entries with no font, or with this font, are dropped. Resets what the faces behind the
	 * indices fed (the worker, face metrics, glyph cache, face table), moves the layout epoch on, and lays out every text
	 * using the font again.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFallbacks(const TArray<FDreamUIFontFallback>& InFallbacks);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const TArray<FDreamUIFontFallback>& GetFallbacks() const { return Fallbacks; }
	/** Replace the real bold, italic and bold-italic faces; null for a style the font should synthesize. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetStyleFonts(UDreamUIFontData_FreeTypeRender* InBold, UDreamUIFontData_FreeTypeRender* InItalic, UDreamUIFontData_FreeTypeRender* InBoldItalic);
	/** Choose which of each face's own metrics the line box is built from; texts using the font lay out again. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVerticalMetrics(EDreamUIFontVerticalMetrics InVerticalMetrics);
protected:
	/** Collection of UIText which use this font to render. Out of undo, as the atlas is: an undo of a font edit unregisters no text. */
	UPROPERTY(VisibleAnywhere, Transient, NonTransactional, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamText>> RenderTextArray;

	friend class FDreamUIFontData_FreeTypeRenderCustomization;
	/** save data when useExternalFileOrEmbedInToUAsset=false */
	UPROPERTY()
		TArray<uint8> FontBinaryArray;
	/** temp array for storing font binary data, because freetype need to load font from it so we need keep it alive */
	TArray<uint8> TempFontBinaryArray;
	/**
	 * This font file's bytes as one immutable buffer, shared by every rasterizer that reads this face:
	 * this font's own worker and the worker of every font that lists this one as a fallback. Taken once
	 * rather than pointing straight at FontBinaryArray, because a reload refills that array while a
	 * worker may still be reading it; dropped on reload so the next worker takes the new bytes and the
	 * running one keeps the old buffer alive through its own reference.
	 */
	TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> SharedFaceBytes;
	/** The shared buffer above, creating it on first use; null when this font has no bytes anywhere. */
	TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> GetOrCreateSharedFaceBytes();

	/**
	 * Rect packing. The atlas is cut into square cells (RectPackCellSizeType on a side), handed out one at a time from a
	 * pool to two packers, each packing into a cell of its own: the field packer -- field, bitmap and colour glyphs, which
	 * stay until the whole atlas is flushed -- and the coverage packer -- small-text coverage glyphs, which are flushed as a
	 * group when they outgrow their budget and give their cells back to the pool. A new slice only adds its cells to the
	 * pool: it never resets either packer.
	 */
	struct FAtlasCell
	{
		int32 Slice = 0;
		/** The cell's top-left texel. */
		int32 X = 0;
		int32 Y = 0;
	};
	struct FAtlasPacker
	{
		rbp::MaxRectsBinPack Bin;
		/** The cell Bin packs into; nothing while bHasCell is false. */
		FAtlasCell Cell;
		bool bHasCell = false;
	};
	/** Cells no packer holds, the next one taken last in the array. */
	TArray<FAtlasCell> FreeAtlasCells;
	FAtlasPacker FieldPacker;
	FAtlasPacker CoveragePacker;
	/** One warning per font for a glyph larger than a cell, which no packer can place. */
	bool bLoggedGlyphLargerThanCell = false;
	/** 1.0 / textureSize */
	float OneDivideTextureSize;

#if WITH_FREETYPE
	FT_LibraryRec_* Library = nullptr;
	FT_FaceRec_* Face = nullptr;
	void InitFreeType();
	void DeinitFreeType();
	/** Loads and rasterizes one glyph of a face at CharSize, synthetic bold by BoldSize pixels. */
	FT_GlyphSlotRec_* RenderGlyphOnFreeType(FT_FaceRec_* InFace, uint32 GlyphIndex, float CharSize, float BoldSize);
public:
	/**
	 * One row of a rasterized glyph as 8-bit coverage. FreeType's rows are `pitch` bytes apart, not
	 * `width`, and the pitch is signed -- negative means the rows run bottom-up. The pixel mode is not
	 * always 8-bit grey either: an embedded bitmap strike, which CJK fonts carry at small sizes, comes
	 * back 1 bit per pixel. Reading buffer[Row * width + x] was none of those things.
	 * Writes InCount bytes; anything past the glyph's width is zero.
	 */
	static void ReadGlyphRow(const FT_Bitmap_& InBitmap, int32 InRow, uint8* OutCoverage, int32 InCount);
	/** The FreeType face behind a face index (this font or a fallback), initializing it on demand; null when missing. */
	FT_FaceRec_* GetFreeTypeFace(int32 FaceIndex);
protected:
#endif
	/** The shaping font over Face; null when HarfBuzz is not compiled in or the face failed to load. */
	hb_font_t* HarfBuzzFont = nullptr;
	/**
	 * For a face drawn from colour bitmap strikes (CBDT/CBLC, sbix): a sub-font of HarfBuzzFont whose advances are the
	 * strike's own -- its horiAdvance times the size over its ppem, what the bitmap was drawn to and what browsers advance
	 * by -- instead of hmtx's, so the shaper needs no case of its own. Null for every other face.
	 */
	hb_font_t* HarfBuzzStrikeFont = nullptr;
	/** The strike font's advances in 26.6, by glyph and scale: each one costs a strike glyph load. */
	TMap<uint64, int32> StrikeAdvanceCache;
	/** HarfBuzzStrikeFont's h_advance callback (hb_font_get_glyph_h_advance_func_t); the font data is this font. */
	static int32 GetHarfBuzzStrikeAdvance(hb_font_t* InFont, void* InFontData, uint32 InGlyph, void* InUserData);
	void InitHarfBuzz();
	void DeinitHarfBuzz();
#if WITH_FREETYPE

#if WITH_EDITOR
	TArray<FString> CacheSubFaces(FT_LibraryRec_* InFTLibrary, const TArray<uint8>& InMemory);
#endif
#endif
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI", AdvancedDisplay)
		TArray<FString> SubFaces;
#endif
	bool bAlreadyInitialized = false;
	/** An InitFreeType that failed; cleared by DeinitFreeType so a reload retries. Stops per-glyph retries. */
	bool bInitFailed = false;

	struct FGlyphBitmap
	{
		float width, height, hOffset, vOffset, hAdvance;
		TArray<unsigned char> buffer;
		/** single pixel data size in byte, eg: RGBA8-4 A8-1 */
		int pixelSize;
	};
	/**
	 * A rectangle of the atlas for one of the packers: from its cell, from the pool's next cell when that one is full, from
	 * a new slice when the pool is empty. Past the slice budget the field packer asks for the atlas flush
	 * (RequestAtlasFlush) and at the RHI's limit flushes on the spot; the coverage packer takes cells past its own budget,
	 * asking for the coverage flush (RequestCoverageFlush), and fails at the RHI's limit. False when the rectangle is
	 * larger than a cell or there is no room left.
	 */
	bool PackAtlasRect(bool bCoverage, int32 InWidth, int32 InHeight, int32& OutSlice, int32& OutX, int32& OutY);
	/** Hand a packer the pool's next cell. A coverage cell counts against the coverage budget. */
	void TakeAtlasCell(FAtlasPacker& InOutPacker, bool bCoverage);
	/** Grow the atlas by a slice, and put its cells in the pool. */
	void AddAtlasSlice();
	/** Put a slice's cells in the pool, in the order the packer has always used them: column by column from the top left. */
	void AddAtlasSliceCells(int32 Slice);
	/** No cells, no packer holding one, no coverage glyph or cell: what releasing or flushing the atlas starts from. */
	void ResetAtlasPacking();
	/** Forget every glyph the atlas holds: the subclass's cache (ClearCharDataCache), the colour glyphs, the coverage glyphs. */
	void ClearAtlasCaches();
	bool UpdateFontTextureRegion(uint32 PosX, uint32 PosY, uint32 Slice, uint32 Width, uint32 Height, uint32 SrcPitch, uint32 SrcBpp, const TArray<uint8>& SrcData);
	bool FlushFontTexture();
	bool EnsureFontTextureAtlasData(int32 SliceCount, int32 BytesPerPixel);
	void ReleaseFontTexture();
	void RenewFontTexture();
public:
	/**
	 * Throws the glyph atlas away and starts it again at one slice: the cache is cleared, the packer is
	 * reset, and every text using this font is told its atlas changed so it re-lays-out. This is how
	 * the atlas is bounded -- a rect-packed atlas has no way to free one glyph for another of a
	 * different size, so growth is capped and the whole thing is refilled, as Slate's font cache does.
	 */
	void FlushGlyphAtlas();
protected:
	/**
	 * A full atlas is not flushed in the middle of a frame's layouts: every glyph handed out earlier in the frame -- the
	 * first glyphs of the text being laid out, every text laid out before it -- would point into an atlas that no longer
	 * holds it until that text laid out again. The atlas grows one slice past its budget instead and asks for a flush,
	 * which happens before the next frame hands out its first glyph (GetGlyphData).
	 */
	bool bAtlasFlushRequested = false;
	uint64 AtlasFlushRequestFrame = 0;
	/**
	 * From a flush until the font has gone a frame with nothing on the worker: the glyphs the screen needs are coming back.
	 * Running out of room in that time means they need more than the budget, and flushing again would only throw them
	 * away to make them again the next frame, every frame. The threshold is raised to what they need instead, as Slate's
	 * font atlas does, until the next flush.
	 */
	bool bAtlasRefilling = false;
	uint64 AtlasFlushFrame = 0;
	/** The slice count at which a full atlas asks for a flush; 0 means the setting's (UDreamUISettings::MaxFontAtlasSlices). */
	int32 AtlasSliceThreshold = 0;
	/** One warning per font when the threshold had to be raised. */
	bool bLoggedAtlasThresholdRaise = false;
	int32 GetAtlasSliceThreshold() const;
	/** Out of room at the threshold: ask for a flush, or raise the threshold while the atlas is still refilling. */
	void RequestAtlasFlush(int32 InSliceCount);
	bool CopyFontTextureAtlasData(void* DestData, int64 DataSize) const;
	virtual void InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const;

	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)PURE_VIRTUAL(UDreamUIFontData_FreeTypeRender::CreateFontTexture, return nullptr;);

	virtual bool GetCharDataFromCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& OutResult) { return false; };
	virtual void AddCharDataToCache(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FDreamUICharData& CharData) {};
	virtual bool RenderGlyph(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold, FGlyphBitmap& OutResult) { return false; };
	virtual void ClearCharDataCache() {};
	/** How many glyphs the subclass's cache holds (GetCharDataFromCache's), for the memory report. */
	virtual int32 GetCharDataCacheCount() const { return 0; }

	/**
	 * Asynchronous rasterization. A font that can generate its glyphs on a worker (outline fields)
	 * fills in the generator's parameters; a font whose cache does not depend on CharSize says so, so
	 * one request covers every size.
	 */
	virtual bool GetAsyncRasterParams(float CharSize, bool IsBold, float& OutPixelsPerEm, float& OutSpreadPixels, float& OutBoldPixels) const { return false; }
	virtual bool IsGlyphCacheSizeIndependent() const { return false; }
	/** True when bold is a shader-side dilation of the regular glyph: the atlas then holds no bold variant, only the advance changes. */
	virtual bool IsBoldSynthesizedInShader() const { return false; }
	/** Pack a rasterized glyph into the atlas (growing it as needed) and describe its quad. */
	bool InsertGlyphBitmap(const FGlyphBitmap& InGlyphBitmap, FDreamUICharData& OutResult);
	/** A quad-less stand-in with the glyph's real advance, for a glyph still on the worker. */
	FDreamUICharData MakePendingCharData(const FDreamUIGlyphKey& Glyph, float CharSize, bool IsBold);
	struct FAsyncGlyphRequest
	{
		FDreamUIGlyphKey Glyph;
		float CharSize = 0.0f;
		bool bBold = false;
		bool operator==(const FAsyncGlyphRequest& Other) const { return Glyph == Other.Glyph && CharSize == Other.CharSize && bBold == Other.bBold; }
		friend FORCEINLINE uint32 GetTypeHash(const FAsyncGlyphRequest& R) { return HashCombine(HashCombine(GetTypeHash(R.Glyph), GetTypeHash(R.CharSize)), GetTypeHash(R.bBold)); }
	};
	TSet<FAsyncGlyphRequest> PendingAsyncGlyphs;
	/** One warning per font when the worker cannot rasterize, rather than one per glyph per frame. */
	bool bLoggedAsyncGlyphFailure = false;
	TSharedPtr<FDreamGlyphRasterizer, ESPMode::ThreadSafe> Rasterizer;
	/** The worker over this font's faces, created on first use. Null when no face has bytes to share. */
	FDreamGlyphRasterizer* GetOrCreateRasterizer();
	/**
	 * Collect finished worker glyphs into the atlas, by the kind of job: field and colour glyphs that landed (or failed) fire
	 * OnGlyphsReady, a relayout; coverage glyphs only mark OnCoverageGlyphsChanged for the end of FlushPendingFontTextures.
	 */
	void DrainAsyncGlyphs();
	/** Whether a glyph request this frame may still be rasterized synchronously. Colour glyphs share this budget with the field's. */
	static bool TakeSyncGlyphBudget();
	/** The same for coverage glyphs, whose budget is their own (UDreamUISettings::GetCoverageGlyphSyncBudgetPerFrame). */
	static bool TakeSyncCoverageBudget();
	/** Anything of this font's on the worker: field, colour or coverage glyphs. */
	bool HasPendingAsyncGlyphs() const { return PendingAsyncGlyphs.Num() + PendingColorGlyphs.Num() + PendingCoverageGlyphs.Num() > 0; }

	/**
	 * Colour glyphs (emoji): CBDT/CBLC and sbix strikes and COLRv0 layers, rasterized by FDreamGlyphColor at a size bucket
	 * and packed by the field packer into this font's own BGRA atlas. Their cache is their own -- a colour glyph has a
	 * size, a field glyph does not -- and it is cleared with the field glyphs'. Nothing here for a font whose atlas
	 * cannot hold colour.
	 */
	struct FColorGlyphKey
	{
		int32 FaceIndex = 0;
		uint32 GlyphIndex = 0;
		/** FDreamGlyphColor::GetSizeBucket of the size asked for. */
		int32 SizeBucket = 0;
		FColorGlyphKey() {}
		FColorGlyphKey(int32 InFaceIndex, uint32 InGlyphIndex, int32 InSizeBucket) : FaceIndex(InFaceIndex), GlyphIndex(InGlyphIndex), SizeBucket(InSizeBucket) {}
		bool operator==(const FColorGlyphKey& Other) const { return FaceIndex == Other.FaceIndex && GlyphIndex == Other.GlyphIndex && SizeBucket == Other.SizeBucket; }
		friend FORCEINLINE uint32 GetTypeHash(const FColorGlyphKey& Key) { return HashCombine(HashCombine(::GetTypeHash(Key.FaceIndex), ::GetTypeHash(Key.GlyphIndex)), ::GetTypeHash(Key.SizeBucket)); }
	};
	struct FColorGlyphEntry
	{
		/** The quad, the advance and the UVs, every length in texels of the stored bitmap. No quad for a glyph that failed. */
		FDreamUICharData Texels;
		/** Texels per em of the stored bitmap: what turns those lengths into pixels at a size. */
		float TexelsPerEm = 0.0f;
	};
	TMap<FColorGlyphKey, FColorGlyphEntry> ColorGlyphs;
	TSet<FColorGlyphKey> PendingColorGlyphs;
	/** One warning per font for a colour glyph that could not be made. */
	bool bLoggedColorGlyphFailure = false;
	/** What colour data a colour face's glyph holds, asked of FreeType once. */
	struct FColorGlyphInfo
	{
		/** Its EDreamGlyphColorKind, once bKindKnown. */
		uint8 Kind = 0;
		bool bKindKnown = false;
	};
	TMap<FDreamUIGlyphKey, FColorGlyphInfo> ColorGlyphInfos;
	/** Whether this font's atlas can hold colour glyphs: BGRA, which the single-channel field is not. */
	virtual bool CanHoldColorGlyphs() const { return true; }
	/** How far outside a colour glyph anything drawn from it samples, in em: the underlay's reach. The colour cell is padded by it. */
	virtual float GetColorGlyphReachEm() const { return 0.0f; }
	/** The colour kind of a glyph of a colour face (an EDreamGlyphColorKind), asked of FreeType once. */
	uint8 GetGlyphColorKind(int32 FaceIndex, uint32 GlyphIndex);
	/** A colour glyph at CharSize: from the cache, from the worker (pending), or rasterized on the spot. */
	FDreamUICharData GetColorGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize);
	/** Pack a rasterized colour glyph with a ring of transparent texels around it, and describe it. */
	bool InsertColorGlyph(const FDreamGlyphColorResult& InColor, FColorGlyphEntry& OutEntry);
	/** A colour glyph that could not be made: its advance, no quad, so it is not tried again before the atlas is flushed. */
	FColorGlyphEntry MakeFailedColorGlyph(int32 FaceIndex, uint32 GlyphIndex, int32 SizeBucket);
	/** A glyph's advance from the face itself, for a glyph that has no quad: a strike face's strike advance, else hmtx. */
	float GetUnrasterizedAdvance(int32 FaceIndex, uint32 GlyphIndex, float CharSize);

	/**
	 * Coverage glyphs (small text, FDreamUICoverageGlyph): rasterized by FDreamGlyphCoverage, packed by the coverage packer
	 * into cells borrowed from the pool, at most UDreamUISettings::GetMaxCoverageCells() of them. Needing more asks for a
	 * coverage flush, which happens at the end of FlushPendingFontTextures -- after that frame's uploads were queued, so
	 * that frame still draws from the old cells -- and drops every coverage glyph. The field glyphs are never touched by it.
	 * Needing more while the glyphs of the last flush are still coming back raises the threshold instead (bCoverageRefilling).
	 *
	 * The glyphs handed out between two flushes are one epoch (CoverageEpoch), and a text holds the epoch it last painted
	 * coverage from (MoveCoverageHold). A flush retires its epoch's cells as a group: they go back to the pool, zeroed, the
	 * first time anything is packed once the frame of the flush has passed and no text holds the epoch -- a text in a world
	 * that draws without ticking keeps drawing from them. While live and retired cells together are more than twice the
	 * budget, a new coverage glyph is not made (it comes back pending, and the texts are told when cells came back).
	 */
	struct FCoverageGlyphKey
	{
		int32 FaceIndex = 0;
		uint32 GlyphIndex = 0;
		int32 Size26Dot6 = 0;
		uint8 Flags = 0;
		/**
		 * The EDreamUICoverageHinting the raster is made with: the font's, None for an Unhinted glyph. An edit reloads the font,
		 * but a glyph hinted one way is still not the other's.
		 */
		uint8 Hinting = 0;
		FCoverageGlyphKey() {}
		FCoverageGlyphKey(int32 InFaceIndex, uint32 InGlyphIndex, int32 InSize26Dot6, uint8 InFlags, uint8 InHinting)
			: FaceIndex(InFaceIndex), GlyphIndex(InGlyphIndex), Size26Dot6(InSize26Dot6), Flags(InFlags), Hinting(InHinting) {}
		bool operator==(const FCoverageGlyphKey& Other) const
		{
			return FaceIndex == Other.FaceIndex && GlyphIndex == Other.GlyphIndex && Size26Dot6 == Other.Size26Dot6 && Flags == Other.Flags && Hinting == Other.Hinting;
		}
		friend FORCEINLINE uint32 GetTypeHash(const FCoverageGlyphKey& Key)
		{
			return HashCombine(HashCombine(::GetTypeHash(Key.FaceIndex), ::GetTypeHash(Key.GlyphIndex)), HashCombine(::GetTypeHash(Key.Size26Dot6), ::GetTypeHash((uint32)Key.Flags | ((uint32)Key.Hinting << 8))));
		}
	};
	struct FCoverageGlyphEntry
	{
		FDreamUICoverageGlyph Glyph;
		/** Never from coverage: the face draws in colour or has no outlines, the raster failed, or it did not fit. */
		bool bFailed = false;
	};
	TMap<FCoverageGlyphKey, FCoverageGlyphEntry> CoverageGlyphs;
	TSet<FCoverageGlyphKey> PendingCoverageGlyphs;
	/**
	 * The key a coverage glyph is cached and queued under, and the synthetic styles it is rasterized with: the font's raster
	 * style (GetCoverageRasterStyle), with the hinter off for an Unhinted glyph -- the key's hinting is the one the raster
	 * is made with, so a glyph back from the worker finds the request it answers.
	 */
	FCoverageGlyphKey MakeCoverageGlyphKey(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags, float& OutBoldEm, float& OutItalicSlope) const;
	/** The cells the coverage packer took since the last coverage flush, the one it packs into included: the current epoch's. */
	TArray<FAtlasCell> CoverageCells;
	/** The cells one coverage flush retired: those of one epoch, and the frame of the flush. */
	struct FRetiredCoverageCells
	{
		uint32 Epoch = 0;
		uint64 Frame = 0;
		TArray<FAtlasCell> Cells;
	};
	/** Retired cells not given back yet, oldest flush first. */
	TArray<FRetiredCoverageCells> RetiredCoverageCells;
	/** The epoch of the coverage glyphs handed out now; never 0. See GetCoverageEpoch. */
	uint32 CoverageEpoch = 1;
	/** How many texts hold each epoch (MoveCoverageHold). An epoch nobody holds has no entry. */
	TMap<uint32, int32> CoverageEpochHolders;
	/** The cell cap kept a coverage glyph from being made since cells last came back: the texts are told once they do. */
	bool bCoverageCapRefused = false;
	/** One warning per font the first time the cell cap keeps a coverage glyph from being made. */
	bool bLoggedCoverageCap = false;
	/** The next epoch, past 0 when the count wraps. */
	void AdvanceCoverageEpoch();
	/** Cells in RetiredCoverageCells, every group together. */
	int32 CountRetiredCoverageCells() const;
	/** Live and retired coverage cells together are more than twice UDreamUISettings::GetMaxCoverageCells(). */
	bool IsCoverageOverCap() const;
	/** The coverage glyphs outgrew their cells: flush them at the end of FlushPendingFontTextures. */
	bool bCoverageFlushRequested = false;
	/** OnCoverageGlyphsChanged is due at the end of FlushPendingFontTextures. */
	bool bCoverageGlyphsChanged = false;
	/** One log line per font the first time its coverage glyphs outgrow their cells. */
	bool bLoggedCoverageFlush = false;
	/**
	 * From a coverage flush until the font has gone a frame with no coverage glyph on the worker: the glyphs the texts paint
	 * with are coming back. Outgrowing the cells in that time means they need more than the budget, and flushing again would
	 * only throw them away to make them again the next frame, every frame. The cell threshold is raised to what they need
	 * instead, up to twice the budget, until the next coverage flush -- as the field atlas's slice threshold is.
	 */
	bool bCoverageRefilling = false;
	uint64 CoverageFlushFrame = 0;
	/** The coverage cell count past which a flush is asked for; 0 means the setting's (UDreamUISettings::GetMaxCoverageCells). */
	int32 CoverageCellThreshold = 0;
	/** One log line per font when the coverage cell threshold had to be raised. */
	bool bLoggedCoverageThresholdRaise = false;
	/**
	 * The coverage raster's style beyond its size: the hinting (an EDreamUICoverageHinting value) and the strengths of the
	 * synthetic styles -- bold in em of stroke growth, the italic slope. Asked only of a font that supports coverage glyphs.
	 */
	virtual void GetCoverageRasterStyle(uint8& OutHinting, float& OutBoldEm, float& OutItalicSlope) const { OutHinting = 0; OutBoldEm = 0.0f; OutItalicSlope = 0.0f; }
	/** Pack four phases of coverage (Width * Height BGRA texels) with a ring of zero texels around them, and describe the glyph. */
	bool InsertCoverageGlyph(int32 InWidth, int32 InHeight, int32 InLeft, int32 InTop, const TArray<uint8>& InPixels, FDreamUICoverageGlyph& OutGlyph);
	void RequestCoverageFlush();
	/**
	 * Drop every coverage glyph, retire their cells as the current epoch's group, and move the epoch on; the end of
	 * FlushPendingFontTextures does it.
	 */
	void FlushCoverageGlyphs();
	/**
	 * Back to the pool, re-initialized and re-uploaded, the cells of every retired group whose flush was in an earlier frame
	 * and whose epoch no text holds. True when any came back.
	 */
	bool ReleaseRetiredCoverageCells();
public:
	/** Block until the worker has finished every queued glyph and put them in the atlas. Tests and teardown. */
	void WaitForAsyncGlyphs();
	/** Glyphs of this font on the worker: field, colour and coverage glyphs together. */
	int32 GetPendingAsyncGlyphCount() const { return PendingAsyncGlyphs.Num() + PendingColorGlyphs.Num() + PendingCoverageGlyphs.Num(); }
	/** Override the per-frame synchronous budgets, the field glyphs' and the coverage glyphs' alike (negative restores the settings). Tests. */
	static void SetAsyncGlyphSyncBudgetOverride(int32 Budget);
	/**
	 * Tests: put a coverage glyph made elsewhere -- a known ramp -- into this font's coverage cache, packed and uploaded
	 * like a rasterized one, so GetCoverageGlyph(FaceIndex, GlyphIndex, Size26Dot6, Flags) answers it. Pixels holds
	 * Width * Height * 4 bytes, rows top first, phases 0..3 in B, G, R, A; Left and Top are FDreamUICoverageGlyph's
	 * BitmapLeft and BitmapTop. False when it does not fit, or the font draws nothing from coverage.
	 */
	bool InjectCoverageGlyphForTesting(int32 FaceIndex, uint32 GlyphIndex, int32 Size26Dot6, EDreamUICoverageGlyphFlags Flags,
		int32 Width, int32 Height, int32 Left, int32 Top, const TArray<uint8>& Pixels);
	/**
	 * Tests: a coverage glyph's texels as the font's CPU copy of the atlas holds them, Width * Height * 4 bytes, rows top
	 * first, phases 0..3 in B, G, R, A -- what FDreamGlyphCoverage made for it. False for an empty or pending glyph, or one
	 * outside the atlas.
	 */
	bool GetCoverageGlyphTexelsForTesting(const FDreamUICoverageGlyph& Glyph, TArray<uint8>& OutPixels) const;
protected:

	/** CPU source of truth used both for deferred uploads and texture-array expansion. */
	TArray<uint8> FontTextureAtlasData;
	/**
	 * What has been written into the atlas since the last upload: per slice, the union of every
	 * region that landed in it.
	 *
	 * A slice used to be marked dirty as a whole, so one new glyph re-uploaded 2048x2048x4 bytes --
	 * 16MB for an MTSDF atlas -- through a full-slice lock. The union keeps the cost proportional to
	 * what changed, and glyphs cluster because the packer fills one 256x256 cell at a time.
	 */
	TMap<int32, FIntRect> DirtyFontTextureSlices;
	int32 FontTextureBytesPerPixel = 0;
	/**
	 * Scratch textures the render thread uses to land partial slice updates.
	 *
	 * Created, used and destroyed only on the render thread; the game thread holds nothing but this
	 * reference and gives it up through a render command, so the RHI references go with it.
	 */
	TSharedPtr<FDreamUIFontAtlasStagingPool, ESPMode::ThreadSafe> AtlasStagingPool;
	/** Note that a rectangle of a slice was written, merging it into that slice's pending region. */
	void MarkAtlasRegionDirty(int32 Slice, const FIntRect& Region);
	/** Hand the staging pool to the render thread so its textures are released there. */
	void ReleaseAtlasStagingPool();
public:
#if WITH_EDITOR
	void ReloadFont();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual void ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
#endif
};
