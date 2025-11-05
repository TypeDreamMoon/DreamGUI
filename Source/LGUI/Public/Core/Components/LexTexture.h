// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/LexUISpriteData.h"
#include "LexTextureBase.h"
#include "LexSprite.h"
#include "LexTexture.generated.h"

UENUM(BlueprintType, Category = LGUI)
enum class ELexUITextureUVRectControlMode :uint8
{
	None,
	KeepAspectRatio_FitIn,
	KeepAspectRatio_Envelope,
};
UCLASS(ClassGroup = (LGUI), NotBlueprintable)
class LGUI_API ULexTexture : public ULexTextureBase
{
	GENERATED_BODY()

public:	
	ULexTexture(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	virtual void BeginPlay()override;
protected:
	friend class FLexTextureCustomization;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUISpriteDrawType DrawType = ELexUISpriteDrawType::Normal;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FLexUISpriteInfo SpriteInfo;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUITextureUVRectControlMode UVRectControlMode = ELexUITextureUVRectControlMode::None;
	/** Texture UV offset and scale info. Only get good result when DrawType is Normal */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta=(EditCondition="UVRectControlMode==ELexUITextureUVRectControlMode::None"))
		FVector4f UVRect = FVector4f(0, 0, 1, 1);

	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexUISpriteFillMethod FillMethod = ELexUISpriteFillMethod::Horizontal;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		uint8 FillOrigin = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool FillDirectionFlip = false;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float FillAmount = 1;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, EditAnywhere, Category = "LGUI")ELexUISpriteFillOriginType_Radial90 fillOriginType_Radial90;
	UPROPERTY(Transient, EditAnywhere, Category = "LGUI")ELexUISpriteFillOriginType_Radial180 fillOriginType_Radial180;
	UPROPERTY(Transient, EditAnywhere, Category = "LGUI")ELexUISpriteFillOriginType_Radial360 fillOriginType_Radial360;
#endif

	void CheckSpriteData();
	void ApplyUVRect();

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	virtual void OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI") ELexUISpriteDrawType GetDrawType()const { return DrawType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") FLexUISpriteInfo GetSpriteInfo()const { return SpriteInfo; }
	UFUNCTION(BlueprintCallable, Category = "LGUI") FVector4f GetUVRect()const { return UVRect; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	ELexUISpriteFillMethod GetFillMethod()const { return FillMethod; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	uint8 GetFillOrigin()const { return FillOrigin; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	bool GetFillDirectionFlip()const { return FillDirectionFlip; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	float GetFillAmount()const { return FillAmount; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")	ELexUITextureUVRectControlMode GetUVRectControlMode()const { return UVRectControlMode; }

	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetDrawType(ELexUISpriteDrawType Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetSpriteInfo(FLexUISpriteInfo Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetUVRect(FVector4f Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetFillMethod(ELexUISpriteFillMethod Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetFillOrigin(uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetFillDirectionFlip(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetFillAmount(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI") void SetUVRectControlMode(ELexUITextureUVRectControlMode Value);

	virtual void SetTexture(UTexture* Value)override;
};
