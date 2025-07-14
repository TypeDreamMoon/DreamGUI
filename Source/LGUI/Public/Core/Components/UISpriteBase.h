// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisualBatchMesh.h"
#include "Core/ILexUISpriteRenderInterface.h"
#include "UISpriteBase.generated.h"

class ULexUISpriteData_BaseObject;

/**
 * This is base class for create custom mesh based on UISprite.
 */
UCLASS(ClassGroup = (LGUI), Abstract, NotBlueprintable)
class LGUI_API UUISpriteBase : public ULexVisualBatchMesh
	, public ILexUISpriteRenderInterface
{
	GENERATED_BODY()

public:	
	UUISpriteBase(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
protected:
	virtual void OnPreChangeSpriteProperty();
	virtual void OnPostChangeSpriteProperty();
#endif
public:
	void CheckSpriteData();
	static const FName GetSpritePropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(UUISpriteBase, Sprite);
	}
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
protected:
	friend class SLexUISpriteBorderEditor;
	friend class FLexUISpriteBaseCustomization;

	/** Sprite may override by UISelectable(UIButton, UIToggle, UISlider ...) */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUISpriteData_BaseObject> Sprite = nullptr;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;

	virtual bool ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const override;

	bool bHasAddToSprite = false;

public:

	UFUNCTION(BlueprintCallable, Category = "LGUI") ULexUISpriteData_BaseObject* GetSprite()const { return Sprite; }
#pragma region LexUISpriteRenderInterface
	virtual ULexUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override{ return Sprite; }
	virtual void ApplyAtlasTextureScaleUp_Implementation()override;
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSprite(ULexUISpriteData_BaseObject* newSprite, bool setSize = true);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSizeFromSpriteData();
};
