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
 * A Sprite-data type that can do automatic packing
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
	 * Texture of this Sprite. Sprite is actually rendered from atlas texture, so SpriteTexture is not needed if atlas-data is packed; But! since atlas texture is packed at runtime, we must include SpriteTexture inside final package.
	 * DoNot modify SpriteTexture's setting unless you know what you do
	 */
	UPROPERTY(EditAnywhere, Category = LGUI)
		TObjectPtr<UTexture2D> SpriteTexture;
	/** Information needed for render this Sprite */
	UPROPERTY(EditAnywhere, Category = LGUI)
		FLexUISpriteInfo SpriteInfo;

	/**
	 * Use a StaticSpriteAtlasData to pack multiple sprites into single atlas texture. The packing process is in editor and cook time, no performance impact at runtime.
	 * Support mipmaps.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		TObjectPtr<ULexUIStaticSpriteAtlasData> packingAtlas = nullptr;
	/**
	 * Sprites that have same PackingTag will be packed into same atlas at runtime. If PackingTag is None, then the UISprite which render this LexUISpriteData will be treated as a UITexture.
	 * Not support mipmaps.
	 * Only valid if PackingAtlas is empty.
	 */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		FName PackingTag = TEXT("Main");
	/** Repeat edge pixel fill spaced between other sprites in atlas texture */
	UPROPERTY(EditAnywhere, Category = "AtlasPacking")
		bool bUseEdgePixelPadding = true;
private:
	bool bIsInitialized = false;
	UPROPERTY(Transient)TObjectPtr<UTexture2D> AtlasTexture = nullptr;
	bool PackageSprite();
	bool InsertTexture(FLexUIDynamicSpriteAtlasData* InAtlasData);
	void CheckSpriteTexture();
	void CopySpriteTextureToAtlas(rbp::Rect InPackedRect, int32 InAtlasTexturePadding);
public:
	bool GetUseEdgePixelPadding()const { return bUseEdgePixelPadding; }
	ULexUIStaticSpriteAtlasData* GetPackingAtlas()const { return packingAtlas; }
	void ApplySpriteInfoAfterStaticPack(const rbp::Rect& InPackedRect, float InAtlasTextureSizeInv);
	//Begin ULexUISpriteData_BaseObject interface
	virtual UTexture2D * GetAtlasTexture()override;
	virtual const FLexUISpriteInfo& GetSpriteInfo()override;
	virtual bool IsIndividual()const override;
	virtual void AddUISprite(TScriptInterface<class ILexUISpriteRenderInterface> InUISprite)override;
	virtual void RemoveUISprite(TScriptInterface<class ILexUISpriteRenderInterface> InUISprite)override;
	virtual bool ReadPixel(const FVector2D& InUV, FColor& OutPixel)const override;
	virtual bool SupportReadPixel()const override;
	//End ULexUISpriteData_BaseObject interface

	/** initialize Sprite data */
	void InitSpriteData();
	UFUNCTION(BlueprintCallable, Category = "LGUI") bool HavePackingTag()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI") const FName& GetPackingTag()const;

	/**
	 * Create a LexUISpriteData with provided parameter. This can use at runtime
	 * @param Outer						Outer of the result LGUISpriteData
	 * @param inSpriteTexture			Use this texture to create
	 * @param inHorizontalBorder		Horizontal border value, x for left, y for right, will be convert to uint16</param>
	 * @param inVerticalBorder			Vertical border value, x for top, y for bottom, will be convert to uint16</param>
	 * @param inPackingTag				see "PackingTag" property
	 * @return							Created LGUISpriteData, nullptr if something wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		static ULexUISpriteData* CreateLexUISpriteData(UObject* Outer, UTexture2D* inSpriteTexture, FVector2D inHorizontalBorder = FVector2D::ZeroVector, FVector2D inVerticalBorder = FVector2D::ZeroVector, FName inPackingTag = TEXT("Main"));

	/**
	 * If texture is changed, use this to reload texture.
	 * Not support packingAtlas (static packing).
	 */
	void ReloadTexture();
	UFUNCTION(BlueprintCallable, Category = "LGUI") UTexture2D* GetSpriteTexture()const { return SpriteTexture; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	static void MarkAllSpritesNeedToReinitialize();
#endif
	static void CheckAndApplySpriteTextureSetting(UTexture2D* InSpriteTexture);
	static ULexUISpriteData* GetDefaultWhiteSolid();
	static ULexUISpriteData* GetDefaultFrameRect();
};