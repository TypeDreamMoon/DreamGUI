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

	/** Texture of this font */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TObjectPtr<UTexture2DArray> Texture;
	int32 CurrentTextureSlice = 0;

	/** if not find char in current font, DreamUI will search the char in this font array until find it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<TObjectPtr<UDreamUIFontData_FreeTypeRender>> FallbackFontArray;
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
	//End UObject

	//Begin UDreamUIFontData_BaseObject interface
	virtual void InitFont()override;
	virtual UMaterialInterface* GetFontMaterial()override { return nullptr; }
	virtual UTexture2DArray* GetFontTexture()override;
	virtual FDreamUICharData GetCharData(uint32 CharCode, float CharSize, bool IsBold)override;
	virtual bool HasKerning()override;
	virtual int32 GetFaceCount()override;
	virtual bool FaceHasCodepoint(int32 FaceIndex, uint32 Codepoint)override;
	virtual void* GetShapingFont(int32 FaceIndex, float FontSize)override;
	virtual FDreamUICharData GetGlyphData(int32 FaceIndex, uint32 GlyphIndex, float CharSize, bool bBold)override;
	/** Face and glyph index a code point resolves to, searching this font then its fallbacks; false when no face has it. */
	bool ResolveCodepoint(uint32 Codepoint, FDreamUIGlyphKey& OutKey);
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
	/** Replace the fallback list: the faces tried, in order, for code points this font lacks. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFallbackFonts(const TArray<UDreamUIFontData_FreeTypeRender*>& InFallbacks);
	/** Replace the real bold, italic and bold-italic faces; null for a style the font should synthesize. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetStyleFonts(UDreamUIFontData_FreeTypeRender* InBold, UDreamUIFontData_FreeTypeRender* InItalic, UDreamUIFontData_FreeTypeRender* InBoldItalic);
	/** Choose which of each face's own metrics the line box is built from; texts using the font lay out again. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVerticalMetrics(EDreamUIFontVerticalMetrics InVerticalMetrics);
protected:
	/** Collection of UIText which use this font to render. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI")
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

	/** for rect packing */
	rbp::MaxRectsBinPack BinPack;
	TArray<rbp::Rect> FreeRectCells;
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
	 * Insert rect into area, assign pixel if succeed
	 * return: if glyph can fit in rect area return true, else false
	 */
	bool PackRectAndInsertChar(const FGlyphBitmap& InGlyphBitmap, rbp::MaxRectsBinPack& InOutBinPack, FDreamUICharData& OutResult);
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
	/** Collect finished worker glyphs into the atlas; fires OnGlyphsReady when any landed. */
	void DrainAsyncGlyphs();
	/** Whether a glyph request this frame may still be rasterized synchronously. */
	static bool TakeSyncGlyphBudget();
public:
	/** Block until the worker has finished every queued glyph and put them in the atlas. Tests and teardown. */
	void WaitForAsyncGlyphs();
	int32 GetPendingAsyncGlyphCount() const { return PendingAsyncGlyphs.Num(); }
	/** Override the per-frame synchronous budget (negative restores the setting). Tests. */
	static void SetAsyncGlyphSyncBudgetOverride(int32 Budget);
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
