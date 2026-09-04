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

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		int FontFace = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIDynamicFontLineHeightType LineHeightType = EDreamUIDynamicFontLineHeightType::FromFontFace;
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
	FDelegateHandle PackingAtlasTextureExpandDelegateHandle;

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
	bool CopyFontTextureAtlasData(void* DestData, int64 DataSize) const;
	virtual void InitializeFontTextureAtlasSlice(uint8* SliceData, int64 SliceDataSize) const;

	virtual UTexture2DArray* CreateFontTexture(int InTextureSize, int InSliceCount)PURE_VIRTUAL(UDreamUIFontData_FreeTypeRender::CreateFontTexture, return nullptr;);
	virtual void ApplyPackingAtlasTextureExpand(UTexture2D* newTexture, int newTextureSize);

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
