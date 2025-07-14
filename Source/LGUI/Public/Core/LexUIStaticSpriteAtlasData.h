// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Engine/Texture2D.h"
#include "LexUIStaticSpriteAtlasData.generated.h"

class ULexUISpriteData;
class UUISpriteBase;
class ILexUISpriteRenderInterface;

//Static packing Sprite into atlas
UCLASS(NotBlueprintable, NotBlueprintType, Experimental)
class LGUI_API ULexUIStaticSpriteAtlasData :public UObject
{
	GENERATED_BODY()
private:
	friend class FLexUIStaticSpriteAtlasDataCustomization;
	/** weather or not use srgb for generate atlas texture */
	UPROPERTY(EditAnywhere, Category = "Atlas-Setting")
		bool AtlasTextureUseSRGB = true;
	UPROPERTY(EditAnywhere, Category = "Atlas-Setting")
		TEnumAsByte<TextureFilter> AtlasTextureFilter = TextureFilter::TF_Trilinear;
#if WITH_EDITORONLY_DATA
	/** space between two sprites when package into atlas */
	UPROPERTY(EditAnywhere, Category = "Atlas-Setting")
		int32 SpaceBetweenSprites = 2;
	/** Repeat edge pixel fill spaced between other sprites in atlas texture */
	UPROPERTY(EditAnywhere, Category = "Atlas-Setting")
		int32 EdgePixelPadding = 2;
	/** If the result atlas texture's size is larger than this, then packing operation will abort. */
	UPROPERTY(EditAnywhere, Category = "Atlas-Setting")
		uint32 MaxAtlasTextureSize = 4096;
#endif

	/** Generated atlas texture. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI")
		TObjectPtr<UTexture2D> AtlasTexture = nullptr;
	/** Collected Sprite array to pack. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TArray<TObjectPtr<ULexUISpriteData>> SpriteArray;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULexUISpriteData>> PrevSpriteArray;
	/** collection of all objects that use this atlas to render. Object must implement IUISpriteRenderableInterface. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI", AdvancedDisplay)
		TArray<TWeakObjectPtr<UObject>> RenderSpriteArray;
#endif
	/**
	 * Store texture mip data, so we can recreate atlas texture with this data.
	 * @todo: Actually I want to save this only in cook time (reduce editor asset size), but I can't get texutre's pixel data in cook time.
	 */
	UPROPERTY()
		TArray<uint8> TextureMipData;
	UPROPERTY()
		uint32 TextureSize;
#if WITH_EDITOR
public:
	virtual void PreEditChange(FProperty* PropertyAboutToChange)override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	void AddSpriteData(ULexUISpriteData* InSpriteData);
	void RemoveSpriteData(ULexUISpriteData* InSpriteData);
	void AddRenderSprite(TScriptInterface<ILexUISpriteRenderInterface> InSprite);
	void RemoveRenderSprite(TScriptInterface<ILexUISpriteRenderInterface> InSprite);
	/** Check Sprite and render Sprite, remove not valid. */
	void CheckSprite();
	bool PackAtlas();
	void MarkNotInitialized();
	/** Return true if some spriteData is invalid */
	bool CheckInvalidSpriteData()const;
	void CleanupInvalidSpriteData();

	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)override;
	virtual void WillNeverCacheCookedPlatformDataAgain()override;
	virtual void ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)override;
private:
	bool PackAtlasTest(uint32 size, TArray<rbp::Rect>& result);
	bool bWarningIsAlreadyAppearedAtCurrentPackingSession = false;
	bool bIsYesToAll = false;
	bool bIsNoToAll = false;
	bool bIsAddedToDelayedCall = false;
#endif
private:
	bool bIsInitialized = false;
	virtual void BeginDestroy();
public:
	bool InitCheck();
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UTexture2D* GetAtlasTexture();
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool ReadPixel(const FVector2D& InUV, FColor& OutPixel);
};