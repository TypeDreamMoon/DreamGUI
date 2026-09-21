// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "DreamSpriteBase.generated.h"

class UDreamUISpriteData_BaseObject;

/**
 * This is base class for create custom mesh based on UISprite.
 */
UCLASS(ClassGroup = (DreamGUI), Abstract, NotBlueprintable)
class DREAMGUI_API UDreamSpriteBase : public UDreamVisualBatchMesh
	, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()

public:	
	UDreamSpriteBase(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
protected:
	virtual void OnPreChangeSpriteProperty();
	virtual void OnPostChangeSpriteProperty();
#endif
public:
	void CheckSpriteData();
	static FName GetPropertyName_Sprite()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamSpriteBase, Sprite);
	}
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void BeginDestroy() override;
protected:
	friend class SDreamUISpriteBorderEditor;
	friend class FDreamSpriteBaseCustomization;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayThumbnail = "true"))
	TObjectPtr<UDreamUISpriteData_BaseObject> Sprite = nullptr;
	/** Use a custom material to render this sprite */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	UMaterialInterface* OverrideMaterial = nullptr;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry() override;

	virtual bool ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const override;

	bool bHasAddToSprite = false;

	/**
	 * The sprite's own pixel size, copied out the last time this component had the sprite data in
	 * its hands. Negative until that has happened at least once.
	 *
	 * Deliberately not a UPROPERTY. It is a duplicate of something the sprite asset already owns,
	 * and serialising it would let a stale copy -- taken while an atlas was mid-repack, say --
	 * outlive the truth and win on load.
	 */
	FVector2f CachedSpriteSourceSize = FVector2f(-1.0f, -1.0f);
	/**
	 * Re-reads the sprite's source size into the cache above.
	 *
	 * Call this ONLY from a site that is already touching the sprite data on that same call. It goes
	 * through GetSpriteInfo, which is not the accessor its name suggests: see GetPreferredWidth.
	 */
	void CacheSpriteSourceSize();

public:

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUISpriteData_BaseObject* GetSprite()const { return Sprite; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}
#pragma region DreamUISpriteRenderInterface
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override{ return Sprite; }
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSprite(UDreamUISpriteData_BaseObject* Value, bool bSetSize = true);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSizeFromSpriteData();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetOverrideMaterial(UMaterialInterface* Value);

	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;
};
