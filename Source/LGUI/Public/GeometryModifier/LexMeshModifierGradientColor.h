// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexMeshModifierBase.h"
#include "LexMeshModifierGradientColor.generated.h"


UENUM(BlueprintType, Category = LGUI)
enum class ELexMeshModifierGradientColorDirection :uint8
{
	BottomToTop,
	TopToBottom,
	LeftToRight,
	RightToLeft,
	FourCorner,
};
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexMeshModifierGradientColor : public ULexMeshModifierBase
{
	GENERATED_BODY()

public:	
	ULexMeshModifierGradientColor();

protected:
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexMeshModifierGradientColorDirection directionType = ELexMeshModifierGradientColorDirection::BottomToTop;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool multiplySourceAlpha = true;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FColor color1 = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FColor color2 = FColor::White;

	//only use for FourCornor
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FColor color3 = FColor::Black;
	//only use for FourCornor
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FColor color4 = FColor::White;
	FORCEINLINE void ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor);
public:
	virtual void ModifyUIGeometry(FLexUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)override
	{
		OutTriangleIndices = false;
		OutVertexPosition = false;
		OutUV = false;
		OutColor = true;
	};
};
