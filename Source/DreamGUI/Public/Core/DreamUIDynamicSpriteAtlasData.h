// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Engine/Texture2D.h"
#include "DreamUIDynamicSpriteAtlasData.generated.h"


class UDreamUISpriteData;
class IDreamUISpriteRenderInterface;

/** Data container for dynamically generated Sprite atlas */
USTRUCT()
struct DREAMGUI_API FDreamUIDynamicSpriteAtlasData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	FName PackingTag;
	/** AtlasTexture is the real texture for render */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI")
	TArray<TObjectPtr<UTexture2D>> AtlasTextureArray;
	/** information needed when insert a Sprite */
	TArray<rbp::MaxRectsBinPack> AtlasBinPackArray;
	/**
	 * Sprites packed into this atlas, in LEAST-RECENTLY-USED ORDER: oldest first, newest last.
	 * TouchSprite is what maintains that, and EvictUnusedSprites walks it from the front.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TObjectPtr<UDreamUISpriteData>> SpriteDataArray;
	/** collection of all objects that use this atlas to render. Object must implement IUISpriteRenderableInterface. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI", AdvancedDisplay)
	TArray<TWeakObjectPtr<UObject>> RenderSpriteArray;

	void EnsureAtlasTexture();
	void CreateAtlasTexture(int InTextureSize);
	/** expand texture array */
	void ExpandAtlasAreaArray();
	void CheckSprite();
	/**
	 * Drop every page this atlas holds and put the sprites that were packed into them back to
	 * "not packed yet", so the next one that is asked for its texture repacks from scratch.
	 * The pages are owned by this entry (no root-set claim), so clearing the array is what frees them.
	 * The caller is responsible for there being nothing on screen drawing from those pages.
	 */
	void ReleaseAtlasTextures();
	int32 GetAtlasTextureSize();

	/**
	 * Reclaim the rectangles held by sprites nothing is drawing any more, oldest first, and rebuild
	 * the pages around the ones that stay.
	 *
	 * This is the answer to an atlas that only ever grows. A bin pack hands a rectangle out and
	 * cannot take it back -- MAXRECTS has no free operation -- so the only way to reclaim one is to
	 * rebuild the page from the sprites that survive, which is what RepackPages does. Who is "not
	 * drawn any more" is not a guess: RenderSpriteArray is every element currently drawing from this
	 * atlas and each one names the sprite it draws.
	 *
	 * @param InRequiredArea stop once this many pixels have been freed; 0 evicts every unused sprite.
	 * @return how many sprites were evicted. They go back to "not packed yet" and repack on next use.
	 */
	int32 EvictUnusedSprites(int32 InRequiredArea = 0);
	/** Rebuild every page from the sprites still in SpriteDataArray, then drop the pages left empty. */
	void RepackPages();
	/** Drop trailing pages nothing is packed into, keeping the first. @return how many were dropped. */
	int32 DropEmptyPages();
	/** Tell every element drawing from this atlas that its rectangle moved. */
	void NotifyRenderSpritesAtlasChanged();
	/** Move a sprite to the most-recently-used end of SpriteDataArray. */
	void TouchSprite(UDreamUISpriteData* Sprite);
	/** The area one sprite occupies in a page, padding included. 0 when it has no texture. */
	int32 GetInsertAreaForSprite(const UDreamUISpriteData* Sprite)const;

	/** Pack into the pages that already exist. No growth and no eviction: PackSprite does those. */
	bool TryPackSpriteIntoExistingPages(UDreamUISpriteData* Sprite);
	bool PackSprite(UDreamUISpriteData* Sprite);
	void CopySpriteTextureToAtlas(UDreamUISpriteData* InSprite, UTexture2D* InAtlasTexture, rbp::Rect InPackedRect, int32 InAtlasTexturePadding);
};

UCLASS(NotBlueprintable, NotBlueprintType)
class DREAMGUI_API UDreamUIDynamicSpriteAtlasManager :public UObject
{
	GENERATED_BODY()
public:
	static UDreamUIDynamicSpriteAtlasManager* Instance;
private:
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI")
		TMap<FName, FDreamUIDynamicSpriteAtlasData> AtlasMap;
protected:
	virtual void BeginDestroy()override;
public:
	static bool InitCheck();
	const TMap<FName, FDreamUIDynamicSpriteAtlasData>& GetAtlasMap() { return AtlasMap; }
	static FDreamUIDynamicSpriteAtlasData* FindOrAdd(const FName& InPackingTag);
	static FDreamUIDynamicSpriteAtlasData* Find(const FName& InPackingTag);
	static void ResetAtlasMap();

	/**
	 * Dispose and release atlas by PackingTag.
	 * This will not dispose the DreamUISpriteData.
	 * Default "Main" tag is not allowed to be disposed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static void DisposeAtlasByPackingTag(FName InPackingTag);

	/**
	 * Release the atlas pages of one packing tag IF nothing is drawing from them any more, and leave
	 * the entry in place so the tag keeps its settings.
	 *
	 * The dynamic atlas grows and never shrinks on its own: a rect is handed out by the bin pack and
	 * never handed back, so a long session that shows many different sprites under one tag keeps
	 * adding pages. This is the explicit "give the memory back" call for the moments an application
	 * knows about -- a level transition, closing a UI that owned a tag's worth of sprites. Sprites of
	 * that tag repack on their next use.
	 *
	 * @return true if pages were released. False means the tag is unknown, already empty, or still in use.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static bool TrimAtlasByPackingTag(FName InPackingTag);

	/**
	 * Run TrimAtlasByPackingTag over every tag. Returns how many atlases were released.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static int32 TrimUnusedAtlases();

	/**
	 * Reclaim the space held by sprites of this tag that nothing is drawing any more, WITHOUT
	 * disturbing the ones that are, and drop any page left empty.
	 *
	 * This is the finer-grained neighbour of TrimAtlasByPackingTag, which can only act when the whole
	 * tag has gone quiet. It also runs by itself whenever a pack would otherwise have to add a page,
	 * so an application that never calls it still gets an atlas that recycles rather than only grows.
	 *
	 * @return how many sprites were evicted; they repack on their next use.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static int32 EvictUnusedSpritesByPackingTag(FName InPackingTag);

	DECLARE_EVENT(UDreamUIDynamicSpriteAtlasManager, FDreamUIAtlasMapChangeEvent);

	FDreamUIAtlasMapChangeEvent OnAtlasMapChanged;
};