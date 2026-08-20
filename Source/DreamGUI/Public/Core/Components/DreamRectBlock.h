// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "Core/DreamUIDataAsTexture.h"
#include "DreamRectBlock.generated.h"


UCLASS(ClassGroup = (DreamGUI), BlueprintType)
class DREAMGUI_API UDreamRectBlockData :public UDreamUIDataAsTexture
{
	GENERATED_BODY()
private:

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> DefaultMaterial;
protected:
	virtual void PostInitProperties()override;
public:
	UMaterialInterface* GetMaterial();
};

UENUM(BlueprintType)
enum class EDreamRectBlockTextureScaleMode: uint8
{
	Stretch,
	FitIn,
	Envelop,
};
UENUM(BlueprintType)
enum class EDreamRectBlockUnitMode : uint8
{
	/** Direct value */
	Value			UMETA(DisplayName="V"),
	/** Percent with rect size from 0 to 100 */
	Percentage		UMETA(DisplayName="%"),
};
UENUM(BlueprintType)
enum class EDreamRectBlockTextureMode : uint8
{
	Texture,
	Sprite,
};

class UDreamUISpriteData_BaseObject;

/**
 * UV channel-
 *		UV0: full 0~1 UV coordinate for calculate sdf
 *		UV1: Default DreamCanvas use, check DreamCanvas
 *		UV2: Body texture's coordinate
 *		UV3: X- for RectBlock data coordinate
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamRectBlock : public UDreamVisualBatchMesh
	, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()

public:
	UDreamRectBlock(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
protected:
	virtual void OnPreChangeSpriteProperty();
	virtual void OnPostChangeSpriteProperty();
#endif
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay()override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;
protected:
	friend class FDreamRectBlockCustomization;

#pragma region BlockData
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector4f CornerRadius = FVector4f(0.1f, 0.1f, 0.1f, 0.1f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode CornerRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	/** Prevent edge aliasing, useful when in 3d. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", AdvancedDisplay)
		bool bSoftEdge = true;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBody = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BodyColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockTextureMode BodyTextureMode = EDreamRectBlockTextureMode::Sprite;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DisplayThumbnail = "false"))
		TObjectPtr<class UTexture> BodyTexture = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UDreamUISpriteData_BaseObject> BodySpriteTexture = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "BodyTexture"))
		EDreamRectBlockTextureScaleMode BodyTextureScaleMode = EDreamRectBlockTextureScaleMode::Stretch;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBodyGradient = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BodyGradientColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f BodyGradientCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode BodyGradientCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f BodyGradientRadius = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode BodyGradientRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float BodyGradientRotation = 0;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBorder = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float BorderWidth = 2;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode BorderWidthUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BorderColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBorderGradient = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BorderGradientColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f BorderGradientCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode BorderGradientCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f BorderGradientRadius = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode BorderGradientRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float BorderGradientRotation = 0;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableInnerShadow = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor InnerShadowColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode InnerShadowSizeUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowBlur = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode InnerShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float InnerShadowAngle = 45;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowDistance = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode InnerShadowDistanceUnitMode = EDreamRectBlockUnitMode::Value;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableRadialFill = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f RadialFillCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode RadialFillCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float RadialFillRotation = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float RadialFillAngle = 270;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableOuterShadow = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor OuterShadowColor = FColor(0, 0, 0, 128);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float OuterShadowSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode OuterShadowSizeUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0"))
		float OuterShadowBlur = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode OuterShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float OuterShadowAngle = 45;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float OuterShadowDistance = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockUnitMode OuterShadowDistanceUnitMode = EDreamRectBlockUnitMode::Value;

	void FillData(uint8* Data, float width, float height);
	float GetValueWithUnitMode(float SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const;
	FVector4f GetValueWithUnitMode(const FVector4f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const;
	FVector2f GetValueWithUnitMode(const FVector2f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight)const;
	FVector2f GetInnerShadowOffset(float RectWidth, float RectHeight);
	FVector2f GetOuterShadowOffset(float RectWidth, float RectHeight);
	static constexpr int DataCountInBytes();

	void FillColorToData(uint8* Data, const FColor& InValue, int& InOutDataOffset);
	uint8 PackBoolToByte(
		bool v0
		, bool v1
		, bool v2
		, bool v3
		, bool v4
		, bool v5
		, bool v6
		, bool v7
	);
	void Fill8BytesToData(uint8* Data, uint8 InValue0, uint8 InValue1, uint8 InValue2, uint8 InValue3, int& InOutDataOffset);
	void FillFloatToData(uint8* Data, const float& InValue, int& InOutDataOffset);
	void FillVector2ToData(uint8* Data, const FVector2f& InValue, int& InOutDataOffset);
	void FillVector4ToData(uint8* Data, const FVector4f& InValue, int& InOutDataOffset);


#define OnFloatUnitModeChanged(Property, AdditionalScale)\
	void On##Property##UnitModeChanged(float width, float height)\
	{\
		if (Property##UnitMode == EDreamRectBlockUnitMode::Value)\
		{\
			Property = Property * (width < height ? width : height) * AdditionalScale;\
		}\
		else\
		{\
			Property = Property / (width < height ? width : height) / AdditionalScale;\
		}\
	}

#define OnVector2UnitModeChanged(Property)\
	void On##Property##UnitModeChanged(float width, float height)\
	{\
		if (Property##UnitMode == EDreamRectBlockUnitMode::Value)\
		{\
			Property.X = Property.X * width;\
			Property.Y = Property.Y * height;\
		}\
		else\
		{\
			Property.X = Property.X / width;\
			Property.Y = Property.Y / height;\
		}\
	}

	void OnCornerRadiusUnitModeChanged(float width, float height);
	OnVector2UnitModeChanged(BodyGradientCenter);
	OnVector2UnitModeChanged(BodyGradientRadius);

	OnFloatUnitModeChanged(BorderWidth, 0.5f);
	OnVector2UnitModeChanged(BorderGradientCenter);
	OnVector2UnitModeChanged(BorderGradientRadius);

	OnFloatUnitModeChanged(InnerShadowSize, 0.5f);
	OnFloatUnitModeChanged(InnerShadowBlur, 1.0f);
	OnFloatUnitModeChanged(InnerShadowDistance, 0.5f);

	OnVector2UnitModeChanged(RadialFillCenter);

	OnFloatUnitModeChanged(OuterShadowSize, 0.5f);
	OnFloatUnitModeChanged(OuterShadowBlur, 1.0f);
	OnFloatUnitModeChanged(OuterShadowDistance, 0.5f);

#pragma endregion

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUniformSetCornerRadius = true;
#endif

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
		TObjectPtr<class UDreamRectBlockData> RectBlockData = nullptr;
	/** When do raycast interaction, will the CornerRadius be considered? Only support RaycastType.Rect. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Raycast")
		bool bRaycastSupportCornerRadius = true;

	int DataStartPosition = 0;
	static FName DataTextureParameterName;

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;
	virtual void OnMaterialInstanceDynamicCreated(class UMaterialInstanceDynamic* mat) override;

	//virtual void OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange, bool InDiscardCache = true)override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void MarkAllDirty()override;
	virtual bool GetAnythingDirty() const override;

	void MarkNeedUpdateBlockData();
	void OnDataTextureChanged(class UTexture* Texture);
	FDelegateHandle OnDataTextureChangedDelegateHandle;
	uint8 bNeedUpdateBlockData : 1;
	uint8 bHasAddToSprite : 1;
protected:
	bool LineTraceUI_CheckCornerRadius(const FVector2D& InLocalHitPoint)const;
	virtual bool LineTraceUIRect(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
public:
#pragma region IDreamUISpriteRenderInterface
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override { return BodySpriteTexture; }
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector4f& GetCornerRadius()const { return CornerRadius; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetCornerRadiusUnitMode()const { return CornerRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBody()const { return bEnableBody; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBodyColor()const { return BodyColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UTexture* GetBodyTexture()const { return BodyTexture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamUISpriteData_BaseObject* GetBodySpriteTexture()const { return BodySpriteTexture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockTextureMode GetBodyTextureMode()const { return BodyTextureMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockTextureScaleMode GetBodyTextureScaleMode()const { return BodyTextureScaleMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetSoftEdge()const { return bSoftEdge; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBodyGradient()const { return bEnableBodyGradient; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBodyGradientColor()const { return BodyGradientColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetBodyGradientCenter()const { return BodyGradientCenter; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBodyGradientCenterUnitMode()const { return BodyGradientCenterUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetBodyGradientRadius()const { return BodyGradientRadius; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBodyGradientRadiusUnitMode()const { return BodyGradientRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBodyGradientRotation()const { return BodyGradientRotation; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBorder()const { return bEnableBorder; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBorderWidth()const { return BorderWidth; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderWidthUnitMode()const { return BorderWidthUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBorderColor()const { return BorderColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBorderGradient()const { return bEnableBorderGradient; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBorderGradientColor()const { return BorderGradientColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetBorderGradientCenter()const { return BorderGradientCenter; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderGradientCenterUnitMode()const { return CornerRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetBorderGradientRadius()const { return BorderGradientRadius; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderGradientRadiusUnitMode()const { return BorderGradientRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBorderGradientRotation()const { return BorderGradientRotation; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableInnerShadow()const { return bEnableInnerShadow; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetInnerShadowColor()const { return InnerShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowSize()const { return InnerShadowSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowSizeUnitMode()const { return InnerShadowSizeUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowBlur()const { return InnerShadowBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowBlurUnitMode()const { return InnerShadowBlurUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowAngle()const { return InnerShadowAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowDistance()const { return InnerShadowDistance; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowDistanceUnitMode()const { return InnerShadowDistanceUnitMode; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableRadialFill()const { return bEnableRadialFill; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetRadialFillCenter()const { return RadialFillCenter; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetRadialFillCenterUnitMode()const { return RadialFillCenterUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetRadialFillRotation()const { return RadialFillRotation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetRadialFillAngle()const { return RadialFillAngle; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableOuterShadow()const { return bEnableOuterShadow; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetOuterShadowColor()const { return OuterShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowSize()const { return OuterShadowSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowSizeUnitMode()const { return OuterShadowSizeUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowBlur()const { return OuterShadowBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowBlurUnitMode()const { return OuterShadowBlurUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowAngle()const { return OuterShadowAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowDistance()const { return OuterShadowDistance; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowDistanceUnitMode()const { return OuterShadowDistanceUnitMode; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetRaycastSupportCornerRadius()const { return bRaycastSupportCornerRadius; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCornerRadius(const FVector4& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCornerRadiusUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBody(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTexture(UTexture* value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodySpriteTexture(UDreamUISpriteData_BaseObject* value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTextureMode(EDreamRectBlockTextureMode value);
	/** Set size from current body texture or Sprite */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSizeFromBodyTexture();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTextureScaleMode(EDreamRectBlockTextureScaleMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSoftEdge(bool value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBodyGradient(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientCenter(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientCenterUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientRadius(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientRadiusUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyGradientRotation(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBorder(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderWidth(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderWidthUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBorderGradient(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientCenter(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientCenterUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientRadius(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientRadiusUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderGradientRotation(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableInnerShadow(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowSize(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowSizeUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowBlur(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowBlurUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowDistance(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowDistanceUnitMode(EDreamRectBlockUnitMode value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableRadialFill(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillCenter(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillCenterUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillRotation(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillAngle(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableOuterShadow(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowSize(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowSizeUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowBlur(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowBlurUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowDistance(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowDistanceUnitMode(EDreamRectBlockUnitMode value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRaycastSupportCornerRadius(bool value);

#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* CornerRadiusTo(FVector4 endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientRadiusTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderWidthTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientRadiusTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowSizeTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowBlurTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowDistanceTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowSizeTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowBlurTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowDistanceTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion
};
