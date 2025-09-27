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
	FMargin Border;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	FMargin Padding;

	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	FVector2f MinUV = FVector2f(0, 0);
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	FVector2f MaxUV = FVector2f(1, 1);
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	FVector2f BorderMinUV = FVector2f(0, 0);
	UPROPERTY(VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	FVector2f BorderMaxUV = FVector2f(1, 1);

public:
	auto GetUV0()const { return FVector2f(MinUV.X, MaxUV.Y); }
	auto GetUV1()const { return FVector2f(MaxUV.X, MaxUV.Y); }
	auto GetUV2()const { return FVector2f(MinUV.X, MinUV.Y); }
	auto GetUV3()const { return FVector2f(MaxUV.X, MinUV.Y); }

	auto GetUVCenter()const { return FVector2f((MaxUV.X - MinUV.X) * 0.5f + MinUV.X, (MaxUV.Y - MinUV.Y) * 0.5f + MinUV.Y); }

	uint16 GetSourceWidth()const { return Width + Padding.Left + Padding.Right; }
	uint16 GetSourceHeight()const { return Height + Padding.Top + Padding.Bottom; }
	
	bool HasBorder()const;
	bool HasPadding()const;
	void ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal);
	void ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4& uvRect);
	void ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal);
	void ScaleUV(float InMultiply)
	{
		MinUV *= InMultiply;
		MaxUV *= InMultiply;

		BorderMinUV *= InMultiply;
		BorderMaxUV *= InMultiply;
	}

	bool operator == (const FLexUISpriteInfo& Other)const
	{
		return Width == Other.Width
			&& Height == Other.Height
			&& Border == Other.Border
			&& Padding == Other.Padding
			;
	}
	bool operator != (const FLexUISpriteInfo& Other)const
	{
		return Width != Other.Width
			|| Height != Other.Height
			|| Border != Other.Border
			|| Padding != Other.Padding
			;
	}
};
