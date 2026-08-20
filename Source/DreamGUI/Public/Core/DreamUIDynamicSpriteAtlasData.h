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
	/** sprites belong to this atlas */
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
	int32 GetAtlasTextureSize();

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

	DECLARE_EVENT(UDreamUIDynamicSpriteAtlasManager, FDreamUIAtlasMapChangeEvent);

	FDreamUIAtlasMapChangeEvent OnAtlasMapChanged;
};