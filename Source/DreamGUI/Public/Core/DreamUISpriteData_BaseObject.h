// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "DreamUISpriteInfo.h"
#include "DreamUISpriteData_BaseObject.generated.h"

class UDreamSpriteBase;
class IDreamUISpriteRenderInterface;

/**
 * Base class for Sprite data.
 * A Sprite is a small area rendered in a big atlas texture.
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUISpriteData_BaseObject :public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual UTexture2D* GetAtlasTexture()PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::GetAtlasTexture, return nullptr;);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual const FDreamUISpriteInfo& GetSpriteInfo()PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::GetSpriteInfo, static FDreamUISpriteInfo ForReturn; return ForReturn;);
	/** This Sprite-data is a individual one? Means it will not pack into any atlas texture. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual bool IsIndividual()const PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::IsIndividual, return false;);
	/**
	 * Read pixel value from packed atlas texture.
	 * @param InUV uv coordinate in atlas texture.
	 * @param OutPixel result pixel value.
	 * @return true- successfully read pixel, false- not support read pixel.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual bool ReadPixel(const FVector2D& InUV, FColor& OutPixel)const PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::ReadPixel, return false;);
	/**
	 * Can we read texture's pixel from this Sprite object?
	 */
	virtual bool SupportReadPixel()const PURE_VIRTUAL(UDreamGUISpriteData_BaseObject::SupportReadPixel, return false;);

	virtual void AddUISprite(TScriptInterface<IDreamUISpriteRenderInterface> InUISprite) {};
	virtual void RemoveUISprite(TScriptInterface<IDreamUISpriteRenderInterface> InUISprite) {};

//#if WITH_EDITOR
//	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
//#endif
};
