// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILexUISpriteRenderInterface.generated.h"

class ULexUISpriteData_BaseObject;

UINTERFACE(Blueprintable, MinimalAPI)
class ULexUISpriteRenderInterface : public UInterface
{
	GENERATED_BODY()
};
class LGUI_API ILexUISpriteRenderInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "LGUI")
		ULexUISpriteData_BaseObject* SpriteRenderGetSprite()const;
	UFUNCTION(BlueprintNativeEvent, Category = "LGUI")
		void ApplyAtlasTextureChange();
	UFUNCTION(BlueprintNativeEvent, Category = "LGUI")
		void ApplyAtlasTextureScaleUp();
};
