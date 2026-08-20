// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamSpriteBase.h"
#include "DreamSprite.generated.h"

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISpriteDrawType :uint8
{
	Normal,
	Sliced,
	SlicedFrame,
	Tiled,
	Filled,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISpriteFillMethod:uint8
{
	Horizontal,
	Vertical,
	Radial90,
	Radial180,
	Radial360,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISpriteFillOriginType_Radial90 :uint8 
{
	BottomLeft,
	TopLeft,
	TopRight,
	BottomRight,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISpriteFillOriginType_Radial180 :uint8
{
	Bottom,
	Left,
	Top,
	Right,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUISpriteFillOriginType_Radial360 :uint8
{
	Bottom,
	Right,
	Top,
	Left,
};

UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUI_API UDreamSprite : public UDreamSpriteBase
{
	GENERATED_BODY()

public:	
	UDreamSprite(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	friend class FDreamSpriteCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUISpriteDrawType DrawType = EDreamUISpriteDrawType::Normal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageBrush")
	float PixelsPerUnitMultiplier = 1;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUISpriteFillMethod FillMethod = EDreamUISpriteFillMethod::Horizontal;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		uint8 FillOrigin = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool FillDirectionFlip = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float FillAmount = 1;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial90 FillOriginType_Radial90;
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial180 FillOriginType_Radial180;
	UPROPERTY(Transient, EditAnywhere, Category = "DreamGUI")EDreamUISpriteFillOriginType_Radial360 FillOriginType_Radial360;
#endif

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	//width direction rectangle count, in tiled mode
	int32 Tiled_WidthRectCount = 0;
	//height direction rectangle count, in tiled mode
	int32 Tiled_HeightRectCount = 0;
	//width direction half rectangle size, in tiled mode
	float Tiled_WidthRemainedRectSize = 0;
	//height direction half rectangle size, in tiled mode
	float Tiled_HeightRemainedRectSize = 0;
	void CalculateTiledWidth();
	void CalculateTiledHeight();

	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUISpriteDrawType GetSpriteDrawType()const { return DrawType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetPixelsPerUnitMultiplier() const { return PixelsPerUnitMultiplier; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	EDreamUISpriteFillMethod GetFillMethod()const { return FillMethod; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	uint8 GetFillOrigin()const { return FillOrigin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	bool GetFillDirectionFlip()const { return FillDirectionFlip; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	float GetFillAmount()const { return FillAmount; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetDrawType(EDreamUISpriteDrawType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPixelsPerUnitMultiplier(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillMethod(EDreamUISpriteFillMethod Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillOrigin(uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillDirectionFlip(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFillAmount(float Value);
};
