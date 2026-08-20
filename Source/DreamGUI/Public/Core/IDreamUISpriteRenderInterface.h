// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDreamUISpriteRenderInterface.generated.h"

class UDreamUISpriteData_BaseObject;

UINTERFACE(Blueprintable, MinimalAPI)
class UDreamUISpriteRenderInterface : public UInterface
{
	GENERATED_BODY()
};
class DREAMGUI_API IDreamUISpriteRenderInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, Category = "DreamGUI")
		UDreamUISpriteData_BaseObject* SpriteRenderGetSprite()const;
	UFUNCTION(BlueprintNativeEvent, Category = "DreamGUI")
		void ApplyAtlasTextureChange();
};
