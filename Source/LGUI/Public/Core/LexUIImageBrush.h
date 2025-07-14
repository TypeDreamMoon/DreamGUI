// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "LexUIImageBrush.generated.h"

UENUM(BlueprintType)
enum class ELexUIImageBrushDrawType:uint8
{
	None,
	/** Draw a 3x3 box, where the sides and the middle stretch based on the Margin */
	Box,
	/** Draw a 3x3 border where the sides tile and the middle is empty */
	Border,
	/** Draw an image; margin is ignored */
	Image,
	/** Draw a solid rectangle with an outline and corner radius */
	//RoundedBox
};


USTRUCT(BlueprintType)
struct LGUI_API FLexUIImageBrush
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (AllowPrivateAccess = "true", DisplayThumbnail = "true", DisplayName = "Image"
		, AllowedClasses = "/Script/Engine.Texture,/Script/Engine.MaterialInterface,/Script/Engine.SlateTextureAtlasInterface,/Script/LGUI.LexUISpriteData_BaseObject"))
	TObjectPtr<UObject> ResourceObject;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FColor TintColor = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	ELexUIImageBrushDrawType DrawAs = ELexUIImageBrushDrawType::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FVector2f ImageSize = FVector2f(100, 100);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (UVSpace="true"))
	FMargin Margin;
	/** UV region for an image, xy for min and zw for max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FVector4f UVRegion = FVector4f(0,0,1,1);
	
	UObject* GetResourceObject()const { return ResourceObject; }
	void SetResourceObject(UObject* Value) { ResourceObject = Value; }
};
