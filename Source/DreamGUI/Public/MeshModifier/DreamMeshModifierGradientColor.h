// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierGradientColor.generated.h"


UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamMeshModifierGradientColorDirection :uint8
{
	BottomToTop,
	TopToBottom,
	LeftToRight,
	RightToLeft,
	FourCorner,
};
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName="GradientColor", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierGradientColor : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierGradientColor();

protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamMeshModifierGradientColorDirection DirectionType = EDreamMeshModifierGradientColorDirection::BottomToTop;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bMultiplySourceAlpha = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor Color1 = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FColor Color2 = FColor::White;

	//only use for FourCorner
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(EditCondition="DirectionType==EDreamMeshModifierGradientColorDirection::FourCorner"))
		FColor Color3 = FColor::Black;
	//only use for FourCorner
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(EditCondition="DirectionType==EDreamMeshModifierGradientColorDirection::FourCorner"))
		FColor Color4 = FColor::White;
	FORCEINLINE void ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor);
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)override
	{
		OutTriangleIndices = false;
		OutVertexPosition = false;
		OutUV = false;
		OutColor = true;
	};

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamMeshModifierGradientColorDirection GetDirectionType()const{return DirectionType;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetMultiplySourceAlpha()const{return bMultiplySourceAlpha;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FColor GetColor1()const{return Color1;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FColor GetColor2()const{return Color2;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FColor GetColor3()const{return Color3;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FColor GetColor4()const{return Color4;}
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetDirectionType(EDreamMeshModifierGradientColorDirection Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMultiplySourceAlpha(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetColor1(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetColor2(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetColor3(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetColor4(FColor Value);
};
