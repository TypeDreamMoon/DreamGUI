// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexWidgetTypes.generated.h"

UENUM(BlueprintType)
enum class ELexWidgetOffsetType : uint8
{
	//Value is set by fixed pixel value
	Fixed,
	//Value is relative to it's parent size, and use Percent to control it 
	RelativeToParentSize,
	//Value is relative to it-self's size, and use Percent to control it 
	RelativeToSelfSize,
};

USTRUCT(BlueprintType)
struct FLexWidgetOffset
{
	GENERATED_BODY()
	FLexWidgetOffset(){}
	FLexWidgetOffset(ELexWidgetOffsetType InType, float InValue, float InPercent)
	{
		Type = InType;
		Value = InValue;
		Percent = InPercent;
	}
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELexWidgetOffsetType Type = ELexWidgetOffsetType::Fixed;
	/** Pixel value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 100.0f;
	/** Percent value relative to parent, valid when Type is ExpandToParent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Percent = 100;

	bool operator==(const FLexWidgetOffset& Other) const
	{
		return Type == Other.Type && Value == Other.Value && Percent == Other.Percent;
	}

	static FLexWidgetOffset MakeFixed(float Value)
	{
		FLexWidgetOffset Result;
		Result.Type = ELexWidgetOffsetType::Fixed;
		Result.Value = Value;
		return Result;
	}
	static FLexWidgetOffset MakeRelativeToParentSize(float Percent)
	{
		FLexWidgetOffset Result;
		Result.Type = ELexWidgetOffsetType::RelativeToParentSize;
		Result.Percent = Percent;
		return Result;
	}
	static FLexWidgetOffset MakeRelativeToSelfSize(float Percent)
	{
		FLexWidgetOffset Result;
		Result.Type = ELexWidgetOffsetType::RelativeToSelfSize;
		Result.Percent = Percent;
		return Result;
	}
};


UENUM(BlueprintType)
enum class ELexWidgetAspectRatioType : uint8
{
	None,
	WidthControlHeight,
	HeightControlWidth,
};

USTRUCT(BlueprintType)
struct FLexWidgetAspectRatio
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELexWidgetAspectRatioType Type;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 1.0f;

	bool operator==(const FLexWidgetAspectRatio& Other) const
	{
		return Type == Other.Type && Value == Other.Value;
	}
};

UENUM(BlueprintType)
enum class ELexWidgetSizeType : uint8
{
	//Size is set by fixed pixel value. Margin will only affect position.
	Fixed,
	//Size expand to it's parent. Margin will affect position and size.
	ExpandToParent,
	//Size shrink to it's children, if no children then shrink to visual, if not visual then fallback to Fixed. Margin will only affect position.
	ShrinkToChildren,
};

USTRUCT(BlueprintType)
struct FLexWidgetSize
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELexWidgetSizeType Type = ELexWidgetSizeType::Fixed;
	/** Fixed pixel value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 100.0f;
	/** Percent value relative to parent, valid when Type is ExpandToParent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin="0", UIMax="100"))
	float Percent = 100;

	bool operator==(const FLexWidgetSize& Other) const
	{
		return Type == Other.Type && Value == Other.Value && Percent == Other.Percent;
	}

	static FLexWidgetSize MakeFixed(float Value)
	{
		FLexWidgetSize Result;
		Result.Type = ELexWidgetSizeType::Fixed;
		Result.Value = Value;
		return Result;
	}
	static FLexWidgetSize MakePercent(float Percent)
	{
		FLexWidgetSize Result;
		Result.Type = ELexWidgetSizeType::ExpandToParent;
		Result.Percent = Percent;
		return Result;
	}
	static FLexWidgetSize MakeShrinkToChildren()
	{
		FLexWidgetSize Result;
		Result.Type = ELexWidgetSizeType::ShrinkToChildren;
		return Result;
	}
};

USTRUCT(BlueprintType)
struct FLexWidgetSize2
{
	GENERATED_BODY()
	FLexWidgetSize2(){}
	FLexWidgetSize2(const FLexWidgetSize& InX, const FLexWidgetSize& InY)
	{
		X = InX;
		Y = InY;
	}
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetSize X;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetSize Y;
	
	bool operator==(const FLexWidgetSize2& Other) const
	{
		return X == Other.X && Y == Other.Y;
	}

	static FLexWidgetSize2 MakeFixed(FVector2f Value)
	{
		return FLexWidgetSize2(FLexWidgetSize::MakeFixed(Value.X), FLexWidgetSize::MakeFixed(Value.Y));
	}
	static FLexWidgetSize2 MakePercent(FVector2f Percent)
	{
		return FLexWidgetSize2(FLexWidgetSize::MakePercent(Percent.X), FLexWidgetSize::MakePercent(Percent.Y));
	}
};

UENUM(BlueprintType)
enum class ELexWidgetMarginSizeType : uint8
{
	//Size is set by fixed pixel value
	Fixed,
	//Size is percentage of it's parent size, if no parent then fallback to Fixed
	PercentOfParent,
};

USTRUCT(BlueprintType)
struct FLexWidgetMarginSize
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELexWidgetMarginSizeType Type = ELexWidgetMarginSizeType::Fixed;
	/** Fixed pixel value, valid when Type is Fixed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 0.0f;
	/** Percent value relative to parent, valid when Type is ExpandToParent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin="0", UIMax="100"))
	float Percent = 0;

	bool operator==(const FLexWidgetMarginSize& Other) const
	{
		return Type == Other.Type && Value == Other.Value && Percent == Other.Percent;
	}

	static FLexWidgetMarginSize MakeFixed(float Value)
	{
		FLexWidgetMarginSize Result;
		Result.Type = ELexWidgetMarginSizeType::Fixed;
		Result.Value = Value;
		return Result;
	}
	static FLexWidgetMarginSize MakePercent(float Percent)
	{
		FLexWidgetMarginSize Result;
		Result.Type = ELexWidgetMarginSizeType::PercentOfParent;
		Result.Percent = Percent;
		return Result;
	}
};

USTRUCT(BlueprintType)
struct FLexWidgetMargin
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetMarginSize Left;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetMarginSize Top;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetMarginSize Right;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLexWidgetMarginSize Bottom;

public:
	FLexWidgetMargin()
	{
		Left = Top = Right = Bottom = FLexWidgetMarginSize::MakeFixed(0.0f);
	}
	FLexWidgetMargin(const FLexWidgetMarginSize& UniformMargin)
	{
		Left = Top = Right = Bottom = UniformMargin;
	}
	FLexWidgetMargin(const FLexWidgetMarginSize& Horizontal, const FLexWidgetMarginSize& Vertical)
	{
		Left = Right = Horizontal;
		Bottom = Top = Vertical;
	}
	FLexWidgetMargin(const FLexWidgetMarginSize& InLeft, const FLexWidgetMarginSize& InTop, const FLexWidgetMarginSize& InRight, const FLexWidgetMarginSize& InBottom)
	{
		Left = InLeft;
		Top = InTop;
		Right = InRight;
		Bottom = InBottom;
	}

	bool AffectByParent()const
	{
		if (Left.Type == ELexWidgetMarginSizeType::PercentOfParent && Left.Percent != 0.0f)
			return true;
		if (Top.Type == ELexWidgetMarginSizeType::PercentOfParent && Top.Percent != 0.0f)
			return true;
		if (Right.Type == ELexWidgetMarginSizeType::PercentOfParent && Right.Percent != 0.0f)
			return true;
		if (Bottom.Type == ELexWidgetMarginSizeType::PercentOfParent && Bottom.Percent != 0.0f)
			return true;
		return false;
	}

	bool operator==(const FLexWidgetMargin& Other) const
	{
		return (Left == Other.Left) && (Right == Other.Right) && (Bottom == Other.Bottom) && (Top == Other.Top);
	}
	bool operator!=(const FLexWidgetMargin& Other) const
	{
		return Left != Other.Left || Right != Other.Right || Bottom != Other.Bottom || Top != Other.Top;
	}
};
