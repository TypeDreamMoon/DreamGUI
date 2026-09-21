// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "DreamUIImageBrush.generated.h"

UENUM(BlueprintType)
enum class EDreamUIImageBrushDrawType:uint8
{
	None,
	/** Draw a 3x3 box, where the sides and the middle stretch based on the Margin */
	Box,
	/** Draw a 3x3 border where the sides tile and the middle is empty */
	Border,
	/** Draw an image; margin is ignored */
	Image,
	/**
	 * Repeat the image at its own size across the rect, instead of stretching one copy over it.
	 * The tile is the sprite's authored size, or Brush.ImageSize when the resource is not a sprite.
	 */
	Tiled,
	/**
	 * Show only part of the image and leave the rest transparent, the way a progress bar does.
	 * FillAmount is how much (0..1), FillMethod is the shape of the cut and FillOrigin which end it
	 * starts from. The same geometry the sprite element fills with, so the two agree.
	 */
	Filled,
	/**
	 * Draw the image inside a rounded rectangle, with a radius per corner (CornerRadius) and
	 * CornerSegments points along each arc.
	 *
	 * This is an OUTLINE, cut out of geometry: the corners are tessellated rather than evaluated in a
	 * shader, so they are as smooth as the segment count and they do not antialias themselves. Where
	 * an exact, resolution-independent rounded rect is wanted -- with a border, gradients or shadows
	 * -- UDreamRectBlock does the same shape as a signed distance field in the material and is the
	 * better tool; this exists so a brush can round its corners without changing element type.
	 */
	RoundedBox,
};

/** How a Filled brush cuts its image. Mirrors EDreamUISpriteFillMethod value for value. */
UENUM(BlueprintType)
enum class EDreamUIImageBrushFillMethod :uint8
{
	Horizontal,
	Vertical,
	Radial90,
	Radial180,
	Radial360,
};


USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIImageBrush
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (AllowPrivateAccess = "true", DisplayThumbnail = "true", DisplayName = "Image"
		, AllowedClasses = "/Script/Engine.Texture,/Script/Engine.MaterialInterface,/Script/Engine.SlateTextureAtlasInterface,/Script/DreamGUI.DreamUISpriteData_BaseObject"))
	TObjectPtr<UObject> ResourceObject;
public:
	FDreamUIImageBrush();
	static FName GetPropertyName_ResourceObject()
	{
		return GET_MEMBER_NAME_CHECKED(FDreamUIImageBrush, ResourceObject);
	}
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FColor TintColor = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	EDreamUIImageBrushDrawType DrawAs = EDreamUIImageBrushDrawType::Box;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FVector2f ImageSize = FVector2f(32, 32);
	/** Margin size from 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (UVSpace="true"))
	FMargin Margin;
	/** UV region for an image, xy for min and zw for max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	FVector4f UVRegion = FVector4f(0,0,1,1);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	float PixelsPerUnitMultiplier = 1;

	/** Filled only: how much of the image is shown, 0 shows none of it and 1 all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (ClampMin = 0.0, ClampMax = 1.0, EditCondition = "DrawAs==EDreamUIImageBrushDrawType::Filled"))
	float FillAmount = 1.0f;
	/** Filled only: the shape of the cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (EditCondition = "DrawAs==EDreamUIImageBrushDrawType::Filled"))
	EDreamUIImageBrushFillMethod FillMethod = EDreamUIImageBrushFillMethod::Horizontal;
	/**
	 * Filled only: which corner or edge a radial fill starts from. Held as a byte for the same reason
	 * UDreamSprite holds it as one: which enum it means depends on FillMethod
	 * (EDreamUISpriteFillOriginType_Radial90 / _Radial180 / _Radial360), and only one of them applies
	 * at a time. Ignored by the horizontal and vertical methods.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (EditCondition = "DrawAs==EDreamUIImageBrushDrawType::Filled"))
	uint8 FillOrigin = 0;
	/** Filled only: fill from the other end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (EditCondition = "DrawAs==EDreamUIImageBrushDrawType::Filled"))
	bool FillDirectionFlip = false;

	/**
	 * RoundedBox only: corner radius in pixels, X top-left, Y top-right, Z bottom-right, W
	 * bottom-left -- clockwise from the top left, the order CSS and UDreamRectBlock both use. Each is
	 * clamped to half the shorter side, so a big enough radius gives a capsule rather than an error.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (EditCondition = "DrawAs==EDreamUIImageBrushDrawType::RoundedBox"))
	FVector4f CornerRadius = FVector4f(8, 8, 8, 8);
	/** RoundedBox only: points along each corner arc. More is smoother and costs two triangles each. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush", meta = (ClampMin = 1, ClampMax = 32, EditCondition = "DrawAs==EDreamUIImageBrushDrawType::RoundedBox"))
	int32 CornerSegments = 6;

	UObject* GetResourceObject()const { return ResourceObject; }
	void SetResourceObject(UObject* Value) { ResourceObject = Value; }
};
