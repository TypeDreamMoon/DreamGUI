// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/DreamUISpriteData.h"
#include "DreamTextureBase.h"
#include "DreamSprite.h"
#include "DreamTexture.generated.h"

UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUI_API UDreamTexture : public UDreamTextureBase
{
	GENERATED_BODY()

public:	
	UDreamTexture(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	virtual void BeginPlay()override;
protected:
	friend class FDreamTextureCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUISpriteDrawType DrawType = EDreamUISpriteDrawType::Normal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	float PixelsPerUnitMultiplier = 1;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FDreamUISpriteInfo SpriteInfo;
	/** Texture UV offset and scale info. Only get good result when DrawType is Normal */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector4f UVRect = FVector4f(0, 0, 1, 1);

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUISpriteFillMethod FillMethod = EDreamUISpriteFillMethod::Horizontal;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		uint8 FillOrigin = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool FillDirectionFlip = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float FillAmount = 1;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial90 fillOriginType_Radial90;
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial180 fillOriginType_Radial180;
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial360 fillOriginType_Radial360;
#endif

	void CheckSpriteData();

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUISpriteDrawType GetDrawType()const { return DrawType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") FDreamUISpriteInfo GetSpriteInfo()const { return SpriteInfo; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") FVector4f GetUVRect()const { return UVRect; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetPixelsPerUnitMultiplier() const { return PixelsPerUnitMultiplier; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	EDreamUISpriteFillMethod GetFillMethod()const { return FillMethod; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	uint8 GetFillOrigin()const { return FillOrigin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	bool GetFillDirectionFlip()const { return FillDirectionFlip; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	float GetFillAmount()const { return FillAmount; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetDrawType(EDreamUISpriteDrawType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetSpriteInfo(FDreamUISpriteInfo Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetUVRect(FVector4f Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPixelsPerUnitMultiplier(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillMethod(EDreamUISpriteFillMethod Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillOrigin(uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillDirectionFlip(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillAmount(float Value);

	virtual void SetTexture(UTexture* Value)override;
};
