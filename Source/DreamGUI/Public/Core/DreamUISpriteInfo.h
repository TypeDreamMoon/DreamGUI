// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUISpriteInfo.generated.h"


/**
 * SpriteInfo contains information for render a Sprite
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUISpriteInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	uint16 Width = 0;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	uint16 Height = 0;

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FMargin Border;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FMargin Padding;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	FVector2f MinUV = FVector2f(0, 0);
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	FVector2f MaxUV = FVector2f(1, 1);
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	FVector2f BorderMinUV = FVector2f(0, 0);
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	FVector2f BorderMaxUV = FVector2f(1, 1);

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bIsBorderDirty = false;
#endif
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
	/**
	 * @return true for anything dirty
	 */
	bool ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal);
	/**
	 * @return true for anything dirty
	 */
	bool ApplyUV(int32 InX, int32 InY, int32 InWidth, int32 InHeight, float texFullWidthReciprocal, float texFullHeightReciprocal, const FVector4f& uvRect);
	/**
	 * @return true for anything dirty
	 */
	bool ApplyBorderUV(float texFullWidthReciprocal, float texFullHeightReciprocal);
	void ScaleUV(float InMultiply)
	{
		MinUV *= InMultiply;
		MaxUV *= InMultiply;

		BorderMinUV *= InMultiply;
		BorderMaxUV *= InMultiply;
	}

	bool operator == (const FDreamUISpriteInfo& Other)const
	{
		return Width == Other.Width
			&& Height == Other.Height
			&& Border == Other.Border
			&& Padding == Other.Padding
			;
	}
	bool operator != (const FDreamUISpriteInfo& Other)const
	{
		return Width != Other.Width
			|| Height != Other.Height
			|| Border != Other.Border
			|| Padding != Other.Padding
			;
	}
};
