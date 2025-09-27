// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexUISpriteInfo.generated.h"


/**
 * SpriteInfo contains information for render a Sprite
 */
USTRUCT(BlueprintType)
struct LGUI_API FLexUISpriteInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		uint16 Width = 0;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		uint16 Height = 0;

	UPROPERTY(EditAnywhere, Category = "LGUI")
		float borderLeft = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float borderRight = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float borderTop = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float borderBottom = 0;

	UPROPERTY(EditAnywhere, Category = "LGUI")
		float paddingLeft = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float paddingRight = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float paddingTop = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float paddingBottom = 0;

	/** left point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float uvMinX = 0;
	/** bottom point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float uvMinY = 1;
	/** right point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float uvMaxX = 1;
	/** top point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float uvMaxY = 0;

	/** border left point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float buvMinX = 0;
	/** border bottom point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float buvMinY = 1;
	/** border right point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float buvMaxX = 1;
	/** border top point uv */
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
		float buvMaxY = 0;

public:
	auto GetUV0()const { return FVector2f(uvMinX, uvMinY); }
	auto GetUV1()const { return FVector2f(uvMaxX, uvMinY); }
	auto GetUV2()const { return FVector2f(uvMinX, uvMaxY); }
	auto GetUV3()const { return FVector2f(uvMaxX, uvMaxY); }

	auto GetUVCenter()const { return FVector2f((uvMaxX - uvMinX) * 0.5f + uvMinX, (uvMinY - uvMaxY) * 0.5f + uvMaxY); }

	uint16 GetSourceWidth()const { return Width + paddingLeft + paddingRight; }
	uint16 GetSourceHeight()const { return Height + paddingTop + paddingBottom; }
	
	bool HasBorder()const;
	bool HasPadding()const;
	void ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal);
	void ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4& uvRect);
	void ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal);
	void ScaleUV(float InMultiply)
	{
		uvMinX *= InMultiply;
		uvMinY *= InMultiply;
		uvMaxX *= InMultiply;
		uvMaxY *= InMultiply;

		buvMinX *= InMultiply;
		buvMaxX *= InMultiply;
		buvMinY *= InMultiply;
		buvMaxY *= InMultiply;
	}

	bool operator == (const FLexUISpriteInfo& Other)const
	{
		return Width == Other.Width
			&& Height == Other.Height
			&& borderLeft == Other.borderLeft
			&& borderRight == Other.borderRight
			&& borderTop == Other.borderTop
			&& borderBottom == Other.borderBottom
			&& paddingLeft == Other.paddingLeft
			&& paddingRight == Other.paddingRight
			&& paddingTop == Other.paddingTop
			&& paddingBottom == Other.paddingBottom
			;
	}
	bool operator != (const FLexUISpriteInfo& Other)const
	{
		return Width != Other.Width
			|| Height != Other.Height
			|| borderLeft != Other.borderLeft
			|| borderRight != Other.borderRight
			|| borderTop != Other.borderTop
			|| borderBottom != Other.borderBottom
			|| paddingLeft != Other.paddingLeft
			|| paddingRight != Other.paddingRight
			|| paddingTop != Other.paddingTop
			|| paddingBottom != Other.paddingBottom
			;
	}
};
