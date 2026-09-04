// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Engine/Texture2D.h"
#include "DreamUISpriteData_BaseObject.h"
#include "DreamUISpriteData.generated.h"


class UDreamUISpriteData;
class UDreamSpriteBase;

#define WARNING_ATLAS_SIZE 4096

UENUM(BlueprintType)
enum class EDreamUISpritePackingType : uint8
{
	/** Static packing, Sprite will be packed in editor no matter if it used, not support runtime packing, support mipmap. */
	Static,
	/** Dynamic packing. Sprite will be packed when using, support runtime packing, not support mipmap. */
	Dynamic,
};

/**
 * A Sprite-data type that can do automatic packing
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUISpriteData :public UDreamUISpriteData_BaseObject
{
	GENERATED_BODY()
	UDreamUISpriteData();
private:
	friend class FDreamUISpriteDataCustomization;
	friend class UDreamUISpriteDataFactory;
	friend struct FDreamUIDynamicSpriteAtlasData;
	friend class UDreamUIStaticSpriteAtlasData;
	/**
	 * Texture of this Sprite. Sprite is actually rendered from atlas texture, so SpriteTexture is not needed if atlas-data is packed; But! since atlas texture is packed at runtime, we must include SpriteTexture inside final package.
	 * DoNot modify SpriteTexture's setting unless you know what you do
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		TObjectPtr<UTexture2D> SpriteTexture;
	/** Information needed for render this Sprite */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
		FDreamUISpriteInfo SpriteInfo;

	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
	EDreamUISpritePackingType PackingType = EDreamUISpritePackingType::Dynamic;
	/**
	 * Use a StaticSpriteAtlasData to pack multiple sprites into single atlas texture. The packing process is in editor and cook time, no performance impact at runtime.
	 * Support mipmaps.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking", meta=(EditCondition="PackingType==EDreamUISpritePackingType::Static"))
		TObjectPtr<UDreamUIStaticSpriteAtlasData> PackingAtlas = nullptr;
	/** Texture index in atlas texture array */
	UPROPERTY(VisibleAnywhere, Category = "AtlasPacking", meta=(EditCondition="PackingType==EDreamUISpritePackingType::Static"))
	int32 AtlasTextureIndex = 0;
	/**
	 * Sprites that have same PackingTag will be packed into same atlas at runtime. If PackingTag is None, then the DreamSprite which render this DreamUISpriteData will be treated as a DreamTexture.
	 * Not support mipmaps.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking", meta=(EditCondition="PackingType==EDreamUISpritePackingType::Dynamic"))
		FName PackingTag = TEXT("Main");
	
	/** Repeat edge pixel fill spaced between other sprites in atlas texture */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		bool bUseEdgePixelPadding = true;
private:
	bool bIsInitialized = false;
	UPROPERTY(Transient)TObjectPtr<UTexture2D> AtlasTexture = nullptr;
	bool PackSprite();
	void CheckSpriteTexture();
public:
	bool GetUseEdgePixelPadding()const { return bUseEdgePixelPadding; }
	UDreamUIStaticSpriteAtlasData* GetPackingAtlas()const { return PackingAtlas; }
	/**
	 * @return anything changed
	 */
	bool ApplySpriteInfoAfterStaticPack(const rbp::Rect& InPackedRect, float InAtlasTextureSizeInv);
	//Begin UDreamUISpriteData_BaseObject interface
	virtual UTexture2D * GetAtlasTexture()override;
	virtual const FDreamUISpriteInfo& GetSpriteInfo()override;
	virtual bool IsIndividual()const override;
	virtual void AddUISprite(TScriptInterface<class IDreamUISpriteRenderInterface> InUISprite)override;
	virtual void RemoveUISprite(TScriptInterface<class IDreamUISpriteRenderInterface> InUISprite)override;
	virtual bool ReadPixel(const FVector2D& InUV, FColor& OutPixel)const override;
	virtual bool SupportReadPixel()const override;
	//End UDreamUISpriteData_BaseObject interface

	/** initialize Sprite data */
	void InitSpriteData();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool HavePackingTag()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FName& GetPackingTag()const;

	/**
	 * Create a DreamUISpriteData with provided parameter. This can use at runtime
	 * @param Outer						Outer of the result DreamUISpriteData
	 * @param InSpriteTexture			Use this texture to create
	 * @param InBorder					Border value
	 * @param InPackingTag				See "PackingTag" property
	 * @return							Created DreamUISpriteData, nullptr if something wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static UDreamUISpriteData* CreateDreamUISpriteData(UObject* Outer, UTexture2D* InSpriteTexture, FMargin InBorder, FName InPackingTag = TEXT("Main"));

	/**
	 * If texture is changed, use this to reload texture.
	 * Not support packingAtlas (static packing).
	 */
	void ReloadTexture();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UTexture2D* GetSpriteTexture()const { return SpriteTexture; }
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	static void MarkAllSpritesNeedToReinitialize();
#endif
	/**
	 * Does the texture still need the sprite settings applied to it?
	 * Callers that wrap the apply in a transaction need this on the outside: a texture that already
	 * has the right settings must not be Modify()d nor have its resource rebuilt.
	 * @return true if compression settings, LOD group or sRGB do not match what a sprite texture needs
	 */
	static bool NeedsSpriteTextureSetting(const UTexture2D* InSpriteTexture);
	static void CheckAndApplySpriteTextureSetting(UTexture2D* InSpriteTexture);
	static UDreamUISpriteData* GetDefaultWhiteSolid();
	static UDreamUISpriteData* GetDefaultFrameRect();
};