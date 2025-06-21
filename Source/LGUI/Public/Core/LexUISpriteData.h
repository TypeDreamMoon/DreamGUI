// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Utils/MaxRectsBinPack/MaxRectsBinPack.h"
#include "Engine/Texture2D.h"
#include "LexUISpriteData_BaseObject.h"
#include "LexUISpriteData.generated.h"


class ULexUISpriteData;
class UUISpriteBase;

#define WARNING_ATLAS_SIZE 4096

/**
 * A sprite-data type that can do automatic packing
 */
UCLASS(BlueprintType)
class LGUI_API ULexUISpriteData :public ULexUISpriteData_BaseObject
{
	GENERATED_BODY()
private:
	friend class FLexUISpriteDataCustomization;
	friend class ULexUISpriteDataFactory;
	friend struct FLexUIDynamicSpriteAtlasData;
	friend class ULexUIStaticSpriteAtlasData;
	/**
	 * Texture of this sprite. Sprite is actually rendered from atlas texture, so spriteTexture is not needed if atlas-data is packed; But! since atlas texture is packed at runtime, we must include spriteTexture inside final package.
	 * DoNot modify spriteTexture's setting unless you know what you do
	 */
	UPROPERTY(EditAnywhere, Category = LGUI)
		TObjectPtr<UTexture2D> spriteTexture;
	/** Information needed for render this sprite */
	UPROPERTY(EditAnywhere, Category = LGUI)
		FLexUISpriteInfo spriteInfo;

	/**
	 * Use a StaticSpriteAtlasData to pack multiple sprites into single atlas texture. The packing process is in editor and cook time, no performance impack at runtime.
	 * Support mipmaps.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		TObjectPtr<ULexUIStaticSpriteAtlasData> packingAtlas = nullptr;
	/**
	 * Sprites that have same packingTag will be packed into same atlas at runtime. If packingTag is None, then the UISprite which render this LGUISpriteData will be treated as a UITexture.
	 * Not support mipmaps.
	 * Only valid if PackingAtals is empty.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		FName packingTag = TEXT("Main");
	/** Repeat edge pixel fill spaced between other sprites in atlas texture */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		bool useEdgePixelPadding = true;
private:
	bool isInitialized = false;
	UPROPERTY(Transient)TObjectPtr<UTexture2D> atlasTexture = nullptr;
	bool PackageSprite();
	bool InsertTexture(FLexUIDynamicSpriteAtlasData* InAtlasData);
	void CheckSpriteTexture();
	void CopySpriteTextureToAtlas(rbp::Rect InPackedRect, int32 InAtlasTexturePadding);
public:
	bool GetUseEdgePixelPadding()const { return useEdgePixelPadding; }
	ULexUIStaticSpriteAtlasData* GetPackingAtlas()const { return packingAtlas; }
	void ApplySpriteInfoAfterStaticPack(const rbp::Rect& InPackedRect, float InAtlasTextureSizeInv);
	//Begin ULGUISpriteData_BaseObject interface
	virtual UTexture2D * GetAtlasTexture()override;
	virtual const FLexUISpriteInfo& GetSpriteInfo()override;
	virtual bool IsIndividual()const override;
	virtual void AddUISprite(TScriptInterface<class IUISpriteRenderableInterface> InUISprite)override;
	virtual void RemoveUISprite(TScriptInterface<class IUISpriteRenderableInterface> InUISprite)override;
	virtual bool ReadPixel(const FVector2D& InUV, FColor& OutPixel)const override;
	virtual bool SupportReadPixel()const override;
	//End ULGUISpriteData_BaseObject interface

	/** initialize sprite data */
	void InitSpriteData();
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool HavePackingTag()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI") const FName& GetPackingTag()const;

	/**
	 * Create a LGUIspriteData with provided parameter. This can use at runtime
	 * @param Outer						Outer of the result LGUISpriteData
	 * @param inSpriteTexture			Use this texture to create
	 * @param inHorizontalBorder		Horizontal border value, x for left, y for right, will be convert to uint16</param>
	 * @param inVerticalBorder			Vertical border value, x for top, y for bottom, will be convert to uint16</param>
	 * @param inPackingTag				see "packingTag" property
	 * @return							Created LGUISpriteData, nullptr if something wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		static ULexUISpriteData* CreateLGUISpriteData(UObject* Outer, UTexture2D* inSpriteTexture, FVector2D inHorizontalBorder = FVector2D::ZeroVector, FVector2D inVerticalBorder = FVector2D::ZeroVector, FName inPackingTag = TEXT("Main"));

	/**
	 * If texture is changed, use this to reload texture.
	 * Not support packingAtlas (static packing).
	 */
	void ReloadTexture();
	UFUNCTION(BlueprintCallable, Category = "LGUI") UTexture2D* GetSpriteTexture()const { return spriteTexture; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	static void MarkAllSpritesNeedToReinitialize();
#endif
	static void CheckAndApplySpriteTextureSetting(UTexture2D* InSpriteTexture);
	static ULexUISpriteData* GetDefaultWhiteSolid();
};